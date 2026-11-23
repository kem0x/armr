#include "bionic_format.h"

#include "guest_address_space.h"
#include "guest_heap.h"
#include "import_registry.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace isomr {
namespace {

constexpr std::size_t kMaximumFormat = 64U * 1024U;
constexpr std::size_t kMaximumResult = 64U * 1024U;
constexpr unsigned kMaximumField = 4096;

struct Aarch64VaList {
    std::uint64_t stack;
    std::uint64_t general_top;
    std::uint64_t vector_top;
    std::int32_t general_offset;
    std::int32_t vector_offset;
};
static_assert(sizeof(Aarch64VaList) == 32);

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

bool ReadGuestString(const GuestAddressSpace* memory, std::uint64_t address,
                     std::string* text, std::string* error) {
    if (memory == nullptr || text == nullptr || address == 0) {
        SetError(error, "vsnprintf received an invalid guest string");
        return false;
    }
    std::uint64_t length = 0;
    if (!memory->BoundedStringLength(address, kMaximumFormat + 1, &length,
                                     error) ||
        length > kMaximumFormat ||
        length > std::numeric_limits<std::size_t>::max()) {
        SetError(error, "vsnprintf guest string exceeds the native bound");
        return false;
    }
    text->resize(static_cast<std::size_t>(length));
    if (text->empty() || memory->Read(address, text->data(), text->size()))
        return true;
    SetError(error, "vsnprintf could not read a guest string");
    return false;
}

class VaReader {
public:
    VaReader(GuestAddressSpace* memory, std::uint64_t address)
        : memory_(memory), address_(address) {}

    bool Initialize(std::string* error) {
        if (memory_ == nullptr || address_ == 0 ||
            !memory_->Read(address_, &list_, sizeof(list_))) {
            SetError(error, "vsnprintf could not read the AArch64 va_list");
            return false;
        }
        if (list_.general_offset < -64 || list_.general_offset > 0 ||
            (list_.general_offset % 8) != 0 ||
            list_.vector_offset < -128 || list_.vector_offset > 0 ||
            (list_.vector_offset % 16) != 0) {
            SetError(error, "vsnprintf received a malformed AArch64 va_list");
            return false;
        }
        return true;
    }

    bool ReadGeneral(std::uint64_t* value, std::string* error) {
        std::uint64_t source = 0;
        if (list_.general_offset < 0) {
            const std::int32_t offset = list_.general_offset;
            list_.general_offset += 8;
            if (list_.general_offset <= 0) {
                if (offset < 0 &&
                    list_.general_top < static_cast<std::uint64_t>(-offset)) {
                    SetError(error, "vsnprintf general save area underflowed");
                    return false;
                }
                source = list_.general_top + offset;
            }
        }
        if (source == 0 && !TakeStackSlot(&source, error)) return false;
        if (!memory_->Read(source, value, sizeof(*value))) {
            SetError(error, "vsnprintf could not read a general argument");
            return false;
        }
        return true;
    }

    bool ReadDouble(double* value, std::string* error) {
        std::uint64_t source = 0;
        if (list_.vector_offset < 0) {
            const std::int32_t offset = list_.vector_offset;
            list_.vector_offset += 16;
            if (list_.vector_offset <= 0) {
                if (offset < 0 &&
                    list_.vector_top < static_cast<std::uint64_t>(-offset)) {
                    SetError(error, "vsnprintf vector save area underflowed");
                    return false;
                }
                source = list_.vector_top + offset;
            }
        }
        if (source == 0 && !TakeStackSlot(&source, error)) return false;
        if (!memory_->Read(source, value, sizeof(*value))) {
            SetError(error, "vsnprintf could not read a floating argument");
            return false;
        }
        return true;
    }

    bool Commit(std::string* error) {
        if (memory_->Write(address_, &list_, sizeof(list_))) return true;
        SetError(error, "vsnprintf could not update the AArch64 va_list");
        return false;
    }

private:
    bool TakeStackSlot(std::uint64_t* source, std::string* error) {
        if (list_.stack > std::numeric_limits<std::uint64_t>::max() - 7U) {
            SetError(error, "vsnprintf argument stack overflowed");
            return false;
        }
        const std::uint64_t aligned = (list_.stack + 7U) & ~7ULL;
        if (aligned > std::numeric_limits<std::uint64_t>::max() - 8U) {
            SetError(error, "vsnprintf argument stack overflowed");
            return false;
        }
        *source = aligned;
        list_.stack = aligned + 8U;
        return true;
    }

