#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "guest_heap.h"

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

#include "import_registry.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace isomr {

std::atomic<std::uint64_t> GuestHeap::global_metadata_generation_advances_{0};
bool GuestHeap::s_device_allocator_fidelity_ = true;

namespace {

constexpr std::size_t kHostPageSize = 4096;
std::atomic<std::uint64_t> next_cache_identity{1};
thread_local int g_alloc_entry_tag = 0;  // ISOMR-ALLOC-SRC diagnostic
thread_local int g_alloc_entry_depth = 0;

std::uint64_t AcquireCacheIdentity() {
    std::uint64_t identity =
        next_cache_identity.load(std::memory_order_relaxed);
    while (identity != std::numeric_limits<std::uint64_t>::max()) {
        if (next_cache_identity.compare_exchange_weak(
                identity, identity + 1, std::memory_order_relaxed))
            return identity;
    }
    return 0;
}

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

}  // namespace

int GuestHeap::FreeExtentIndex::Height(const Handle& node) {
    return node == nullptr ? 0 : node->height;
}

std::size_t GuestHeap::FreeExtentIndex::Maximum(const Handle& node) {
    return node == nullptr ? 0 : node->subtree_max_size;
}

void GuestHeap::FreeExtentIndex::Refresh(Node* node) {
    node->height = 1 + (std::max)(Height(node->left), Height(node->right));
    node->subtree_max_size =
        (std::max)(node->size,
                   (std::max)(Maximum(node->left), Maximum(node->right)));
}

GuestHeap::FreeExtentIndex::Handle
GuestHeap::FreeExtentIndex::RotateLeft(Handle node) {
    Handle root = std::move(node->right);
    node->right = std::move(root->left);
    Refresh(node.get());
    root->left = std::move(node);
    Refresh(root.get());
    return root;
}

GuestHeap::FreeExtentIndex::Handle
GuestHeap::FreeExtentIndex::RotateRight(Handle node) {
    Handle root = std::move(node->left);
    node->left = std::move(root->right);
    Refresh(node.get());
    root->right = std::move(node);
    Refresh(root.get());
    return root;
}

GuestHeap::FreeExtentIndex::Handle
GuestHeap::FreeExtentIndex::Balance(Handle node) {
    if (node == nullptr) return node;
    Refresh(node.get());
    const int balance = Height(node->left) - Height(node->right);
    if (balance > 1) {
        if (Height(node->left->right) > Height(node->left->left))
            node->left = RotateLeft(std::move(node->left));
        return RotateRight(std::move(node));
    }
    if (balance < -1) {
        if (Height(node->right->left) > Height(node->right->right))
            node->right = RotateRight(std::move(node->right));
        return RotateLeft(std::move(node));
    }
    return node;
}

GuestHeap::FreeExtentIndex::Handle
GuestHeap::FreeExtentIndex::InsertNode(Handle root, Handle node,
                                       bool* inserted) {
    if (root == nullptr) {
        *inserted = true;
        return node;
    }
    if (node->offset < root->offset)
        root->left = InsertNode(std::move(root->left), std::move(node),
                                inserted);
    else if (node->offset > root->offset)
        root->right = InsertNode(std::move(root->right), std::move(node),
                                 inserted);
    else
        return root;
    return Balance(std::move(root));
}

GuestHeap::FreeExtentIndex::Handle
GuestHeap::FreeExtentIndex::DetachMinimum(Handle root, Handle* minimum) {
    if (root->left == nullptr) {
        *minimum = std::move(root);
        Handle right = std::move((*minimum)->right);
        (*minimum)->right.reset();
        Refresh(minimum->get());
        return right;
    }
    root->left = DetachMinimum(std::move(root->left), minimum);
    return Balance(std::move(root));
}

GuestHeap::FreeExtentIndex::Handle
GuestHeap::FreeExtentIndex::Join(Handle left, Handle right) {
    if (left == nullptr) return right;
    if (right == nullptr) return left;
    if (Height(left) > Height(right) + 1) {
        left->right = Join(std::move(left->right), std::move(right));
        return Balance(std::move(left));
    }
    if (Height(right) > Height(left) + 1) {
        right->left = Join(std::move(left), std::move(right->left));
        return Balance(std::move(right));
    }
    Handle root;
    right = DetachMinimum(std::move(right), &root);
    root->left = std::move(left);
    root->right = std::move(right);
    return Balance(std::move(root));
}

GuestHeap::FreeExtentIndex::Handle
GuestHeap::FreeExtentIndex::ExtractNode(Handle root, std::size_t offset,
                                        Handle* extracted) {
    if (root == nullptr) return root;
    if (offset < root->offset)
        root->left = ExtractNode(std::move(root->left), offset, extracted);
    else if (offset > root->offset)
        root->right = ExtractNode(std::move(root->right), offset, extracted);
    else {
        *extracted = std::move(root);
        Handle joined = Join(std::move((*extracted)->left),
                             std::move((*extracted)->right));
        (*extracted)->left.reset();
        (*extracted)->right.reset();
        Refresh(extracted->get());
        return joined;
    }
    return Balance(std::move(root));
}

GuestHeap::FreeExtentIndex::Handle
GuestHeap::FreeExtentIndex::MakeNode(std::size_t offset, std::size_t size) {
    return std::make_unique<Node>(offset, size);
}

bool GuestHeap::FreeExtentIndex::Insert(Handle node) {
    if (node == nullptr) return false;
    node->left.reset();
    node->right.reset();
    Refresh(node.get());
    bool inserted = false;
    root_ = InsertNode(std::move(root_), std::move(node), &inserted);
    if (inserted) ++size_;
    return inserted;
}

GuestHeap::FreeExtentIndex::Handle
GuestHeap::FreeExtentIndex::Extract(std::size_t offset) {
    Handle extracted;
    root_ = ExtractNode(std::move(root_), offset, &extracted);
    if (extracted != nullptr) --size_;
    return extracted;
}

const GuestHeap::FreeExtentIndex::Node*
GuestHeap::FreeExtentIndex::Find(std::size_t offset) const {
    const Node* node = root_.get();
    while (node != nullptr) {
        if (offset < node->offset) node = node->left.get();
        else if (offset > node->offset) node = node->right.get();
        else return node;
    }
    return nullptr;
}

std::optional<std::size_t> GuestHeap::FreeExtentIndex::FindFirstFit(
    std::size_t size, std::size_t* visits) const {
    if (visits != nullptr) *visits = 0;
    const Node* node = root_.get();
    while (node != nullptr) {
        if (visits != nullptr) ++*visits;
        if (node->left != nullptr &&
            node->left->subtree_max_size >= size) {
            node = node->left.get();
        } else if (node->size >= size) {
            return node->offset;
        } else {
            node = node->right.get();
        }
    }
    return std::nullopt;
}

bool GuestHeap::FreeExtentIndex::CheckNode(
    const Node* node, std::optional<std::size_t> lower,
    std::optional<std::size_t> upper, std::size_t* count, int* height,
    std::size_t* maximum, std::string* error) {
    if (node == nullptr) { *height = 0; *maximum = 0; return true; }
    if ((lower && node->offset <= *lower) ||
        (upper && node->offset >= *upper) || node->size == 0) {
        SetError(error, "guest heap free AVL ordering is invalid");
        return false;
    }
    int left_height = 0, right_height = 0;
    std::size_t left_max = 0, right_max = 0;
    if (!CheckNode(node->left.get(), lower, node->offset, count,
                   &left_height, &left_max, error) ||
        !CheckNode(node->right.get(), node->offset, upper, count,
                   &right_height, &right_max, error)) return false;
    ++*count;
    *height = 1 + (std::max)(left_height, right_height);
    *maximum = (std::max)(node->size, (std::max)(left_max, right_max));
    if (node->height != *height || node->subtree_max_size != *maximum ||
        left_height - right_height > 1 || right_height - left_height > 1) {
        SetError(error, "guest heap free AVL augmentation is invalid");
        return false;
    }
    return true;
}

bool GuestHeap::FreeExtentIndex::Check(std::string* error) const {
    std::size_t count = 0, maximum = 0;
    int height = 0;
    return CheckNode(root_.get(), std::nullopt, std::nullopt, &count,
                     &height, &maximum, error) && count == size_;
}

void GuestHeap::FreeExtentIndex::Swap(FreeExtentIndex& other) noexcept {
    root_.swap(other.root_);
    std::swap(size_, other.size_);
}

GuestHeap::GuestHeap(std::uint64_t guest_base, std::size_t capacity,
                     std::size_t watch_allocation_size,
                     std::size_t watch_range_offset,
                     std::size_t watch_range_size)
    : guest_base_(guest_base),
      watch_allocation_size_(watch_allocation_size),
      watch_range_offset_(watch_range_offset),
      watch_range_size_(watch_range_size),
      cache_identity_(AcquireCacheIdentity()) {
    access_span_cache_enabled_ = cache_identity_ != 0;
    const std::size_t aligned_capacity = capacity - capacity % kAlignment;
    const bool watch_disabled = watch_allocation_size == 0 &&
                                watch_range_offset == 0 &&
                                watch_range_size == 0;
    const bool watch_valid =
        watch_disabled ||
        (watch_allocation_size != 0 && watch_range_size != 0 &&
         watch_range_size <= sizeof(std::uint64_t) &&
         watch_range_offset <= watch_allocation_size &&
         watch_range_size <= watch_allocation_size - watch_range_offset);
    valid_configuration_ = guest_base != 0 &&
                           (guest_base % kAlignment) == 0 &&
                           aligned_capacity >= kAlignment &&
                           watch_valid &&
                           guest_base <=
                               std::numeric_limits<std::uint64_t>::max() -
                                   aligned_capacity;
    if (!valid_configuration_) return;
    memory_ = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, aligned_capacity, MEM_RESERVE, PAGE_NOACCESS));
    if (memory_ == nullptr) {
        valid_configuration_ = false;
        return;
    }
    capacity_ = aligned_capacity;
    committed_pages_.resize(
        (aligned_capacity + kHostPageSize - 1) / kHostPageSize);
    blocks_.emplace(0, Block{aligned_capacity, false});
    free_extents_.Insert(FreeExtentIndex::MakeNode(0, aligned_capacity));
    if (watch_disabled) {
        watch_allocation_size_ = 0;
        watch_range_offset_ = 0;
        watch_range_size_ = 0;
    }
}

GuestHeap::~GuestHeap() {
    const std::unique_lock lock(mutex_);
    AdvanceMetadataGenerationLocked();
    if (memory_ != nullptr) VirtualFree(memory_, 0, MEM_RELEASE);
}

std::optional<std::size_t> GuestHeap::NormalizeSize(
    std::uint64_t size) const {
    if (!valid_configuration_) return std::nullopt;
    if (size == 0) size = 1;
    if (size > capacity_ ||
        size > std::numeric_limits<std::uint64_t>::max() -
                   (kAlignment - 1))
        return std::nullopt;
    const std::uint64_t aligned =
        (size + (kAlignment - 1)) & ~(kAlignment - 1);
    if (aligned > capacity_) return std::nullopt;
    return static_cast<std::size_t>(aligned);
}

std::size_t GuestHeap::EffectiveOverflowRedzone() noexcept {
    return s_device_allocator_fidelity_ ? kOverflowRedzone : 0;
}

void GuestHeap::SetDeviceAllocatorFidelityForTesting(bool enabled) {
    s_device_allocator_fidelity_ = enabled;
}

void GuestHeap::SetAllocDebugTag(const char* tag) {
    const std::unique_lock lock(mutex_);
    if (tag == nullptr) {
        alloc_debug_tag_.clear();
    } else {
        alloc_debug_tag_ = tag;
    }
}

std::uint64_t GuestHeap::Allocate(std::uint64_t size) {
    g_alloc_entry_tag = 1;
    const std::unique_lock lock(mutex_);
    return AllocateLocked(size);
}

std::uint64_t GuestHeap::AllocatePinned(std::uint64_t size) {
    g_alloc_entry_tag = 2;
    const std::unique_lock lock(mutex_);
    const std::uint64_t address = AllocateAlignedLocked(size, kAlignment);
    if (address == 0) return 0;
    const std::optional<std::size_t> block = FindAllocatedBlock(address);
    if (!block) return 0;
    blocks_.at(*block).pinned = true;
    return address;
}

