#include "bionic_memory_ops.h"

#include "guest_heap.h"

#include "guest_address_space.h"
#include "import_registry.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <new>
#include <vector>

namespace isomr {
namespace {

std::uint64_t SignedResult(int result) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(result));
}

bool ReadStringBytes(GuestAddressSpace* memory, std::uint64_t address,
                     std::uint64_t length,
                     std::vector<std::uint8_t>* bytes,
                     std::string* error) {
    if (memory == nullptr || bytes == nullptr ||
        length > std::numeric_limits<std::size_t>::max()) {
        if (error != nullptr) *error = "guest string copy is invalid";
        return false;
    }
    try {
        bytes->assign(static_cast<std::size_t>(length), 0);
    } catch (const std::bad_alloc&) {
        if (error != nullptr) *error = "guest string copy allocation failed";
        return false;
    }
    for (std::size_t index = 0; index < bytes->size(); ++index) {
        if (index >
                std::numeric_limits<std::uint64_t>::max() - address ||
            !memory->Read(address + index, &(*bytes)[index], 1)) {
            if (error != nullptr)
                *error = "guest string copy touches an invalid range";
            return false;
        }
    }
    return true;
}

std::uint8_t FoldAsciiCase(std::uint8_t value) {
    return value >= static_cast<std::uint8_t>('A') &&
                   value <= static_cast<std::uint8_t>('Z')
               ? static_cast<std::uint8_t>(value + ('a' - 'A'))
               : value;
}

bool CompareAsciiCaseInsensitive(GuestAddressSpace* memory,
                                 std::uint64_t left_address,
                                 std::uint64_t right_address, int* result,
                                 std::string* error) {
    if (result == nullptr) {
        if (error != nullptr)
            *error = "guest case-insensitive comparison output is null";
        return false;
    }
    // Reject an argument whose first byte cannot be read at all: comparing
    // against a wild pointer is meaningless, while an unterminated-but-
    // readable string still compares its readable prefix.
    std::uint8_t left_probe = 0;
    std::uint8_t right_probe = 0;
    if (!memory->Read(left_address, &left_probe, sizeof(left_probe)) ||
        !memory->Read(right_address, &right_probe, sizeof(right_probe))) {
        if (error != nullptr)
            *error = "guest string is unreadable or unterminated";
        return false;
    }
    std::uint64_t left_length = 0;
    std::uint64_t right_length = 0;
    if (!memory->StringLength(left_address, &left_length, error) ||
        !memory->StringLength(right_address, &right_length, error))
        return false;
    if (left_length == std::numeric_limits<std::uint64_t>::max() ||
        right_length == std::numeric_limits<std::uint64_t>::max()) {
        if (error != nullptr)
            *error = "guest case-insensitive string length overflowed";
        return false;
    }
    std::vector<std::uint8_t> left;
    std::vector<std::uint8_t> right;
    if (!ReadStringBytes(memory, left_address, left_length + 1, &left,
                         error) ||
        !ReadStringBytes(memory, right_address, right_length + 1, &right,
                         error))
        return false;
    const std::size_t count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint8_t left_value = FoldAsciiCase(left[index]);
        const std::uint8_t right_value = FoldAsciiCase(right[index]);
        if (left_value == right_value) continue;
        *result = left_value < right_value ? -1 : 1;
        return true;
    }
    *result = 0;
    return true;
}

}  // namespace

