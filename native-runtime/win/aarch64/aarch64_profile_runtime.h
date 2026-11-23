#pragma once

#include "aarch64_guest_session.h"
#include "bionic_files.h"
#include "bionic_time.h"
#include "guest_jni.h"
#include "import_registry.h"
#include "native_opensles.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace isomr {

class Aarch64ExclusiveMonitor;
class BionicRuntime;
class Elf64ModuleSet;
class GuestJniBackend;

struct Aarch64GuestCodeSubstitution {
    std::string import_name;
    // Empty selects the root image; otherwise this is an exact contained
    // module basename.
    std::string module;
    std::uint64_t module_offset = 0;
    std::array<std::uint32_t, 2> expected{};
    std::function<Aarch64ImportRegistry::Handler(BionicRuntime*)>
        handler_factory;
};

struct Aarch64ProfileRuntimeConfig {
    std::uint64_t heap_base = 0x710000000000ULL;
    std::size_t heap_capacity = 64U * 1024U * 1024U;
    std::uint64_t import_base = 0x680000000000ULL;
    std::size_t import_capacity = 4096;
    std::uint64_t first_module_base = 0x4000000000ULL;
    std::size_t stack_size = 1024U * 1024U;
    std::uint64_t instruction_budget = 100000000ULL;
    std::vector<BionicFileMount> file_mounts;
    std::string guest_current_directory;
    std::unordered_set<std::string> host_dependencies;
    std::unordered_set<std::string> dlopen_sibling_libraries;
    bool fail_closed_unimplemented_imports = false;
    GuestJniBackend* jni_backend = nullptr;
    GuestJniCompatibilityConfig jni_compatibility;
    std::size_t heap_watch_allocation_size = 0;
    std::size_t heap_watch_range_offset = 0;
    std::size_t heap_watch_range_size = 0;
    bool import_journal_enabled = false;
    BionicNanosleepBackend nanosleep_backend =
        BionicNanosleepBackend::kSleepFor;
    bool nanosleep_callsite_telemetry_enabled = false;
    NativeOpenSlMode opensl_mode = NativeOpenSlMode::kDisabled;
    Aarch64MemoryCaptureConfig memory_capture;
    std::vector<Aarch64GuestCodeSubstitution> code_substitutions;
};

enum class Aarch64ProfileShutdownMode {
    kRunGuestFinalizers,
    kGuestProcessExit,
};

class Aarch64ProfileRuntime {
public:
    explicit Aarch64ProfileRuntime(Aarch64ProfileRuntimeConfig config);
    ~Aarch64ProfileRuntime();
    Aarch64ProfileRuntime(const Aarch64ProfileRuntime&) = delete;
    Aarch64ProfileRuntime& operator=(const Aarch64ProfileRuntime&) = delete;