bool GuestHeap::ReleasePinned(std::uint64_t address, std::string* error) {
    const std::unique_lock lock(mutex_);
    if (address == 0) return true;
    const std::optional<std::size_t> block = FindAllocatedBlock(address);
    if (!block) {
        SetError(error, "guest pinned pointer is not an active allocation");
        return false;
    }
    blocks_.at(*block).pinned = false;
    return ReleaseLocked(address, error);
}

std::uint64_t GuestHeap::AllocateAligned(std::uint64_t size,
                                         std::uint64_t alignment) {
    g_alloc_entry_tag = 3;
    const std::unique_lock lock(mutex_);
    if (alignment < sizeof(std::uint64_t) ||
        (alignment & (alignment - 1)) != 0) {
        RecordAllocationFailureLocked(
            AllocationFailureReason::kInvalidRequest, size, std::nullopt,
            alignment);
        return 0;
    }
    return AllocateAlignedLocked(size, std::max<std::uint64_t>(
                                           alignment, kAlignment));
}

std::uint64_t GuestHeap::AllocateLocked(std::uint64_t size) {
    return AllocateAlignedLocked(size, kAlignment);
}

std::uint64_t GuestHeap::AllocateAlignedLocked(std::uint64_t size,
                                                std::uint64_t alignment,
                                                bool commit_pages,
                                                bool mapping,
                                                bool allow_index_rebuild) {
    std::optional<std::size_t> normalized = NormalizeSize(size);
    if (!normalized || alignment < kAlignment ||
        (alignment & (alignment - 1)) != 0 ||
        alignment > std::numeric_limits<std::size_t>::max()) {
        RecordAllocationFailureLocked(
            AllocationFailureReason::kInvalidRequest, size, normalized,
            alignment);
        return 0;
    }
    // Overflow containment: device allocators (jemalloc) hand out
    // size-class chunks larger than the request, so small guest buffer
    // overruns land in chunk slack instead of clobbering the next
    // object. Pad every block by a redzone so our tighter packing
    // behaves the same way; guest-visible semantics are unchanged.
    normalized.value() += EffectiveOverflowRedzone();
    if (normalized.value() > capacity_) {
        RecordAllocationFailureLocked(
            AllocationFailureReason::kNoFittingExtent, size, normalized,
            alignment);
        return 0;
    }
    std::optional<std::size_t> normal_candidate;
    if (alignment == kAlignment) {
        normal_candidate = free_extents_.FindFirstFit(
            *normalized, &last_normal_query_node_visits_);
        if (!normal_candidate && !free_quarantine_.empty()) {
            // Quarantined extents are a last-resort free pool under memory
            // pressure: like jemalloc, recycle them rather than refuse an
            // allocation that quarantined space can satisfy.
            FlushQuarantineLocked();
            normal_candidate = free_extents_.FindFirstFit(
                *normalized, &last_normal_query_node_visits_);
        }
        if (!normal_candidate) {
            RecordAllocationFailureLocked(
                AllocationFailureReason::kNoFittingExtent, size, normalized,
                alignment);
            return 0;
        }
    }
    // Over-aligned allocations intentionally retain the exact increasing-
    // offset partition scan: alignment prefixes can invalidate an earlier
    // extent even when its raw size is sufficient.
    bool index_diverged = false;
    for (auto block_it = alignment == kAlignment
                             ? blocks_.find(*normal_candidate)
                             : blocks_.begin();
         block_it != blocks_.end(); ++block_it) {
        if (block_it->second.allocated) continue;
        const std::size_t block_offset = block_it->first;
        const Block& candidate = block_it->second;
        const std::uint64_t block_address = guest_base_ + block_offset;
        if (block_address > std::numeric_limits<std::uint64_t>::max() -
                                (alignment - 1))
            continue;
        const std::uint64_t aligned_address =
            (block_address + alignment - 1) & ~(alignment - 1);
        const std::uint64_t prefix64 = aligned_address - block_address;
        if (prefix64 > candidate.size) continue;
        const std::size_t prefix = static_cast<std::size_t>(prefix64);
        if (*normalized > candidate.size - prefix) continue;
        const std::size_t suffix = candidate.size - prefix - *normalized;
        const std::size_t allocation_offset = block_offset + prefix;

        const auto* indexed = free_extents_.Find(block_offset);
        // A free block with no index entry is a quarantined extent, not
        // corruption: it is held out of free_extents_ until the quarantine
        // flushes. Skip it; the empty-quarantine retry below recovers it.
        if (indexed == nullptr) continue;
        if (indexed->size != candidate.size) {
            std::fprintf(stderr,
                         "ISOMR_HEAP_INDEX_DIVERGE off=%zx block_size=%zx "
                         "block_alloc=%d block_pinned=%d idx_size=%llx "
                         "req=%llx norm=%zx\n",
                         block_offset, candidate.size,
                         candidate.allocated ? 1 : 0,
                         candidate.pinned ? 1 : 0,
                         static_cast<unsigned long long>(indexed->size),
                         static_cast<unsigned long long>(size),
                         *normalized);
            std::fflush(stderr);
            index_diverged = true;
            break;  // Rebuild the index below rather than fail outright.
        }
        const std::size_t suffix_offset = allocation_offset + *normalized;
        std::map<std::size_t, Block> staged_blocks;
        std::map<std::size_t, ProtectionRange> staged_protection;
        FreeExtentIndex::Handle prefix_node;
        FreeExtentIndex::Handle suffix_node;
        try {
            if (prefix != 0) {
                MaybeFailMetadataAllocationLocked();
                staged_blocks.emplace(block_offset, Block{prefix, false});
                MaybeFailMetadataAllocationLocked();
                prefix_node = FreeExtentIndex::MakeNode(block_offset, prefix);
            }
            MaybeFailMetadataAllocationLocked();
            staged_blocks.emplace(allocation_offset,
                                  Block{*normalized, true, true, mapping});
            if (suffix != 0) {
                MaybeFailMetadataAllocationLocked();
                staged_blocks.emplace(suffix_offset, Block{suffix, false});
                MaybeFailMetadataAllocationLocked();
                suffix_node = FreeExtentIndex::MakeNode(suffix_offset, suffix);
            }
            MaybeFailMetadataAllocationLocked();
            staged_protection.emplace(
                allocation_offset,
                ProtectionRange{allocation_offset + *normalized, true, true});
        } catch (const std::bad_alloc&) {
            RecordAllocationFailureLocked(
                AllocationFailureReason::kMetadataAllocationFailure, size,
                normalized, alignment);
            return 0;
        }
        if (commit_pages &&
            !CommitRangeLocked(allocation_offset, *normalized, nullptr)) {
            RecordAllocationFailureLocked(
                AllocationFailureReason::kHostCommitFailure, size,
                normalized, alignment);
            return 0;
        }
        try {
            ConfigureWatchLocked(allocation_offset,
                                 &staged_blocks.at(allocation_offset),
                                 "malloc");
        } catch (const std::bad_alloc&) {
            RecordAllocationFailureLocked(
                AllocationFailureReason::kWatchAllocationFailure, size,
                normalized, alignment);
            return 0;
        }
        auto original_free_node = free_extents_.Extract(block_offset);
        if (original_free_node == nullptr) {
            RecordAllocationFailureLocked(
                AllocationFailureReason::kExtractionFailure, size,
                normalized, alignment);
            return 0;
        }
        auto original_block_node = blocks_.extract(block_it);
        if (original_block_node.empty()) {
            free_extents_.Insert(std::move(original_free_node));
            RecordAllocationFailureLocked(
                AllocationFailureReason::kExtractionFailure, size,
                normalized, alignment);
            return 0;
        }
        while (!staged_blocks.empty()) {
            auto node = staged_blocks.extract(staged_blocks.begin());
            blocks_.insert(std::move(node));
        }
        // A previously freed block may still carry a stale protection
        // range (for example loader metadata marked read-only after
        // relocation processing). That stale range would shadow the new
        // allocation's writable range in HasProtectionAccessLocked and
        // make every write to the fresh block fail closed, so drop any
        // protection covering this extent before inserting the new RW
        // range.
        RemoveProtectionLocked(allocation_offset, *normalized);
        while (!staged_protection.empty()) {
            auto node = staged_protection.extract(staged_protection.begin());
            protections_.insert(std::move(node));
        }
        if (prefix_node != nullptr)
            free_extents_.Insert(std::move(prefix_node));
        if (suffix_node != nullptr)
            free_extents_.Insert(std::move(suffix_node));
        AdvanceMetadataGenerationLocked();
        if (*normalized >= (std::size_t{1} << 20)) {
            std::fprintf(stderr,
                         "ISOMR_BIGALLOC offset=%zx size=%zx padded=%zu\n",
                         allocation_offset, size, *normalized);
            std::fflush(stderr);
            if (size >= 0xF0000 && size <= 0x110000) {
                static std::uint64_t big_src = 0;
                if ((big_src++ % 500) == 0) {
                    const auto imp =
                        ActiveAarch64ImportNameForCurrentThread();
                    void* frames[12] = {};
                    const USHORT nframes = CaptureStackBackTrace(
                        1, 12, frames, nullptr);
                    std::fprintf(stderr,
                                 "ISOMR_BIGALLOC_SRC n=%llu size=%zx "
                                 "import=%s entry=%d",
                                 (unsigned long long)big_src, size,
                                 imp ? std::string(*imp).c_str() : "none",
                                 g_alloc_entry_tag);
                    const std::uintptr_t base =
                        reinterpret_cast<std::uintptr_t>(&__ImageBase);
                    for (USHORT f = 0; f < nframes; ++f) {
                        std::fprintf(stderr, " f%zx",
                                     reinterpret_cast<std::uintptr_t>(
                                         frames[f]) -
                                         base);
                    }
                    std::fprintf(stderr, "\n");
                    std::fflush(stderr);
                }
            }
        }
        return aligned_address;
    }
    if (alignment == kAlignment && !free_quarantine_.empty()) {
        FlushQuarantineLocked();
        return AllocateAlignedLocked(size, alignment, commit_pages,
                                     mapping, allow_index_rebuild);
    }
    // The scan found no allocatable extent even though the free index claimed
    // one (a stale node whose offset is no longer a block start, an
    // over-reported subtree bound, or an unindexed survivor). The block map is
    // authoritative: rebuild the index from it and retry exactly once before
    // reporting a genuine failure.
    if (allow_index_rebuild &&
        (index_diverged ||
         (alignment == kAlignment && normal_candidate))) {
        try {
            RebuildFreeIndexLocked();
        } catch (const std::bad_alloc&) {
            RecordAllocationFailureLocked(
                AllocationFailureReason::kMetadataAllocationFailure, size,
                normalized, alignment);
            return 0;
        }
        return AllocateAlignedLocked(size, alignment, commit_pages,
                                     mapping, false);
    }
    RecordAllocationFailureLocked(
        alignment == kAlignment
            ? AllocationFailureReason::kMetadataIndexInconsistency
            : AllocationFailureReason::kNoFittingExtent,
        size, normalized, alignment);
    return 0;
}

std::uint64_t GuestHeap::Callocate(std::uint64_t count, std::uint64_t size) {
    g_alloc_entry_tag = 5;
    const std::unique_lock lock(mutex_);
    if (count != 0 &&
        size > std::numeric_limits<std::uint64_t>::max() / count) {
        RecordAllocationFailureLocked(
            AllocationFailureReason::kInvalidRequest, size, std::nullopt,
            kAlignment);
        return 0;
    }
    const std::uint64_t total = count * size;
    const std::uint64_t address = AllocateLocked(total);
    if (address == 0) return 0;
    const std::optional<std::size_t> block = FindAllocatedBlock(address);
    if (!block) return 0;
    const Block& allocated = blocks_.at(*block);
    std::fill(memory_ + *block,
              memory_ + *block + allocated.size,
              std::uint8_t{0});
    ConfigureWatchLocked(*block, &blocks_.at(*block), "calloc-zero");
    return address;
}

std::uint64_t GuestHeap::CallocateAligned(std::uint64_t size,
                                          std::uint64_t alignment) {
    g_alloc_entry_tag = 6;
    const std::unique_lock lock(mutex_);
    const std::uint64_t address = AllocateAlignedLocked(size, alignment);
    if (address == 0) return 0;
    const std::optional<std::size_t> block = FindAllocatedBlock(address);
    if (!block) return 0;
    const Block& allocated = blocks_.at(*block);
    std::fill(memory_ + *block,
              memory_ + *block + allocated.size,
              std::uint8_t{0});
    ConfigureWatchLocked(*block, &blocks_.at(*block), "calloc-zero");
    return address;
}

