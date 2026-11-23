#include "aarch64_profile_runtime.h"

#include "aarch64_guest_session.h"
#include "bionic_runtime.h"
#include "elf64_modules.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
#include <memory>
#include <sstream>
#include <set>
#include <utility>
#include <atomic>

namespace isomr {

extern std::atomic<std::uint64_t> g_armr_instructions_executed;


namespace {
// Attached-thread call sessions register under this base OR'd with the
// caller's errno slot so their keys cannot collide with guest pthread
// handles, which come from the same guest heap allocator.
constexpr std::uint64_t kCallSessionKeyBase = 0x8000000000000000ULL;
}  // namespace
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::string FormatFinalizerContext(const Elf64ModuleSet* modules,
                                   const BionicFinalizer& finalizer) {
    std::ostringstream message;
    message << " function=";
    const std::optional<Elf64ResolvedCodeAddress> resolved =
        modules != nullptr
            ? modules->ResolveCodeAddress(finalizer.function)
            : std::nullopt;
    if (resolved) {
        message << resolved->module << "+0x" << std::hex
                << resolved->offset;
    } else {
        message << "unresolved-0x" << std::hex << finalizer.function;
    }
    message << " argument=0x" << std::hex << finalizer.argument
            << " dso=0x" << finalizer.dso_handle;
    return message.str();
}

bool ValidateCodeSubstitutions(
    const std::vector<Aarch64GuestCodeSubstitution>& substitutions,
    std::string* error) {
    constexpr std::size_t kMaximumSubstitutions = 64;
    if (substitutions.size() > kMaximumSubstitutions) {
        SetError(error, "profile code substitution count exceeds its bound");
        return false;
    }
    std::set<std::string> names;
    std::set<std::pair<std::string, std::uint64_t>> targets;
    for (const Aarch64GuestCodeSubstitution& substitution : substitutions) {
        if (substitution.import_name.empty() ||
            substitution.import_name.size() > 1024 ||
            (substitution.module_offset & 3U) != 0 ||
            !substitution.handler_factory) {
            SetError(error, "profile code substitution is invalid");
            return false;
        }
        if (!names.insert(substitution.import_name).second ||
            !targets
                 .insert({substitution.module,
                          substitution.module_offset})
                 .second) {
            SetError(error, "profile code substitution is duplicated");
            return false;
        }
    }
    return true;
}

bool LaneProbeEnabled() {
    wchar_t flag[8]{};
    return GetEnvironmentVariableW(L"ISOMR_LANE_PROBE", flag, 8) > 0 &&
           flag[0] == L'1';
}

}  // namespace

Aarch64ProfileRuntime::Aarch64ProfileRuntime(
    Aarch64ProfileRuntimeConfig config)
    : config_(std::move(config)) {}

Aarch64ProfileRuntime::~Aarch64ProfileRuntime() {
    pc_sampler_stop_.store(true, std::memory_order_relaxed);
    if (pc_sampler_thread_.joinable()) pc_sampler_thread_.join();
    main_session_.reset();
    if (main_attached_ && runtime_ != nullptr) {
        std::string ignored;
        (void)runtime_->DetachCurrentThread(&ignored);
    }
    runtime_.reset();
    modules_.reset();
}

bool Aarch64ProfileRuntime::RaiseProbeEnabled() {
    wchar_t flag[8]{};
    return GetEnvironmentVariableW(L"ISOMR_RAISE_PROBE", flag, 8) > 0 &&
           flag[0] == L'1';
}

void Aarch64ProfileRuntime::EnsureMainRaiseProbe() {
    if (!raise_probe_install_fn_ || main_session_ == nullptr) return;
    {
        const std::lock_guard lock(raise_probe_mutex_);
        if (raise_probe_done_.contains(main_session_.get())) return;
    }
    if (!raise_probe_install_fn_(main_session_.get())) return;
    const std::lock_guard lock(raise_probe_mutex_);
    raise_probe_done_.insert(main_session_.get());
}

