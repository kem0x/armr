#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>

#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"

namespace A64 = Dynarmic::A64;

namespace {

constexpr std::size_t kMemorySize = 1U << 20;
constexpr std::uint64_t kAddressStride = 1ULL << 32;
constexpr std::uint64_t kFirstBase = 0x3000000000ULL;
constexpr std::uint64_t kCodeOffset = 0x1000;
constexpr std::uint64_t kTlsOffset = 0x2000;
constexpr std::uint64_t kStackOffset = 0x80000;
constexpr std::uint64_t kTlsSlotOffset = 5 * sizeof(std::uint64_t);

struct SharedState {
    HANDLE workers_ready = nullptr;
    HANDLE start_workers = nullptr;
    HANDLE workers_active = nullptr;
    HANDLE run_workers = nullptr;
    std::atomic<long> ready{0};
    std::atomic<long> active{0};
    std::atomic<long> peak_active{0};
};

struct WorkerState {
    SharedState* shared = nullptr;
    unsigned index = 0;
    std::uint64_t guest_base = 0;
    std::uint64_t canary = 0;
    DWORD windows_thread_id = 0;
    std::uint64_t observed_tpidr = 0;
    std::uint64_t observed_tls = 0;
    std::uint64_t observed_stack = 0;
    std::uint64_t halt_reason = 0;
    std::uint64_t callback_address = 0;
    const char* callback_stage = "none";
    bool callbacks_valid = false;
    bool callback_faulted = false;
    bool callback_stopped = false;
    bool stack_pointer_valid = false;
};

class Callbacks final : public A64::UserCallbacks {
public:
    Callbacks(std::array<std::uint8_t, kMemorySize>* memory,
              std::uint64_t guest_base, std::uint64_t terminal)
        : memory_(memory), guest_base_(guest_base), terminal_(terminal) {}
    void Attach(A64::Jit* jit) { jit_ = jit; }

    std::optional<std::uint32_t> MemoryReadCode(
        std::uint64_t address) override {
        std::uint32_t value = 0;
        if (address < guest_base_ + kCodeOffset ||
            address + sizeof(value) > guest_base_ + kCodeOffset + 0x1000)
            return std::nullopt;
        if (!Read(address, &value, sizeof(value))) {
            Abort("code", address);
            return std::nullopt;
        }
        return value;
    }
    std::uint8_t MemoryRead8(std::uint64_t address) override {
        return ReadScalar<std::uint8_t>(address);
    }
    std::uint16_t MemoryRead16(std::uint64_t address) override {
        return ReadScalar<std::uint16_t>(address);
    }
    std::uint32_t MemoryRead32(std::uint64_t address) override {
        return ReadScalar<std::uint32_t>(address);
    }
    std::uint64_t MemoryRead64(std::uint64_t address) override {
        return ReadScalar<std::uint64_t>(address);
    }
    A64::Vector MemoryRead128(std::uint64_t address) override {
        A64::Vector value{};
        if (!Read(address, value.data(), sizeof(value)))
            Abort("read128", address);
        return value;
    }
    void MemoryWrite8(std::uint64_t address, std::uint8_t value) override {
        WriteScalar(address, value);
    }
    void MemoryWrite16(std::uint64_t address, std::uint16_t value) override {
        WriteScalar(address, value);
    }
    void MemoryWrite32(std::uint64_t address, std::uint32_t value) override {
        WriteScalar(address, value);
    }
    void MemoryWrite64(std::uint64_t address, std::uint64_t value) override {
        WriteScalar(address, value);
    }
    void MemoryWrite128(std::uint64_t address, A64::Vector value) override {
        if (!Write(address, value.data(), sizeof(value)))
            Abort("write128", address);
    }