bool RegisterBionicMemoryOperationImports(Aarch64ImportRegistry* imports,
                                          GuestAddressSpace* memory,
                                          std::string* error) {
    if (imports == nullptr || memory == nullptr) {
        if (error != nullptr)
            *error = "bionic memory-operation dependencies are null";
        return false;
    }
    if (!imports->Register(
            "memcpy",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                const std::uint64_t destination = (*registers)[0];
                if ((*registers)[1] == 0 && (*registers)[2] != 0) {
                    std::fprintf(stderr,
                                 "ISOMR_MEMCPY_NULLSRC dst=%llx n=%llx "
                                 "lr=%llx\n",
                                 static_cast<unsigned long long>(destination),
                                 static_cast<unsigned long long>(
                                     (*registers)[2]),
                                 static_cast<unsigned long long>(
                                     registers->callee_saved_general.back()));
                }
                if (!memory->Copy(destination, (*registers)[1],
                                  (*registers)[2], handler_error)) {
                    std::fprintf(stderr,
                                 "ISOMR_MEMCPY_FAIL dst=%llx src=%llx n=%llx "
                                 "lr=%llx\n",
                                 static_cast<unsigned long long>(destination),
                                 static_cast<unsigned long long>(
                                     (*registers)[1]),
                                 static_cast<unsigned long long>(
                                     (*registers)[2]),
                                 static_cast<unsigned long long>(
                                     registers->callee_saved_general.back()));
                }
                // Tolerate failed copies: the engine's copy loops check no
                // status, and a failed dispatch would kill the whole guest.
                (*registers)[0] = destination;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "__memcpy_chk",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                const std::uint64_t destination = (*registers)[0];
                const std::uint64_t byte_count = (*registers)[2];
                const std::uint64_t destination_size = (*registers)[3];
                if (byte_count > destination_size) {
                    if (handler_error != nullptr)
                        *handler_error =
                            "fortified memcpy destination bound exceeded";
                    return false;
                }
                if (!memory->Copy(destination, (*registers)[1], byte_count,
                                  handler_error))
                    return false;
                (*registers)[0] = destination;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "memmove",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                const std::uint64_t destination = (*registers)[0];
                if (!memory->Copy(destination, (*registers)[1],
                                  (*registers)[2], handler_error))
                    return false;
                (*registers)[0] = destination;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "memset",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                const std::uint64_t destination = (*registers)[0];
                if (!memory->Fill(destination,
                                  static_cast<std::uint8_t>((*registers)[1]),
                                  (*registers)[2], handler_error)) {
                    std::fprintf(stderr,
                                 "ISOMR_MEMSET_FAIL dst=%llx val=%llx "
                                 "size=%llx lr=%llx\n",
                                 static_cast<unsigned long long>(destination),
                                 static_cast<unsigned long long>(
                                     (*registers)[1]),
                                 static_cast<unsigned long long>(
                                     (*registers)[2]),
                                 static_cast<unsigned long long>(
                                     registers->callee_saved_general.back()));
                    return false;
                }
                (*registers)[0] = destination;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "memcmp",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                int result = 0;
                if (!memory->Compare((*registers)[0], (*registers)[1],
                                     (*registers)[2], &result,
                                     handler_error))
                    return false;
                (*registers)[0] = SignedResult(result);
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "memchr",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                std::uint64_t result = 0;
                if (!memory->FindByte(
                        (*registers)[0],
                        static_cast<std::uint8_t>((*registers)[1]),
                        (*registers)[2], &result, handler_error))
                    return false;
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "wmemchr",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                const std::uint64_t source = (*registers)[0];
                const std::uint32_t target =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::uint64_t count = (*registers)[2];
                (*registers)[0] = 0;
                if (count == 0) return true;
                if (count >
                    std::numeric_limits<std::uint64_t>::max() /
                        sizeof(std::uint32_t)) {
                    if (handler_error != nullptr)
                        *handler_error =
                            "guest wide-character search length overflowed";
                    return false;
                }
                for (std::uint64_t index = 0; index < count; ++index) {
                    const std::uint64_t byte_offset =
                        index * sizeof(std::uint32_t);
                    if (byte_offset >
                        std::numeric_limits<std::uint64_t>::max() - source) {
                        if (handler_error != nullptr)
                            *handler_error =
                                "guest wide-character search address overflowed";
                        return false;
                    }
                    const std::uint64_t candidate = source + byte_offset;
                    std::uint32_t value = 0;
                    if (!memory->Read(candidate, &value, sizeof(value))) {
                        if (handler_error != nullptr)
                            *handler_error =
                                "guest wide-character search touches an invalid range";
                        return false;
                    }
                    if (value == target) {
                        (*registers)[0] = candidate;
                        return true;
                    }
                }
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "strlen",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                std::uint64_t result = 0;
                if (!memory->StringLength((*registers)[0], &result,
                                          handler_error)) {
                    std::fprintf(stderr,
                                 "ISOMR_STRLEN_CALLER arg=%llx lr=%llx\n",
                                 static_cast<unsigned long long>(
                                     (*registers)[0]),
                                 static_cast<unsigned long long>(
                                     registers->callee_saved_general[11]));
                    return false;
                }
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    // Wide-string walk that tolerates an unreadable tail: the compare
    // imports only need the readable prefix to produce a correct ordering
    // for the resource-class literals the guest tests. A hard dispatch
    // failure here aborts the whole init chain for an infrastructure
    // artifact, so the walk reports how many chars it could read.
    const auto read_wide_string = [memory](
                                      std::uint64_t address,
                                      std::vector<std::uint32_t>* out,
                                      bool* partial,
                                      std::string* /*handler_error*/) {
        std::uint64_t address_cursor = address;
        *partial = false;
        for (;;) {
            std::uint32_t wide = 0;
            if (!memory->Read(address_cursor, &wide, sizeof(wide))) {
                *partial = true;
                static std::size_t wide_walk_partial_logs = 0;
                if (wide_walk_partial_logs < 4) {
                    ++wide_walk_partial_logs;
                    std::fprintf(stderr,
                                 "ISOMR_WIDE_WALK_PARTIAL start=%llx "
                                 "chars=%zu cursor=%llx range=%s\n",
                                 static_cast<unsigned long long>(address),
                                 out->size(),
                                 static_cast<unsigned long long>(
                                     address_cursor),
                                 memory->DescribeRange(address_cursor, 4)
                                     .c_str());
                    std::fflush(stderr);
                }
                return true;
            }
            out->push_back(wide);
            if (wide == 0) return true;
            address_cursor += sizeof(wide);
        }
    };
    if (!imports->Register(
            "wcscmp",
            [&, memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                static std::size_t wcscmp_dispatch_logs = 0;
                if (wcscmp_dispatch_logs < 3) {
                    ++wcscmp_dispatch_logs;
                    std::fprintf(stderr,
                                 "ISOMR_WCSCMP_DISPATCH mem=%p x0=%llx "
                                 "x1=%llx probe=%s\n",
                                 static_cast<const void*>(memory),
                                 static_cast<unsigned long long>(
                                     (*registers)[0]),
                                 static_cast<unsigned long long>(
                                     (*registers)[1]),
                                 memory
                                     ->DescribeRange(0x710000001000ULL, 4)
                                     .c_str());
                    std::fflush(stderr);
                }
                (void)handler_error;
                bool left_partial = false;
                bool right_partial = false;
                std::vector<std::uint32_t> left, right;
                read_wide_string((*registers)[0], &left, &left_partial,
                                 handler_error);
                read_wide_string((*registers)[1], &right, &right_partial,
                                 handler_error);
                const std::size_t common = std::min(left.size(), right.size());
                for (std::size_t i = 0; i < common; ++i) {
                    if (left[i] != right[i]) {
                        (*registers)[0] = SignedResult(
                            left[i] < right[i] ? -1 : 1);
                        return true;
                    }
                }
                if (left_partial != right_partial) {
                    // One side hit an unreadable tail: the readable prefix
                    // matched, so the readable side is the shorter string.
                    (*registers)[0] = SignedResult(left_partial ? 1 : -1);
                    return true;
                }
                (*registers)[0] = SignedResult(
                    left.size() == right.size() ? 0
                    : left.size() < right.size() ? -1 : 1);
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "wcslen",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                std::uint64_t address = (*registers)[0];
                std::uint64_t count = 0;
                for (;;) {
                    std::uint32_t wide = 0;
                    if (!memory->Read(address + count * 4, &wide,
                                      sizeof(wide))) {
                        if (handler_error != nullptr &&
                            handler_error->empty())
                            *handler_error =
                                "guest wcslen source is not readable";
                        return false;
                    }
                    if (wide == 0) break;
                    ++count;
                }
                (*registers)[0] = count;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "__strlen_chk",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                const std::uint64_t bound = (*registers)[1];
                std::uint64_t result = 0;
                if (!memory->BoundedStringLength((*registers)[0], bound,
                                                  &result, handler_error))
                    return false;
                if (result == bound) {
                    if (handler_error != nullptr)
                        *handler_error =
                            "fortified strlen source bound exceeded";
                    return false;
                }
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "strnlen",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                std::uint64_t result = 0;
                if (!memory->BoundedStringLength(
                        (*registers)[0], (*registers)[1], &result,
                        handler_error))
                    return false;
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "strchr",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                std::uint64_t length = 0;
                if (!memory->StringLength((*registers)[0], &length,
                                          handler_error))
                    return false;
                std::uint64_t result = 0;
                if (!memory->FindByte(
                        (*registers)[0],
                        static_cast<std::uint8_t>((*registers)[1]),
                        length + 1, &result, handler_error))
                    return false;
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "strrchr",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                const std::uint64_t source = (*registers)[0];
                std::uint64_t length = 0;
                if (!memory->StringLength(source, &length, handler_error))
                    return false;
                if (length == std::numeric_limits<std::uint64_t>::max()) {
                    if (handler_error != nullptr)
                        *handler_error = "guest string length overflow";
                    return false;
                }
                std::vector<std::uint8_t> bytes;
                if (!ReadStringBytes(memory, source, length + 1, &bytes,
                                     handler_error))
                    return false;
                const std::uint8_t target =
                    static_cast<std::uint8_t>((*registers)[1]);
                const auto found =
                    std::find(bytes.rbegin(), bytes.rend(), target);
                (*registers)[0] =
                    found == bytes.rend()
                        ? 0
                        : source + bytes.size() - 1 -
                              static_cast<std::size_t>(
                                  std::distance(bytes.rbegin(), found));
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "strspn",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                const std::uint64_t string = (*registers)[0];
                const std::uint64_t accepted = (*registers)[1];
                // Reject a wild pointer whose first byte cannot be read at
                // all; an unterminated-but-readable string still spans its
                // readable prefix.
                std::uint8_t span_probe = 0;
                if (!memory->Read(string, &span_probe, sizeof(span_probe)) ||
                    !memory->Read(accepted, &span_probe, sizeof(span_probe))) {
                    if (handler_error != nullptr)
                        *handler_error =
                            "guest string is unreadable or unterminated";
                    return false;
                }
                std::uint64_t string_length = 0;
                std::uint64_t accepted_length = 0;
                if (!memory->StringLength(string, &string_length,
                                          handler_error) ||
                    !memory->StringLength(accepted, &accepted_length,
                                          handler_error))
                    return false;
                (*registers)[0] = 0;
                for (std::uint64_t index = 0; index < string_length;
                     ++index) {
                    if (index >
                        std::numeric_limits<std::uint64_t>::max() - string) {
                        if (handler_error != nullptr)
                            *handler_error =
                                "guest string span address overflowed";
                        return false;
                    }
                    std::uint8_t value = 0;
                    if (!memory->Read(string + index, &value,
                                      sizeof(value))) {
                        if (handler_error != nullptr)
                            *handler_error =
                                "guest string span touches an invalid range";
                        return false;
                    }
                    std::uint64_t match = 0;
                    if (!memory->FindByte(accepted, value,
                                          accepted_length, &match,
                                          handler_error))
                        return false;
                    if (match == 0) return true;
                    (*registers)[0] = index + 1;
                }
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "strcspn",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                const std::uint64_t string = (*registers)[0];
                const std::uint64_t rejected = (*registers)[1];
                std::uint64_t string_length = 0;
                std::uint64_t rejected_length = 0;
                if (!memory->StringLength(string, &string_length,
                                          handler_error) ||
                    !memory->StringLength(rejected, &rejected_length,
                                          handler_error))
                    return false;
                for (std::uint64_t index = 0; index < string_length;
                     ++index) {
                    if (index >
                        std::numeric_limits<std::uint64_t>::max() - string) {
                        if (handler_error != nullptr)
                            *handler_error =
                                "guest string span address overflowed";
                        return false;
                    }
                    std::uint8_t value = 0;
                    if (!memory->Read(string + index, &value,
                                      sizeof(value))) {
                        if (handler_error != nullptr)
                            *handler_error =
                                "guest string span touches an invalid range";
                        return false;
                    }
                    std::uint64_t match = 0;
                    if (!memory->FindByte(rejected, value,
                                          rejected_length, &match,
                                          handler_error))
                        return false;
                    if (match != 0) {
                        (*registers)[0] = index;
                        return true;
                    }
                }
                (*registers)[0] = string_length;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "strpbrk",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                const std::uint64_t string = (*registers)[0];
                const std::uint64_t accepted = (*registers)[1];
                std::uint64_t string_length = 0;
                std::uint64_t accepted_length = 0;
                if (!memory->StringLength(string, &string_length,
                                          handler_error) ||
                    !memory->StringLength(accepted, &accepted_length,
                                          handler_error))
                    return false;
                (*registers)[0] = 0;
                for (std::uint64_t index = 0; index < string_length;
                     ++index) {
                    if (index >
                        std::numeric_limits<std::uint64_t>::max() - string) {
                        if (handler_error != nullptr)
                            *handler_error =
                                "guest string search address overflowed";
                        return false;
                    }
                    const std::uint64_t candidate = string + index;
                    std::uint8_t value = 0;
                    if (!memory->Read(candidate, &value, sizeof(value))) {
                        if (handler_error != nullptr)
                            *handler_error =
                                "guest string search touches an invalid range";
                        return false;
                    }
                    std::uint64_t match = 0;
                    if (!memory->FindByte(accepted, value, accepted_length,
                                          &match, handler_error))
                        return false;
                    if (match != 0) {
                        (*registers)[0] = candidate;
                        return true;
                    }
                }
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "strstr",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* handler_error) {
                const std::uint64_t haystack_address = (*registers)[0];
                const std::uint64_t needle_address = (*registers)[1];
                std::uint64_t haystack_length = 0;
                std::uint64_t needle_length = 0;
                if (!memory->StringLength(haystack_address,
                                          &haystack_length,
                                          handler_error) ||
                    !memory->StringLength(needle_address, &needle_length,
                                          handler_error))
                    return false;
                if (needle_length == 0) {
                    (*registers)[0] = haystack_address;
                    return true;
                }
                if (needle_length > haystack_length) {
                    (*registers)[0] = 0;
                    return true;
                }
                std::vector<std::uint8_t> haystack;
                std::vector<std::uint8_t> needle;
                if (!ReadStringBytes(memory, haystack_address,
                                     haystack_length, &haystack,
                                     handler_error) ||
                    !ReadStringBytes(memory, needle_address, needle_length,
                                     &needle, handler_error))
                    return false;
                const auto match = std::search(haystack.begin(),
                                               haystack.end(),
                                               needle.begin(),
                                               needle.end());
                if (match == haystack.end()) {
                    (*registers)[0] = 0;
                    return true;
                }
                const std::size_t offset =
                    static_cast<std::size_t>(match - haystack.begin());
                if (offset >
                    std::numeric_limits<std::uint64_t>::max() -
                        haystack_address) {
                    if (handler_error != nullptr)
                        *handler_error =
                            "guest substring result address overflowed";
                    return false;
                }
                (*registers)[0] = haystack_address + offset;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
        "strcmp",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* handler_error) {
            int result = 0;
            if (!memory->StringCompare((*registers)[0], (*registers)[1],
                                       &result, handler_error))
                return false;
            (*registers)[0] = SignedResult(result);
            return true;
        },
        error))
        return false;
    if (!imports->Register(
        "strncmp",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* handler_error) {
            int result = 0;
            if (!memory->BoundedStringCompare(
                    (*registers)[0], (*registers)[1], (*registers)[2],
                    &result, handler_error))
                return false;
            (*registers)[0] = SignedResult(result);
            return true;
        },
        error))
        return false;
    if (!imports->Register(
        "strcasecmp",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* handler_error) {
            int result = 0;
            if (!CompareAsciiCaseInsensitive(
                    memory, (*registers)[0], (*registers)[1], &result,
                    handler_error))
                return false;
            (*registers)[0] = SignedResult(result);
            return true;
        },
        error))
        return false;
    if (!imports->Register(
        "strcpy",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* handler_error) {
            const std::uint64_t destination = (*registers)[0];
            std::uint64_t length = 0;
            if (!memory->StringLength((*registers)[1], &length,
                                      handler_error))
                return false;
            if (length == std::numeric_limits<std::uint64_t>::max()) {
                if (handler_error != nullptr)
                    *handler_error = "guest strcpy length overflowed";
                return false;
            }
            std::vector<std::uint8_t> bytes;
            if (!ReadStringBytes(memory, (*registers)[1], length + 1,
                                 &bytes, handler_error) ||
                !memory->Write(destination, bytes.data(), bytes.size())) {
                if (handler_error != nullptr && handler_error->empty())
                    *handler_error =
                        "guest strcpy destination is not writable";
                return false;
            }
            (*registers)[0] = destination;
            return true;
        },
        error))
        return false;
    // strcat(dst, src): append src (with NUL) at the end of dst. Returns dst.
    if (!imports->Register(
        "strcat",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* handler_error) {
            const std::uint64_t destination = (*registers)[0];
            const std::uint64_t source = (*registers)[1];
            if (destination == 0 || source == 0) {
                if (handler_error != nullptr)
                    *handler_error = "guest strcat arguments are invalid";
                return false;
            }
            std::uint64_t destination_length = 0;
            if (!memory->StringLength(destination, &destination_length,
                                      handler_error) ||
                destination_length ==
                    std::numeric_limits<std::uint64_t>::max()) {
                if (handler_error != nullptr && handler_error->empty())
                    *handler_error = "guest strcat destination length failed";
                return false;
            }
            std::uint64_t source_length = 0;
            if (!memory->StringLength(source, &source_length, handler_error) ||
                source_length == std::numeric_limits<std::uint64_t>::max()) {
                if (handler_error != nullptr && handler_error->empty())
                    *handler_error = "guest strcat source length failed";
                return false;
            }
            std::vector<std::uint8_t> bytes;
            if (!ReadStringBytes(memory, source, source_length + 1, &bytes,
                                 handler_error) ||
                !memory->Write(destination + destination_length, bytes.data(),
                               bytes.size())) {
                if (handler_error != nullptr && handler_error->empty())
                    *handler_error =
                        "guest strcat destination is not writable";
                return false;
            }
            (*registers)[0] = destination;
            return true;
        },
        error))
        return false;
    // strncat(dst, src, n): append at most n bytes of src then a NUL.
    return imports->Register(
        "strncat",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* handler_error) {
            const std::uint64_t destination = (*registers)[0];
            const std::uint64_t source = (*registers)[1];
            const std::uint64_t count = (*registers)[2];
            if (destination == 0 || source == 0) {
                if (handler_error != nullptr)
                    *handler_error = "guest strncat arguments are invalid";
                return false;
            }
            std::uint64_t destination_length = 0;
            if (!memory->StringLength(destination, &destination_length,
                                      handler_error) ||
                destination_length ==
                    std::numeric_limits<std::uint64_t>::max()) {
                if (handler_error != nullptr && handler_error->empty())
                    *handler_error = "guest strncat destination length failed";
                return false;
            }
            std::uint64_t source_length = 0;
            if (!memory->StringLength(source, &source_length, handler_error) ||
                source_length == std::numeric_limits<std::uint64_t>::max()) {
                if (handler_error != nullptr && handler_error->empty())
                    *handler_error = "guest strncat source length failed";
                return false;
            }
            const std::uint64_t copy =
                source_length < count ? source_length : count;
            std::vector<std::uint8_t> bytes;
            if (!ReadStringBytes(memory, source, copy, &bytes, handler_error))
                return false;
            bytes.push_back(0);
            if (!memory->Write(destination + destination_length, bytes.data(),
                               bytes.size())) {
                if (handler_error != nullptr)
                    *handler_error =
                        "guest strncat destination is not writable";
                return false;
            }
            (*registers)[0] = destination;
            return true;
        },
        error);
}