    GuestAddressSpace* memory_ = nullptr;
    std::uint64_t address_ = 0;
    Aarch64VaList list_{};
};

bool AppendPiece(std::string_view piece, std::string* output,
                 std::string* error) {
    if (piece.size() > kMaximumResult - output->size()) {
        SetError(error, "vsnprintf result exceeds the native bound");
        return false;
    }
    output->append(piece);
    return true;
}

template <typename Value>
bool AppendFormatted(const std::string& specification, Value value,
                     std::string* output, std::string* error) {
    std::array<char, kMaximumField + 1> buffer{};
    const int count = std::snprintf(buffer.data(), buffer.size(),
                                    specification.c_str(), value);
    if (count < 0 || static_cast<std::size_t>(count) >= buffer.size()) {
        SetError(error, "vsnprintf field exceeds the native bound");
        return false;
    }
    return AppendPiece(std::string_view(buffer.data(),
                                        static_cast<std::size_t>(count)),
                       output, error);
}

bool ParseUnsigned(std::string_view format, std::size_t* cursor,
                   unsigned* value, std::string* error) {
    unsigned parsed = 0;
    while (*cursor < format.size() &&
           std::isdigit(static_cast<unsigned char>(format[*cursor])) != 0) {
        const unsigned digit =
            static_cast<unsigned>(format[*cursor] - '0');
        if (parsed > (kMaximumField - digit) / 10U) {
            SetError(error, "vsnprintf field width exceeds the native bound");
            return false;
        }
        parsed = parsed * 10U + digit;
        ++*cursor;
    }
    *value = parsed;
    return true;
}

std::int64_t SignedInteger(std::uint64_t raw, std::string_view length) {
    if (length == "hh")
        return static_cast<std::int8_t>(raw);
    if (length == "h")
        return static_cast<std::int16_t>(raw);
    if (length.empty())
        return static_cast<std::int32_t>(raw);
    return static_cast<std::int64_t>(raw);
}

std::uint64_t UnsignedInteger(std::uint64_t raw, std::string_view length) {
    if (length == "hh") return static_cast<std::uint8_t>(raw);
    if (length == "h") return static_cast<std::uint16_t>(raw);
    if (length.empty()) return static_cast<std::uint32_t>(raw);
    return raw;
}

bool Format(GuestAddressSpace* memory, std::uint64_t format_address,
            std::uint64_t list_address, std::string* output,
            std::string* error) {
    std::string format;
    if (!ReadGuestString(memory, format_address, &format, error)) return false;
    VaReader arguments(memory, list_address);
    if (!arguments.Initialize(error)) return false;

    for (std::size_t cursor = 0; cursor < format.size();) {
        if (format[cursor] != '%') {
            const std::size_t begin = cursor;
            while (cursor < format.size() && format[cursor] != '%') ++cursor;
            if (!AppendPiece(std::string_view(format).substr(begin,
                                                             cursor - begin),
                             output, error))
                return false;
            continue;
        }
        ++cursor;
        if (cursor < format.size() && format[cursor] == '%') {
            if (!AppendPiece("%", output, error)) return false;
            ++cursor;
            continue;
        }

        std::string flags;
        while (cursor < format.size() &&
               std::string_view("-+ #0").find(format[cursor]) !=
                   std::string_view::npos) {
            if (flags.find(format[cursor]) == std::string::npos)
                flags.push_back(format[cursor]);
            ++cursor;
        }

        bool has_width = false;
        unsigned width = 0;
        if (cursor < format.size() && format[cursor] == '*') {
            std::uint64_t raw = 0;
            if (!arguments.ReadGeneral(&raw, error)) return false;
            const std::int32_t signed_width = static_cast<std::int32_t>(raw);
            if (signed_width == std::numeric_limits<std::int32_t>::min() ||
                static_cast<std::uint32_t>(signed_width < 0
                                               ? -signed_width
                                               : signed_width) >
                    kMaximumField) {
                SetError(error,
                         "vsnprintf field width exceeds the native bound");
                return false;
            }
            width = static_cast<unsigned>(signed_width < 0 ? -signed_width
                                                            : signed_width);
            has_width = true;
            if (signed_width < 0 && flags.find('-') == std::string::npos)
                flags.push_back('-');
            ++cursor;
        } else if (cursor < format.size() &&
                   std::isdigit(static_cast<unsigned char>(format[cursor])) !=
                       0) {
            has_width = true;
            if (!ParseUnsigned(format, &cursor, &width, error)) return false;
        }

        bool has_precision = false;
        unsigned precision = 0;
        if (cursor < format.size() && format[cursor] == '.') {
            ++cursor;
            if (cursor < format.size() && format[cursor] == '*') {
                std::uint64_t raw = 0;
                if (!arguments.ReadGeneral(&raw, error)) return false;
                const std::int32_t signed_precision =
                    static_cast<std::int32_t>(raw);
                if (signed_precision >= 0) {
                    if (static_cast<std::uint32_t>(signed_precision) >
                        kMaximumField) {
                        SetError(error,
                                 "vsnprintf precision exceeds the native bound");
                        return false;
                    }
                    precision = static_cast<unsigned>(signed_precision);
                    has_precision = true;
                }
                ++cursor;
            } else {
                has_precision = true;
                if (!ParseUnsigned(format, &cursor, &precision, error))
                    return false;
            }
        }

        std::string length;
        if (cursor < format.size() &&
            std::string_view("hljztL").find(format[cursor]) !=
                std::string_view::npos) {
            length.push_back(format[cursor++]);
            if (cursor < format.size() &&
                ((length == "h" && format[cursor] == 'h') ||
                 (length == "l" && format[cursor] == 'l')))
                length.push_back(format[cursor++]);
        }
        if (cursor >= format.size()) {
            SetError(error, "vsnprintf format ends inside a conversion");
            return false;
        }
        const char conversion = format[cursor++];
        if (conversion == 'n' || conversion == 'm' || length == "L" ||
            ((conversion == 'c' || conversion == 's') && !length.empty())) {
            SetError(error, "vsnprintf encountered an unsupported conversion");
            return false;
        }

        std::string specification = "%" + flags;
        if (has_width) specification += std::to_string(width);
        if (has_precision) specification += "." + std::to_string(precision);

        std::uint64_t raw = 0;
        switch (conversion) {
        case 'd':
        case 'i':
            if (!arguments.ReadGeneral(&raw, error)) return false;
            specification += "ll";
            specification.push_back(conversion);
            if (!AppendFormatted(specification, SignedInteger(raw, length),
                                 output, error))
                return false;
            break;
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            if (!arguments.ReadGeneral(&raw, error)) return false;
            specification += "ll";
            specification.push_back(conversion);
            if (!AppendFormatted(specification, UnsignedInteger(raw, length),
                                 output, error))
                return false;
            break;
        case 'c':
            if (!arguments.ReadGeneral(&raw, error)) return false;
            specification.push_back(conversion);
            if (!AppendFormatted(specification, static_cast<int>(raw), output,
                                 error))
                return false;
            break;
        case 's': {
            if (!arguments.ReadGeneral(&raw, error)) return false;
            std::string text;
            if (raw == 0)
                text = "(null)";
            else if (!ReadGuestString(memory, raw, &text, error))
                return false;
            specification.push_back(conversion);
            if (!AppendFormatted(specification, text.c_str(), output, error))
                return false;
            break;
        }
        case 'p':
            if (!length.empty() || !arguments.ReadGeneral(&raw, error)) {
                if (length.empty()) return false;
                SetError(error,
                         "vsnprintf pointer conversion has a length modifier");
                return false;
            }
            specification += "llx";
            if (raw == 0) {
                if (!AppendPiece("(nil)", output, error)) return false;
            } else {
                std::string pointer_piece = "0x";
                if (!AppendFormatted(specification, raw, &pointer_piece,
                                     error) ||
                    !AppendPiece(pointer_piece, output, error))
                    return false;
            }
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            if (!length.empty() && length != "l") {
                SetError(error,
                         "vsnprintf floating conversion has an unsupported length");
                return false;
            }
            double value = 0;
            if (!arguments.ReadDouble(&value, error)) return false;
            specification.push_back(conversion);
            if (!AppendFormatted(specification, value, output, error))
                return false;
            break;
        }
        default:
            SetError(error, "vsnprintf encountered an unsupported conversion");
            return false;
        }
    }
    return arguments.Commit(error);
}

