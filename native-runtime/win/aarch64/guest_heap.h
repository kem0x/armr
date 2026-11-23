#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace isomr {

class Aarch64ImportRegistry;

class GuestHeap {
public:
    enum class AllocationFailureReason : std::uint8_t {
        kInvalidRequest,
        kNoFittingExtent,
        kMetadataIndexInconsistency,
        kMetadataAllocationFailure,
        kHostCommitFailure,
        kWatchAllocationFailure,
        kExtractionFailure,
    };

    struct AllocationFailureSnapshot {
        AllocationFailureReason reason =
            AllocationFailureReason::kInvalidRequest;
        std::uint64_t requested_size = 0;
        std::size_t normalized_size = 0;
        std::uint64_t alignment = 0;
        std::size_t allocated_bytes = 0;
        std::size_t capacity = 0;
        std::size_t committed_bytes = 0;
        std::size_t allocation_count = 0;
        std::size_t free_extent_count = 0;
        std::size_t largest_free_extent = 0;
    };

    enum class AccessSpanResult : std::uint8_t {
        kFailed,
        kValidated,
        kCacheHit,
    };

    class AccessSpanCache {
    public:
        AccessSpanCache() = default;

    private:
        friend class GuestHeap;
        std::uint64_t owner_identity_ = 0;
        std::uint64_t generation_ = 0;
        std::size_t start_ = 0;
        std::size_t end_ = 0;
        bool writable_ = false;
    };

    explicit GuestHeap(std::uint64_t guest_base, std::size_t capacity,
                       std::size_t watch_allocation_size = 0,
                       std::size_t watch_range_offset = 0,
                       std::size_t watch_range_size = 0);
    ~GuestHeap();

    std::uint64_t Allocate(std::uint64_t size);
    std::uint64_t AllocateAligned(std::uint64_t size,
                                  std::uint64_t alignment);
    std::uint64_t Callocate(std::uint64_t count, std::uint64_t size);
    std::uint64_t CallocateAligned(std::uint64_t size,
                                   std::uint64_t alignment);
    // `preferred` is a non-MAP_FIXED placement hint: when nonzero and
    // page-aligned, the reservation is carved at that exact guest address if
    // the extent is free (Boehm GC scratch-arena contiguous growth), otherwise
    // it falls back to ordinary first-fit placement.
    std::uint64_t ReserveMappingAligned(std::uint64_t size,
                                        std::uint64_t alignment,
                                        bool readable, bool writable,
                                        std::string* error,
                                        std::uint64_t preferred = 0);
    // Runtime-internal allocation that survives a guest mprotect/munmap/
    // decommit over its range: the block stays committed and readable+writable
    // so host-side objects (function tables, tokens, loader metadata) remain
    // reachable even when the guest tears down the enclosing mapping.
    std::uint64_t AllocatePinned(std::uint64_t size);
    bool ReleasePinned(std::uint64_t address, std::string* error);
    // Label the watch kind of subsequent allocations until cleared with
    // nullptr (diagnostics only; no effect when watch instrumentation is off).
    void SetAllocDebugTag(const char* tag);
    bool Reallocate(std::uint64_t address, std::uint64_t size,
                    std::uint64_t* result, std::string* error);
    bool Release(std::uint64_t address, std::string* error);
    bool ReleaseMappingRange(std::uint64_t address, std::uint64_t size,
                             std::string* error);
    bool DiscardMappingRange(std::uint64_t address, std::uint64_t size,
                             std::string* error);

