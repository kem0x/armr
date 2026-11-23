#pragma once

#include <array>
#include <bit>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace isomr {

std::optional<std::string_view> ActiveAarch64ImportNameForCurrentThread();
std::optional<std::uint64_t>
ActiveAarch64ImportSequenceForCurrentThread();

class Aarch64ImportRegistry {
public:
    struct Registers {
        struct NanosleepRequestedDurationDiagnostic {
            bool capture_enabled = false;
            std::optional<std::uint64_t> result;

            void Publish(std::uint64_t value) noexcept {
                if (capture_enabled) result = value;
            }
            void Reset(bool enabled) noexcept {
                capture_enabled = enabled;
                result.reset();
            }
        };

        std::array<std::uint64_t, 8> general{};
        std::array<std::array<std::uint64_t, 2>, 8> vector{};
        // State which an AAPCS64 non-local return must preserve. Ordinary
        // imports receive and return these values unchanged.
        std::uint64_t platform_register = 0;  // x18
        std::array<std::uint64_t, 12> callee_saved_general{};  // x19-x30
        std::array<std::array<std::uint64_t, 2>, 8>
            callee_saved_vector{};  // v8-v15
        // AAPCS64 arguments beyond x0-x7 begin at the caller's SP. This is
        // dispatch metadata, not a writable general-purpose register.
        std::uint64_t stack_pointer = 0;
        // Value-owned diagnostic channel. Only SVC-category telemetry enables
        // capture for a nanosleep dispatch; rollback snapshots copy it safely.
        NanosleepRequestedDurationDiagnostic
            nanosleep_requested_duration_diagnostic;
        struct NonLocalReturn {
            bool requested = false;
            std::uint64_t program_counter = 0;
            std::uint64_t stack_pointer = 0;
        } non_local_return;

        std::uint64_t& operator[](std::size_t index) {
            return general[index];
        }
        const std::uint64_t& operator[](std::size_t index) const {
            return general[index];
        }
        constexpr std::size_t size() const { return general.size(); }

        void SetFloatResult(float value) {
            vector[0] = {};
            vector[0][0] = std::bit_cast<std::uint32_t>(value);
        }
        void SetDoubleResult(double value) {
            vector[0] = {};
            vector[0][0] = std::bit_cast<std::uint64_t>(value);
        }
    };
    using Handler = std::function<bool(Registers*, std::string*)>;

    explicit Aarch64ImportRegistry(std::uint64_t guest_base,
                                   std::size_t capacity = 4096,
                                   bool dispatch_journal_enabled = true);

