#pragma once

#include "bionic_files.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace isomr {

class Aarch64ImportRegistry;
class BionicThreadState;
class GuestHeap;

class BionicMappings {
public:
    BionicMappings(GuestHeap* heap, BionicThreadState* threads,
                   BionicFileSystem* files)
        : heap_(heap), threads_(threads), files_(files) {}

    bool RegisterImports(Aarch64ImportRegistry* imports, std::string* error);
    bool Shutdown(std::string* error);
    std::size_t mapping_count() const;

private:
    struct Mapping {
        std::uint64_t length;
        std::uint64_t protection;
        BionicFileSystem::FileReference file;
        std::int64_t file_offset = 0;
        std::uint64_t file_length = 0;
        bool shared_writeback_capable = false;
        bool shared_writeback = false;
    };

    bool CoversRangeLocked(std::uint64_t address,
                           std::uint64_t length) const;
    void SplitAtLocked(std::uint64_t address);
    void MergeAdjacentLocked();

    GuestHeap* heap_ = nullptr;
    BionicThreadState* threads_ = nullptr;
    BionicFileSystem* files_ = nullptr;
    mutable std::mutex mutex_;
    std::map<std::uint64_t, Mapping> mappings_;
};

}  // namespace isomr
