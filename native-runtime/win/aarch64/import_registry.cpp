#include "import_registry.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace isomr {

namespace {

thread_local const Aarch64ImportRegistry* g_last_dispatch_owner = nullptr;
thread_local std::size_t g_last_dispatch_index =
    std::numeric_limits<std::size_t>::max();
thread_local std::string_view g_active_dispatch_name;
thread_local std::uint64_t g_active_dispatch_sequence = 0;
struct RecentDispatches {
    const Aarch64ImportRegistry* owner = nullptr;
    std::array<std::size_t, 512> indices{};
    std::size_t count = 0;
    std::size_t next = 0;
    std::array<std::size_t, 32> jni_indices{};
    std::size_t jni_count = 0;
    std::size_t jni_next = 0;
    std::string last_jni_semantic_note;
    std::array<std::string, 64> jni_semantic_notes{};
    std::size_t jni_semantic_note_count = 0;
    std::size_t jni_semantic_note_next = 0;
    std::uint64_t dispatch_sequence = 0;
    std::uint64_t last_jni_sequence = 0;
    std::array<std::string, 128> semantic_notes{};
    std::size_t semantic_note_count = 0;
    std::size_t semantic_note_next = 0;
};
thread_local RecentDispatches g_recent_dispatches;

// Cross-thread ring of "jni-call-*" semantic notes so a controller thread
// can inspect what JNI calls a parked guest thread last made.
struct GlobalJniCallNotes {
    std::mutex mutex;
    std::array<std::string, 64> notes{};
    std::size_t count = 0;
    std::size_t next = 0;
};
GlobalJniCallNotes g_global_jni_call_notes;

void RecordGlobalJniCallNote(const std::string& note) {
    std::scoped_lock lock(g_global_jni_call_notes.mutex);
    if (g_global_jni_call_notes.count != 0) {
        const std::size_t last =
            (g_global_jni_call_notes.next +
             g_global_jni_call_notes.notes.size() - 1) %
            g_global_jni_call_notes.notes.size();
        if (g_global_jni_call_notes.notes[last] == note) return;
    }
    g_global_jni_call_notes.notes[g_global_jni_call_notes.next] = note;
    g_global_jni_call_notes.next =
        (g_global_jni_call_notes.next + 1) %
        g_global_jni_call_notes.notes.size();
    if (g_global_jni_call_notes.count <
        g_global_jni_call_notes.notes.size())
        ++g_global_jni_call_notes.count;
}

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

class ActiveDispatchScope {
public:
    ActiveDispatchScope(std::string_view name, std::uint64_t sequence)
        : previous_name_(g_active_dispatch_name),
          previous_sequence_(g_active_dispatch_sequence) {
        g_active_dispatch_name = name;
        g_active_dispatch_sequence = sequence;
    }
    ~ActiveDispatchScope() {
        g_active_dispatch_name = previous_name_;
        g_active_dispatch_sequence = previous_sequence_;
    }
    ActiveDispatchScope(const ActiveDispatchScope&) = delete;
    ActiveDispatchScope& operator=(const ActiveDispatchScope&) = delete;

private:
    std::string_view previous_name_;
    std::uint64_t previous_sequence_ = 0;
};

std::string Hex(std::uint64_t value) {
    std::array<char, 16> digits{};
    const auto result =
        std::to_chars(digits.data(), digits.data() + digits.size(), value, 16);
    return result.ec == std::errc{}
               ? "0x" + std::string(digits.data(), result.ptr)
               : "invalid";
}

}  // namespace

std::optional<std::string_view> ActiveAarch64ImportNameForCurrentThread() {
    return g_active_dispatch_name.empty()
               ? std::nullopt
               : std::optional<std::string_view>(g_active_dispatch_name);
}

std::optional<std::uint64_t>
ActiveAarch64ImportSequenceForCurrentThread() {
    return g_active_dispatch_sequence == 0
               ? std::nullopt
               : std::optional<std::uint64_t>(g_active_dispatch_sequence);
}

Aarch64ImportRegistry::Aarch64ImportRegistry(std::uint64_t guest_base,
                                             std::size_t capacity,
                                             bool dispatch_journal_enabled)
    : guest_base_(guest_base),
      capacity_(capacity),
      dispatch_journal_enabled_(dispatch_journal_enabled) {
    valid_configuration_ = (guest_base & 3U) == 0 && capacity != 0 &&
                           capacity <= kMaximumCells &&
                           guest_base <=
                               std::numeric_limits<std::uint64_t>::max() -
                                   capacity * kCellSize;
    if (valid_configuration_) code_.reserve(capacity * kCellSize);
}

