#include "bionic_locales.h"

#include "bionic_thread_state.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "import_registry.h"

#include <array>
#include <utility>

namespace isomr {
namespace {

constexpr std::uint64_t kAllCategoryMask = 8127;
constexpr std::uint64_t kGlobalLocale = UINT64_MAX;
constexpr std::size_t kMaximumLocaleName = 128;
constexpr int kInvalidArgument = 22;
constexpr int kBadAddress = 14;
constexpr int kOutOfMemory = 12;
constexpr int kIllegalSequence = 84;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

// Decodes one UTF-8 sequence at cursor; updates cursor past it. Returns
// false on a truncated/invalid sequence (errno EILSEQ semantics).
bool DecodeUtf8At(GuestAddressSpace* space, std::uint64_t* cursor,
                  std::uint32_t* codepoint) {
    std::uint8_t lead = 0;
    if (!space->Read(*cursor, &lead, sizeof(lead))) return false;
    if (lead == 0) return true;  // caller handles the terminator
    std::size_t count = 1;
    std::uint32_t value = lead;
    if (lead >= 0xf0 && lead <= 0xf4) {
        count = 4;
        value = lead & 0x07U;
    } else if (lead >= 0xe0) {
        count = 3;
        value = lead & 0x0fU;
    } else if (lead >= 0xc2) {
        count = 2;
        value = lead & 0x1fU;
    } else if (lead >= 0x80) {
        return false;  // stray continuation byte
    }
    for (std::size_t index = 1; index < count; ++index) {
        std::uint8_t next = 0;
        if (!space->Read(*cursor + index, &next, sizeof(next)))
            return false;
        if ((next & 0xc0U) != 0x80U) return false;
        value = (value << 6) | (next & 0x3fU);
    }
    *codepoint = value;
    *cursor += count;
    return true;
}

}  // namespace

std::uint64_t BionicLocales::NewLocale(std::uint64_t mask,
                                       std::uint64_t name_address,
                                       std::uint64_t base,
                                       std::string* error) {
    if (mask == 0 || (mask & ~kAllCategoryMask) != 0) {
        (void)threads_->SetCurrentErrno(kInvalidArgument, error);
        return 0;
    }
    std::string name;
    for (std::size_t index = 0; index < kMaximumLocaleName; ++index) {
        char value = 0;
        if (name_address == 0 ||
            !address_space_->Read(name_address + index, &value,
                                  sizeof(value))) {
            (void)threads_->SetCurrentErrno(kBadAddress, error);
            return 0;
        }
        if (value == '\0') break;
        name.push_back(value);
    }
    if (name.empty() || name.size() == kMaximumLocaleName) {
        (void)threads_->SetCurrentErrno(kInvalidArgument, error);
        return 0;
    }

    const std::lock_guard lock(mutex_);
    if (base != 0 && base != kGlobalLocale && !locales_.contains(base)) {
        (void)threads_->SetCurrentErrno(kInvalidArgument, error);
        return 0;
    }
    const std::uint64_t locale = heap_->Callocate(1, 16);
    if (locale == 0) {
        (void)threads_->SetCurrentErrno(kOutOfMemory, error);
        return 0;
    }
    const std::array<std::uint64_t, 2> marker{0x49534f4d524c4f43ULL,
                                              mask};
    if (!heap_->Write(locale, marker.data(), sizeof(marker))) {
        std::string ignored;
        (void)heap_->Release(locale, &ignored);
        SetError(error, "guest locale allocation is unwritable");
        return 0;
    }
    locales_.insert(locale);
    return locale;
}

bool BionicLocales::FreeLocale(std::uint64_t locale, std::string* error) {
    const std::lock_guard lock(mutex_);
    if (locale == 0 || locale == kGlobalLocale ||
        !locales_.contains(locale)) {
        SetError(error, "guest locale handle is invalid");
        return false;
    }
    for (const auto& [thread, selected] : thread_locales_) {
        (void)thread;
        if (selected == locale) {
            SetError(error, "guest locale handle is still selected");
            return false;
        }
    }
    if (!heap_->Release(locale, error)) return false;
    locales_.erase(locale);
    return true;
}

bool BionicLocales::UseLocale(std::uint64_t locale,
                              std::uint64_t* previous,
                              std::string* error) {
    const std::optional<std::uint32_t> thread =
        threads_->CurrentGuestThreadId();
    if (!thread || previous == nullptr) {
        SetError(error, "current guest thread has no locale state");
        return false;
    }
    const std::lock_guard lock(mutex_);
    const auto found = thread_locales_.find(*thread);
    *previous = found == thread_locales_.end() ? kGlobalLocale
                                               : found->second;
    if (locale == 0) return true;
    if (locale == kGlobalLocale) {
        thread_locales_.erase(*thread);
        return true;
    }
    if (!locales_.contains(locale)) {
        (void)threads_->SetCurrentErrno(kInvalidArgument, error);
        *previous = 0;
        return true;
    }
    thread_locales_[*thread] = locale;
    return true;
}

std::uint64_t BionicLocales::MbToWide(std::uint64_t output,
                                      std::uint64_t input,
                                      std::uint64_t available,
                                      std::string* error) {
    if (input == 0) return 0;
    if (available == 0) {
        (void)threads_->SetCurrentErrno(kIllegalSequence, error);
        return UINT64_MAX;
    }
    std::array<std::uint8_t, 4> bytes{};
    if (!address_space_->Read(input, &bytes[0], sizeof(bytes[0]))) {
        (void)threads_->SetCurrentErrno(kBadAddress, error);
        return UINT64_MAX;
    }
    if (bytes[0] == 0) {
        const std::uint32_t zero = 0;
        if (output != 0 &&
            !address_space_->Write(output, &zero, sizeof(zero))) {
            (void)threads_->SetCurrentErrno(kBadAddress, error);
            return UINT64_MAX;
        }
        return 0;
    }

    std::size_t count = 0;
    std::uint32_t codepoint = 0;
    if (bytes[0] < 0x80) {
        count = 1;
        codepoint = bytes[0];
    } else if (bytes[0] >= 0xc2 && bytes[0] <= 0xdf) {
        count = 2;
        codepoint = bytes[0] & 0x1fU;
    } else if (bytes[0] >= 0xe0 && bytes[0] <= 0xef) {
        count = 3;
        codepoint = bytes[0] & 0x0fU;
    } else if (bytes[0] >= 0xf0 && bytes[0] <= 0xf4) {
        count = 4;
        codepoint = bytes[0] & 0x07U;
    } else {
        (void)threads_->SetCurrentErrno(kIllegalSequence, error);
        return UINT64_MAX;
    }
    if (available < count ||
        (count > 1 &&
         !address_space_->Read(input + 1, bytes.data() + 1, count - 1))) {
        (void)threads_->SetCurrentErrno(
            available < count ? kIllegalSequence : kBadAddress, error);
        return UINT64_MAX;
    }
    for (std::size_t index = 1; index < count; ++index) {
        if ((bytes[index] & 0xc0U) != 0x80U) {
            (void)threads_->SetCurrentErrno(kIllegalSequence, error);
            return UINT64_MAX;
        }
        codepoint = (codepoint << 6) | (bytes[index] & 0x3fU);
    }
    const bool overlong =
        (count == 2 && codepoint < 0x80) ||
        (count == 3 && codepoint < 0x800) ||
        (count == 4 && codepoint < 0x10000);
    if (overlong || (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
        codepoint > 0x10ffff) {
        (void)threads_->SetCurrentErrno(kIllegalSequence, error);
        return UINT64_MAX;
    }
    if (output != 0 &&
        !address_space_->Write(output, &codepoint, sizeof(codepoint))) {
        (void)threads_->SetCurrentErrno(kBadAddress, error);
        return UINT64_MAX;
    }
    return count;
}

std::uint64_t BionicLocales::WideToMb(std::uint64_t output,
                                      std::uint64_t codepoint_value,
                                      std::uint64_t state,
                                      std::string* error) {
    const std::uint32_t zero = 0;
    if (state != 0 &&
        !address_space_->Write(state, &zero, sizeof(zero))) {
        (void)threads_->SetCurrentErrno(kBadAddress, error);
        return UINT64_MAX;
    }
    if (output == 0) return 1;
    if (codepoint_value > 0x10ffff ||
        (codepoint_value >= 0xd800 && codepoint_value <= 0xdfff)) {
        (void)threads_->SetCurrentErrno(kIllegalSequence, error);
        return UINT64_MAX;
    }
    const std::uint32_t codepoint =
        static_cast<std::uint32_t>(codepoint_value);
    std::array<std::uint8_t, 4> bytes{};
    std::size_t count = 0;
    if (codepoint < 0x80) {
        bytes[0] = static_cast<std::uint8_t>(codepoint);
        count = 1;
    } else if (codepoint < 0x800) {
        bytes[0] = static_cast<std::uint8_t>(0xc0U | (codepoint >> 6));
        bytes[1] = static_cast<std::uint8_t>(0x80U | (codepoint & 0x3fU));
        count = 2;
    } else if (codepoint < 0x10000) {
        bytes[0] = static_cast<std::uint8_t>(0xe0U | (codepoint >> 12));
        bytes[1] = static_cast<std::uint8_t>(
            0x80U | ((codepoint >> 6) & 0x3fU));
        bytes[2] = static_cast<std::uint8_t>(0x80U | (codepoint & 0x3fU));
        count = 3;
    } else {
        bytes[0] = static_cast<std::uint8_t>(0xf0U | (codepoint >> 18));
        bytes[1] = static_cast<std::uint8_t>(
            0x80U | ((codepoint >> 12) & 0x3fU));
        bytes[2] = static_cast<std::uint8_t>(
            0x80U | ((codepoint >> 6) & 0x3fU));
        bytes[3] = static_cast<std::uint8_t>(0x80U | (codepoint & 0x3fU));
        count = 4;
    }
    if (!address_space_->Write(output, bytes.data(), count)) {
        (void)threads_->SetCurrentErrno(kBadAddress, error);
        return UINT64_MAX;
    }
    return count;
}

std::uint64_t BionicLocales::SetLocale(std::uint64_t category,
                                       std::uint64_t name_address,
                                       std::string* error) {
    (void)category;  // all categories share the single compat locale name
    std::string requested;
    if (name_address != 0) {
        for (std::size_t index = 0; index < kMaximumLocaleName; ++index) {
            char value = 0;
            if (!address_space_->Read(name_address + index, &value,
                                      sizeof(value))) {
                (void)threads_->SetCurrentErrno(kBadAddress, error);
                return 0;
            }
            if (value == '\0') break;
            requested.push_back(value);
        }
    }
    const std::lock_guard lock(mutex_);
    if (name_address != 0) {
        // An empty name selects the implementation default; anything else is
        // accepted and echoed back, matching setlocale's return contract.
        setlocale_name_ = requested.empty() ? "C.UTF-8" : requested;
    }
    if (setlocale_buffer_ == 0) {
        setlocale_buffer_ = heap_->Callocate(1, kMaximumLocaleName);
        if (setlocale_buffer_ == 0) {
            (void)threads_->SetCurrentErrno(kOutOfMemory, error);
            return 0;
        }
    }
    std::array<std::uint8_t, kMaximumLocaleName> image{};
    for (std::size_t index = 0;
         index < setlocale_name_.size() && index < kMaximumLocaleName - 1;
         ++index)
        image[index] = static_cast<std::uint8_t>(setlocale_name_[index]);
    if (!heap_->Write(setlocale_buffer_, image.data(), image.size())) {
        SetError(error, "setlocale buffer is unwritable");
        return 0;
    }
    return setlocale_buffer_;
}

bool BionicLocales::RegisterImports(Aarch64ImportRegistry* registry,
                                    std::string* error) {
    if (registry == nullptr || heap_ == nullptr || address_space_ == nullptr ||
        threads_ == nullptr)
        return false;
    if (!registry->Register(
            "newlocale",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                (*registers)[0] =
                    NewLocale((*registers)[0], (*registers)[1],
                              (*registers)[2], dispatch_error);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "freelocale",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                if (!FreeLocale((*registers)[0], dispatch_error))
                    return false;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
        "uselocale",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* dispatch_error) {
            std::uint64_t previous = 0;
            if (!UseLocale((*registers)[0], &previous, dispatch_error))
                return false;
            (*registers)[0] = previous;
            return true;
        },
        error))
        return false;
    if (!registry->Register(
            "setlocale",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                (*registers)[0] = SetLocale((*registers)[0], (*registers)[1],
                                            dispatch_error);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
        "mbtowc",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* dispatch_error) {
            (*registers)[0] = MbToWide((*registers)[0], (*registers)[1],
                                       (*registers)[2], dispatch_error);
            return true;
        },
        error))
        return false;
    if (!registry->Register(
            "wcrtomb",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                (*registers)[0] =
                    WideToMb((*registers)[0], (*registers)[1],
                             (*registers)[2], dispatch_error);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
        "__ctype_get_mb_cur_max",
        [](Aarch64ImportRegistry::Registers* registers, std::string*) {
            (*registers)[0] = 4;
            return true;
        },
        error))
        return false;
    // localeconv: the C locale view. PvZ1's float-formatting paths call it
    // during static init; a lazily built guest block mirrors bionic's
    // C-locale lconv (decimal point ".", every monetary field empty or
    // CHAR_MAX) and the pointer stays valid for the session.
    if (!registry->Register(
        "localeconv",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* dispatch_error) {
            const std::lock_guard lock(mutex_);
            constexpr std::uint64_t kStructBytes = 176;
            constexpr std::uint64_t kBlockBytes = 192;
            if (lconv_address_ == 0) {
                const std::uint64_t block = heap_->Callocate(1, kBlockBytes);
                if (block == 0) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = "localeconv lconv allocation failed";
                    return false;
                }
                std::array<std::uint8_t, kBlockBytes> image{};
                const std::uint64_t dot = block + kStructBytes;
                const std::uint64_t empty = block + kStructBytes + 2;
                image[kStructBytes] = '.';
                const auto pointer_at = [&image](std::uint64_t offset,
                                                 std::uint64_t value) {
                    for (std::size_t byte = 0; byte < 8; ++byte)
                        image[offset + byte] =
                            static_cast<std::uint8_t>((value >> (8 * byte)) &
                                                      0xff);
                };
                pointer_at(0, dot);      // decimal_point
                pointer_at(8, empty);    // thousands_sep
                pointer_at(16, empty);   // grouping
                pointer_at(24, empty);   // int_curr_symbol
                pointer_at(32, empty);   // currency_symbol
                pointer_at(40, empty);   // mon_decimal_point
                pointer_at(48, empty);   // mon_thousands_sep
                pointer_at(56, empty);   // mon_grouping
                pointer_at(64, empty);   // positive_sign
                pointer_at(72, empty);   // negative_sign
                for (std::size_t field = 0; field < 18; ++field)
                    image[80 + field] = 0x7f;  // CHAR_MAX, C locale
                if (!address_space_->Write(block, image.data(),
                                           image.size())) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = "localeconv lconv write failed";
                    return false;
                }
                lconv_address_ = block;
            }
                (*registers)[0] = lconv_address_;
                return true;
            },
            error))
        return false;
    // mbsrtowcs: stateless UTF-8 -> UTF-32 under the default C locale.
    // dst may be null to measure; *src is updated per POSIX semantics.
    const auto convert_mbs_to_wide =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* dispatch_error) {
            const std::uint64_t dst = (*registers)[0];
            const std::uint64_t src_pointer = (*registers)[1];
            const std::uint64_t limit = (*registers)[2];
            std::uint64_t source = 0;
            if (src_pointer != 0 &&
                !address_space_->Read(src_pointer, &source,
                                      sizeof(source))) {
                if (dispatch_error != nullptr)
                    *dispatch_error = "mbsrtowcs *src is not readable";
                return false;
            }
            std::uint64_t cursor = source;
            std::uint64_t written = 0;
            for (;;) {
                std::uint8_t terminator = 0;
                if (!address_space_->Read(cursor, &terminator,
                                          sizeof(terminator))) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = "mbsrtowcs source is not readable";
                    return false;
                }
                if (terminator == 0) {
                    if (dst != 0 && written < limit) {
                        const std::uint32_t zero = 0;
                        (void)address_space_->Write(
                            dst + written * 4, &zero, sizeof(zero));
                    }
                    if (src_pointer != 0) {
                        const std::uint64_t null_pointer = 0;
                        (void)address_space_->Write(src_pointer, &null_pointer,
                                                    sizeof(null_pointer));
                    }
                    (*registers)[0] = written;
                    return true;
                }
                std::uint32_t wide = 0;
                if (!DecodeUtf8At(address_space_, &cursor, &wide)) {
                    (void)threads_->SetCurrentErrno(kIllegalSequence,
                                                    dispatch_error);
                    (*registers)[0] =
                        std::numeric_limits<std::uint64_t>::max();
                    return true;
                }
                if (dst != 0 && written < limit)
                    (void)address_space_->Write(dst + written * 4, &wide,
                                                sizeof(wide));
                ++written;
                if (dst != 0 && written >= limit) {
                    if (src_pointer != 0)
                        (void)address_space_->Write(src_pointer, &cursor,
                                                    sizeof(cursor));
                    (*registers)[0] = written;
                    return true;
                }
            }
        };
    if (!registry->Register(
            "mbsrtowcs",
            [convert_mbs_to_wide](Aarch64ImportRegistry::Registers* registers,
                                  std::string* dispatch_error) {
                return convert_mbs_to_wide(registers, dispatch_error);
            },
            error))
        return false;
    // mbstowcs: non-restartable form; the source pointer arrives directly.
    if (!registry->Register(
            "mbstowcs",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint64_t dst = (*registers)[0];
                const std::uint64_t source = (*registers)[1];
                const std::uint64_t limit = (*registers)[2];
                if (source == 0) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = "mbstowcs source is null";
                    return false;
                }
                std::uint64_t cursor = source;
                std::uint64_t written = 0;
                for (;;) {
                    std::uint8_t terminator = 0;
                    if (!address_space_->Read(cursor, &terminator,
                                              sizeof(terminator))) {
                        if (dispatch_error != nullptr)
                            *dispatch_error =
                                "mbstowcs source is not readable";
                        return false;
                    }
                    if (terminator == 0) {
                        if (dst != 0 && written < limit) {
                            const std::uint32_t zero = 0;
                            (void)address_space_->Write(dst + written * 4,
                                                        &zero, sizeof(zero));
                        }
                        (*registers)[0] = written;
                        return true;
                    }
                    std::uint32_t wide = 0;
                    if (!DecodeUtf8At(address_space_, &cursor, &wide)) {
                        (void)threads_->SetCurrentErrno(kIllegalSequence,
                                                        dispatch_error);
                        (*registers)[0] =
                            std::numeric_limits<std::uint64_t>::max();
                        return true;
                    }
                    if (dst != 0 && written < limit)
                        (void)address_space_->Write(dst + written * 4, &wide,
                                                    sizeof(wide));
                    ++written;
                    if (dst != 0 && written >= limit) {
                        (*registers)[0] = written;
                        return true;
                    }
                }
            },
            error))
        return false;
    // The _l ctype family: every served locale uses the default C
    // categories, so each handler ignores its locale_t argument and
    // applies the plain ASCII classification.
    const std::pair<const char*, int (*)(int)> ctype_handlers[] = {
        {"toupper_l", ::toupper},   {"tolower_l", ::tolower},
        {"isalpha_l", ::isalpha},   {"isdigit_l", ::isdigit},
        {"isspace_l", ::isspace},   {"isalnum_l", ::isalnum},
        {"isupper_l", ::isupper},   {"islower_l", ::islower},
        {"ispunct_l", ::ispunct},   {"isxdigit_l", ::isxdigit},
        {"isprint_l", ::isprint},   {"iscntrl_l", ::iscntrl},
        {"isgraph_l", ::isgraph},   {"isblank_l", ::isblank},
        // The plain (non-_l) ctype family resolves to the same C-locale
        // classification; libc exports both spellings.
        {"toupper", ::toupper},     {"tolower", ::tolower},
        {"isalpha", ::isalpha},     {"isdigit", ::isdigit},
        {"isspace", ::isspace},     {"isalnum", ::isalnum},
        {"isupper", ::isupper},     {"islower", ::islower},
        {"ispunct", ::ispunct},     {"isxdigit", ::isxdigit},
        {"isprint", ::isprint},     {"iscntrl", ::iscntrl},
        {"isgraph", ::isgraph},     {"isblank", ::isblank},
    };
    for (const auto& entry : ctype_handlers) {
        const auto classify = entry.second;
        if (!registry->Register(
                entry.first,
                [classify](Aarch64ImportRegistry::Registers* registers,
                           std::string*) {
                    (*registers)[0] = static_cast<std::uint64_t>(
                        classify(static_cast<int>((*registers)[0]) & 0xff));
                    return true;
                },
                error))
            return false;
    }
    // Wide _l variants: the same C-locale argument is ignored and the
    // wide classification is applied to the UCS-4 code point.
    const std::pair<const char*, int (*)(wint_t)> wide_ctype_handlers[] = {
        {"iswspace_l", ::iswspace}, {"iswalpha_l", ::iswalpha},
        {"iswdigit_l", ::iswdigit}, {"iswalnum_l", ::iswalnum},
        {"iswupper_l", ::iswupper}, {"iswlower_l", ::iswlower},
        {"iswpunct_l", ::iswpunct}, {"iswxdigit_l", ::iswxdigit},
        {"iswprint_l", ::iswprint}, {"iswcntrl_l", ::iswcntrl},
        {"iswgraph_l", ::iswgraph},
        // iswspace is already registered by the numbers module.
        {"iswalpha", ::iswalpha},
        {"iswdigit", ::iswdigit},   {"iswalnum", ::iswalnum},
        {"iswupper", ::iswupper},   {"iswlower", ::iswlower},
        {"iswpunct", ::iswpunct},   {"iswxdigit", ::iswxdigit},
        {"iswprint", ::iswprint},   {"iswcntrl", ::iswcntrl},
        {"iswgraph", ::iswgraph},
    };
    for (const auto& entry : wide_ctype_handlers) {
        const auto classify = entry.second;
        if (!registry->Register(
                entry.first,
                [classify](Aarch64ImportRegistry::Registers* registers,
                           std::string*) {
                    (*registers)[0] = static_cast<std::uint64_t>(
                        classify(static_cast<wint_t>((*registers)[0])));
                    return true;
                },
                error))
            return false;
    }
    if (!registry->Register(
            "towlower_l",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    ::towlower(static_cast<wint_t>((*registers)[0])));
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "towupper_l",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    ::towupper(static_cast<wint_t>((*registers)[0])));
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "towlower",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    ::towlower(static_cast<wint_t>((*registers)[0])));
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "towupper",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    ::towupper(static_cast<wint_t>((*registers)[0])));
                return true;
            },
            error))
        return false;
    // isascii/toascii: ASCII-range predicates used by the C++ <locale> and
    // ctype facets in libc++.
    if (!registry->Register(
            "isascii",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] =
                    ((*registers)[0] >= 0 && (*registers)[0] < 0x80) ? 1 : 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "toascii",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = (*registers)[0] & 0x7f;
                return true;
            },
            error))
        return false;
    // wctob: a code point below 0x80 is its own single byte; anything
    // wider has no single-byte form in the C locale (EOF).
    if (!registry->Register(
            "wctob",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                const std::uint64_t wide = (*registers)[0];
                (*registers)[0] =
                    wide < 0x80 ? wide
                                : static_cast<std::uint64_t>(UINT64_MAX);
                return true;
            },
            error))
        return false;
    return true;
}

bool BionicLocales::Shutdown(std::string* error) {
    const std::lock_guard lock(mutex_);
    thread_locales_.clear();
    for (const std::uint64_t locale : locales_) {
        if (!heap_->Release(locale, error)) return false;
    }
    locales_.clear();
    if (setlocale_buffer_ != 0) {
        if (!heap_->Release(setlocale_buffer_, error)) return false;
        setlocale_buffer_ = 0;
    }
    return true;
}

std::size_t BionicLocales::locale_count() const {
    const std::lock_guard lock(mutex_);
    return locales_.size();
}

bool BionicLocales::IsLocale(std::uint64_t locale) const {
    const std::lock_guard lock(mutex_);
    return locale != 0 && locale != kGlobalLocale &&
           locales_.contains(locale);
}

}  // namespace isomr