    bool Read(std::uint64_t address, void* destination,
              std::size_t size,
              std::uint64_t* watched_generation = nullptr) const;
    AccessSpanResult ReadWithAccessSpanCache(
        std::uint64_t address, void* destination, std::size_t size,
        AccessSpanCache* cache) const;
    bool Write(std::uint64_t address, const void* source, std::size_t size);
    bool WriteUntrackedChecked(std::uint64_t address, const void* source,
                               std::size_t size);
    // Host-side fallback for guest mmap-backed pages: writes directly into
    // committed arena pages that no tracked malloc block covers (bionic
    // mmap allocations). Refuses uncommitted or out-of-arena addresses.
    bool WriteCommittedUntracked(std::uint64_t address, const void* source,
                                 std::size_t size);
    // Read-side counterpart: committed arena pages without malloc-block
    // metadata (bionic mmap allocations) are readable host-side.
    bool ReadCommittedUntracked(std::uint64_t address, void* destination,
                                std::size_t size) const;
    // Host-side physical read gated only by the host commit state of memory_
    // (VirtualQuery MEM_COMMIT), not by the guest-side protection or
    // committed_pages_ bookkeeping. Unity reserves multi-hundred-MB JobQueue /
    // il2cpp arenas PROT_NONE and commits+mprotects sub-ranges on demand; the
    // protection map for those pages can lag actual commit, which makes the
    // audited/untracked paths refuse addresses the guest genuinely uses. For
    // diagnostics only: reads whatever host pages are committed.
    bool ReadPhys(std::uint64_t address, void* destination,
                  std::size_t size) const;
    AccessSpanResult WriteWithAccessSpanCache(
        std::uint64_t address, const void* source, std::size_t size,
        AccessSpanCache* cache);
    bool WriteFromGuest(std::uint64_t address, const void* source,
                        std::size_t size, std::uint64_t guest_program_counter,
                        std::uint64_t guest_link_register,
                        std::uint64_t guest_thread_pointer,
                        std::uint64_t guest_stack_pointer,
                        std::string_view last_import,
                        std::string_view last_jni_import,
                        std::string_view last_jni_note,
                        std::uint64_t current_dispatch_sequence,
                        std::uint64_t last_jni_dispatch_sequence,
                        std::uint64_t* watched_generation = nullptr);
    void AnnotateAllocationFromGuest(
        std::uint64_t address, std::uint64_t guest_link_register,
        std::uint64_t guest_frame_pointer,
        std::uint64_t guest_stack_pointer,
        const Aarch64ImportRegistry* imports);
    void AnnotateAllocationFromGuest(
        std::uint64_t address, std::uint64_t guest_link_register,
        std::uint64_t guest_frame_pointer,
        std::uint64_t guest_stack_pointer,
        std::string_view last_jni_import,
        std::string_view last_jni_note,
        std::uint64_t current_dispatch_sequence,
        std::uint64_t last_jni_dispatch_sequence,
        std::string_view recent_imports);
    bool SetWritable(std::uint64_t address, std::uint64_t size,
                     bool writable, std::string* error);
    bool SetProtection(std::uint64_t address, std::uint64_t size,
                       bool readable, bool writable, std::string* error);
    bool IsWritableRange(std::uint64_t address, std::size_t size) const;
    bool IsReadableRange(std::uint64_t address, std::size_t size) const;
    // Tracks guest PROT_EXEC pages so the JIT's instruction fetch may read
    // code from mmap'd/decrypted memory (packers like pglarmor decrypt in
    // place and jump into it). Separate from the R/W protection map so the
    // merge/split logic is untouched.
    void SetExecutable(std::uint64_t address, std::uint64_t size,
                       bool executable);
    bool IsExecutable(std::uint64_t address) const;
    bool ContainsAddress(std::uint64_t address) const;
    std::string DescribeRange(std::uint64_t address,
                              std::size_t size) const;
    // Diagnostic: print every live (allocated) block with its watch kind to
    // stderr. Used by smoke fixtures when a leak check finds leftovers.
    void DumpLiveAllocations(const char* tag) const;

    std::uint64_t guest_base() const { return guest_base_; }
    std::size_t capacity() const { return capacity_; }
    std::size_t committed_bytes() const;
    std::size_t allocation_count() const;
    std::size_t allocated_bytes() const;
    std::optional<AllocationFailureSnapshot>
    LastAllocationFailureSnapshot() const;
    std::string DescribeLastAllocationFailure() const;
    // Diagnostic: identifies which internal Reallocate path returned a zero
    // result without setting the allocation-failure snapshot (metadata
    // bad_alloc, MergeFreeBlock, CommitRange, or fresh AllocateLocked miss).
    const char* LastReallocZeroPath() const { return last_realloc_zero_; }
    // Structural diagnostic used by the zero-payload fixture. Drains the
    // free-quarantine first so the check verifies the settled free partition:
    // every free offset block must have exactly one (size, offset) index entry.
    bool CheckMetadataInvariants(std::string* error = nullptr);
    // Narrow zero-payload diagnostics for the augmented normal-allocation path.
    std::size_t LastNormalQueryNodeVisitsForTesting() const;
    std::uint64_t MetadataGenerationForTesting() const;
    // Fail the next metadata allocation after `successful_allocations` staged
    // allocations. Disabled until explicitly armed by a fixture.
    void FailMetadataAllocationAfterForTesting(
        std::size_t successful_allocations);
    // Return every extent held in the free-quarantine to the free index. The
    // quarantine deliberately withholds recently released memory from reuse so
    // guest use-after-free behaves like device allocators; fixtures that need
    // deterministic release-then-reuse or a fully indexed free partition call
    // this to drain it explicitly.
    void FlushQuarantineForTesting();
    // Process-wide metadata mutations across every GuestHeap. This is an
    // observation counter only; it never exposes heap storage or pointers.
    static std::uint64_t GlobalMetadataGenerationAdvanceCount();
    static std::uint64_t MetadataGenerationAdvanceDelta(
        std::uint64_t before, std::uint64_t after);
    // Device-allocator fidelity: every block carries a kOverflowRedzone slack
    // tail and released extents sit in a free-quarantine before recycling,
    // mimicking device allocators (jemalloc) so guest overruns/use-after-free
    // behave like hardware. This is a policy layer over the core allocator
    // algorithm (first-fit/coalesce/index/access/OOM), which is identical
    // either way. Enabled by default for guests. Diagnostic fixtures that
    // verify deterministic tight-packing internals disable it here so the
    // core algorithm is checked without the fidelity padding.
    static void SetDeviceAllocatorFidelityForTesting(bool enabled);
    bool heap_watch_diagnostics_configured() const {
        return watch_allocation_size_ != 0;
    }