bool Aarch64ImportRegistry::Register(std::string name, Handler handler,
                                     std::string* error) {
    if (!valid_configuration_) {
        SetError(error, "AArch64 import registry configuration is invalid");
        return false;
    }
    if (sealed_) {
        SetError(error, "AArch64 import registry is sealed");
        return false;
    }
    if (name.empty() || name.size() > 1024 || !handler) {
        SetError(error, "AArch64 import name or handler is invalid");
        return false;
    }
    if (names_.contains(name) || data_addresses_.contains(name)) {
        SetError(error, "duplicate AArch64 import: " + name);
        return false;
    }
    if (handlers_.size() + data_addresses_.size() >= capacity_) {
        SetError(error, "AArch64 import registry is full");
        return false;
    }

    const std::size_t index = handlers_.size();
    const std::uint32_t immediate = static_cast<std::uint32_t>(index + 1);
    const std::uint32_t svc = 0xd4000001U | (immediate << 5);
    constexpr std::uint32_t ret = 0xd65f03c0U;
    const std::size_t offset = code_.size();
    code_.resize(offset + kCellSize);
    std::memcpy(code_.data() + offset, &svc, sizeof(svc));
    std::memcpy(code_.data() + offset + sizeof(svc), &ret, sizeof(ret));
    names_.emplace(name, index);
    ordered_names_.push_back(std::move(name));
    handlers_.push_back(std::move(handler));
    return true;
}

bool Aarch64ImportRegistry::RegisterInlineStub(std::string name,
                                               std::uint32_t word0,
                                               std::uint32_t word1,
                                               std::string* error) {
    if (!valid_configuration_) {
        SetError(error, "AArch64 import registry configuration is invalid");
        return false;
    }
    if (sealed_) {
        SetError(error, "AArch64 import registry is sealed");
        return false;
    }
    if (name.empty() || name.size() > 1024) {
        SetError(error, "AArch64 inline-stub import name is invalid");
        return false;
    }
    if (names_.contains(name) || data_addresses_.contains(name)) {
        SetError(error, "duplicate AArch64 import: " + name);
        return false;
    }
    if (handlers_.size() + data_addresses_.size() >= capacity_) {
        SetError(error, "AArch64 import registry is full");
        return false;
    }
    const std::size_t index = handlers_.size();
    const std::size_t offset = code_.size();
    code_.resize(offset + kCellSize);
    std::memcpy(code_.data() + offset, &word0, sizeof(word0));
    std::memcpy(code_.data() + offset + sizeof(word0), &word1,
                sizeof(word1));
    names_.emplace(name, index);
    ordered_names_.push_back(std::move(name));
    handlers_.push_back(
        [](Aarch64ImportRegistry::Registers*, std::string* handler_error) {
            SetError(handler_error,
                     "inline-stub import cell dispatched via SVC");
            return false;
        });
    return true;
}

bool Aarch64ImportRegistry::RegisterDataAddress(
    std::string name, std::uint64_t guest_address, std::string* error) {
    if (!valid_configuration_) {
        SetError(error, "AArch64 import registry configuration is invalid");
        return false;
    }
    if (sealed_) {
        SetError(error, "AArch64 import registry is sealed");
        return false;
    }
    if (name.empty() || name.size() > 1024 || guest_address == 0) {
        SetError(error, "AArch64 data import name or address is invalid");
        return false;
    }
    if (names_.contains(name) || data_addresses_.contains(name)) {
        SetError(error, "duplicate AArch64 import: " + name);
        return false;
    }
    if (handlers_.size() + data_addresses_.size() >= capacity_) {
        SetError(error, "AArch64 import registry is full");
        return false;
    }
    const std::uint64_t code_limit =
        guest_base_ + capacity_ * kCellSize;
    if (guest_address >= guest_base_ && guest_address < code_limit) {
        SetError(error, "AArch64 data import overlaps reserved import code");
        return false;
    }
    data_addresses_.emplace(std::move(name), guest_address);
    return true;
}

bool Aarch64ImportRegistry::Seal(std::string* error) {
    if (!valid_configuration_ ||
        (handlers_.empty() && data_addresses_.empty())) {
        SetError(error,
                 "cannot seal an invalid or empty AArch64 import registry");
        return false;
    }
    sealed_ = true;
    return true;
}

bool Aarch64ImportRegistry::Contains(std::string_view name) const {
    const std::string key(name);
    return names_.contains(key) || data_addresses_.contains(key);
}

std::optional<std::uint64_t> Aarch64ImportRegistry::Resolve(
    std::string_view name) const {
    if (!sealed_) return std::nullopt;
    const std::string key(name);
    const auto callable = names_.find(key);
    if (callable != names_.end())
        return guest_base_ + callable->second * kCellSize;
    const auto data = data_addresses_.find(key);
    return data == data_addresses_.end()
               ? std::nullopt
               : std::optional<std::uint64_t>(data->second);
}

std::optional<std::uint64_t> Aarch64ImportRegistry::ResolveCallable(
    std::string_view name) const {
    if (!sealed_) return std::nullopt;
    const auto found = names_.find(std::string(name));
    if (found == names_.end()) return std::nullopt;
    return guest_base_ + found->second * kCellSize;
}

bool Aarch64ImportRegistry::ReadCode(std::uint64_t guest_address,
                                     void* destination,
                                     std::size_t size) const {
    if (!sealed_ || guest_address < guest_base_ || size > code_.size() ||
        (destination == nullptr && size != 0))
        return false;
    const std::uint64_t offset = guest_address - guest_base_;
    if (offset > code_.size() - size) return false;
    std::memcpy(destination, code_.data() + static_cast<std::size_t>(offset),
                size);
    return true;
}

