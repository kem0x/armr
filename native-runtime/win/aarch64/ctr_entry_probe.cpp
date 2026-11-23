#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "aarch64_profile_runtime.h"
#include "bionic_runtime.h"
#include "ctr_entry_bridge.h"
#include "game_profile.h"
#include "game_script.h"
#include "native_gles.h"
#include "pvz_inflate_host.h"
#include "ui/game_window.h"

#include <cerrno>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

const std::unordered_set<std::string> kAndroidHostDependencies{
    "libandroid.so", "libc.so",         "libdl.so",
    "libEGL.so",     "libGLESv2.so",   "libGLESv1_CM.so",
    "liblog.so",     "libm.so",        "libmediandk.so",
    "libOpenSLES.so", "libstdc++.so", "libz.so",
    "libjnigraphics.so"};

void ReportFailure(const char* stage, const std::string& error) {
    const std::size_t recent = error.find(" recent=");
    const std::size_t summary_size =
        recent == std::string::npos ? error.size() : recent;
    std::fprintf(stderr,
                 "ISOMR_CTR_FAILURE_SUMMARY stage=%s error=%.*s\n",
                 stage, static_cast<int>(summary_size), error.c_str());
    std::fprintf(stderr,
                 "ISOMR_CTR_ENTRY stage=%s result=fail error=%s\n",
                 stage, error.c_str());
}