std::uint64_t GuestHeap::ReserveMappingAligned(
    std::uint64_t size, std::uint64_t alignment, bool readable,
    bool writable, std::string* error, std::uint64_t preferred) {
    g_alloc_entry_tag = 7;
    const std::unique_lock lock(mutex_);
    if (size == 0 || (size & (kHostPageSize - 1)) != 0 ||
        alignment < kHostPageSize ||
        (alignment & (alignment - 1)) != 0 ||
        (guest_base_ & (kHostPageSize - 1)) != 0) {
        SetError(error, "guest mapping reservation is not page aligned");
        return 0;
    }
    std::uint64_t address = 0;
    // A non-MAP_FIXED placement hint requests a specific guest address
    // (Boehm GC_expand_hp/GC_scratch_alloc grow the scratch arena
    // contiguously). Honour it only when the exact extent is free; a taken or
    // out-of-arena hint falls back to ordinary placement.
    if (preferred != 0 && (preferred & (kHostPageSize - 1)) == 0) {
        const std::optional<std::uint64_t> carved =
            TryCarveAtLocked(preferred, size, false, true);
        if (carved) address = *carved;
    }
    if (address == 0)
        address = AllocateAlignedLocked(size, alignment, false, true);
    if (address == 0) {
        SetError(error, "guest mapping reservation is out of address space");
        return 0;
    }
    const std::size_t offset =
        static_cast<std::size_t>(address - guest_base_);
    const std::size_t checked_size = static_cast<std::size_t>(size);
    if (!DecommitRangeLocked(offset, checked_size, error) ||
        !SetProtectionLocked(offset, checked_size, readable, writable,
                             error)) {
        ReleaseLocked(address, nullptr);
        return 0;
    }
    AdvanceMetadataGenerationLocked();
    return address;
}