bool WriteFormattedOutput(GuestAddressSpace* memory,
                          std::uint64_t destination,
                          std::uint64_t capacity,
                          const std::string& output,
                          std::string_view function,
                          std::string* error) {
    if (capacity == 0) return true;
    if (destination == 0) {
        SetError(error, std::string(function) +
                            " received a null output buffer");
        return false;
    }
    const std::size_t copy = static_cast<std::size_t>(
        std::min<std::uint64_t>(capacity - 1U, output.size()));
    if (!memory->IsWritableRange(destination, copy + 1U) ||
        (copy != 0 && !memory->Write(destination, output.data(), copy)) ||
        !memory->Write(destination + copy, "", 1)) {
        SetError(error, std::string(function) +
                            " could not write the guest output");
        return false;
    }
    return true;
}

bool WriteFortifiedFormattedOutput(GuestAddressSpace* memory,
                                   std::uint64_t destination,
                                   std::uint64_t capacity,
                                   const std::string& output,
                                   std::string_view function,
                                   std::string* error) {
    if (capacity != std::numeric_limits<std::uint64_t>::max() &&
        output.size() >= capacity) {
        SetError(error, std::string(function) +
                            " prevented a write past the guest output buffer");
        return false;
    }
    return WriteFormattedOutput(memory, destination, capacity, output,
                                function, error);
}

enum class ScanLength {
    kNone,
    kChar,
    kShort,
    kLong,
    kLongLong,
    kIntMax,
    kSize,
    kPtrDiff,
};

struct ScanConversion {
    bool suppressed = false;
    bool has_width = false;
    unsigned width = 0;
    ScanLength length = ScanLength::kNone;
    char conversion = '\0';
    // For the '[' scanset conversion: a 256-entry membership bitmap. Index by
    // the (unsigned char) input byte; true means the byte is in the set.
    std::bitset<256> scanset;
};

struct PendingScanWrite {
    std::uint64_t address = 0;
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> original;
    std::size_t conversion_index = 0;
    std::size_t format_offset = 0;
    char conversion = '\0';
    std::string argument_source;
};

struct ScanWriteContext {
    std::size_t conversion_index = 0;
    std::size_t format_offset = 0;
    char conversion = '\0';
    std::string argument_source;
};

bool ReadScanString(const GuestAddressSpace* memory, std::uint64_t address,
                    const char* description, std::string* text,
                    std::string* error) {
    if (memory == nullptr || text == nullptr || address == 0) {
        SetError(error, std::string("sscanf received an invalid guest ") +
                            description);
        return false;
    }
    std::uint64_t length = 0;
    if (!memory->BoundedStringLength(address, kMaximumFormat + 1, &length,
                                     error) ||
        length > kMaximumFormat ||
        length > std::numeric_limits<std::size_t>::max()) {
        SetError(error, std::string("sscanf guest ") + description +
                            " exceeds the native bound");
        return false;
    }
    text->resize(static_cast<std::size_t>(length));
    if (text->empty() || memory->Read(address, text->data(), text->size()))
        return true;
    SetError(error, std::string("sscanf could not read the guest ") +
                        description);
    return false;
}

