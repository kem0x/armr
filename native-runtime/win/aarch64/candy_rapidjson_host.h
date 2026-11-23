#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace isomr {

class GuestHeap;

struct CandyRapidJsonHostBuildResult {
    std::uint64_t nodes = 0;
    std::uint64_t pool_bytes = 0;
};

// Builds the measured ARM64 RapidJSON representation used by the pinned Candy
// profile: 1.1.0 parser/value layout with the legacy five-field memory-pool
// allocator. The supplied document must be freshly constructed with its owned
// allocator still empty. No payload bytes leave the local process.
bool BuildCandyRapidJsonHybridDocument(
    GuestHeap& heap, std::uint64_t document_address, std::string_view json,
    CandyRapidJsonHostBuildResult* result, std::string* error);

}  // namespace isomr