std::optional<std::string_view> Aarch64ImportRegistry::NameForImmediate(
    std::uint32_t immediate) const {
    if (!sealed_ || immediate == 0 || immediate > ordered_names_.size())
        return std::nullopt;
    return ordered_names_[immediate - 1];
}

std::optional<std::string_view> Aarch64ImportRegistry::NameForStubAddress(
    std::uint64_t guest_address) const {
    if (!sealed_ || guest_address < guest_base_ ||
        guest_address >= guest_base_ + code_.size())
        return std::nullopt;
    const std::size_t index =
        static_cast<std::size_t>(guest_address - guest_base_) / kCellSize;
    if (index >= ordered_names_.size()) return std::nullopt;
    return ordered_names_[index];
}

namespace {

// Per-thread dispatch breadcrumb ring. Lives at file scope (not inside
// Dispatch) so RecentJniNotesForCurrentThread can serialize the calling
// thread's ring on demand — e.g. at an exception log line, where the ring
// still names the JNI call whose result the managed code dereferenced.
struct DispatchTrace {
    const Aarch64ImportRegistry* owner = nullptr;
    std::array<std::size_t, 512> indices{};
    std::size_t count = 0;
    std::size_t next = 0;
    std::array<std::size_t, 32> jni_indices{};
    std::size_t jni_count = 0;
    std::size_t jni_next = 0;
    std::array<std::string, 64> notes{};
    std::size_t note_count = 0;
    std::size_t note_next = 0;
    std::array<std::string, 32> jni_notes{};
    std::size_t jni_note_count = 0;
    std::size_t jni_note_next = 0;
    std::array<std::string, 128> semantic_notes{};
    std::size_t semantic_note_count = 0;
    std::size_t semantic_note_next = 0;
};
thread_local DispatchTrace g_dispatch_trace;

}  // namespace

