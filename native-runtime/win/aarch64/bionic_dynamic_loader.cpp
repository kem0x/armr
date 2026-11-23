#include "bionic_dynamic_loader.h"

#include "bionic_thread_state.h"
#include "elf64_modules.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "import_registry.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace isomr {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

}  // namespace

bool BionicDynamicLoader::RegisterImports(Aarch64ImportRegistry* imports,
                                          std::string* error) {
    if (heap_ == nullptr || address_space_ == nullptr || threads_ == nullptr ||
        imports == nullptr || imports_ != nullptr) {
        SetError(error, "bionic dynamic-loader dependencies are invalid");
        return false;
    }
    imports_ = imports;
    if (!imports->Register(
            "dlopen",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* handler_error) {
                return Dlopen((*registers)[0], (*registers)[1],
                              &(*registers)[0], handler_error);
            },
            error) ||
        !imports->Register(
            "dlsym",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* handler_error) {
                return Dlsym((*registers)[0], (*registers)[1],
                             &(*registers)[0], handler_error);
            },
            error) ||
        !imports->Register(
            "dlclose",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* handler_error) {
                return Dlclose((*registers)[0], &(*registers)[0],
                               handler_error);
            },
            error) ||
        !imports->Register(
            "dl_iterate_phdr",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* handler_error) {
                return DlIteratePhdr((*registers)[0], (*registers)[1],
                                     &(*registers)[0], handler_error);
            },
            error) ||
        !imports->Register(
            "dladdr",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* handler_error) {
                return Dladdr((*registers)[0], (*registers)[1],
                              &(*registers)[0], handler_error);
            },
            error))
        return false;
    return imports->Register(
        "dlerror",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* handler_error) {
            return Dlerror(&(*registers)[0], handler_error);
        },
        error);
}

bool BionicDynamicLoader::AttachModules(Elf64ModuleSet* modules,
                                        std::string* error) {
    const std::lock_guard lock(mutex_);
    if (shutdown_ || modules == nullptr || modules_ != nullptr) {
        SetError(error,
                 "dynamic loader module set is null, shut down, or attached");
        return false;
    }
    const std::vector<Elf64LoadedModule> loaded =
        modules->LoadedModulesRootFirst();
    if (loaded.empty()) {
        SetError(error, "dynamic loader has no guest modules to stage");
        return false;
    }
    if (!StageIterationModulesLocked(loaded, error)) return false;
    modules_ = modules;
    for (const Elf64LoadedModule& module : loaded)
        initialized_modules_.insert(module.image);
    return true;
}

