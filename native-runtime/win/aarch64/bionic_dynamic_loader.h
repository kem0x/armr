#pragma once

#include "bionic_threads.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace isomr {

class Aarch64ImportRegistry;
class BionicThreadState;
class Elf64Image;
class Elf64ModuleSet;
struct Elf64LoadedModule;
class GuestAddressSpace;
class GuestHeap;

struct BionicDlPhdrInfo {
    std::uint64_t address = 0;
    std::uint64_t name = 0;
    std::uint64_t program_headers = 0;
    std::uint16_t program_header_count = 0;
    std::array<std::uint8_t, 6> padding{};
    std::uint64_t adds = 0;
    std::uint64_t subtracts = 0;
    std::uint64_t tls_module_id = 0;
    std::uint64_t tls_data = 0;
};

static_assert(sizeof(BionicDlPhdrInfo) == 64);
static_assert(offsetof(BionicDlPhdrInfo, address) == 0);
static_assert(offsetof(BionicDlPhdrInfo, name) == 8);
static_assert(offsetof(BionicDlPhdrInfo, program_headers) == 16);
static_assert(offsetof(BionicDlPhdrInfo, program_header_count) == 24);
static_assert(offsetof(BionicDlPhdrInfo, adds) == 32);
static_assert(offsetof(BionicDlPhdrInfo, subtracts) == 40);
static_assert(offsetof(BionicDlPhdrInfo, tls_module_id) == 48);
static_assert(offsetof(BionicDlPhdrInfo, tls_data) == 56);

class BionicDynamicLoader {
public:
    BionicDynamicLoader(GuestHeap* heap, GuestAddressSpace* address_space,
                        BionicThreadState* threads,
                        BionicThreads::GuestExecutorFactory executor_factory,
                        std::unordered_set<std::string>
                            allowed_sibling_libraries = {})
        : heap_(heap), address_space_(address_space), threads_(threads),
          executor_factory_(std::move(executor_factory)),
          allowed_sibling_libraries_(
              std::move(allowed_sibling_libraries)) {}

    bool RegisterImports(Aarch64ImportRegistry* imports, std::string* error);
    bool AttachModules(Elf64ModuleSet* modules, std::string* error);
    bool DiscardHandlesForProcessExit(std::size_t* discarded,
                                      std::string* error);
    bool Shutdown(std::string* error);

    std::size_t handle_count() const;

private:
    enum class HandleKind { Global, Module, HostDependency };

    struct Handle {
        HandleKind kind = HandleKind::Global;
        const Elf64Image* module = nullptr;
        std::string name;
        std::size_t references = 0;
    };

    struct ThreadError {
        std::uint64_t address = 0;
        bool pending = false;
    };

    struct IterationModule {
        const Elf64Image* image = nullptr;
        std::uint64_t load_bias = 0;
        std::uint64_t name = 0;
        std::uint64_t program_headers = 0;
        std::uint16_t program_header_count = 0;
    };

    bool StageIterationModulesLocked(
        const std::vector<Elf64LoadedModule>& modules,
        std::string* error);
    bool RunModuleInitializersLocked(
        std::unique_lock<std::recursive_mutex>* loader_lock,
        const Elf64Image* module, std::string* error);
    bool Dlopen(std::uint64_t filename, std::uint64_t flags,
                std::uint64_t* result, std::string* error);
    bool Dlsym(std::uint64_t handle, std::uint64_t symbol,
               std::uint64_t* result, std::string* error);
    bool Dlclose(std::uint64_t handle, std::uint64_t* result,
                 std::string* error);
    bool Dlerror(std::uint64_t* result, std::string* error);
    bool DlIteratePhdr(std::uint64_t callback, std::uint64_t data,
                       std::uint64_t* result, std::string* error);
    bool Dladdr(std::uint64_t address, std::uint64_t info_address,
                std::uint64_t* result, std::string* error);
    bool ReadGuestString(std::uint64_t address, std::string* result,
                         std::string* error) const;
    bool SetPendingErrorLocked(std::string message, std::string* error);
    bool OpenHandleLocked(HandleKind kind, const Elf64Image* module,
                          std::string name, std::uint64_t* result,
                          std::string* error);

    static constexpr std::uint64_t kRtldLazy = 1;
    static constexpr std::uint64_t kRtldNow = 2;
    static constexpr std::size_t kMaximumName = 255;
    static constexpr std::size_t kErrorCapacity = 256;
    static constexpr std::size_t kMaximumIterationMetadata = 4U * 1024U * 1024U;

    GuestHeap* heap_ = nullptr;
    GuestAddressSpace* address_space_ = nullptr;
    BionicThreadState* threads_ = nullptr;
    BionicThreads::GuestExecutorFactory executor_factory_;
    std::unordered_set<std::string> allowed_sibling_libraries_;
    Aarch64ImportRegistry* imports_ = nullptr;
    Elf64ModuleSet* modules_ = nullptr;
    mutable std::recursive_mutex mutex_;
    std::unordered_map<std::uint64_t, Handle> handles_;
    std::unordered_map<std::string, std::uint64_t> handles_by_scope_;
    std::unordered_map<std::uint32_t, ThreadError> thread_errors_;
    std::vector<IterationModule> iteration_modules_;
    std::unordered_set<const Elf64Image*> initialized_modules_;
    std::unordered_set<const Elf64Image*> initializing_modules_;
    std::unordered_map<const Elf64Image*, std::uint32_t>
        initializing_module_owners_;
    std::unordered_set<const Elf64Image*> failed_modules_;
    std::unordered_map<const Elf64Image*, std::string> failed_module_errors_;
    std::size_t iteration_metadata_size_ = 0;
    std::uint64_t module_add_count_ = 0;
    std::uint64_t module_subtract_count_ = 0;
    std::size_t active_iterations_ = 0;
    bool shutdown_ = false;
};

}  // namespace isomr