std::optional<std::uint64_t> GuestHeap::TryCarveAtLocked(
    std::uint64_t preferred, std::uint64_t size, bool commit_pages,
    bool mapping) {
    if (preferred < guest_base_) return std::nullopt;
    const std::uint64_t offset64 = preferred - guest_base_;
    if (offset64 > std::numeric_limits<std::size_t>::max())
        return std::nullopt;
    const std::size_t carve_offset = static_cast<std::size_t>(offset64);
    std::optional<std::size_t> normalized = NormalizeSize(size);
    if (!normalized) return std::nullopt;
    normalized.value() += EffectiveOverflowRedzone();
    if (normalized.value() > capacity_ ||
        carve_offset > capacity_ - normalized.value())
        return std::nullopt;
    // Locate the free extent containing [carve_offset, carve_offset+normalized).
    auto block_it = blocks_.upper_bound(carve_offset);
    if (block_it == blocks_.begin()) return std::nullopt;
    --block_it;
    const std::size_t block_offset = block_it->first;
    const Block& candidate = block_it->second;
    const std::size_t block_end = block_offset + candidate.size;
    if (candidate.allocated || carve_offset < block_offset ||
        normalized.value() > block_end - carve_offset)
        return std::nullopt;
    std::fprintf(stderr,
                 "ISOMR_HEAP_CARVE pref=%llx off=%zx norm=%zx blk_off=%zx "
                 "blk_size=%zx\n",
                 static_cast<unsigned long long>(preferred), carve_offset,
                 *normalized, block_offset, candidate.size);
    std::fflush(stderr);
    const std::size_t prefix = carve_offset - block_offset;
    const std::size_t suffix = block_end - carve_offset - *normalized;
    const auto* indexed = free_extents_.Find(block_offset);
    if (indexed == nullptr || indexed->size != candidate.size)
        return std::nullopt;  // Quarantined/unindexed extent: leave it alone.
    std::map<std::size_t, Block> staged_blocks;
    std::map<std::size_t, ProtectionRange> staged_protection;
    FreeExtentIndex::Handle prefix_node;
    FreeExtentIndex::Handle suffix_node;
    try {
        if (prefix != 0) {
            MaybeFailMetadataAllocationLocked();
            staged_blocks.emplace(block_offset, Block{prefix, false});
            MaybeFailMetadataAllocationLocked();
            prefix_node = FreeExtentIndex::MakeNode(block_offset, prefix);
        }
        MaybeFailMetadataAllocationLocked();
        staged_blocks.emplace(carve_offset,
                              Block{*normalized, true, true, mapping});
        if (suffix != 0) {
            MaybeFailMetadataAllocationLocked();
            const std::size_t suffix_offset = carve_offset + *normalized;
            staged_blocks.emplace(suffix_offset, Block{suffix, false});
            MaybeFailMetadataAllocationLocked();
            suffix_node =
                FreeExtentIndex::MakeNode(suffix_offset, suffix);
        }
        MaybeFailMetadataAllocationLocked();
        staged_protection.emplace(
            carve_offset,
            ProtectionRange{carve_offset + *normalized, true, true});
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
    if (commit_pages &&
        !CommitRangeLocked(carve_offset, *normalized, nullptr))
        return std::nullopt;
    try {
        ConfigureWatchLocked(carve_offset,
                             &staged_blocks.at(carve_offset), "mmap-carve");
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
    auto original_free_node = free_extents_.Extract(block_offset);
    if (original_free_node == nullptr) return std::nullopt;
    auto original_block_node = blocks_.extract(block_it);
    if (original_block_node.empty()) {
        free_extents_.Insert(std::move(original_free_node));
        return std::nullopt;
    }
    while (!staged_blocks.empty()) {
        auto node = staged_blocks.extract(staged_blocks.begin());
        blocks_.insert(std::move(node));
    }
    RemoveProtectionLocked(carve_offset, *normalized);
    while (!staged_protection.empty()) {
        auto node = staged_protection.extract(staged_protection.begin());
        protections_.insert(std::move(node));
    }
    if (prefix_node != nullptr)
        free_extents_.Insert(std::move(prefix_node));
    if (suffix_node != nullptr)
        free_extents_.Insert(std::move(suffix_node));
    AdvanceMetadataGenerationLocked();
    return preferred;
}

void GuestHeap::PreservePinnedLocked(std::size_t offset, std::size_t size) {
    if (size == 0) return;
    const std::size_t end = offset + size;
    auto block = blocks_.upper_bound(offset);
    if (block != blocks_.begin()) --block;
    for (; block != blocks_.end() && block->first < end; ++block) {
        const Block& value = block->second;
        if (!value.allocated || !value.pinned) continue;
        const std::size_t pin_start = std::max(block->first, offset);
        const std::size_t pin_end =
            std::min(block->first + value.size, end);
        if (pin_start >= pin_end) continue;
        // The host still owns pinned objects; keep their pages committed and
        // readable+writable even when the guest deprotects the enclosing range.
        (void)CommitRangeLocked(pin_start, pin_end - pin_start, nullptr);
        RemoveProtectionLocked(pin_start, pin_end - pin_start);
        protections_.emplace(pin_start, ProtectionRange{pin_end, true, true});
    }
}

bool GuestHeap::PagePinnedLocked(std::size_t page) const {
    const std::size_t page_offset = page * kHostPageSize;
    const std::size_t page_end = page_offset + kHostPageSize;
    auto block = blocks_.upper_bound(page_offset);
    if (block != blocks_.begin()) --block;
    for (; block != blocks_.end() && block->first < page_end; ++block) {
        const Block& value = block->second;
        if (value.allocated && value.pinned &&
            block->first + value.size > page_offset)
            return true;
    }
    return false;
}

struct GuestHeap::DirectPageTableState {
    std::mutex build_mutex;
    std::vector<void*> entries;
    std::size_t address_space_bits = 0;
    bool ready = false;
};

bool GuestHeap::BuildDirectPageTable(std::size_t address_space_bits) {
    if (watch_allocation_size_ != 0 || !valid_configuration_ ||
        memory_ == nullptr) {
        return false;
    }
    if (address_space_bits < 12 || address_space_bits > 36) {
        return false;
    }
    const std::uint64_t space_end =
        std::uint64_t{1} << address_space_bits;
    if (guest_base_ >= space_end ||
        capacity_ > space_end - guest_base_) {
        return false;
    }
    if (!direct_page_table_) {
        direct_page_table_ = std::make_unique<DirectPageTableState>();
    }
    DirectPageTableState& state = *direct_page_table_;
    const std::lock_guard build_lock(state.build_mutex);
    if (state.ready) {
        return state.address_space_bits == address_space_bits;
    }
    std::vector<void*> table(
        std::size_t{1} << (address_space_bits - 12), nullptr);
    bool ready = false;
    {
        const std::unique_lock lock(mutex_);
        for (std::size_t page = 0; page < committed_pages_.size(); ++page) {
            if (committed_pages_[page] == 0) continue;
            table[(guest_base_ >> 12) + page] =
                memory_ + page * kHostPageSize;
        }
        state.entries = std::move(table);
        state.address_space_bits = address_space_bits;
        state.ready = true;
        ready = true;
    }
    if (ready) {
        std::printf(
            "ISOMR_AARCH64_DIRECT_HEAP_TABLE state=enabled bits=%zu heap-base=0x%llx\n",
            address_space_bits,
            static_cast<unsigned long long>(guest_base_));
    }
    return true;
}

void** GuestHeap::direct_page_table_entries() const noexcept {
    return direct_page_table_ && direct_page_table_->ready
               ? const_cast<void**>(direct_page_table_->entries.data())
               : nullptr;
}

std::size_t GuestHeap::direct_page_table_address_space_bits() const noexcept {
    return direct_page_table_ ? direct_page_table_->address_space_bits : 0;
}

bool GuestHeap::CommitRangeLocked(std::size_t offset, std::size_t size,
                                  std::string* error) {
    if (size == 0) return true;
    if (offset >= capacity_ || size > capacity_ - offset) {
        SetError(error, "guest commit range is outside the virtual arena");
        return false;
    }
    const std::size_t page_start = offset & ~(kHostPageSize - 1);
    const std::size_t end = offset + size;
    const std::size_t page_end =
        (end + kHostPageSize - 1) & ~(kHostPageSize - 1);
    if (VirtualAlloc(memory_ + page_start, page_end - page_start,
                     MEM_COMMIT, PAGE_READWRITE) == nullptr) {
        SetError(error, "Win32 guest-page commit failed");
        return false;
    }
    const std::size_t first_page = page_start / kHostPageSize;
    const std::size_t last_page = page_end / kHostPageSize;
    std::fill(committed_pages_.begin() +
                  static_cast<std::ptrdiff_t>(first_page),
              committed_pages_.begin() +
                  static_cast<std::ptrdiff_t>(last_page),
              std::uint8_t{1});
    if (direct_page_table_ && direct_page_table_->ready) {
        const std::size_t base_page = guest_base_ >> 12;
        for (std::size_t page = first_page; page < last_page; ++page) {
            direct_page_table_->entries[base_page + page] =
                memory_ + page * kHostPageSize;
        }
    }
    return true;
}

bool GuestHeap::DecommitRangeLocked(std::size_t offset, std::size_t size,
                                    std::string* error) {
    if (size == 0) return true;
    if ((offset & (kHostPageSize - 1)) != 0 ||
        (size & (kHostPageSize - 1)) != 0 || offset >= capacity_ ||
        size > capacity_ - offset) {
        SetError(error, "guest decommit range is not page aligned");
        return false;
    }
    const std::size_t first_page = offset / kHostPageSize;
    const std::size_t last_page = (offset + size) / kHostPageSize;
    if (direct_page_table_ && direct_page_table_->ready) {
        // Direct-table mode keeps the host pages committed: zero committed
        // pages so guest content semantics match decommit, mark the pages
        // uncommitted for bookkeeping, and leave the direct entries valid.
        // A decommit racing an in-flight JIT access could otherwise fault on
        // a host page that callbacks would have handled. Never-committed
        // pages stay PAGE_NOACCESS and must not be written here. Pinned pages
        // are skipped entirely so host-owned objects keep their contents.
        for (std::size_t page = first_page; page < last_page; ++page) {
            if (PagePinnedLocked(page) || committed_pages_[page] == 0)
                continue;
            std::memset(memory_ + page * kHostPageSize, 0, kHostPageSize);
        }
    } else {
        // Decommit maximal non-pinned page runs so pinned objects keep their
        // host backing even when the guest unmaps the enclosing range.
        std::size_t page = first_page;
        while (page < last_page) {
            if (PagePinnedLocked(page)) {
                ++page;
                continue;
            }
            std::size_t run_end = page + 1;
            while (run_end < last_page && !PagePinnedLocked(run_end))
                ++run_end;
            if (!VirtualFree(memory_ + page * kHostPageSize,
                             (run_end - page) * kHostPageSize,
                             MEM_DECOMMIT)) {
                SetError(error, "Win32 guest-page decommit failed");
                return false;
            }
            page = run_end;
        }
    }
    for (std::size_t page = first_page; page < last_page; ++page) {
        if (!PagePinnedLocked(page)) committed_pages_[page] = 0;
    }
    return true;
}

bool GuestHeap::IsAllocatedRangeLocked(std::size_t offset, std::size_t size,
                                       bool mapping_only) const {
    if (offset > capacity_ || size > capacity_ - offset) return false;
    if (size == 0) return true;
    const std::size_t end = offset + size;
    std::size_t cursor = offset;
    auto block = blocks_.upper_bound(offset);
    if (block == blocks_.begin()) return false;
    --block;
    for (; block != blocks_.end(); ++block) {
        const std::size_t block_offset = block->first;
        const Block& value = block->second;
        const std::size_t block_end = block_offset + value.size;
        if (block_end <= cursor) continue;
        if (block_offset > cursor || !value.allocated ||
            (mapping_only && !value.mapping))
            return false;
        cursor = std::min(end, block_end);
        if (cursor == end) return true;
    }
    return false;
}

bool GuestHeap::HasAccessLocked(std::size_t offset, std::size_t size,
                                bool write) const {
    if (!IsAllocatedRangeLocked(offset, size, false)) return false;
    return HasProtectionAccessLocked(offset, size, write);
}

bool GuestHeap::HasProtectionAccessLocked(std::size_t offset,
                                          std::size_t size,
                                          bool write) const {
    if (size == 0) return true;
    const std::size_t end = offset + size;
    std::size_t cursor = offset;
    auto range = protections_.upper_bound(cursor);
    if (range != protections_.begin()) --range;
    while (cursor < end) {
        if (range == protections_.end() || range->first > cursor ||
            range->second.end <= cursor ||
            (write ? !range->second.writable
                   : !range->second.readable))
            return false;
        cursor = std::min(end, range->second.end);
        ++range;
    }
    return true;
}

bool GuestHeap::ResolveAccessSpanLocked(
    std::size_t offset, bool write, std::size_t* span_start,
    std::size_t* span_end, std::size_t* allocation_end) const {
    if (span_start == nullptr || span_end == nullptr ||
        allocation_end == nullptr || offset >= capacity_)
        return false;
    auto block = blocks_.upper_bound(offset);
    if (block == blocks_.begin()) return false;
    --block;
    const std::size_t block_end = block->first + block->second.size;
    if (!block->second.allocated || offset >= block_end) return false;

    auto protection = protections_.upper_bound(offset);
    if (protection == protections_.begin()) return false;
    --protection;
    if (protection->first > offset || protection->second.end <= offset ||
        (write ? !protection->second.writable
               : !protection->second.readable))
        return false;
    *span_start = std::max(block->first, protection->first);
    *span_end = std::min(block_end, protection->second.end);
    *allocation_end = block_end;
    return *span_start <= offset && offset < *span_end;
}

void GuestHeap::StageProtectionRemovalLocked(
    std::size_t offset, std::size_t size,
    std::map<std::size_t, ProtectionRange>* preserved,
    bool inject_failure) {
    if (size == 0) return;
    const std::size_t end = offset + size;
    auto range = protections_.lower_bound(offset);
    if (range != protections_.begin()) {
        auto previous = std::prev(range);
        if (previous->second.end > offset) range = previous;
    }
    while (range != protections_.end() && range->first < end) {
        const std::size_t start = range->first;
        const ProtectionRange value = range->second;
        if (start < offset) {
            if (inject_failure) MaybeFailMetadataAllocationLocked();
            preserved->emplace(
                start, ProtectionRange{offset, value.readable,
                                       value.writable});
        }
        if (value.end > end) {
            if (inject_failure) MaybeFailMetadataAllocationLocked();
            preserved->emplace(
                end, ProtectionRange{value.end, value.readable,
                                     value.writable});
        }
        ++range;
    }
}

void GuestHeap::ApplyProtectionRemovalLocked(
    std::size_t offset, std::size_t size,
    std::map<std::size_t, ProtectionRange>* preserved) noexcept {
    if (size == 0) return;
    const std::size_t end = offset + size;
    auto range = protections_.lower_bound(offset);
    if (range != protections_.begin()) {
        auto previous = std::prev(range);
        if (previous->second.end > offset) range = previous;
    }
    while (range != protections_.end() && range->first < end) {
        range = protections_.erase(range);
    }
    while (!preserved->empty()) {
        auto node = preserved->extract(preserved->begin());
        protections_.insert(std::move(node));
    }
}

void GuestHeap::RemoveProtectionLocked(std::size_t offset,
                                       std::size_t size) {
    std::map<std::size_t, ProtectionRange> preserved;
    StageProtectionRemovalLocked(offset, size, &preserved, false);
    ApplyProtectionRemovalLocked(offset, size, &preserved);
}

void GuestHeap::MergeProtectionLocked() {
    auto range = protections_.begin();
    while (range != protections_.end()) {
        auto next = std::next(range);
        if (next == protections_.end()) break;
        if (range->second.end == next->first &&
            range->second.readable == next->second.readable &&
            range->second.writable == next->second.writable) {
            range->second.end = next->second.end;
            protections_.erase(next);
        } else {
            range = next;
        }
    }
}

bool GuestHeap::SetProtectionLocked(std::size_t offset, std::size_t size,
                                    bool readable, bool writable,
                                    std::string* error) {
    if (!IsAllocatedRangeLocked(offset, size, false)) {
        SetError(error, "guest protection range is not fully allocated");
        return false;
    }
    if ((readable || writable) &&
        !CommitRangeLocked(offset, size, error))
        return false;
    RemoveProtectionLocked(offset, size);
    if (size != 0)
        protections_.emplace(
            offset, ProtectionRange{offset + size, readable, writable});
    PreservePinnedLocked(offset, size);
    MergeProtectionLocked();
    return true;
}

void GuestHeap::ConfigureWatchLocked(std::size_t offset, Block* block,
                                     const char* initial_kind) {
    if (block == nullptr) return;
    block->watch.reset();
    if (watch_allocation_size_ == 0 || !block->allocated ||
        (block->size != watch_allocation_size_ &&
         block->size !=
             watch_allocation_size_ + EffectiveOverflowRedzone()) ||
        watch_range_offset_ > block->size ||
        watch_range_size_ > block->size - watch_range_offset_)
        return;
    block->watch = std::make_unique<WatchState>();
    block->watch->generation = next_watch_generation_++;
    // A set alloc debug tag names the allocation's logical role (e.g.
    // "dl-module-name"); otherwise fall back to the caller's kind label.
    const char* kind = alloc_debug_tag_.empty() ? initial_kind
                                                : alloc_debug_tag_.c_str();
    const std::size_t kind_size =
        std::min(std::strlen(kind),
                 block->watch->initial_kind.size() - 1);
    std::memcpy(block->watch->initial_kind.data(), kind, kind_size);
    std::memcpy(&block->watch->initial_value,
                memory_ + offset + watch_range_offset_,
                watch_range_size_);
}

std::optional<std::size_t> GuestHeap::FindAllocatedBlock(
    std::uint64_t address) const {
    if (!ContainsAddress(address)) return std::nullopt;
    const std::size_t offset = static_cast<std::size_t>(address - guest_base_);
    const auto block = blocks_.find(offset);
    if (block != blocks_.end() && block->second.allocated) return offset;
    return std::nullopt;
}

std::optional<std::size_t> GuestHeap::FindAllocatedRange(
    std::uint64_t address, std::size_t size) const {
    if (!ContainsAddress(address)) return std::nullopt;
    const std::size_t offset = static_cast<std::size_t>(address - guest_base_);
    if (size > capacity_ - offset) return std::nullopt;
    auto block = blocks_.upper_bound(offset);
    if (block == blocks_.begin()) return std::nullopt;
    --block;
    const std::size_t relative = offset - block->first;
    if (block->second.allocated && relative <= block->second.size &&
        size <= block->second.size - relative)
        return block->first;
    return std::nullopt;
}

void GuestHeap::InsertFreeIndexLocked(std::size_t offset) {
    const auto block = blocks_.find(offset);
    if (block != blocks_.end() && !block->second.allocated)
        free_extents_.Insert(
            FreeExtentIndex::MakeNode(offset, block->second.size));
}

void GuestHeap::EraseFreeIndexLocked(std::size_t offset) {
    const auto block = blocks_.find(offset);
    if (block != blocks_.end() && !block->second.allocated)
        free_extents_.Extract(offset);
}

bool GuestHeap::MergeFreeBlock(std::size_t offset) {
    auto block = blocks_.find(offset);
    if (block == blocks_.end() || block->second.allocated) return false;
    auto next = std::next(block);
    auto previous = block == blocks_.begin() ? blocks_.end()
                                              : std::prev(block);
    if (free_extents_.Find(block->first) == nullptr ||
        (next != blocks_.end() && !next->second.allocated &&
         free_extents_.Find(next->first) == nullptr) ||
        (previous != blocks_.end() && !previous->second.allocated &&
         free_extents_.Find(previous->first) == nullptr))
        return false;
    auto free_node = free_extents_.Extract(block->first);
    if (free_node == nullptr) return false;
    if (next != blocks_.end() && !next->second.allocated) {
        EraseFreeIndexLocked(next->first);
        block->second.size += next->second.size;
        blocks_.erase(next);
    }
    if (block != blocks_.begin()) {
        if (!previous->second.allocated) {
            EraseFreeIndexLocked(previous->first);
            previous->second.size += block->second.size;
            blocks_.erase(block);
            block = previous;
        }
    }
    free_node->offset = block->first;
    free_node->size = block->second.size;
    return free_extents_.Insert(std::move(free_node));
}

void GuestHeap::RebuildFreeIndexLocked() {
    // Re-derive the free-extents index from the authoritative block map and
    // coalesce adjacent free extents (restoring the no-adjacent-free
    // invariant). Used to recover from a stale/over-reported index node that
    // would otherwise strand a fitting extent or trip the allocation-path
    // consistency check.
    // Pass 1 computes merged extents; pass 2 builds the index. Both allocate
    // before any mutation of blocks_/free_extents_, so a bad_alloc leaves the
    // prior (consistent) state untouched.
    std::vector<std::pair<std::size_t, std::size_t>> merged;
    for (const auto& [offset, block] : blocks_) {
        if (block.allocated) continue;
        if (!merged.empty() &&
            merged.back().first + merged.back().second == offset) {
            merged.back().second += block.size;
        } else {
            merged.emplace_back(offset, block.size);
        }
    }
    FreeExtentIndex fresh;
    for (const auto& [offset, size] : merged)
        fresh.Insert(FreeExtentIndex::MakeNode(offset, size));
    // Pass 3: coalesce adjacent free blocks in blocks_ to match the index.
    // erase() does not allocate, so this cannot throw.
    for (auto block = blocks_.begin(); block != blocks_.end();) {
        if (block->second.allocated) {
            ++block;
            continue;
        }
        auto next = std::next(block);
        while (next != blocks_.end() && !next->second.allocated) {
            block->second.size += next->second.size;
            auto erase_it = next++;
            blocks_.erase(erase_it);
        }
        ++block;
    }
    free_extents_.Swap(fresh);
}

bool GuestHeap::Reallocate(std::uint64_t address, std::uint64_t size,
                           std::uint64_t* result, std::string* error) {
    g_alloc_entry_tag = 8;
    const std::unique_lock lock(mutex_);
    if (result == nullptr) {
        SetError(error, "guest realloc result pointer is null");
        return false;
    }
    last_realloc_zero_ = nullptr;
    if (address == 0) {
        *result = AllocateLocked(size);
        return true;
    }
    if (size == 0) {
        if (!ReleaseLocked(address, error)) return false;
        *result = 0;
        return true;
    }
    const std::optional<std::size_t> block_index =
        FindAllocatedBlock(address);
    std::optional<std::size_t> normalized = NormalizeSize(size);
    if (!block_index) {
        SetError(error, "guest realloc pointer is not an active allocation");
        return false;
    }
    if (!normalized) {
        last_realloc_zero_ = "normalize-fail";
        *result = 0;
        return true;
    }
    // Keep realloc'd blocks consistent with Allocate: every block carries the
    // overflow redzone, so a block grown or shrunk through realloc keeps the
    // same overrun slack as a freshly allocated one instead of losing it.
    *normalized += EffectiveOverflowRedzone();

    const std::size_t old_offset = *block_index;
    Block& current = blocks_.at(old_offset);
    const std::size_t old_size = current.size;
    if (*normalized <= old_size) {
        const std::size_t remainder = old_size - *normalized;
        const std::size_t remainder_offset = old_offset + *normalized;
        std::map<std::size_t, ProtectionRange> preserved_protection;
        if (remainder != 0) {
            std::map<std::size_t, Block> staged;
            FreeExtentIndex::Handle free_node;
            try {
                MaybeFailMetadataAllocationLocked();
                staged.emplace(remainder_offset, Block{remainder, false});
                MaybeFailMetadataAllocationLocked();
                free_node = FreeExtentIndex::MakeNode(remainder_offset,
                                                      remainder);
                StageProtectionRemovalLocked(
                    old_offset + *normalized, remainder,
                    &preserved_protection, true);
            } catch (const std::bad_alloc&) {
                last_realloc_zero_ = "shrink-meta-oom";
                *result = 0;
                return true;
            }
            blocks_.insert(staged.extract(staged.begin()));
            free_extents_.Insert(std::move(free_node));
        }
        ApplyProtectionRemovalLocked(old_offset + *normalized, remainder,
                                     &preserved_protection);
        current.size = *normalized;
        if (remainder != 0) {
            if (!MergeFreeBlock(remainder_offset)) {
                last_realloc_zero_ = "shrink-merge-fail";
                *result = 0;
                return true;
            }
        }
        ConfigureWatchLocked(old_offset, &blocks_.at(old_offset),
                             "realloc-in-place");
        AdvanceMetadataGenerationLocked();
        *result = address;
        return true;
    }

    auto next = blocks_.upper_bound(old_offset);
    // A free-but-unindexed neighbor is a quarantined extent: ReleaseLocked
    // marks it allocated=false yet holds it out of free_extents_ until the
    // quarantine flushes. Growing into it makes free_extents_.Extract below
    // fail with a spurious "grow-inconsistent" miss, so only an indexed free
    // neighbor is a valid in-place merge candidate; otherwise fall through to
    // AllocateLocked, matching how AllocateLocked skips quarantined extents.
    if (next != blocks_.end() && !next->second.allocated &&
        free_extents_.Find(next->first) != nullptr &&
        free_extents_.Find(next->first)->size == next->second.size &&
        old_size + next->second.size >= *normalized) {
        const std::size_t needed = *normalized - old_size;
        std::map<std::size_t, ProtectionRange> staged_protection;
        try {
            MaybeFailMetadataAllocationLocked();
            staged_protection.emplace(
                old_offset + old_size,
                ProtectionRange{old_offset + *normalized, true, true});
        } catch (const std::bad_alloc&) {
            last_realloc_zero_ = "grow-meta-oom";
            *result = 0;
            return true;
        }
        if (!CommitRangeLocked(old_offset + old_size, needed, error)) {
            last_realloc_zero_ = "grow-commit-fail";
            *result = 0;
            return true;
        }
        const std::size_t next_offset = next->first;
        const std::size_t next_size = next->second.size;
        auto free_node = free_extents_.Extract(next_offset);
        auto block_node = blocks_.extract(next);
        if (free_node == nullptr || block_node.empty()) {
            // This can only indicate an internal metadata inconsistency.
            if (!block_node.empty()) blocks_.insert(std::move(block_node));
            if (free_node != nullptr)
                free_extents_.Insert(std::move(free_node));
            last_realloc_zero_ = "grow-inconsistent";
            *result = 0;
            return true;
        }
        current.size = *normalized;
        if (next_size != needed) {
            const std::size_t remaining_offset = next_offset + needed;
            block_node.key() = remaining_offset;
            block_node.mapped().size = next_size - needed;
            free_node->offset = remaining_offset;
            free_node->size = next_size - needed;
            blocks_.insert(std::move(block_node));
            free_extents_.Insert(std::move(free_node));
        }
        protections_.insert(
            staged_protection.extract(staged_protection.begin()));
        MergeProtectionLocked();
        ConfigureWatchLocked(old_offset, &blocks_.at(old_offset),
                             "realloc-in-place");
        AdvanceMetadataGenerationLocked();
        *result = address;
        return true;
    }

    FreeExtentIndex::Handle old_release_node;
    std::map<std::size_t, ProtectionRange> old_preserved_protection;
    try {
        MaybeFailMetadataAllocationLocked();
        old_release_node = FreeExtentIndex::MakeNode(old_offset, old_size);
        StageProtectionRemovalLocked(old_offset, old_size,
                                     &old_preserved_protection, true);
    } catch (const std::bad_alloc&) {
        last_realloc_zero_ = "newalloc-meta-oom";
        *result = 0;
        return true;
    }
    const std::uint64_t replacement = AllocateLocked(size);
    if (replacement == 0) {
        last_realloc_zero_ = "newalloc-alloc-fail";
        *result = 0;
        return true;
    }
    const std::size_t replacement_offset =
        static_cast<std::size_t>(replacement - guest_base_);
    std::memmove(memory_ + replacement_offset,
                 memory_ + old_offset, old_size);
    const std::optional<std::size_t> replacement_block =
        FindAllocatedBlock(replacement);
    if (replacement_block)
        ConfigureWatchLocked(*replacement_block,
                             &blocks_.at(*replacement_block),
                             "realloc-copy");
    if (!ReleaseLocked(address, error, std::move(old_release_node),
                       std::move(old_preserved_protection), true))
        return false;
    *result = replacement;
    return true;
}

bool GuestHeap::Release(std::uint64_t address, std::string* error) {
    const std::unique_lock lock(mutex_);
    return ReleaseLocked(address, error);
}

bool GuestHeap::ReleaseLocked(std::uint64_t address, std::string* error,
                              FreeExtentIndex::Handle released_node,
                              std::map<std::size_t, ProtectionRange>
                                  preserved_protection,
                              bool protection_staged) {
    if (address == 0) return true;
    const std::optional<std::size_t> block = FindAllocatedBlock(address);
    if (!block) {
        SetError(error, "guest free pointer is not an active allocation");
        return false;
    }
    Block& released = blocks_.at(*block);
    if (released_node == nullptr) {
        try {
            MaybeFailMetadataAllocationLocked();
            released_node = FreeExtentIndex::MakeNode(*block, released.size);
            if (!protection_staged)
                StageProtectionRemovalLocked(*block, released.size,
                                             &preserved_protection, true);
        } catch (const std::bad_alloc&) {
            SetError(error, "guest free metadata allocation failed");
            return false;
        }
    } else {
        released_node->offset = *block;
        released_node->size = released.size;
        if (!protection_staged) {
            try {
                StageProtectionRemovalLocked(*block, released.size,
                                             &preserved_protection, true);
            } catch (const std::bad_alloc&) {
                SetError(error, "guest free metadata allocation failed");
                return false;
            }
        }
    }
    if (free_extents_.Find(*block) != nullptr) {
        SetError(error, "guest free metadata index already exists");
        return false;
    }
    ApplyProtectionRemovalLocked(*block, released.size,
                                 &preserved_protection);
    released.allocated = false;
    released.writable = true;
    released.mapping = false;
    released.pinned = false;
    released.watch.reset();
    // Hold the extent in the quarantine instead of recycling it
    // immediately; the node handle is no longer needed.
    (void)released_node;
    QuarantineExtentLocked(*block, released.size);
    AdvanceMetadataGenerationLocked();
    return true;
}

void GuestHeap::QuarantineExtentLocked(std::size_t offset,
                                       std::size_t size) {
    if (!s_device_allocator_fidelity_) {
        // Fidelity off: recycle immediately like a plain allocator — insert
        // the extent into the free index and coalesce with neighbors.
        auto node = FreeExtentIndex::MakeNode(offset, size);
        if (free_extents_.Insert(std::move(node)))
            (void)MergeFreeBlock(offset);
        return;
    }
    constexpr std::size_t kQuarantineCapacity = 16U * 1024U * 1024U;
    free_quarantine_.emplace_back(offset, size);
    quarantine_bytes_ += size;
    while (quarantine_bytes_ > kQuarantineCapacity &&
           !free_quarantine_.empty()) {
        auto oldest = free_quarantine_.front();
        free_quarantine_.pop_front();
        quarantine_bytes_ -= oldest.second;
        auto node = FreeExtentIndex::MakeNode(oldest.first, oldest.second);
        if (free_extents_.Insert(std::move(node)))
            (void)MergeFreeBlock(oldest.first);
    }
}

void GuestHeap::FlushQuarantineLocked() {
    while (!free_quarantine_.empty()) {
        auto extent = free_quarantine_.front();
        free_quarantine_.pop_front();
        quarantine_bytes_ -= extent.second;
        auto node = FreeExtentIndex::MakeNode(extent.first, extent.second);
        if (free_extents_.Insert(std::move(node)))
            (void)MergeFreeBlock(extent.first);
    }
}

bool GuestHeap::ReleaseMappingRange(std::uint64_t address,
                                    std::uint64_t size,
                                    std::string* error) {
    const std::unique_lock lock(mutex_);
    if (address < guest_base_ ||
        (address & (kHostPageSize - 1)) != 0 ||
        size == 0 || (size & (kHostPageSize - 1)) != 0 ||
        size > std::numeric_limits<std::size_t>::max()) {
        SetError(error, "guest mapping release range is not page aligned");
        return false;
    }
    const std::size_t offset =
        static_cast<std::size_t>(address - guest_base_);
    const std::size_t checked_size = static_cast<std::size_t>(size);
    if (!IsAllocatedRangeLocked(offset, checked_size, true)) {
        SetError(error, "guest mapping release range is not fully mapped");
        return false;
    }
    const std::size_t end = offset + checked_size;
    std::map<std::size_t, Block> replacement;
    std::map<std::size_t, ProtectionRange> preserved_protection;
    FreeExtentIndex replacement_free;
    try {
        for (const auto& [block_offset, block] : blocks_) {
            const std::size_t block_end = block_offset + block.size;
            // Pinned runtime blocks are never released by a guest munmap; keep
            // them allocated even when the released range overlaps them.
            if (block_end <= offset || block_offset >= end || block.pinned) {
                Block copy{block.size, block.allocated, block.writable,
                           block.mapping, block.pinned};
                if (block.watch != nullptr)
                    copy.watch = std::make_unique<WatchState>(*block.watch);
                MaybeFailMetadataAllocationLocked();
                replacement.emplace(block_offset, std::move(copy));
                continue;
            }
            const std::size_t overlap_start =
                std::max(block_offset, offset);
            const std::size_t overlap_end = std::min(block_end, end);
            if (block_offset < overlap_start)
            {
                MaybeFailMetadataAllocationLocked();
                replacement.emplace(
                    block_offset,
                    Block{overlap_start - block_offset, true,
                          block.writable, true});
            }
            MaybeFailMetadataAllocationLocked();
            replacement.emplace(
                overlap_start,
                Block{overlap_end - overlap_start, false});
            if (overlap_end < block_end)
            {
                MaybeFailMetadataAllocationLocked();
                replacement.emplace(
                    overlap_end,
                    Block{block_end - overlap_end, true,
                          block.writable, true});
            }
        }

        for (auto block = replacement.begin(); block != replacement.end();) {
            auto next = std::next(block);
            if (next == replacement.end()) break;
            if (!block->second.allocated && !next->second.allocated) {
                block->second.size += next->second.size;
                replacement.erase(next);
            } else {
                block = next;
            }
        }
        for (const auto& [block_offset, block] : replacement) {
            if (!block.allocated) {
                MaybeFailMetadataAllocationLocked();
                replacement_free.Insert(
                    FreeExtentIndex::MakeNode(block_offset, block.size));
            }
        }
        StageProtectionRemovalLocked(offset, checked_size,
                                     &preserved_protection, true);
    } catch (const std::bad_alloc&) {
        SetError(error, "guest mapping metadata allocation failed");
        return false;
    }
    if (!DecommitRangeLocked(offset, checked_size, error)) return false;
    blocks_ = std::move(replacement);
    free_extents_.Swap(replacement_free);
    ApplyProtectionRemovalLocked(offset, checked_size,
                                 &preserved_protection);
    AdvanceMetadataGenerationLocked();
    return true;
}

bool GuestHeap::DiscardMappingRange(std::uint64_t address,
                                    std::uint64_t size,
                                    std::string* error) {
    const std::unique_lock lock(mutex_);
    if (address < guest_base_ ||
        (address & (kHostPageSize - 1)) != 0 || size == 0 ||
        (size & (kHostPageSize - 1)) != 0 ||
        size > std::numeric_limits<std::size_t>::max()) {
        SetError(error, "guest mapping discard range is not page aligned");
        return false;
    }
    const std::size_t offset =
        static_cast<std::size_t>(address - guest_base_);
    const std::size_t checked_size = static_cast<std::size_t>(size);
    if (!IsAllocatedRangeLocked(offset, checked_size, true)) {
        SetError(error, "guest mapping discard range is not fully mapped");
        return false;
    }
    if (!DecommitRangeLocked(offset, checked_size, error)) return false;
    const std::size_t end = offset + checked_size;
    auto protection = protections_.upper_bound(offset);
    if (protection != protections_.begin()) --protection;
    while (protection != protections_.end() && protection->first < end) {
        const std::size_t commit_start =
            std::max(offset, protection->first);
        const std::size_t commit_end =
            std::min(end, protection->second.end);
        if (commit_start < commit_end &&
            (protection->second.readable ||
             protection->second.writable) &&
            !CommitRangeLocked(commit_start, commit_end - commit_start,
                               error))
            return false;
        ++protection;
    }
    AdvanceMetadataGenerationLocked();
    return true;
}

bool GuestHeap::Read(std::uint64_t address, void* destination,
                     std::size_t size,
                     std::uint64_t* watched_generation) const {
    const std::shared_lock lock(mutex_);
    if (watched_generation != nullptr) *watched_generation = 0;
    if ((destination == nullptr && size != 0) ||
        !ContainsAddress(address))
        return false;
    if (size == 0) return true;
    const std::size_t offset = static_cast<std::size_t>(address - guest_base_);
    if (!HasAccessLocked(offset, size, false)) return false;
    if (watched_generation != nullptr) {
        const std::optional<std::size_t> block =
            FindAllocatedRange(address, size);
        if (block) {
            const Block& target = blocks_.at(*block);
            const std::size_t watch_start =
                *block + watch_range_offset_;
            const std::size_t watch_end = watch_start + watch_range_size_;
            if (target.watch != nullptr && offset < watch_end &&
                watch_start < offset + size)
                *watched_generation = target.watch->generation;
        }
    }
    std::memcpy(destination, memory_ + offset, size);
    return true;
}

GuestHeap::AccessSpanResult GuestHeap::ReadWithAccessSpanCache(
    std::uint64_t address, void* destination, std::size_t size,
    AccessSpanCache* cache) const {
    const std::shared_lock lock(mutex_);
    if (cache == nullptr || (destination == nullptr && size != 0) ||
        !ContainsAddress(address))
        return AccessSpanResult::kFailed;
    if (cache_identity_ != 0 && cache->owner_identity_ != 0 &&
        cache->owner_identity_ != cache_identity_)
        return AccessSpanResult::kFailed;
    if (cache_identity_ != 0) cache->owner_identity_ = cache_identity_;
    const std::size_t offset =
        static_cast<std::size_t>(address - guest_base_);
    if (size == 0) return AccessSpanResult::kValidated;
    if (size > capacity_ - offset) return AccessSpanResult::kFailed;
    if (access_span_cache_enabled_ &&
        cache->generation_ == metadata_generation_ &&
        !cache->writable_ && offset >= cache->start_ &&
        offset < cache->end_ &&
        size <= cache->end_ - offset) {
        std::memcpy(destination, memory_ + offset, size);
        return AccessSpanResult::kCacheHit;
    }

    std::size_t span_start = 0;
    std::size_t span_end = 0;
    std::size_t allocation_end = 0;
    if (ResolveAccessSpanLocked(offset, false, &span_start, &span_end,
                                &allocation_end) &&
        size <= span_end - offset) {
        if (access_span_cache_enabled_) {
            cache->generation_ = metadata_generation_;
            cache->start_ = span_start;
            cache->end_ = span_end;
            cache->writable_ = false;
        }
    } else if (!HasAccessLocked(offset, size, false)) {
        return AccessSpanResult::kFailed;
    }
    std::memcpy(destination, memory_ + offset, size);
    return AccessSpanResult::kValidated;
}

bool GuestHeap::Write(std::uint64_t address, const void* source,
                      std::size_t size) {
    const std::optional<std::string_view> active_import =
        ActiveAarch64ImportNameForCurrentThread();
    const std::optional<std::uint64_t> active_sequence =
        ActiveAarch64ImportSequenceForCurrentThread();
    return WriteWithOrigin(address, source, size, 0, 0, 0, 0,
                           active_import.value_or("host"), {},
                           active_sequence.value_or(0), 0, nullptr);
}

bool GuestHeap::ReadCommittedUntracked(std::uint64_t address,
                                       void* destination,
                                       std::size_t size) const {
    const std::unique_lock lock(mutex_);
    if (destination == nullptr && size != 0) return false;
    if (size == 0) return true;
    if (address < guest_base_ ||
        address - guest_base_ > capacity_ - size)
        return false;
    const std::size_t offset = static_cast<std::size_t>(address - guest_base_);
    const std::size_t first_page = offset / kHostPageSize;
    const std::size_t last_page = (offset + size - 1) / kHostPageSize;
    if (last_page >= committed_pages_.size()) return false;
    for (std::size_t page = first_page; page <= last_page; ++page) {
        if (committed_pages_[page] == 0) return false;
    }
    std::memcpy(destination, memory_ + offset, size);
    return true;
}

bool GuestHeap::ReadPhys(std::uint64_t address, void* destination,
                         std::size_t size) const {
    if (destination == nullptr && size != 0) return false;
    if (size == 0) return true;
    if (memory_ == nullptr || address < guest_base_ ||
        address - guest_base_ > capacity_ - size) {
        return false;
    }
    const std::size_t offset =
        static_cast<std::size_t>(address - guest_base_);
    // Only committed host pages are safe to touch; a reserved-but-
    // uncommitted arena page (still PAGE_NOACCESS) would fault on memcpy.
    const std::size_t end = offset + size;
    for (std::size_t page = offset & ~(kHostPageSize - 1); page < end;
         page += kHostPageSize) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(memory_ + page, &info, sizeof(info)) == 0 ||
            info.State != MEM_COMMIT)
            return false;
    }
    std::memcpy(destination, memory_ + offset, size);
    return true;
}