bool Aarch64ProfileRuntime::Load(const std::wstring& root_library,
                                 std::string* error) {
    if (load_attempted_) {
        SetError(error, loaded_ ? "profile runtime is already loaded"
                                : "profile runtime load already failed");
        return false;
    }
    load_attempted_ = true;
    const bool heap_watch_disabled =
        config_.heap_watch_allocation_size == 0 &&
        config_.heap_watch_range_offset == 0 &&
        config_.heap_watch_range_size == 0;
    const bool heap_watch_valid =
        heap_watch_disabled ||
        (config_.heap_watch_allocation_size != 0 &&
         config_.heap_watch_range_size != 0 &&
         config_.heap_watch_range_size <= sizeof(std::uint64_t) &&
         config_.heap_watch_range_offset <=
             config_.heap_watch_allocation_size &&
         config_.heap_watch_range_size <=
             config_.heap_watch_allocation_size -
                 config_.heap_watch_range_offset);
    if (root_library.empty() || config_.heap_capacity == 0 ||
        config_.import_capacity == 0 || config_.stack_size < 4096 ||
        config_.instruction_budget == 0 || !heap_watch_valid) {
        SetError(error, "profile runtime configuration is invalid");
        return false;
    }
    if (!ValidateCodeSubstitutions(config_.code_substitutions, error))
        return false;
    std::set<std::string> requested_imports;
    if (config_.fail_closed_unimplemented_imports) {
        std::uint64_t fallback = 0x6b0000000000ULL;
        Elf64ModuleSet preflight(
            [&](std::string_view name) -> std::optional<std::uint64_t> {
                if (name.empty() || name.size() > 1024 ||
                    !std::all_of(name.begin(), name.end(),
                                 [](unsigned char value) {
                                     return value >= 0x21 && value <= 0x7e;
                                 }))
                    return std::nullopt;
                requested_imports.emplace(name);
                fallback += 8;
                return fallback;
            },
            config_.host_dependencies);
        const Elf64ModuleLoadResult preflight_root = preflight.LoadRoot(
            root_library, config_.first_module_base);
        if (preflight_root.root == nullptr) {
            SetError(error, "profile import preflight failed for root library: " +
                                preflight_root.error);
            return false;
        }
        for (const std::string& sibling :
             config_.dlopen_sibling_libraries) {
            if (sibling.empty() || sibling == "." || sibling == ".." ||
                sibling.find('/') != std::string::npos ||
                sibling.find('\\') != std::string::npos ||
                sibling.find(':') != std::string::npos) {
                SetError(error,
                         "profile preflight sibling name is unsafe");
                return false;
            }
            const Elf64DynamicLoadResult sibling_load =
                preflight.LoadSibling(sibling);
            if (sibling_load.module == nullptr) {
                SetError(error,
                         "profile import preflight failed for sibling " +
                             sibling + ": " + sibling_load.error);
                return false;
            }
            if (!sibling_load.new_modules.empty() &&
                !preflight.CommitDynamicLoad(sibling_load.new_modules,
                                             error)) {
                if (error != nullptr)
                    *error = "profile import preflight sibling " + sibling +
                             " commit failed: " + *error;
                return false;
            }
        }
    }
    exclusive_monitor_ = std::make_unique<Aarch64ExclusiveMonitor>(64);
    const auto executor_factory =
        [this](BionicThreads::GuestExecutorContext context,
               std::string* factory_error) -> BionicThreads::GuestCall {
        if (modules_ == nullptr || runtime_ == nullptr) {
            SetError(factory_error, "profile modules are not loaded");
            return {};
        }
        // Bounded diagnostic: ISOMR_RAISE_PROBE=1 installs raise-path
        // breakpoints on each session so a managed throw logs the
        // exception class/message and managed callsite. Sessions created
        // before libil2cpp loads cannot be hooked yet; they go on a
        // pending list swept by every later Call wrapper invocation.
        static const bool raise_probe = RaiseProbeEnabled();
        const auto install_raise_probe =
            [this](Aarch64GuestSession* session) -> bool {
            // The exported il2cpp_raise_exception is only the public API;
            // managed `throw` sites call the internal raise helper at
            // libil2cpp+0x2674e88 (140 callers in this build), ThrowHelper
            // /icall raises reach vm::Exception::Raise at +0x26758b8, and
            // everything funnels into __cxa_throw. The PLT binds
            // __cxa_throw to the first exporting module in load order (not
            // necessarily libil2cpp's local copy), so read the live GOT.
            const Elf64Image* il2cpp =
                modules_->FindModuleByBasename("libil2cpp.so");
            if (il2cpp == nullptr) return false;
            const std::optional<std::uint64_t> cxa_throw =
                modules_->FindExport("__cxa_throw");
            // The GOT slot for libil2cpp's __cxa_throw JUMP_SLOT is at
            // base+0x5c22a98; its live value is the actually-bound target
            // (may be another module's copy).
            std::uint64_t cxa_bound = 0;
            runtime_->address_space().Read(il2cpp->guest_base() + 0x5c22a98,
                                           &cxa_bound, 8);
            static bool reported = false;
            if (!reported) {
                reported = true;
                std::fprintf(
                    stderr,
                    "ISOMR_RAISE_SETUP cxa_export=%llx cxa_got=%llx\n",
                    static_cast<unsigned long long>(
                        cxa_throw ? *cxa_throw : 0),
                    static_cast<unsigned long long>(cxa_bound));
                for (const Elf64LoadedModule& loaded :
                     modules_->LoadedModulesRootFirst()) {
                    if (loaded.image == nullptr) continue;
                    std::fprintf(
                        stderr, "ISOMR_MODULE %s base=%llx size=%llx\n",
                        loaded.name.c_str(),
                        static_cast<unsigned long long>(
                            loaded.image->guest_base()),
                        static_cast<unsigned long long>(
                            loaded.image->mapped_size()));
                }
                std::fflush(stderr);
            }
            for (std::uint64_t raise_off :
                 {0x2674e88ULL, 0x26758b8ULL, 0x26f72b0ULL}) {
                std::uint64_t raise_address = 0;
                if (raise_off == 0x26f72b0ULL) {
                    raise_address =
                        cxa_bound != 0
                            ? cxa_bound
                            : (cxa_throw ? *cxa_throw : 0);
                } else {
                    raise_address = il2cpp->guest_base() + raise_off;
                }
                if (raise_address == 0) continue;
                {
                    std::string breakpoint_error;
                    session->AddOwnerThreadExecutionBreakpoint(
                        raise_address,
                        [this, raise_off](Aarch64PausedExecutionState* state,
                                    std::string*) {
                            // Every managed throw funnels through raise
                            // thunks. At 0x2674e88 the thunk saved the real
                            // caller LR at *(sp); at 0x26758b8 the caller is
                            // in x30 directly (no save yet). Dedupe on the
                            // callsite so repeated first-chance raises cannot
                            // consume the log budget.
                            static std::set<std::uint64_t> seen_sites;
                            std::uint64_t caller = state->registers[30];
                            if (raise_off == 0x2674e88ULL)
                                runtime_->address_space().Read(
                                    state->stack_pointer, &caller, 8);
                            // x0 is the exception object on the managed
                            // raise paths; at __cxa_throw it is an
                            // Il2CppExceptionWrapper whose first field is
                            // the Il2CppException*.
                            std::uint64_t exception = state->registers[0];
                            if (raise_off == 0x26f72b0ULL) {
                                std::uint64_t inner = 0;
                                if (runtime_->address_space().Read(
                                        exception, &inner, 8) && inner != 0)
                                    exception = inner;
                            }
                            // Dedupe per (callsite, exception type, entry):
                            // __cxa_throw's caller is always Raise, so the
                            // klass keeps distinct exception types visible.
                            std::uint64_t key_klass = 0;
                            runtime_->address_space().Read(
                                exception, &key_klass, 8);
                            const bool deduped =
                                !seen_sites
                                     .insert(caller ^ (key_klass << 1) ^
                                             raise_off)
                                     .second ||
                                seen_sites.size() > 24;
                            if (!deduped) {
                            const auto describe =
                                [this](std::uint64_t address) -> std::string {
                                if (modules_ != nullptr) {
                                    for (const Elf64LoadedModule& loaded :
                                         modules_
                                             ->LoadedModulesRootFirst()) {
                                        if (loaded.image == nullptr)
                                            continue;
                                        const std::uint64_t base =
                                            loaded.image->guest_base();
                                        if (address >= base &&
                                            address - base <
                                                loaded.image
                                                    ->mapped_size()) {
                                            char buf[96];
                                            std::snprintf(
                                                buf, sizeof(buf),
                                                "%s+0x%llx",
                                                loaded.name.c_str(),
                                                static_cast<unsigned long long>(
                                                    address - base));
                                            return buf;
                                        }
                                    }
                                }
                                char raw[24];
                                std::snprintf(raw, sizeof(raw), "0x%llx",
                                              static_cast<unsigned long long>(
                                                  address));
                                return raw;
                            };
                            // Resolve the exception's managed class name:
                            // x0 -> Il2CppException -> klass -> name char*.
                            std::string class_name;
                            std::uint64_t klass = 0;
                            if (runtime_->address_space().Read(
                                    exception, &klass, 8) &&
                                klass != 0) {
                                for (std::uint64_t off = 0x08; off <= 0x58;
                                     off += 8) {
                                    std::uint64_t name_ptr = 0;
                                    if (!runtime_->address_space().Read(
                                            klass + off, &name_ptr, 8) ||
                                        name_ptr < 0x4000000000ULL ||
                                        name_ptr > 0x7fffffffffffULL)
                                        continue;
                                    char name[96]{};
                                    if (runtime_->address_space().Read(
                                            name_ptr, name, sizeof(name) - 1)) {
                                        bool printable = true;
                                        std::size_t len = 0;
                                        for (; len < sizeof(name) - 1 &&
                                               name[len] != '\0'; ++len)
                                            if (name[len] < 0x20 ||
                                                name[len] > 0x7e)
                                                printable = false;
                                        if (printable && len > 3) {
                                            class_name += "|";
                                            class_name += name;
                                        }
                                    }
                                }
                            }
                            // Il2CppException fields begin at +0x10; dump
                            // any Il2CppString fields (_message, _paramName)
                            // as UTF-16LE for the managed throw context.
                            const auto read_guest_string =
                                [this](std::uint64_t str_obj) -> std::string {
                                std::uint32_t len = 0;
                                if (str_obj < 0x4000000000ULL ||
                                    str_obj > 0x7fffffffffffULL ||
                                    !runtime_->address_space().Read(
                                        str_obj + 0x10, &len, 4) ||
                                    len == 0 || len > 512)
                                    return {};
                                std::wstring wide(
                                    len > 128 ? 128 : len, L'\0');
                                if (!runtime_->address_space().Read(
                                        str_obj + 0x14, wide.data(),
                                        wide.size() * 2))
                                    return {};
                                std::string out;
                                for (wchar_t ch : wide)
                                    out += (ch >= 0x20 && ch < 0x7f)
                                               ? static_cast<char>(ch)
                                               : '?';
                                return out;
                            };
                            std::string fields;
                            for (std::uint64_t off = 0x10; off <= 0x40;
                                 off += 8) {
                                std::uint64_t field = 0;
                                if (!runtime_->address_space().Read(
                                        exception + off, &field, 8))
                                    break;
                                const std::string text =
                                    read_guest_string(field);
                                if (!text.empty()) {
                                    fields += " [+" +
                                              std::to_string(off) +
                                              "]=\"" + text + "\"";
                                }
                            }
                            std::string line;
                            char part[220];
                            std::snprintf(
                                part, sizeof(part),
                                "ISOMR_RAISE@%llx x0=%llx class=%s site=%s "
                                "sp=%llx fields=%s",
                                static_cast<unsigned long long>(raise_off),
                                static_cast<unsigned long long>(exception),
                                class_name.c_str(),
                                describe(caller).c_str(),
                                static_cast<unsigned long long>(
                                    state->stack_pointer),
                                fields.c_str());
                            line += part;
                            // il2cpp codegen does not keep a standard x29
                            // chain at the raise entry; scan the live stack
                            // for module-range words (return addresses)
                            // instead.
                            std::array<std::uint64_t, 256> window{};
                            const std::uint64_t sp = state->stack_pointer;
                            std::size_t got = 0;
                            if (runtime_->address_space().Read(
                                    sp, window.data(), sizeof(window))) {
                                for (std::uint64_t word : window) {
                                    if (got >= 16) break;
                                    if (word < 0x4100000000ULL ||
                                        word > 0x4300000000ULL ||
                                        (word & 3U) != 0)
                                        continue;
                                    line += " <" + describe(word) + ">";
                                    ++got;
                                }
                            }
                            std::fprintf(stderr, "%s\n", line.c_str());
                            std::fflush(stderr);
                            }
                            // The breakpoint substitutes the trapped
                            // instruction at compile time: resuming at the
                            // same PC would re-trap forever, so emulate the
                            // displaced prologue instruction and continue
                            // at the next one. This must run even for
                            // deduped hits or the session deadlocks.
                            const std::uint64_t trapped_pc =
                                state->program_counter;
                            state->program_counter = trapped_pc + 4;
                            if (raise_off == 0x2674e88ULL) {
                                // stp x30, x19, [sp, #-0x10]!
                                state->stack_pointer -= 0x10;
                                const std::uint64_t saved_lr =
                                    state->registers[30];
                                const std::uint64_t saved_x19 =
                                    state->registers[19];
                                runtime_->address_space().Write(
                                    state->stack_pointer, &saved_lr, 8);
                                runtime_->address_space().Write(
                                    state->stack_pointer + 8, &saved_x19, 8);
                            } else if (raise_off == 0x26758b8ULL) {
                                // sub sp, sp, #0x60
                                state->stack_pointer -= 0x60;
                            }
                            // 0x26f72b0 (paciasp) executes as a no-op.
                            return true;
                        },
                        &breakpoint_error);
                    }
                }
            // Catalog-decode diagnostic: ReadObjectFromByteArray's UTF-16
            // record read at libil2cpp+0x5292680 is `blr x9` calling
            // Encoding.GetString(bytes,index,count). Capture the failing
            // call's array, index, count and the raw bytes feeding it.
            const std::uint64_t gs_site = il2cpp->guest_base() + 0x5292680ULL;
            {
                std::string breakpoint_error;
                session->AddOwnerThreadExecutionBreakpoint(
                    gs_site,
                    [this](Aarch64PausedExecutionState* state,
                           std::string*) {
                        const std::uint64_t bytes = state->registers[1];
                        const std::uint32_t index =
                            static_cast<std::uint32_t>(state->registers[2]);
                        const std::uint32_t count =
                            static_cast<std::uint32_t>(state->registers[3]);
                        std::uint32_t arr_len = 0;
                        runtime_->address_space().Read(
                            bytes + 0x18, &arr_len, 4);
                        const bool failing =
                            arr_len < index ||
                            static_cast<std::uint64_t>(arr_len - index) <
                                count;
                        if (failing) {
                            std::array<std::uint8_t, 64> dump{};
                            std::size_t got = 0;
                            if (bytes != 0 &&
                                runtime_->address_space().Read(
                                    bytes + 0x20, dump.data(),
                                    dump.size())) {
                                got = dump.size();
                            }
                            std::string hex;
                            for (std::size_t i = 0; i < got; ++i) {
                                char b[4];
                                std::snprintf(b, sizeof(b), "%02x", dump[i]);
                                hex += b;
                            }
                            // Bytes just before the failing index hold the
                            // int32 length prefix this record was built
                            // from.
                            std::array<std::uint8_t, 16> ctx{};
                            std::size_t ctx_got = 0;
                            if (bytes != 0 && index >= 8 &&
                                runtime_->address_space().Read(
                                    bytes + 0x20 + index - 8, ctx.data(),
                                    ctx.size())) {
                                ctx_got = ctx.size();
                            }
                            std::string ctxhex;
                            for (std::size_t i = 0; i < ctx_got; ++i) {
                                char b[4];
                                std::snprintf(b, sizeof(b), "%02x", ctx[i]);
                                ctxhex += b;
                            }
                            std::fprintf(
                                stderr,
                                "ISOMR_GETSTRING_FAIL bytes=%llx "
                                "arr_len=%u index=%u count=%u head=%s "
                                "ctx[-8]=%s\n",
                                static_cast<unsigned long long>(bytes),
                                arr_len, index, count, hex.c_str(),
                                ctxhex.c_str());
                            std::fflush(stderr);
                        }
                        // Emulate `blr x9`: link to pc+4, branch to x9.
                        state->registers[30] = state->program_counter + 4;
                        state->program_counter = state->registers[9];
                        return true;
                    },
                    &breakpoint_error);
            }
            return true;
        };
        // JobQueue lane diagnostic: ISOMR_LANE_PROBE=1 installs an
        // owner-thread execution breakpoint on the job-lane enqueue retry
        // branch at libunity+0x5babf4. The first few hits dump the lane's
        // ring counters, its neighbor lane's wake word, and the stack so a
        // producer-side full-ring spin can be attributed; the breakpoint
        // then removes itself. Default off.
        static const bool lane_probe = LaneProbeEnabled();
        const auto install_lane_probe =
            [this](Aarch64GuestSession* session) -> bool {
            const Elf64Image* unity =
                modules_->FindModuleByBasename("libunity.so");
            if (unity == nullptr) return false;
            const std::uint64_t base = unity->guest_base();
            const std::uint64_t site = base + 0x5babf4ULL;
            const auto describe =
                [this](std::uint64_t address) -> std::string {
                if (modules_ != nullptr) {
                    for (const Elf64LoadedModule& loaded :
                         modules_->LoadedModulesRootFirst()) {
                        if (loaded.image == nullptr) continue;
                        const std::uint64_t mbase =
                            loaded.image->guest_base();
                        if (address >= mbase &&
                            address - mbase <
                                loaded.image->mapped_size()) {
                            char buf[96];
                            std::snprintf(
                                buf, sizeof(buf), "%s+0x%llx",
                                loaded.name.c_str(),
                                static_cast<unsigned long long>(
                                    address - mbase));
                            return buf;
                        }
                    }
                }
                char raw[24];
                std::snprintf(raw, sizeof(raw), "0x%llx",
                              static_cast<unsigned long long>(address));
                return raw;
            };
            std::string breakpoint_error;
            if (!session->AddOwnerThreadExecutionBreakpoint(
                    site,
                    [this, base, site, session,
                     describe](Aarch64PausedExecutionState* state,
                               std::string*) {
                    // Transient ring-full hits happen on several lanes;
                    // the stall signature is consecutive hits on one lane.
                    static std::size_t hits = 0, streak = 0;
                    static std::uint64_t last_lane = 0;
                    static bool retired = false;
                    if (retired) {
                        // Removal takes call_mutex_, which CallEntry holds
                        // while this callback runs — self-deadlock. Keep the
                        // trap armed but emulate the branch and return.
                        state->program_counter = base + 0x5babe0ULL;
                        return true;
                    }
                    ++hits;
                    // Inside the retry loop x20 is lane+0x80 (the ring head
                    // pointer) and x19 the packed handle being pushed.
                    const std::uint64_t ring = state->registers[20];
                    const std::uint64_t lane =
                        ring >= 0x80 ? ring - 0x80 : 0;
                    streak = (lane == last_lane) ? streak + 1 : 0;
                    last_lane = lane;
                    auto& as = runtime_->address_space();
                    const auto r64 = [&as](std::uint64_t a) {
                        std::uint64_t v = 0;
                        as.Read(a, &v, 8);
                        return v;
                    };
                    const auto r32 = [&as](std::uint64_t a) {
                        std::uint32_t v = 0;
                        as.Read(a, &v, 4);
                        return v;
                    };
                    std::string line;
                    char part[160];
                    const std::uint64_t head =
                        lane ? r64(lane + 0x80) : 0;
                    std::snprintf(
                        part, sizeof(part),
                        "ISOMR_LANE hit=%zu lane=0x%llx handle=0x%llx "
                        "x2=0x%llx word=0x%x head=0x%llx tail40=0x%llx "
                        "tail48=0x%llx tag=0x%x flag=0x%x nword=0x%x "
                        "pword=0x%x lr=%s",
                        hits, static_cast<unsigned long long>(lane),
                        static_cast<unsigned long long>(
                            state->registers[19]),
                        static_cast<unsigned long long>(
                            state->registers[2]),
                        lane ? r32(lane) : 0,
                        static_cast<unsigned long long>(head),
                        lane ? r64(lane + 0xc0) : 0,
                        lane ? r64(lane + 0xc8) : 0,
                        lane ? (r32(lane + 0x814c) & 0xffff) : 0,
                        lane ? ((r32(lane + 0x814c) >> 16) & 0xff) : 0,
                        lane ? r32(lane + 0x8180) : 0,
                        lane ? r32(lane - 0x8180) : 0,
                        describe(state->registers[30]).c_str());
                    line += part;
                    // Oldest ring slots: repeated handles would mean the
                    // backlog is duplicated work, not distinct groups.
                    if (lane) {
                        line += " ring=[";
                        for (int slot = 0; slot < 4; ++slot) {
                            std::snprintf(
                                part, sizeof(part), "%s0x%llx",
                                slot ? " " : "",
                                static_cast<unsigned long long>(r64(
                                    lane + 0x100 +
                                    ((head + slot) & 0xfffULL) * 8)));
                            line += part;
                        }
                        line += "]";
                    }
                    // A JobQueue context keeps its lane-array base in field
                    // 0; for lane[0] that base equals the lane itself, so a
                    // stack word pointing at memory whose first field is the
                    // lane is a context candidate. Its +0x48 word counts
                    // active drainers and +0x34 the lane count.
                    std::array<std::uint64_t, 256> window{};
                    if (as.Read(state->stack_pointer, window.data(),
                                sizeof(window))) {
                        int scanned = 0;
                        bool ctx_found = false;
                        for (std::uint64_t word : window) {
                            if (word == lane) {
                                std::snprintf(part, sizeof(part),
                                              " ctxsp=0x%llx",
                                              static_cast<unsigned long long>(
                                                  state->stack_pointer +
                                                  scanned * 8));
                                line += part;
                            } else if (!ctx_found && lane &&
                                       word >= 0x4000000000ULL &&
                                       word <= 0x8000000000ULL &&
                                       (word & 7U) == 0 &&
                                       r64(word) == lane) {
                                ctx_found = true;
                                std::snprintf(
                                    part, sizeof(part),
                                    " ctx=0x%llx drainers=0x%x "
                                    "lanes=0x%x meta=0x%llx",
                                    static_cast<unsigned long long>(word),
                                    r32(word + 0x48), r32(word + 0x34),
                                    static_cast<unsigned long long>(
                                        r64(lane + 0x8140)));
                                line += part;
                            } else if (word >= 0x4100000000ULL &&
                                       word <= 0x4300000000ULL &&
                                       (word & 3U) == 0 &&
                                       line.size() < 1800) {
                                line += " <" + describe(word) + ">";
                            }
                            ++scanned;
                        }
                    }
                    // First hit also dumps the full register file: the
                    // enqueue's caller regs (x21-x28) may still hold the
                    // JobQueue ctx and element pointer.
                    if (hits == 1) {
                        std::string regs = "ISOMR_LANE regs";
                        for (int r = 0; r <= 30; ++r) {
                            std::snprintf(part, sizeof(part), " x%d=0x%llx",
                                          r,
                                          static_cast<unsigned long long>(
                                              state->registers[r]));
                            regs += part;
                        }
                        std::fprintf(stderr, "%s\n", regs.c_str());
                    }
                    // Resolve the head element's live state word. The queue
                    // keeps the element-array base at +8; candidate queue
                    // pointers are callee-saved regs and stack words whose
                    // +8 is itself a plausible heap pointer. A stale head
                    // handle (state seq != handle gen, or bit59 clear)
                    // wedges the ring permanently.
                    if (lane && hits <= 2) {
                        const std::uint64_t head_handle =
                            r64(lane + 0x100 + (head & 0xfffULL) * 8);
                        const std::uint32_t head_gen =
                            static_cast<std::uint32_t>(head_handle >> 32);
                        const std::uint64_t head_idx =
                            head_handle & 0xffffffffULL;
                        std::string extra;
                        const auto try_queue =
                            [&](std::uint64_t q, const char* tag) {
                            if (q < 0x4000000000ULL || q > 0x8000000000ULL ||
                                (q & 7U) != 0)
                                return;
                            const std::uint64_t elem_base = r64(q + 8);
                            if (elem_base < 0x4000000000ULL ||
                                elem_base > 0x8000000000ULL)
                                return;
                            const std::uint64_t elem =
                                elem_base + head_idx * 64;
                            const std::uint64_t estate = r64(elem);
                            const std::uint32_t seq =
                                static_cast<std::uint32_t>(estate);
                            std::snprintf(
                                part, sizeof(part),
                                " %s:q=0x%llx ebase=0x%llx estate=0x%llx"
                                "(seq=0x%x queued=%u refs=%u)%s",
                                tag,
                                static_cast<unsigned long long>(q),
                                static_cast<unsigned long long>(elem_base),
                                static_cast<unsigned long long>(estate),
                                seq,
                                static_cast<unsigned>((estate >> 59) & 1),
                                static_cast<unsigned>((estate >> 32) & 0xff),
                                seq == head_gen ? " MATCH" : " STALE");
                            extra += part;
                        };
                        for (int r = 21; r <= 28; ++r)
                            try_queue(state->registers[r], "reg");
                        for (std::uint64_t word : window)
                            try_queue(word, "sp");
                        line += extra;
                    }
                    std::fprintf(stderr, "%s\n", line.c_str());
                    std::fflush(stderr);
                    // Emulate `b 0x5babe0` (the trapped loop-back branch).
                    state->program_counter = base + 0x5babe0ULL;
                    if (streak >= 5 || hits >= 60) retired = true;
                    return true;
                    },
                    &breakpoint_error)) {
                std::fprintf(stderr, "ISOMR_LANE setup failed: %s\n",
                             breakpoint_error.c_str());
                std::fflush(stderr);
                return false;
            }
            // WaitForJobGroupID drain probe at libunity+0x5b9604: the waiter's
            // work-stealing tail. `tbz w0,#0,#0x5b9644` selects the per-index
            // callback loop (bit0 set) or the group-counter decrement path.
            // Each pass dumps the group state word, the batch range, the
            // current element's claimed counter, and the lane ring so a
            // livelock (repeating range / frozen counter) is distinguishable
            // from a slow serial drain. Throttled; emulates the tbz.
            if (!session->AddOwnerThreadExecutionBreakpoint(
                    base + 0x5b9604ULL,
                    [this, base, describe](Aarch64PausedExecutionState* state,
                                           std::string*) {
                    static std::size_t hits = 0, dumps = 0;
                    static std::int64_t last_start = -1, last_end = -1;
                    static std::uint64_t last_gstate = 0, last_handle = 0;
                    static std::size_t handle_hits = 0;
                    ++hits;
                    const std::uint64_t w0 =
                        state->registers[0] & 0xffffffffULL;
                    state->program_counter =
                        base + ((w0 & 1) ? 0x5b9608ULL : 0x5b9644ULL);
                    const std::uint64_t cur_handle = state->registers[19];
                    if (cur_handle != last_handle) {
                        last_handle = cur_handle;
                        handle_hits = 0;
                    }
                    ++handle_hits;
                    const bool dump =
                        hits <= 6 || handle_hits <= 3 ||
                        (handle_hits % 1024) == 0;
                    if (!dump || dumps >= 120) return true;
                    ++dumps;
                    auto& as = runtime_->address_space();
                    const auto r64 = [&as](std::uint64_t a) {
                        std::uint64_t v = 0;
                        as.Read(a, &v, 8);
                        return v;
                    };
                    const auto r32 = [&as](std::uint64_t a) {
                        std::uint32_t v = 0;
                        as.Read(a, &v, 4);
                        return v;
                    };
                    const std::uint64_t grp = state->registers[20];
                    const std::uint64_t lane = state->registers[21];
                    const std::uint64_t list = r64(grp + 0x20);
                    const std::uint64_t gstate = r64(grp);
                    const std::uint64_t lo = r64(state->stack_pointer + 0x8);
                    const std::int64_t start =
                        static_cast<std::int32_t>(lo >> 32);
                    const std::int64_t end =
                        static_cast<std::int32_t>(lo & 0xffffffffULL);
                    const std::uint64_t elem = state->registers[28];
                    // Resolve the lane's ring-head element state: the queue
                    // ctx (x22, handed to the completion call) keeps the
                    // element-array base at +8; elements stride 64. A stale
                    // head handle (gen mismatch vs element seq) fails the
                    // pop claim check and wedges the ring.
                    const std::uint64_t head = r64(lane + 0x80);
                    const std::uint64_t hhandle = r64(
                        lane + 0x100 + (head & 0xfffULL) * 8);
                    const std::uint64_t ctx = state->registers[22];
                    const std::uint64_t earr = r64(ctx + 8);
                    const std::uint64_t helem =
                        earr + (hhandle & 0xffffffffULL) * 64;
                    const std::uint64_t hestate =
                        (earr >= 0x4000000000ULL &&
                         earr <= 0x8000000000ULL)
                            ? r64(helem) : 0;
                    // Cached Unity CPU-topology globals: the count/flag pair
                    // at libunity+0x134bb90 (JobWorkerCount = count-1 when
                    // flag<=0) and the online-mask at +0x12c1734 (-1 when
                    // the enumeration produced nothing).
                    const std::uint32_t cpu_n = r32(base + 0x134bb90ULL);
                    const std::uint32_t cpu_flag = r32(base + 0x134bb94ULL);
                    const std::uint32_t onl = r32(base + 0x12c1734ULL);
                    // libunity+0x13472c0: global per-CPU entry array built by
                    // 0x7353f0 (count at +0, entries at +0x20 stride 0x20:
                    // +0 capacity float, +8 online flag, +0x10 mask word,
                    // +0x18 cpu-part key for the capacity-class table).
                    std::string cpus;
                    {
                        const std::uint64_t g =
                            r64(base + 0x13472c0ULL);
                        if (g != 0) {
                            const std::uint32_t n = r32(g);
                            char hdr[48];
                            std::snprintf(hdr, sizeof(hdr),
                                          "g=0x%llx n=%u",
                                          static_cast<unsigned long long>(g),
                                          n);
                            cpus = hdr;
                            for (unsigned i = 0;
                                 i < n && i < 6; ++i) {
                                const std::uint64_t e2 =
                                    g + 0x20 + i * 0x20;
                                char row[96];
                                std::snprintf(
                                    row, sizeof(row),
                                    " {%u:f0=%08x f=%02x m=%08x k=%08x}",
                                    i, r32(e2), r32(e2 + 8) & 0xff,
                                    r32(e2 + 0x10), r32(e2 + 0x18));
                                cpus += row;
                            }
                        }
                    }
                    char part[704];
                    std::snprintf(
                        part, sizeof(part),
                        "ISOMR_WAIT hit=%zu hh=%zu grp=0x%llx handle=0x%llx "
                        "gstate=0x%llx(pend=%u done=%u) f18=%u f19=%u "
                        "list=0x%llx l0=%u l8=%u elemarr=0x%llx "
                        "range=%lld..%lld%s%s elem=0x%llx eclaim=0x%llx "
                        "cb=%s lane=0x%llx head=0x%llx tail=0x%llx "
                        "nword=0x%x hhdl=0x%llx ctx=0x%llx earr=0x%llx "
                        "hest=0x%llx(seq=0x%x q=%u refs=%u)%s "
                        "drainers=0x%x ring+1=0x%llx ring+2=0x%llx "
                        "meta8140=0x%llx cpu_n=%d cpu_flag=%d onl=0x%x "
                        "cpus=%s",
                        hits, handle_hits,
                        static_cast<unsigned long long>(grp),
                        static_cast<unsigned long long>(
                            state->registers[19]),
                        static_cast<unsigned long long>(gstate),
                        static_cast<unsigned>((gstate >> 32) & 0xff),
                        static_cast<unsigned>((gstate >> 40) & 0xff),
                        r32(grp + 0x18) & 0xff, r32(grp + 0x19) & 0xff,
                        static_cast<unsigned long long>(list),
                        list ? r32(list) : 0, list ? r32(list + 8) : 0,
                        static_cast<unsigned long long>(
                            list ? r64(list + 0x10) : 0),
                        static_cast<long long>(start),
                        static_cast<long long>(end),
                        (start == last_start && end == last_end)
                            ? " SAME-RANGE" : "",
                        (gstate == last_gstate) ? " GST-SAME" : " GST-MOVED",
                        static_cast<unsigned long long>(elem),
                        (elem >= 0x4000000000ULL &&
                         elem <= 0x8000000000ULL)
                            ? r64(elem) : 0,
                        describe(state->registers[27]).c_str(),
                        static_cast<unsigned long long>(lane),
                        static_cast<unsigned long long>(r64(lane + 0x80)),
                        static_cast<unsigned long long>(r64(lane + 0xc8)),
                        r32(lane + 0x8180),
                        static_cast<unsigned long long>(hhandle),
                        static_cast<unsigned long long>(ctx),
                        static_cast<unsigned long long>(earr),
                        static_cast<unsigned long long>(hestate),
                        static_cast<std::uint32_t>(hestate),
                        static_cast<unsigned>((hestate >> 59) & 1),
                        static_cast<unsigned>((hestate >> 32) & 0xff),
                        (static_cast<std::uint32_t>(hestate) ==
                         static_cast<std::uint32_t>(hhandle >> 32))
                            ? " HMATCH" : " HSTALE",
                        r32(ctx + 0x48),
                        static_cast<unsigned long long>(r64(
                            lane + 0x100 +
                            ((head + 1) & 0xfffULL) * 8)),
                        static_cast<unsigned long long>(r64(
                            lane + 0x100 +
                            ((head + 2) & 0xfffULL) * 8)),
                        static_cast<unsigned long long>(
                            r64(lane + 0x8140)),
                        static_cast<std::int32_t>(cpu_n),
                        static_cast<std::int32_t>(cpu_flag), onl,
                        cpus.c_str());
                    last_start = start; last_end = end;
                    last_gstate = gstate;
                    std::fprintf(stderr, "%s\n", part);
                    std::fflush(stderr);
                    return true;
                    },
                    &breakpoint_error)) {
                std::fprintf(stderr, "ISOMR_WAIT setup failed: %s\n",
                             breakpoint_error.c_str());
                std::fflush(stderr);
            }
            // Per-element callback probe at libunity+0x5b9614 (`blr x27`):
            // fires once per job-element invocation inside the drain loop,
            // exposing the live index w26, the range end at [sp+8], and the
            // element claim word. A corrupted element total shows up as an
            // astronomical end or an index that never reaches it.
            // Emulation: x30 <- 0x5b9618, pc <- x27.
            if (!session->AddOwnerThreadExecutionBreakpoint(
                    base + 0x5b9614ULL,
                    [this, base, describe](Aarch64PausedExecutionState* state,
                                           std::string*) {
                    static std::size_t hits = 0, dumps = 0;
                    static std::uint32_t last_idx = 0xffffffffU;
                    static std::size_t same_idx = 0;
                    ++hits;
                    const std::uint64_t cb = state->registers[27];
                    state->registers[30] = base + 0x5b9618ULL;
                    state->program_counter = cb;
                    const std::uint32_t idx =
                        static_cast<std::uint32_t>(state->registers[26]);
                    if (idx == last_idx) ++same_idx; else same_idx = 0;
                    last_idx = idx;
                    const bool dump = hits <= 4 || (hits % 8192) == 0 ||
                                      (same_idx != 0 &&
                                       (same_idx & (same_idx - 1)) == 0 &&
                                       same_idx >= 64);
                    if (!dump || dumps >= 100) return true;
                    ++dumps;
                    auto& as = runtime_->address_space();
                    const auto r64 = [&as](std::uint64_t a) {
                        std::uint64_t v = 0;
                        as.Read(a, &v, 8);
                        return v;
                    };
                    const auto r32 = [&as](std::uint64_t a) {
                        std::uint32_t v = 0;
                        as.Read(a, &v, 4);
                        return v;
                    };
                    const std::uint64_t elem = state->registers[28];
                    const std::uint64_t grp = state->registers[20];
                    std::fprintf(
                        stderr,
                        "ISOMR_WCB hit=%zu idx=0x%x same=%zu end=0x%x "
                        "elem=0x%llx eclaim=0x%llx grp=0x%llx "
                        "gstate=0x%llx job=0x%llx cb=%s\n",
                        hits, idx, same_idx,
                        r32(state->stack_pointer + 8),
                        static_cast<unsigned long long>(elem),
                        (elem >= 0x4000000000ULL &&
                         elem <= 0x8000000000ULL)
                            ? r64(elem) : 0,
                        static_cast<unsigned long long>(grp),
                        static_cast<unsigned long long>(r64(grp)),
                        static_cast<unsigned long long>(
                            state->registers[23]),
                        describe(cb).c_str());
                    std::fflush(stderr);
                    return true;
                    },
                    &breakpoint_error)) {
                std::fprintf(stderr, "ISOMR_WCB setup failed: %s\n",
                             breakpoint_error.c_str());
                std::fflush(stderr);
            }
            // JobQueue worker-count setter probe at libunity+0x5b9d84:
            // `[ctx+0x48] = min([ctx+0x38], w1)`; a stored -1 suppresses the
            // per-lane kick loop (kick predicate treats it as "all lanes
            // active") while 0 selects inline drain. Fires once during
            // Unity init. Emulates `sub sp, sp, #0x30`.
            if (!session->AddOwnerThreadExecutionBreakpoint(
                    base + 0x5b9d84ULL,
                    [this, base, describe](Aarch64PausedExecutionState* state,
                                           std::string*) {
                    static std::size_t hits = 0;
                    ++hits;
                    const std::uint64_t ctx = state->registers[0];
                    const std::uint32_t req =
                        static_cast<std::uint32_t>(state->registers[1]);
                    auto& as = runtime_->address_space();
                    std::uint32_t maxf = 0, oldf = 0;
                    // Guest heap/arena pointers sit at 0x7100_xxxxxxxx
                    // (~123TB) — far above the 0x8000000000 bound used for
                    // module addresses. as.Read fails cleanly on unmapped
                    // addresses, so accept any plausibly-mapped guest pointer.
                    if (ctx >= 0x4000000000ULL && ctx < 0x800000000000ULL) {
                        as.Read(ctx + 0x38, &maxf, 4);
                        as.Read(ctx + 0x48, &oldf, 4);
                    }
                    std::uint32_t cpu_n = 0, cpu_flag = 0, onl = 0;
                    as.Read(base + 0x134bb90ULL, &cpu_n, 4);
                    as.Read(base + 0x134bb94ULL, &cpu_flag, 4);
                    as.Read(base + 0x12c1734ULL, &onl, 4);
                    std::fprintf(
                        stderr,
                        "ISOMR_SETWC hit=%zu ctx=0x%llx req=0x%x(%d) "
                        "max38=0x%x(%d) old48=0x%x(%d) cpu_n=%d "
                        "cpu_flag=%d onl=0x%x lr=%s\n",
                        hits,
                        static_cast<unsigned long long>(ctx), req,
                        static_cast<std::int32_t>(req), maxf,
                        static_cast<std::int32_t>(maxf), oldf,
                        static_cast<std::int32_t>(oldf),
                        static_cast<std::int32_t>(cpu_n),
                        static_cast<std::int32_t>(cpu_flag), onl,
                        describe(state->registers[30]).c_str());
                    std::fflush(stderr);
                    // ISOMR_CTR_JQMAX=N: A/B override of the JobQueue
                    // max-worker cap at ctx+0x38, applied only when the real
                    // field reads 0 (min(0,req)=0 would force serial inline
                    // drain and strand the spawned lanes). A nonzero cap is
                    // left untouched so Unity's intent is preserved.
                    wchar_t jqmax[16]{};
                    if (maxf == 0 &&
                        GetEnvironmentVariableW(L"ISOMR_CTR_JQMAX", jqmax,
                                                16) > 0) {
                        const std::uint32_t cap =
                            static_cast<std::uint32_t>(_wtoi(jqmax));
                        if (cap > 0 && ctx >= 0x4000000000ULL &&
                            ctx < 0x800000000000ULL) {
                            as.Write(ctx + 0x38, &cap, 4);
                            std::fprintf(stderr,
                                         "ISOMR_CTR_JQMAX wrote "
                                         "ctx+0x38=%u (was 0)\n",
                                         cap);
                            std::fflush(stderr);
                        }
                    }
                    state->stack_pointer -= 0x30;
                    state->program_counter = base + 0x5b9d88ULL;
                    return true;
                    },
                    &breakpoint_error)) {
                std::fprintf(stderr, "ISOMR_SETWC setup failed: %s\n",
                             breakpoint_error.c_str());
                std::fflush(stderr);
            }
            // CPU-topology mask-computer probe at libunity+0x734df4
            // (`ldr w8, [x0]` — the per-core entry array's count field).
            // 0xf770ac fills the 0x20-stride entries from /proc/cpuinfo and
            // /sys/devices/system/cpu; the function derives the
            // (little,big) mask pair that becomes the cached CPU count. Dump
            // the raw entries so a parse failure (count=0, flags clear,
            // keys unrecognised) is distinguishable from a downstream mask
            // bug. Emulates the ldr.
            if (!session->AddOwnerThreadExecutionBreakpoint(
                    base + 0x734df4ULL,
                    [this, base](Aarch64PausedExecutionState* state,
                                 std::string*) {
                    static std::size_t hits = 0;
                    ++hits;
                    const std::uint64_t arr = state->registers[0];
                    auto& as = runtime_->address_space();
                    std::uint32_t count = 0;
                    if (arr >= 0x4000000000ULL && arr <= 0x8000000000ULL)
                        as.Read(arr, &count, 4);
                    std::string entries;
                    for (unsigned i = 0;
                         i < count && i < 12 && entries.size() < 512; ++i) {
                        std::uint8_t raw[0x20] = {};
                        as.Read(arr + i * 0x20, raw, sizeof(raw));
                        char row[160];
                        std::snprintf(
                            row, sizeof(row),
                            " [%u]f0=%08x flg=%02x m10=%08x%08x "
                            "k18=%08x cap=%08x",
                            i,
                            *reinterpret_cast<std::uint32_t*>(raw),
                            raw[8],
                            *reinterpret_cast<std::uint32_t*>(raw + 0x10),
                            *reinterpret_cast<std::uint32_t*>(raw + 0x14),
                            *reinterpret_cast<std::uint32_t*>(raw + 0x18),
                            *reinterpret_cast<std::uint32_t*>(raw + 0x1c));
                        entries += row;
                    }
                    std::fprintf(
                        stderr,
                        "ISOMR_CPUM hit=%zu arr=0x%llx count=%u%s\n",
                        hits, static_cast<unsigned long long>(arr), count,
                        entries.c_str());
                    std::fflush(stderr);
                    std::uint32_t w8 = 0;
                    as.Read(arr, &w8, 4);
                    state->registers[8] = w8;
                    state->program_counter = base + 0x734df8ULL;
                    return true;
                    },
                    &breakpoint_error)) {
                std::fprintf(stderr, "ISOMR_CPUM setup failed: %s\n",
                             breakpoint_error.c_str());
                std::fflush(stderr);
            }
            // Lazy-singleton store probe at libunity+0x65aedc
            // (`stlr x0, [x20]`): the allocator writes the ctor result into
            // the slot word. Dumps the stored pointer plus the slot's
            // allocator params (+8 id, +0x10 context) so a null store is
            // distinguishable from a later clear. Emulates the store via a
            // plain guest write.
            if (!session->AddOwnerThreadExecutionBreakpoint(
                    base + 0x65aedcULL,
                    [this, base](Aarch64PausedExecutionState* state,
                                 std::string*) {
                    static std::size_t hits = 0;
                    ++hits;
                    const std::uint64_t slot = state->registers[20];
                    const std::uint64_t value = state->registers[0];
                    auto& as = runtime_->address_space();
                    std::uint64_t p8 = 0, p10 = 0;
                    as.Read(slot + 8, &p8, 8);
                    as.Read(slot + 0x10, &p10, 8);
                    std::fprintf(
                        stderr,
                        "ISOMR_SLOT hit=%zu slot=0x%llx store=0x%llx "
                        "s+8=0x%llx s+10=0x%llx\n",
                        hits, static_cast<unsigned long long>(slot),
                        static_cast<unsigned long long>(value),
                        static_cast<unsigned long long>(p8),
                        static_cast<unsigned long long>(p10));
                    std::fflush(stderr);
                    as.Write(slot, &value, 8);
                    state->program_counter = base + 0x65aee0ULL;
                    return true;
                    },
                    &breakpoint_error)) {
                std::fprintf(stderr, "ISOMR_SLOT setup failed: %s\n",
                             breakpoint_error.c_str());
                std::fflush(stderr);
            }
            // Cache-gate probe at libunity+0x73549c (`tbz w0, #0` after the
            // 0x76ccdc refresh call on the cpuinfo record cache at
            // buffer+0x408). w0 bit0 decides whether the record-applier
            // 0x7356a8 runs — that applier is what sets each entry's flag
            // byte, and flags gate the mask bits in 0x734df0. Emulates the
            // tbz branch.
            if (!session->AddOwnerThreadExecutionBreakpoint(
                    base + 0x73549cULL,
                    [this, base](Aarch64PausedExecutionState* state,
                                 std::string*) {
                    static std::size_t hits = 0;
                    ++hits;
                    const std::uint32_t w0 =
                        static_cast<std::uint32_t>(state->registers[0]);
                    std::fprintf(stderr,
                                 "ISOMR_CGATE hit=%zu w0=0x%x data=%u\n",
                                 hits, w0, w0 & 1);
                    std::fflush(stderr);
                    if ((w0 & 1) == 0)
                        state->program_counter = base + 0x7354b8ULL;
                    else
                        state->program_counter = base + 0x7354a0ULL;
                    return true;
                    },
                    &breakpoint_error)) {
                std::fprintf(stderr, "ISOMR_CGATE setup failed: %s\n",
                             breakpoint_error.c_str());
                std::fflush(stderr);
            }
            // Record-applier probe at libunity+0x7356a8 entry. Dumps the
            // buffer count and the record-list head ({ptr,count} at [x1] and
            // [x1+0x10]) plus a preview of the first record strings — this is
            // the parsed /proc/cpuinfo line list whose "processor"/"CPU part"
            // records set entry flags and keys. Emulates `sub sp, sp, #0x80`.
            if (!session->AddOwnerThreadExecutionBreakpoint(
                    base + 0x7356a8ULL,
                    [this, base](Aarch64PausedExecutionState* state,
                                 std::string*) {
                    static std::size_t hits = 0;
                    ++hits;
                    const std::uint64_t buf = state->registers[0];
                    const std::uint64_t rec = state->registers[1];
                    auto& as = runtime_->address_space();
                    std::uint32_t n = 0;
                    std::uint64_t rp = 0, rc = 0;
                    as.Read(buf, &n, 4);
                    as.Read(rec, &rp, 8);
                    as.Read(rec + 0x10, &rc, 8);
                    std::string preview;
                    for (unsigned i = 0; i < rc && i < 4; ++i) {
                        std::uint64_t lp = 0, ll = 0;
                        as.Read(rp + i * 0x10, &lp, 8);
                        as.Read(rp + i * 0x10 + 8, &ll, 8);
                        char line[96] = {};
                        const std::size_t take =
                            ll < sizeof(line) - 1 ? static_cast<std::size_t>(ll)
                                                  : sizeof(line) - 1;
                        if (take > 0) as.Read(lp, line, take);
                        preview += " [";
                        preview += line;
                        preview += "]";
                    }
                    std::fprintf(
                        stderr,
                        "ISOMR_HINTS hit=%zu buf=0x%llx n=%u rec=0x%llx "
                        "rp=0x%llx rc=%llu%s\n",
                        hits, static_cast<unsigned long long>(buf), n,
                        static_cast<unsigned long long>(rec),
                        static_cast<unsigned long long>(rp),
                        static_cast<unsigned long long>(rc),
                        preview.c_str());
                    std::fflush(stderr);
                    // emulate `sub sp, sp, #0x80`
                    state->stack_pointer -= 0x80;
                    state->program_counter = base + 0x7356acULL;
                    return true;
                    },
                    &breakpoint_error)) {
                std::fprintf(stderr, "ISOMR_HINTS setup failed: %s\n",
                             breakpoint_error.c_str());
                std::fflush(stderr);
            }
            // Producer-handle probe at libunity+0x76cd18 (`cbz x25` inside the
            // file-provider refresh 0x76ccdc). x25 = the provider's [+0x98]
            // handle — if zero the refresh body is skipped wholesale and the
            // record cache stays empty (the observed data=0 outcome).
            // Emulates the cbz.
            if (!session->AddOwnerThreadExecutionBreakpoint(
                    base + 0x76cd18ULL,
                    [this, base](Aarch64PausedExecutionState* state,
                                 std::string*) {
                    static std::size_t hits = 0;
                    ++hits;
                    const std::uint64_t producer = state->registers[25];
                    auto& as = runtime_->address_space();
                    std::uint64_t ring_lo = 0, ring_hi = 0, src = 0;
                    as.Read(state->registers[19] + 0x160, &ring_lo, 8);
                    as.Read(state->registers[19] + 0x170, &ring_hi, 8);
                    as.Read(state->registers[19] + 0x20, &src, 8);
                    if (hits <= 12) {
                        std::fprintf(
                            stderr,
                            "ISOMR_PROD hit=%zu prov=0x%llx src=0x%llx "
                            "ring=0x%llx/0x%llx\n",
                            hits,
                            static_cast<unsigned long long>(producer),
                            static_cast<unsigned long long>(src),
                            static_cast<unsigned long long>(ring_lo),
                            static_cast<unsigned long long>(ring_hi));
                        std::fflush(stderr);
                    }
                    if (producer == 0)
                        state->program_counter = base + 0x76cdc4ULL;
                    else
                        state->program_counter = base + 0x76cd1cULL;
                    return true;
                    },
                    &breakpoint_error)) {
                std::fprintf(stderr, "ISOMR_PROD setup failed: %s\n",
                             breakpoint_error.c_str());
                std::fflush(stderr);
            }
            return true;
        };
        // Sessions created before libil2cpp loads carry no raise hooks.
        // Breakpoints are owner-thread-bound, so each Call wrapper lazily
        // installs on its own session the first time it runs after the
        // module appears (the wrapper always executes on the session's
        // owner thread).
        const auto ensure_raise_probe =
            [this, install_raise_probe,
             install_lane_probe](Aarch64GuestSession* session) {
                if (!raise_probe && !lane_probe) return;
                {
                    const std::lock_guard lock(raise_probe_mutex_);
                    if (raise_probe_done_.contains(session)) return;
                }
                if (raise_probe && !install_raise_probe(session)) return;
                if (lane_probe && !install_lane_probe(session)) return;
                const std::lock_guard lock(raise_probe_mutex_);
                raise_probe_done_.insert(session);
            };
        if (raise_probe || lane_probe) {
            raise_probe_install_fn_ =
                [install_raise_probe,
                 install_lane_probe](Aarch64GuestSession* session) -> bool {
                    if (raise_probe && !install_raise_probe(session))
                        return false;
                    if (lane_probe && !install_lane_probe(session))
                        return false;
                    return true;
                };
        }
        const auto create_session =
            [this, &context, install_raise_probe,
             ensure_raise_probe](std::string* create_error)
                -> std::shared_ptr<Aarch64GuestSession> {
            Aarch64GuestProcessExitCancellation process_exit_cancellation;
            process_exit_cancellation.requested =
                context.process_exit_cancellation_requested;
            process_exit_cancellation.observed =
                context.process_exit_cancellation_observed;
            static std::atomic<std::uint64_t> create_sess_exec{0};
            const std::uint64_t n_exec = ++create_sess_exec;
            if ((n_exec % 2000) == 1)
                std::fprintf(stderr,
                             "ISOMR_CREATE_SESS exec n=%llu worker=%d "
                             "th=0x%llx stacksize=0x%zx\n",
                             (unsigned long long)n_exec,
                             (int)context.worker_execution,
                             (unsigned long long)context.thread_handle,
                             config_.stack_size);
            std::unique_ptr<Aarch64GuestSession> created =
                Aarch64GuestSession::Create(
                    exclusive_monitor_.get(), modules_.get(),
                    &runtime_->imports(), &runtime_->heap(),
                    context.thread_handle, config_.stack_size,
                    config_.instruction_budget, create_error,
                    config_.memory_capture, context.execution_telemetry,
                    std::move(process_exit_cancellation));
            if (!created) return nullptr;
            if (context.worker_execution) {
                const std::uint64_t worker_slice_instruction_budget =
                    context.execution_slice_ticks != 0
                        ? context.execution_slice_ticks
                        : config_.instruction_budget;
                if (!created->SetOwnerThreadInterSliceCallback(
                        worker_slice_instruction_budget,
                        [](std::string*) { return true; }, create_error,
                        /*renew_total_budget_after_slice=*/true))
                    return nullptr;
            } else {
                static const bool instr_count = [] {
                    wchar_t flag[8] = {};
                    return GetEnvironmentVariableW(
                               L"ISOMR_PC_SAMPLE", flag, 8) > 0 &&
                           flag[0] != L'0';
                }();
                if (instr_count) {
                    // Renewing inter-slice callback forces periodic slice
                    // boundaries so AddTicks ticks the session (measurement).
                    if (!created->SetOwnerThreadInterSliceCallback(
                            20000000ULL, [](std::string*) { return true; },
                            create_error,
                            /*renew_total_budget_after_slice=*/true))
                        return nullptr;
                }
            }
            std::shared_ptr<Aarch64GuestSession> session(std::move(created));
            ensure_raise_probe(session.get());
            RegisterSignalSession(context.thread_handle, session);
            return session;
        };
        if (!context.worker_execution) {
            std::shared_ptr<SharedExecutor> slot;
            {
                const std::lock_guard lock(executor_sessions_mutex_);
                std::shared_ptr<SharedExecutor>& entry =
                    executor_sessions_[context.thread_handle];
                if (!entry) entry = std::make_shared<SharedExecutor>();
                slot = entry;
            }
            if (!slot->in_use.load(std::memory_order_acquire)) {
                if (!slot->session) {
                    slot->session = create_session(factory_error);
                    if (!slot->session) return {};
                }
                std::shared_ptr<Aarch64GuestSession> session = slot->session;
                return [this, session, slot, ensure_raise_probe](
                           std::uint64_t function,
                           const std::array<std::uint64_t, 8>& arguments,
                           std::uint64_t* result,
                           std::string* call_error) {
                    ensure_raise_probe(session.get());
                    if (LaneProbeEnabled()) {
                        static std::set<std::uint64_t> seen_routines;
                        static std::mutex seen_mutex;
                        bool first = false;
                        {
                            const std::lock_guard seen_lock(seen_mutex);
                            first = seen_routines.insert(function).second;
                        }
                        if (first) {
                            std::string name;
                            if (modules_ != nullptr) {
                                for (const Elf64LoadedModule& loaded :
                                     modules_->LoadedModulesRootFirst()) {
                                    if (loaded.image == nullptr) continue;
                                    const std::uint64_t mbase =
                                        loaded.image->guest_base();
                                    if (function >= mbase &&
                                        function - mbase <
                                            loaded.image->mapped_size()) {
                                        char buf[96];
                                        std::snprintf(
                                            buf, sizeof(buf), "%s+0x%llx",
                                            loaded.name.c_str(),
                                            static_cast<unsigned long long>(
                                                function - mbase));
                                        name = buf;
                                        break;
                                    }
                                }
                            }
                            std::fprintf(stderr, "ISOMR_EXEC fn=%s\n",
                                         name.empty() ? "?" : name.c_str());
                            std::fflush(stderr);
                        }
                    }
                    slot->in_use.store(true, std::memory_order_release);
                    struct InUseGuard {
                        std::atomic<bool>* flag;
                        ~InUseGuard() {
                            flag->store(false, std::memory_order_release);
                        }
                    } guard{&slot->in_use};
                    return session->Call(function, arguments, result,
                                         call_error);
                };
            }
            // The shared executor is mid-call on this thread (a comparator
            // invoked a nested bsearch/qsort): fall through to a one-shot
            // session so the nested call cannot deadlock on call_mutex_.
        }
        std::shared_ptr<Aarch64GuestSession> session =
            create_session(factory_error);
        if (!session) return {};
        return [this, session, ensure_raise_probe](
                   std::uint64_t function,
                   const std::array<std::uint64_t, 8>& arguments,
                   std::uint64_t* result, std::string* call_error) {
            ensure_raise_probe(session.get());
            const bool ok =
                session->Call(function, arguments, result, call_error);
            if (LaneProbeEnabled()) {
                // Dedup by routine: log each distinct executor target once so
                // pthread_once initializers (e.g. libunity+0x7538d4, the CPU
                // topology cache fill) are visible in the trace.
                static std::set<std::uint64_t> seen_routines;
                static std::mutex seen_mutex;
                {
                    const std::lock_guard seen_lock(seen_mutex);
                    if (!seen_routines.insert(function).second) return ok;
                }
                std::string name;
                if (modules_ != nullptr) {
                    for (const Elf64LoadedModule& loaded :
                         modules_->LoadedModulesRootFirst()) {
                        if (loaded.image == nullptr) continue;
                        const std::uint64_t mbase =
                            loaded.image->guest_base();
                        if (function >= mbase &&
                            function - mbase <
                                loaded.image->mapped_size()) {
                            char buf[96];
                            std::snprintf(buf, sizeof(buf), "%s+0x%llx",
                                          loaded.name.c_str(),
                                          static_cast<unsigned long long>(
                                              function - mbase));
                            name = buf;
                            break;
                        }
                    }
                }
                std::fprintf(stderr,
                             "ISOMR_EXEC fn=%s ok=%d\n",
                             name.empty() ? "?" : name.c_str(),
                             ok ? 1 : 0);
                std::fflush(stderr);
            }
            return ok;
        };
    };
    runtime_ = std::make_unique<BionicRuntime>(
        config_.heap_base, config_.heap_capacity, config_.import_base,
        executor_factory, config_.file_mounts,
        config_.guest_current_directory, config_.import_capacity,
        config_.jni_backend, config_.heap_watch_allocation_size,
        config_.heap_watch_range_offset, config_.heap_watch_range_size,
        config_.import_journal_enabled, config_.dlopen_sibling_libraries,
        config_.nanosleep_backend, BionicWaitableTimerApi{},
        config_.nanosleep_callsite_telemetry_enabled, config_.opensl_mode);
    // A detached thread's executor/session entries pin an exclusive-monitor
    // processor slot for the whole run; thread churn (il2cpp workers calling
    // guest comparators) would otherwise exhaust the fixed slot capacity.
    runtime_->thread_state().SetDetachObserver(
        [this](std::uint64_t storage) {
            if (storage == 0) return;
            {
                const std::lock_guard lock(executor_sessions_mutex_);
                executor_sessions_.erase(storage);
            }
            {
                const std::lock_guard lock(attached_call_sessions_mutex_);
                attached_call_sessions_.erase(storage);
            }
            {
                const std::lock_guard lock(signal_sessions_mutex_);
                signal_sessions_.erase(storage);
                signal_sessions_.erase(kCallSessionKeyBase | storage);
            }
        });
    for (const Aarch64GuestCodeSubstitution& substitution :
         config_.code_substitutions) {
        Aarch64ImportRegistry::Handler handler =
            substitution.handler_factory(runtime_.get());
        if (!handler) {
            SetError(error,
                     "profile code substitution handler is unavailable");
            return false;
        }
        if (!runtime_->imports().Register(substitution.import_name,
                                          std::move(handler), error))
            return false;
    }
    const std::vector<std::string> fail_closed_imports(
        requested_imports.begin(), requested_imports.end());
    if (!runtime_->jni().ConfigureCompatibility(
            config_.jni_compatibility, error) ||
        !runtime_->Initialize(fail_closed_imports, error))
        return false;
    const auto fail_after_runtime_initialize = [this, error]() {
        std::string failure =
            error != nullptr && !error->empty()
                ? *error
                : "profile runtime load failed after initialization";
        main_session_.reset();
        if (main_attached_) {
            std::string detach_error;
            if (!runtime_->DetachCurrentThread(&detach_error))
                failure += "; detach cleanup failed: " + detach_error;
            main_attached_ = false;
        }
        std::string shutdown_error;
        if (!runtime_->Shutdown(&shutdown_error))
            failure += "; runtime cleanup failed: " + shutdown_error;
        SetError(error, std::move(failure));
        return false;
    };
    modules_ = std::make_unique<Elf64ModuleSet>(
        [this](std::string_view name) {
            return runtime_->imports().Resolve(name);
        },
        config_.host_dependencies);
    const Elf64ModuleLoadResult load =
        modules_->LoadRoot(root_library, config_.first_module_base);
    if (load.root == nullptr) {
        SetError(error, load.error.empty() ? "profile module load failed"
                                           : load.error);
        return fail_after_runtime_initialize();
    }
    for (const Aarch64GuestCodeSubstitution& substitution :
         config_.code_substitutions) {
        const std::optional<std::uint64_t> callable =
            runtime_->imports().ResolveCallable(substitution.import_name);
        const std::uint64_t import_base = runtime_->imports().guest_base();
        constexpr std::uint64_t kImportCellSize = 8;
        if (!callable || *callable < import_base ||
            (*callable - import_base) % kImportCellSize != 0) {
            SetError(error,
                     "profile code substitution import is unresolved");
            return fail_after_runtime_initialize();
        }
        const std::uint64_t index =
            (*callable - import_base) / kImportCellSize;
        if (index >= 65535) {
            SetError(error,
                     "profile code substitution immediate is out of range");
            return fail_after_runtime_initialize();
        }
        const std::uint32_t immediate =
            static_cast<std::uint32_t>(index + 1);
        const std::array<std::uint32_t, 2> replacement{
            0xd4000001U | (immediate << 5), 0xd65f03c0U};
        if (!modules_->InstallCodeSubstitution(
                substitution.module, substitution.module_offset,
                substitution.expected, replacement, error)) {
            if (error != nullptr)
                *error = "profile code substitution " +
                         substitution.import_name + " failed: " + *error;
            return fail_after_runtime_initialize();
        }
    }
    entry_point_ = load.root->entry_point();
    if (!runtime_->AttachModules(modules_.get(), error) ||
        !runtime_->AttachCurrentThread(error))
        return fail_after_runtime_initialize();
    main_attached_ = true;
    const std::optional<std::uint64_t> thread_pointer =
        runtime_->thread_state().CurrentErrnoAddress();
    if (!thread_pointer) {
        SetError(error, "profile main thread has no guest thread pointer");
        return fail_after_runtime_initialize();
    }
    Aarch64GuestProcessExitCancellation process_exit_cancellation;
    BionicThreads* threads = &runtime_->threads();
    process_exit_cancellation.requested = [threads] {
        return threads->process_exit_requested();
    };
    process_exit_cancellation.observed = [] {};
    main_session_ = Aarch64GuestSession::Create(
        exclusive_monitor_.get(), modules_.get(), &runtime_->imports(),
        &runtime_->heap(),
        *thread_pointer, config_.stack_size, config_.instruction_budget,
        error, config_.memory_capture, nullptr,
        std::move(process_exit_cancellation));
    if (!main_session_) return fail_after_runtime_initialize();
    {
        static const bool instr_count = [] {
            wchar_t flag[8] = {};
            return GetEnvironmentVariableW(L"ISOMR_PC_SAMPLE", flag, 8) > 0 &&
                   flag[0] != L'0';
        }();
        if (instr_count) {
            std::string ignored;
            (void)main_session_->SetOwnerThreadInterSliceCallback(
                20000000ULL, [](std::string*) { return true; }, &ignored,
                /*renew_total_budget_after_slice=*/true);
        }
    }
    EnsureMainRaiseProbe();
    EnsurePcSampler();
    initializers_ = modules_->Initializers();
    load_thread_ = std::this_thread::get_id();
    loaded_ = true;
    return true;
}

