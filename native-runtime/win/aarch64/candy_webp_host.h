#pragma once

#include "import_registry.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace isomr {

class BionicRuntime;
struct Aarch64ProfileRuntimeConfig;

struct CandyWebP103Api {
    using GetDecoderVersion = int (*)();
    using DecodeRgbaInto = std::uint8_t* (*)(
        const std::uint8_t*, std::size_t, std::uint8_t*, std::size_t, int);

    GetDecoderVersion get_decoder_version = nullptr;
    DecodeRgbaInto decode_rgba_into = nullptr;
};

struct CandyWebP103HostSnapshot {
    std::uint64_t attempts = 0;
    std::uint64_t successes = 0;
    std::uint64_t decoder_failures = 0;
    std::uint64_t refusals = 0;
    std::uint64_t input_bytes = 0;
    std::uint64_t output_bytes = 0;
};

class CandyWebP103Host {
public:
    ~CandyWebP103Host();

    CandyWebP103Host(const CandyWebP103Host&) = delete;
    CandyWebP103Host& operator=(const CandyWebP103Host&) = delete;

    CandyWebP103HostSnapshot snapshot() const;

    // Public only for the bounded configuration/test factories below.
    CandyWebP103Host(CandyWebP103Api api,
                     std::shared_ptr<void> library_owner);
    Aarch64ImportRegistry::Handler CreateHandler(BionicRuntime* runtime);

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;

    friend bool ConfigureCandyWebP103Host(
        Aarch64ProfileRuntimeConfig*, std::shared_ptr<CandyWebP103Host>*,
        const std::wstring&, std::string*);
    friend bool ConfigureCandyWebP103HostForTesting(
        Aarch64ProfileRuntimeConfig*, std::shared_ptr<CandyWebP103Host>*,
        CandyWebP103Api, std::string*);
};

// Adds the exact Candy 1.333.2.1 WebPDecodeRGBAInto substitution after loading
// a repo-built wrapper around the embedded decoder's exact libwebp 1.0.3
// version. The explicit library path must be absolute. The caller retains the
// host object for address-free counters.
bool ConfigureCandyWebP103Host(
    Aarch64ProfileRuntimeConfig* config,
    std::shared_ptr<CandyWebP103Host>* host,
    const std::wstring& library_path, std::string* error);

// Dependency-free semantic fixture entry point. Production callers must use
// ConfigureCandyWebP103Host so the exact-version DLL and exports are verified.
bool ConfigureCandyWebP103HostForTesting(
    Aarch64ProfileRuntimeConfig* config,
    std::shared_ptr<CandyWebP103Host>* host, CandyWebP103Api api,
    std::string* error);

}  // namespace isomr