bool Aarch64ImportRegistry::Dispatch(std::uint32_t immediate,
                                     Registers* registers,
                                     std::string* error) const {
    if (!sealed_ || immediate == 0 || immediate > handlers_.size() ||
        registers == nullptr) {
        SetError(error, "AArch64 import dispatch identifier is invalid");
        return false;
    }
    DispatchTrace& trace = g_dispatch_trace;
    const std::uint64_t journal_sequence =
        dispatch_journal_sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (trace.owner != this) trace = DispatchTrace{this};
    trace.indices[trace.next] = immediate - 1;
    trace.next = (trace.next + 1) % trace.indices.size();
    if (trace.count < trace.indices.size()) ++trace.count;
    g_last_dispatch_owner = this;
    g_last_dispatch_index = immediate - 1;
    if (g_recent_dispatches.owner != this)
        g_recent_dispatches = RecentDispatches{this};
    g_recent_dispatches.dispatch_sequence = journal_sequence;
    g_recent_dispatches.indices[g_recent_dispatches.next] = immediate - 1;
    g_recent_dispatches.next =
        (g_recent_dispatches.next + 1) % g_recent_dispatches.indices.size();
    if (g_recent_dispatches.count < g_recent_dispatches.indices.size())
        ++g_recent_dispatches.count;
    const std::string& import_name = ordered_names_[immediate - 1];
    {
        // GL/EGL call trace: every gl*/egl* dispatch with its input
        // registers. Unlike the breadcrumb this is uncapped and unaffected
        // by the hot-name filter — texture/FBO setup happens late and is
        // otherwise drowned out by libc churn. Opt-in via ISOMR_GLTRACE=1:
        // uncapped stderr tracing slows the render loop by an order of
        // magnitude, so it is off unless requested.
        static const bool gltrace_enabled = [] {
            wchar_t text[8]{};
            SetLastError(ERROR_SUCCESS);
            const DWORD length = GetEnvironmentVariableW(
                L"ISOMR_GLTRACE", text,
                static_cast<DWORD>(std::size(text)));
            return length > 0 && length < std::size(text) &&
                   text[0] != L'0';
        }();
        const bool gl_name =
            gltrace_enabled &&
            ((import_name.size() >= 2 && import_name[0] == 'g' &&
              import_name[1] == 'l') ||
             import_name.rfind("egl", 0) == 0);
        if (gl_name) {
            std::fprintf(
                stderr, "GLTRACE %s x0=%llx x1=%llx x2=%llx x3=%llx x4=%llx "
                        "x5=%llx x6=%llx x7=%llx\n",
                import_name.c_str(),
                static_cast<unsigned long long>((*registers)[0]),
                static_cast<unsigned long long>((*registers)[1]),
                static_cast<unsigned long long>((*registers)[2]),
                static_cast<unsigned long long>((*registers)[3]),
                static_cast<unsigned long long>((*registers)[4]),
                static_cast<unsigned long long>((*registers)[5]),
                static_cast<unsigned long long>((*registers)[6]),
                static_cast<unsigned long long>((*registers)[7]));
            std::fflush(stderr);
        }
        // JNI/JVM env call trace: every __isomr_jni_*/__isomr_jvm_* dispatch.
        // Opt-in via ISOMR_JNI_TRACE=1 so a protected loader's boot sequence
        // (FindClass/GetMethodID/RegisterNatives/...) is visible.
        static const bool jnitrace_enabled = [] {
            wchar_t text[8]{};
            SetLastError(ERROR_SUCCESS);
            const DWORD length = GetEnvironmentVariableW(
                L"ISOMR_JNI_TRACE", text,
                static_cast<DWORD>(std::size(text)));
            return length > 0 && length < std::size(text) &&
                   text[0] != L'0';
        }();
        if (jnitrace_enabled &&
            (import_name.rfind("__isomr_jni_", 0) == 0 ||
             import_name.rfind("__isomr_jvm_", 0) == 0)) {
            std::fprintf(stderr, "JINITRACE %s x1=%llx x2=%llx x3=%llx\n",
                         import_name.c_str(),
                         static_cast<unsigned long long>((*registers)[1]),
                         static_cast<unsigned long long>((*registers)[2]),
                         static_cast<unsigned long long>((*registers)[3]));
            std::fflush(stderr);
        }
    }
    if (import_name.rfind("__isomr_jni_", 0) == 0 ||
        import_name.rfind("__isomr_jvm_", 0) == 0) {
        g_recent_dispatches.jni_indices[g_recent_dispatches.jni_next] =
            immediate - 1;
        g_recent_dispatches.jni_next =
            (g_recent_dispatches.jni_next + 1) %
            g_recent_dispatches.jni_indices.size();
        if (g_recent_dispatches.jni_count <
            g_recent_dispatches.jni_indices.size())
            ++g_recent_dispatches.jni_count;
        g_recent_dispatches.last_jni_sequence =
            g_recent_dispatches.dispatch_sequence;
        trace.jni_indices[trace.jni_next] = immediate - 1;
        trace.jni_next = (trace.jni_next + 1) % trace.jni_indices.size();
        if (trace.jni_count < trace.jni_indices.size()) ++trace.jni_count;
    }
    std::string detail;
    const ActiveDispatchScope active_dispatch(import_name, journal_sequence);
    if (dispatch_journal_enabled_) {
        std::scoped_lock journal_lock(dispatch_journal_mutex_);
        DispatchJournalRecord& record = dispatch_journal_[
            (journal_sequence - 1) % dispatch_journal_.size()];
        record = {};
        record.sequence = journal_sequence;
        record.thread = static_cast<std::uint64_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        record.name_index = immediate - 1;
        std::copy_n(registers->general.begin(), record.arguments.size(),
                    record.arguments.begin());
        record.result = (*registers)[0];
        record.platform_register = registers->platform_register;
        record.link_register = registers->callee_saved_general.back();
    }
    const Registers input_registers = *registers;
    {
        // Death breadcrumb: native (non-debugger) runs die silently during
        // will-finish-launching; the last non-hot import name printed here
        // bounds where the process stopped.
        static std::size_t breadcrumb_count = 0;
        static thread_local std::size_t last_index = static_cast<std::size_t>(-1);
        if (breadcrumb_count < 20000 &&
            last_index != immediate - 1) {
            ++breadcrumb_count;
            last_index = immediate - 1;
            std::string_view name = ordered_names_[immediate - 1];
            const std::string_view hot[] = {
                "memcpy", "memset", "memmove", "memcmp", "strlen", "malloc",
                "free", "pthread_mutex_lock", "pthread_mutex_unlock",
                "__isomr_jni_exception_check", "__isomr_jvm_get_env",
                "pthread_mutexattr_init", "pthread_mutexattr_settype",
                "pthread_mutex_init", "__cxa_atexit", "getauxval"};
            bool hot_hit = false;
            for (const std::string_view candidate : hot)
                if (name == candidate) { hot_hit = true; break; }
            if (!hot_hit) {
                std::fprintf(stderr, "ISOMR_DISPATCH %s\n",
                             std::string(name).c_str());
                std::fflush(stderr);
            }
        }
    }
    // Opt-in per-import census (ISOMR_IMPORT_CENSUS=1): periodic top-N
    // import call totals to attribute CPU-bound phases to the import loops
    // that drive them. Counters live in a heap array indexed by import
    // ordinal; the periodic dump sorts a snapshot copy.
    static const bool import_census_enabled = [] {
        wchar_t flag[4] = {};
        return GetEnvironmentVariableW(L"ISOMR_IMPORT_CENSUS", flag, 4) > 0 &&
               flag[0] != L'0';
    }();
    if (import_census_enabled) {
        static std::vector<std::atomic<std::uint64_t>> census_counts(
            ordered_names_.size());
        census_counts[immediate - 1]
            .fetch_add(1, std::memory_order_relaxed);
        static std::atomic<std::uint64_t> census_total{0};
        if (census_total.fetch_add(1, std::memory_order_relaxed) %
                2000000 ==
            1999999) {
            std::vector<std::pair<std::uint64_t, std::size_t>> snapshot;
            snapshot.reserve(ordered_names_.size());
            for (std::size_t index = 0;
                 index < ordered_names_.size(); ++index)
                snapshot.emplace_back(
                    census_counts[index].load(std::memory_order_relaxed),
                    index);
            std::partial_sort(
                snapshot.begin(),
                snapshot.begin() + std::min<std::size_t>(12, snapshot.size()),
                snapshot.end(),
                [](const auto& left, const auto& right) {
                    return left.first > right.first;
                });
            std::fprintf(stderr, "ISOMR_IMPORT_CENSUS");
            for (std::size_t rank = 0;
                 rank < std::min<std::size_t>(12, snapshot.size()); ++rank)
                std::fprintf(stderr, " %s=%llu",
                             std::string(ordered_names_[snapshot[rank].second])
                                 .c_str(),
                             static_cast<unsigned long long>(
                                 snapshot[rank].first));
            std::fprintf(stderr, "\n");
            std::fflush(stderr);
        }
    }
    const bool handled = handlers_[immediate - 1](registers, &detail);
    if (dispatch_journal_enabled_) {
        std::scoped_lock journal_lock(dispatch_journal_mutex_);
        DispatchJournalRecord& record = dispatch_journal_[
            (journal_sequence - 1) % dispatch_journal_.size()];
        if (record.sequence == journal_sequence) {
            record.result = (*registers)[0];
            record.complete = true;
            record.succeeded = handled;
            const std::size_t detail_size =
                std::min(detail.size(), record.detail.size() - 1);
            std::memcpy(record.detail.data(), detail.data(), detail_size);
            record.detail_truncated = detail.size() > detail_size;
        }
    }
    if (handled) {
        constexpr std::string_view kTracePrefix = "trace:";
        std::string note;
        if (import_name == "strlen") {
            note = "strlen-len" + std::to_string((*registers)[0]) +
                   "-lr" + Hex(registers->callee_saved_general.back());
        } else if (import_name == "malloc") {
            note = "malloc-size" +
                   std::to_string(input_registers[0]) + "-result" +
                   Hex((*registers)[0]) + "-lr" +
                   Hex(registers->callee_saved_general.back());
        } else if (import_name == "calloc") {
            note = "calloc-count" +
                   std::to_string(input_registers[0]) + "-size" +
                   std::to_string(input_registers[1]) + "-result" +
                   Hex((*registers)[0]) + "-lr" +
                   Hex(registers->callee_saved_general.back());
        } else if (detail.rfind(kTracePrefix, 0) == 0) {
            note = detail.substr(kTracePrefix.size());
        }
        if (!note.empty()) {
            trace.notes[trace.note_next] = note;
            trace.note_next = (trace.note_next + 1) % trace.notes.size();
            if (trace.note_count < trace.notes.size()) ++trace.note_count;
        }
        if (event_stream_enabled_.load(std::memory_order_relaxed)) {
            const bool jni_call =
                import_name.rfind("__isomr_jni_call_", 0) == 0 ||
                import_name.rfind("__isomr_jvm_", 0) == 0;
            const bool file_event = import_name == "open" ||
                                    import_name == "openat" ||
                                    import_name == "access" ||
                                    import_name == "stat" ||
                                    import_name == "mkdir" ||
                                    import_name == "unlink" ||
                                    import_name == "rmdir";
            const bool net_event =
                import_name == "socket" || import_name == "connect" ||
                import_name == "send" || import_name == "sendto" ||
                import_name == "recv" || import_name == "recvfrom" ||
                import_name == "getaddrinfo" ||
                import_name == "shutdown";
            const bool gl_event =
                import_name == "glTexImage2D" ||
                import_name == "glTexSubImage2D" ||
                import_name == "glCreateShader" ||
                import_name == "glShaderSource" ||
                import_name == "glCompileShader" ||
                import_name == "glCreateProgram" ||
                import_name == "glLinkProgram" ||
                import_name == "glDrawElements" ||
                import_name == "glDrawArrays" ||
                import_name == "glClear" ||
                import_name == "glViewport";
            if (jni_call && !note.empty()) {
                std::fprintf(stderr, "ISOMR_EV jni %s\n",
                             note.empty() ? import_name.c_str()
                                          : note.c_str());
            } else if (file_event && !note.empty()) {
                std::fprintf(stderr, "ISOMR_EV file %s\n", note.c_str());
            } else if (net_event) {
                std::fprintf(stderr, "ISOMR_EV net %s a0=%llx a1=%llx\n",
                             import_name.c_str(),
                             static_cast<unsigned long long>(
                                 input_registers[0]),
                             static_cast<unsigned long long>(
                                 input_registers[1]));
            } else if (gl_event) {
                std::fprintf(stderr,
                             "ISOMR_EV gl %s a0=%llx a1=%llx a2=%llx\n",
                             import_name.c_str(),
                             static_cast<unsigned long long>(
                                 input_registers[0]),
                             static_cast<unsigned long long>(
                                 input_registers[1]),
                             static_cast<unsigned long long>(
                                 input_registers[2]));
            }
            // GL census: the first dispatch of each distinct gl/egl import
            // name, so a run shows exactly which guest GL entry points ever
            // reached the host even when the per-frame markers are silent.
            if (import_name.rfind("gl", 0) == 0 ||
                import_name.rfind("egl", 0) == 0) {
                static std::mutex census_mutex;
                static std::set<std::string> census_seen;
                std::lock_guard<std::mutex> census_lock(census_mutex);
                if (census_seen.insert(import_name).second) {
                    std::fprintf(stderr, "ISOMR_EV census %s\n",
                                 import_name.c_str());
                }
            }
        }
        if (!note.empty()) {
            if (import_name.rfind("__isomr_jni_", 0) == 0 ||
                import_name.rfind("__isomr_jvm_", 0) == 0) {
                g_recent_dispatches.last_jni_semantic_note = note;
                if (note.rfind("jni-call-", 0) == 0) {
                    RecordGlobalJniCallNote(note);
                    bool seen = false;
                    for (std::size_t index = 0;
                         index <
                         g_recent_dispatches.jni_semantic_note_count;
                         ++index) {
                        if (g_recent_dispatches.jni_semantic_notes[index] ==
                            note) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen &&
                        g_recent_dispatches.jni_semantic_note_count <
                            g_recent_dispatches.jni_semantic_notes.size()) {
                        g_recent_dispatches.jni_semantic_notes[
                            g_recent_dispatches.jni_semantic_note_count] =
                            note;
                        ++g_recent_dispatches.jni_semantic_note_count;
                        g_recent_dispatches.jni_semantic_note_next =
                            g_recent_dispatches.jni_semantic_note_count %
                            g_recent_dispatches.jni_semantic_notes.size();
                    }
                }
                trace.jni_notes[trace.jni_note_next] = std::move(note);
                trace.jni_note_next =
                    (trace.jni_note_next + 1) % trace.jni_notes.size();
                if (trace.jni_note_count < trace.jni_notes.size())
                    ++trace.jni_note_count;
            }
            if (detail.rfind(kTracePrefix, 0) == 0) {
                const std::string semantic_note =
                    detail.substr(kTracePrefix.size());
                trace.semantic_notes[trace.semantic_note_next] =
                    semantic_note;
                trace.semantic_note_next =
                    (trace.semantic_note_next + 1) %
                    trace.semantic_notes.size();
                if (trace.semantic_note_count < trace.semantic_notes.size())
                    ++trace.semantic_note_count;
                g_recent_dispatches.semantic_notes[
                    g_recent_dispatches.semantic_note_next] = semantic_note;
                g_recent_dispatches.semantic_note_next =
                    (g_recent_dispatches.semantic_note_next + 1) %
                    g_recent_dispatches.semantic_notes.size();
                if (g_recent_dispatches.semantic_note_count <
                    g_recent_dispatches.semantic_notes.size())
                    ++g_recent_dispatches.semantic_note_count;
            }
        }
        return true;
    }
    std::string recent;
    const std::size_t first =
        trace.count == trace.indices.size() ? trace.next : 0;
    for (std::size_t offset = 0; offset < trace.count; ++offset) {
        if (!recent.empty()) recent.push_back(',');
        recent += ordered_names_[
            trace.indices[(first + offset) % trace.indices.size()]];
    }
    std::string recent_jni;
    const std::size_t jni_first =
        trace.jni_count == trace.jni_indices.size() ? trace.jni_next : 0;
    for (std::size_t offset = 0; offset < trace.jni_count; ++offset) {
        if (!recent_jni.empty()) recent_jni.push_back(',');
        recent_jni += ordered_names_[trace.jni_indices[
            (jni_first + offset) % trace.jni_indices.size()]];
    }
    std::string recent_notes;
    const std::size_t note_first =
        trace.note_count == trace.notes.size() ? trace.note_next : 0;
    for (std::size_t offset = 0; offset < trace.note_count; ++offset) {
        if (!recent_notes.empty()) recent_notes.push_back(',');
        recent_notes +=
            trace.notes[(note_first + offset) % trace.notes.size()];
    }
    std::string recent_jni_notes;
    const std::size_t jni_note_first =
        trace.jni_note_count == trace.jni_notes.size()
            ? trace.jni_note_next
            : 0;
    for (std::size_t offset = 0; offset < trace.jni_note_count; ++offset) {
        if (!recent_jni_notes.empty()) recent_jni_notes.push_back(',');
        recent_jni_notes += trace.jni_notes[
            (jni_note_first + offset) % trace.jni_notes.size()];
    }
    std::string recent_semantic_notes;
    const std::size_t semantic_note_first =
        trace.semantic_note_count == trace.semantic_notes.size()
            ? trace.semantic_note_next
            : 0;
    for (std::size_t offset = 0; offset < trace.semantic_note_count;
         ++offset) {
        if (!recent_semantic_notes.empty())
            recent_semantic_notes.push_back(',');
        recent_semantic_notes += trace.semantic_notes[
            (semantic_note_first + offset) % trace.semantic_notes.size()];
    }
    SetError(error,
             "AArch64 import " + import_name +
                 " failed: " +
                 (detail.empty() ? "handler rejected the call" : detail) +
                 " recent=" + recent + " recent-jni=" +
                 (recent_jni.empty() ? "none" : recent_jni) +
                 " recent-notes=" +
                 (recent_notes.empty() ? "none" : recent_notes) +
                 " recent-jni-notes=" +
                 (recent_jni_notes.empty() ? "none" : recent_jni_notes) +
                 " recent-semantic-notes=" +
                 (recent_semantic_notes.empty() ? "none"
                                                : recent_semantic_notes) +
                 " guest-lr=" +
                 Hex(registers->callee_saved_general.back()));
    return false;
}