    bool Register(std::string name, Handler handler, std::string* error);
    // Registers a name whose emitted cell contains host-authored guest code
    // instead of [svc #imm; ret]. Calls that reach the cell never dispatch to
    // the host: the registry still owns the slot mapping and the name stays
    // resolvable, but the handler is a tripwire because dispatch would mean
    // the inline code was bypassed.
    bool RegisterInlineStub(std::string name, std::uint32_t word0,
                            std::uint32_t word1, std::string* error);
    // Registration is single-writer and must complete before Seal(). The
    // registry does not own or dereference data addresses: providers must keep
    // them valid until every sealed reader has stopped and the registry is no
    // longer used. After Seal(), symbol metadata is immutable and const
    // resolution may be used concurrently.
    bool RegisterDataAddress(std::string name, std::uint64_t guest_address,
                             std::string* error);
    bool Seal(std::string* error);
    bool Contains(std::string_view name) const;
    std::optional<std::uint64_t> Resolve(std::string_view name) const;
    std::optional<std::uint64_t> ResolveCallable(
        std::string_view name) const;
    bool ReadCode(std::uint64_t guest_address, void* destination,
                  std::size_t size) const;
    std::optional<std::string_view> NameForImmediate(
        std::uint32_t immediate) const;
    // Maps a guest PC that lands inside an emitted import stub cell back to
    // the registered import name (the cell index, not the SVC immediate).
    std::optional<std::string_view> NameForStubAddress(
        std::uint64_t guest_address) const;
    bool Dispatch(std::uint32_t immediate, Registers* registers,
                  std::string* error) const;
    bool Dispatch(std::string_view name, Registers* registers,
                  std::string* error) const;
    std::optional<std::string_view>
    LastDispatchedNameForCurrentThread() const;
    std::optional<std::string_view>
    LastJniDispatchedNameForCurrentThread() const;
    std::optional<std::string_view>
    LastJniSemanticNoteForCurrentThread() const;
    std::optional<std::uint64_t>
    CurrentDispatchSequenceForCurrentThread() const;
    std::optional<std::uint64_t>
    LastJniDispatchSequenceForCurrentThread() const;
    std::string RecentDispatchedNamesForCurrentThread(
        std::size_t maximum) const;
    std::string RecentJniDispatchedNamesForCurrentThread(
        std::size_t maximum) const;
    std::string RecentJniSemanticNotesForCurrentThread(
        std::size_t maximum) const;
    // Per-thread ring of "jni-call-*"/"jni-*" semantic notes (newest last).
    // Unlike the global semantic ring this is not drowned by other threads'
    // per-frame traffic, so it still names the throwing thread's last calls
    // at an exception log line.
    std::string RecentJniNotesForCurrentThread(
        std::size_t maximum) const;
    std::string RecentSemanticNotesForCurrentThread(
        std::size_t maximum) const;
    std::string RecentDispatchJournal(std::size_t maximum) const;
    // Global (cross-thread) ring of recent "jni-call-*" semantic notes.
    // The per-thread traces above are invisible to other threads; this ring
    // lets a controller inspect what JNI calls a parked guest thread last
    // made (e.g. a void-stub post whose completion the guest awaits).
    std::string RecentGlobalJniCallNotes(std::size_t maximum) const;

    std::uint64_t guest_base() const { return guest_base_; }
    std::size_t code_size() const { return code_.size(); }
    std::size_t size() const { return handlers_.size(); }
    std::size_t symbol_count() const {
        return handlers_.size() + data_addresses_.size();
    }
    bool sealed() const { return sealed_; }
    // Live activity stream for the call viewer: prints one short
    // ISOMR_EV line per guest JNI call, file open/stat, network socket
    // touch, and texture upload. Off by default; the experimental CTR
    // probe enables it for its debugger view.
    void EnableEventStream() { event_stream_enabled_.store(true); }

private:
    std::atomic<bool> event_stream_enabled_{false};
    struct DispatchJournalRecord {
        std::uint64_t sequence = 0;
        std::uint64_t thread = 0;
        std::size_t name_index = 0;
        std::array<std::uint64_t, 3> arguments{};
        std::uint64_t result = 0;
        std::uint64_t platform_register = 0;
        std::uint64_t link_register = 0;
        bool complete = false;
        bool succeeded = false;
        bool detail_truncated = false;
        std::array<char, 160> detail{};
    };

    static constexpr std::size_t kCellSize = 8;
    static constexpr std::size_t kMaximumCells = 65535;
    static constexpr std::size_t kDispatchJournalCapacity = 64;

    std::uint64_t guest_base_ = 0;
    std::size_t capacity_ = 0;
    bool valid_configuration_ = false;
    bool sealed_ = false;
    bool dispatch_journal_enabled_ = true;
    std::vector<std::uint8_t> code_;
    std::vector<Handler> handlers_;
    std::vector<std::string> ordered_names_;
    std::unordered_map<std::string, std::size_t> names_;
    std::unordered_map<std::string, std::uint64_t> data_addresses_;
    mutable std::atomic<std::uint64_t> dispatch_journal_sequence_{0};
    mutable std::mutex dispatch_journal_mutex_;
    mutable std::array<DispatchJournalRecord, kDispatchJournalCapacity>
        dispatch_journal_{};
};

}  // namespace isomr