    void InterpreterFallback(std::uint64_t address, std::size_t) override {
        StopOrAbort("fallback", address);
    }
    void CallSVC(std::uint32_t) override { Abort("svc", 0); }
    void ExceptionRaised(std::uint64_t address, A64::Exception) override {
        StopOrAbort("exception", address);
    }
    void AddTicks(std::uint64_t) override {}
    std::uint64_t GetTicksRemaining() override { return 1000000ULL; }
    std::uint64_t GetCNTPCT() override { return 0; }
    bool valid() const { return valid_ && stopped_; }
    bool faulted() const { return !valid_; }
    bool stopped() const { return stopped_; }
    std::uint64_t failure_address() const { return failure_address_; }
    const char* failure_stage() const { return failure_stage_; }

private:
    bool Offset(std::uint64_t address, std::size_t size,
                std::size_t* offset) const {
        if (address < guest_base_ || size > memory_->size()) return false;
        const std::uint64_t relative = address - guest_base_;
        if (relative > memory_->size() - size) return false;
        *offset = static_cast<std::size_t>(relative);
        return true;
    }

    bool Read(std::uint64_t address, void* destination, std::size_t size) const {
        std::size_t offset = 0;
        if (!Offset(address, size, &offset)) return false;
        std::memcpy(destination, memory_->data() + offset, size);
        return true;
    }

    bool Write(std::uint64_t address, const void* source, std::size_t size) {
        std::size_t offset = 0;
        if (!Offset(address, size, &offset)) return false;
        std::memcpy(memory_->data() + offset, source, size);
        return true;
    }

    template <typename T>
    T ReadScalar(std::uint64_t address) {
        T value{};
        if (!Read(address, &value, sizeof(value))) Abort("read", address);
        return value;
    }

    template <typename T>
    void WriteScalar(std::uint64_t address, T value) {
        if (!Write(address, &value, sizeof(value))) Abort("write", address);
    }

    void Abort(const char* stage, std::uint64_t address) {
        valid_ = false;
        failure_stage_ = stage;
        failure_address_ = address;
        if (jit_ != nullptr)
            jit_->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }

    void StopOrAbort(const char* stage, std::uint64_t address) {
        if (address != terminal_) {
            Abort(stage, address);
            return;
        }
        stopped_ = true;
        jit_->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }

