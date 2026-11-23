#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "aarch64_profile_runtime.h"
#include "bionic_jumps.h"
#include "bionic_jumps_fixture.h"
#include "bionic_runtime.h"
#include "import_registry.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

bool WriteFixture(const std::wstring& path,
                  const std::vector<std::uint8_t>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = bytes.size() <= MAXDWORD &&
                    WriteFile(file, bytes.data(),
                              static_cast<DWORD>(bytes.size()), &written,
                              nullptr) != FALSE &&
                    written == bytes.size();
    CloseHandle(file);
    return ok;
}

class FixtureFile {
public:
    static std::wstring Create() {
        std::array<wchar_t, MAX_PATH + 1> directory{};
        const DWORD length = GetTempPathW(
            static_cast<DWORD>(directory.size()), directory.data());
        if (length == 0 || length >= directory.size()) return {};
        std::array<wchar_t, MAX_PATH + 1> path{};
        if (GetTempFileNameW(directory.data(), L"isj", 0, path.data()) == 0)
            return {};
        return path.data();
    }
    explicit FixtureFile(std::wstring path) : path_(std::move(path)) {}
    ~FixtureFile() {
        if (!path_.empty()) DeleteFileW(path_.c_str());
    }
    const std::wstring& path() const { return path_; }

private:
    std::wstring path_;
};

int Fail(const char* stage, const std::string& error = {}) {
    std::fprintf(stderr, "ISOMR_BIONIC_JUMPS_FAIL stage=%s error=%s\n",
                 stage, error.c_str());
    return 1;
}

}  // namespace

