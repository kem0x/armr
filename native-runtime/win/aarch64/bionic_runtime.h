#pragma once

#include "bionic_atexit.h"
#include "bionic_assets.h"
#include "bionic_files.h"
#include "bionic_dynamic_loader.h"
#include "bionic_locales.h"
#include "bionic_log.h"
#include "bionic_jumps.h"
#include "bionic_mappings.h"
#include "bionic_process.h"
#include "bionic_random.h"
#include "bionic_signals.h"
#include "bionic_sync.h"
#include "bionic_stdio.h"
#include "bionic_thread_state.h"
#include "bionic_threads.h"
#include "bionic_time.h"
#include "bionic_tls.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "guest_jni.h"
#include "import_registry.h"
#include "native_gles.h"
#include "native_opensles.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace isomr {

class Elf64ModuleSet;

class BionicRuntime {
public:
    BionicRuntime(std::uint64_t heap_base, std::size_t heap_capacity,
                  std::uint64_t import_base,
                  BionicThreads::GuestExecutorFactory executor_factory,
                  std::vector<BionicFileMount> file_mounts = {},
                  std::string current_directory = {},
                  std::size_t import_capacity = 4096,
                  GuestJniBackend* jni_backend = nullptr,
                  std::size_t heap_watch_allocation_size = 0,
                  std::size_t heap_watch_range_offset = 0,
                  std::size_t heap_watch_range_size = 0,
                  bool import_journal_enabled = false,
                  std::unordered_set<std::string>
                      dlopen_sibling_libraries = {},
                  BionicNanosleepBackend nanosleep_backend =
                      BionicNanosleepBackend::kSleepFor,
                  BionicWaitableTimerApi waitable_timer_api = {},
                  bool nanosleep_callsite_telemetry_enabled = false,
                  NativeOpenSlMode opensl_mode = NativeOpenSlMode::kDisabled);

    bool Initialize(std::string* error);
    bool Initialize(const std::vector<std::string>& fail_closed_imports,
                    std::string* error);
    bool AttachModules(Elf64ModuleSet* modules, std::string* error);
    bool AttachCurrentThread(std::string* error);
    bool DetachCurrentThread(std::string* error);
    bool Shutdown(std::string* error);

    GuestHeap& heap() { return heap_; }
    GuestAddressSpace& address_space() { return address_space_; }
    BionicThreadState& thread_state() { return thread_state_; }
    BionicMappings& mappings() { return mappings_; }
    BionicFileSystem& files() { return files_; }
    BionicStdio& stdio() { return stdio_; }
    BionicSignals& signals() { return signals_; }
    BionicTime& time() { return time_; }
    BionicDynamicLoader& dynamic_loader() { return dynamic_loader_; }
    BionicAtExit& at_exit() { return at_exit_; }
    BionicLocales& locales() { return locales_; }
    BionicSync& sync() { return sync_; }
    BionicTls& tls() { return tls_; }
    GuestJni& jni() { return jni_; }
    BionicAssets& assets() { return assets_; }
    BionicLog& log() { return log_; }
    BionicJumps& jumps() { return jumps_; }
    BionicThreads& threads() { return threads_; }
    NativeGles& gles() { return gles_; }
    NativeOpenSl& opensl() { return opensl_; }
    Aarch64ImportRegistry& imports() { return imports_; }
    const Aarch64ImportRegistry& imports() const { return imports_; }

private:
    enum class ShutdownStage : std::uint8_t {
        kOpenSl,
        kJumps,
        kTime,
        kSync,
        kStdio,
        kSignals,
        kMappings,
        kLocales,
        kAssets,
        kJni,
        kProcessIdentity,
        kGles,
        kDynamicLoader,
        kComplete,
    };

    GuestHeap heap_;
    GuestAddressSpace address_space_;
    BionicThreadState thread_state_;
    BionicProcessIdentity process_identity_;
    BionicTime time_;
    BionicFileSystem files_;
    BionicMappings mappings_;
    BionicStdio stdio_;
    BionicSignals signals_;
    BionicDynamicLoader dynamic_loader_;
    BionicAtExit at_exit_;
    BionicLocales locales_;
    BionicRandom random_;
    BionicSync sync_;
    BionicTls tls_;
    GuestJni jni_;
    BionicAssets assets_;
    BionicLog log_;
    BionicJumps jumps_;
    NativeGles gles_;
    Aarch64ImportRegistry imports_;
    BionicThreads threads_;
    NativeOpenSl opensl_;
    bool initialization_attempted_ = false;
    bool initialized_ = false;
    ShutdownStage shutdown_stage_ = ShutdownStage::kOpenSl;
    bool shutdown_ = false;
    // libc data-symbol block (__stack_chk_guard/environ/optarg/...), allocated
    // once at Initialize. Released as the final shutdown stage so a clean
    // teardown drains the guest heap.
    std::uint64_t libc_globals_ = 0;
};

}  // namespace isomr
