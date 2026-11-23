#include "bionic_assets.h"

#include "bionic_files.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "guest_jni.h"
#include "import_registry.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>
#include <sstream>
#include <string_view>
#include <vector>

namespace isomr {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::string_view AssetPathCategory(std::string_view path) {
    if (path.starts_with("assets/res_output/"))
        return "assets-res-output";
    if (path.starts_with("res_output/")) return "res-output";
    if (path.starts_with("/assets/res_output/"))
        return "slash-assets-res-output";
    if (path.starts_with("/res_output/")) return "slash-res-output";
    if (path.starts_with("assets/")) return "assets-prefix";
    if (path.starts_with('/')) return "absolute";
    return "other";
}

std::uint64_t AssetPathFingerprint(std::string_view path) {
    std::uint64_t value = 1469598103934665603ULL;
    for (const unsigned char byte : path) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return value;
}

std::string AssetPathTrace(std::string_view path, std::string_view outcome) {
    const std::size_t slash = path.find_last_of('/');
    const std::string_view basename =
        slash == std::string_view::npos ? path : path.substr(slash + 1);
    std::ostringstream trace;
    trace << "trace:asset-open-" << AssetPathCategory(path) << "-len"
          << path.size() << "-fnv" << std::hex << AssetPathFingerprint(path)
          << "-basefnv" << AssetPathFingerprint(basename) << '-'
          << outcome;
    return trace.str();
}

}  // namespace

BionicAssets::BionicAssets(GuestHeap* heap, GuestAddressSpace* memory,
                           BionicFileSystem* files, GuestJni* jni)
    : heap_(heap), memory_(memory), files_(files), jni_(jni) {}

BionicAssets::~BionicAssets() {
    if (heap_ == nullptr || files_ == nullptr) return;
    std::string ignored;
    for (const auto& [handle, descriptor] : assets_) {
        (void)files_->Close(descriptor);
        const auto buffer = buffers_.find(handle);
        if (buffer != buffers_.end())
            (void)heap_->Release(buffer->second, &ignored);
        (void)heap_->Release(handle, &ignored);
    }
    if (manager_ != 0) (void)heap_->Release(manager_, &ignored);
}

bool BionicAssets::RegisterImports(Aarch64ImportRegistry* imports,
                                   std::string* error) {
    if (registration_attempted_ || shutdown_ || imports == nullptr ||
        heap_ == nullptr || memory_ == nullptr || files_ == nullptr ||
        jni_ == nullptr) {
        SetError(error, "bionic asset manager configuration is invalid");
        return false;
    }
    registration_attempted_ = true;
    if (!imports->Register(
            "AAssetManager_fromJava",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                std::uint64_t result = 0;
                if (!FromJava((*registers)[0], (*registers)[1], &result,
                              dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = result;
                return true;
            },
            error) ||
        !imports->Register(
            "AAssetManager_open",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                std::uint64_t result = 0;
                if (!Open((*registers)[0], (*registers)[1],
                          static_cast<std::uint32_t>((*registers)[2]),
                          &result, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = result;
                if (dispatch_error != nullptr && dispatch_error->empty())
                    *dispatch_error = result == 0
                                          ? "trace:asset-open-miss"
                                          : "trace:asset-open-hit";
                return true;
            },
            error) ||
        !imports->Register(
            "AAsset_getLength64",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    GetLength64((*registers)[0]));
                return true;
            },
            error) ||
        !imports->Register(
            "AAsset_getRemainingLength64",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    GetRemainingLength64((*registers)[0]));
                return true;
            },
            error) ||
        !imports->Register(
            "AAsset_read",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(Read(
                    (*registers)[0], (*registers)[1], (*registers)[2]));
                return true;
            },
            error) ||
        !imports->Register(
            "AAsset_seek64",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(Seek64(
                    (*registers)[0],
                    std::bit_cast<std::int64_t>((*registers)[1]),
                    std::bit_cast<std::int32_t>(
                        static_cast<std::uint32_t>((*registers)[2]))));
                return true;
            },
            error) ||
        !imports->Register(
            "AAsset_getBuffer",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                (*registers)[0] = GetBuffer((*registers)[0]);
                return true;
            },
            error))
        return false;
    return imports->Register(
        "AAsset_close",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* dispatch_error) {
            const std::uint64_t asset = (*registers)[0];
            (*registers)[0] = 0;
            return Close(asset, dispatch_error);
        },
        error);
}