int main() {
    constexpr std::uint64_t kModuleBase = 0x4800000000ULL;
    constexpr std::uint64_t kHeapBase = 0x730000000000ULL;
    const std::wstring fixture_path = FixtureFile::Create();
    if (fixture_path.empty()) return Fail("temp-path");
    FixtureFile fixture(fixture_path);
    if (!WriteFixture(fixture.path(),
                      isomr::test::MakeBionicJumpsFixture()))
        return Fail("fixture-write");

    isomr::Aarch64ProfileRuntimeConfig config;
    config.heap_base = kHeapBase;
    config.heap_capacity = 512U * 1024U;
    config.import_base = 0x6c0000000000ULL;
    config.first_module_base = kModuleBase;
    config.stack_size = 64U * 1024U;
    config.instruction_budget = 1000000;
    isomr::Aarch64ProfileRuntime profile(std::move(config));
    std::string error;
    if (!profile.Load(fixture.path(), &error) ||
        !profile.RunInitializers(&error) || profile.bionic() == nullptr)
        return Fail("runtime-load", error);
    if (profile.import_count() != 980) {
        std::fprintf(stderr,
                     "ISOMR_BIONIC_JUMPS imports=%zu expected=980\n",
                     profile.import_count());
        for (std::size_t i = 1; i <= profile.bionic()->imports().size();
             ++i) {
            const auto name =
                profile.bionic()->imports().NameForImmediate(
                    static_cast<std::uint32_t>(i));
            if (name) std::fprintf(stderr, "IMP %zu %.*s\n", i,
                                   (int)name->size(), name->data());
        }
        return Fail("runtime-import-count", error);
    }
    if (!profile.bionic()->imports().Resolve("setjmp") ||
        !profile.bionic()->imports().Resolve("longjmp"))
        return Fail("runtime-import-resolution", error);

    isomr::BionicRuntime* runtime = profile.bionic();
    const std::uint64_t buffer = runtime->heap().Callocate(
        1, isomr::BionicJumps::kJmpBufStorageSize);
    const std::uint64_t output =
        runtime->heap().Callocate(1, sizeof(isomr::test::JumpFixtureResult));
    if (buffer == 0 || output == 0) return Fail("fixture-allocation");
    std::array<std::uint64_t, 8> arguments{};
    arguments[0] = buffer;
    arguments[1] = output;
    arguments[2] = kModuleBase + isomr::test::kJumpFixtureSetjmpSlot;
    arguments[3] = kModuleBase + isomr::test::kJumpFixtureLongjmpSlot;
    std::uint64_t call_result = 0;
    if (!profile.Call(profile.entry_point(), arguments, &call_result, &error))
        return Fail("guest-control-flow", error);
    isomr::test::JumpFixtureResult observed{};
    if (call_result != 42 ||
        !runtime->heap().Read(output, &observed, sizeof(observed)) ||
        observed.setjmp_result != 1 || observed.general[0] != 0x1818 ||
        observed.general[1] != buffer || observed.general[2] != output ||
        observed.stack_pointer == 0 ||
        (observed.stack_pointer & 15U) != 0 ||
        observed.general[12] !=
            kModuleBase + isomr::test::kJumpFixtureSetjmpResume)
        return Fail("guest-result");
    for (std::size_t index = 3; index <= 11; ++index) {
        if (observed.general[index] != 18 + index)
            return Fail("guest-general-restore");
    }
    for (std::size_t index = 0; index < observed.vector_low.size(); ++index) {
        if (observed.vector_low[index] != 0x80 + index)
            return Fail("guest-vector-restore");
    }

    const std::uint64_t direct_buffer = runtime->heap().Callocate(
        1, isomr::BionicJumps::kJmpBufStorageSize);
    const std::uint64_t short_buffer = runtime->heap().Callocate(1, 16);
    if (direct_buffer == 0 || short_buffer == 0)
        return Fail("direct-allocation");
    isomr::Aarch64ImportRegistry::Registers saved{};
    saved[0] = direct_buffer;
    saved.stack_pointer = observed.stack_pointer;
    saved.platform_register = observed.general[0];
    for (std::size_t index = 0;
         index < saved.callee_saved_general.size(); ++index)
        saved.callee_saved_general[index] = observed.general[index + 1];
    saved.callee_saved_general[11] =
        kModuleBase + isomr::test::kJumpFixtureEntry;
    for (std::size_t index = 0; index < saved.callee_saved_vector.size();
         ++index)
        saved.callee_saved_vector[index][0] = 0x100 + index;
    if (!runtime->imports().Dispatch("setjmp", &saved, &error) ||
        saved[0] != 0)
        return Fail("direct-setjmp", error);

    isomr::Aarch64ImportRegistry::Registers nonzero{};
    nonzero[0] = direct_buffer;
    nonzero[1] = 7;
    if (!runtime->imports().Dispatch("longjmp", &nonzero, &error) ||
        nonzero[0] != 7 || !nonzero.non_local_return.requested ||
        nonzero.non_local_return.program_counter !=
            kModuleBase + isomr::test::kJumpFixtureEntry ||
        nonzero.non_local_return.stack_pointer != observed.stack_pointer ||
        nonzero.platform_register != saved.platform_register ||
        nonzero.callee_saved_general != saved.callee_saved_general)
        return Fail("direct-nonzero", error);

    std::atomic<bool> cross_thread_rejected{false};
    std::thread cross_thread([&] {
        std::string thread_error;
        if (!runtime->AttachCurrentThread(&thread_error)) return;
        isomr::Aarch64ImportRegistry::Registers cross{};
        cross[0] = direct_buffer;
        cross[1] = 2;
        cross_thread_rejected.store(
            !runtime->imports().Dispatch("longjmp", &cross, &thread_error) &&
                thread_error.find("another guest thread") !=
                    std::string::npos,
            std::memory_order_relaxed);
        (void)runtime->DetachCurrentThread(&thread_error);
    });
    cross_thread.join();
    if (!cross_thread_rejected.load(std::memory_order_relaxed))
        return Fail("cross-thread-rejection");

    isomr::Aarch64ImportRegistry::Registers invalid{};
    if (runtime->imports().Dispatch("setjmp", &invalid, &error))
        return Fail("null-rejection");
    invalid[0] = short_buffer;
    if (runtime->imports().Dispatch("setjmp", &invalid, &error))
        return Fail("range-rejection");
    std::uint8_t corrupt = 0;
    if (!runtime->heap().Write(direct_buffer, &corrupt, sizeof(corrupt)))
        return Fail("corrupt-write");
    invalid = {};
    invalid[0] = direct_buffer;
    invalid[1] = 1;
    if (runtime->imports().Dispatch("longjmp", &invalid, &error) ||
        error.find("invalid or corrupt") == std::string::npos)
        return Fail("corrupt-rejection", error);

    const bool rel_short = runtime->heap().Release(short_buffer, &error);
    const bool rel_direct = runtime->heap().Release(direct_buffer, &error);
    const bool rel_output = runtime->heap().Release(output, &error);
    const bool rel_buffer = runtime->heap().Release(buffer, &error);
    const bool shutdown_ok = profile.Shutdown(&error);
    const std::size_t remaining = runtime->heap().allocation_count();
    if (!rel_short || !rel_direct || !rel_output || !rel_buffer ||
        !shutdown_ok || remaining != 0) {
        std::fprintf(stderr,
                     "ISOMR_BIONIC_JUMPS_CLEANUP rel=%d%d%d%d shutdown=%d remaining=%zu\n",
                     rel_short, rel_direct, rel_output, rel_buffer,
                     shutdown_ok, remaining);
        runtime->heap().DumpLiveAllocations("jumps");
        return Fail("cleanup", error);
    }

    std::puts("ISOMR_BIONIC_JUMPS imports=setjmp,longjmp control=non-local zero=one nonzero=preserved");
    std::puts("ISOMR_BIONIC_JUMPS restore=x18-x30,sp,v8-v15 guest-buffer=256 thread-bound=pass");
    std::puts("ISOMR_BIONIC_JUMPS reject=null,range,corrupt,cross-thread cleanup=pass");
    std::puts("ISOMR_NATIVE_BIONIC_JUMPS=pass");
    return 0;
}
