#include "bionic_runtime.h"

#include "bionic_memory.h"
#include "bionic_memory_ops.h"
#include "bionic_format.h"
#include "bionic_math.h"
#include "bionic_numbers.h"
#include "bionic_process.h"
#include "bionic_properties.h"
#include "bionic_stdio.h"

#include <cstdio>
#include <utility>

namespace isomr {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

}  // namespace

BionicRuntime::BionicRuntime(
    std::uint64_t heap_base, std::size_t heap_capacity,
    std::uint64_t import_base,
    BionicThreads::GuestExecutorFactory executor_factory,
    std::vector<BionicFileMount> file_mounts,
    std::string current_directory,
    std::size_t import_capacity,
    GuestJniBackend* jni_backend,
    std::size_t heap_watch_allocation_size,
    std::size_t heap_watch_range_offset,
    std::size_t heap_watch_range_size,
    bool import_journal_enabled,
    std::unordered_set<std::string> dlopen_sibling_libraries,
    BionicNanosleepBackend nanosleep_backend,
    BionicWaitableTimerApi waitable_timer_api,
    bool nanosleep_callsite_telemetry_enabled,
    NativeOpenSlMode opensl_mode)
    : heap_(heap_base, heap_capacity, heap_watch_allocation_size,
            heap_watch_range_offset, heap_watch_range_size),
      address_space_(&heap_),
      thread_state_(&heap_),
      process_identity_(&heap_),
      time_(&heap_, &address_space_, &thread_state_, {}, nanosleep_backend,
            std::move(waitable_timer_api),
            nanosleep_callsite_telemetry_enabled),
      files_(&heap_, &address_space_, &thread_state_, std::move(file_mounts),
             std::move(current_directory)),
      mappings_(&heap_, &thread_state_, &files_),
      stdio_(&heap_, &address_space_, &files_, &thread_state_),
      signals_(&address_space_, &thread_state_),
      dynamic_loader_(&heap_, &address_space_, &thread_state_,
                      executor_factory,
                      std::move(dlopen_sibling_libraries)),
      at_exit_(),
      locales_(&heap_, &address_space_, &thread_state_),
      random_(),
      sync_(&address_space_),
      tls_(&thread_state_, &address_space_),
      jni_(&heap_, &address_space_, &thread_state_, &files_, jni_backend),
      assets_(&heap_, &address_space_, &files_, &jni_),
      log_(&heap_, &address_space_),
      jumps_(&address_space_, &thread_state_),
      gles_(&heap_, &address_space_),
      imports_(import_base, import_capacity, import_journal_enabled),
      threads_(&heap_, &address_space_, &thread_state_, &tls_,
               std::move(executor_factory),
               [this](std::string* error) {
                   if (!time_.AttachCurrentThread(error)) return false;
                   if (jni_.AttachCurrentThread(error)) return true;
                   std::string ignored;
                   (void)time_.DetachCurrentThread(&ignored);
                   return false;
               },
               [this](std::string* error) {
                   if (!jni_.DetachCurrentThread(error)) return false;
                   if (time_.DetachCurrentThread(error)) return true;
                   std::string ignored;
                   (void)jni_.AttachCurrentThread(&ignored);
                   return false;
               }),
      opensl_(&heap_, &address_space_, opensl_mode, &threads_) {
    std::fprintf(stderr,
                 "ISOMR_BIONIC_RUNTIME_CTOR this=%p heap=%p space=%p "
                 "base=%llx\n",
                 static_cast<const void*>(this),
                 static_cast<const void*>(&heap_),
                 static_cast<const void*>(&address_space_),
                 static_cast<unsigned long long>(heap_base));
    std::fflush(stderr);
}

bool BionicRuntime::Initialize(std::string* error) {
    return Initialize({}, error);
}