bool Aarch64ImportRegistry::Dispatch(std::string_view name,
                                     Registers* registers,
                                     std::string* error) const {
    if (!sealed_) {
        SetError(error, "AArch64 import registry is not sealed");
        return false;
    }
    const auto found = names_.find(std::string(name));
    if (found == names_.end()) {
        if (data_addresses_.contains(std::string(name)))
            SetError(error, "AArch64 import symbol is not callable");
        else
            SetError(error, "AArch64 import name is not registered");
        return false;
    }
    return Dispatch(static_cast<std::uint32_t>(found->second + 1), registers,
                    error);
}

std::optional<std::string_view>
Aarch64ImportRegistry::LastDispatchedNameForCurrentThread() const {
    if (g_last_dispatch_owner != this ||
        g_last_dispatch_index >= ordered_names_.size())
        return std::nullopt;
    return ordered_names_[g_last_dispatch_index];
}

std::optional<std::string_view>
Aarch64ImportRegistry::LastJniDispatchedNameForCurrentThread() const {
    if (g_recent_dispatches.owner != this ||
        g_recent_dispatches.jni_count == 0)
        return std::nullopt;
    const std::size_t index =
        (g_recent_dispatches.jni_next +
         g_recent_dispatches.jni_indices.size() - 1) %
        g_recent_dispatches.jni_indices.size();
    const std::size_t name_index =
        g_recent_dispatches.jni_indices[index];
    if (name_index >= ordered_names_.size()) return std::nullopt;
    return ordered_names_[name_index];
}