bool ParseScanConversion(std::string_view format, std::size_t* cursor,
                         ScanConversion* result, std::string* error) {
    if (cursor == nullptr || result == nullptr || *cursor >= format.size()) {
        SetError(error, "sscanf format ends inside a conversion");
        return false;
    }
    ScanConversion parsed{};
    if (format[*cursor] == '*') {
        parsed.suppressed = true;
        ++*cursor;
    }
    if (*cursor < format.size() &&
        std::isdigit(static_cast<unsigned char>(format[*cursor])) != 0) {
        parsed.has_width = true;
        if (!ParseUnsigned(format, cursor, &parsed.width, error)) return false;
        if (parsed.width == 0) {
            SetError(error, "sscanf field width must be nonzero");
            return false;
        }
    }

    if (*cursor < format.size()) {
        switch (format[*cursor]) {
        case 'h':
            ++*cursor;
            if (*cursor < format.size() && format[*cursor] == 'h') {
                ++*cursor;
                parsed.length = ScanLength::kChar;
            } else {
                parsed.length = ScanLength::kShort;
            }
            break;
        case 'l':
            ++*cursor;
            if (*cursor < format.size() && format[*cursor] == 'l') {
                ++*cursor;
                parsed.length = ScanLength::kLongLong;
            } else {
                parsed.length = ScanLength::kLong;
            }
            break;
        case 'j':
            ++*cursor;
            parsed.length = ScanLength::kIntMax;
            break;
        case 'z':
            ++*cursor;
            parsed.length = ScanLength::kSize;
            break;
        case 't':
            ++*cursor;
            parsed.length = ScanLength::kPtrDiff;
            break;
        case 'L':
            SetError(error,
                     "sscanf long-double conversion is unsupported on the native host");
            return false;
        default:
            break;
        }
    }
    if (*cursor >= format.size()) {
        SetError(error, "sscanf format ends inside a conversion");
        return false;
    }
    parsed.conversion = format[(*cursor)++];
    const bool integer =
        std::string_view("diouxXn").find(parsed.conversion) !=
        std::string_view::npos;
    if (integer) {
        if (parsed.conversion == 'n' &&
            (parsed.suppressed || parsed.has_width)) {
            SetError(error,
                     "sscanf rejects suppressed or width-qualified percent-n");
            return false;
        }
    } else if (parsed.conversion == 'c' || parsed.conversion == 's') {
        if (parsed.length != ScanLength::kNone) {
            SetError(error,
                     "sscanf wide character conversions are unsupported");
            return false;
        }
    } else if (parsed.conversion == '[') {
        // Scanset: %[set] / %[^set]. A ']' as the first member is a literal
        // ']', and 'a-z' denotes an inclusive range.
        if (parsed.length != ScanLength::kNone) {
            SetError(error,
                     "sscanf wide scanset conversions are unsupported");
            return false;
        }
        bool negate = false;
        if (*cursor < format.size() && format[*cursor] == '^') {
            negate = true;
            ++*cursor;
        }
        // A ']' as the first set member is a literal ']'.
        if (*cursor < format.size() && format[*cursor] == ']') {
            parsed.scanset[static_cast<unsigned char>(']')] = true;
            ++*cursor;
        }
        bool closed = false;
        while (*cursor < format.size()) {
            const char member = format[(*cursor)++];
            if (member == ']') {
                closed = true;
                break;
            }
            if (member == '-' && *cursor < format.size() &&
                format[*cursor] != ']' && format[*cursor - 2] != '[' &&
                format[*cursor - 2] != '^') {
                const unsigned char lo =
                    static_cast<unsigned char>(format[*cursor - 2]);
                const unsigned char hi =
                    static_cast<unsigned char>(format[*cursor]);
                ++*cursor;
                for (unsigned value = lo; value <= hi; ++value)
                    parsed.scanset[value] = true;
                continue;
            }
            parsed.scanset[static_cast<unsigned char>(member)] = true;
        }
        if (!closed) {
            SetError(error, "sscanf scanset is missing a closing bracket");
            return false;
        }
        if (negate) parsed.scanset.flip();
    } else if (std::string_view("fgeEaA").find(parsed.conversion) !=
               std::string_view::npos) {
        // Floating conversions: no length -> float*, 'l' -> double*. 'L'
        // (long double) is already rejected during length parsing.
        if (parsed.length == ScanLength::kChar ||
            parsed.length == ScanLength::kShort) {
            SetError(error,
                     "sscanf floating conversion has an invalid length");
            return false;
        }
    } else {
        SetError(error, "sscanf encountered an unsupported conversion");
        return false;
    }
    *result = parsed;
    return true;
}

bool PreflightScanFormat(std::string_view format, std::string* error) {
    for (std::size_t cursor = 0; cursor < format.size();) {
        if (format[cursor++] != '%') continue;
        if (cursor < format.size() && format[cursor] == '%') {
            ++cursor;
            continue;
        }
        ScanConversion conversion{};
        if (!ParseScanConversion(format, &cursor, &conversion, error))
            return false;
    }
    return true;
}

class RawScanArgumentReader {
public:
    RawScanArgumentReader(const Aarch64ImportRegistry::Registers& registers,
                          GuestAddressSpace* memory)
        : registers_(registers), memory_(memory) {}

    bool Read(std::uint64_t* value, std::string* source_description,
              std::string* error) {
        if (value == nullptr || source_description == nullptr ||
            memory_ == nullptr) {
            SetError(error, "sscanf variadic reader is not configured");
            return false;
        }
        if (index_ < 6) {
            *value = registers_.general[index_ + 2];
            *source_description = "x" + std::to_string(index_ + 2);
            ++index_;
            return true;
        }
        const std::size_t stack_index = index_ - 6;
        if (stack_index >
            (std::numeric_limits<std::uint64_t>::max() -
             registers_.stack_pointer) /
                sizeof(std::uint64_t)) {
            SetError(error, "sscanf variadic stack address overflowed");
            return false;
        }
        const std::uint64_t source =
            registers_.stack_pointer +
            static_cast<std::uint64_t>(stack_index) * sizeof(std::uint64_t);
        *source_description =
            "stack+" + std::to_string(stack_index * sizeof(std::uint64_t));
        ++index_;
        if (source == 0 || !memory_->Read(source, value, sizeof(*value))) {
            SetError(error,
                     "sscanf could not read an AArch64 variadic stack argument");
            return false;
        }
        return true;
    }

private:
    const Aarch64ImportRegistry::Registers& registers_;
    GuestAddressSpace* memory_ = nullptr;
    std::size_t index_ = 0;
};

template <typename Value>
void StageScanValue(std::uint64_t address, Value value,
                    const ScanWriteContext& context,
                    std::vector<PendingScanWrite>* writes) {
    PendingScanWrite write{};
    write.address = address;
    write.bytes.resize(sizeof(value));
    std::memcpy(write.bytes.data(), &value, sizeof(value));
    write.conversion_index = context.conversion_index;
    write.format_offset = context.format_offset;
    write.conversion = context.conversion;
    write.argument_source = context.argument_source;
    writes->push_back(std::move(write));
}