    // Direct JIT page-table support. When built, direct_page_table_entries()
    // returns a dense array indexed by guest page number whose slots hold the
    // host page base for committed arena pages, and null for everything else.
    // Dynarmic emits inline guest loads/stores for non-null slots and calls
    // the checked memory callbacks for null slots, so allocation, metadata,
    // protection, and host-side checked access behavior are unchanged.
    // Refused while heap watch diagnostics are configured. In this mode
    // guest decommit zeroes the arena range instead of VirtualFree decommit,
    // so direct entries never reference decommitted host pages; guest content
    // semantics are identical and arena memory stays bounded by capacity.
    bool BuildDirectPageTable(std::size_t address_space_bits);
    void** direct_page_table_entries() const noexcept;
    std::size_t direct_page_table_address_space_bits() const noexcept;

private:
    struct WatchEvent {
        std::uint64_t sequence = 0;
        std::uint64_t guest_program_counter = 0;
        std::uint64_t guest_link_register = 0;
        std::uint64_t guest_thread_pointer = 0;
        std::uint64_t guest_stack_pointer = 0;
        std::uint64_t thread_id = 0;
        std::uint64_t current_dispatch_sequence = 0;
        std::uint64_t last_jni_dispatch_sequence = 0;
        std::size_t write_offset = 0;
        std::size_t write_size = 0;
        std::uint64_t before = 0;
        std::uint64_t after = 0;
        std::array<char, 96> origin{};
        std::array<char, 256> jni_note{};
    };

    struct WatchState {
        std::uint64_t generation = 0;
        std::uint64_t initial_value = 0;
        std::uint64_t allocation_sequence = 0;
        std::uint64_t allocation_link_register = 0;
        std::uint64_t allocation_frame_pointer = 0;
        std::uint64_t allocation_stack_pointer = 0;
        std::uint64_t allocation_current_dispatch_sequence = 0;
        std::uint64_t allocation_last_jni_dispatch_sequence = 0;
        std::array<std::uint64_t, 8> allocation_frame_link_registers{};
        std::size_t allocation_frame_count = 0;
        std::array<char, 24> initial_kind{};
        std::array<char, 64> allocation_last_jni_import{};
        std::array<char, 256> allocation_last_jni_note{};
        std::array<char, 512> allocation_recent_imports{};
        std::array<WatchEvent, 16> events{};
        std::size_t event_count = 0;
        std::size_t event_next = 0;
        bool overflow = false;
    };

    struct Block {
        std::size_t size;
        bool allocated;
        bool writable = true;
        bool mapping = false;
        // Pinned blocks are runtime-internal allocations that must stay
        // committed and accessible across guest mprotect/munmap/decommit.
        bool pinned = false;
        std::unique_ptr<WatchState> watch;
    };

    struct ProtectionRange {
        std::size_t end = 0;
        bool readable = false;
        bool writable = false;
    };

    class FreeExtentIndex {
    public:
        struct Node {
            Node(std::size_t node_offset, std::size_t node_size)
                : offset(node_offset), size(node_size),
                  subtree_max_size(node_size) {}
            std::size_t offset;
            std::size_t size;
            int height = 1;
            std::size_t subtree_max_size;
            std::unique_ptr<Node> left;
            std::unique_ptr<Node> right;
        };
        using Handle = std::unique_ptr<Node>;