bool BionicDynamicLoader::StageIterationModulesLocked(
    const std::vector<Elf64LoadedModule>& modules, std::string* error) {
    std::vector<IterationModule> staged;
    staged.reserve(modules.size());
    const auto rollback = [this, &staged]() {
        std::string ignored;
        for (auto module = staged.rbegin(); module != staged.rend(); ++module) {
            (void)heap_->Release(module->program_headers, &ignored);
            (void)heap_->Release(module->name, &ignored);
        }
        staged.clear();
    };
    std::size_t metadata_size = 0;
    for (const Elf64LoadedModule& module : modules) {
        if (module.image == nullptr || module.name.empty() ||
            module.image->program_headers().empty() ||
            module.image->program_headers().size() >
                std::numeric_limits<std::uint16_t>::max()) {
            rollback();
            SetError(error, "dynamic-loader iteration metadata is invalid");
            return false;
        }
        const std::size_t name_size = module.name.size() + 1;
        const std::size_t header_size =
            module.image->program_headers().size() *
            sizeof(Elf64ProgramHeader);
        const std::size_t name_allocation_size =
            (name_size + 15U) & ~std::size_t{15U};
        const std::size_t header_allocation_size =
            (header_size + 15U) & ~std::size_t{15U};
        if (name_allocation_size > kMaximumIterationMetadata ||
            header_allocation_size >
                kMaximumIterationMetadata - name_allocation_size ||
            iteration_metadata_size_ >
                kMaximumIterationMetadata - name_allocation_size -
                    header_allocation_size ||
            metadata_size >
                kMaximumIterationMetadata - name_allocation_size -
                    header_allocation_size - iteration_metadata_size_) {
            rollback();
            SetError(error,
                     "dynamic-loader iteration metadata exceeds its limit");
            return false;
        }
        metadata_size += name_allocation_size + header_allocation_size;
        IterationModule record;
        record.image = module.image;
        record.load_bias = module.image->load_bias();
        record.program_header_count = static_cast<std::uint16_t>(
            module.image->program_headers().size());
        heap_->SetAllocDebugTag("dl-module-name");
        record.name = heap_->Allocate(name_size);
        heap_->SetAllocDebugTag(nullptr);
        staged.push_back(record);
        if (record.name == 0 ||
            !heap_->Write(record.name, module.name.c_str(), name_size) ||
            !heap_->SetWritable(record.name, name_allocation_size, false,
                                error)) {
            rollback();
            if (error != nullptr && error->empty())
                *error = "dynamic-loader module-name staging failed";
            return false;
        }
        heap_->SetAllocDebugTag("dl-phdrs");
        record.program_headers = heap_->AllocateAligned(
            header_size, alignof(Elf64ProgramHeader));
        heap_->SetAllocDebugTag(nullptr);
        staged.back().program_headers = record.program_headers;
        if (record.program_headers == 0 ||
            !heap_->Write(record.program_headers,
                          module.image->program_headers().data(),
                          header_size) ||
            !heap_->SetWritable(record.program_headers,
                                header_allocation_size, false, error)) {
            rollback();
            if (error != nullptr && error->empty())
                *error = "dynamic-loader program-header staging failed";
            return false;
        }
    }
    iteration_modules_.insert(iteration_modules_.end(), staged.begin(),
                              staged.end());
    iteration_metadata_size_ += metadata_size;
    module_add_count_ += static_cast<std::uint64_t>(modules.size());
    return true;
}

bool BionicDynamicLoader::RunModuleInitializersLocked(
    std::unique_lock<std::recursive_mutex>* loader_lock,
    const Elf64Image* module, std::string* error) {
    if (loader_lock == nullptr || !loader_lock->owns_lock()) {
        SetError(error, "dynamic-loader initializer lock is unavailable");
        return false;
    }
    if (module == nullptr || failed_modules_.contains(module)) {
        const auto found = failed_module_errors_.find(module);
        SetError(error,
                 found != failed_module_errors_.end()
                     ? found->second
                     : "dynamic-loader module is invalid or previously failed");
        return false;
    }
    if (initialized_modules_.contains(module))
        return true;
    const std::optional<std::uint32_t> guest_thread =
        threads_->CurrentGuestThreadId();
    if (!guest_thread) {
        SetError(error, "dynamic-loader initializer caller is not attached");
        return false;
    }
    if (initializing_modules_.contains(module)) {
        const auto owner = initializing_module_owners_.find(module);
        if (owner != initializing_module_owners_.end() &&
            owner->second == *guest_thread)
            return true;
        SetError(error,
                 "dynamic-loader module initialization is active on another thread");
        return false;
    }
    const std::optional<std::uint64_t> thread_handle =
        threads_->CurrentErrnoAddress();
    BionicThreads::GuestCall executor =
        thread_handle && executor_factory_
            ? executor_factory_(*thread_handle, error)
            : BionicThreads::GuestCall{};
    if (!executor) {
        failed_modules_.insert(module);
        if (error != nullptr && error->empty())
            *error = "dynamic-loader initializer executor is unavailable";
        failed_module_errors_[module] =
            error != nullptr ? *error
                             : "dynamic-loader initializer executor is unavailable";
        return false;
    }
    initializing_modules_.insert(module);
    initializing_module_owners_[module] = *guest_thread;
    for (std::size_t index = 0; index < module->initializers().size();
         ++index) {
        const std::array<std::uint64_t, 8> arguments{};
        std::uint64_t ignored = 0;
        loader_lock->unlock();
        const bool executed = executor(module->initializers()[index],
                                       arguments, &ignored, error);
        loader_lock->lock();
        if (shutdown_) {
            initializing_modules_.erase(module);
            initializing_module_owners_.erase(module);
            SetError(error,
                     "dynamic loader shut down during module initialization");
            return false;
        }
        if (!executed) {
            initializing_modules_.erase(module);
            initializing_module_owners_.erase(module);
            failed_modules_.insert(module);
            if (error != nullptr)
                *error = "dynamic-loader initializer " +
                         std::to_string(index) + " failed: " + *error;
            failed_module_errors_[module] =
                error != nullptr ? *error
                                 : "dynamic-loader initializer failed";
            return false;
        }
    }
    initializing_modules_.erase(module);
    initializing_module_owners_.erase(module);
    initialized_modules_.insert(module);
    return true;
}