bool StageSignedScanValue(std::uint64_t address, std::int64_t value,
                          ScanLength length,
                          const ScanWriteContext& context,
                          std::vector<PendingScanWrite>* writes,
                          std::string* error) {
    switch (length) {
    case ScanLength::kChar:
        if (value < std::numeric_limits<std::int8_t>::min() ||
            value > std::numeric_limits<std::int8_t>::max())
            break;
        StageScanValue(address, static_cast<std::int8_t>(value), context,
                       writes);
        return true;
    case ScanLength::kShort:
        if (value < std::numeric_limits<std::int16_t>::min() ||
            value > std::numeric_limits<std::int16_t>::max())
            break;
        StageScanValue(address, static_cast<std::int16_t>(value), context,
                       writes);
        return true;
    case ScanLength::kNone:
        if (value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max())
            break;
        StageScanValue(address, static_cast<std::int32_t>(value), context,
                       writes);
        return true;
    case ScanLength::kLong:
    case ScanLength::kLongLong:
    case ScanLength::kIntMax:
    case ScanLength::kSize:
    case ScanLength::kPtrDiff:
        StageScanValue(address, value, context, writes);
        return true;
    }
    SetError(error, "sscanf signed value does not fit its guest destination");
    return false;
}

bool StageUnsignedScanValue(std::uint64_t address, std::uint64_t value,
                            ScanLength length,
                            const ScanWriteContext& context,
                            std::vector<PendingScanWrite>* writes,
                            std::string* error) {
    switch (length) {
    case ScanLength::kChar:
        if (value > std::numeric_limits<std::uint8_t>::max()) break;
        StageScanValue(address, static_cast<std::uint8_t>(value), context,
                       writes);
        return true;
    case ScanLength::kShort:
        if (value > std::numeric_limits<std::uint16_t>::max()) break;
        StageScanValue(address, static_cast<std::uint16_t>(value), context,
                       writes);
        return true;
    case ScanLength::kNone:
        if (value > std::numeric_limits<std::uint32_t>::max()) break;
        StageScanValue(address, static_cast<std::uint32_t>(value), context,
                       writes);
        return true;
    case ScanLength::kLong:
    case ScanLength::kLongLong:
    case ScanLength::kIntMax:
    case ScanLength::kSize:
    case ScanLength::kPtrDiff:
        StageScanValue(address, value, context, writes);
        return true;
    }
    SetError(error,
             "sscanf unsigned value does not fit its guest destination");
    return false;
}

bool CommitScanWrites(GuestAddressSpace* memory,
                      std::vector<PendingScanWrite>* writes,
                      std::string* error) {
    if (memory == nullptr || writes == nullptr) {
        SetError(error, "sscanf output staging is not configured");
        return false;
    }
    for (PendingScanWrite& write : *writes) {
        const std::string provenance =
            " conversion=" + std::to_string(write.conversion_index) +
            " format-offset=" + std::to_string(write.format_offset) + " %" +
            std::string(1, write.conversion) + " argument=" +
            write.argument_source + " range=" +
            memory->DescribeRange(write.address, write.bytes.size());
        if (write.address == 0 || write.bytes.empty()) {
            SetError(error,
                     "sscanf received a null guest output pointer" +
                         provenance);
            return false;
        }
        write.original.resize(write.bytes.size());
        if (!memory->Read(write.address, write.original.data(),
                          write.original.size()) ||
            !memory->IsWritableRange(write.address, write.bytes.size())) {
            SetError(error,
                     "sscanf output pointer is not writable guest memory" +
                         provenance);
            return false;
        }
    }
    std::size_t committed = 0;
    for (; committed < writes->size(); ++committed) {
        PendingScanWrite& write = (*writes)[committed];
        if (memory->Write(write.address, write.bytes.data(),
                          write.bytes.size()))
            continue;
        while (committed != 0) {
            --committed;
            PendingScanWrite& rollback = (*writes)[committed];
            (void)memory->Write(rollback.address, rollback.original.data(),
                                rollback.original.size());
        }
        SetError(error, "sscanf could not write a validated guest output");
        return false;
    }
    return true;
}

bool FinishScan(GuestAddressSpace* memory,
                std::vector<PendingScanWrite>* writes, int result,
                std::uint64_t* raw_result, std::string* error) {
    if (raw_result == nullptr ||
        !CommitScanWrites(memory, writes, error))
        return false;
    *raw_result = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(result));
    return true;
}