bool BionicRuntime::Initialize(
    const std::vector<std::string>& fail_closed_imports,
    std::string* error) {
    if (initialization_attempted_) {
        SetError(error, initialized_ ? "bionic runtime is already initialized"
                                     : "bionic runtime initialization failed");
        return false;
    }
    initialization_attempted_ = true;
    if (!files_.configuration_valid()) {
        SetError(error, "bionic filesystem current directory is not mounted");
        return false;
    }
    if (!time_.ValidateConfiguration(error)) return false;
    if (!gles_.Initialize(error) ||
        !RegisterBionicMemoryImports(&imports_, &heap_, error) ||
        !RegisterBionicMemoryOperationImports(&imports_, &address_space_,
                                               error) ||
        !RegisterBionicMemoryOperationImports(&imports_, &address_space_,
                                              &heap_, error) ||
        !RegisterStringNCopies(&imports_, &address_space_, error) ||
        !mappings_.RegisterImports(&imports_, error) ||
        !RegisterBionicFormatImports(&imports_, &heap_, &address_space_,
                                     error) ||
        !log_.RegisterImports(&imports_, error) ||
        !RegisterBionicMathImports(&imports_, &address_space_, error) ||
        !RegisterBionicNumberImports(&imports_, &address_space_,
                                     &thread_state_, &locales_, error) ||
        !RegisterBionicFileImports(&imports_, &files_, error) ||
        !time_.RegisterImports(&imports_, error) ||
        !RegisterBionicProcessImports(&imports_, &address_space_, &files_,
                                      &thread_state_, &sync_,
                                      &process_identity_, &threads_,
                                      error) ||
        !RegisterBionicPropertyImports(&imports_, &address_space_, error) ||
        !stdio_.RegisterImports(&imports_, error) ||
        !signals_.RegisterImports(&imports_, error) ||
        !dynamic_loader_.RegisterImports(&imports_, error) ||
        !at_exit_.RegisterImports(&imports_, error) ||
        !locales_.RegisterImports(&imports_, error) ||
        !random_.RegisterImports(&imports_, error) ||
        !RegisterBionicErrnoImport(&imports_, &thread_state_, error) ||
        !RegisterBionicThreadIdentityImports(&imports_, &address_space_,
                                             &thread_state_, error) ||
        !RegisterBionicSyncImports(&imports_, &sync_, &thread_state_, error) ||
        !RegisterBionicTlsImports(&imports_, &tls_, error) ||
        !RegisterBionicThreadLifecycleImports(&imports_, &threads_, error) ||
        !jumps_.RegisterImports(&imports_, error) ||
        !gles_.RegisterImports(&imports_, error) ||
        !jni_.RegisterImports(&imports_, error) ||
        !assets_.RegisterImports(&imports_, error) ||
        !opensl_.RegisterImports(&imports_, error))
        return false;
    // Core libc data symbols the guest binds through undefined GLOB_DAT
    // relocations. These are objects, not functions, so they must resolve to
    // readable guest storage rather than the callable fail-closed code cells
    // below (a callable cell would be dereferenced as data and fault).
    const std::uint64_t libc_globals = heap_.Callocate(1, 0x400);
    libc_globals_ = libc_globals;
    if (libc_globals == 0) {
        SetError(error, "bionic libc globals allocation failed");
        return false;
    }
    // __stack_chk_guard holds the stack-protector canary: the guest loads
    // *guard at frame entry and compares it on return. It must be a real
    // readable word holding a nonzero value.
    const std::uint64_t stack_chk_canary = 0x9e3779b97f4a7c15ULL;
    if (!heap_.Write(libc_globals, &stack_chk_canary,
                     sizeof(stack_chk_canary))) {
        SetError(error, "bionic __stack_chk_guard write failed");
        return false;
    }
    {
        std::size_t cursor = 0x10;
        const auto register_global =
            [&](const char* name, std::size_t bytes) -> bool {
                const std::uint64_t address = libc_globals + cursor;
                cursor += (bytes + 0xf) & ~std::size_t{0xf};
                return imports_.RegisterDataAddress(name, address, error);
            };
        if (!imports_.RegisterDataAddress("__stack_chk_guard", libc_globals,
                                          error) ||
            // Zero-initialized object storage: environ/tzname read as empty
            // tables, optarg/opt*/__progname/in6addr_* read as null/zero.
            !register_global("environ", sizeof(void*)) ||
            !register_global("tzname", 2 * sizeof(void*)) ||
            !register_global("timezone", sizeof(long)) ||
            !register_global("daylight", sizeof(int)) ||
            !register_global("optarg", sizeof(void*)) ||
            !register_global("optind", sizeof(int)) ||
            !register_global("opterr", sizeof(int)) ||
            !register_global("optopt", sizeof(int)) ||
            !register_global("__progname", sizeof(void*)) ||
            !register_global("program_invocation_name", sizeof(void*)) ||
            !register_global("program_invocation_short_name",
                             sizeof(void*)) ||
            !register_global("in6addr_any", 16) ||
            !register_global("in6addr_loopback", 16))
            return false;
    }
    for (const std::string& name : fail_closed_imports) {
        if (imports_.Contains(name)) continue;
        // _ZTH<name> symbols are undefined-weak thread_local init helpers.
        // When no loaded module provides one the call is a no-op (the variable
        // is statically initialized or owned by another module's init), so it
        // must not abort the guest.
        if (name.rfind("_ZTH", 0) == 0) {
            if (!imports_.Register(
                    name,
                    [](Aarch64ImportRegistry::Registers*, std::string*) {
                        return true;
                    },
                    error))
                return false;
            continue;
        }
        if (!imports_.Register(
                name,
                [this, name](Aarch64ImportRegistry::Registers* registers,
                             std::string* dispatch_error) {
                    std::uint64_t stack_argument = 0;
                    const bool stack_argument_read =
                        registers->stack_pointer != 0 &&
                        address_space_.Read(registers->stack_pointer,
                                            &stack_argument,
                                            sizeof(stack_argument));
                    SetError(dispatch_error,
                             "unimplemented guest import invoked: " + name +
                                 " x0=" +
                                 std::to_string((*registers)[0]) + " x1=" +
                                 std::to_string((*registers)[1]) + " x2=" +
                                 std::to_string((*registers)[2]) + " x3=" +
                                 std::to_string((*registers)[3]) + " x4=" +
                                 std::to_string((*registers)[4]) + " x5=" +
                                 std::to_string((*registers)[5]) + " x6=" +
                                 std::to_string((*registers)[6]) + " x7=" +
                                 std::to_string((*registers)[7]) + " sp=" +
                                 std::to_string(registers->stack_pointer) +
                                 " stack0=" +
                                 (stack_argument_read
                                      ? std::to_string(stack_argument)
                                      : std::string("unreadable")));
                    return false;
                },
                error))
            return false;
    }
    if (!imports_.Seal(error) ||
        !gles_.ResolveVaoTrampolines(&imports_, error) ||
        !jni_.Initialize(imports_, error))
        return false;
    initialized_ = true;
    return true;
}