std::optional<std::string_view>
Aarch64ImportRegistry::LastJniSemanticNoteForCurrentThread() const {
    if (g_recent_dispatches.owner != this ||
        g_recent_dispatches.last_jni_semantic_note.empty())
        return std::nullopt;
    return g_recent_dispatches.last_jni_semantic_note;
}

std::optional<std::uint64_t>
Aarch64ImportRegistry::CurrentDispatchSequenceForCurrentThread() const {
    if (g_recent_dispatches.owner != this) return std::nullopt;
    return g_recent_dispatches.dispatch_sequence;
}

std::optional<std::uint64_t>
Aarch64ImportRegistry::LastJniDispatchSequenceForCurrentThread() const {
    if (g_recent_dispatches.owner != this ||
        g_recent_dispatches.last_jni_sequence == 0)
        return std::nullopt;
    return g_recent_dispatches.last_jni_sequence;
}

std::string Aarch64ImportRegistry::RecentDispatchedNamesForCurrentThread(
    std::size_t maximum) const {
    if (g_recent_dispatches.owner != this || maximum == 0) return {};
    const std::size_t available =
        std::min(maximum, g_recent_dispatches.count);
    const std::size_t oldest =
        g_recent_dispatches.count == g_recent_dispatches.indices.size()
            ? g_recent_dispatches.next
            : 0;
    const std::size_t first =
        (oldest + g_recent_dispatches.count - available) %
        g_recent_dispatches.indices.size();
    std::string result;
    for (std::size_t offset = 0; offset < available; ++offset) {
        if (!result.empty()) result.push_back(',');
        result += ordered_names_[g_recent_dispatches.indices[
            (first + offset) % g_recent_dispatches.indices.size()]];
    }
    return result;
}