bool ScanBuffer(GuestAddressSpace* memory, const std::string& input,
                const std::string& format,
                const Aarch64ImportRegistry::Registers& registers,
                std::size_t* consumed_out, std::uint64_t* raw_result,
                std::string* error) {
    if (!PreflightScanFormat(format, error)) return false;

    RawScanArgumentReader arguments(registers, memory);
    std::vector<PendingScanWrite> writes;
    std::size_t input_cursor = 0;
    std::size_t format_cursor = 0;
    std::size_t conversion_index = 0;
    int assignments = 0;
    bool conversion_succeeded = false;
    const auto report_consumed = [&]() {
        if (consumed_out != nullptr) *consumed_out = input_cursor;
    };
    const auto finish_matching_failure = [&]() {
        report_consumed();
        return FinishScan(memory, &writes, assignments, raw_result, error);
    };
    const auto finish_input_failure = [&]() {
        report_consumed();
        const int result = conversion_succeeded ? assignments : -1;
        return FinishScan(memory, &writes, result, raw_result, error);
    };

    while (format_cursor < format.size()) {
        const unsigned char format_character =
            static_cast<unsigned char>(format[format_cursor]);
        if (std::isspace(format_character) != 0) {
            while (format_cursor < format.size() &&
                   std::isspace(static_cast<unsigned char>(
                       format[format_cursor])) != 0)
                ++format_cursor;
            while (input_cursor < input.size() &&
                   std::isspace(static_cast<unsigned char>(
                       input[input_cursor])) != 0)
                ++input_cursor;
            continue;
        }
        if (format[format_cursor] != '%') {
            if (input_cursor >= input.size()) return finish_input_failure();
            if (input[input_cursor] != format[format_cursor])
                return finish_matching_failure();
            ++input_cursor;
            ++format_cursor;
            continue;
        }
        const std::size_t conversion_format_offset = format_cursor;
        ++format_cursor;
        if (format_cursor < format.size() && format[format_cursor] == '%') {
            if (input_cursor >= input.size()) return finish_input_failure();
            if (input[input_cursor] != '%')
                return finish_matching_failure();
            ++input_cursor;
            ++format_cursor;
            continue;
        }

        ScanConversion conversion{};
        if (!ParseScanConversion(format, &format_cursor, &conversion, error))
            return false;
        const std::size_t current_conversion_index = conversion_index++;
        const bool skip_space = conversion.conversion != 'c' &&
                                conversion.conversion != 'n' &&
                                conversion.conversion != '[';
        if (skip_space) {
            while (input_cursor < input.size() &&
                   std::isspace(static_cast<unsigned char>(
                       input[input_cursor])) != 0)
                ++input_cursor;
        }

        std::uint64_t output_address = 0;
        std::string argument_source;
        if (!conversion.suppressed &&
            !arguments.Read(&output_address, &argument_source, error))
            return false;
        const ScanWriteContext write_context{
            current_conversion_index, conversion_format_offset,
            conversion.conversion, std::move(argument_source)};

        if (conversion.conversion == 'n') {
            if (!StageSignedScanValue(
                    output_address, static_cast<std::int64_t>(input_cursor),
                    conversion.length, write_context, &writes, error))
                return false;
            conversion_succeeded = true;
            continue;
        }

        if (conversion.conversion == 's' || conversion.conversion == 'c') {
            const std::size_t available = input.size() - input_cursor;
            std::size_t count = 0;
            if (conversion.conversion == 'c') {
                count = conversion.has_width ? conversion.width : 1U;
                if (available < count) return finish_input_failure();
            } else {
                const std::size_t limit = conversion.has_width
                                              ? conversion.width
                                              : kMaximumField;
                while (count < available && count < limit &&
                       std::isspace(static_cast<unsigned char>(
                           input[input_cursor + count])) == 0)
                    ++count;
                if (count == 0) return finish_input_failure();
                if (!conversion.has_width && count == kMaximumField &&
                    count < available &&
                    std::isspace(static_cast<unsigned char>(
                        input[input_cursor + count])) == 0) {
                    SetError(error,
                             "sscanf string field exceeds the native bound");
                    return false;
                }
            }
            if (!conversion.suppressed) {
                PendingScanWrite write{};
                write.address = output_address;
                write.bytes.assign(input.begin() + input_cursor,
                                   input.begin() + input_cursor + count);
                if (conversion.conversion == 's') write.bytes.push_back(0);
                write.conversion_index = write_context.conversion_index;
                write.format_offset = write_context.format_offset;
                write.conversion = write_context.conversion;
                write.argument_source = write_context.argument_source;
                writes.push_back(std::move(write));
                ++assignments;
            }
            input_cursor += count;
            conversion_succeeded = true;
            continue;
        }

        if (conversion.conversion == '[') {
            const std::size_t available = input.size() - input_cursor;
            const std::size_t limit = conversion.has_width
                                          ? conversion.width
                                          : kMaximumField;
            std::size_t count = 0;
            while (count < available && count < limit &&
                   conversion.scanset[static_cast<unsigned char>(
                       input[input_cursor + count])])
                ++count;
            if (count == 0) return finish_matching_failure();
            if (!conversion.suppressed) {
                PendingScanWrite write{};
                write.address = output_address;
                write.bytes.assign(input.begin() + input_cursor,
                                   input.begin() + input_cursor + count);
                write.bytes.push_back(0);
                write.conversion_index = write_context.conversion_index;
                write.format_offset = write_context.format_offset;
                write.conversion = write_context.conversion;
                write.argument_source = write_context.argument_source;
                writes.push_back(std::move(write));
                ++assignments;
            }
            input_cursor += count;
            conversion_succeeded = true;
            continue;
        }

        if (std::string_view("fgeEaA")
                .find(conversion.conversion) != std::string_view::npos) {
            const std::size_t available = input.size() - input_cursor;
            const std::size_t field_limit = conversion.has_width
                                                ? conversion.width
                                                : kMaximumField;
            const std::size_t count = std::min(available, field_limit);
            if (count == 0) return finish_input_failure();
            std::string field(input.data() + input_cursor, count);
            char* end = nullptr;
            const int saved_errno = errno;
            errno = 0;
            const double value = std::strtod(field.c_str(), &end);
            const int parse_errno = errno;
            errno = saved_errno;
            const std::size_t consumed =
                static_cast<std::size_t>(end - field.c_str());
            if (consumed == 0) return finish_matching_failure();
            if (parse_errno == ERANGE) {
                SetError(error,
                         "sscanf floating value exceeds the native bound conversion=" +
                             std::to_string(current_conversion_index));
                return false;
            }
            if (!conversion.suppressed) {
                const bool is_double =
                    conversion.length == ScanLength::kLong ||
                    conversion.length == ScanLength::kLongLong;
                if (is_double) {
                    StageScanValue(output_address,
                                   static_cast<double>(value),
                                   write_context, &writes);
                } else {
                    StageScanValue(output_address,
                                   static_cast<float>(value), write_context,
                                   &writes);
                }
                ++assignments;
            }
            input_cursor += consumed;
            conversion_succeeded = true;
            continue;
        }

        const std::size_t available = input.size() - input_cursor;
        const std::size_t field_limit = conversion.has_width
                                            ? conversion.width
                                            : kMaximumField;
        const std::size_t count = std::min(available, field_limit);
        if (count == 0) return finish_input_failure();
        std::string field(input.data() + input_cursor, count);
        char* end = nullptr;
        std::size_t consumed = 0;
        const bool is_signed = conversion.conversion == 'd' ||
                               conversion.conversion == 'i';
        int base = 10;
        if (conversion.conversion == 'i') base = 0;
        else if (conversion.conversion == 'o') base = 8;
        else if (conversion.conversion == 'x' ||
                 conversion.conversion == 'X')
            base = 16;
        const int saved_errno = errno;
        errno = 0;
        if (is_signed) {
            const long long value =
                std::strtoll(field.c_str(), &end, base);
            const int parse_errno = errno;
            errno = saved_errno;
            consumed = static_cast<std::size_t>(end - field.c_str());
            if (consumed == 0) return finish_matching_failure();
            if (parse_errno == ERANGE) {
                SetError(error,
                         "sscanf signed integer exceeds the native bound conversion=" +
                             std::to_string(current_conversion_index) +
                             " format-offset=" +
                             std::to_string(conversion_format_offset));
                return false;
            }
            if (!conversion.suppressed) {
                if (!StageSignedScanValue(
                        output_address, static_cast<std::int64_t>(value),
                        conversion.length, write_context, &writes, error))
                    return false;
                ++assignments;
            }
        } else {
            const unsigned long long value =
                std::strtoull(field.c_str(), &end, base);
            const int parse_errno = errno;
            errno = saved_errno;
            consumed = static_cast<std::size_t>(end - field.c_str());
            if (consumed == 0) return finish_matching_failure();
            if (parse_errno == ERANGE) {
                SetError(error,
                         "sscanf unsigned integer exceeds the native bound conversion=" +
                             std::to_string(current_conversion_index) +
                             " format-offset=" +
                             std::to_string(conversion_format_offset));
                return false;
            }
            if (!conversion.suppressed) {
                if (!StageUnsignedScanValue(
                        output_address, static_cast<std::uint64_t>(value),
                        conversion.length, write_context, &writes, error))
                    return false;
                ++assignments;
            }
        }
        if (!conversion.has_width && consumed == kMaximumField &&
            available > kMaximumField) {
            SetError(error, "sscanf numeric field exceeds the native bound");
            return false;
        }
        input_cursor += consumed;
        conversion_succeeded = true;
    }
    report_consumed();
    return FinishScan(memory, &writes, assignments, raw_result, error);
}

