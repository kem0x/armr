#include "guest_function_table.h"

#include "guest_heap.h"
#include "import_registry.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace isomr {
namespace {

constexpr std::size_t kGuestWordSize = sizeof(std::uint64_t);
constexpr std::size_t kGuestHeapAlignment = 16;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

}  // namespace

GuestFunctionTable::GuestFunctionTable(GuestHeap* heap,
                                       std::size_t slot_count,
                                       std::string* error)
    : heap_(heap) {
    if (heap_ == nullptr || slot_count == 0) {
        SetError(error, "guest function table configuration is empty");
        return;
    }
    if (slot_count >
        std::numeric_limits<std::size_t>::max() / kGuestWordSize) {
        SetError(error, "guest function table slot byte count overflows");
        return;
    }

    const std::size_t table_bytes = slot_count * kGuestWordSize;
    if (table_bytes >
        std::numeric_limits<std::size_t>::max() -
            (kGuestHeapAlignment - 1)) {
        SetError(error, "guest function table aligned size overflows");
        return;
    }
    const std::size_t aligned_table_bytes =
        (table_bytes + (kGuestHeapAlignment - 1)) &
        ~(kGuestHeapAlignment - 1);
    if (aligned_table_bytes > heap_->capacity() ||
        kGuestHeapAlignment > heap_->capacity() - aligned_table_bytes) {
        SetError(error, "guest function table exceeds guest heap capacity");
        return;
    }

    try {
        slots_.resize(slot_count);
    } catch (const std::bad_alloc&) {
        SetError(error, "guest function table host slot allocation failed");
        return;
    } catch (const std::length_error&) {
        SetError(error, "guest function table host slot allocation failed");
        return;
    }

    table_allocation_ = heap_->AllocatePinned(table_bytes);
    if (table_allocation_ == 0) {
        SetError(error, "guest function table allocation failed");
        return;
    }
    // Two words: the functions pointer (written at Seal) and the inline
    // ExceptionCheck stub's flag byte (see exception_flag_address()).
    object_allocation_ = heap_->AllocatePinned(kGuestWordSize * 2);
    if (object_allocation_ == 0) {
        SetError(error, "guest function table object allocation failed");
        ReleaseAllocations();
        return;
    }
    valid_ = true;
}

GuestFunctionTable::~GuestFunctionTable() noexcept {
    ReleaseAllocations();
}

void GuestFunctionTable::ReleaseAllocations() noexcept {
    if (heap_ == nullptr) return;
    if (object_allocation_ != 0) {
        (void)heap_->ReleasePinned(object_allocation_, nullptr);
        object_allocation_ = 0;
    }
    if (table_allocation_ != 0) {
        (void)heap_->ReleasePinned(table_allocation_, nullptr);
        table_allocation_ = 0;
    }
}

bool GuestFunctionTable::AssignSlot(
    std::size_t slot, std::string_view handler_name,
    const Aarch64ImportRegistry& registry, std::string* error) {
    if (!valid_) {
        SetError(error, "guest function table configuration is invalid");
        return false;
    }
    if (sealed_) {
        SetError(error, "guest function table is sealed");
        return false;
    }
    if (!registry.sealed()) {
        SetError(error, "AArch64 import registry is not sealed");
        return false;
    }
    if (slot >= slots_.size()) {
        SetError(error, "guest function table slot is out of range");
        return false;
    }
    if (slots_[slot]) {
        SetError(error, "guest function table slot is already assigned");
        return false;
    }
    const std::optional<std::uint64_t> address =
        registry.ResolveCallable(handler_name);
    if (!address) {
        SetError(error, "guest function table handler is unresolved");
        return false;
    }
    slots_[slot] = *address;
    ++assigned_slots_;
    return true;
}

bool GuestFunctionTable::AssignNullSlot(std::size_t slot,
                                        std::string* error) {
    if (!valid_) {
        SetError(error, "guest function table configuration is invalid");
        return false;
    }
    if (sealed_) {
        SetError(error, "guest function table is sealed");
        return false;
    }
    if (slot >= slots_.size()) {
        SetError(error, "guest function table slot is out of range");
        return false;
    }
    if (slots_[slot]) {
        SetError(error, "guest function table slot is already assigned");
        return false;
    }
    slots_[slot] = std::uint64_t{0};
    ++assigned_slots_;
    return true;
}

void GuestFunctionTable::SetExceptionFlagByte(std::uint8_t value) {
    if (object_allocation_ == 0 || heap_ == nullptr) return;
    (void)heap_->Write(object_allocation_ + kGuestObjectFlagOffset,
                       &value, sizeof(value));
}

bool GuestFunctionTable::Seal(std::string* error) {
    if (!valid_) {
        SetError(error, "guest function table configuration is invalid");
        return false;
    }
    if (sealed_) {
        SetError(error, "guest function table is already sealed");
        return false;
    }
    if (assigned_slots_ == 0) {
        SetError(error, "cannot seal an empty guest function table");
        return false;
    }
    if (assigned_slots_ != slots_.size()) {
        SetError(error, "cannot seal an incomplete guest function table");
        return false;
    }

    std::vector<std::uint64_t> addresses;
    try {
        addresses.reserve(slots_.size());
        for (const std::optional<std::uint64_t>& slot : slots_)
            addresses.push_back(*slot);
    } catch (const std::bad_alloc&) {
        SetError(error, "guest function table seal allocation failed");
        return false;
    } catch (const std::length_error&) {
        SetError(error, "guest function table seal allocation failed");
        return false;
    }

    const std::size_t table_bytes = addresses.size() * kGuestWordSize;
    const std::array<std::uint64_t, 2> object{table_allocation_, 0};
    if (!heap_->Write(table_allocation_, addresses.data(), table_bytes) ||
        !heap_->Write(object_allocation_, object.data(),
                      sizeof(object))) {
        SetError(error, "guest function table checked write failed");
        valid_ = false;
        ReleaseAllocations();
        return false;
    }
    sealed_ = true;
    return true;
}

}  // namespace isomr