    std::array<std::uint8_t, kMemorySize>* memory_;
    std::uint64_t guest_base_;
    std::uint64_t terminal_;
    A64::Jit* jit_ = nullptr;
    bool valid_ = true;
    bool stopped_ = false;
    const char* failure_stage_ = "none";
    std::uint64_t failure_address_ = 0;
};

void PutInstruction(std::array<std::uint8_t, kMemorySize>* memory,
                    std::uint64_t offset, std::uint32_t instruction) {
    std::memcpy(memory->data() + offset, &instruction, sizeof(instruction));
}

void UpdatePeak(SharedState* shared, long value) {
    long observed = shared->peak_active.load(std::memory_order_relaxed);
    while (observed < value &&
           !shared->peak_active.compare_exchange_weak(
               observed, value, std::memory_order_relaxed)) {
    }
}

unsigned __stdcall WorkerMain(void* raw) {
    WorkerState* state = static_cast<WorkerState*>(raw);
    state->windows_thread_id = GetCurrentThreadId();
    auto memory =
        std::make_unique<std::array<std::uint8_t, kMemorySize>>();
    const std::uint64_t code = state->guest_base + kCodeOffset;
    const std::uint64_t tls = state->guest_base + kTlsOffset;
    const std::uint64_t stack = state->guest_base + kStackOffset;
    std::memcpy(memory->data() + kTlsOffset + kTlsSlotOffset,
                &state->canary, sizeof(state->canary));

    // mrs x0,tpidr_el0 ; ldr x1,[x0,#40] ; str x1,[sp] ;
    // ldr x2,[sp] ; hlt #0
    PutInstruction(memory.get(), kCodeOffset, 0xd53bd040U);
    PutInstruction(memory.get(), kCodeOffset + 4, 0xf9401401U);
    PutInstruction(memory.get(), kCodeOffset + 8, 0xf90003e1U);
    PutInstruction(memory.get(), kCodeOffset + 12, 0xf94003e2U);
    PutInstruction(memory.get(), kCodeOffset + 16, 0xd4400000U);

    std::uint64_t tpidr_el0 = tls;
    Callbacks callbacks(memory.get(), state->guest_base, code + 16);
    A64::UserConfig config;
    config.callbacks = &callbacks;
    config.processor_id = state->index;
    config.tpidr_el0 = &tpidr_el0;
    A64::Jit jit(config);
    callbacks.Attach(&jit);
    jit.SetPC(code);
    jit.SetSP(stack);

    if (state->shared->ready.fetch_add(1) + 1 == 2)
        SetEvent(state->shared->workers_ready);
    if (WaitForSingleObject(state->shared->start_workers, 30000) != WAIT_OBJECT_0)
        return 2;

    const long active = state->shared->active.fetch_add(1) + 1;
    UpdatePeak(state->shared, active);
    if (active == 2) SetEvent(state->shared->workers_active);
    if (WaitForSingleObject(state->shared->run_workers, 30000) != WAIT_OBJECT_0) {
        state->shared->active.fetch_sub(1);
        return 3;
    }

    const Dynarmic::HaltReason halt = jit.Run();
    state->shared->active.fetch_sub(1);
    state->observed_tpidr = jit.GetRegister(0);
    state->observed_tls = jit.GetRegister(1);
    state->observed_stack = jit.GetRegister(2);
    state->halt_reason = static_cast<std::uint64_t>(halt);
    state->callbacks_valid = callbacks.valid();
    state->callback_faulted = callbacks.faulted();
    state->callback_stopped = callbacks.stopped();
    state->callback_stage = callbacks.failure_stage();
    state->callback_address = callbacks.failure_address();
    state->stack_pointer_valid = jit.GetSP() == stack;
    return state->callbacks_valid && state->stack_pointer_valid &&
                   state->observed_tpidr == tls &&
                   state->observed_tls == state->canary &&
                   state->observed_stack == state->canary
               ? 0
               : 1;
}

class Handles {
public:
    ~Handles() {
        for (HANDLE handle : handles_)
            if (handle != nullptr) CloseHandle(handle);
    }
    void Add(HANDLE handle) { handles_[used_++] = handle; }

private:
    std::array<HANDLE, 6> handles_{};
    std::size_t used_ = 0;
};

int Fail(const char* stage) {
    std::fprintf(stderr, "ISOMR_THREAD failure=%s windows=%lu\n", stage,
                 static_cast<unsigned long>(GetLastError()));
    return 1;
}

}  // namespace