        FreeExtentIndex() = default;
        FreeExtentIndex(FreeExtentIndex&&) noexcept = default;
        FreeExtentIndex& operator=(FreeExtentIndex&&) noexcept = default;
        FreeExtentIndex(const FreeExtentIndex&) = delete;
        FreeExtentIndex& operator=(const FreeExtentIndex&) = delete;

        static Handle MakeNode(std::size_t offset, std::size_t size);
        bool Insert(Handle node);
        Handle Extract(std::size_t offset);
        const Node* Find(std::size_t offset) const;
        std::optional<std::size_t> FindFirstFit(
            std::size_t size, std::size_t* visits) const;
        std::size_t size() const { return size_; }
        std::size_t largest_extent() const { return Maximum(root_); }
        bool Check(std::string* error) const;
        void Swap(FreeExtentIndex& other) noexcept;

    private:
        static int Height(const Handle& node);
        static std::size_t Maximum(const Handle& node);
        static void Refresh(Node* node);
        static Handle RotateLeft(Handle node);
        static Handle RotateRight(Handle node);
        static Handle Balance(Handle node);
        static Handle InsertNode(Handle root, Handle node, bool* inserted);
        static Handle ExtractNode(Handle root, std::size_t offset,
                                  Handle* extracted);
        static Handle DetachMinimum(Handle root, Handle* minimum);
        static Handle Join(Handle left, Handle right);
        static bool CheckNode(const Node* node,
                              std::optional<std::size_t> lower,
                              std::optional<std::size_t> upper,
                              std::size_t* count, int* height,
                              std::size_t* maximum, std::string* error);

        Handle root_;
        std::size_t size_ = 0;
    };

    static constexpr std::size_t kAlignment = 16;
    // Extra slack appended to every allocation to contain small guest
    // buffer overruns (device size-class behavior). Only applied when
    // device-allocator fidelity is enabled (see below).
    static constexpr std::size_t kOverflowRedzone = 64;
    // Effective overrun padding for the current fidelity mode: the slack is
    // appended only while device-allocator fidelity is enabled.
    static std::size_t EffectiveOverflowRedzone() noexcept;
    // Process-wide device-allocator fidelity toggle (redzone + free
    // quarantine). Enabled by default; diagnostic fixtures disable it.
    static bool s_device_allocator_fidelity_;