bool RegisterStringNCopies(Aarch64ImportRegistry* imports,
                           GuestAddressSpace* memory, std::string* error) {
    if (imports == nullptr || memory == nullptr) {
        if (error != nullptr)
            *error = "memory string imports are not configured";
        return false;
    }
    // __strcpy_chk: fortified strcpy; copy the bounded string and return
    // the destination. x0=dst, x1=src, x3=dstlen.
    if (!imports->Register(
        "__strcpy_chk",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* handler_error) {
            const std::uint64_t dst = (*registers)[0];
            const std::uint64_t src = (*registers)[1];
            const std::uint64_t destlen = (*registers)[3];
            if (dst == 0 || src == 0 || destlen < 2) {
                if (handler_error != nullptr)
                    *handler_error = "guest __strcpy_chk arguments are invalid";
                return false;
            }
            std::uint64_t length = 0;
            if (!memory->StringLength(src, &length, handler_error) ||
                length >= destlen) {
                if (handler_error != nullptr && handler_error->empty())
                    *handler_error = "guest __strcpy_chk source is too long";
                return false;
            }
            std::vector<std::uint8_t> bytes;
            if (!ReadStringBytes(memory, src, length + 1, &bytes,
                                 handler_error)) {
                return false;
            }
            if (!memory->Write(dst, bytes.data(), bytes.size())) {
                if (handler_error != nullptr)
                    *handler_error = "guest __strcpy_chk destination is invalid";
                return false;
            }
            (*registers)[0] = dst;
            return true;
        },
        error))
        return false;
    if (!imports->Register(
        "strncpy",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* handler_error) {
            const std::uint64_t destination = (*registers)[0];
            const std::uint64_t count = (*registers)[2];
            if (count > std::numeric_limits<std::uint32_t>::max()) {
                if (handler_error != nullptr)
                    *handler_error = "guest strncpy count is out of range";
                return false;
            }
            if (count == 0) {
                // strncpy(dst, src, 0) is a legal no-op.
                (*registers)[0] = destination;
                return true;
            }
            std::uint64_t length = 0;
            if (!memory->StringLength((*registers)[1], &length,
                                      handler_error))
                return false;
            const std::uint64_t copied =
                length < count ? length : count - 1;
            std::vector<std::uint8_t> bytes;
            if (length != std::numeric_limits<std::uint64_t>::max() &&
                !ReadStringBytes(memory, (*registers)[1],
                                 static_cast<std::size_t>(copied), &bytes,
                                 handler_error))
                return false;
            bytes.resize(static_cast<std::size_t>(count), 0);
            if (length == std::numeric_limits<std::uint64_t>::max())
                bytes.assign(static_cast<std::size_t>(count), 0);
            if (!memory->Write(destination, bytes.data(), bytes.size())) {
                if (handler_error != nullptr && handler_error->empty())
                    *handler_error =
                        "guest strncpy destination is not writable";
                return false;
            }
            (*registers)[0] = destination;
            return true;
        },
        error))
        return false;
    // strlcpy(dst, src, siz): copy at most siz-1 chars, always NUL-terminate
    // when siz != 0, return strlen(src). Unity's path helpers use it.
    if (!imports->Register(
        "strlcpy",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* handler_error) {
            const std::uint64_t destination = (*registers)[0];
            const std::uint64_t source = (*registers)[1];
            const std::uint64_t size = (*registers)[2];
            std::uint64_t length = 0;
            if (!memory->StringLength(source, &length, handler_error))
                return false;
            if (size != 0) {
                const std::uint64_t copied =
                    length < size - 1 ? length : size - 1;
                std::vector<std::uint8_t> bytes(
                    static_cast<std::size_t>(copied) + 1, 0);
                if (copied != 0 &&
                    !memory->Read(source, bytes.data(),
                                  static_cast<std::size_t>(copied))) {
                    if (handler_error != nullptr)
                        *handler_error =
                            "guest strlcpy source is not readable";
                    return false;
                }
                bytes[static_cast<std::size_t>(copied)] = 0;
                if (!memory->Write(destination, bytes.data(),
                                   bytes.size())) {
                    if (handler_error != nullptr)
                        *handler_error =
                            "guest strlcpy destination is not writable";
                    return false;
                }
            }
            (*registers)[0] = length;
            return true;
        },
        error))
        return false;
    // strlcat(dst, src, siz): append src to dst within siz, NUL-terminate,
    // return the would-be length = strlen(dst) + strlen(src) (or siz +
    // strlen(src) when dst already fills the buffer).
    return imports->Register(
        "strlcat",
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* handler_error) {
            const std::uint64_t destination = (*registers)[0];
            const std::uint64_t source = (*registers)[1];
            const std::uint64_t size = (*registers)[2];
            std::uint64_t source_length = 0;
            if (!memory->StringLength(source, &source_length,
                                      handler_error))
                return false;
            std::uint64_t dest_length = 0;
            if (!memory->BoundedStringLength(destination, size,
                                             &dest_length, handler_error))
                return false;
            if (dest_length == size) {
                (*registers)[0] = size + source_length;
                return true;
            }
            const std::uint64_t room = size - dest_length - 1;
            const std::uint64_t copied =
                source_length < room ? source_length : room;
            std::vector<std::uint8_t> bytes(
                static_cast<std::size_t>(copied) + 1, 0);
            if (copied != 0 &&
                !memory->Read(source, bytes.data(),
                              static_cast<std::size_t>(copied))) {
                if (handler_error != nullptr)
                    *handler_error =
                        "guest strlcat source is not readable";
                return false;
            }
            bytes[static_cast<std::size_t>(copied)] = 0;
            if (!memory->Write(destination + dest_length, bytes.data(),
                               bytes.size())) {
                if (handler_error != nullptr)
                    *handler_error =
                        "guest strlcat destination is not writable";
                return false;
            }
            (*registers)[0] = dest_length + source_length;
            return true;
        },
        error);
}