bool Aarch64ProfileRuntime::RunInitializers(std::string* error) {
    if (!loaded_ || main_session_ == nullptr || initializers_attempted_) {
        SetError(error, initializers_attempted_
                            ? "profile initializers already attempted"
                            : "profile runtime is not loaded");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error, "profile runtime main session belongs to its load thread");
        return false;
    }
    initializers_attempted_ = true;
    const std::array<std::uint64_t, 8> arguments{};
    for (std::size_t index = 0; index < initializers_.size(); ++index) {
        std::uint64_t ignored = 0;
        std::fprintf(stdout, "ISOMR_CTR_INIT addr=0x%llx index=%zu\n",
            (unsigned long long)initializers_[index], index);
        EnsureMainRaiseProbe();
        if (!main_session_->Call(initializers_[index], arguments, &ignored,
                                 error)) {
            if (error != nullptr)
                *error = "profile initializer " + std::to_string(index) +
                         " at addr 0x" +
                         ([](std::uint64_t a) {
                             char b[24];
                             snprintf(b, sizeof(b), "%llx", a);
                             return std::string(b);
                         })(initializers_[index]) +
                         " failed: " + *error;
            return false;
        }
    }
    initializers_ran_ = true;
    return true;
}

std::optional<std::uint64_t> Aarch64ProfileRuntime::FindExport(
    std::string_view name) const {
    return loaded_ && modules_ != nullptr ? modules_->FindExport(name)
                                          : std::nullopt;
}

