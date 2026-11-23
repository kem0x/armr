#include "candy_rapidjson_host.h"

#include "guest_heap.h"

#include <rapidjson/document.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace isomr {
namespace {

constexpr std::size_t kGuestValueSize = 24;
constexpr std::size_t kGuestMemberSize = 48;
constexpr std::size_t kGuestDocumentSize = 104;
constexpr std::size_t kGuestAllocatorSize = 40;
constexpr std::size_t kChunkHeaderSize = 24;
constexpr std::size_t kDefaultChunkCapacity = 64U * 1024U;
constexpr std::size_t kMaximumDepth = 1024;
constexpr std::size_t kShortStringMaximum = 21;

constexpr std::uint16_t kNullFlag = 0;
constexpr std::uint16_t kFalseFlag = 1U | 0x8U;
constexpr std::uint16_t kTrueFlag = 2U | 0x8U;
constexpr std::uint16_t kObjectFlag = 3;
constexpr std::uint16_t kArrayFlag = 4;
constexpr std::uint16_t kCopyStringFlag = 5U | 0x400U | 0x800U;
constexpr std::uint16_t kShortStringFlag =
    kCopyStringFlag | 0x1000U;
constexpr std::uint16_t kNumberIntFlag =
    6U | 0x10U | 0x20U | 0x80U;
constexpr std::uint16_t kNumberUintFlag =
    6U | 0x10U | 0x40U | 0x100U | 0x80U;
constexpr std::uint16_t kNumberInt64Flag = 6U | 0x10U | 0x80U;
constexpr std::uint16_t kNumberUint64Flag = 6U | 0x10U | 0x100U;
constexpr std::uint16_t kNumberDoubleFlag = 6U | 0x10U | 0x200U;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::uint32_t Load32(const std::uint8_t* bytes) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

std::uint16_t Load16(const std::uint8_t* bytes) {
    std::uint16_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

std::uint64_t Load64(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

void Store32(std::uint8_t* bytes, std::uint32_t value) {
    std::memcpy(bytes, &value, sizeof(value));
}

void Store16(std::uint8_t* bytes, std::uint16_t value) {
    std::memcpy(bytes, &value, sizeof(value));
}

void Store64(std::uint8_t* bytes, std::uint64_t value) {
    std::memcpy(bytes, &value, sizeof(value));
}

bool AddAligned(std::size_t size, std::size_t* total) {
    if (total == nullptr ||
        size > (std::numeric_limits<std::size_t>::max)() - 7U) {
        return false;
    }
    const std::size_t aligned = (size + 7U) & ~std::size_t{7U};
    if (*total > (std::numeric_limits<std::size_t>::max)() - aligned) {
        return false;
    }
    *total += aligned;
    return true;
}

bool MeasurePool(const rapidjson::Value& value, std::size_t depth,
                 std::size_t* bytes, std::uint64_t* nodes) {
    if (bytes == nullptr || nodes == nullptr || depth > kMaximumDepth ||
        *nodes == (std::numeric_limits<std::uint64_t>::max)()) {
        return false;
    }
    ++*nodes;
    if (value.IsString()) {
        const std::size_t length = value.GetStringLength();
        return length <= kShortStringMaximum ||
               (length != (std::numeric_limits<std::size_t>::max)() &&
                AddAligned(length + 1U, bytes));
    }
    if (value.IsArray()) {
        const std::size_t count = value.Size();
        if (count > (std::numeric_limits<std::size_t>::max)() /
                        kGuestValueSize ||
            !AddAligned(count * kGuestValueSize, bytes)) {
            return false;
        }
        for (auto item = value.Begin(); item != value.End(); ++item) {
            if (!MeasurePool(*item, depth + 1U, bytes, nodes)) return false;
        }
    } else if (value.IsObject()) {
        const std::size_t count = value.MemberCount();
        if (count > (std::numeric_limits<std::size_t>::max)() /
                        kGuestMemberSize ||
            !AddAligned(count * kGuestMemberSize, bytes)) {
            return false;
        }
        for (auto member = value.MemberBegin(); member != value.MemberEnd();
             ++member) {
            if (!MeasurePool(member->name, depth + 1U, bytes, nodes) ||
                !MeasurePool(member->value, depth + 1U, bytes, nodes)) {
                return false;
            }
        }
    }
    return true;
}

class GuestPoolImage {
public:
    GuestPoolImage(std::uint64_t chunk_address, std::size_t capacity)
        : chunk_address_(chunk_address), capacity_(capacity),
          bytes_(kChunkHeaderSize + capacity, 0) {
        Store64(bytes_.data(), capacity);
    }

    std::uint64_t Allocate(std::size_t size) {
        const std::size_t before = used_;
        if (!AddAligned(size, &used_) || used_ > capacity_) {
            used_ = before;
            return 0;
        }
        return chunk_address_ + kChunkHeaderSize + before;
    }

    std::uint8_t* Mutable(std::uint64_t address, std::size_t size) {
        if (address < chunk_address_) return nullptr;
        const std::uint64_t offset64 = address - chunk_address_;
        if (offset64 > bytes_.size()) return nullptr;
        const std::size_t offset = static_cast<std::size_t>(offset64);
        if (size > bytes_.size() - offset) return nullptr;
        return bytes_.data() + offset;
    }

    void FinishHeader() {
        Store64(bytes_.data() + 8U, used_);
        Store64(bytes_.data() + 16U, 0);
    }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    std::size_t used() const { return used_; }

private:
    std::uint64_t chunk_address_ = 0;
    std::size_t capacity_ = 0;
    std::size_t used_ = 0;
    std::vector<std::uint8_t> bytes_;
};

bool BuildValue(const rapidjson::Value& value, GuestPoolImage* pool,
                std::size_t depth,
                std::array<std::uint8_t, kGuestValueSize>* output) {
    if (pool == nullptr || output == nullptr || depth > kMaximumDepth) {
        return false;
    }
    output->fill(0);
    if (value.IsNull()) {
        Store16(output->data() + 22U, kNullFlag);
        return true;
    }
    if (value.IsBool()) {
        Store16(output->data() + 22U,
                value.GetBool() ? kTrueFlag : kFalseFlag);
        return true;
    }
    if (value.IsString()) {
        const std::size_t length = value.GetStringLength();
        if (length <= kShortStringMaximum) {
            if (length != 0)
                std::memcpy(output->data(), value.GetString(), length);
            (*output)[kShortStringMaximum] =
                static_cast<std::uint8_t>(kShortStringMaximum - length);
            Store16(output->data() + 22U, kShortStringFlag);
            return true;
        }
        const std::uint64_t string_address = pool->Allocate(length + 1U);
        std::uint8_t* destination =
            pool->Mutable(string_address, length + 1U);
        if (string_address == 0 || destination == nullptr) return false;
        std::memcpy(destination, value.GetString(), length);
        destination[length] = 0;
        Store32(output->data(), static_cast<std::uint32_t>(length));
        Store32(output->data() + 4U, 0);
        Store64(output->data() + 8U, string_address);
        Store16(output->data() + 22U, kCopyStringFlag);
        return true;
    }
    if (value.IsNumber()) {
        std::uint64_t bits = 0;
        std::uint16_t flags = 0;
        if (value.IsDouble()) {
            const double number = value.GetDouble();
            bits = std::bit_cast<std::uint64_t>(number);
            flags = kNumberDoubleFlag;
        } else if (value.IsInt64() && value.GetInt64() < 0) {
            const std::int64_t number = value.GetInt64();
            bits = std::bit_cast<std::uint64_t>(number);
            flags = number >= (std::numeric_limits<std::int32_t>::min)()
                        ? kNumberIntFlag
                        : kNumberInt64Flag;
        } else {
            const std::uint64_t number = value.GetUint64();
            bits = number;
            if (number <= (std::numeric_limits<std::uint32_t>::max)()) {
                flags = kNumberUintFlag;
                if (number <=
                    static_cast<std::uint64_t>(
                        (std::numeric_limits<std::int32_t>::max)())) {
                    flags |= 0x20U;
                }
            } else {
                flags = kNumberUint64Flag;
                if (number <= static_cast<std::uint64_t>(
                                  (std::numeric_limits<std::int64_t>::max)())) {
                    flags |= 0x80U;
                }
            }
        }
        Store64(output->data(), bits);
        Store16(output->data() + 22U, flags);
        return true;
    }
    if (value.IsArray()) {
        const std::size_t count = value.Size();
        std::uint64_t elements = 0;
        if (count != 0) {
            elements = pool->Allocate(count * kGuestValueSize);
            if (elements == 0) return false;
            std::size_t index = 0;
            for (auto item = value.Begin(); item != value.End();
                 ++item, ++index) {
                std::array<std::uint8_t, kGuestValueSize> child{};
                if (!BuildValue(*item, pool, depth + 1U, &child)) return false;
                std::uint8_t* destination = pool->Mutable(
                    elements + index * kGuestValueSize, child.size());
                if (destination == nullptr) return false;
                std::memcpy(destination, child.data(), child.size());
            }
        }
        Store32(output->data(), static_cast<std::uint32_t>(count));
        Store32(output->data() + 4U, static_cast<std::uint32_t>(count));
        Store64(output->data() + 8U, elements);
        Store16(output->data() + 22U, kArrayFlag);
        return true;
    }
    if (value.IsObject()) {
        const std::size_t count = value.MemberCount();
        std::uint64_t members = 0;
        if (count != 0) {
            members = pool->Allocate(count * kGuestMemberSize);
            if (members == 0) return false;
            std::size_t index = 0;
            for (auto member = value.MemberBegin(); member != value.MemberEnd();
                 ++member, ++index) {
                std::array<std::uint8_t, kGuestValueSize> name{};
                std::array<std::uint8_t, kGuestValueSize> child{};
                if (!BuildValue(member->name, pool, depth + 1U, &name) ||
                    !BuildValue(member->value, pool, depth + 1U, &child)) {
                    return false;
                }
                std::uint8_t* destination = pool->Mutable(
                    members + index * kGuestMemberSize, kGuestMemberSize);
                if (destination == nullptr) return false;
                std::memcpy(destination, name.data(), name.size());
                std::memcpy(destination + kGuestValueSize, child.data(),
                            child.size());
            }
        }
        Store32(output->data(), static_cast<std::uint32_t>(count));
        Store32(output->data() + 4U, static_cast<std::uint32_t>(count));
        Store64(output->data() + 8U, members);
        Store16(output->data() + 22U, kObjectFlag);
        return true;
    }
    return false;
}

class AllocationRollback {
public:
    explicit AllocationRollback(GuestHeap* heap) : heap_(heap) {}
    ~AllocationRollback() {
        if (released_ || heap_ == nullptr) return;
        for (auto address = addresses_.rbegin(); address != addresses_.rend();
             ++address) {
            std::string ignored;
            (void)heap_->Release(*address, &ignored);
        }
    }
    void Add(std::uint64_t address) {
        if (address != 0) addresses_.push_back(address);
    }
    void ReleaseOwnership() { released_ = true; }

private:
    GuestHeap* heap_ = nullptr;
    std::vector<std::uint64_t> addresses_;
    bool released_ = false;
};

}  // namespace

bool BuildCandyRapidJsonHybridDocument(
    GuestHeap& heap, std::uint64_t document_address, std::string_view json,
    CandyRapidJsonHostBuildResult* result, std::string* error) {
    if (result == nullptr || document_address == 0 || json.empty() ||
        json.back() == '\0') {
        SetError(error, "Candy RapidJSON host build arguments are invalid");
        return false;
    }
    *result = {};

    std::array<std::uint8_t, kGuestDocumentSize> document_bytes{};
    if (!heap.Read(document_address, document_bytes.data(),
                   document_bytes.size())) {
        SetError(error, "Candy RapidJSON host document is unreadable");
        return false;
    }
    const std::uint64_t allocator_address =
        Load64(document_bytes.data() + 24U);
    if (Load16(document_bytes.data() + 22U) != kNullFlag ||
        allocator_address == 0 ||
        Load64(document_bytes.data() + 32U) != allocator_address ||
        Load64(document_bytes.data() + 40U) != 0 ||
        Load64(document_bytes.data() + 48U) != 0 ||
        Load64(document_bytes.data() + 56U) != 0 ||
        Load64(document_bytes.data() + 64U) != 0 ||
        Load64(document_bytes.data() + 72U) != 0 ||
        Load64(document_bytes.data() + 80U) != 1024U ||
        Load32(document_bytes.data() + 88U) != 0 ||
        Load64(document_bytes.data() + 96U) != 0) {
        SetError(error,
                 "Candy RapidJSON host document is not freshly constructed");
        return false;
    }

    std::array<std::uint8_t, kGuestAllocatorSize> allocator_bytes{};
    if (!heap.Read(allocator_address, allocator_bytes.data(),
                   allocator_bytes.size()) ||
        Load64(allocator_bytes.data()) != 0 ||
        Load64(allocator_bytes.data() + 8U) != kDefaultChunkCapacity ||
        Load64(allocator_bytes.data() + 16U) != 0 ||
        Load64(allocator_bytes.data() + 24U) != 0 ||
        Load64(allocator_bytes.data() + 32U) != 0) {
        SetError(error,
                 "Candy RapidJSON legacy allocator layout mismatch");
        return false;
    }

    std::string input(json);
    rapidjson::Document host_document;
    host_document.Parse<0>(input.c_str());
    if (host_document.HasParseError()) {
        SetError(error, "Candy RapidJSON host parser rejected input");
        return false;
    }

    std::size_t required = 0;
    std::uint64_t nodes = 0;
    if (!MeasurePool(host_document, 0, &required, &nodes)) {
        SetError(error, "Candy RapidJSON host pool measurement overflowed");
        return false;
    }
    const std::size_t capacity =
        (std::max)(required, kDefaultChunkCapacity);
    if (capacity > (std::numeric_limits<std::uint64_t>::max)() -
                       kChunkHeaderSize) {
        SetError(error, "Candy RapidJSON host pool is too large");
        return false;
    }

    AllocationRollback rollback(&heap);
    const std::uint64_t base_allocator = heap.Allocate(1);
    rollback.Add(base_allocator);
    const std::uint64_t chunk =
        heap.Allocate(static_cast<std::uint64_t>(kChunkHeaderSize) + capacity);
    rollback.Add(chunk);
    if (base_allocator == 0 || chunk == 0) {
        SetError(error, "Candy RapidJSON host guest allocation failed");
        return false;
    }

    GuestPoolImage pool(chunk, capacity);
    std::array<std::uint8_t, kGuestValueSize> root{};
    if (!BuildValue(host_document, &pool, 0, &root) ||
        pool.used() != required) {
        SetError(error, "Candy RapidJSON host document image failed");
        return false;
    }
    pool.FinishHeader();
    const std::uint8_t zero = 0;
    if (!heap.Write(base_allocator, &zero, sizeof(zero)) ||
        !heap.Write(chunk, pool.bytes().data(), pool.bytes().size())) {
        SetError(error, "Candy RapidJSON host pool commit failed");
        return false;
    }

    Store64(allocator_bytes.data(), chunk);
    Store64(allocator_bytes.data() + 8U, kDefaultChunkCapacity);
    Store64(allocator_bytes.data() + 16U, 0);
    Store64(allocator_bytes.data() + 24U, base_allocator);
    Store64(allocator_bytes.data() + 32U, base_allocator);
    if (!heap.Write(allocator_address, allocator_bytes.data(),
                    allocator_bytes.size())) {
        SetError(error, "Candy RapidJSON host allocator commit failed");
        return false;
    }
    rollback.ReleaseOwnership();
    if (!heap.Write(document_address, root.data(), root.size())) {
        SetError(error, "Candy RapidJSON host root commit failed");
        return false;
    }

    result->nodes = nodes;
    result->pool_bytes = required;
    return true;
}

}  // namespace isomr
