#pragma once

#include "import_registry.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace isomr {
class BionicFileSystem;
class BionicThreadState;
class GuestAddressSpace;
class GuestHeap;

class BionicStdio {
public:
    BionicStdio(GuestHeap* heap, GuestAddressSpace* memory,
                BionicFileSystem* files, BionicThreadState* threads)
        : heap_(heap), memory_(memory), files_(files), threads_(threads) {}

    bool RegisterImports(Aarch64ImportRegistry* registry,
                         std::string* error);
    bool Shutdown(std::string* error);
    std::size_t stream_count() const;

private:
    bool ReadMode(std::uint64_t address, std::string* mode);
    std::string ClassifyPath(std::uint64_t address) const;
    std::uint64_t Open(std::uint64_t path, std::uint64_t mode);
    int Close(std::uint64_t stream, std::string* error);
    std::uint64_t Read(std::uint64_t output, std::uint64_t element_size,
                       std::uint64_t element_count, std::uint64_t stream);
    int Seek(std::uint64_t stream, std::int64_t offset, int origin);
    std::int64_t Tell(std::uint64_t stream);
    // fscanf(stream, format, ...): reads the stream's remaining content, runs
    // the shared bionic scanf engine over it, writes results to the variadic
    // output pointers taken from `registers` (general[2]+), and advances the
    // descriptor's file position by the bytes actually consumed. On success
    // *result receives the assignment count (or -1 for end-of-input); a false
    // return is a hard failure the caller should propagate.
    bool ScanfStream(
        std::uint64_t stream, std::uint64_t format_address,
        const Aarch64ImportRegistry::Registers& registers,
        std::int64_t* result, std::string* error);
    bool IsModeledStream(std::uint64_t stream) const;
    void SetErrno(std::int32_t value);

    GuestHeap* heap_ = nullptr;
    GuestAddressSpace* memory_ = nullptr;
    BionicFileSystem* files_ = nullptr;
    BionicThreadState* threads_ = nullptr;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, int> streams_;
    // The __sF[3]/stdin/stdout/stderr backing block, allocated once at import
    // registration. Freed at Shutdown so a clean teardown drains the heap.
    std::uint64_t stream_vars_base_ = 0;
};

}  // namespace isomr