bool BionicDynamicLoader::ReadGuestString(std::uint64_t address,
                                          std::string* result,
                                          std::string* error) const {
    if (address == 0 || result == nullptr) {
        SetError(error, "dynamic-loader guest string pointer is invalid");
        return false;
    }
    std::uint64_t length = 0;
    if (!address_space_->BoundedStringLength(address, kMaximumName + 1,
                                             &length, error) ||
        length > kMaximumName) {
        SetError(error, "dynamic-loader guest string is invalid or too long");
        return false;
    }
    std::vector<char> bytes(static_cast<std::size_t>(length));
    if (length != 0 &&
        !address_space_->Read(address, bytes.data(), bytes.size())) {
        SetError(error, "dynamic-loader guest string is unreadable");
        return false;
    }
    result->assign(bytes.begin(), bytes.end());
    return true;
}

bool BionicDynamicLoader::SetPendingErrorLocked(std::string message,
                                                std::string* error) {
    const std::optional<std::uint32_t> thread =
        threads_->CurrentGuestThreadId();
    if (!thread) {
        SetError(error, "dynamic-loader caller is not attached");
        return false;
    }
    if (message.size() >= kErrorCapacity)
        message.resize(kErrorCapacity - 1);
    ThreadError& state = thread_errors_[*thread];
    if (state.address == 0) {
        heap_->SetAllocDebugTag("dl-error");
        state.address = heap_->Callocate(1, kErrorCapacity);
        heap_->SetAllocDebugTag(nullptr);
        if (state.address == 0) {
            thread_errors_.erase(*thread);
            SetError(error, "dynamic-loader error buffer allocation failed");
            return false;
        }
    }
    std::vector<char> buffer(kErrorCapacity, '\0');
    std::copy(message.begin(), message.end(), buffer.begin());
    if (!heap_->Write(state.address, buffer.data(), buffer.size())) {
        SetError(error, "dynamic-loader error buffer is unwritable");
        return false;
    }
    state.pending = true;
    return true;
}

bool BionicDynamicLoader::OpenHandleLocked(HandleKind kind,
                                           const Elf64Image* module,
                                           std::string name,
                                           std::uint64_t* result,
                                           std::string* error) {
    const std::string scope =
        std::to_string(static_cast<unsigned>(kind)) + ":" + name;
    const auto existing = handles_by_scope_.find(scope);
    if (existing != handles_by_scope_.end()) {
        Handle& handle = handles_.at(existing->second);
        if (handle.references == std::numeric_limits<std::size_t>::max()) {
            *result = 0;
            return SetPendingErrorLocked("dlopen reference count overflow",
                                         error);
        }
        ++handle.references;
        *result = existing->second;
        return true;
    }
    const std::uint64_t address = heap_->Callocate(1, 16);
    if (address == 0) {
        *result = 0;
        return SetPendingErrorLocked("dlopen handle allocation failed", error);
    }
    handles_.emplace(address, Handle{kind, module, std::move(name), 1});
    handles_by_scope_.emplace(scope, address);
    *result = address;
    return true;
}