    std::optional<std::size_t> NormalizeSize(std::uint64_t size) const;
    // Bounded free quarantine: recently released extents are held back
    // from immediate reuse (matching device allocators such as jemalloc
    // that never recycle memory instantly) so guest use-after-free
    // races behave like they do on hardware instead of reading freshly
    // reused data.
    void QuarantineExtentLocked(std::size_t offset, std::size_t size);
    void FlushQuarantineLocked();
    std::deque<std::pair<std::size_t, std::size_t>> free_quarantine_;
    std::size_t quarantine_bytes_ = 0;
    std::uint64_t AllocateLocked(std::uint64_t size);
    std::uint64_t AllocateAlignedLocked(std::uint64_t size,
                                        std::uint64_t alignment,
                                        bool commit_pages = true,
                                        bool mapping = false,
                                        bool allow_index_rebuild = true);
    bool ReleaseLocked(std::uint64_t address, std::string* error,
                       FreeExtentIndex::Handle released_node = {},
                       std::map<std::size_t, ProtectionRange>
                           preserved_protection = {},
                       bool protection_staged = false);
    bool CommitRangeLocked(std::size_t offset, std::size_t size,
                           std::string* error);
    bool DecommitRangeLocked(std::size_t offset, std::size_t size,
                             std::string* error);
    bool SetProtectionLocked(std::size_t offset, std::size_t size,
                             bool readable, bool writable,
                             std::string* error);
    bool HasAccessLocked(std::size_t offset, std::size_t size,
                         bool write) const;
    bool HasProtectionAccessLocked(std::size_t offset, std::size_t size,
                                   bool write) const;
    bool ResolveAccessSpanLocked(std::size_t offset, bool write,
                                 std::size_t* span_start,
                                 std::size_t* span_end,
                                 std::size_t* allocation_end) const;
    bool IsAllocatedRangeLocked(std::size_t offset, std::size_t size,
                                bool mapping_only) const;
    void StageProtectionRemovalLocked(
        std::size_t offset, std::size_t size,
        std::map<std::size_t, ProtectionRange>* preserved,
        bool inject_failure);
    void ApplyProtectionRemovalLocked(
        std::size_t offset, std::size_t size,
        std::map<std::size_t, ProtectionRange>* preserved) noexcept;
    void RemoveProtectionLocked(std::size_t offset, std::size_t size);
    void MergeProtectionLocked();
    // Re-commit and re-protect (readable+writable) any pinned block that
    // overlaps [offset, offset+size) after a guest protection teardown.
    void PreservePinnedLocked(std::size_t offset, std::size_t size);
    // True when any pinned block overlaps the page at `page`.
    bool PagePinnedLocked(std::size_t page) const;
    // Carve `size` bytes at the exact guest address `preferred` out of the
    // free extent containing it. Returns the guest address, or nullopt when
    // the preferred extent is unavailable.
    std::optional<std::uint64_t> TryCarveAtLocked(std::uint64_t preferred,
                                                  std::uint64_t size,
                                                  bool commit_pages,
                                                  bool mapping);
    void ConfigureWatchLocked(std::size_t offset, Block* block,
                              const char* initial_kind);
    bool WriteWithOrigin(std::uint64_t address, const void* source,
                         std::size_t size,
                         std::uint64_t guest_program_counter,
                         std::uint64_t guest_link_register,
                         std::uint64_t guest_thread_pointer,
                         std::uint64_t guest_stack_pointer,
                         std::string_view origin,
                         std::string_view jni_note,
                         std::uint64_t current_dispatch_sequence,
                         std::uint64_t last_jni_dispatch_sequence,
                         std::uint64_t* watched_generation);
    std::optional<std::size_t> FindAllocatedBlock(
        std::uint64_t address) const;
    std::optional<std::size_t> FindAllocatedRange(
        std::uint64_t address, std::size_t size) const;
    void InsertFreeIndexLocked(std::size_t offset);
    void EraseFreeIndexLocked(std::size_t offset);
    bool MergeFreeBlock(std::size_t offset);
    // Re-derive free_extents_ from the authoritative block map (coalescing
    // adjacent free extents) after a detected index divergence.
    void RebuildFreeIndexLocked();
    bool CheckMetadataInvariantsLocked(std::string* error) const;
    void RecordAllocationFailureLocked(
        AllocationFailureReason reason, std::uint64_t requested_size,
        std::optional<std::size_t> normalized_size,
        std::uint64_t alignment);
    void AdvanceMetadataGenerationLocked();
    void MaybeFailMetadataAllocationLocked();
    static std::atomic<std::uint64_t> global_metadata_generation_advances_;

    std::uint64_t guest_base_ = 0;
    bool valid_configuration_ = false;
    std::uint8_t* memory_ = nullptr;
    std::size_t capacity_ = 0;
    std::vector<std::uint8_t> committed_pages_;
    std::map<std::size_t, ProtectionRange> protections_;
    // Page-granular set of guest offsets carrying PROT_EXEC. Kept apart from
    // protections_ because exec-ness gates JIT fetch, not host R/W access.
    std::set<std::size_t> exec_pages_;
    // blocks_ is the authoritative address-ordered partition. free_extents_
    // is its exact offset-keyed augmented AVL mirror for ordinary first-fit.
    std::map<std::size_t, Block> blocks_;
    FreeExtentIndex free_extents_;
    std::size_t watch_allocation_size_ = 0;
    std::size_t watch_range_offset_ = 0;
    std::size_t watch_range_size_ = 0;
    std::uint64_t next_watch_generation_ = 1;
    std::uint64_t next_watch_sequence_ = 1;
    std::uint64_t cache_identity_ = 0;
    std::uint64_t metadata_generation_ = 1;
    std::optional<std::size_t> metadata_failure_countdown_;
    std::optional<AllocationFailureSnapshot> last_allocation_failure_;
    const char* last_realloc_zero_ = nullptr;
    std::size_t last_normal_query_node_visits_ = 0;
    std::string alloc_debug_tag_;
    bool access_span_cache_enabled_ = true;
    struct DirectPageTableState;
    // Heap-allocated on first direct-table use so the GuestHeap object size
    // stays effectively unchanged for stack-resident fixtures.
    std::unique_ptr<DirectPageTableState> direct_page_table_;
    mutable std::shared_mutex mutex_;
};

}  // namespace isomr
