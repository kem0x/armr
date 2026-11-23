#pragma once

#include "elf64_loader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace isomr {

struct Elf64ModuleLoadResult {
    Elf64Image* root = nullptr;
    std::string error;
    std::size_t modules_loaded = 0;
};

struct Elf64LoadedModule {
    const Elf64Image* image = nullptr;
    std::string name;
};

struct Elf64DynamicLoadResult {
    Elf64Image* module = nullptr;
    std::vector<Elf64LoadedModule> new_modules;
    std::string error;
};

struct Elf64ResolvedCodeAddress {
    std::string module;
    std::uint64_t offset = 0;
};

class Elf64ModuleSet {
public:
    explicit Elf64ModuleSet(
        Elf64ImportResolver host_resolver = {},
        std::unordered_set<std::string> host_dependencies = {});

    Elf64ModuleLoadResult LoadRoot(const std::wstring& path,
                                   std::uint64_t first_guest_base);
    Elf64DynamicLoadResult LoadSibling(std::string_view name);
    bool CommitDynamicLoad(
        const std::vector<Elf64LoadedModule>& new_modules,
        std::string* error);
    bool RollbackDynamicLoad(
        const std::vector<Elf64LoadedModule>& new_modules,
        std::string* error);
    std::optional<std::uint64_t> FindExport(std::string_view name) const;
    // Resolves against the root image only. load_order_ finishes
    // dependencies before the root, so name-wide lookup can shadow a root
    // export (JNI_OnLoad) with a dependency's.
    std::optional<std::uint64_t> FindRootExport(std::string_view name) const;
    std::optional<Elf64ResolvedCodeAddress> ResolveCodeAddress(
        std::uint64_t guest_address) const;
    const Elf64Image* FindModuleByBasename(std::string_view name) const;
    bool IsHostDependencyAllowed(std::string_view name) const;
    std::vector<Elf64LoadedModule> LoadedModulesRootFirst() const;
    std::vector<std::uint64_t> Initializers() const;
    bool Read(std::uint64_t guest_address, void* destination,
              std::size_t size) const;
    bool ReadCode(std::uint64_t guest_address, void* destination,
                  std::size_t size) const;
    // Installation is permitted only before any guest session executes. An
    // empty module name selects the root image; named targets use a contained
    // loaded basename. The original two instructions must match exactly.
    bool InstallCodeSubstitution(
        std::string_view module, std::uint64_t module_offset,
        const std::array<std::uint32_t, 2>& expected,
        const std::array<std::uint32_t, 2>& replacement,
        std::string* error);
    bool IsWritableRange(std::uint64_t guest_address,
                         std::size_t size) const;
    bool Write(std::uint64_t guest_address, const void* source,
               std::size_t size);
    std::size_t size() const;
    std::size_t host_dependency_count() const;

private:
    Elf64Image* LoadModule(const std::wstring& path, std::string* error);
    std::optional<std::uint64_t> FindExportUnlocked(
        std::string_view name) const;
    const Elf64Image* FindModuleByBasenameUnlocked(
        std::string_view name) const;

    Elf64ImportResolver host_resolver_;
    std::unordered_set<std::string> host_dependencies_;
    std::unordered_set<std::string> used_host_dependencies_;
    std::wstring allowed_directory_;
    std::uint64_t next_guest_base_ = 0;
    std::unordered_map<std::wstring, std::unique_ptr<Elf64Image>> modules_;
    std::unordered_set<std::wstring> loading_;
    std::vector<Elf64Image*> load_order_;
    std::unordered_map<const Elf64Image*, std::string> module_names_;
    Elf64Image* root_ = nullptr;
    struct PendingDynamicLoad {
        std::vector<const Elf64Image*> modules;
        std::uint64_t previous_next_guest_base = 0;
        std::unordered_set<std::string> previous_host_dependencies;
    };
    std::optional<PendingDynamicLoad> pending_dynamic_load_;
    mutable std::shared_mutex mutex_;
};

}  // namespace isomr
