#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "bionic_runtime.h"
#include "elf64_modules.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

bool SafeName(std::string_view name) {
    return !name.empty() && name.size() <= 1024 &&
           std::all_of(name.begin(), name.end(), [](unsigned char value) {
               return value >= 0x21 && value <= 0x7e;
           });
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || argv == nullptr || argv[1] == nullptr) {
        std::fputs("usage: ArmrElf64Probe <aarch64-elf>\n", stderr);
        return 2;
    }

    const auto executor_factory =
        [](std::uint64_t, std::string*) -> isomr::BionicThreads::GuestCall {
        return {};
    };
    constexpr std::uint64_t kHeapBase = 0x730000000000ULL;
    constexpr std::uint64_t kImportBase = 0x6a0000000000ULL;
    isomr::BionicRuntime runtime(kHeapBase, 4U * 1024U * 1024U,
                                 kImportBase, executor_factory);
    std::string error;
    if (!runtime.Initialize(&error)) {
        std::fprintf(stderr, "ISOMR_ELF_PROBE runtime=%s\n", error.c_str());
        return 1;
    }

    std::set<std::string> requested;
    std::set<std::string> supported;
    std::set<std::string> missing;
    std::uint64_t fallback = 0x6b0000000000ULL;
    const isomr::Elf64ImportResolver resolve =
        [&](std::string_view name) -> std::optional<std::uint64_t> {
        if (!SafeName(name)) return std::nullopt;
        requested.emplace(name);
        const std::optional<std::uint64_t> address =
            runtime.imports().Resolve(name);
        if (address) {
            supported.emplace(name);
            return address;
        }
        missing.emplace(name);
        fallback += 8;
        return fallback;
    };
    const std::unordered_set<std::string> host_dependencies{
        "libandroid.so", "libc.so",       "libdl.so",
        "libEGL.so",     "libGLESv2.so", "liblog.so",
        "libm.so",       "libmediandk.so", "libOpenSLES.so",
        "libz.so"};
    isomr::Elf64ModuleSet modules(resolve, host_dependencies);
    const isomr::Elf64ModuleLoadResult loaded =
        modules.LoadRoot(argv[1], 0x4800000000ULL);
    if (loaded.root == nullptr) {
        std::fprintf(stderr, "ISOMR_ELF_PROBE load=fail error=%s\n",
                     loaded.error.c_str());
        return 1;
    }

    std::printf(
        "ISOMR_ELF_PROBE load=pass modules=%zu needed=%zu host-needed=%zu imports=%zu supported=%zu missing=%zu\n",
        loaded.modules_loaded, loaded.root->needed().size(),
        modules.host_dependency_count(), requested.size(), supported.size(),
        missing.size());
    for (const std::string& name : loaded.root->needed())
        std::printf("NEEDED %s\n", name.c_str());
    for (const std::string& name : missing)
        std::printf("MISSING %s\n", name.c_str());
    return 0;
}