bool Aarch64ProfileRuntime::Call(
    std::uint64_t function,
    const std::array<std::uint64_t, 8>& arguments, std::uint64_t* result,
    std::string* error) {
    if (!loaded_ || !initializers_ran_ || main_session_ == nullptr) {
        SetError(error, "profile runtime is not initialized for calls");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error, "profile runtime main session belongs to its load thread");
        return false;
    }
    EnsureMainRaiseProbe();
    return main_session_->Call(function, arguments, result, error);
}

bool Aarch64ProfileRuntime::CallEntry(
    std::uint64_t function,
    const std::vector<Aarch64CallArgument>& arguments,
    Aarch64CallResult* result, std::string* error) {
    if (!loaded_ || !initializers_ran_ || main_session_ == nullptr) {
        SetError(error, "profile runtime is not initialized for calls");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error, "profile runtime main session belongs to its load thread");
        return false;
    }
    EnsureMainRaiseProbe();
    return main_session_->CallEntry(function, arguments, result, error);
}

bool Aarch64ProfileRuntime::CallExport(
    std::string_view name,
    const std::array<std::uint64_t, 8>& arguments, std::uint64_t* result,
    std::string* error) {
    if (loaded_ && std::this_thread::get_id() != load_thread_) {
        SetError(error, "profile runtime main session belongs to its load thread");
        return false;
    }
    const std::optional<std::uint64_t> function = FindExport(name);
    if (!function) {
        SetError(error, "profile export is unavailable: " +
                            std::string(name));
        return false;
    }
    return Call(*function, arguments, result, error);
}