bool BionicRuntime::AttachModules(Elf64ModuleSet* modules,
                                  std::string* error) {
    const bool shutdown_in_progress =
        shutdown_stage_ != ShutdownStage::kOpenSl &&
        shutdown_stage_ != ShutdownStage::kComplete;
    if (!initialized_ || shutdown_ || shutdown_in_progress) {
        if (shutdown_in_progress) {
            SetError(error, "bionic runtime shutdown is in progress");
            return false;
        }
        SetError(error, "bionic runtime is not initialized");
        return false;
    }
    if (!address_space_.AttachModules(modules, error)) return false;
    return dynamic_loader_.AttachModules(modules, error);
}

bool BionicRuntime::AttachCurrentThread(std::string* error) {
    const bool shutdown_in_progress =
        shutdown_stage_ != ShutdownStage::kOpenSl &&
        shutdown_stage_ != ShutdownStage::kComplete;
    if (!initialized_ || shutdown_ || shutdown_in_progress) {
        if (shutdown_in_progress) {
            SetError(error, "bionic runtime shutdown is in progress");
            return false;
        }
        SetError(error, "bionic runtime is not initialized");
        return false;
    }
    if (!thread_state_.AttachCurrentThread(error)) return false;
    if (!time_.AttachCurrentThread(error)) {
        std::string ignored;
        (void)thread_state_.DetachCurrentThread(&ignored);
        return false;
    }
    if (jni_.AttachCurrentThread(error)) return true;
    std::string ignored;
    (void)time_.DetachCurrentThread(&ignored);
    (void)thread_state_.DetachCurrentThread(&ignored);
    return false;
}

bool BionicRuntime::DetachCurrentThread(std::string* error) {
    const bool shutdown_in_progress =
        shutdown_stage_ != ShutdownStage::kOpenSl &&
        shutdown_stage_ != ShutdownStage::kComplete;
    if (!initialized_ || shutdown_ || shutdown_in_progress) {
        if (shutdown_in_progress) {
            SetError(error, "bionic runtime shutdown is in progress");
            return false;
        }
        SetError(error, "bionic runtime is not initialized");
        return false;
    }
    if (tls_.current_thread_destructor_count() != 0) {
        SetError(error,
                 "current thread has pending guest TLS destructors");
        return false;
    }
    if (!tls_.ClearCurrentThreadValues(error)) return false;
    if (!jni_.DetachCurrentThread(error)) return false;
    if (!time_.DetachCurrentThread(error)) {
        std::string ignored;
        (void)jni_.AttachCurrentThread(&ignored);
        return false;
    }
    if (thread_state_.DetachCurrentThread(error)) return true;
    std::string ignored;
    (void)time_.AttachCurrentThread(&ignored);
    (void)jni_.AttachCurrentThread(&ignored);
    return false;
}

