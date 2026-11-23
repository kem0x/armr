#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "aarch64_profile_runtime.h"

#include <cstdio>
#include <string>
#include <unordered_set>
#include <utility>

namespace {

const std::unordered_set<std::string> kAndroidHostDependencies{
    "libandroid.so", "libc.so",         "libdl.so",
    "libEGL.so",     "libGLESv2.so",   "liblog.so",
    "libm.so",       "libmediandk.so", "libOpenSLES.so",
    "libz.so"};

void ReportFailure(const char* stage, const std::string& error) {
    std::fprintf(stderr, "ISOMR_PROFILE_PROBE stage=%s result=fail error=%s\n",
                 stage, error.c_str());
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 4 || argv == nullptr || argv[1] == nullptr ||
        argv[2] == nullptr || argv[3] == nullptr) {
        std::fputs(
            "usage: ArmrProfileProbe <root-library> <payload-root> <data-root>\n",
            stderr);
        return 2;
    }

    isomr::Aarch64ProfileRuntimeConfig config;
    config.file_mounts = {{"/app", argv[2]}, {"/data", argv[3]}};
    config.host_dependencies = kAndroidHostDependencies;
    config.fail_closed_unimplemented_imports = true;
    isomr::Aarch64ProfileRuntime runtime(std::move(config));

    std::string error;
    if (!runtime.Load(argv[1], &error)) {
        ReportFailure("load", error);
        return 3;
    }
    std::printf(
        "ISOMR_PROFILE_PROBE stage=load result=pass modules=%zu imports=%zu initializers=%zu\n",
        runtime.module_count(), runtime.import_count(),
        runtime.initializer_count());

    if (!runtime.RunInitializers(&error)) {
        ReportFailure("initializers", error);
        std::string shutdown_error;
        if (!runtime.Shutdown(&shutdown_error))
            ReportFailure("shutdown", shutdown_error);
        return 4;
    }
    std::puts("ISOMR_PROFILE_PROBE stage=initializers result=pass");

    std::uint64_t version = 0;
    if (!runtime.CallJniOnLoad(&version, &error)) {
        ReportFailure("jni-on-load", error);
        std::string shutdown_error;
        if (!runtime.Shutdown(&shutdown_error))
            ReportFailure("shutdown", shutdown_error);
        return 5;
    }
    std::printf(
        "ISOMR_PROFILE_PROBE stage=jni-on-load result=pass version=0x%08llx\n",
        static_cast<unsigned long long>(version));

    if (!runtime.Shutdown(&error)) {
        ReportFailure("shutdown", error);
        return 6;
    }
    std::puts("ISOMR_PROFILE_PROBE=pass");
    return 0;
}