bool Aarch64ProfileRuntime::CallExportEntry(
    std::string_view name,
    const std::vector<Aarch64CallArgument>& arguments,
    Aarch64CallResult* result, std::string* error) {
    if (loaded_ && std::this_thread::get_id() != load_thread_) {
        SetError(error, "profile runtime main session belongs to its load thread");
        return false;
    }
    const std::optional<std::uint64_t> function = FindExport(name);
    if (!function) {
        SetError(error, "profile export is unavailable: " +
                            std::string(name));
        return false;
    }
    return CallEntry(*function, arguments, result, error);
}

bool Aarch64ProfileRuntime::CallRegisteredNative(
    std::string_view class_type, std::string_view name,
    std::string_view signature,
    const std::vector<Aarch64CallArgument>& arguments,
    Aarch64CallResult* result, std::string* error) {
    if (!loaded_ || !initializers_ran_ || runtime_ == nullptr ||
        main_session_ == nullptr) {
        SetError(error,
                 "profile runtime is not initialized for JNI native calls");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error,
                 "profile runtime main session belongs to its load thread");
        return false;
    }
    const std::optional<GuestJni::RegisteredNativeHandle> handle =
        runtime_->jni().FindRegisteredNative(class_type, name, signature);
    if (!handle) {
        SetError(error, "registered guest JNI native is unavailable: " +
                            std::string(class_type) + "." +
                            std::string(name) + std::string(signature));
        return false;
    }
    std::uint64_t function = 0;
    if (!runtime_->jni().ResolveRegisteredNativeTarget(*handle, &function,
                                                        error))
        return false;
    EnsureMainRaiseProbe();
    return main_session_->CallEntry(function, arguments, result, error);
}

