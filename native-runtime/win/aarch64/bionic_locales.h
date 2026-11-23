#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace isomr {

class Aarch64ImportRegistry;
class BionicThreadState;
class GuestAddressSpace;
class GuestHeap;

class BionicLocales {
public:
    BionicLocales(GuestHeap* heap, GuestAddressSpace* address_space,
                  BionicThreadState* threads)
        : heap_(heap), address_space_(address_space), threads_(threads) {}

    bool RegisterImports(Aarch64ImportRegistry* registry,
                         std::string* error);
    bool Shutdown(std::string* error);
    std::size_t locale_count() const;
    bool IsLocale(std::uint64_t locale) const;

private:
    std::uint64_t NewLocale(std::uint64_t mask, std::uint64_t name_address,
                            std::uint64_t base, std::string* error);
    bool FreeLocale(std::uint64_t locale, std::string* error);
    bool UseLocale(std::uint64_t locale, std::uint64_t* previous,
                   std::string* error);
    std::uint64_t MbToWide(std::uint64_t output, std::uint64_t input,
                           std::uint64_t available, std::string* error);
    std::uint64_t WideToMb(std::uint64_t output, std::uint64_t codepoint,
                           std::uint64_t state, std::string* error);
    // setlocale(category, name): name==0 queries, otherwise sets. Returns a
    // persistent guest pointer to the resulting locale name (or 0 on a bad
    // read). The buffer is lazily allocated once and reused.
    std::uint64_t SetLocale(std::uint64_t category, std::uint64_t name_address,
                            std::string* error);

    GuestHeap* heap_ = nullptr;
    GuestAddressSpace* address_space_ = nullptr;
    BionicThreadState* threads_ = nullptr;
    mutable std::mutex mutex_;
    std::unordered_set<std::uint64_t> locales_;
    std::unordered_map<std::uint32_t, std::uint64_t> thread_locales_;
    std::uint64_t lconv_address_ = 0;
    std::uint64_t setlocale_buffer_ = 0;
    std::string setlocale_name_ = "C.UTF-8";
};

}  // namespace isomr
