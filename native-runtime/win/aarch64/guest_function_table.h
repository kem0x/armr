#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace isomr {

class Aarch64ImportRegistry;
class GuestHeap;

// Owns the guest allocations for a two-level AArch64 C ABI function table:
// the first word of the object points to a contiguous table of guest function
// pointers. This models the ABI shape only; it does not define JNI semantics or
// slot indices.
class GuestFunctionTable {
public:
    GuestFunctionTable(GuestHeap* heap, std::size_t slot_count,
                       std::string* error);
    ~GuestFunctionTable() noexcept;

    GuestFunctionTable(const GuestFunctionTable&) = delete;
    GuestFunctionTable& operator=(const GuestFunctionTable&) = delete;
    GuestFunctionTable(GuestFunctionTable&&) = delete;
    GuestFunctionTable& operator=(GuestFunctionTable&&) = delete;

    bool AssignSlot(std::size_t slot, std::string_view handler_name,
                    const Aarch64ImportRegistry& registry,
                    std::string* error);
    bool AssignNullSlot(std::size_t slot, std::string* error);
    bool Seal(std::string* error);

    bool valid() const { return valid_; }
    bool sealed() const { return sealed_; }
    std::size_t slot_count() const { return slots_.size(); }
    std::uint64_t object_address() const {
        return sealed_ ? object_allocation_ : 0;
    }
    // Second word of the pinned JNIEnv object block: the byte an inline
    // ExceptionCheck stub cell reads (LDRB W0,[X0,#8]). Kept zero except by
    // SetExceptionFlagByte; the guest never touches it.
    static constexpr std::uint64_t kGuestObjectFlagOffset = 8;

    std::uint64_t exception_flag_address() const {
        return object_allocation_ + kGuestObjectFlagOffset;
    }

    void SetExceptionFlagByte(std::uint8_t value);

    std::uint64_t table_address() const {
        return sealed_ ? table_allocation_ : 0;
    }

private:
    void ReleaseAllocations() noexcept;

    GuestHeap* heap_ = nullptr;
    std::vector<std::optional<std::uint64_t>> slots_;
    std::uint64_t table_allocation_ = 0;
    std::uint64_t object_allocation_ = 0;
    std::size_t assigned_slots_ = 0;
    bool valid_ = false;
    bool sealed_ = false;
};

}  // namespace isomr