bool Aarch64ProfileRuntime::CallRegisteredNativeOnAttachedThread(
    std::string_view class_type, std::string_view name,
    std::string_view signature,
    const std::vector<Aarch64CallArgument>& arguments,
    Aarch64CallResult* result, std::string* error) {
    if (!loaded_ || !initializers_ran_ || runtime_ == nullptr ||
        modules_ == nullptr || exclusive_monitor_ == nullptr) {
        SetError(error,
                 "profile runtime is not initialized for attached JNI native calls");
        return false;
    }
    if (std::this_thread::get_id() == load_thread_)
        return CallRegisteredNative(class_type, name, signature, arguments,
                                    result, error);
    const std::optional<std::uint64_t> thread_pointer =
        runtime_->thread_state().CurrentErrnoAddress();
    if (!thread_pointer || !runtime_->jni().CurrentEnvironmentAddress()) {
        SetError(error,
                 "profile runtime secondary calling thread is not attached");
        return false;
    }
    const std::optional<GuestJni::RegisteredNativeHandle> handle =
        runtime_->jni().FindRegisteredNative(class_type, name, signature);
    if (!handle) {
        SetError(error, "registered guest JNI native is unavailable: " +
                            std::string(class_type) + "." +
                            std::string(name) + std::string(signature));
        return false;
    }
    std::uint64_t function = 0;
    if (!runtime_->jni().ResolveRegisteredNativeTarget(*handle, &function,
                                                        error))
        return false;
    static std::atomic<std::uint64_t> call_sess_regnat{0};
    const std::uint64_t n_rn = ++call_sess_regnat;
    if ((n_rn % 20000) == 1) {
        std::fprintf(stderr,
                     "ISOMR_CALL_SESS regnat n=%llu tp=0x%llx "
                     "fn=%.*s%.*s nargs=%zu",
                     (unsigned long long)n_rn,
                     (unsigned long long)*thread_pointer,
                     (int)name.size(), name.data(),
                     (int)signature.size(), signature.data(),
                     arguments.size());
        for (std::size_t ai = 0; ai < arguments.size() && ai < 6; ++ai)
            std::fprintf(stderr, " a%zu=%llx", ai,
                         (unsigned long long)arguments[ai].bits[0]);
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
    }
    // Observe the process-wide exit flag so a guest exit()/abort() inside a
    // registered-native call halts this session instead of returning to the
    // guest (where _exit must not return). The per-thread-pointer session is
    // cached so reflective natives (Method.invoke) don't rebuild a 1 MiB
    // session per call.
    return CallEntryOnAttachedSession(*thread_pointer, function, arguments,
                                      result, error);
}

bool Aarch64ProfileRuntime::CallAddressOnAttachedThread(
    std::uint64_t function,
    const std::vector<Aarch64CallArgument>& arguments,
    Aarch64CallResult* result, std::string* error) {
    if (!loaded_ || runtime_ == nullptr || modules_ == nullptr ||
        exclusive_monitor_ == nullptr) {
        SetError(error,
                 "profile runtime is not initialized for attached-thread calls");
        return false;
    }
    if (function == 0) {
        SetError(error, "attached-thread call target is null");
        return false;
    }
    if (std::this_thread::get_id() == load_thread_) {
        if (main_session_ == nullptr) {
            SetError(error, "profile runtime main session is unavailable");
            return false;
        }
        EnsureMainRaiseProbe();
        return main_session_->CallEntry(function, arguments, result, error);
    }
    const std::optional<std::uint64_t> thread_pointer =
        runtime_->thread_state().CurrentErrnoAddress();
    if (!thread_pointer || !runtime_->jni().CurrentEnvironmentAddress()) {
        SetError(error,
                 "profile runtime secondary calling thread is not attached");
        return false;
    }
    // Reuse the per-thread-pointer attached-call session so each call does
    // not rebuild a 1 MiB guest stack + Dynarmic jit state.
    return CallEntryOnAttachedSession(*thread_pointer, function, arguments,
                                      result, error);
}

std::shared_ptr<Aarch64GuestSession>
Aarch64ProfileRuntime::CreateAttachedCallSession(std::uint64_t thread_pointer,
                                                 bool register_session,
                                                 std::string* error) {
    Aarch64GuestProcessExitCancellation process_exit_cancellation;
    BionicThreads* threads = &runtime_->threads();
    process_exit_cancellation.requested = [threads] {
        return threads->process_exit_requested();
    };
    process_exit_cancellation.observed = [] {};
    std::unique_ptr<Aarch64GuestSession> created =
        Aarch64GuestSession::Create(
            exclusive_monitor_.get(), modules_.get(), &runtime_->imports(),
            &runtime_->heap(), thread_pointer, config_.stack_size,
            config_.instruction_budget, error, config_.memory_capture, {},
            std::move(process_exit_cancellation));
    if (!created) return nullptr;
    std::shared_ptr<Aarch64GuestSession> session(std::move(created));
    if (raise_probe_install_fn_)
        raise_probe_install_fn_(session.get());
    if (register_session)
        RegisterSignalSession(kCallSessionKeyBase | thread_pointer, session);
    return session;
}

bool Aarch64ProfileRuntime::CallEntryOnAttachedSession(
    std::uint64_t thread_pointer, std::uint64_t function,
    const std::vector<Aarch64CallArgument>& arguments,
    Aarch64CallResult* result, std::string* error) {
    std::shared_ptr<SharedExecutor> slot;
    {
        const std::lock_guard lock(attached_call_sessions_mutex_);
        std::shared_ptr<SharedExecutor>& entry =
            attached_call_sessions_[thread_pointer];
        if (!entry) entry = std::make_shared<SharedExecutor>();
        slot = entry;
    }
    std::shared_ptr<Aarch64GuestSession> session;
    const bool cached = !slot->in_use.load(std::memory_order_acquire);
    if (cached) {
        if (!slot->session) {
            slot->session =
                CreateAttachedCallSession(thread_pointer, true, error);
            if (!slot->session) return false;
        }
        session = slot->session;
    } else {
        // Re-entrant attached call while this thread's shared session is
        // mid-CallEntry (e.g. a reflectively-invoked method that itself
        // invokes Method.invoke): use a one-shot session so the nested call
        // cannot deadlock on call_mutex_. Not registered so it doesn't
        // clobber the cached primary session's signal registration.
        session = CreateAttachedCallSession(thread_pointer, false, error);
        if (!session) return false;
    }
    struct InUseGuard {
        std::atomic<bool>* flag;
        ~InUseGuard() { flag->store(false, std::memory_order_release); }
    };
    std::unique_ptr<InUseGuard> guard;
    if (cached) {
        slot->in_use.store(true, std::memory_order_release);
        guard = std::make_unique<InUseGuard>(InUseGuard{&slot->in_use});
    }
    return session->CallEntry(function, arguments, result, error);
}

bool Aarch64ProfileRuntime::CallDependencyJniOnLoads(std::string* error) {
    if (runtime_ == nullptr || runtime_->jni().vm_address() == 0) {
        SetError(error, "profile JavaVM is unavailable");
        return false;
    }
    // Dependency JNI_OnLoad runs BEFORE the root initializers by design
    // (Android loadLibrary order), so the initializers_ran_ gate does not
    // apply here; the main session is already live at this point.
    std::array<std::uint64_t, 8> arguments{};
    arguments[0] = runtime_->jni().vm_address();
    // Non-root module images: call JNI_OnLoad where exported, matching
    // Android's System.loadLibrary behavior for engine dependency libs.
    for (const auto& loaded : modules_->LoadedModulesRootFirst()) {
        if (loaded.name.find("libpvz") != std::string::npos &&
            loaded.name.find("libmain") == std::string::npos)
            continue;  // root library handled by CallJniOnLoad
        const auto function =
            loaded.image ? loaded.image->FindExport("JNI_OnLoad") : std::nullopt;
        if (!function) continue;
        std::uint64_t version = 0;
        std::string call_error;
        EnsureMainRaiseProbe();
        const bool call_ok = main_session_ != nullptr &&
            main_session_->Call(*function, arguments, &version,
                                &call_error);
        if (!call_ok) {
            SetError(error, "dependency JNI_OnLoad failed in " +
                                loaded.name + ": " + call_error);
            return false;
        }
        std::fprintf(stdout,
            "ISOMR_CTR_ENTRY stage=dependency-jni-on-load result=pass module=%s version=0x%08llx\n",
            loaded.name.c_str(), (unsigned long long)version);
    }
    return true;
}

