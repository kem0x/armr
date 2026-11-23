#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "elf64_modules.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace isomr {
namespace {

constexpr std::uint64_t kModuleAddressStride = 1ULL << 32;
constexpr std::size_t kMaximumModules = 64;

std::optional<std::wstring> CanonicalExistingPath(const std::wstring& path,
                                                  std::string* error) {
    HANDLE raw = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE |
                                 FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
        *error = "cannot open ELF module path (Windows error " +
                 std::to_string(GetLastError()) + ")";
        return std::nullopt;
    }
    const auto close_handle = [](void* handle) {
        CloseHandle(static_cast<HANDLE>(handle));
    };
    std::unique_ptr<void, decltype(close_handle)> file(raw, close_handle);

    const DWORD required = GetFinalPathNameByHandleW(
        raw, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (required == 0) {
        *error = "cannot canonicalize ELF module path (Windows error " +
                 std::to_string(GetLastError()) + ")";
        return std::nullopt;
    }
    std::wstring result(required, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(
        raw, result.data(), required, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0 || written >= required) {
        *error = "canonical ELF module path changed while reading";
        return std::nullopt;
    }
    result.resize(written);
    return result;
}

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) {
                       return static_cast<wchar_t>(std::towlower(character));
                   });
    return value;
}

std::wstring ParentPath(const std::wstring& path) {
    const std::size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring()
                                            : path.substr(0, separator);
}

std::optional<std::string> Utf8Basename(const std::wstring& path,
                                        std::string* error) {
    const std::size_t separator = path.find_last_of(L"\\/");
    const std::wstring_view basename =
        separator == std::wstring::npos
            ? std::wstring_view(path)
            : std::wstring_view(path).substr(separator + 1);
    if (basename.empty() ||
        basename.size() > static_cast<std::size_t>(
                              std::numeric_limits<int>::max())) {
        *error = "ELF module basename is empty or too long";
        return std::nullopt;
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, basename.data(),
        static_cast<int>(basename.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        *error = "ELF module basename is not valid Unicode";
        return std::nullopt;
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, basename.data(),
                            static_cast<int>(basename.size()), result.data(),
                            required, nullptr, nullptr) != required) {
        *error = "ELF module basename conversion failed";
        return std::nullopt;
    }
    return result;
}

std::optional<std::wstring> SafeDependencyName(std::string_view name,
                                               std::string* error) {
    if (name.empty() || name.size() > 255 || name == "." || name == ".." ||
        name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos ||
        name.find(':') != std::string_view::npos) {
        *error = "DT_NEEDED name is not a safe sibling filename";
        return std::nullopt;
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()),
        nullptr, 0);
    if (required <= 0) {
        *error = "DT_NEEDED name is not valid UTF-8";
        return std::nullopt;
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                            static_cast<int>(name.size()), wide.data(),
                            required) != required) {
        *error = "DT_NEEDED name conversion failed";
        return std::nullopt;
    }
    return wide;
}

}  // namespace

Elf64ModuleSet::Elf64ModuleSet(
    Elf64ImportResolver host_resolver,
    std::unordered_set<std::string> host_dependencies)
    : host_resolver_(std::move(host_resolver)),
      host_dependencies_(std::move(host_dependencies)) {}

Elf64ModuleLoadResult Elf64ModuleSet::LoadRoot(const std::wstring& path,
                                               std::uint64_t first_guest_base) {
    const std::unique_lock lock(mutex_);
    modules_.clear();
    loading_.clear();
    load_order_.clear();
    module_names_.clear();
    root_ = nullptr;
    pending_dynamic_load_.reset();
    used_host_dependencies_.clear();
    allowed_directory_.clear();
    next_guest_base_ = first_guest_base;
    if ((first_guest_base & 0xfffU) != 0) {
        return {nullptr, "first guest module base is not page-aligned", 0};
    }

    std::string error;
    const std::optional<std::wstring> canonical =
        CanonicalExistingPath(path, &error);
    if (!canonical) return {nullptr, std::move(error), 0};
    allowed_directory_ = Lowercase(ParentPath(*canonical));
    if (allowed_directory_.empty())
        return {nullptr, "root ELF module has no parent directory", 0};

    Elf64Image* root = LoadModule(*canonical, &error);
    if (root == nullptr) {
        modules_.clear();
        loading_.clear();
        load_order_.clear();
        module_names_.clear();
        return {nullptr, std::move(error), 0};
    }
    root_ = root;
    return {root, {}, modules_.size()};
}