bool GuestHeap::WriteCommittedUntracked(std::uint64_t address,
                                        const void* source,
                                        std::size_t size) {
    const std::unique_lock lock(mutex_);
    if (source == nullptr && size != 0) {
        std::fprintf(stderr, "WCU fail=nullsrc\\n");
        return false;
    }
    if (size == 0) return true;
    if (address < guest_base_ ||
        address - guest_base_ > capacity_ - size) {
        std::fprintf(stderr,
                     "WCU fail=bounds addr=%llx base=%llx cap=%llx\n",
                     static_cast<unsigned long long>(address),
                     static_cast<unsigned long long>(guest_base_),
                     static_cast<unsigned long long>(capacity_));
        return false;
    }
    const std::size_t offset = static_cast<std::size_t>(address - guest_base_);
    // Never let the untracked fallback write into an active tracked
    // allocation: tracked writes must go through the audited path, and
    // an untracked write landing inside a live object silently corrupts
    // it (observed as the clobbered ResourceManager pointer at
    // LawnApp+0x940).
    {
        auto next = blocks_.upper_bound(offset);
        if (next != blocks_.begin()) {
            auto current = std::prev(next);
            if (current->second.allocated &&
                !current->second.mapping &&
                current->first + current->second.size > offset)
                return false;
        }
        for (auto scan = next;
             scan != blocks_.end() &&
             scan->first < offset + size;
             ++scan) {
            if (scan->second.allocated && !scan->second.mapping)
                return false;
        }
    }
    // Untracked heap pages (committed bionic mmap allocations) are
    // guest-writable by default. Only an explicit non-writable
    // protection range overlapping the span denies the host write; the
    // absence of coverage must not, because the protection map only
    // records ranges with explicit mprotect/loader contracts and large
    // untracked allocations legitimately sit outside it.
    {
        const std::size_t end = offset + size;
        std::size_t cursor = offset;
        auto range = protections_.upper_bound(cursor);
        if (range != protections_.begin()) --range;
        bool denied = false;
        while (cursor < end && range != protections_.end() &&
               range->first < end) {
            if (range->second.end <= cursor) {
                ++range;
                continue;
            }
            if (!range->second.writable) {
                denied = true;
                break;
            }
            cursor = range->second.end;
            ++range;
        }
        if (denied) {
            std::fprintf(stderr,
                         "WCU fail=protection offset=%zx size=%zx range=[%zx,%zx) rw=%d/%d\n",
                         offset, size,
                         static_cast<std::size_t>(range->first),
                         static_cast<std::size_t>(range->second.end),
                         range->second.readable ? 1 : 0,
                         range->second.writable ? 1 : 0);
            return false;
        }
    }
    std::string ignored;
    if (!CommitRangeLocked(offset, size, &ignored)) {
        std::fprintf(stderr, "WCU fail=commit offset=%zx err=%s\n", offset,
                     ignored.c_str());
        return false;
    }
    std::memcpy(memory_ + offset, source, size);
    std::fprintf(stderr, "WCU ok offset=%zx\n", offset);
    return true;
}