std::string Aarch64ImportRegistry::RecentJniDispatchedNamesForCurrentThread(
    std::size_t maximum) const {
    if (g_recent_dispatches.owner != this || maximum == 0) return {};
    const std::size_t available =
        std::min(maximum, g_recent_dispatches.jni_count);
    const std::size_t oldest =
        g_recent_dispatches.jni_count ==
                g_recent_dispatches.jni_indices.size()
            ? g_recent_dispatches.jni_next
            : 0;
    const std::size_t first =
        (oldest + g_recent_dispatches.jni_count - available) %
        g_recent_dispatches.jni_indices.size();
    std::string result;
    for (std::size_t offset = 0; offset < available; ++offset) {
        if (!result.empty()) result.push_back(',');
        result += ordered_names_[g_recent_dispatches.jni_indices[
            (first + offset) % g_recent_dispatches.jni_indices.size()]];
    }
    return result;
}

std::string Aarch64ImportRegistry::RecentJniSemanticNotesForCurrentThread(
    std::size_t maximum) const {
    if (maximum == 0 || g_recent_dispatches.owner != this ||
        g_recent_dispatches.jni_semantic_note_count == 0)
        return {};
    const std::size_t amount = std::min(
        maximum, g_recent_dispatches.jni_semantic_note_count);
    const std::size_t first =
        (g_recent_dispatches.jni_semantic_note_next +
         g_recent_dispatches.jni_semantic_notes.size() - amount) %
        g_recent_dispatches.jni_semantic_notes.size();
    std::string result;
    for (std::size_t offset = 0; offset < amount; ++offset) {
        if (!result.empty()) result.push_back(',');
        result += g_recent_dispatches.jni_semantic_notes[
            (first + offset) %
            g_recent_dispatches.jni_semantic_notes.size()];
    }
    return result;
}

