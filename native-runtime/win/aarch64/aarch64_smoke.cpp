// Native Windows AArch64 execution gate for Armr.
// Synthetic instructions only: no game payload, Android runtime, or VM.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"

namespace A64 = Dynarmic::A64;

static constexpr std::uint64_t kGuestBase = 0x1000000000ULL;
static constexpr std::size_t kGuestSize = 1U << 20;
static constexpr std::uint64_t kAdd = kGuestBase + 0x1000;
static constexpr std::uint64_t kMemory = kGuestBase + 0x1100;
static constexpr std::uint64_t kSvc = kGuestBase + 0x1200;
static constexpr std::uint64_t kData = kGuestBase + 0x4000;
static std::array<std::uint8_t, kGuestSize> g_memory{};

static std::uint8_t *guest_pointer(std::uint64_t address, std::size_t size) {
    if (address < kGuestBase || size > kGuestSize) return nullptr;
    const std::uint64_t offset = address - kGuestBase;
    if (offset > kGuestSize - size) return nullptr;
    return g_memory.data() + offset;
}

template <typename T>
static T read_guest(std::uint64_t address) {
    T value{};
    std::uint8_t *source = guest_pointer(address, sizeof(value));
    if (source != nullptr) std::memcpy(&value, source, sizeof(value));
    return value;
}

template <typename T>
static void write_guest(std::uint64_t address, T value) {
    std::uint8_t *destination = guest_pointer(address, sizeof(value));
    if (destination != nullptr)
        std::memcpy(destination, &value, sizeof(value));
}

class Callbacks final : public A64::UserCallbacks {
public:
    void Attach(A64::Jit *jit) { jit_ = jit; }

    std::optional<std::uint32_t> MemoryReadCode(
        std::uint64_t address) override {
        if (guest_pointer(address, sizeof(std::uint32_t)) == nullptr)
            return std::nullopt;
        return read_guest<std::uint32_t>(address);
    }
    std::uint8_t MemoryRead8(std::uint64_t address) override {
        return read_guest<std::uint8_t>(address);
    }
    std::uint16_t MemoryRead16(std::uint64_t address) override {
        return read_guest<std::uint16_t>(address);
    }
    std::uint32_t MemoryRead32(std::uint64_t address) override {
        return read_guest<std::uint32_t>(address);
    }
    std::uint64_t MemoryRead64(std::uint64_t address) override {
        return read_guest<std::uint64_t>(address);
    }
    A64::Vector MemoryRead128(std::uint64_t address) override {
        A64::Vector value{};
        std::uint8_t *source = guest_pointer(address, value.size());
        if (source != nullptr) std::memcpy(value.data(), source, value.size());
        return value;
    }
    void MemoryWrite8(std::uint64_t address, std::uint8_t value) override {
        write_guest(address, value);
    }
    void MemoryWrite16(std::uint64_t address, std::uint16_t value) override {
        write_guest(address, value);
    }
    void MemoryWrite32(std::uint64_t address, std::uint32_t value) override {
        write_guest(address, value);
    }
    void MemoryWrite64(std::uint64_t address, std::uint64_t value) override {
        write_guest(address, value);
    }
    void MemoryWrite128(std::uint64_t address, A64::Vector value) override {
        std::uint8_t *destination = guest_pointer(address, value.size());
        if (destination != nullptr)
            std::memcpy(destination, value.data(), value.size());
    }

    void InterpreterFallback(std::uint64_t, std::size_t) override {
        jit_->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
    void CallSVC(std::uint32_t immediate) override {
        if (immediate != 7) {
            svc_ok_ = false;
            jit_->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
            return;
        }
        jit_->SetRegister(0, jit_->GetRegister(0) * 2);
    }
    void ExceptionRaised(std::uint64_t, A64::Exception) override {
        jit_->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
    void AddTicks(std::uint64_t) override {}
    std::uint64_t GetTicksRemaining() override { return 1000000ULL; }
    std::uint64_t GetCNTPCT() override { return 0; }
    bool svc_ok() const { return svc_ok_; }

private:
    A64::Jit *jit_ = nullptr;
    bool svc_ok_ = true;
};

static void put_code(std::uint64_t address, std::uint32_t instruction) {
    write_guest(address, instruction);
}

int main() {
    // add x0, x0, x1 ; hlt #0
    put_code(kAdd, 0x8b010000u);
    put_code(kAdd + 4, 0xd4400000u);

    // movz x2,#0x4000 ; movk x2,#0x10,lsl#32 ; str x0,[x2] ;
    // ldr x0,[x2] ; hlt #0
    put_code(kMemory, 0xd2880002u);
    put_code(kMemory + 4, 0xf2c00202u);
    put_code(kMemory + 8, 0xf9000040u);
    put_code(kMemory + 12, 0xf9400040u);
    put_code(kMemory + 16, 0xd4400000u);

    // movz x0,#42 ; svc #7 ; hlt #0
    put_code(kSvc, 0xd2800540u);
    put_code(kSvc + 4, 0xd40000e1u);
    put_code(kSvc + 8, 0xd4400000u);

    Callbacks callbacks;
    A64::UserConfig config;
    config.callbacks = &callbacks;
    A64::Jit jit(config);
    callbacks.Attach(&jit);

    jit.SetPC(kAdd);
    jit.SetRegister(0, 42);
    jit.SetRegister(1, 81);
    jit.Run();
    const std::uint64_t add_result = jit.GetRegister(0);
    std::printf("ISOMR_AARCH64 add=%llu\n",
                static_cast<unsigned long long>(add_result));
    if (add_result != 123) return 1;

    const std::uint64_t canary = 0xfeedbeefcafef00dULL;
    jit.SetPC(kMemory);
    jit.SetRegister(0, canary);
    jit.Run();
    const std::uint64_t memory_result = read_guest<std::uint64_t>(kData);
    std::printf("ISOMR_AARCH64 memory=0x%llx\n",
                static_cast<unsigned long long>(memory_result));
    if (jit.GetRegister(0) != canary || memory_result != canary) return 1;

    jit.SetPC(kSvc);
    jit.Run();
    const std::uint64_t svc_result = jit.GetRegister(0);
    std::printf("ISOMR_AARCH64 svc=%llu\n",
                static_cast<unsigned long long>(svc_result));
    if (!callbacks.svc_ok() || svc_result != 84) return 1;

    std::puts("ISOMR_AARCH64_EXECUTION=pass");
    return 0;
}