bool Aarch64ProfileRuntime::CallJniOnLoad(std::uint64_t* version,
                                          std::string* error) {
    if (runtime_ == nullptr || runtime_->jni().vm_address() == 0) {
        SetError(error, "profile JavaVM is unavailable");
        return false;
    }
    // Android invokes the dlopen'd library's own JNI_OnLoad. load_order_
    // finishes dependencies before the root, so name-wide lookup can shadow
    // the root's export with a dependency's (libNimble shadowed libpvz).
    const std::optional<std::uint64_t> function =
        loaded_ && modules_ != nullptr
            ? modules_->FindRootExport("JNI_OnLoad")
            : std::nullopt;
    if (!function) {
        SetError(error, "root library does not export JNI_OnLoad");
        return false;
    }
    std::array<std::uint64_t, 8> arguments{};
    arguments[0] = runtime_->jni().vm_address();
    return Call(*function, arguments, version, error);
}

bool Aarch64ProfileRuntime::SetMainInstructionBudgetForNextCall(
    std::uint64_t instruction_budget, std::string* error) {
    if (!loaded_ || !initializers_ran_ || main_session_ == nullptr) {
        SetError(error, "profile runtime is not initialized for calls");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error,
                 "profile runtime main session belongs to its load thread");
        return false;
    }
    return main_session_->SetInstructionBudgetForNextCall(
        instruction_budget, error);
}

bool Aarch64ProfileRuntime::SetMainOwnerThreadInterSliceCallback(
    std::uint64_t slice_instruction_budget,
    Aarch64OwnerThreadInterSliceCallback callback,
    std::string* error) {
    if (!loaded_ || !initializers_ran_ || main_session_ == nullptr) {
        SetError(error, "profile runtime is not initialized for calls");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error,
                 "profile runtime main session belongs to its load thread");
        return false;
    }
    return main_session_->SetOwnerThreadInterSliceCallback(
        slice_instruction_budget, std::move(callback), error);
}

bool Aarch64ProfileRuntime::SetMainOwnerThreadInterSliceBudgetForNextSlice(
    std::uint64_t slice_instruction_budget, std::string* error) {
    if (!loaded_ || !initializers_ran_ || main_session_ == nullptr) {
        SetError(error, "profile runtime is not initialized for calls");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error,
                 "profile runtime main session belongs to its load thread");
        return false;
    }
    return main_session_->SetOwnerThreadInterSliceBudgetForNextSlice(
        slice_instruction_budget, error);
}

void Aarch64ProfileRuntime::ClearMainOwnerThreadInterSliceCallback() {
    if (main_session_ != nullptr)
        main_session_->ClearOwnerThreadInterSliceCallback();
}

bool Aarch64ProfileRuntime::SetMainOwnerThreadExecutionBreakpoint(
    std::uint64_t guest_address,
    Aarch64OwnerThreadExecutionBreakpointCallback callback,
    std::string* error) {
    if (!loaded_ || !initializers_ran_ || main_session_ == nullptr) {
        SetError(error, "profile runtime is not initialized for calls");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error,
                 "profile runtime main session belongs to its load thread");
        return false;
    }
    return main_session_->SetOwnerThreadExecutionBreakpoint(
        guest_address, std::move(callback), error);
}

void Aarch64ProfileRuntime::ClearMainOwnerThreadExecutionBreakpoint() {
    if (main_session_ != nullptr)
        main_session_->ClearOwnerThreadExecutionBreakpoint();
}

bool Aarch64ProfileRuntime::AddMainOwnerThreadExecutionBreakpoint(
    std::uint64_t guest_address,
    Aarch64OwnerThreadExecutionBreakpointCallback callback,
    std::string* error) {
    if (!loaded_ || !initializers_ran_ || main_session_ == nullptr) {
        SetError(error, "profile runtime is not initialized for calls");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error,
                 "profile runtime main session belongs to its load thread");
        return false;
    }
    return main_session_->AddOwnerThreadExecutionBreakpoint(
        guest_address, std::move(callback), error);
}

void Aarch64ProfileRuntime::RemoveMainOwnerThreadExecutionBreakpoint(
    std::uint64_t guest_address) {
    if (main_session_ != nullptr)
        main_session_->RemoveOwnerThreadExecutionBreakpoint(guest_address);
}

bool Aarch64ProfileRuntime::CaptureMainGuestPcTelemetrySample(
    Aarch64GuestCallTelemetry* telemetry, std::string* error) const {
    if (!loaded_ || !initializers_ran_ || main_session_ == nullptr) {
        SetError(error, "profile runtime is not initialized for calls");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error,
                 "profile runtime main session belongs to its load thread");
        return false;
    }
    return main_session_->CaptureGuestPcTelemetrySample(telemetry, error);
}

bool Aarch64ProfileRuntime::SetMainCallTelemetryEnabled(
    bool enabled, std::string* error) {
    return SetMainCallTelemetryMode(
        enabled ? Aarch64CallTelemetryMode::kFull
                : Aarch64CallTelemetryMode::kOff,
        error);
}

bool Aarch64ProfileRuntime::SetMainCallTelemetryMode(
    Aarch64CallTelemetryMode mode, std::string* error) {
    if (mode != Aarch64CallTelemetryMode::kOff &&
        mode != Aarch64CallTelemetryMode::kEnvelope &&
        mode != Aarch64CallTelemetryMode::kFull &&
        mode != Aarch64CallTelemetryMode::kSvcCategory) {
        SetError(error, "profile runtime call telemetry mode is invalid");
        return false;
    }
    if (!loaded_ || !initializers_ran_ || main_session_ == nullptr) {
        SetError(error, "profile runtime is not initialized for calls");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error,
                 "profile runtime main session belongs to its load thread");
        return false;
    }
    main_session_->SetCallTelemetryMode(mode);
    return true;
}

Aarch64GuestCallTelemetry Aarch64ProfileRuntime::last_call_telemetry() const {
    return main_session_ != nullptr
               ? main_session_->last_call_telemetry()
               : Aarch64GuestCallTelemetry{};
}

NanosleepCallsiteTelemetryToken
Aarch64ProfileRuntime::BeginNanosleepCallsiteTelemetry() noexcept {
    return runtime_ != nullptr
               ? runtime_->time().BeginNanosleepCallsiteTelemetry()
               : NanosleepCallsiteTelemetryToken{};
}

NanosleepCallsiteTelemetrySnapshot
Aarch64ProfileRuntime::EndNanosleepCallsiteTelemetry(
    NanosleepCallsiteTelemetryToken token) noexcept {
    return runtime_ != nullptr
               ? runtime_->time().EndNanosleepCallsiteTelemetry(token)
               : NanosleepCallsiteTelemetrySnapshot{};
}

std::optional<Aarch64ProfileRuntime::ResolvedGuestCodeAddress>
Aarch64ProfileRuntime::ResolveGuestCodeAddress(
    std::uint64_t guest_address) const {
    if (modules_ == nullptr) return std::nullopt;
    const std::optional<Elf64ResolvedCodeAddress> resolved =
        modules_->ResolveCodeAddress(guest_address);
    if (!resolved) return std::nullopt;
    return ResolvedGuestCodeAddress{resolved->module, resolved->offset};
}

std::optional<std::uint64_t> Aarch64ProfileRuntime::GuestModuleBase(
    std::string_view basename) const {
    if (modules_ == nullptr) return std::nullopt;
    const Elf64Image* image = modules_->FindModuleByBasename(basename);
    if (image == nullptr) return std::nullopt;
    return image->guest_base();
}

bool Aarch64ProfileRuntime::ResolveGuestModuleCodeOffset(
    std::string_view module, std::uint64_t module_offset,
    std::span<const std::uint32_t> expected,
    std::uint64_t* guest_address, std::string* error) const {
    constexpr std::size_t kMaximumExpectedInstructions = 32;
    if (modules_ == nullptr || guest_address == nullptr || expected.empty() ||
        expected.size() > kMaximumExpectedInstructions ||
        module_offset > (std::numeric_limits<std::uint64_t>::max)() -
                            expected.size_bytes()) {
        SetError(error, "profile module code-offset request is invalid");
        return false;
    }
    const Elf64Image* image = nullptr;
    if (module.empty()) {
        const std::vector<Elf64LoadedModule> loaded =
            modules_->LoadedModulesRootFirst();
        if (!loaded.empty()) image = loaded.front().image;
    } else {
        image = modules_->FindModuleByBasename(module);
    }
    if (image == nullptr ||
        module_offset > (std::numeric_limits<std::uint64_t>::max)() -
                            image->guest_base()) {
        SetError(error, "profile module code-offset target is unavailable");
        return false;
    }
    const std::uint64_t resolved = image->guest_base() + module_offset;
    if (resolved > (std::numeric_limits<std::uint64_t>::max)() -
                       expected.size_bytes()) {
        SetError(error, "profile module code-offset target is unavailable");
        return false;
    }
    std::array<std::uint32_t, kMaximumExpectedInstructions> actual{};
    if (!image->ReadCode(resolved, actual.data(), expected.size_bytes()) ||
        !std::equal(expected.begin(), expected.end(), actual.begin())) {
        SetError(error, "profile module code-offset signature mismatch");
        return false;
    }
    *guest_address = resolved;
    return true;
}