bool ParseAddressArgument(std::wstring_view text, std::uint64_t* value) {
    if (text.empty() || value == nullptr) return false;
    const std::wstring owned(text);
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::wcstoull(owned.c_str(), &end, 0);
    if (errno != 0 || end == owned.c_str() || end == nullptr ||
        *end != L'\0' || parsed == 0)
        return false;
    *value = static_cast<std::uint64_t>(parsed);
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Surface what threw when a worker's uncaught exception reaches
    // std::terminate; without this the process dies via fastfail with no
    // diagnostic at all.
    std::set_terminate([]() {
        if (const auto eptr = std::current_exception()) {
            try {
                std::rethrow_exception(eptr);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "ISOMR_TERMINATE what=%s\n", e.what());
            } catch (...) {
                std::fprintf(stderr, "ISOMR_TERMINATE unknown exception\n");
            }
        } else {
            std::fprintf(stderr, "ISOMR_TERMINATE no active exception\n");
        }
        std::fflush(nullptr);
        std::abort();
    });

    // `--game <id>` selects a title through its games/<id>.profile manifest:
    // the profile supplies the payload/data/lib paths plus package identity,
    // engine driver, and per-game feature flags, so the probe carries no
    // game-specific constants. The resolved paths are rewritten into the
    // positional <lib> <payload> <data> form the rest of the launcher uses.
    isomr::GameProfile game_profile;
    bool has_game_profile = false;
    std::wstring resolved_lib, resolved_payload, resolved_data;
    std::wstring resolved_script;
    std::wstring game_compat_path;  // games/<id>.compat (tracked defaults)
    std::vector<std::wstring> opt_storage;
    std::vector<wchar_t*> arg_storage;
    if (argc >= 2 && argv != nullptr && argv[1] != nullptr &&
        std::wcscmp(argv[1], L"--game") == 0) {
        if (argc < 3 || argv[2] == nullptr) {
            std::fputs("usage: ArmrCtrEntryProbe --game <id> "
                       "[native-render-timeout=N] "
                       "[native-instruction-budget=N] "
                       "[breakpoint=<address>] "
                       "[sync-trace] [lane-probe]\n",
                       stderr);
            return 2;
        }
        std::string profile_error;
        const std::wstring profile_path =
            isomr::ResolveGameProfilePath(argv[2]);
        if (profile_path.empty() ||
            !isomr::LoadGameProfile(profile_path, &game_profile,
                                    &profile_error)) {
            std::fprintf(stderr, "game '%ls': %s\n", argv[2],
                         profile_error.c_str());
            return 2;
        }
        has_game_profile = true;
        const std::wstring games_dir = isomr::FindGamesDirectory();
        // games/ sits at the repository root; payload/data dirs are relative
        // to it.
        const std::wstring repo_root = games_dir.substr(
            0, games_dir.size() - std::wstring(L"\\games").size());
        const auto to_wide = [](const std::string& s) {
            std::wstring w;
            w.reserve(s.size());
            for (const char c : s) w.push_back(static_cast<wchar_t>(c));
            return w;
        };
        resolved_payload = repo_root + L"\\" + to_wide(game_profile.payload_dir);
        resolved_data = repo_root + L"\\" + to_wide(game_profile.data_dir);
        resolved_lib =
            resolved_payload + L"\\" + to_wide(game_profile.root_lib);
        // Lifecycle script: profile `script=` names a file under games/;
        // absent it, games/<id>.lua is tried so a script alone opts a game
        // into the scripted path. Missing file = legacy C++ driver path.
        const std::wstring script_name = to_wide(
            game_profile.script.empty() ? game_profile.id + ".lua"
                                        : game_profile.script);
        const std::wstring script_path = games_dir + L"\\" + script_name;
        wchar_t no_script_flag[4] = {};
        const bool script_disabled =
            GetEnvironmentVariableW(L"ARMR_NO_SCRIPT", no_script_flag, 4) >
                0 &&
            no_script_flag[0] != L'0';
        if (!script_disabled &&
            GetFileAttributesW(script_path.c_str()) !=
                INVALID_FILE_ATTRIBUTES)
            resolved_script = script_path;
        const std::wstring compat_path =
            games_dir + L"\\" + to_wide(game_profile.id) + L".compat";
        if (GetFileAttributesW(compat_path.c_str()) !=
            INVALID_FILE_ATTRIBUTES)
            game_compat_path = compat_path;
        arg_storage.push_back(argv[0]);
        arg_storage.push_back(resolved_lib.data());
        arg_storage.push_back(resolved_payload.data());
        arg_storage.push_back(resolved_data.data());
        for (int i = 3; i < argc; ++i) arg_storage.push_back(argv[i]);
        arg_storage.push_back(nullptr);
        argc = static_cast<int>(arg_storage.size()) - 1;
        argv = arg_storage.data();
        std::printf("ISOMR_GAME id=%s name=%s driver=%s lib=%s\n",
                    game_profile.id.c_str(), game_profile.display_name.c_str(),
                    game_profile.driver == isomr::GameDriver::kPvz ? "pvz"
                                                                   : "unity",
                    game_profile.root_lib.c_str());
    }
    if (argc < 4 || argv == nullptr || argv[1] == nullptr ||
        argv[2] == nullptr || argv[3] == nullptr) {
        std::fputs(
            "usage: ArmrCtrEntryProbe <libmain.so> <payload-root> <data-root> "
            "[breakpoint=<address>]\n"
            "   or: ArmrCtrEntryProbe --game <id> [options]\n",
            stderr);
        return 2;
    }

    isomr::Aarch64ProfileRuntimeConfig config;
    // Forensic write watch on the LawnApp object (block size 8112,
    // measured via DescribeRange at ISOMR_STRASSIGN): trap every write
    // to the ResourceManager pointer slot at +0x940 with before/after
    // snapshots and guest pc/lr.
    // The LawnApp+0x940 corruption it trapped is fixed; the watch forces
    // slow-path memory callbacks (10-50x) and PvZ1's load never completes
    // inside a render budget with it on, so frame-flow runs leave it off.
    config.heap_watch_allocation_size = 0;
    config.heap_watch_range_offset = 0;
    config.heap_watch_range_size = 0;
    // Unity repeatedly over-reserves 512 MiB, trims each mapping to a
    // 256 MiB-aligned arena, and commits only the pages it uses. The freed
    // ~256 MiB head/tail holes can never satisfy the next contiguous 512 MiB
    // reservation, so each cycle consumes a fresh stride — ~14 cycles
    // exhausted an 8 GiB arena during CTR's bundle load (run219 ENOMEM).
    // Keep generous sparse virtual address capacity; this is not an eager
    // host allocation.
    config.heap_capacity =
        std::size_t{32} * 1024U * 1024U * 1024U;
    // Optional direct guest-memory mapping. The default heap arena sits at
    // 0x710000000000, far above the range the Dynarmic direct page table can
    // cover, so JIT loads/stores fall back to the checked (locked) memory
    // callbacks for every access. ARMR_GUEST_HEAP_BASE relocates the arena
    // into the low window the table covers (e.g. 0x40000000), letting the
    // JIT hit host memory directly for heap + stack accesses.
    {
        wchar_t heap_base_text[32] = {};
        if (GetEnvironmentVariableW(L"ARMR_GUEST_HEAP_BASE", heap_base_text,
                                    32) > 0) {
            config.heap_base =
                std::wcstoull(heap_base_text, nullptr, 0);
            std::printf("ISOMR_DIRECT_HEAP base=0x%llx\n",
                        static_cast<unsigned long long>(config.heap_base));
        }
    }
    // PvZ1's OBB central-directory parse, EA service init, and the guest
    // zlib inflate of main.rsb (30 MB compressed -> 35 MB raw) are all
    // CPU-heavy; the earlier 100M, 8G, and 4T budgets all exhausted
    // mid-load. The menu-scene deserialize runs inside a single
    // nativeRender call and is a genuinely enormous serial workload
    // (object graph still growing past ~600 MiB at the 4T cap), so give
    // it a much larger per-call budget; tunable via
    // native-instruction-budget=N below.
    config.instruction_budget =
        std::size_t{4096} * 1000U * 1000U * 1000U * 16U;
    // PvZ's EAAudioCore drives the OpenSL ES engine entry points; the
    // timed-discard shim answers them without an audio device.
    config.opensl_mode = isomr::NativeOpenSlMode::kTimedDiscard;
    // Match the accepted Candy launcher default: high-resolution waitable
    // timers. The kSleepFor default rounds every nanosleep/futex-timeout to
    // the ~15.6ms Windows timer tick, which is a uniform per-wait slowdown for
    // a sync-heavy title like CTR.
    config.nanosleep_backend =
        isomr::BionicNanosleepBackend::kHighResolutionWaitable;
    config.file_mounts = {{"/app", argv[2]}, {"/data", argv[3], true}};
    // Unity resolves jar:file://!/assets/... Addressables URLs to the bare
    // in-archive path (/assets/...) rather than sourceDir!entry, so expose
    // the payload's extracted assets tree at the same absolute path. Mounts
    // are longest-prefix matched, so /app and /data still win where they
    // overlap; a missing assets directory simply skips the mount.
    {
        std::wstring assets_root = std::wstring(argv[2]) + L"\\assets";
        config.file_mounts.push_back({"/assets", std::move(assets_root)});
    }
    config.guest_current_directory = "/data";
    config.host_dependencies = kAndroidHostDependencies;
    // Discover sibling libraries from the payload's own lib directory so
    // any game's dependency set loads without per-game probe changes.
    {
        // Discover sibling libraries from the payload's own lib directory
        // so any game's dependency set loads without per-game probe changes.
        const std::wstring pattern =
            std::wstring(argv[2]) + L"\\lib\\arm64-v8a\\*.so";
        WIN32_FIND_DATAW find_data{};
        const HANDLE find = FindFirstFileW(pattern.c_str(), &find_data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    const std::wstring wide(find_data.cFileName);
                    std::string narrow;
                    narrow.reserve(wide.size());
                    for (const wchar_t wc : wide)
                        narrow.push_back(static_cast<char>(wc));
                    // Skip analytics/crash-reporter plugins: they are not in
                    // the root library's dependency chain and several ship
                    // segment layouts the loader need not support.
                    if (narrow.find("crashlytics") != std::string::npos ||
                        narrow.find("applovin") != std::string::npos ||
                        narrow.find("tapjoy") != std::string::npos ||
                        narrow.find("gluads") != std::string::npos)
                        continue;
                    // Skip packed/encrypted payloads (a ".so" that is not a
                    // valid little-endian ELF64). Protected games ship the
                    // real code as an encrypted blob that their own loader
                    // reads+decrypts through the file layer; it is never a
                    // dlopen-able sibling, so registering it just trips the
                    // ELF preflight. Read the magic and skip non-ELF files.
                    {
                        const std::wstring full =
                            std::wstring(argv[2]) + L"\\lib\\arm64-v8a\\" +
                            wide;
                        const HANDLE fh =
                            CreateFileW(full.c_str(), GENERIC_READ,
                                        FILE_SHARE_READ, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                        nullptr);
                        bool is_elf = false;
                        if (fh != INVALID_HANDLE_VALUE) {
                            unsigned char magic[4] = {};
                            DWORD got = 0;
                            if (ReadFile(fh, magic, sizeof(magic), &got,
                                         nullptr) &&
                                got == sizeof(magic))
                                is_elf = magic[0] == 0x7f &&
                                         magic[1] == 'E' && magic[2] == 'L' &&
                                         magic[3] == 'F';
                            CloseHandle(fh);
                        }
                        if (!is_elf) continue;
                    }
                    config.dlopen_sibling_libraries.insert(narrow);
                }
            } while (FindNextFileW(find, &find_data));
            FindClose(find);
        }
    }
    config.fail_closed_unimplemented_imports = true;
    // The dispatch journal serializes every imported-call record (name,
    // args, result string) under a mutex on the hot memcpy/malloc/mutex
    // paths — thousands of contended writes a second during resource
    // streaming. It exists only to feed the watchdog's stall tail dump.
    // Default it off; opt back in with ISOMR_IMPORT_JOURNAL=1.
    config.import_journal_enabled = [] {
        wchar_t flag[8] = {};
        return GetEnvironmentVariableW(L"ISOMR_IMPORT_JOURNAL", flag, 8) > 0 &&
               flag[0] != L'0';
    }();
    // Package identity comes from the selected game profile. The legacy
    // positional launch (no profile) falls back to the CTR default that
    // predated profiles.
    config.jni_compatibility.package_name =
        has_game_profile ? game_profile.package_name : "com.zeptolab.ctr.ads";
    config.jni_compatibility.package_code_path =
        has_game_profile ? game_profile.package_code_path : "/app";
    config.jni_compatibility.package_version =
        has_game_profile && !game_profile.package_version.empty()
            ? game_profile.package_version
            : "4.0.2";
    std::uint64_t native_render_timeout =
        has_game_profile ? game_profile.render_timeout : 600;
    for (int argi = 4; argi < argc; ++argi) {
        constexpr std::wstring_view kBreakpointPrefix = L"breakpoint=";
        constexpr std::wstring_view kRenderTimeoutPrefix =
            L"native-render-timeout=";
        constexpr std::wstring_view kInstructionBudgetPrefix =
            L"native-instruction-budget=";
        const std::wstring_view option(argv[argi]);
        if (option.starts_with(kRenderTimeoutPrefix)) {
            std::uint64_t timeout = 0;
            try {
                timeout = std::stoull(std::wstring(
                    option.substr(kRenderTimeoutPrefix.size())));
            } catch (const std::exception&) {
                timeout = 0;
            }
            if (timeout < 10 || timeout > 86400) {
                std::fputs("invalid native render timeout\n", stderr);
                return 2;
            }
            native_render_timeout = timeout;
            std::printf("ISOMR_CTR_RENDER_TIMEOUT config=%llu\n",
                        static_cast<unsigned long long>(timeout));
        } else if (option.starts_with(kInstructionBudgetPrefix)) {
            std::uint64_t budget = 0;
            try {
                budget = std::stoull(std::wstring(
                    option.substr(kInstructionBudgetPrefix.size())));
            } catch (const std::exception&) {
                budget = 0;
            }
            if (budget == 0) {
                std::fputs("invalid native instruction budget\n", stderr);
                return 2;
            }
            config.instruction_budget = budget;
            std::printf("ISOMR_CTR_INSTRUCTION_BUDGET config=%llu\n",
                        static_cast<unsigned long long>(budget));
        } else if (option.starts_with(kBreakpointPrefix)) {
            if (!ParseAddressArgument(
                    option.substr(kBreakpointPrefix.size()),
                    &config.memory_capture.breakpoint_address)) {
                std::fputs("invalid breakpoint option\n", stderr);
                return 2;
            }
            std::printf(
                "ISOMR_CTR_BREAKPOINT config=address0x%llx\n",
                static_cast<unsigned long long>(
                    config.memory_capture.breakpoint_address));
        } else if (option == L"sync-trace") {
            SetEnvironmentVariableW(L"ISOMR_SYNC_TRACE", L"1");
            std::puts("ISOMR_CTR_SYNC_TRACE config=enabled");
        } else if (option == L"lane-probe") {
            SetEnvironmentVariableW(L"ISOMR_LANE_PROBE", L"1");
            std::puts("ISOMR_CTR_LANE_PROBE config=enabled");
        } else {
            std::fputs("unrecognized option\n", stderr);
            return 2;
        }
    }
    // Host-side zlib inflate for the PvZ payload: the guest's statically
    // linked inflate runs under Dynarmic at ~1/100 host speed, which is the
    // dominant main.rsb load cost. Redirect its entry to a worker-safe SVC
    // stub handled by the resumable host decoder. Default on; set
    // ARMR_HOST_INFLATE=0 to keep guest inflate for A/B comparison.
    // The engine driver comes from the profile; the legacy positional launch
    // falls back to matching the root library filename for "libpvz".
    const bool game_pvz1_early =
        has_game_profile
            ? (game_profile.driver == isomr::GameDriver::kPvz)
            : (std::wstring(argv[1]).find(L"libpvz") != std::wstring::npos);
    const bool host_inflate_enabled =
        has_game_profile ? game_profile.host_inflate : game_pvz1_early;
    if (host_inflate_enabled) {
        wchar_t flag[4] = {};
        const DWORD len =
            GetEnvironmentVariableW(L"ARMR_HOST_INFLATE", flag, 4);
        const bool enabled = !(len > 0 && flag[0] == L'0');
        if (enabled) {
            std::string inflate_error;
            if (!isomr::ConfigurePvzInflateHost(&config, &inflate_error)) {
                std::fprintf(stderr, "ISOMR_HOST_INFLATE config-fail %s\n",
                             inflate_error.c_str());
            } else {
                std::puts("ISOMR_HOST_INFLATE config=on");
            }
        } else {
            std::puts("ISOMR_HOST_INFLATE config=off");
        }
    }
    // The ARMR game window is the native shell that hosts the guest surface.
    // It is created before runtime.Load because gles_.Initialize (inside the
    // load) needs the host window to parent the display child under. The
    // guest logical surface stays fixed; the shell scales the presented
    // image and isolates chrome clicks from guest input. ARMR_GAME_WINDOW=0
    // keeps the legacy standalone window.
    isomr::GameWindow game_window;
    bool game_window_active = false;
    if (has_game_profile &&
        isomr::GameWindow::IsEnabledByEnvironment()) {
        std::int32_t logical_w = 1280, logical_h = 720;
        isomr::NativeGles::ProcessLogicalSurfaceSize(&logical_w, &logical_h);
        std::wstring title;
        title.reserve(game_profile.display_name.size());
        for (const char c : game_profile.display_name)
            title.push_back(static_cast<wchar_t>(c));
        std::string window_error;
        if (game_window.Create(title, logical_w, logical_h, &window_error)) {
            isomr::NativeGles::SetProcessHostWindow(game_window.hwnd());
            isomr::GameWindowCaps caps;
            caps.fullscreen = true;
            // Audio + remapping controls only appear behind a real host
            // capability; the OpenSL timed-discard shim produces no audio
            // sink, so those stay hidden (spec: no dead chrome).
            caps.audio = false;
            caps.input_mapping = false;
            game_window.SetCaps(caps);
            game_window_active = true;
            std::fprintf(stderr,
                         "ISOMR_GAME_WINDOW state=created title=%s\n",
                         game_profile.display_name.c_str());
        } else {
            std::fprintf(stderr,
                         "ISOMR_GAME_WINDOW state=create-fail error=%s\n",
                         window_error.c_str());
        }
    }
    isomr::Aarch64ProfileRuntime runtime(std::move(config));

    std::string error;
    if (!runtime.Load(argv[1], &error)) {
        ReportFailure("load", error);
        return 3;
    }
    std::printf(
        "ISOMR_CTR_ENTRY stage=load result=pass modules=%zu imports=%zu initializers=%zu\n",
        runtime.module_count(), runtime.import_count(),
        runtime.initializer_count());
    if (game_window_active) {
        // The display child now exists under the shell; hand it to the
        // shell so aspect-fit layout can position it.
        game_window.AttachSurface(runtime.bionic()->gles().window_handle());
        std::fprintf(stderr,
                     "ISOMR_GAME_WINDOW state=attached surface=%p\n",
                     static_cast<void*>(
                         runtime.bionic()->gles().window_handle()));
    }
    {
        // Per-game compat policy: data-root/compat-overrides.txt turns
        // unsupported JNI methods into configured results without a
        // rebuild. Absence keeps the fail-closed behavior.
        std::string narrow_data;
        for (const wchar_t* wc = argv[3]; *wc; ++wc)
            narrow_data.push_back(static_cast<char>(*wc));
        // Live activity stream for the ArmrCallView debugger window:
        // one short ISOMR_EV line per guest JNI call, file open/stat,
        // network socket touch, and texture upload. Opt-in via
        // ISOMR_EVENT_STREAM=1 — the per-call fprintf dominates the frame
        // budget on an instrumented run.
        {
            wchar_t ev_flag[8] = {0};
            const DWORD ev_len =
                GetEnvironmentVariableW(L"ISOMR_EVENT_STREAM", ev_flag, 8);
            if (ev_len > 0 && ev_flag[0] == L'1')
                runtime.bionic()->imports().EnableEventStream();
        }
        // Two layers, first match wins: an optional user-local
        // data-root/compat-overrides.txt, then the tracked per-game defaults
        // in games/<id>.compat.
        std::string compat_error;
        std::size_t loaded =
            runtime.bionic()->jni().LoadCompatOverrides(
                narrow_data + "/compat-overrides.txt", &compat_error);
        if (compat_error.empty() && !game_compat_path.empty()) {
            std::string narrow_compat;
            for (const wchar_t wc : game_compat_path)
                narrow_compat.push_back(static_cast<char>(wc));
            loaded = runtime.bionic()->jni().LoadCompatOverrides(
                narrow_compat, &compat_error);
        }
        if (!compat_error.empty()) {
            std::fprintf(stderr, "ISOMR_COMPAT_OVERRIDES error=%s\n",
                         compat_error.c_str());
            return 2;
        }
        std::printf("ISOMR_COMPAT_OVERRIDES loaded=%zu\n", loaded);
        }
    isomr::GameScript game_script;
    if (!resolved_script.empty()) {
        std::string script_path_narrow;
        script_path_narrow.reserve(resolved_script.size());
        for (const wchar_t wc : resolved_script)
            script_path_narrow.push_back(static_cast<char>(wc));
        std::string script_load_error;
        if (!game_script.Load(resolved_script, &script_load_error)) {
            ReportFailure("game-script", script_load_error);
            return 8;
        }
        game_script.BindRuntime(&runtime);
        std::fprintf(stderr,
                     "ISOMR_CTR_ENTRY stage=game-script result=pass "
                     "path=%s\n",
                     script_path_narrow.c_str());
        runtime.bionic()->jni().set_scripted_jni_handler(
            [&game_script](isomr::ScriptedJniContext& ctx) {
                return game_script.TryJniOverride(ctx);
            });
    }
    std::string jni_on_load_error;
    if (!runtime.CallDependencyJniOnLoads(&jni_on_load_error)) {
        ReportFailure("dependency-jni-on-load", jni_on_load_error);
        return 7;
    }
    if (game_script.loaded()) {
        if (game_script.HasHook("pre_initializers") &&
            !game_script.CallHook("pre_initializers", &error)) {
            ReportFailure("pre-initializers", error);
            return 8;
        }
    }
    if (!runtime.RunInitializers(&error)) {
        ReportFailure("initializers", error);
        return 4;
    }
    std::puts("ISOMR_CTR_ENTRY stage=initializers result=pass");

    std::uint64_t version = 0;
    if (!runtime.CallJniOnLoad(&version, &error)) {
        ReportFailure("libmain-jni-on-load", error);
        return 5;
    }
    std::printf(
        "ISOMR_CTR_ENTRY stage=libmain-jni-on-load result=pass version=0x%08llx registered=%zu\n",
        static_cast<unsigned long long>(version),
        runtime.bionic()->jni().registered_native_count());
    for (const auto& method :
         runtime.bionic()->jni().AllRegisteredNatives()) {
        std::printf("ISOMR_CTR_NATIVE class=%s name=%s signature=%s\n",
                    method.class_type.c_str(), method.name.c_str(),
                    method.signature.c_str());
    }

    // Scripted games take over here: the script's main() owns lifecycle,
    // breakpoints, the frame loop, and input. It normally never returns
    // (the watchdog exits the process); a returning main gets the generic
    // shutdown path.
    if (game_script.loaded()) {
        // Hand the configured frame budget to the script's main().
        {
            wchar_t timeout_text[24];
            std::swprintf(timeout_text, 24, L"%llu",
                          static_cast<unsigned long long>(
                              native_render_timeout));
            SetEnvironmentVariableW(L"ARMR_RENDER_TIMEOUT", timeout_text);
        }
        std::string script_error;
        const bool main_ok = game_script.CallHook("main", &script_error);
        // A close on the ARMR game window is a user quit. The running game's
        // guest worker threads sit blocked in emulated syscalls and can never
        // be joined cooperatively, so the orderly BionicThreads teardown
        // would hang forever. Terminate the process and let the OS reap every
        // thread, handle, and the surface/shell window — the correct
        // close=quit path for a real game window.
        if (game_window_active &&
            runtime.bionic()->gles().window_close_requested()) {
            std::fflush(nullptr);
            // Persist host UI prefs + placement before the hard exit; the
            // GameWindow destructor never runs under TerminateProcess.
            game_window.Shutdown();
            TerminateProcess(GetCurrentProcess(), main_ok ? 0u : 8u);
        }
        game_script.RequestStop();
        game_script.JoinThreads();
        if (!main_ok) {
            ReportFailure("script-main", script_error);
            return 8;
        }
        std::string shutdown_error;
        (void)runtime.Shutdown(&shutdown_error);
        return 0;
    }

    isomr::CtrEntryResult result;
    if (!isomr::CtrEntryBridge::LoadUnity(&runtime, "/app", &result,
                                          &error)) {
        ReportFailure("native-loader-load", error);
        return 6;
    }
    const bool has_native_render =
        runtime.bionic()->jni().FindRegisteredNative(
            "com/unity3d/player/UnityPlayer", "nativeRender", "()Z")
            .has_value();
    std::printf(
        "ISOMR_CTR_ENTRY stage=native-loader-load result=pass loaded=%u modules=%zu registered=%zu native-render=%u\n",
        result.unity_loaded ? 1U : 0U, result.module_count,
        result.registered_native_count, has_native_render ? 1U : 0U);
    for (const auto& method :
         runtime.bionic()->jni().RegisteredNativesForClass(
             "com/unity3d/player/UnityPlayer")) {
        std::printf(
            "ISOMR_CTR_NATIVE class=%s name=%s signature=%s\n",
            method.class_type.c_str(), method.name.c_str(),
            method.signature.c_str());
    }
    if (!has_native_render) {
        ReportFailure("unity-registration",
                      "UnityPlayer.nativeRender()Z is unavailable");
        return 7;
    }

    bool rendered = false;
    bool render_call = false;
    std::mutex render_mutex;
    std::condition_variable render_condition;
    bool render_finished = false;
    std::string dispatcher_error;
    std::string render_error;
    // Unity presents through the host window; ANGLE blocks on vsync for
    // hidden windows, so surface it (owner thread) before rendering.
    std::string show_error;
    if (!runtime.bionic()->gles().Show(&show_error)) {
        ReportFailure("window-show", show_error);
        return 8;
    }
    std::printf("ISOMR_CTR_ENTRY stage=window-show result=pass\n");
    std::thread render_thread([&] {
        std::string attach_error;
        const bool attached =
            runtime.bionic()->AttachCurrentThread(&attach_error);
        if (!attached) {
            render_error = "CTR render thread attach failed: " +
                           attach_error;
        } else {
            render_call = isomr::CtrEntryBridge::RenderUnityOnce(
                &runtime, &rendered, &render_error);
            if (render_call) {
                // Unity's lifecycle expects nativeRender to be driven once
                // per frame. Render frames until the process ends; UI tasks
                // are dispatched by the launcher thread between frames.
                std::size_t frames = 1;
                while (render_call) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(16));
                    render_call = isomr::CtrEntryBridge::RenderUnityFrame(
                        &runtime, &rendered, &render_error);
                    if (render_call) ++frames;
                    if (frames % 120 == 0)
                        std::fprintf(stdout,
                            "ISOMR_CTR_FRAMES count=%zu\n", frames);
                }
                std::fprintf(stdout, "ISOMR_CTR_FRAMES total=%zu\n", frames);
            }
        }
        if (attached) {
            std::string detach_error;
            if (!runtime.bionic()->DetachCurrentThread(&detach_error)) {
                if (render_error.empty())
                    render_error = "CTR render thread detach failed: " +
                                   detach_error;
                render_call = false;
            }
        }
        const std::lock_guard lock(render_mutex);
        render_finished = true;
        render_condition.notify_all();
    });

    std::thread watchdog([&] {
        std::unique_lock lock(render_mutex);
        if (render_condition.wait_for(
                lock, std::chrono::seconds(native_render_timeout),
                [&] { return render_finished; }))
            return;
        lock.unlock();
        std::fprintf(
            stderr,
            "ISOMR_CTR_WATCHDOG stage=native-render result=fail "
            "pending-ui=%zu pcs=%s\n",
            runtime.bionic()->jni().pending_ui_task_count(),
            runtime.DescribeWorkerGuestPcs().c_str());
        std::fprintf(
            stderr,
            "ISOMR_CTR_WATCHDOG_THREADS threads=%s imports=%s\n",
            runtime.bionic()->threads().DescribeThreads().c_str(),
            runtime.bionic()
                ->imports()
                .RecentDispatchJournal(32)
                .c_str());
        ReportFailure(
            "native-render-watchdog",
            "nativeRender did not complete within the configured timeout");
        std::fflush(nullptr);
        std::_Exit(8);
    });
    while (true) {
        {
            const std::lock_guard lock(render_mutex);
            if (render_finished) break;
        }
        for (const std::uint64_t native_handle :
             runtime.bionic()->jni().TakeReadyPendingUiTaskHandles()) {
            std::string callback_error;
            if (!isomr::CtrEntryBridge::InvokeRunnable(
                    &runtime, native_handle, &callback_error)) {
                if (dispatcher_error.empty()) {
                    dispatcher_error =
                        "CTR UI Runnable dispatch failed: " +
                        callback_error;
                }
                std::fprintf(
                    stderr,
                    "ISOMR_CTR_UI_TASK result=fail handle=0x%llx error=%s\n",
                    static_cast<unsigned long long>(native_handle),
                    callback_error.c_str());
                std::fprintf(
                    stderr,
                    "ISOMR_CTR_UI_TRACE handle=0x%llx jni-notes=%s recent=%s\n",
                    static_cast<unsigned long long>(native_handle),
                    runtime.bionic()
                        ->imports()
                        .RecentJniSemanticNotesForCurrentThread(32)
                        .c_str(),
                    runtime.bionic()
                        ->imports()
                        .RecentDispatchedNamesForCurrentThread(64)
                        .c_str());
            } else {
                std::printf(
                    "ISOMR_CTR_UI_TASK result=pass handle=0x%llx\n",
                    static_cast<unsigned long long>(native_handle));
            }
        }
        std::unique_lock lock(render_mutex);
        render_condition.wait_for(lock, std::chrono::milliseconds(2),
                                  [&] { return render_finished; });
    }
    render_thread.join();
    watchdog.join();
    {
        if (!dispatcher_error.empty()) {
            ReportFailure("ui-dispatch", dispatcher_error);
            return 8;
        }
    }
    if (!render_call) {
        ReportFailure("native-render", render_error);
        return 8;
    }
    std::printf(
        "ISOMR_CTR_ENTRY stage=native-render result=pass continue=%u\n",
        rendered ? 1U : 0U);

    if (!isomr::CtrEntryBridge::UnloadUnity(&runtime, &error)) {
        ReportFailure("native-loader-unload", error);
        return 9;
    }
    std::puts("ISOMR_CTR_ENTRY stage=native-loader-unload result=pass");
    if (!runtime.Shutdown(&error)) {
        ReportFailure("shutdown", error);
        return 10;
    }
    std::puts("ISOMR_NATIVE_CTR_ENTRY=pass");
    return 0;
}