bool BionicDynamicLoader::Dlopen(std::uint64_t filename,
                                 std::uint64_t flags,
                                 std::uint64_t* result,
                                 std::string* error) {
    if (result == nullptr) {
        SetError(error, "dlopen result pointer is null");
        return false;
    }
    std::unique_lock lock(mutex_);
    *result = 0;
    if (shutdown_ || modules_ == nullptr || imports_ == nullptr ||
        !imports_->sealed()) {
        return SetPendingErrorLocked("dlopen runtime is unavailable", error);
    }
    if (flags != kRtldLazy && flags != kRtldNow)
        return SetPendingErrorLocked("dlopen flags are unsupported", error);
    if (filename == 0)
        return OpenHandleLocked(HandleKind::Global, nullptr, {}, result,
                                error);

    std::string name;
    std::string read_error;
    if (!ReadGuestString(filename, &name, &read_error))
        return SetPendingErrorLocked("dlopen filename is invalid", error);
    std::fprintf(stderr, "ISOMR_DLOPEN name=%s flags=0x%llx\n", name.c_str(),
                 static_cast<unsigned long long>(flags));
    std::fflush(stderr);
    std::string module_name = name;
    const std::size_t separator = name.find_last_of('/');
    if (separator != std::string::npos) {
        if (name.find('\\') != std::string::npos ||
            name.find(':') != std::string::npos ||
            separator + 1 == name.size())
            return SetPendingErrorLocked("dlopen filename path is invalid",
                                         error);
        std::size_t segment_start = 0;
        while (segment_start < name.size()) {
            const std::size_t segment_end = name.find('/', segment_start);
            const std::string_view segment(
                name.data() + segment_start,
                (segment_end == std::string::npos ? name.size()
                                                  : segment_end) -
                    segment_start);
            if (segment == "." || segment == "..")
                return SetPendingErrorLocked(
                    "dlopen filename path is invalid", error);
            if (segment_end == std::string::npos) break;
            segment_start = segment_end + 1;
        }
        module_name = name.substr(separator + 1);
    }
    const Elf64Image* module = modules_->FindModuleByBasename(module_name);
    if (module == nullptr && separator == std::string::npos &&
        modules_->IsHostDependencyAllowed(name))
        return OpenHandleLocked(HandleKind::HostDependency, nullptr, name,
                                result, error);
    if (module == nullptr) {
        if (!allowed_sibling_libraries_.contains(module_name))
            return SetPendingErrorLocked(
                "dlopen guest module is not configured", error);
        const Elf64DynamicLoadResult loaded =
            modules_->LoadSibling(module_name);
        if (loaded.module == nullptr)
            return SetPendingErrorLocked(
                "dlopen guest module load failed: " + loaded.error, error);
        if (!StageIterationModulesLocked(loaded.new_modules, &read_error)) {
            std::string rollback_error;
            if (!modules_->RollbackDynamicLoad(loaded.new_modules,
                                               &rollback_error))
                read_error += "; rollback failed: " + rollback_error;
            return SetPendingErrorLocked(
                "dlopen guest module metadata failed: " + read_error,
                error);
        }
        if (!modules_->CommitDynamicLoad(loaded.new_modules,
                                         &read_error))
            return SetPendingErrorLocked(
                "dlopen guest module commit failed: " + read_error,
                error);
        for (const Elf64LoadedModule& added : loaded.new_modules) {
            if (!RunModuleInitializersLocked(&lock, added.image,
                                             &read_error))
                return SetPendingErrorLocked(
                    "dlopen guest module initialization failed: " +
                        read_error,
                    error);
        }
        module = loaded.module;
    }
    if (module != nullptr) {
        if (failed_modules_.contains(module)) {
            const auto failed = failed_module_errors_.find(module);
            return SetPendingErrorLocked(
                failed != failed_module_errors_.end()
                    ? "dlopen guest module previously failed: " +
                          failed->second
                    : "dlopen guest module previously failed",
                error);
        }
        const auto owner = initializing_module_owners_.find(module);
        if (owner != initializing_module_owners_.end()) {
            const std::optional<std::uint32_t> guest_thread =
                threads_->CurrentGuestThreadId();
            if (!guest_thread || owner->second != *guest_thread)
                return SetPendingErrorLocked(
                    "dlopen guest module is initializing on another thread",
                    error);
        }
        return OpenHandleLocked(HandleKind::Module, module, module_name,
                                result, error);
    }
    return SetPendingErrorLocked("dlopen target is unavailable", error);
}