bool GuestHeap::WriteUntrackedChecked(std::uint64_t address,
                                      const void* source,
                                      std::size_t size) {
    const std::unique_lock lock(mutex_);
    const std::optional<std::size_t> block =
        FindAllocatedRange(address, size);
    if ((source == nullptr && size != 0) || !block) return false;
    if (size == 0) return true;
    const std::size_t offset = static_cast<std::size_t>(address - guest_base_);
    if (!HasAccessLocked(offset, size, true)) return false;
    std::memcpy(memory_ + offset, source, size);
    return true;
}

GuestHeap::AccessSpanResult GuestHeap::WriteWithAccessSpanCache(
    std::uint64_t address, const void* source, std::size_t size,
    AccessSpanCache* cache) {
    const std::unique_lock lock(mutex_);
    if (cache == nullptr || (source == nullptr && size != 0) ||
        !ContainsAddress(address))
        return AccessSpanResult::kFailed;
    if (cache_identity_ != 0 && cache->owner_identity_ != 0 &&
        cache->owner_identity_ != cache_identity_)
        return AccessSpanResult::kFailed;
    if (cache_identity_ != 0) cache->owner_identity_ = cache_identity_;
    const std::size_t offset =
        static_cast<std::size_t>(address - guest_base_);
    if (size == 0) return AccessSpanResult::kValidated;
    if (size > capacity_ - offset) return AccessSpanResult::kFailed;
    if (access_span_cache_enabled_ &&
        cache->generation_ == metadata_generation_ &&
        cache->writable_ && offset >= cache->start_ &&
        offset < cache->end_ &&
        size <= cache->end_ - offset) {
        std::memcpy(memory_ + offset, source, size);
        return AccessSpanResult::kCacheHit;
    }

    std::size_t span_start = 0;
    std::size_t span_end = 0;
    std::size_t allocation_end = 0;
    if (!ResolveAccessSpanLocked(offset, true, &span_start, &span_end,
                                 &allocation_end))
        return AccessSpanResult::kFailed;
    if (size <= span_end - offset) {
        if (access_span_cache_enabled_) {
            cache->generation_ = metadata_generation_;
            cache->start_ = span_start;
            cache->end_ = span_end;
            cache->writable_ = true;
        }
    } else {
        if (size > allocation_end - offset ||
            !HasProtectionAccessLocked(offset, size, true))
            return AccessSpanResult::kFailed;
    }
    std::memcpy(memory_ + offset, source, size);
    return AccessSpanResult::kValidated;
}