Elf64DynamicLoadResult Elf64ModuleSet::LoadSibling(std::string_view name) {
    const std::unique_lock lock(mutex_);
    if (root_ == nullptr || allowed_directory_.empty() || !loading_.empty() ||
        pending_dynamic_load_) {
        return {nullptr, {}, "ELF module set is not ready for a sibling load"};
    }
    std::string error;
    const std::optional<std::wstring> wide = SafeDependencyName(name, &error);
    if (!wide) return {nullptr, {}, std::move(error)};
    if (const Elf64Image* present = FindModuleByBasenameUnlocked(name)) {
        return {const_cast<Elf64Image*>(present), {}, {}};
    }

    const std::size_t previous_order_size = load_order_.size();
    const std::uint64_t previous_next_guest_base = next_guest_base_;
    const std::unordered_set<std::string> previous_host_dependencies =
        used_host_dependencies_;
    const std::wstring candidate = allowed_directory_ + L"\\" + *wide;
    Elf64Image* loaded = LoadModule(candidate, &error);
    if (loaded == nullptr) {
        std::unordered_set<const Elf64Image*> added;
        for (std::size_t index = previous_order_size;
             index < load_order_.size(); ++index)
            added.insert(load_order_[index]);
        for (auto module = modules_.begin(); module != modules_.end();) {
            if (added.contains(module->second.get()))
                module = modules_.erase(module);
            else
                ++module;
        }
        for (const Elf64Image* image : added) module_names_.erase(image);
        load_order_.resize(previous_order_size);
        next_guest_base_ = previous_next_guest_base;
        used_host_dependencies_ = previous_host_dependencies;
        loading_.clear();
        return {nullptr, {}, std::move(error)};
    }

    std::vector<Elf64LoadedModule> new_modules;
    new_modules.reserve(load_order_.size() - previous_order_size);
    for (std::size_t index = previous_order_size;
         index < load_order_.size(); ++index) {
        const Elf64Image* image = load_order_[index];
        const auto found = module_names_.find(image);
        if (found != module_names_.end())
            new_modules.push_back({image, found->second});
    }
    PendingDynamicLoad pending;
    pending.previous_next_guest_base = previous_next_guest_base;
    pending.previous_host_dependencies = previous_host_dependencies;
    for (const Elf64LoadedModule& module : new_modules)
        pending.modules.push_back(module.image);
    pending_dynamic_load_ = std::move(pending);
    return {loaded, std::move(new_modules), {}};
}

bool Elf64ModuleSet::CommitDynamicLoad(
    const std::vector<Elf64LoadedModule>& new_modules,
    std::string* error) {
    const std::unique_lock lock(mutex_);
    if (!pending_dynamic_load_ ||
        pending_dynamic_load_->modules.size() != new_modules.size()) {
        if (error != nullptr)
            *error = "ELF dynamic-load commit has no matching transaction";
        return false;
    }
    for (std::size_t index = 0; index < new_modules.size(); ++index) {
        if (new_modules[index].image !=
            pending_dynamic_load_->modules[index]) {
            if (error != nullptr)
                *error = "ELF dynamic-load commit modules do not match";
            return false;
        }
    }
    pending_dynamic_load_.reset();
    return true;
}

bool Elf64ModuleSet::RollbackDynamicLoad(
    const std::vector<Elf64LoadedModule>& new_modules,
    std::string* error) {
    const std::unique_lock lock(mutex_);
    if (!pending_dynamic_load_ ||
        pending_dynamic_load_->modules.size() != new_modules.size()) {
        if (error != nullptr)
            *error = "ELF dynamic-load rollback has no matching transaction";
        return false;
    }
    for (std::size_t index = 0; index < new_modules.size(); ++index) {
        if (new_modules[index].image !=
                pending_dynamic_load_->modules[index] ||
            load_order_.size() < new_modules.size() ||
            load_order_[load_order_.size() - new_modules.size() + index] !=
                new_modules[index].image) {
            if (error != nullptr)
                *error = "ELF dynamic-load rollback modules do not match";
            return false;
        }
    }
    std::unordered_set<const Elf64Image*> removed(
        pending_dynamic_load_->modules.begin(),
        pending_dynamic_load_->modules.end());
    for (auto module = modules_.begin(); module != modules_.end();) {
        if (removed.contains(module->second.get()))
            module = modules_.erase(module);
        else
            ++module;
    }
    for (const Elf64Image* image : removed) module_names_.erase(image);
    load_order_.resize(load_order_.size() - new_modules.size());
    next_guest_base_ = pending_dynamic_load_->previous_next_guest_base;
    used_host_dependencies_ =
        pending_dynamic_load_->previous_host_dependencies;
    pending_dynamic_load_.reset();
    return true;
}

std::optional<std::uint64_t> Elf64ModuleSet::FindExport(
    std::string_view name) const {
    const std::shared_lock lock(mutex_);
    return FindExportUnlocked(name);
}