bool BionicDynamicLoader::Dlsym(std::uint64_t handle,
                                std::uint64_t symbol,
                                std::uint64_t* result,
                                std::string* error) {
    if (result == nullptr) {
        SetError(error, "dlsym result pointer is null");
        return false;
    }
    std::string name;
    std::string read_error;
    if (!ReadGuestString(symbol, &name, &read_error)) {
        const std::lock_guard lock(mutex_);
        *result = 0;
        return SetPendingErrorLocked("dlsym symbol name is invalid", error);
    }
    const std::lock_guard lock(mutex_);
    *result = 0;
    const auto found = handles_.find(handle);
    if (shutdown_ || modules_ == nullptr || imports_ == nullptr ||
        !imports_->sealed() || found == handles_.end())
        return SetPendingErrorLocked("dlsym handle is not owned", error);

    std::optional<std::uint64_t> address;
    const Handle& selected = found->second;
    const std::optional<std::uint32_t> guest_thread =
        threads_->CurrentGuestThreadId();
    const auto module_visible =
        [&](const Elf64Image* module) {
            if (module == nullptr || failed_modules_.contains(module))
                return false;
            const auto owner = initializing_module_owners_.find(module);
            return owner == initializing_module_owners_.end() ||
                   (guest_thread && owner->second == *guest_thread);
        };
    if (selected.kind == HandleKind::Global) {
        for (const Elf64LoadedModule& module :
             modules_->LoadedModulesRootFirst()) {
            if (!module_visible(module.image)) continue;
            address = module.image->FindExport(name);
            if (address) break;
        }
        if (!address) address = imports_->Resolve(name);
    } else if (selected.kind == HandleKind::Module) {
        if (!module_visible(selected.module))
            return SetPendingErrorLocked(
                "dlsym module is failed or initializing on another thread",
                error);
        address = selected.module->FindExport(name);
    } else {
        address = imports_->Resolve(name);
    }
    if (!address) {
        std::fprintf(stderr,
                     "ISOMR_DLSYM_MISS name=%s handle=0x%llx kind=%u\n",
                     name.c_str(), static_cast<unsigned long long>(handle),
                     static_cast<unsigned>(selected.kind));
        std::fflush(stderr);
        return SetPendingErrorLocked("dlsym symbol is unavailable in scope",
                                     error);
    }
    static const bool dlsym_trace = [] {
#pragma warning(suppress : 4996)
        const char* v = std::getenv("ISOMR_DLSYM_TRACE");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    if (dlsym_trace) {
        std::fprintf(stderr, "ISOMR_DLSYM_HIT name=%s addr=0x%llx\n",
                     name.c_str(),
                     static_cast<unsigned long long>(*address));
        std::fflush(stderr);
    }
    *result = *address;
    return true;
}

bool BionicDynamicLoader::Dlclose(std::uint64_t handle,
                                  std::uint64_t* result,
                                  std::string* error) {
    if (result == nullptr) {
        SetError(error, "dlclose result pointer is null");
        return false;
    }
    const std::lock_guard lock(mutex_);
    *result = std::numeric_limits<std::uint64_t>::max();
    const auto found = handles_.find(handle);
    if (shutdown_ || found == handles_.end())
        return SetPendingErrorLocked("dlclose handle is not owned", error);
    if (--found->second.references != 0) {
        *result = 0;
        return true;
    }
    const std::string scope =
        std::to_string(static_cast<unsigned>(found->second.kind)) + ":" +
        found->second.name;
    if (!heap_->Release(handle, error)) return false;
    handles_by_scope_.erase(scope);
    handles_.erase(found);
    *result = 0;
    return true;
}

bool BionicDynamicLoader::Dlerror(std::uint64_t* result,
                                  std::string* error) {
    if (result == nullptr) {
        SetError(error, "dlerror result pointer is null");
        return false;
    }
    const std::lock_guard lock(mutex_);
    const std::optional<std::uint32_t> thread =
        threads_->CurrentGuestThreadId();
    if (!thread) {
        SetError(error, "dlerror caller is not attached");
        return false;
    }
    const auto found = thread_errors_.find(*thread);
    if (found == thread_errors_.end() || !found->second.pending) {
        *result = 0;
        return true;
    }
    *result = found->second.address;
    found->second.pending = false;
    return true;
}

bool BionicDynamicLoader::DlIteratePhdr(std::uint64_t callback,
                                        std::uint64_t data,
                                        std::uint64_t* result,
                                        std::string* error) {
    if (callback == 0 || result == nullptr) {
        SetError(error, "dl_iterate_phdr arguments are invalid");
        return false;
    }
    std::vector<IterationModule> modules;
    std::uint64_t adds = 0;
    std::uint64_t subtracts = 0;
    {
        const std::lock_guard lock(mutex_);
        if (shutdown_ || modules_ == nullptr || imports_ == nullptr ||
            !imports_->sealed() || iteration_modules_.empty()) {
            SetError(error, "dl_iterate_phdr runtime is unavailable");
            return false;
        }
        const std::optional<std::uint32_t> guest_thread =
            threads_->CurrentGuestThreadId();
        for (const IterationModule& module : iteration_modules_) {
            if (failed_modules_.contains(module.image)) continue;
            const auto owner =
                initializing_module_owners_.find(module.image);
            if (owner != initializing_module_owners_.end() &&
                (!guest_thread || owner->second != *guest_thread))
                continue;
            modules.push_back(module);
        }
        adds = module_add_count_;
        subtracts = module_subtract_count_;
        ++active_iterations_;
    }
    const auto finish_iteration = [this]() {
        const std::lock_guard lock(mutex_);
        --active_iterations_;
    };

    const std::optional<std::uint64_t> thread_handle =
        threads_->CurrentErrnoAddress();
    BionicThreads::GuestCall executor =
        thread_handle && executor_factory_
            ? executor_factory_(*thread_handle, error)
            : BionicThreads::GuestCall{};
    if (!executor) {
        finish_iteration();
        if (error != nullptr && error->empty())
            *error = "dl_iterate_phdr guest callback executor is unavailable";
        return false;
    }
    const std::uint64_t info_address =
        heap_->Callocate(1, sizeof(BionicDlPhdrInfo));
    if (info_address == 0) {
        finish_iteration();
        SetError(error, "dl_iterate_phdr info allocation failed");
        return false;
    }
    const auto cleanup = [this, info_address, &finish_iteration,
                          error]() {
        const bool released = heap_->Release(info_address, error);
        finish_iteration();
        return released;
    };

    *result = 0;
    for (const IterationModule& module : modules) {
        const BionicDlPhdrInfo info{
            module.load_bias,
            module.name,
            module.program_headers,
            module.program_header_count,
            {},
            adds,
            subtracts,
            0,
            0,
        };
        if (!heap_->Write(info_address, &info, sizeof(info))) {
            SetError(error, "dl_iterate_phdr info staging failed");
            (void)cleanup();
            return false;
        }
        std::array<std::uint64_t, 8> arguments{};
        arguments[0] = info_address;
        arguments[1] = sizeof(BionicDlPhdrInfo);
        arguments[2] = data;
        std::uint64_t callback_result = 0;
        if (!executor(callback, arguments, &callback_result, error)) {
            if (error != nullptr && error->empty())
                *error = "dl_iterate_phdr guest callback failed";
            (void)cleanup();
            return false;
        }
        const std::int32_t callback_integer = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(callback_result));
        if (callback_integer != 0) {
            *result = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(callback_integer));
            return cleanup();
        }
    }
    return cleanup();
}

