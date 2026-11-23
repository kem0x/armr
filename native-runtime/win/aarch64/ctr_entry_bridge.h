#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace isomr {

class Aarch64ProfileRuntime;

struct CtrEntryResult {
    bool unity_loaded = false;
    std::size_t module_count = 0;
    std::size_t registered_native_count = 0;
};

class CtrEntryBridge {
public:
    static bool LoadUnity(Aarch64ProfileRuntime* runtime,
                          std::string_view guest_library_directory,
                          CtrEntryResult* result, std::string* error);
    static bool UnloadUnity(Aarch64ProfileRuntime* runtime,
                            std::string* error);
    static bool RenderUnityOnce(Aarch64ProfileRuntime* runtime,
                                bool* rendered, std::string* error);
    // Frame phase: renders one frame via UnityPlayer.nativeRender without
    // repeating the initJni/nativeResume lifecycle. Drive repeatedly.
    static bool RenderUnityFrame(Aarch64ProfileRuntime* runtime,
                                 bool* rendered, std::string* error);
    static bool InvokeRunnable(Aarch64ProfileRuntime* runtime,
                               std::uint64_t native_handle,
                               std::string* error);
};

}  // namespace isomr