bool GuestHeap::WriteFromGuest(std::uint64_t address, const void* source,
                               std::size_t size,
                               std::uint64_t guest_program_counter,
                               std::uint64_t guest_link_register,
                               std::uint64_t guest_thread_pointer,
                               std::uint64_t guest_stack_pointer,
                               std::string_view last_import,
                               std::string_view last_jni_import,
                               std::string_view last_jni_note,
                               std::uint64_t current_dispatch_sequence,
                               std::uint64_t last_jni_dispatch_sequence,
                               std::uint64_t* watched_generation) {
    std::array<char, 96> context{};
    constexpr std::string_view prefix = "guest-after-";
    std::memcpy(context.data(), prefix.data(), prefix.size());
    const std::size_t import_size = std::min(
        last_import.size(), context.size() - prefix.size() - 1);
    std::memcpy(context.data() + prefix.size(), last_import.data(),
                import_size);
    const std::size_t jni_prefix_offset = prefix.size() + import_size;
    constexpr std::string_view jni_prefix = "-jni-";
    const std::size_t jni_prefix_size = std::min(
        jni_prefix.size(), context.size() - jni_prefix_offset - 1);
    std::memcpy(context.data() + jni_prefix_offset, jni_prefix.data(),
                jni_prefix_size);
    const std::size_t jni_offset = jni_prefix_offset + jni_prefix_size;
    const std::size_t jni_size = std::min(
        last_jni_import.size(), context.size() - jni_offset - 1);
    std::memcpy(context.data() + jni_offset, last_jni_import.data(),
                jni_size);
    return WriteWithOrigin(address, source, size, guest_program_counter,
                           guest_link_register, guest_thread_pointer,
                           guest_stack_pointer,
                           context.data(), last_jni_note,
                           current_dispatch_sequence,
                           last_jni_dispatch_sequence,
                           watched_generation);
}

void GuestHeap::AnnotateAllocationFromGuest(
    std::uint64_t address, std::uint64_t guest_link_register,
    std::uint64_t guest_frame_pointer,
    std::uint64_t guest_stack_pointer,
    const Aarch64ImportRegistry* imports) {
    if (imports == nullptr) return;
    {
        const std::shared_lock lock(mutex_);
        const std::optional<std::size_t> block = FindAllocatedBlock(address);
        if (!block || *block != address - guest_base_ ||
            blocks_.at(*block).watch == nullptr)
            return;
    }
    const std::string recent_imports =
        imports->RecentDispatchedNamesForCurrentThread(16);
    AnnotateAllocationFromGuest(
        address, guest_link_register, guest_frame_pointer,
        guest_stack_pointer,
        imports->LastJniDispatchedNameForCurrentThread().value_or("none"),
        imports->LastJniSemanticNoteForCurrentThread().value_or("none"),
        imports->CurrentDispatchSequenceForCurrentThread().value_or(0),
        imports->LastJniDispatchSequenceForCurrentThread().value_or(0),
        recent_imports);
}

void GuestHeap::AnnotateAllocationFromGuest(
    std::uint64_t address, std::uint64_t guest_link_register,
    std::uint64_t guest_frame_pointer,
    std::uint64_t guest_stack_pointer,
    std::string_view last_jni_import,
    std::string_view last_jni_note,
    std::uint64_t current_dispatch_sequence,
    std::uint64_t last_jni_dispatch_sequence,
    std::string_view recent_imports) {
    const std::unique_lock lock(mutex_);
    const std::optional<std::size_t> block = FindAllocatedBlock(address);
    if (!block || *block != address - guest_base_ ||
        blocks_.at(*block).watch == nullptr)
        return;
    WatchState& watch = *blocks_.at(*block).watch;
    watch.allocation_sequence = next_watch_sequence_++;
    watch.allocation_link_register = guest_link_register;
    watch.allocation_frame_pointer = guest_frame_pointer;
    watch.allocation_stack_pointer = guest_stack_pointer;
    watch.allocation_current_dispatch_sequence = current_dispatch_sequence;
    watch.allocation_last_jni_dispatch_sequence =
        last_jni_dispatch_sequence;
    const std::size_t import_size = std::min(
        last_jni_import.size(), watch.allocation_last_jni_import.size() - 1);
    std::memcpy(watch.allocation_last_jni_import.data(),
                last_jni_import.data(), import_size);
    const std::size_t note_size = std::min(
        last_jni_note.size(), watch.allocation_last_jni_note.size() - 1);
    std::memcpy(watch.allocation_last_jni_note.data(), last_jni_note.data(),
                note_size);
    const std::size_t recent_size = std::min(
        recent_imports.size(), watch.allocation_recent_imports.size() - 1);
    std::memcpy(watch.allocation_recent_imports.data(),
                recent_imports.data(), recent_size);
    std::uint64_t frame_pointer = guest_frame_pointer;
    while (watch.allocation_frame_count <
               watch.allocation_frame_link_registers.size() &&
           frame_pointer != 0) {
        const std::optional<std::size_t> frame_block =
            FindAllocatedRange(frame_pointer, 2 * sizeof(std::uint64_t));
        if (!frame_block) break;
        std::array<std::uint64_t, 2> frame{};
        const std::size_t frame_offset =
            static_cast<std::size_t>(frame_pointer - guest_base_);
        std::memcpy(frame.data(), memory_ + frame_offset,
                    sizeof(frame));
        watch.allocation_frame_link_registers[
            watch.allocation_frame_count++] = frame[1];
        if (frame[0] <= frame_pointer || (frame[0] & 15U) != 0) break;
        frame_pointer = frame[0];
    }
}

bool GuestHeap::WriteWithOrigin(std::uint64_t address, const void* source,
                                std::size_t size,
                                std::uint64_t guest_program_counter,
                                std::uint64_t guest_link_register,
                                std::uint64_t guest_thread_pointer,
                                std::uint64_t guest_stack_pointer,
                                std::string_view origin,
                                std::string_view jni_note,
                                std::uint64_t current_dispatch_sequence,
                                std::uint64_t last_jni_dispatch_sequence,
                                std::uint64_t* watched_generation) {
    const std::unique_lock lock(mutex_);
    if (watched_generation != nullptr) *watched_generation = 0;
    const std::optional<std::size_t> block =
        FindAllocatedRange(address, size);
    if ((source == nullptr && size != 0) || !block) {
        const auto exact = FindAllocatedBlock(address);
        const auto tiny = FindAllocatedRange(address, 4096);
        std::fprintf(stderr,
                     "WWO fail=noblock addr=%llx size=%zx exact=%d tiny=%d tinysize=%zx\n",
                     static_cast<unsigned long long>(address), size,
                     exact.has_value() ? 1 : 0,
                     tiny.has_value() ? 1 : 0,
                     tiny ? blocks_.at(*tiny).size : 0);
        return false;
    }
    if (size == 0) return true;
    const std::size_t offset = static_cast<std::size_t>(address - guest_base_);
    if (!HasAccessLocked(offset, size, true)) {
        auto range = protections_.upper_bound(offset);
        if (range != protections_.begin()) --range;
        std::fprintf(stderr,
                     "WWO fail=access offset=%zx size=%zx range=[%zx,%zx) rw=%d/%d\n",
                     offset, size,
                     range == protections_.end() ? 0 : range->first,
                     range == protections_.end() ? 0 : range->second.end,
                     range == protections_.end() ? 0
                                                 : range->second.readable,
                     range == protections_.end() ? 0
                                                 : range->second.writable);
        return false;
    }
    Block& target = blocks_.at(*block);
    const std::size_t watch_start =
        *block + watch_range_offset_;
    const std::size_t watch_end = watch_start + watch_range_size_;
    const bool watched = target.watch != nullptr &&
                         offset < watch_end && watch_start < offset + size;
    if (watched && watched_generation != nullptr)
        *watched_generation = target.watch->generation;
    WatchEvent event{};
    if (watched) {
        event.sequence = next_watch_sequence_++;
        event.guest_program_counter = guest_program_counter;
        event.guest_link_register = guest_link_register;
        event.guest_thread_pointer = guest_thread_pointer;
        event.guest_stack_pointer = guest_stack_pointer;
        event.current_dispatch_sequence = current_dispatch_sequence;
        event.last_jni_dispatch_sequence = last_jni_dispatch_sequence;
        event.thread_id = static_cast<std::uint64_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        event.write_offset = offset - *block;
        event.write_size = size;
        std::memcpy(&event.before,
                    memory_ + *block + watch_range_offset_,
                    watch_range_size_);
        const std::size_t origin_size =
            std::min(origin.size(), event.origin.size() - 1);
        std::memcpy(event.origin.data(), origin.data(), origin_size);
        const std::size_t jni_note_size =
            std::min(jni_note.size(), event.jni_note.size() - 1);
        std::memcpy(event.jni_note.data(), jni_note.data(),
                    jni_note_size);
    }
    std::memcpy(memory_ + offset, source, size);
    if (watched) {
        std::memcpy(&event.after,
                    memory_ + *block + watch_range_offset_,
                    watch_range_size_);
        target.watch->events[target.watch->event_next] = event;
        target.watch->event_next =
            (target.watch->event_next + 1) % target.watch->events.size();
        if (target.watch->event_count < target.watch->events.size()) {
            ++target.watch->event_count;
        } else {
            target.watch->overflow = true;
        }
    }
    return true;
}

bool GuestHeap::SetWritable(std::uint64_t address, std::uint64_t size,
                            bool writable, std::string* error) {
    const std::unique_lock lock(mutex_);
    const std::optional<std::size_t> block = FindAllocatedBlock(address);
    if (!block || (size != blocks_.at(*block).size &&
                   size + EffectiveOverflowRedzone() !=
                       blocks_.at(*block).size)) {
        SetError(error,
                 "guest protection range is not an exact active allocation");
        return false;
    }
    blocks_.at(*block).writable = writable;
    const bool protected_range = SetProtectionLocked(
        *block, blocks_.at(*block).size, true, writable, error);
    AdvanceMetadataGenerationLocked();
    return protected_range;
}

bool GuestHeap::SetProtection(std::uint64_t address, std::uint64_t size,
                              bool readable, bool writable,
                              std::string* error) {
    const std::unique_lock lock(mutex_);
    if (address < guest_base_ || size > std::numeric_limits<std::size_t>::max()) {
        SetError(error, "guest protection range is outside the virtual arena");
        return false;
    }
    const std::size_t offset =
        static_cast<std::size_t>(address - guest_base_);
    const bool protected_range = SetProtectionLocked(
        offset, static_cast<std::size_t>(size), readable, writable, error);
    if (protected_range) AdvanceMetadataGenerationLocked();
    return protected_range;
}

bool GuestHeap::IsWritableRange(std::uint64_t address,
                                std::size_t size) const {
    const std::shared_lock lock(mutex_);
    if (!ContainsAddress(address)) return false;
    const std::size_t offset =
        static_cast<std::size_t>(address - guest_base_);
    return HasAccessLocked(offset, size, true);
}

bool GuestHeap::IsReadableRange(std::uint64_t address,
                                std::size_t size) const {
    const std::shared_lock lock(mutex_);
    if (!ContainsAddress(address)) return false;
    const std::size_t offset =
        static_cast<std::size_t>(address - guest_base_);
    return HasAccessLocked(offset, size, false);
}

void GuestHeap::SetExecutable(std::uint64_t address, std::uint64_t size,
                              bool executable) {
    if (!ContainsAddress(address) || size == 0) return;
    const std::size_t first =
        static_cast<std::size_t>(address - guest_base_) &
        ~(kHostPageSize - 1);
    const std::size_t last =
        static_cast<std::size_t>(address - guest_base_ + size - 1) &
        ~(kHostPageSize - 1);
    const std::lock_guard lock(mutex_);
    for (std::size_t page = first; page <= last; page += kHostPageSize) {
        if (executable)
            exec_pages_.insert(page);
        else
            exec_pages_.erase(page);
    }
}

bool GuestHeap::IsExecutable(std::uint64_t address) const {
    if (!ContainsAddress(address)) return false;
    const std::size_t offset =
        static_cast<std::size_t>(address - guest_base_) &
        ~(kHostPageSize - 1);
    const std::shared_lock lock(mutex_);
    return exec_pages_.find(offset) != exec_pages_.end();
}