bool Scan(GuestAddressSpace* memory,
          const Aarch64ImportRegistry::Registers& registers,
          std::uint64_t* raw_result, std::string* error) {
    std::string input;
    std::string format;
    if (!ReadScanString(memory, registers[0], "input", &input, error) ||
        !ReadScanString(memory, registers[1], "format", &format, error))
        return false;
    return ScanBuffer(memory, input, format, registers, nullptr, raw_result,
                      error);
}

}  // namespace

bool ScanBionicBuffer(GuestAddressSpace* memory, const std::string& input,
                      const std::string& format,
                      const Aarch64ImportRegistry::Registers& registers,
                      std::size_t* consumed, std::uint64_t* raw_result,
                      std::string* error) {
    return ScanBuffer(memory, input, format, registers, consumed, raw_result,
                      error);
}

namespace {

}  // namespace

bool FormatBionicVaList(GuestAddressSpace* memory,
                        std::uint64_t format_address,
                        std::uint64_t list_address,
                        std::string* output, std::string* error) {
    if (output != nullptr) output->clear();
    if (memory == nullptr || output == nullptr || format_address == 0 ||
        list_address == 0) {
        SetError(error, "va_list formatting configuration is invalid");
        return false;
    }
    return Format(memory, format_address, list_address, output, error);
}

bool FormatBionicVariadicCall(
    GuestHeap* heap, GuestAddressSpace* memory,
    const Aarch64ImportRegistry::Registers& registers,
    std::size_t named_general_arguments, std::uint64_t format_address,
    std::string* output, std::string* error) {
    if (output != nullptr) output->clear();
    if (heap == nullptr || memory == nullptr || output == nullptr ||
        named_general_arguments > registers.general.size()) {
        SetError(error, "variadic call formatting configuration is invalid");
        return false;
    }
    const std::size_t general_count =
        registers.general.size() - named_general_arguments;
    const std::size_t general_bytes =
        general_count * sizeof(std::uint64_t);
    constexpr std::size_t kVectorBytes =
        8 * 2 * sizeof(std::uint64_t);
    const std::size_t vector_offset = general_bytes;
    const std::size_t list_offset = vector_offset + kVectorBytes;
    const std::size_t allocation_size = list_offset + sizeof(Aarch64VaList);
    const std::uint64_t allocation = heap->Allocate(allocation_size);
    if (allocation == 0) {
        SetError(error, "variadic call save-area allocation failed");
        return false;
    }
    Aarch64VaList list{};
    list.stack = registers.stack_pointer;
    list.general_top = allocation + general_bytes;
    list.vector_top = allocation + vector_offset + kVectorBytes;
    list.general_offset = -static_cast<std::int32_t>(general_bytes);
    list.vector_offset = -static_cast<std::int32_t>(kVectorBytes);
    bool staged =
        (general_bytes == 0 ||
         heap->Write(allocation,
                     registers.general.data() + named_general_arguments,
                     general_bytes)) &&
        heap->Write(allocation + vector_offset, registers.vector.data(),
                    kVectorBytes) &&
        heap->Write(allocation + list_offset, &list, sizeof(list));
    bool formatted = false;
    if (staged) {
        formatted = Format(memory, format_address,
                           allocation + list_offset, output, error);
    } else {
        SetError(error, "variadic call save-area staging failed");
    }
    std::string cleanup_error;
    if (!heap->Release(allocation, &cleanup_error)) {
        if (formatted || error == nullptr || error->empty())
            SetError(error, "variadic call save-area cleanup failed: " +
                                cleanup_error);
        return false;
    }
    return formatted;
}