    bool Load(const std::wstring& root_library, std::string* error);
    bool RunInitializers(std::string* error);
    std::optional<std::uint64_t> FindExport(std::string_view name) const;
    bool Call(std::uint64_t function,
              const std::array<std::uint64_t, 8>& arguments,
              std::uint64_t* result, std::string* error);
    bool CallEntry(std::uint64_t function,
                   const std::vector<Aarch64CallArgument>& arguments,
                   Aarch64CallResult* result, std::string* error);
    bool CallExport(std::string_view name,
                    const std::array<std::uint64_t, 8>& arguments,
                    std::uint64_t* result, std::string* error);
    bool CallExportEntry(
        std::string_view name,
        const std::vector<Aarch64CallArgument>& arguments,
        Aarch64CallResult* result, std::string* error);
    bool CallRegisteredNative(
        std::string_view class_type, std::string_view name,
        std::string_view signature,
        const std::vector<Aarch64CallArgument>& arguments,
        Aarch64CallResult* result, std::string* error);
    // Invoke a registered JNI native from a secondary host thread that has
    // already been attached through BionicRuntime::AttachCurrentThread().
    // Each caller receives an independent AArch64 execution session while
    // sharing the loaded modules and compatibility runtime.
    bool CallRegisteredNativeOnAttachedThread(
        std::string_view class_type, std::string_view name,
        std::string_view signature,
        const std::vector<Aarch64CallArgument>& arguments,
        Aarch64CallResult* result, std::string* error);
    // Invoke an arbitrary guest function address on the calling thread's
    // attached session. Used to run guest TLS destructors before detach,
    // matching bionic thread-exit semantics.
    bool CallAddressOnAttachedThread(
        std::uint64_t function,
        const std::vector<Aarch64CallArgument>& arguments,
        Aarch64CallResult* result, std::string* error);
    bool CallJniOnLoad(std::uint64_t* version, std::string* error);
    // Calls JNI_OnLoad(vm) for every loaded dependency module that exports
    // it. Android's System.loadLibrary does this per library; DT_NEEDED
    // dependency loads do not, leaving engine Java-bridge caches unset.
    bool CallDependencyJniOnLoads(std::string* error);
    bool SetMainInstructionBudgetForNextCall(
        std::uint64_t instruction_budget, std::string* error);
    bool SetMainOwnerThreadInterSliceCallback(
        std::uint64_t slice_instruction_budget,
        Aarch64OwnerThreadInterSliceCallback callback,
        std::string* error);
    bool SetMainOwnerThreadInterSliceBudgetForNextSlice(
        std::uint64_t slice_instruction_budget, std::string* error);
    void ClearMainOwnerThreadInterSliceCallback();
    bool CaptureMainGuestPcTelemetrySample(
        Aarch64GuestCallTelemetry* telemetry, std::string* error) const;
    bool SetMainOwnerThreadExecutionBreakpoint(
        std::uint64_t guest_address,
        Aarch64OwnerThreadExecutionBreakpointCallback callback,
        std::string* error);
    void ClearMainOwnerThreadExecutionBreakpoint();
    bool AddMainOwnerThreadExecutionBreakpoint(
        std::uint64_t guest_address,
        Aarch64OwnerThreadExecutionBreakpointCallback callback,
        std::string* error);
    void RemoveMainOwnerThreadExecutionBreakpoint(
        std::uint64_t guest_address);
    bool SetMainCallTelemetryEnabled(bool enabled, std::string* error);
    bool SetMainCallTelemetryMode(Aarch64CallTelemetryMode mode,
                                  std::string* error);
    Aarch64GuestCallTelemetry last_call_telemetry() const;
    NanosleepCallsiteTelemetryToken BeginNanosleepCallsiteTelemetry()
        noexcept;
    NanosleepCallsiteTelemetrySnapshot EndNanosleepCallsiteTelemetry(
        NanosleepCallsiteTelemetryToken token) noexcept;
    struct ResolvedGuestCodeAddress {
        std::string module;
        std::uint64_t offset = 0;
    };
    std::optional<ResolvedGuestCodeAddress> ResolveGuestCodeAddress(
        std::uint64_t guest_address) const;
    // Empty module selects the root image. Every supplied instruction must
    // match before the resolved executable address is returned.
    bool ResolveGuestModuleCodeOffset(
        std::string_view module, std::uint64_t module_offset,
        std::span<const std::uint32_t> expected,
        std::uint64_t* guest_address, std::string* error) const;
    // Load address of a dependency by basename (e.g. "libNimble.so");
    // nullopt when the module is not loaded.
    std::optional<std::uint64_t> GuestModuleBase(
        std::string_view basename) const;
    // Scans a module's executable segments for a byte signature. `pattern` and
    // `mask` are parallel spans; a mask byte of 0 marks a wildcard. Empty
    // module selects the root image. Returns the guest address of the first
    // match; when `match_count` is non-null it receives the total number of
    // matches (so a caller can reject ambiguous signatures). Signatures make
    // a site version-tolerant where a fixed RVA would not survive a rebuild.
    std::optional<std::uint64_t> ScanModuleCode(
        std::string_view module, std::span<const std::uint8_t> pattern,
        std::span<const std::uint8_t> mask,
        std::size_t* match_count) const;
    bool Shutdown(
        std::string* error,
        Aarch64ProfileShutdownMode mode =
            Aarch64ProfileShutdownMode::kRunGuestFinalizers);