bool BionicDynamicLoader::Dladdr(std::uint64_t address,
                                 std::uint64_t info_address,
                                 std::uint64_t* result,
                                 std::string* error) {
    if (info_address == 0 || result == nullptr) {
        SetError(error, "dladdr info pointer is null");
        return false;
    }
    const std::lock_guard lock(mutex_);
    if (shutdown_ || modules_ == nullptr) {
        SetError(error, "dladdr runtime is unavailable");
        return false;
    }
    const IterationModule* found = nullptr;
    for (const IterationModule& module : iteration_modules_) {
        if (module.image == nullptr || failed_modules_.contains(module.image))
            continue;
        const std::uint64_t base = module.image->guest_base();
        const std::uint64_t end = base + module.image->mapped_size();
        if (address >= base && address < end) {
            found = &module;
            break;
        }
    }
    if (found == nullptr) {
        // Address outside every loaded module: dladdr reports failure.
        std::array<std::uint64_t, 4> empty{};
        (void)heap_->Write(info_address, empty.data(), empty.size());
        *result = 0;
        return true;
    }
    // Dl_info: { dli_fname, dli_fbase, dli_sname, dli_saddr }. The soname is
    // already staged in guest memory for dl_iterate_phdr; the symbol fields
    // stay null since we do not yet resolve address-to-nearest-export.
    const std::array<std::uint64_t, 4> info{
        found->name,
        found->load_bias,
        0,
        0,
    };
    if (!heap_->Write(info_address, info.data(), info.size())) {
        SetError(error, "dladdr info write failed");
        return false;
    }
    *result = 1;
    return true;
}