std::string Aarch64ImportRegistry::RecentJniNotesForCurrentThread(
    std::size_t maximum) const {
    DispatchTrace& trace = g_dispatch_trace;
    if (trace.owner != this || maximum == 0 || trace.jni_note_count == 0)
        return {};
    const std::size_t amount = std::min(maximum, trace.jni_note_count);
    const std::size_t first =
        (trace.jni_note_next + trace.jni_notes.size() - amount) %
        trace.jni_notes.size();
    std::string result;
    for (std::size_t offset = 0; offset < amount; ++offset) {
        if (!result.empty()) result.push_back('|');
        result += trace.jni_notes[(first + offset) % trace.jni_notes.size()];
    }
    return result;
}

std::string Aarch64ImportRegistry::RecentSemanticNotesForCurrentThread(
    std::size_t maximum) const {
    if (maximum == 0 || g_recent_dispatches.owner != this ||
        g_recent_dispatches.semantic_note_count == 0)
        return {};
    const std::size_t amount =
        std::min(maximum, g_recent_dispatches.semantic_note_count);
    const std::size_t first =
        (g_recent_dispatches.semantic_note_next +
         g_recent_dispatches.semantic_notes.size() - amount) %
        g_recent_dispatches.semantic_notes.size();
    std::string result;
    for (std::size_t offset = 0; offset < amount; ++offset) {
        if (!result.empty()) result.push_back(',');
        result += g_recent_dispatches.semantic_notes[
            (first + offset) % g_recent_dispatches.semantic_notes.size()];
    }
    return result;
}

std::string Aarch64ImportRegistry::RecentDispatchJournal(
    std::size_t maximum) const {
    if (maximum == 0 || !dispatch_journal_enabled_) return {};
    std::array<DispatchJournalRecord, kDispatchJournalCapacity> snapshot{};
    {
        std::scoped_lock journal_lock(dispatch_journal_mutex_);
        snapshot = dispatch_journal_;
    }
    std::vector<DispatchJournalRecord> records;
    records.reserve(snapshot.size());
    for (const DispatchJournalRecord& record : snapshot) {
        if (record.sequence != 0 && record.name_index < ordered_names_.size())
            records.push_back(record);
    }
    std::sort(records.begin(), records.end(),
              [](const DispatchJournalRecord& left,
                 const DispatchJournalRecord& right) {
                  return left.sequence < right.sequence;
              });
    const std::size_t first =
        records.size() > maximum ? records.size() - maximum : 0;
    std::string result =
        "count" + std::to_string(records.size() - first) + "-overflow" +
        (dispatch_journal_sequence_.load(std::memory_order_relaxed) >
                 kDispatchJournalCapacity
             ? "1:"
             : "0:");
    for (std::size_t index = first; index < records.size(); ++index) {
        const DispatchJournalRecord& record = records[index];
        if (index != first) result.push_back('|');
        std::string detail(
            record.detail.data(),
            strnlen(record.detail.data(), record.detail.size()));
        for (char& character : detail) {
            if (character == '|' || character == '\r' || character == '\n')
                character = '_';
        }
        result += "seq" + std::to_string(record.sequence) + "-thread" +
                  Hex(record.thread) + "-name" +
                  ordered_names_[record.name_index] + "-x0in" +
                  Hex(record.arguments[0]) + "-x1in" +
                  Hex(record.arguments[1]) + "-x2in" +
                  Hex(record.arguments[2]) + "-x0out" +
                  Hex(record.result) + "-x18" +
                  Hex(record.platform_register) + "-lr" +
                  Hex(record.link_register) + "-status" +
                  (record.complete
                       ? (record.succeeded ? "pass" : "fail")
                       : "pending") +
                  "-detail" + (detail.empty() ? "none" : detail) +
                  "-truncated" + (record.detail_truncated ? "1" : "0");
    }
    return result;
}

std::string Aarch64ImportRegistry::RecentGlobalJniCallNotes(
    std::size_t maximum) const {
    if (maximum == 0) return {};
    std::array<std::string, 64> snapshot{};
    std::size_t count = 0;
    std::size_t next = 0;
    {
        std::scoped_lock lock(g_global_jni_call_notes.mutex);
        snapshot = g_global_jni_call_notes.notes;
        count = g_global_jni_call_notes.count;
        next = g_global_jni_call_notes.next;
    }
    const std::size_t shown = count > maximum ? maximum : count;
    const std::size_t start =
        count == snapshot.size()
            ? (next + snapshot.size() - shown) % snapshot.size()
            : count - shown;
    std::string result = "count" + std::to_string(shown) + ":";
    for (std::size_t offset = 0; offset < shown; ++offset) {
        const std::size_t index =
            count == snapshot.size()
                ? (start + offset) % snapshot.size()
                : start + offset;
        if (offset != 0) result.push_back('|');
        result += snapshot[index];
    }
    return result;
}

}  // namespace isomr