    std::size_t module_count() const;
    std::size_t import_count() const;
    std::size_t initializer_count() const;
    std::uint64_t entry_point() const { return entry_point_; }
    BionicRuntime* bionic() { return runtime_.get(); }
    // Diagnostics: per-worker guest PC for each live session, resolved to
    // module+offset where possible. Sessions register themselves by guest
    // thread handle so a stall can be attributed to a concrete location.
    std::string DescribeWorkerGuestPcs() const;
    // Diagnostics: force a live session's guest call to halt at the next
    // dynarmic boundary so its CallEntry surfaces the stalled PC and the
    // recent-import tail through the ordinary error path.
    bool HaltSignalSession(std::uint64_t thread_handle);

private:
    void RegisterSignalSession(
        std::uint64_t thread_handle,
        const std::shared_ptr<Aarch64GuestSession>& session);
    std::shared_ptr<Aarch64GuestSession> LookupSignalSession(
        std::uint64_t thread_handle) const;
    // Builds a fresh attached-thread call session (Create + raise probe).
    // When register_session is true the session is published to
    // signal_sessions_ under kCallSessionKeyBase|thread_pointer; nested
    // one-shot fallbacks pass false so they don't clobber the cached primary
    // session's stable registration.
    std::shared_ptr<Aarch64GuestSession> CreateAttachedCallSession(
        std::uint64_t thread_pointer, bool register_session,
        std::string* error);
    // Runs `function` via CallEntry on the per-thread-pointer cached
    // attached-call session, reusing it across calls. Nested re-entrant
    // calls (slot already in_use) fall back to a one-shot session.
    bool CallEntryOnAttachedSession(
        std::uint64_t thread_pointer, std::uint64_t function,
        const std::vector<Aarch64CallArgument>& arguments,
        Aarch64CallResult* result, std::string* error);
    std::unordered_map<std::uint64_t, std::weak_ptr<Aarch64GuestSession>>
        signal_sessions_;
    mutable std::mutex signal_sessions_mutex_;
    // Comparator/callback executor cache. Imports such as bsearch/qsort and
    // JNI callbacks invoke guest code through executor_factory; creating a
    // fresh Aarch64GuestSession (1 MiB stack + full Dynarmic jit state) per
    // call dominated Unity/IL2CPP startup, which issues tens of thousands of
    // metadata bsearches. One persistent session per guest thread keeps the
    // jit block cache warm across calls. A slot marked in_use falls back to a
    // one-shot session so nested guest calls (bsearch inside a comparator)
    // stay safe.
    struct SharedExecutor {
        std::shared_ptr<Aarch64GuestSession> session;
        std::atomic<bool> in_use{false};
    };
    std::unordered_map<std::uint64_t, std::shared_ptr<SharedExecutor>>
        executor_sessions_;
    std::mutex executor_sessions_mutex_;
    // Attached-thread call session cache. JNI registered-native and
    // address calls invoked on a guest-attached thread (e.g. il2cpp's
    // System.Reflection Method.invoke, issued tens of thousands of times
    // during CTR content deserialization) previously built a fresh
    // Aarch64GuestSession (1 MiB stack + full Dynarmic jit state) per call.
    // One persistent session per guest thread, reused across calls, removes
    // that per-call alloc/jit-warm cost. A slot marked in_use falls back to a
    // one-shot session so nested attached calls (a reflectively-invoked
    // method that itself invokes Method.invoke) cannot deadlock on
    // call_mutex_.
    std::unordered_map<std::uint64_t, std::shared_ptr<SharedExecutor>>
        attached_call_sessions_;
    std::mutex attached_call_sessions_mutex_;
    // ISOMR_RAISE_PROBE diagnostics: sessions created before libil2cpp
    // loads cannot take raise breakpoints; each Call wrapper lazily
    // installs them on its own session (breakpoints are owner-bound).
    static bool RaiseProbeEnabled();
    void EnsureMainRaiseProbe();
    std::function<bool(Aarch64GuestSession*)> raise_probe_install_fn_;
    std::unordered_set<Aarch64GuestSession*> raise_probe_done_;
    std::mutex raise_probe_mutex_;
    // ISOMR_PC_SAMPLE=1: host-side PC sampler. A detached thread polls every
    // registered session's program_counter() every few ms and bins the PCs
    // that sit inside guest code (i.e. actively executing guest work, not
    // parked in a futex/syscall stub). Periodically dumps the hottest PCs
    // resolved to module+offset. Used to locate where the enormous CTR
    // deserialize spends guest instructions.
    void EnsurePcSampler();
    void PcSamplerLoop();
    bool pc_sampler_started_ = false;
    std::atomic<bool> pc_sampler_stop_{false};
    std::thread pc_sampler_thread_;
    std::mutex pc_sampler_mutex_;
    std::unordered_map<std::uint64_t, std::uint64_t> pc_sampler_hist_;
    std::uint64_t pc_sampler_total_ = 0;
    Aarch64ProfileRuntimeConfig config_;
    std::unique_ptr<Aarch64ExclusiveMonitor> exclusive_monitor_;
    std::unique_ptr<BionicRuntime> runtime_;
    std::unique_ptr<Elf64ModuleSet> modules_;
    std::unique_ptr<Aarch64GuestSession> main_session_;
    std::vector<std::uint64_t> initializers_;
    std::uint64_t entry_point_ = 0;
    bool load_attempted_ = false;
    bool loaded_ = false;
    bool initializers_attempted_ = false;
    bool initializers_ran_ = false;
    bool main_attached_ = false;
    std::thread::id load_thread_{};
};

}  // namespace isomr