bool BionicRuntime::Shutdown(std::string* error) {
    if (!initialized_ || shutdown_) {
        SetError(error, shutdown_ ? "bionic runtime is already shut down"
                                  : "bionic runtime is not initialized");
        return false;
    }
    // A guest _exit leaves per-thread records and cleanup state behind by
    // definition; the ownership gate only applies to an orderly shutdown.
    const bool guest_process_exit = threads_.process_exit_requested();
    if (shutdown_stage_ == ShutdownStage::kOpenSl && !guest_process_exit &&
        (threads_.thread_count() != 0 || threads_.active_count() != 0 ||
         thread_state_.CurrentErrnoAddress() || at_exit_.size() != 0 ||
         dynamic_loader_.handle_count() != 0 || time_.result_count() != 0 ||
         files_.open_directory_count() != 0)) {
        SetError(error,
                 "bionic runtime ownership remains: threads=" +
                     std::to_string(threads_.thread_count()) +
                     " active=" + std::to_string(threads_.active_count()) +
                     " attached=" +
                     std::to_string(
                         thread_state_.CurrentErrnoAddress().has_value()) +
                     " time-results=" +
                     std::to_string(time_.result_count()) +
                     " directories=" +
                     std::to_string(files_.open_directory_count()) +
                     " finalizers=" + std::to_string(at_exit_.size()) +
                     " dl-handles=" +
                     std::to_string(dynamic_loader_.handle_count()));
        return false;
    }
    const auto run_stage =
        [&](ShutdownStage stage, bool ok) {
            if (ok || !guest_process_exit) return ok;
            // A worker abandoned by _exit can leave per-subsystem residue
            // that the orderly guards reject. The process is ending, so
            // report the residue and continue tearing down.
            std::fprintf(stderr,
                         "ISOMR_PROCESS_EXIT_RESIDUE stage=%d error=%s\n",
                         static_cast<int>(stage),
                         error != nullptr ? error->c_str() : "");
            if (error != nullptr) error->clear();
            return true;
        };
    for (;;) {
        switch (shutdown_stage_) {
        case ShutdownStage::kOpenSl:
            if (!run_stage(shutdown_stage_, opensl_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kJumps;
            break;
        case ShutdownStage::kJumps:
            if (!run_stage(shutdown_stage_, jumps_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kTime;
            break;
        case ShutdownStage::kTime:
            if (!run_stage(shutdown_stage_, time_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kSync;
            break;
        case ShutdownStage::kSync:
            if (!run_stage(shutdown_stage_, sync_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kStdio;
            break;
        case ShutdownStage::kStdio:
            if (!run_stage(shutdown_stage_, stdio_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kSignals;
            break;
        case ShutdownStage::kSignals:
            if (!run_stage(shutdown_stage_, signals_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kMappings;
            break;
        case ShutdownStage::kMappings:
            if (!run_stage(shutdown_stage_, mappings_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kLocales;
            break;
        case ShutdownStage::kLocales:
            if (!run_stage(shutdown_stage_, locales_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kAssets;
            break;
        case ShutdownStage::kAssets:
            if (!run_stage(shutdown_stage_, assets_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kJni;
            break;
        case ShutdownStage::kJni:
            if (!run_stage(shutdown_stage_, jni_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kProcessIdentity;
            break;
        case ShutdownStage::kProcessIdentity:
            if (!run_stage(shutdown_stage_, process_identity_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kGles;
            break;
        case ShutdownStage::kGles:
            if (!run_stage(shutdown_stage_, gles_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kDynamicLoader;
            break;
        case ShutdownStage::kDynamicLoader:
            if (!run_stage(shutdown_stage_, dynamic_loader_.Shutdown(error)))
                return false;
            shutdown_stage_ = ShutdownStage::kComplete;
            break;
        case ShutdownStage::kComplete:
            if (libc_globals_ != 0) {
                if (!run_stage(shutdown_stage_,
                               heap_.Release(libc_globals_, error)))
                    return false;
                libc_globals_ = 0;
            }
            shutdown_ = true;
            return true;
        }
    }
}

}  // namespace isomr
