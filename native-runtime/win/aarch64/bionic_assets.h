#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace isomr {

class Aarch64ImportRegistry;
class BionicFileSystem;
class GuestAddressSpace;
class GuestHeap;
class GuestJni;

class BionicAssets {
public:
    BionicAssets(GuestHeap* heap, GuestAddressSpace* memory,
                 BionicFileSystem* files, GuestJni* jni);
    ~BionicAssets();

    BionicAssets(const BionicAssets&) = delete;
    BionicAssets& operator=(const BionicAssets&) = delete;

    bool RegisterImports(Aarch64ImportRegistry* imports,
                         std::string* error);
    bool Shutdown(std::string* error);

    std::size_t manager_count() const;
    std::size_t asset_count() const;
    // Parent-local treatment support. The handle must belong to this asset
    // manager, and the complete immutable asset is copied under the manager
    // lock. No path or payload bytes are logged.
    bool CopyBufferLocalOnly(std::uint64_t asset, std::size_t maximum_size,
                             std::vector<std::uint8_t>* output,
                             std::string* error);

private:
    bool FromJava(std::uint64_t environment, std::uint64_t java_manager,
                  std::uint64_t* result, std::string* error);
    bool Open(std::uint64_t manager, std::uint64_t name,
              std::uint32_t mode, std::uint64_t* result,
              std::string* error);
    std::int64_t GetLength64(std::uint64_t asset);
    std::int64_t GetRemainingLength64(std::uint64_t asset);
    std::int64_t Read(std::uint64_t asset, std::uint64_t output,
                      std::uint64_t count);
    std::int64_t Seek64(std::uint64_t asset, std::int64_t offset,
                        int origin);
    std::uint64_t GetBuffer(std::uint64_t asset);
    std::uint64_t GetBufferLocked(std::uint64_t asset);
    bool Close(std::uint64_t asset, std::string* error);

    GuestHeap* heap_ = nullptr;
    GuestAddressSpace* memory_ = nullptr;
    BionicFileSystem* files_ = nullptr;
    GuestJni* jni_ = nullptr;
    mutable std::mutex mutex_;
    std::uint64_t manager_ = 0;
    std::unordered_map<std::uint64_t, int> assets_;
    std::unordered_map<std::uint64_t, std::uint64_t> buffers_;
    bool registration_attempted_ = false;
    bool manager_adopted_ = false;
    bool shutdown_ = false;
};

}  // namespace isomr