std::optional<std::uint64_t> Elf64ModuleSet::FindRootExport(
    std::string_view name) const {
    const std::shared_lock lock(mutex_);
    if (root_ == nullptr) return std::nullopt;
    return root_->FindExport(name);
}

std::optional<std::uint64_t> Elf64ModuleSet::FindExportUnlocked(
    std::string_view name) const {
    for (Elf64Image* module : load_order_) {
        const std::optional<std::uint64_t> address = module->FindExport(name);
        if (address) return address;
    }
    return std::nullopt;
}

std::optional<Elf64ResolvedCodeAddress>
Elf64ModuleSet::ResolveCodeAddress(std::uint64_t guest_address) const {
    const std::shared_lock lock(mutex_);
    constexpr std::size_t kMaximumSanitizedName = 96;
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (const Elf64Image* module : load_order_) {
        std::uint8_t byte = 0;
        if (guest_address < module->guest_base() ||
            !module->ReadCode(guest_address, &byte, sizeof(byte)))
            continue;
        const auto found = module_names_.find(module);
        if (found == module_names_.end()) return std::nullopt;
        std::string sanitized;
        sanitized.reserve(std::min(found->second.size(),
                                   kMaximumSanitizedName));
        for (const unsigned char value : found->second) {
            const bool safe =
                (value >= 'a' && value <= 'z') ||
                (value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9') || value == '_' ||
                value == '.' || value == '-';
            const std::size_t needed = safe ? 1U : 3U;
            if (sanitized.size() + needed > kMaximumSanitizedName) break;
            if (safe) {
                sanitized.push_back(static_cast<char>(value));
            } else {
                sanitized.push_back('%');
                sanitized.push_back(kHex[value >> 4]);
                sanitized.push_back(kHex[value & 15U]);
            }
        }
        if (sanitized.empty()) sanitized = "-";
        return Elf64ResolvedCodeAddress{
            std::move(sanitized), guest_address - module->guest_base()};
    }
    return std::nullopt;
}

const Elf64Image* Elf64ModuleSet::FindModuleByBasename(
    std::string_view name) const {
    const std::shared_lock lock(mutex_);
    return FindModuleByBasenameUnlocked(name);
}

const Elf64Image* Elf64ModuleSet::FindModuleByBasenameUnlocked(
    std::string_view name) const {
    std::string ignored;
    const std::optional<std::wstring> wide = SafeDependencyName(name, &ignored);
    if (!wide) return nullptr;
    const std::wstring wanted = Lowercase(*wide);
    for (const auto& entry : modules_) {
        const std::size_t separator = entry.first.find_last_of(L"\\/");
        const std::wstring_view basename =
            separator == std::wstring::npos
                ? std::wstring_view(entry.first)
                : std::wstring_view(entry.first).substr(separator + 1);
        if (basename == wanted) return entry.second.get();
    }
    return nullptr;
}

bool Elf64ModuleSet::IsHostDependencyAllowed(std::string_view name) const {
    const std::shared_lock lock(mutex_);
    std::string ignored;
    return SafeDependencyName(name, &ignored).has_value() &&
           host_dependencies_.contains(std::string(name));
}

std::vector<Elf64LoadedModule> Elf64ModuleSet::LoadedModulesRootFirst() const {
    const std::shared_lock lock(mutex_);
    std::vector<Elf64LoadedModule> result;
    result.reserve(load_order_.size());
    const auto append = [this, &result](const Elf64Image* image) {
        const auto found = module_names_.find(image);
        if (found != module_names_.end())
            result.push_back({image, found->second});
    };
    if (root_ != nullptr) append(root_);
    for (const Elf64Image* image : load_order_) {
        if (image != root_) append(image);
    }
    return result;
}

std::vector<std::uint64_t> Elf64ModuleSet::Initializers() const {
    const std::shared_lock lock(mutex_);
    std::vector<std::uint64_t> result;
    for (Elf64Image* module : load_order_)
        result.insert(result.end(), module->initializers().begin(),
                      module->initializers().end());
    return result;
}

bool Elf64ModuleSet::Read(std::uint64_t guest_address, void* destination,
                          std::size_t size) const {
    const std::shared_lock lock(mutex_);
    for (Elf64Image* module : load_order_)
        if (module->Read(guest_address, destination, size)) return true;
    return false;
}

bool Elf64ModuleSet::ReadCode(std::uint64_t guest_address, void* destination,
                              std::size_t size) const {
    const std::shared_lock lock(mutex_);
    for (Elf64Image* module : load_order_)
        if (module->ReadCode(guest_address, destination, size)) return true;
    return false;
}

bool Elf64ModuleSet::InstallCodeSubstitution(
    std::string_view module_name, std::uint64_t module_offset,
    const std::array<std::uint32_t, 2>& expected,
    const std::array<std::uint32_t, 2>& replacement,
    std::string* error) {
    const std::unique_lock lock(mutex_);
    if (root_ == nullptr || !loading_.empty() || pending_dynamic_load_) {
        if (error != nullptr)
            *error = "ELF module set is not ready for code substitution";
        return false;
    }
    Elf64Image* module = root_;
    if (!module_name.empty()) {
        module = const_cast<Elf64Image*>(
            FindModuleByBasenameUnlocked(module_name));
        if (module == nullptr) {
            if (error != nullptr)
                *error = "ELF code substitution module is unavailable";
            return false;
        }
    }
    if (module_offset >
        std::numeric_limits<std::uint64_t>::max() - module->guest_base()) {
        if (error != nullptr)
            *error = "ELF code substitution offset overflowed";
        return false;
    }
    return module->PatchCode(module->guest_base() + module_offset,
                             expected, replacement, error);
}

bool Elf64ModuleSet::IsWritableRange(std::uint64_t guest_address,
                                     std::size_t size) const {
    const std::shared_lock lock(mutex_);
    for (const Elf64Image* module : load_order_)
        if (module->IsWritableRange(guest_address, size)) return true;
    return false;
}

bool Elf64ModuleSet::Write(std::uint64_t guest_address, const void* source,
                           std::size_t size) {
    const std::unique_lock lock(mutex_);
    for (Elf64Image* module : load_order_)
        if (module->Write(guest_address, source, size)) return true;
    return false;
}

Elf64Image* Elf64ModuleSet::LoadModule(const std::wstring& path,
                                       std::string* error) {
    const std::optional<std::wstring> canonical =
        CanonicalExistingPath(path, error);
    if (!canonical) return nullptr;
    const std::optional<std::string> module_name =
        Utf8Basename(*canonical, error);
    if (!module_name) return nullptr;
    const std::wstring key = Lowercase(*canonical);
    if (Lowercase(ParentPath(*canonical)) != allowed_directory_) {
        *error = "ELF dependency resolved outside the root module directory";
        return nullptr;
    }
    const auto present = modules_.find(key);
    if (present != modules_.end()) return present->second.get();
    if (loading_.contains(key)) {
        *error = "ELF dependency cycle detected";
        return nullptr;
    }
    if (modules_.size() + loading_.size() >= kMaximumModules) {
        *error = "ELF dependency graph exceeds the module limit";
        return nullptr;
    }
    if (next_guest_base_ >
        std::numeric_limits<std::uint64_t>::max() - kModuleAddressStride) {
        *error = "ELF guest module address space is exhausted";
        return nullptr;
    }
    const std::uint64_t guest_base = next_guest_base_;
    next_guest_base_ += kModuleAddressStride;
    loading_.insert(key);

    const Elf64DependencyPreparer prepare =
        [&](const std::vector<std::string>& needed,
            std::string* dependency_error) {
            for (const std::string& name : needed) {
                const std::optional<std::wstring> wide =
                    SafeDependencyName(name, dependency_error);
                if (!wide) return false;
                if (host_dependencies_.contains(name)) {
                    used_host_dependencies_.insert(name);
                    continue;
                }
                const std::wstring candidate =
                    allowed_directory_ + L"\\" + *wide;
                if (LoadModule(candidate, dependency_error) == nullptr) {
                    *dependency_error = "ELF dependency " + name +
                                        " failed: " + *dependency_error;
                    return false;
                }
            }
            return true;
        };
    const Elf64ImportResolver resolve = [&](std::string_view name)
        -> std::optional<std::uint64_t> {
        const std::optional<std::uint64_t> guest = FindExportUnlocked(name);
        if (guest) return guest;
        return host_resolver_ ? host_resolver_(name) : std::nullopt;
    };
    Elf64LoadResult loaded =
        LoadElf64File(*canonical, guest_base, resolve, prepare);
    loading_.erase(key);
    if (!loaded.image) {
        *error = "module load failed: " + loaded.error;
        return nullptr;
    }

    auto image =
        std::make_unique<Elf64Image>(std::move(*loaded.image));
    Elf64Image* result = image.get();
    modules_.emplace(key, std::move(image));
    load_order_.push_back(result);
    module_names_.emplace(result, *module_name);
    return result;
}

std::size_t Elf64ModuleSet::size() const {
    const std::shared_lock lock(mutex_);
    return modules_.size();
}

std::size_t Elf64ModuleSet::host_dependency_count() const {
    const std::shared_lock lock(mutex_);
    return used_host_dependencies_.size();
}

}  // namespace isomr