bool RegisterBionicFormatImports(Aarch64ImportRegistry* registry,
                                  GuestHeap* heap,
                                  GuestAddressSpace* memory,
                                  std::string* error) {
    if (registry == nullptr || heap == nullptr || memory == nullptr) {
        SetError(error, "bionic format imports require a registry and memory");
        return false;
    }
    if (!registry->Register(
            "sprintf",
            [heap, memory](Aarch64ImportRegistry::Registers* registers,
                           std::string* dispatch_error) {
                std::string output;
                if (!FormatBionicVariadicCall(
                        heap, memory, *registers, 2, (*registers)[1],
                        &output, dispatch_error) ||
                    !WriteFormattedOutput(
                        memory, (*registers)[0], output.size() + 1U,
                        output, "sprintf", dispatch_error))
                    return false;
                (*registers)[0] = output.size();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "snprintf",
            [heap, memory](Aarch64ImportRegistry::Registers* registers,
                           std::string* dispatch_error) {
                std::string output;
                if (!FormatBionicVariadicCall(
                        heap, memory, *registers, 3, (*registers)[2],
                        &output, dispatch_error) ||
                    !WriteFormattedOutput(
                        memory, (*registers)[0], (*registers)[1], output,
                        "snprintf", dispatch_error))
                    return false;
                (*registers)[0] = output.size();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "printf",
            [heap, memory](Aarch64ImportRegistry::Registers* registers,
                           std::string* dispatch_error) {
                std::string output;
                if (!FormatBionicVariadicCall(
                        heap, memory, *registers, 1, (*registers)[0],
                        &output, dispatch_error))
                    return false;
                // EAAudioCore debug prints stream to the host console.
                std::fwrite(output.data(), 1, output.size(), stdout);
                (*registers)[0] = output.size();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
        "__vsnprintf_chk",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* dispatch_error) {
            // __vsnprintf_chk(dst, len, flags, src_len, fmt, ap)
            std::string output;
            if (!Format(memory, (*registers)[4], (*registers)[5], &output,
                        dispatch_error))
                return false;
            if (!WriteFormattedOutput(memory, (*registers)[0],
                                      (*registers)[1], output,
                                      "__vsnprintf_chk", dispatch_error))
                return false;
            (*registers)[0] = output.size();
            return true;
        },
        error))
        return false;
    if (!registry->Register(
        "vsnprintf",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* dispatch_error) {
            std::string output;
            if (!Format(memory, (*registers)[2], (*registers)[3], &output,
                        dispatch_error))
                return false;
            if (!WriteFormattedOutput(memory, (*registers)[0],
                                      (*registers)[1], output, "vsnprintf",
                                      dispatch_error))
                return false;
            (*registers)[0] = output.size();
            return true;
        },
        error))
        return false;
    if (!registry->Register(
            "__vsprintf_chk",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* dispatch_error) {
                std::string output;
                // Bionic's fortified ABI is (dst, flags, object_size, format,
                // va_list). The flags argument is intentionally unused.
                if (!Format(memory, (*registers)[3], (*registers)[4], &output,
                            dispatch_error) ||
                    !WriteFortifiedFormattedOutput(
                        memory, (*registers)[0], (*registers)[2], output,
                        "__vsprintf_chk", dispatch_error))
                    return false;
                (*registers)[0] = output.size();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "vasprintf",
            [heap, memory](Aarch64ImportRegistry::Registers* registers,
                           std::string* dispatch_error) {
                std::string output;
                if (!Format(memory, (*registers)[1], (*registers)[2], &output,
                            dispatch_error))
                    return false;
                const std::uint64_t buffer = heap->Allocate(output.size() + 1U);
                if (buffer == 0) {
                    std::uint64_t null_pointer = 0;
                    (void)memory->Write((*registers)[0], &null_pointer,
                                        sizeof(null_pointer));
                    (*registers)[0] =
                        std::numeric_limits<std::uint64_t>::max();
                    return true;
                }
                if (!WriteFormattedOutput(memory, buffer, output.size() + 1U,
                                          output, "vasprintf",
                                          dispatch_error))
                    return false;
                if (!memory->Write((*registers)[0], &buffer,
                                   sizeof(buffer))) {
                    SetError(dispatch_error,
                             "vasprintf could not store the guest pointer");
                    return false;
                }
                (*registers)[0] = output.size();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "asprintf",
            [heap, memory](Aarch64ImportRegistry::Registers* registers,
                           std::string* dispatch_error) {
                std::string output;
                if (!FormatBionicVariadicCall(
                        heap, memory, *registers, 2, (*registers)[1], &output,
                        dispatch_error))
                    return false;
                const std::uint64_t buffer = heap->Allocate(output.size() + 1U);
                if (buffer == 0) {
                    std::uint64_t null_pointer = 0;
                    (void)memory->Write((*registers)[0], &null_pointer,
                                        sizeof(null_pointer));
                    (*registers)[0] =
                        std::numeric_limits<std::uint64_t>::max();
                    return true;
                }
                if (!WriteFormattedOutput(memory, buffer, output.size() + 1U,
                                          output, "asprintf",
                                          dispatch_error))
                    return false;
                if (!memory->Write((*registers)[0], &buffer,
                                   sizeof(buffer))) {
                    SetError(dispatch_error,
                             "asprintf could not store the guest pointer");
                    return false;
                }
                (*registers)[0] = output.size();
                return true;
            },
            error))
        return false;
    return registry->Register(
        "sscanf",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* dispatch_error) {
            std::uint64_t result = 0;
            if (!Scan(memory, *registers, &result, dispatch_error))
                return false;
            (*registers)[0] = result;
            return true;
        },
        error);
}

}  // namespace isomr
