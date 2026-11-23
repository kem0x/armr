#include "bionic_numbers.h"

#include "bionic_locales.h"
#include "bionic_thread_state.h"
#include "guest_address_space.h"
#include "import_registry.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

namespace isomr {
namespace {

constexpr std::size_t kMaximumNumberText = 4096;
constexpr int kInvalidArgument = 22;
constexpr int kBadAddress = 14;
constexpr int kRange = 34;

bool ReadText(const GuestAddressSpace& address_space,
              std::uint64_t address, std::string* text) {
    if (address == 0 || text == nullptr) return false;
    text->clear();
    for (std::size_t index = 0; index < kMaximumNumberText; ++index) {
        char value = 0;
        if (!address_space.Read(address + index, &value, sizeof(value)))
            return false;
        if (value == '\0') return true;
        text->push_back(value);
    }
    return false;
}

bool WriteEnd(GuestAddressSpace* address_space, std::uint64_t end_output,
              std::uint64_t input, const char* begin, const char* end) {
    if (end_output == 0) return true;
    const std::uint64_t guest_end =
        input + static_cast<std::uint64_t>(end - begin);
    return address_space->Write(end_output, &guest_end, sizeof(guest_end));
}

bool WriteEndWide(GuestAddressSpace* address_space, std::uint64_t end_output,
                  std::uint64_t input, const wchar_t* begin,
                  const wchar_t* end) {
    if (end_output == 0) return true;
    const std::uint64_t guest_end =
        input + static_cast<std::uint64_t>(end - begin) * 4;
    return address_space->Write(end_output, &guest_end, sizeof(guest_end));
}

void SetVectorDouble(Aarch64ImportRegistry::Registers* registers,
                     double value) {
    registers->vector[0] = {};
    std::memcpy(registers->vector[0].data(), &value, sizeof(value));
}

void SetVectorFloat(Aarch64ImportRegistry::Registers* registers,
                    float value) {
    registers->vector[0] = {};
    std::memcpy(registers->vector[0].data(), &value, sizeof(value));
}

}  // namespace

bool RegisterBionicNumberImports(Aarch64ImportRegistry* registry,
                                 GuestAddressSpace* address_space,
                                 BionicThreadState* threads,
                                 BionicLocales* locales,
                                 std::string* error) {
    if (registry == nullptr || address_space == nullptr || threads == nullptr ||
        locales == nullptr)
        return false;
    const auto read_failure = [threads](std::string* dispatch_error) {
        return threads->SetCurrentErrno(kBadAddress, dispatch_error);
    };
    const auto register_signed =
        [registry, address_space, threads, locales, read_failure](
            const char* name, bool locale_aware, std::string* out_error) {
            return registry->Register(
                name,
                [address_space, threads, locales, read_failure,
                 locale_aware](
                    Aarch64ImportRegistry::Registers* registers,
                    std::string* dispatch_error) {
                    if (locale_aware &&
                        !locales->IsLocale((*registers)[3])) {
                        (void)threads->SetCurrentErrno(kInvalidArgument,
                                                       dispatch_error);
                        (*registers)[0] = 0;
                        return true;
                    }
                    const int base = static_cast<int>((*registers)[2]);
                    if (base != 0 && (base < 2 || base > 36)) {
                        (void)threads->SetCurrentErrno(kInvalidArgument,
                                                       dispatch_error);
                        (*registers)[0] = 0;
                        return true;
                    }
                    const std::uint64_t input = (*registers)[0];
                    std::string text;
                    if (!ReadText(*address_space, input, &text)) {
                        (void)read_failure(dispatch_error);
                        (*registers)[0] = 0;
                        return true;
                    }
                    errno = 0;
                    char* end = nullptr;
                    const long long value =
                        std::strtoll(text.c_str(), &end, base);
                    if (!WriteEnd(address_space, (*registers)[1], input,
                                  text.c_str(), end)) {
                        (void)read_failure(dispatch_error);
                        (*registers)[0] = 0;
                        return true;
                    }
                    if (errno == ERANGE)
                        (void)threads->SetCurrentErrno(kRange,
                                                       dispatch_error);
                    (*registers)[0] = static_cast<std::uint64_t>(value);
                    return true;
                },
                out_error);
        };
    const auto register_unsigned =
        [registry, address_space, threads, read_failure](const char* name,
                                                        std::string* out_error) {
            return registry->Register(
                name,
                [address_space, threads, read_failure](
                    Aarch64ImportRegistry::Registers* registers,
                    std::string* dispatch_error) {
                    const int base = static_cast<int>((*registers)[2]);
                    if (base != 0 && (base < 2 || base > 36)) {
                        (void)threads->SetCurrentErrno(kInvalidArgument,
                                                       dispatch_error);
                        (*registers)[0] = 0;
                        return true;
                    }
                    const std::uint64_t input = (*registers)[0];
                    std::string text;
                    if (!ReadText(*address_space, input, &text)) {
                        (void)read_failure(dispatch_error);
                        (*registers)[0] = 0;
                        return true;
                    }
                    errno = 0;
                    char* end = nullptr;
                    const unsigned long long value =
                        std::strtoull(text.c_str(), &end, base);
                    if (!WriteEnd(address_space, (*registers)[1], input,
                                  text.c_str(), end)) {
                        (void)read_failure(dispatch_error);
                        (*registers)[0] = 0;
                        return true;
                    }
                    if (errno == ERANGE)
                        (void)threads->SetCurrentErrno(kRange,
                                                       dispatch_error);
                    (*registers)[0] = static_cast<std::uint64_t>(value);
                    return true;
                },
                out_error);
        };
    const auto register_float =
        [registry, address_space, threads, read_failure](const char* name,
                                                        bool single,
                                                        std::string* out_error) {
            return registry->Register(
                name,
                [address_space, threads, read_failure, single](
                    Aarch64ImportRegistry::Registers* registers,
                    std::string* dispatch_error) {
                    const std::uint64_t input = (*registers)[0];
                    std::string text;
                    if (!ReadText(*address_space, input, &text)) {
                        (void)read_failure(dispatch_error);
                        if (single) SetVectorFloat(registers, 0);
                        else SetVectorDouble(registers, 0);
                        return true;
                    }
                    errno = 0;
                    char* end = nullptr;
                    if (single) {
                        const float value = std::strtof(text.c_str(), &end);
                        SetVectorFloat(registers, value);
                    } else {
                        const double value = std::strtod(text.c_str(), &end);
                        SetVectorDouble(registers, value);
                    }
                    if (!WriteEnd(address_space, (*registers)[1], input,
                                  text.c_str(), end)) {
                        (void)read_failure(dispatch_error);
                        return true;
                    }
                    if (errno == ERANGE)
                        (void)threads->SetCurrentErrno(kRange,
                                                       dispatch_error);
                    return true;
                },
                out_error);
        };
    if (!register_signed("strtol", false, error) ||
        !register_signed("strtoll", false, error) ||
        !register_signed("strtoll_l", true, error) ||
        !register_unsigned("strtoul", error) ||
        !register_unsigned("strtoull", error) ||
        !register_float("strtod", false, error) ||
        !register_float("strtof", true, error))
        return false;
    // Wide-char variants: the guest string is UTF-32 (4-byte wchar_t),
    // which matches both Android's wchar_t and this host's.
    const auto register_wide_signed =
        [registry, address_space, threads, read_failure](const char* name,
                                                         std::string* out_error) {
            return registry->Register(
                name,
                [address_space, threads, read_failure](
                    Aarch64ImportRegistry::Registers* registers,
                    std::string* dispatch_error) {
                    const int base = static_cast<int>((*registers)[2]);
                    if (base != 0 && (base < 2 || base > 36)) {
                        (void)threads->SetCurrentErrno(kInvalidArgument,
                                                       dispatch_error);
                        (*registers)[0] = 0;
                        return true;
                    }
                    const std::uint64_t input = (*registers)[0];
                    std::wstring text;
                    bool readable = false;
                    for (std::size_t index = 0;
                         index < kMaximumNumberText; ++index) {
                        std::uint32_t wide = 0;
                        if (!address_space->Read(input + index * 4, &wide,
                                                 sizeof(wide)))
                            break;
                        if (wide == 0) {
                            readable = true;
                            break;
                        }
                        text.push_back(static_cast<wchar_t>(wide));
                    }
                    if (!readable) {
                        (void)read_failure(dispatch_error);
                        (*registers)[0] = 0;
                        return true;
                    }
                    errno = 0;
                    wchar_t* end = nullptr;
                    const long long value =
                        std::wcstol(text.c_str(), &end, base);
                    if (!WriteEndWide(address_space, (*registers)[1], input,
                                      text.c_str(), end)) {
                        (void)read_failure(dispatch_error);
                        (*registers)[0] = 0;
                        return true;
                    }
                    if (errno == ERANGE)
                        (void)threads->SetCurrentErrno(kRange,
                                                       dispatch_error);
                    (*registers)[0] = static_cast<std::uint64_t>(value);
                    return true;
                },
                out_error);
        };
    if (!register_wide_signed("wcstol", error) ||
        !register_wide_signed("wcstoll", error))
        return false;
    // Wide ctype predicates: the render path's wide-string parsing calls
    // iswspace; without it the worker thread dies mid-init and the main
    // thread faults on the half-built state.
    if (!registry->Register(
            "iswspace",
            [address_space](Aarch64ImportRegistry::Registers* registers,
                            std::string*) {
                const std::uint64_t wide = (*registers)[0] & 0xFFFFFFFFULL;
                (*registers)[0] =
                    (wide == L' ' || wide == L'\t' || wide == L'\n' ||
                     wide == L'\v' || wide == L'\f' || wide == L'\r')
                        ? 1
                        : 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "atoi",
            [address_space](Aarch64ImportRegistry::Registers* registers,
                            std::string*) {
                std::string text;
                if (!ReadText(*address_space, (*registers)[0], &text)) {
                    (*registers)[0] = 0;
                    return true;
                }
                (*registers)[0] = static_cast<std::uint32_t>(
                    std::strtol(text.c_str(), nullptr, 10));
                return true;
            },
            error))
        return false;
    for (const char* name : {"atol", "atoll"}) {
        if (!registry->Register(
                name,
                [address_space](Aarch64ImportRegistry::Registers* registers,
                                std::string*) {
                    std::string text;
                    if (!ReadText(*address_space, (*registers)[0], &text)) {
                        (*registers)[0] = 0;
                        return true;
                    }
                    (*registers)[0] = static_cast<std::uint64_t>(
                        std::strtoll(text.c_str(), nullptr, 10));
                    return true;
                },
                error))
            return false;
    }
    // lldiv/ldiv/imaxdiv return their {quot, rem} pair in x0:x1; div packs
    // int quot:rem into x0. Divide-by-zero yields zeros rather than trapping.
    for (const char* name : {"lldiv", "ldiv", "imaxdiv"}) {
        if (!registry->Register(
                name,
                [](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                    const std::int64_t numerator =
                        static_cast<std::int64_t>((*registers)[0]);
                    const std::int64_t denominator =
                        static_cast<std::int64_t>((*registers)[1]);
                    if (denominator == 0) {
                        (*registers)[0] = 0;
                        (*registers)[1] = 0;
                        return true;
                    }
                    (*registers)[0] = static_cast<std::uint64_t>(
                        numerator / denominator);
                    (*registers)[1] = static_cast<std::uint64_t>(
                        numerator % denominator);
                    return true;
                },
                error))
            return false;
    }
    if (!registry->Register(
            "div",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                const std::int32_t numerator =
                    static_cast<std::int32_t>((*registers)[0]);
                const std::int32_t denominator =
                    static_cast<std::int32_t>((*registers)[1]);
                if (denominator == 0) {
                    (*registers)[0] = 0;
                    return true;
                }
                const std::uint32_t quotient =
                    static_cast<std::uint32_t>(numerator / denominator);
                const std::uint32_t remainder =
                    static_cast<std::uint32_t>(numerator % denominator);
                (*registers)[0] =
                    (static_cast<std::uint64_t>(remainder) << 32) | quotient;
                return true;
            },
            error))
        return false;
    return registry->Register(
        "atof",
        [address_space](Aarch64ImportRegistry::Registers* registers,
                        std::string*) {
            std::string text;
            const double value =
                ReadText(*address_space, (*registers)[0], &text)
                    ? std::strtod(text.c_str(), nullptr)
                    : 0;
            SetVectorDouble(registers, value);
            return true;
        },
        error);
}

}  // namespace isomr