bool GuestHeap::ContainsAddress(std::uint64_t address) const {
    const bool result = valid_configuration_ && address >= guest_base_ &&
                        address < guest_base_ + capacity_;
    if (!result && address >> 40ULL == 0x71ULL) {
        // A guest-heap-range address refused by this heap can only mean a
        // foreign heap instance or a corrupted base: log it (bounded).
        static std::size_t contains_refusal_logs = 0;
        if (contains_refusal_logs < 6) {
            ++contains_refusal_logs;
            std::fprintf(stderr,
                         "ISOMR_HEAP_REFUSED_GUEST_RANGE heap=%p valid=%d "
                         "base=%llx capacity=%llx address=%llx\n",
                         static_cast<const void*>(this),
                         valid_configuration_ ? 1 : 0,
                         static_cast<unsigned long long>(guest_base_),
                         static_cast<unsigned long long>(capacity_),
                         static_cast<unsigned long long>(address));
            std::fflush(stderr);
        }
    }
    return result;
}

std::string GuestHeap::DescribeRange(std::uint64_t address,
                                     std::size_t size) const {
    const std::shared_lock lock(mutex_);
    const std::optional<std::size_t> block_index =
        FindAllocatedRange(address, size);
    if (!block_index) return "not-active-allocation";
    const Block& block = blocks_.at(*block_index);
    const std::size_t heap_offset =
        static_cast<std::size_t>(address - guest_base_);
    std::ostringstream description;
    description << "active-allocation-heap-offset" << heap_offset
                << "-allocation-offset" << *block_index
                << "-allocation-size" << block.size << "-range-offset"
                << (heap_offset - *block_index) << "-range-size" << size;
    if (block.watch != nullptr) {
        description << "-watch-generation" << block.watch->generation
                    << "-watch-initial-" << block.watch->initial_kind.data()
                    << "-watch-initial-value0x" << std::hex
                    << block.watch->initial_value << std::dec
                    << "-watch-allocation-seq"
                    << block.watch->allocation_sequence
                    << "-watch-allocation-lr0x" << std::hex
                    << block.watch->allocation_link_register
                    << "-watch-allocation-fp0x"
                    << block.watch->allocation_frame_pointer
                    << "-watch-allocation-sp0x"
                    << block.watch->allocation_stack_pointer << std::dec
                    << "-watch-allocation-dispatch-seq"
                    << block.watch->allocation_current_dispatch_sequence
                    << "-watch-allocation-last-jni-dispatch-seq"
                    << block.watch->allocation_last_jni_dispatch_sequence
                    << "-watch-allocation-last-jni-"
                    << block.watch->allocation_last_jni_import.data()
                    << "-watch-allocation-jni-note-"
                    << block.watch->allocation_last_jni_note.data()
                    << "-watch-allocation-recent-imports-"
                    << block.watch->allocation_recent_imports.data()
                    << "-watch-allocation-frame-lrs";
        for (std::size_t frame_index = 0;
             frame_index < block.watch->allocation_frame_count;
             ++frame_index) {
            description << (frame_index == 0 ? "-0x" : ",0x")
                        << std::hex
                        << block.watch
                               ->allocation_frame_link_registers[frame_index]
                        << std::dec;
        }
        description
                    << "-watch-events" << block.watch->event_count
                    << "-watch-overflow" << block.watch->overflow;
        const std::size_t first =
            block.watch->event_count == block.watch->events.size()
                ? block.watch->event_next
                : 0;
        for (std::size_t event_index = 0;
             event_index < block.watch->event_count; ++event_index) {
            const WatchEvent& event = block.watch->events[
                (first + event_index) % block.watch->events.size()];
            description << "-event" << event_index << "-seq"
                        << event.sequence << "-origin" << event.origin.data()
                        << "-jni-note" << event.jni_note.data()
                        << "-write-offset" << event.write_offset
                        << "-write-size" << event.write_size << "-before0x"
                        << std::hex << event.before << "-after0x" << event.after
                        << "-guest-pc0x" << event.guest_program_counter
                        << "-guest-lr0x" << event.guest_link_register
                        << "-guest-tp0x" << event.guest_thread_pointer
                        << "-guest-sp0x" << event.guest_stack_pointer
                        << std::dec
                        << "-dispatch-seq"
                        << event.current_dispatch_sequence
                        << "-last-jni-dispatch-seq"
                        << event.last_jni_dispatch_sequence
                        << "-thread0x" << std::hex << event.thread_id
                        << std::dec;
        }
    }
    return description.str();
}

void GuestHeap::DumpLiveAllocations(const char* tag) const {
    const std::shared_lock lock(mutex_);
    std::size_t live = 0;
    for (const auto& [offset, block] : blocks_)
        if (block.allocated) ++live;
    std::fprintf(stderr, "ISOMR_HEAP_LIVE tag=%s count=%zu\n",
                 tag != nullptr ? tag : "?", live);
    for (const auto& [offset, block] : blocks_) {
        if (!block.allocated) continue;
        const char* kind = block.watch != nullptr
                               ? block.watch->initial_kind.data() : "-";
        std::fprintf(
            stderr,
            "  guest=%llx off=%zx size=%zx mapping=%d pinned=%d kind=%s\n",
            static_cast<unsigned long long>(guest_base_ + offset), offset,
            block.size, block.mapping ? 1 : 0, block.pinned ? 1 : 0, kind);
    }
    std::fflush(stderr);
}

std::size_t GuestHeap::allocation_count() const {
    const std::shared_lock lock(mutex_);
    return static_cast<std::size_t>(std::count_if(
        blocks_.begin(), blocks_.end(),
        [](const auto& entry) { return entry.second.allocated; }));
}

std::size_t GuestHeap::allocated_bytes() const {
    const std::shared_lock lock(mutex_);
    std::size_t total = 0;
    for (const auto& [offset, block] : blocks_) {
        (void)offset;
        if (block.allocated) total += block.size;
    }
    return total;
}

void GuestHeap::RecordAllocationFailureLocked(
    AllocationFailureReason reason, std::uint64_t requested_size,
    std::optional<std::size_t> normalized_size,
    std::uint64_t alignment) {
    AllocationFailureSnapshot snapshot{};
    snapshot.reason = reason;
    snapshot.requested_size = requested_size;
    snapshot.normalized_size = normalized_size.value_or(0);
    snapshot.alignment = alignment;
    snapshot.capacity = capacity_;
    snapshot.committed_bytes =
        static_cast<std::size_t>(std::count(
            committed_pages_.begin(), committed_pages_.end(),
            std::uint8_t{1})) *
        kHostPageSize;
    for (const auto& [offset, block] : blocks_) {
        (void)offset;
        if (!block.allocated) continue;
        snapshot.allocated_bytes += block.size;
        ++snapshot.allocation_count;
    }
    snapshot.free_extent_count = free_extents_.size();
    snapshot.largest_free_extent = free_extents_.largest_extent();
    last_allocation_failure_ = snapshot;
}

std::optional<GuestHeap::AllocationFailureSnapshot>
GuestHeap::LastAllocationFailureSnapshot() const {
    const std::shared_lock lock(mutex_);
    return last_allocation_failure_;
}

std::string GuestHeap::DescribeLastAllocationFailure() const {
    const std::optional<AllocationFailureSnapshot> snapshot =
        LastAllocationFailureSnapshot();
    if (!snapshot) return "heap-allocation-failure=unavailable";
    const char* reason = "invalid-request";
    switch (snapshot->reason) {
    case AllocationFailureReason::kInvalidRequest:
        reason = "invalid-request";
        break;
    case AllocationFailureReason::kNoFittingExtent:
        reason = "no-fitting-extent";
        break;
    case AllocationFailureReason::kMetadataIndexInconsistency:
        reason = "metadata-index-inconsistency";
        break;
    case AllocationFailureReason::kMetadataAllocationFailure:
        reason = "metadata-allocation-failure";
        break;
    case AllocationFailureReason::kHostCommitFailure:
        reason = "host-commit-failure";
        break;
    case AllocationFailureReason::kWatchAllocationFailure:
        reason = "watch-allocation-failure";
        break;
    case AllocationFailureReason::kExtractionFailure:
        reason = "extraction-failure";
        break;
    }
    std::ostringstream description;
    description << "heap-allocation-failure reason=" << reason
                << " requested=" << snapshot->requested_size
                << " normalized=" << snapshot->normalized_size
                << " alignment=" << snapshot->alignment
                << " allocated=" << snapshot->allocated_bytes
                << " capacity=" << snapshot->capacity
                << " committed=" << snapshot->committed_bytes
                << " allocations=" << snapshot->allocation_count
                << " free-extents=" << snapshot->free_extent_count
                << " largest-free-extent="
                << snapshot->largest_free_extent;
    return description.str();
}

bool GuestHeap::CheckMetadataInvariants(std::string* error) {
    const std::unique_lock lock(mutex_);
    // The free-quarantine holds recently released extents out of the free
    // index on purpose; drain it so the invariant verifies the settled free
    // partition rather than failing on intentionally unindexed extents.
    FlushQuarantineLocked();
    return CheckMetadataInvariantsLocked(error);
}

bool GuestHeap::CheckMetadataInvariantsLocked(std::string* error) const {
    if (!valid_configuration_) {
        SetError(error, "guest heap configuration is invalid");
        return false;
    }
    std::size_t cursor = 0;
    std::size_t free_count = 0;
    bool previous_free = false;
    if (!free_extents_.Check(error)) return false;
    for (const auto& [offset, block] : blocks_) {
        if (offset != cursor || block.size == 0 ||
            block.size > capacity_ - cursor) {
            SetError(error, "guest heap offset map is not a full partition");
            return false;
        }
        if (!block.allocated) {
            const auto* indexed = free_extents_.Find(offset);
            if (previous_free || indexed == nullptr ||
                indexed->size != block.size) {
                SetError(error, "guest heap free extent is unmerged or unindexed");
                return false;
            }
            ++free_count;
        } else if (free_extents_.Find(offset) != nullptr) {
            SetError(error, "guest heap allocated block is free-indexed");
            return false;
        }
        previous_free = !block.allocated;
        cursor += block.size;
    }
    if (cursor != capacity_ || free_count != free_extents_.size()) {
        SetError(error, "guest heap free index cardinality is inconsistent");
        return false;
    }
    return true;
}

std::size_t GuestHeap::LastNormalQueryNodeVisitsForTesting() const {
    const std::shared_lock lock(mutex_);
    return last_normal_query_node_visits_;
}

std::uint64_t GuestHeap::MetadataGenerationForTesting() const {
    const std::shared_lock lock(mutex_);
    return metadata_generation_;
}

void GuestHeap::FailMetadataAllocationAfterForTesting(
    std::size_t successful_allocations) {
    const std::unique_lock lock(mutex_);
    metadata_failure_countdown_ = successful_allocations;
}

void GuestHeap::FlushQuarantineForTesting() {
    const std::unique_lock lock(mutex_);
    FlushQuarantineLocked();
}

void GuestHeap::MaybeFailMetadataAllocationLocked() {
    if (!metadata_failure_countdown_) return;
    if (*metadata_failure_countdown_ == 0) {
        metadata_failure_countdown_.reset();
        throw std::bad_alloc();
    }
    --*metadata_failure_countdown_;
}

std::size_t GuestHeap::committed_bytes() const {
    const std::shared_lock lock(mutex_);
    return static_cast<std::size_t>(std::count(
               committed_pages_.begin(), committed_pages_.end(),
               std::uint8_t{1})) *
           kHostPageSize;
}

void GuestHeap::AdvanceMetadataGenerationLocked() {
    std::uint64_t observed =
        global_metadata_generation_advances_.load(std::memory_order_relaxed);
    while (observed != (std::numeric_limits<std::uint64_t>::max)() &&
           !global_metadata_generation_advances_.compare_exchange_weak(
               observed, observed + 1, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
    if (metadata_generation_ ==
        std::numeric_limits<std::uint64_t>::max()) {
        metadata_generation_ = 1;
        access_span_cache_enabled_ = false;
        return;
    }
    ++metadata_generation_;
}

std::uint64_t GuestHeap::GlobalMetadataGenerationAdvanceCount() {
    return global_metadata_generation_advances_.load(std::memory_order_relaxed);
}

std::uint64_t GuestHeap::MetadataGenerationAdvanceDelta(
    std::uint64_t before, std::uint64_t after) {
    // The global counter saturates, so reversal can only be an invalid or
    // torn observation. Fail closed rather than reporting a wrapped delta.
    return after >= before ? after - before : 0;
}

}  // namespace isomr