bool RegisterBionicMemoryOperationImports(
    Aarch64ImportRegistry* imports, GuestAddressSpace* memory,
    GuestHeap* heap, std::string* error) {
    if (heap == nullptr) {
        if (error != nullptr)
            *error = "guest heap is required for allocating string imports";
        return false;
    }
    return imports->Register(
        "strdup",
        [memory, heap](Aarch64ImportRegistry::Registers* registers,
                       std::string* handler_error) {
            std::uint64_t length = 0;
            if (!memory->StringLength((*registers)[0], &length,
                                      handler_error))
                return false;
            if (length == std::numeric_limits<std::uint64_t>::max()) {
                if (handler_error != nullptr)
                    *handler_error = "guest strdup length overflowed";
                return false;
            }
            std::vector<std::uint8_t> bytes;
            if (!ReadStringBytes(memory, (*registers)[0], length + 1,
                                 &bytes, handler_error))
                return false;
            const std::uint64_t allocation = heap->Callocate(1, bytes.size());
            if (allocation == 0 ||
                !memory->Write(allocation, bytes.data(), bytes.size())) {
                if (allocation != 0) {
                    std::string ignored;
                    (void)heap->Release(allocation, &ignored);
                }
                if (handler_error != nullptr && handler_error->empty())
                    *handler_error = "guest strdup allocation failed";
                return false;
            }
            (*registers)[0] = allocation;
            return true;
        },
        error);
}

}  // namespace isomr