bool BionicAssets::FromJava(std::uint64_t environment,
                            std::uint64_t java_manager,
                            std::uint64_t* result, std::string* error) {
    constexpr std::string_view kAssetManagerType =
        "Landroid/content/res/AssetManager;";
    if (result == nullptr) {
        SetError(error, "AAssetManager_fromJava result is null");
        return false;
    }
    *result = 0;
    if (!registration_attempted_ || shutdown_ ||
        !jni_->IsCurrentEnvironmentReference(environment) ||
        !jni_->IsObjectReferenceOfType(java_manager, kAssetManagerType)) {
        SetError(error,
                 "AAssetManager_fromJava JNI provenance is invalid");
        return false;
    }
    const std::lock_guard lock(mutex_);
    manager_adopted_ = true;
    if (manager_ == 0) {
        manager_ = heap_->Callocate(1, sizeof(std::uint64_t));
        if (manager_ == 0) {
            SetError(error,
                     "AAssetManager_fromJava guest handle allocation failed");
            return false;
        }
    }
    *result = manager_;
    return true;
}

bool BionicAssets::Open(std::uint64_t manager, std::uint64_t name,
                        std::uint32_t mode, std::uint64_t* result,
                        std::string* error) {
    constexpr std::size_t kMaximumName = 32767;
    if (result == nullptr) {
        SetError(error, "AAssetManager_open result is null");
        return false;
    }
    *result = 0;
    {
        const std::lock_guard lock(mutex_);
        if (shutdown_ || mode > 3) {
            SetError(error, "AAssetManager_open arguments are invalid");
            return false;
        }
        if (!manager_adopted_) {
            // Guest code can reach AAssetManager_open without ever calling
            // AAssetManager_fromJava (the compat runtime skips the Java
            // classes that hand out native managers), so adopt the first
            // presented handle — including null — as the manager identity.
            // Asset bytes still resolve exclusively from the configured
            // /app and /data mounts.
            manager_adopted_ = true;
            manager_ = manager;
        }
        if (manager != manager_) {
            SetError(error, "AAssetManager_open arguments are invalid");
            return false;
        }
    }
    std::uint64_t length = 0;
    std::string read_error;
    if (name == 0) {
        SetError(error, "AAssetManager_open name pointer is null");
        return false;
    }
    if (!memory_->BoundedStringLength(name, kMaximumName + 1, &length,
                                      &read_error)) {
        SetError(error, "AAssetManager_open name is unreadable: " +
                            read_error + " " +
                            memory_->DescribeRange(name, 1));
        return false;
    }
    if (length > kMaximumName) {
        SetError(error, "AAssetManager_open name exceeds its bound");
        return false;
    }
    if (length == 0) {
        SetError(error, "trace:asset-open-empty-miss");
        return true;
    }
    std::string ignored;
    std::string relative(static_cast<std::size_t>(length), '\0');
    if (!memory_->Read(name, relative.data(), relative.size())) {
        SetError(error, "AAssetManager_open name memory is invalid");
        return false;
    }
    std::string path;
    if (relative.front() == '/') {
        if (relative.size() == 1 || relative[1] == '/') return true;
        if (relative.starts_with("/data/")) {
            path = relative;
            relative.erase(0, 6);
        } else if (relative.starts_with("/app/")) {
            // EA's appbundle:/ scheme hands us the full guest path of an
            // APK asset (e.g. /app/assets/main.0...obb); resolve it
            // through the /app mount instead of rejecting it.
            path = relative;
            relative.erase(0, 1);
        } else if (relative.find('/', 1) == std::string::npos) {
            path = "/data" + relative;
            relative.erase(0, 1);
        } else {
            return true;
        }
    }
    if (relative.find('\\') != std::string::npos) {
        return true;
    }
    if (relative.find(':') != std::string::npos) {
        return true;
    }
    std::size_t start = 0;
    while (start < relative.size()) {
        const std::size_t end = relative.find('/', start);
        const std::string_view component(
            relative.data() + start,
            (end == std::string::npos ? relative.size() : end) - start);
        if (component.empty() || component == "." || component == "..") {
            return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (path.empty()) path = "/app/assets/" + relative;
    const std::uint64_t path_address = heap_->Allocate(path.size() + 1);
    if (path_address == 0 ||
        !heap_->Write(path_address, path.c_str(), path.size() + 1)) {
        if (path_address != 0) (void)heap_->Release(path_address, &ignored);
        SetError(error, "AAssetManager_open path staging failed");
        return false;
    }
    const int descriptor = files_->Open(path_address, 0, 0);
    if (!heap_->Release(path_address, &ignored)) {
        if (descriptor >= 0) (void)files_->Close(descriptor);
        SetError(error, "AAssetManager_open path cleanup failed");
        return false;
    }
    if (descriptor < 0) {
        SetError(error, AssetPathTrace(relative, "miss"));
        return true;
    }
    const std::uint64_t handle = heap_->Callocate(1, sizeof(std::uint64_t));
    if (handle == 0) {
        (void)files_->Close(descriptor);
        SetError(error, "AAssetManager_open guest handle allocation failed");
        return false;
    }
    {
        const std::lock_guard lock(mutex_);
        if (shutdown_) {
            (void)files_->Close(descriptor);
            (void)heap_->Release(handle, &ignored);
            SetError(error, "AAssetManager_open raced with shutdown");
            return false;
        }
        assets_.emplace(handle, descriptor);
    }
    *result = handle;
    SetError(error, AssetPathTrace(relative, "hit"));
    return true;
}

bool BionicAssets::Close(std::uint64_t asset, std::string* error) {
    const std::lock_guard lock(mutex_);
    const auto found = assets_.find(asset);
    if (shutdown_ || found == assets_.end()) {
        SetError(error, "AAsset_close handle is invalid");
        return false;
    }
    if (files_->Close(found->second) != 0) {
        SetError(error, "AAsset_close file descriptor is invalid");
        return false;
    }
    const auto buffer = buffers_.find(asset);
    if (buffer != buffers_.end()) {
        if (!heap_->Release(buffer->second, error)) return false;
        buffers_.erase(buffer);
    }
    if (!heap_->Release(found->first, error)) return false;
    assets_.erase(found);
    return true;
}

std::int64_t BionicAssets::GetLength64(std::uint64_t asset) {
    const std::lock_guard lock(mutex_);
    const auto found = assets_.find(asset);
    if (shutdown_ || found == assets_.end()) return -1;
    const std::int64_t original = files_->Lseek64(found->second, 0, 1);
    if (original < 0) return -1;
    const std::int64_t length = files_->Lseek64(found->second, 0, 2);
    if (length < 0) return -1;
    if (files_->Lseek64(found->second, original, 0) != original) return -1;
    return length;
}

std::int64_t BionicAssets::GetRemainingLength64(std::uint64_t asset) {
    const std::lock_guard lock(mutex_);
    const auto found = assets_.find(asset);
    if (shutdown_ || found == assets_.end()) return -1;
    const std::int64_t current = files_->Lseek64(found->second, 0, 1);
    if (current < 0) return -1;
    const std::int64_t length = files_->Lseek64(found->second, 0, 2);
    const bool restored =
        files_->Lseek64(found->second, current, 0) == current;
    if (length < current || !restored)
        return -1;
    return length - current;
}

std::int64_t BionicAssets::Read(std::uint64_t asset,
                                std::uint64_t output,
                                std::uint64_t count) {
    const std::lock_guard lock(mutex_);
    const auto found = assets_.find(asset);
    if (shutdown_ || found == assets_.end()) return -1;
    return files_->Read(found->second, output, count);
}

std::int64_t BionicAssets::Seek64(std::uint64_t asset,
                                  std::int64_t offset, int origin) {
    const std::lock_guard lock(mutex_);
    const auto found = assets_.find(asset);
    if (shutdown_ || found == assets_.end() || origin < 0 || origin > 2)
        return -1;
    const std::int64_t original = files_->Lseek64(found->second, 0, 1);
    if (original < 0) return -1;
    const std::int64_t length = files_->Lseek64(found->second, 0, 2);
    if (length < 0 ||
        files_->Lseek64(found->second, original, 0) != original)
        return -1;
    const std::int64_t base = origin == 0 ? 0 : origin == 1 ? original
                                                             : length;
    if ((offset > 0 &&
         base > std::numeric_limits<std::int64_t>::max() - offset) ||
        (offset < 0 && offset < -base))
        return -1;
    const std::int64_t target = base + offset;
    if (target < 0 || target > length) return -1;
    return files_->Lseek64(found->second, target, 0) == target ? target : -1;
}

std::uint64_t BionicAssets::GetBuffer(std::uint64_t asset) {
    const std::lock_guard lock(mutex_);
    return GetBufferLocked(asset);
}

std::uint64_t BionicAssets::GetBufferLocked(std::uint64_t asset) {
    constexpr std::uint64_t kChunkSize = 1024U * 1024U;
    const auto found = assets_.find(asset);
    if (shutdown_ || found == assets_.end()) return 0;
    const auto existing = buffers_.find(asset);
    if (existing != buffers_.end()) return existing->second;
    const std::int64_t original = files_->Lseek64(found->second, 0, 1);
    const std::int64_t length = files_->Lseek64(found->second, 0, 2);
    if (original < 0 || length < 0 ||
        static_cast<std::uint64_t>(length) >
            std::numeric_limits<std::size_t>::max() ||
        files_->Lseek64(found->second, 0, 0) != 0)
        return 0;
    const std::size_t allocation_size =
        length == 0 ? 1 : static_cast<std::size_t>(length);
    const std::uint64_t buffer = heap_->Allocate(allocation_size);
    if (buffer == 0) {
        (void)files_->Lseek64(found->second, original, 0);
        return 0;
    }
    std::uint64_t offset = 0;
    while (offset < static_cast<std::uint64_t>(length)) {
        const std::uint64_t requested = std::min(
            kChunkSize, static_cast<std::uint64_t>(length) - offset);
        const std::int64_t read =
            files_->Read(found->second, buffer + offset, requested);
        if (read <= 0) break;
        offset += static_cast<std::uint64_t>(read);
    }
    std::string ignored;
    if (offset != static_cast<std::uint64_t>(length) ||
        files_->Lseek64(found->second, original, 0) != original) {
        (void)heap_->Release(buffer, &ignored);
        return 0;
    }
    buffers_.emplace(asset, buffer);
    return buffer;
}

bool BionicAssets::CopyBufferLocalOnly(
    std::uint64_t asset, std::size_t maximum_size,
    std::vector<std::uint8_t>* output, std::string* error) {
    if (output == nullptr || maximum_size == 0) {
        SetError(error, "local asset copy configuration is invalid");
        return false;
    }
    output->clear();
    const std::lock_guard lock(mutex_);
    const auto found = assets_.find(asset);
    if (shutdown_ || found == assets_.end()) {
        SetError(error, "local asset copy handle is invalid");
        return false;
    }
    const std::int64_t original = files_->Lseek64(found->second, 0, 1);
    const std::int64_t length = files_->Lseek64(found->second, 0, 2);
    const bool restored =
        original >= 0 &&
        files_->Lseek64(found->second, original, 0) == original;
    if (original < 0 || length < 0 ||
        static_cast<std::uint64_t>(length) > maximum_size ||
        static_cast<std::uint64_t>(length) >
            (std::numeric_limits<std::size_t>::max)() ||
        !restored) {
        SetError(error, "local asset copy length is invalid or exceeds its bound");
        return false;
    }
    const std::uint64_t buffer = GetBufferLocked(asset);
    if (buffer == 0) {
        SetError(error, "local asset copy buffer is unavailable");
        return false;
    }
    output->resize(static_cast<std::size_t>(length));
    if (length != 0 &&
        !memory_->Read(buffer, output->data(), output->size())) {
        output->clear();
        SetError(error, "local asset copy guest buffer is unreadable");
        return false;
    }
    return true;
}

std::size_t BionicAssets::manager_count() const {
    const std::lock_guard lock(mutex_);
    return manager_ == 0 ? 0 : 1;
}

std::size_t BionicAssets::asset_count() const {
    const std::lock_guard lock(mutex_);
    return assets_.size();
}

bool BionicAssets::Shutdown(std::string* error) {
    const std::lock_guard lock(mutex_);
    if (shutdown_) {
        SetError(error, "bionic asset manager is already shut down");
        return false;
    }
    for (const auto& [handle, descriptor] : assets_) {
        if (files_->Close(descriptor) != 0 ||
            !heap_->Release(handle, error)) {
            SetError(error, "bionic asset cleanup failed");
            return false;
        }
    }
    for (const auto& [asset, buffer] : buffers_) {
        (void)asset;
        if (!heap_->Release(buffer, error)) {
            SetError(error, "bionic asset buffer cleanup failed");
            return false;
        }
    }
    buffers_.clear();
    assets_.clear();
    if (manager_ != 0) {
        if (!heap_->Release(manager_, error)) return false;
        manager_ = 0;
    }
    shutdown_ = true;
    return true;
}

}  // namespace isomr