std::optional<std::uint64_t> Aarch64ProfileRuntime::ScanModuleCode(
    std::string_view module, std::span<const std::uint8_t> pattern,
    std::span<const std::uint8_t> mask,
    std::size_t* match_count) const {
    if (match_count != nullptr) *match_count = 0;
    if (modules_ == nullptr || pattern.empty() ||
        pattern.size() != mask.size()) {
        return std::nullopt;
    }
    const Elf64Image* image = nullptr;
    if (module.empty()) {
        const std::vector<Elf64LoadedModule> loaded =
            modules_->LoadedModulesRootFirst();
        if (!loaded.empty()) image = loaded.front().image;
    } else {
        image = modules_->FindModuleByBasename(module);
    }
    if (image == nullptr) return std::nullopt;
    std::optional<std::uint64_t> first;
    for (const Elf64Segment& segment : image->segments()) {
        if ((segment.flags & kElfSegmentExecute) == 0 ||
            segment.memory_size < pattern.size()) {
            continue;
        }
        const std::optional<std::uint64_t> start_opt =
            image->GuestAddress(segment.virtual_address);
        if (!start_opt) continue;
        const std::uint64_t start = *start_opt;
        std::vector<std::uint8_t> bytes(segment.memory_size);
        if (!image->ReadCode(start, bytes.data(), segment.memory_size))
            continue;
        const std::size_t limit = segment.memory_size - pattern.size();
        for (std::size_t i = 0; i <= limit; ++i) {
            bool match = true;
            for (std::size_t j = 0; j < pattern.size(); ++j) {
                if (mask[j] != 0 && bytes[i + j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (!match) continue;
            if (!first) first = start + i;
            if (match_count != nullptr) ++*match_count;
            if (match_count == nullptr) return first;
        }
    }
    return first;
}

bool Aarch64ProfileRuntime::Shutdown(std::string* error,
                                     Aarch64ProfileShutdownMode mode) {
    if (!loaded_ || runtime_ == nullptr || !main_attached_) {
        SetError(error, "profile runtime is not attached");
        return false;
    }
    if (std::this_thread::get_id() != load_thread_) {
        SetError(error, "profile runtime shutdown belongs to its load thread");
        return false;
    }
    if ((runtime_->threads().thread_count() != 0 ||
         runtime_->threads().active_count() != 0) &&
        mode == Aarch64ProfileShutdownMode::kGuestProcessExit) {
        // A guest _exit leaves workers halted at their cancellation
        // boundaries but still registered; cancel and reap them here.
        constexpr std::chrono::seconds kWorkerShutdownTimeout{10};
        BionicThreads::ProcessExitWorkerCleanup worker_cleanup;
        if (!runtime_->threads().CancelAndReapWorkersForProcessExit(
                [this] {
                    return runtime_->sync()
                        .WakeAllConditionWaitersForLifecycleShutdown();
                },
                kWorkerShutdownTimeout, &worker_cleanup, error)) {
            // A worker deep in one JIT run cannot observe its cancellation
            // flag until the run returns. The process is exiting anyway, so
            // report the residue and continue teardown rather than fail.
            std::fprintf(stderr,
                         "ISOMR_PROCESS_EXIT_WORKER_RESIDUE "
                         "requested=%zu observed=%zu woken=%zu reaped=%zu\n",
                         worker_cleanup.cancellation_requests,
                         worker_cleanup.cancellation_observed,
                         worker_cleanup.blocked_calls_woken,
                         worker_cleanup.workers_reaped);
            if (error != nullptr) error->clear();
        }
    }
    if ((runtime_->threads().thread_count() != 0 ||
         runtime_->threads().active_count() != 0) &&
        mode != Aarch64ProfileShutdownMode::kGuestProcessExit) {
        SetError(error, "profile runtime still has active guest threads");
        return false;
    }
    if (mode == Aarch64ProfileShutdownMode::kGuestProcessExit) {
        (void)runtime_->at_exit().DiscardForProcessExit();
    } else if (mode == Aarch64ProfileShutdownMode::kRunGuestFinalizers) {
        constexpr std::size_t kMaximumFinalizerCalls = 1024U * 1024U;
        std::size_t finalizer_calls = 0;
        for (;;) {
            const std::vector<BionicFinalizer> finalizers =
                runtime_->at_exit().DrainFinalizerPass();
            if (finalizers.empty()) break;
            if (finalizers.size() >
                kMaximumFinalizerCalls - finalizer_calls) {
                SetError(
                    error,
                    "profile finalizers exceeded the bounded call count");
                return false;
            }
            for (const BionicFinalizer& finalizer : finalizers) {
                std::array<std::uint64_t, 8> arguments{};
                arguments[0] = finalizer.argument;
                std::uint64_t ignored = 0;
                if (!main_session_->Call(finalizer.function, arguments,
                                         &ignored, error)) {
                    if (error != nullptr)
                        *error =
                            "profile finalizer " +
                            std::to_string(finalizer_calls) +
                            FormatFinalizerContext(modules_.get(),
                                                   finalizer) +
                            " failed: " + *error;
                    return false;
                }
                if (!runtime_->at_exit().CompleteFinalizer()) {
                    SetError(
                        error,
                        "profile finalizer completion state is invalid");
                    return false;
                }
                ++finalizer_calls;
            }
        }
    } else {
        SetError(error, "profile runtime shutdown mode is invalid");
        return false;
    }
    // _exit semantics run no TLS destructors; skipping them also keeps the
    // main session's process-exit cancellation from cancelling these calls.
    const std::size_t destructor_iterations =
        mode == Aarch64ProfileShutdownMode::kGuestProcessExit ? 0 : 4;
    for (std::size_t iteration = 0; iteration < destructor_iterations;
         ++iteration) {
        const std::vector<BionicTlsDestructorCall> calls =
            runtime_->tls().DrainCurrentDestructorPass();
        if (calls.empty()) break;
        for (std::size_t index = 0; index < calls.size(); ++index) {
            std::array<std::uint64_t, 8> arguments{};
            arguments[0] = calls[index].value;
            std::uint64_t ignored = 0;
            if (!main_session_->Call(calls[index].function, arguments,
                                     &ignored, error)) {
                if (error != nullptr)
                    *error = "profile TLS destructor pass " +
                             std::to_string(iteration) + " call " +
                             std::to_string(index) + " failed: " + *error;
                return false;
            }
            if (!runtime_->tls().CompleteCurrentDestructorCall()) {
                SetError(
                    error,
                    "profile TLS destructor completion state is invalid");
                return false;
            }
        }
    }
    if (mode != Aarch64ProfileShutdownMode::kGuestProcessExit &&
        runtime_->tls().current_thread_destructor_count() != 0) {
        SetError(error,
                 "profile TLS destructors exceeded the bounded pass count");
        return false;
    }
    if (mode == Aarch64ProfileShutdownMode::kGuestProcessExit) {
        std::size_t discarded_handles = 0;
        if (!runtime_->dynamic_loader().DiscardHandlesForProcessExit(
                &discarded_handles, error))
            return false;
        (void)runtime_->tls().DiscardCurrentThreadDestructors();
    }
    main_session_.reset();
    if (!runtime_->DetachCurrentThread(error)) return false;
    main_attached_ = false;
    if (!runtime_->Shutdown(error)) return false;
    loaded_ = false;
    return true;
}

std::size_t Aarch64ProfileRuntime::module_count() const {
    return modules_ != nullptr ? modules_->size() : 0;
}
std::size_t Aarch64ProfileRuntime::import_count() const {
    return runtime_ != nullptr ? runtime_->imports().size() : 0;
}
std::size_t Aarch64ProfileRuntime::initializer_count() const {
    return initializers_.size();
}

void Aarch64ProfileRuntime::RegisterSignalSession(
    std::uint64_t thread_handle,
    const std::shared_ptr<Aarch64GuestSession>& session) {
    if (thread_handle == 0 || session == nullptr) return;
    {
        static bool first = true;
        if (first) {
            first = false;
            wchar_t flag[8] = {};
            const bool en =
                GetEnvironmentVariableW(L"ISOMR_PC_SAMPLE", flag, 8) > 0;
            std::fprintf(stderr,
                         "RSS first-register handle=0x%llx pcsample-env=%d\n",
                         (unsigned long long)thread_handle, (int)en);
        }
    }
    static const bool trace = [] {
        wchar_t flag[8] = {};
        return GetEnvironmentVariableW(L"ISOMR_CTR_TRACE", flag, 8) > 0;
    }();
    if (trace)
        std::fprintf(stderr, "RSS register key=0x%llx session=%p\n",
                     (unsigned long long)thread_handle, (void*)session.get());
    EnsurePcSampler();
    const std::lock_guard lock(signal_sessions_mutex_);
    signal_sessions_[thread_handle] = session;
}

void Aarch64ProfileRuntime::EnsurePcSampler() {
    if (pc_sampler_started_) return;
    static const bool enabled = [] {
        wchar_t flag[8] = {};
        return GetEnvironmentVariableW(L"ISOMR_PC_SAMPLE", flag, 8) > 0 &&
               flag[0] != L'0';
    }();
    if (!enabled) return;
    pc_sampler_started_ = true;
    pc_sampler_thread_ = std::thread([this] { PcSamplerLoop(); });
    std::fprintf(stderr, "PC_SAMPLE sampler started\n");
}

void Aarch64ProfileRuntime::PcSamplerLoop() {
    auto next_dump = std::chrono::steady_clock::now() +
                     std::chrono::seconds(15);
    while (!pc_sampler_stop_.load(std::memory_order_relaxed)) {
        std::vector<std::shared_ptr<Aarch64GuestSession>> sessions;
        {
            const std::lock_guard lock(signal_sessions_mutex_);
            sessions.reserve(signal_sessions_.size() + 1);
            for (const auto& entry : signal_sessions_) {
                if (std::shared_ptr<Aarch64GuestSession> s = entry.second.lock())
                    sessions.push_back(std::move(s));
            }
            if (main_session_ != nullptr)
                sessions.push_back(std::shared_ptr<Aarch64GuestSession>(
                    std::shared_ptr<void>(), main_session_.get()));
        }
        std::vector<std::tuple<const Aarch64GuestSession*, std::uint64_t,
                               std::uint64_t>>
            per_session;
        for (const auto& s : sessions) {
            const std::uint64_t pc = s->program_counter();
            per_session.emplace_back(s.get(), s->instructions_executed(), pc);
            // Only count PCs executing guest code. Parked/syscall threads sit
            // at the halt sentinel (0x7ffffffff...) or the import stub
            // (0x680000000...). Guest code lives in the loaded-module range.
            if (pc >= 0x4000000000ull && pc < 0x6000000000ull) {
                const std::lock_guard lock(pc_sampler_mutex_);
                ++pc_sampler_hist_[pc];
                ++pc_sampler_total_;
            }
        }
        if (std::chrono::steady_clock::now() >= next_dump) {
            next_dump = std::chrono::steady_clock::now() +
                        std::chrono::seconds(15);
            std::vector<std::pair<std::uint64_t, std::uint64_t>> top;
            {
                const std::lock_guard lock(pc_sampler_mutex_);
                top.assign(pc_sampler_hist_.begin(), pc_sampler_hist_.end());
            }
            std::sort(top.begin(), top.end(),
                      [](const auto& a, const auto& b) {
                          return a.second > b.second;
                      });
            std::ostringstream out;
            out << "PC_SAMPLE total=" << pc_sampler_total_;
            const std::size_t n = std::min<std::size_t>(top.size(), 24);
            for (std::size_t i = 0; i < n; ++i) {
                const std::uint64_t pc = top[i].first;
                std::string where;
                if (modules_ != nullptr) {
                    if (const auto r = modules_->ResolveCodeAddress(pc)) {
                        std::ostringstream o;
                        o << r->module << "+0x" << std::hex << r->offset;
                        where = o.str();
                    }
                }
                if (where.empty()) {
                    std::ostringstream o;
                    o << "pc0x" << std::hex << pc;
                    where = o.str();
                }
                out << " [" << where << "]=" << std::dec << top[i].second;
            }
            std::sort(per_session.begin(), per_session.end(),
                      [](const auto& a, const auto& b) {
                          return std::get<1>(a) > std::get<1>(b);
                      });
            out << " |INSTR|";
            for (std::size_t i = 0;
                 i < per_session.size() && i < 8; ++i) {
                const std::uint64_t spc = std::get<2>(per_session[i]);
                std::string swhere;
                if (modules_ != nullptr) {
                    if (const auto r = modules_->ResolveCodeAddress(spc)) {
                        std::ostringstream o;
                        o << r->module << "+0x" << std::hex << r->offset;
                        swhere = o.str();
                    }
                }
                if (swhere.empty()) {
                    std::ostringstream o;
                    o << "0x" << std::hex << spc;
                    swhere = o.str();
                }
                out << " [" << swhere << "]=" << std::dec
                    << std::get<1>(per_session[i]);
            }
            out << " |GINSTR|=" << g_armr_instructions_executed.load(
                std::memory_order_relaxed);
            std::fprintf(stderr, "%s\n", out.str().c_str());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
}

std::shared_ptr<Aarch64GuestSession>
Aarch64ProfileRuntime::LookupSignalSession(
    std::uint64_t thread_handle) const {
    const std::lock_guard lock(signal_sessions_mutex_);
    const auto found = signal_sessions_.find(thread_handle);
    return found != signal_sessions_.end() ? found->second.lock() : nullptr;
}

bool Aarch64ProfileRuntime::HaltSignalSession(std::uint64_t thread_handle) {
    std::shared_ptr<Aarch64GuestSession> session =
        LookupSignalSession(thread_handle);
    if (session == nullptr)
        session = LookupSignalSession(kCallSessionKeyBase | thread_handle);
    if (session == nullptr) return false;
    session->RequestHalt();
    return true;
}

std::string Aarch64ProfileRuntime::DescribeWorkerGuestPcs() const {
    std::vector<std::pair<std::uint64_t, std::shared_ptr<Aarch64GuestSession>>>
        sessions;
    {
        const std::lock_guard lock(signal_sessions_mutex_);
        sessions.reserve(signal_sessions_.size() + 1);
        for (const auto& entry : signal_sessions_) {
            if (std::shared_ptr<Aarch64GuestSession> session =
                    entry.second.lock())
                sessions.emplace_back(entry.first, std::move(session));
        }
    }
    if (main_session_ != nullptr) {
        sessions.emplace_back(0, std::shared_ptr<Aarch64GuestSession>(
                                     std::shared_ptr<void>(),
                                     main_session_.get()));
    }
    std::sort(sessions.begin(), sessions.end(),
              [](const auto& left, const auto& right) {
                  return left.first < right.first;
              });
    std::ostringstream out;
    out << "count" << sessions.size();
    for (const auto& entry : sessions) {
        const std::uint64_t pc = entry.second->program_counter();
        out << "|handle0x" << std::hex << entry.first << "-pc0x" << pc
            << std::dec;
        if (modules_ != nullptr) {
            const auto resolved = modules_->ResolveCodeAddress(pc);
            if (resolved)
                out << "-at" << resolved->module << "+0x" << std::hex
                    << resolved->offset << std::dec;
        }
        if (runtime_ != nullptr) {
            const auto import_name =
                runtime_->imports().NameForStubAddress(pc);
            if (import_name) out << "-import:" << *import_name;
        }
        out << "-x0=0x" << std::hex << entry.second->register_value(0)
            << "-x1=0x" << entry.second->register_value(1)
            << "-x2=0x" << entry.second->register_value(2)
            << "-x3=0x" << entry.second->register_value(3)
            << "-x4=0x" << entry.second->register_value(4)
            << "-x8=0x" << entry.second->register_value(8)
            << "-x19=0x" << entry.second->register_value(19)
            << "-x20=0x" << std::hex << entry.second->register_value(20)
            << "-x21=0x" << entry.second->register_value(21)
            << "-lr=0x" << entry.second->register_value(30)
            << "-fp=0x" << entry.second->register_value(29) << std::dec;
        if (modules_ != nullptr) {
            const auto caller = modules_->ResolveCodeAddress(
                entry.second->register_value(30));
            if (caller)
                out << "-caller=" << caller->module << "+0x" << std::hex
                    << caller->offset << std::dec;
        }
        // Walk the guest frame chain for threads parked outside the job
        // pool wait (libunity+0xec00c0) to expose the full call chain.
        const bool pool_wait = entry.second->register_value(30) ==
                               (0x4100000000ull + 0xec00c0ull);
        if (modules_ != nullptr && runtime_ != nullptr && !pool_wait) {
            std::uint64_t fp = entry.second->register_value(29);
            out << "-bt=";
            for (int depth = 0; depth < 10 && fp != 0; ++depth) {
                std::uint64_t saved_fp = 0, saved_lr = 0;
                auto& space = runtime_->address_space();
                if (!space.Read(fp, &saved_fp, sizeof(saved_fp)) ||
                    !space.Read(fp + 8, &saved_lr, sizeof(saved_lr)))
                    break;
                const auto frame = modules_->ResolveCodeAddress(saved_lr);
                if (frame)
                    out << frame->module << "+0x" << std::hex << frame->offset
                        << std::dec << ",";
                else
                    out << "0x" << std::hex << saved_lr << std::dec << ",";
                if (saved_fp <= fp) break;
                fp = saved_fp;
            }
            // Leaf-style frames leave no fp chain; scan the live stack for
            // words that resolve to code — a best-effort caller list for
            // threads parked inside imports (cond_wait/futex/sleep).
            const std::uint64_t sp = entry.second->stack_pointer();
            if (sp != 0) {
                out << "-ss=";
                std::size_t found = 0;
                for (std::size_t index = 0; index < 256 && found < 12;
                     ++index) {
                    std::uint64_t word = 0;
                    if (!runtime_->address_space().Read(
                            sp + index * sizeof(word), &word, sizeof(word)))
                        break;
                    const auto site = modules_->ResolveCodeAddress(word);
                    if (!site) continue;
                    ++found;
                    out << site->module << "+0x" << std::hex << site->offset
                        << std::dec << ",";
                }
            }
        }
    }
    return out.str();
}

}  // namespace isomr
