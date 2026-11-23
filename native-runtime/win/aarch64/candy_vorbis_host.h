#pragma once

#include "import_registry.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace isomr {

class BionicRuntime;
struct Aarch64ProfileRuntimeConfig;

struct CandyVorbisHostSnapshot {
    std::uint64_t test_callbacks = 0;
    std::uint64_t test_open = 0;
    std::uint64_t streams = 0;
    std::uint64_t time_total = 0;
    std::uint64_t pcm_total = 0;
    std::uint64_t pcm_seek = 0;
    std::uint64_t pcm_tell = 0;
    std::uint64_t info = 0;
    std::uint64_t read = 0;
    std::uint64_t clear = 0;
    std::uint64_t read_bytes = 0;
    std::uint64_t refusals = 0;
    std::size_t active = 0;
};

class CandyVorbisHostMirror {
public:
    ~CandyVorbisHostMirror();

    CandyVorbisHostMirror(const CandyVorbisHostMirror&) = delete;
    CandyVorbisHostMirror& operator=(const CandyVorbisHostMirror&) = delete;

    CandyVorbisHostSnapshot snapshot() const;

private:
    struct Impl;

    CandyVorbisHostMirror();
    Aarch64ImportRegistry::Handler CreateHandler(
        std::size_t operation, BionicRuntime* runtime);

    std::unique_ptr<Impl> impl_;

    friend bool ConfigureCandyVorbisHostMirror(
        Aarch64ProfileRuntimeConfig* config,
        std::shared_ptr<CandyVorbisHostMirror>* mirror,
        std::string* error);
};

// Adds the exact Candy 1.333.2.1 decoder substitutions. The caller retains
// the mirror to report address-free counters; the profile's factories retain
// it for the complete runtime lifetime.
bool ConfigureCandyVorbisHostMirror(
    Aarch64ProfileRuntimeConfig* config,
    std::shared_ptr<CandyVorbisHostMirror>* mirror,
    std::string* error);

}  // namespace isomr