int main() {
    Handles handles;
    SharedState shared;
    shared.workers_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    shared.start_workers = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    shared.workers_active = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    shared.run_workers = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    handles.Add(shared.workers_ready);
    handles.Add(shared.start_workers);
    handles.Add(shared.workers_active);
    handles.Add(shared.run_workers);
    if (shared.workers_ready == nullptr || shared.start_workers == nullptr ||
        shared.workers_active == nullptr || shared.run_workers == nullptr)
        return Fail("events");
    std::array<WorkerState, 2> workers{};
    std::array<HANDLE, 2> threads{};
    std::size_t created_threads = 0;
    for (std::size_t index = 0; index < workers.size(); ++index) {
        workers[index].shared = &shared;
        workers[index].index = static_cast<unsigned>(index);
        workers[index].guest_base = kFirstBase + index * kAddressStride;
        workers[index].canary = 0x1111222233334400ULL + index;
        const uintptr_t thread = _beginthreadex(
            nullptr, 0, WorkerMain, &workers[index], 0, nullptr);
        if (thread == 0) {
            SetEvent(shared.start_workers);
            SetEvent(shared.run_workers);
            if (created_threads != 0)
                WaitForMultipleObjects(static_cast<DWORD>(created_threads),
                                       threads.data(), TRUE, 30000);
            return Fail("thread-create");
        }
        threads[index] = reinterpret_cast<HANDLE>(thread);
        handles.Add(threads[index]);
        ++created_threads;
    }

    bool synchronization_ok =
        WaitForSingleObject(shared.workers_ready, 30000) == WAIT_OBJECT_0;
    SetEvent(shared.start_workers);
    synchronization_ok = synchronization_ok &&
        WaitForSingleObject(shared.workers_active, 30000) == WAIT_OBJECT_0;
    SetEvent(shared.run_workers);
    const DWORD joined = WaitForMultipleObjects(
        static_cast<DWORD>(threads.size()), threads.data(), TRUE, 30000);
    if (!synchronization_ok || joined != WAIT_OBJECT_0)
        return Fail("synchronize-or-join");

    std::array<DWORD, 2> exit_codes{};
    for (std::size_t index = 0; index < threads.size(); ++index)
        if (!GetExitCodeThread(threads[index], &exit_codes[index]))
            return Fail("thread-exit-code");
    if (exit_codes[0] != 0 || exit_codes[1] != 0) {
        std::fprintf(stderr,
                     "ISOMR_THREAD exit0=%lu exit1=%lu valid=%d,%d "
                     "sp=%d,%d fault=%d,%d stopped=%d,%d halt=0x%llx,0x%llx "
                     "stage=%s,%s address=0x%llx,0x%llx "
                     "tpidr=0x%llx,0x%llx tls=0x%llx,0x%llx "
                     "stack=0x%llx,0x%llx\n",
                     static_cast<unsigned long>(exit_codes[0]),
                     static_cast<unsigned long>(exit_codes[1]),
                     workers[0].callbacks_valid ? 1 : 0,
                     workers[1].callbacks_valid ? 1 : 0,
                     workers[0].stack_pointer_valid ? 1 : 0,
                     workers[1].stack_pointer_valid ? 1 : 0,
                     workers[0].callback_faulted ? 1 : 0,
                     workers[1].callback_faulted ? 1 : 0,
                     workers[0].callback_stopped ? 1 : 0,
                     workers[1].callback_stopped ? 1 : 0,
                     static_cast<unsigned long long>(workers[0].halt_reason),
                     static_cast<unsigned long long>(workers[1].halt_reason),
                     workers[0].callback_stage,
                     workers[1].callback_stage,
                     static_cast<unsigned long long>(workers[0].callback_address),
                     static_cast<unsigned long long>(workers[1].callback_address),
                     static_cast<unsigned long long>(workers[0].observed_tpidr),
                     static_cast<unsigned long long>(workers[1].observed_tpidr),
                     static_cast<unsigned long long>(workers[0].observed_tls),
                     static_cast<unsigned long long>(workers[1].observed_tls),
                     static_cast<unsigned long long>(workers[0].observed_stack),
                     static_cast<unsigned long long>(workers[1].observed_stack));
        return Fail("worker-result");
    }
    if (shared.peak_active.load() != 2 ||
        workers[0].windows_thread_id == workers[1].windows_thread_id)
        return Fail("concurrency-or-identity");

    std::printf("ISOMR_THREAD workers=2 peak=2 ids=distinct\n");
    std::printf("ISOMR_THREAD tls0=0x%llx tls1=0x%llx\n",
                static_cast<unsigned long long>(workers[0].observed_tpidr),
                static_cast<unsigned long long>(workers[1].observed_tpidr));
    std::printf("ISOMR_THREAD canary0=0x%llx canary1=0x%llx\n",
                static_cast<unsigned long long>(workers[0].observed_tls),
                static_cast<unsigned long long>(workers[1].observed_tls));
    std::puts("ISOMR_THREAD stack=pass joins=pass");
    std::puts("ISOMR_NATIVE_THREADS=pass");
    return 0;
}