bool BionicDynamicLoader::DiscardHandlesForProcessExit(
    std::size_t* discarded, std::string* error) {
    const std::lock_guard lock(mutex_);
    if (discarded == nullptr || shutdown_) {
        SetError(error,
                 "bionic dynamic loader process-exit arguments are invalid");
        return false;
    }
    *discarded = 0;
    if (!initializing_modules_.empty() || active_iterations_ != 0) {
        SetError(error,
                 "bionic dynamic loader has active module work at process exit");
        return false;
    }
    while (!handles_.empty()) {
        const auto found = handles_.begin();
        const std::uint64_t address = found->first;
        const std::string scope =
            std::to_string(static_cast<unsigned>(found->second.kind)) + ":" +
            found->second.name;
        if (!heap_->Release(address, error)) return false;
        handles_by_scope_.erase(scope);
        handles_.erase(found);
        ++*discarded;
    }
    return true;
}

bool BionicDynamicLoader::Shutdown(std::string* error) {
    const std::lock_guard lock(mutex_);
    if (shutdown_) {
        SetError(error, "bionic dynamic loader is already shut down");
        return false;
    }
    if (!initializing_modules_.empty()) {
        SetError(error,
                 "bionic dynamic loader has active module initializers");
        return false;
    }
    if (!handles_.empty() || active_iterations_ != 0) {
        SetError(error, "bionic dynamic loader has open handles");
        return false;
    }
    for (const auto& entry : thread_errors_) {
        if (!heap_->Release(entry.second.address, error)) return false;
    }
    thread_errors_.clear();
    handles_by_scope_.clear();
    for (const IterationModule& module : iteration_modules_) {
        if (!heap_->Release(module.program_headers, error) ||
            !heap_->Release(module.name, error))
            return false;
    }
    iteration_modules_.clear();
    initialized_modules_.clear();
    initializing_modules_.clear();
    initializing_module_owners_.clear();
    failed_modules_.clear();
    failed_module_errors_.clear();
    iteration_metadata_size_ = 0;
    module_add_count_ = 0;
    module_subtract_count_ = 0;
    modules_ = nullptr;
    shutdown_ = true;
    return true;
}

std::size_t BionicDynamicLoader::handle_count() const {
    const std::lock_guard lock(mutex_);
    return handles_.size();
}

}  // namespace isomr
