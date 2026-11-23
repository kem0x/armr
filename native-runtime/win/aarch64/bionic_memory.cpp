#include "bionic_memory.h"

#include "guest_heap.h"
#include "import_registry.h"

#include <windows.h>

#include <vector>

namespace isomr {

bool RegisterBionicMemoryImports(Aarch64ImportRegistry* imports,
                                 GuestHeap* heap, std::string* error) {
    if (imports == nullptr || heap == nullptr) {
        if (error != nullptr)
            *error = "bionic memory import dependencies are null";
        return false;
    }

    if (!imports->Register(
            "malloc",
            [imports, heap](Aarch64ImportRegistry::Registers* registers,
                            std::string*) {
                const std::uint64_t want = (*registers)[0];
                // Diagnostic: the CTR startup allocates byte[100000] in a
                // runaway loop (~80k times). Unwind the guest frame-pointer
                // chain to identify the deserialize caller driving it.
                if (want >= 0xF0000 && want <= 0x110000) {
                    static std::uint64_t big_alloc_probe = 0;
                    if ((big_alloc_probe++ % 500) == 0) {
                        std::string frames;
                        char fb[16];
                        std::uint64_t fp =
                            registers->callee_saved_general[10];
                        for (int d = 0; d < 10 && fp != 0; ++d) {
                            std::uint64_t fr[2] = {0, 0};
                            if (!heap->Read(fp, fr, sizeof(fr)) ||
                                fr[1] == 0)
                                break;
                            std::snprintf(fb, sizeof(fb), "%llx ",
                                          (unsigned long long)fr[1]);
                            frames += fb;
                            if (fr[0] <= fp || (fr[0] & 15U) != 0) break;
                            fp = fr[0];
                        }
                        std::fprintf(
                            stderr,
                            "ISOMR_BIGALLOC_CALLER n=%llu size=%llx "
                            "lr=%llx frames=%s\n",
                            (unsigned long long)big_alloc_probe,
                            (unsigned long long)want,
                            (unsigned long long)
                                registers->callee_saved_general[11],
                            frames.c_str());
                    }
                }
                (*registers)[0] = heap->Allocate((*registers)[0]);
                if ((*registers)[0] == 0) {
                    std::fprintf(stderr,
                                 "ISOMR_MALLOC0 size=%llx lr=%llx\n",
                                 (unsigned long long)want,
                                 (unsigned long long)
                                     registers->callee_saved_general.back());
                    const auto snapshot =
                        heap->LastAllocationFailureSnapshot();
                    if (snapshot) {
                        std::fprintf(
                            stderr,
                            "ISOMR_HEAP_FAIL reason=%d req=%llx "
                            "norm=%llx cap=%llx alloc=%llx "
                            "count=%llu free_ext=%llu largest_free=%llx "
                            "committed=%llx\n",
                            static_cast<int>(snapshot->reason),
                            static_cast<unsigned long long>(
                                snapshot->requested_size),
                            static_cast<unsigned long long>(
                                snapshot->normalized_size),
                            static_cast<unsigned long long>(
                                snapshot->capacity),
                            static_cast<unsigned long long>(
                                snapshot->allocated_bytes),
                            static_cast<unsigned long long>(
                                snapshot->allocation_count),
                            static_cast<unsigned long long>(
                                snapshot->free_extent_count),
                            static_cast<unsigned long long>(
                                snapshot->largest_free_extent),
                            static_cast<unsigned long long>(
                                snapshot->committed_bytes));
                    }
                }
                // Arena-bounds audit: PvZ1's boot corrupts FileStream state
                // with heap-base+multi-GB values; catch any allocator that
                // hands out an out-of-arena pointer.
                const std::uint64_t allocation = (*registers)[0];
                if (allocation != 0 &&
                    (allocation & 0xFFFF000000000000ULL) !=
                        (heap->guest_base() & 0xFFFF000000000000ULL)) {
                    std::fprintf(stderr,
                                 "ISOMR_MALLOC_OUT_OF_ARENA ptr=%llx lr=%llx\\n",
                                 static_cast<unsigned long long>(allocation),
                                 static_cast<unsigned long long>(
                                     registers->callee_saved_general.back()));
                }
                heap->AnnotateAllocationFromGuest(
                    (*registers)[0],
                    registers->callee_saved_general.back(),
                    registers->callee_saved_general[10],
                    registers->stack_pointer, imports);
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "calloc",
            [imports, heap](Aarch64ImportRegistry::Registers* registers,
                            std::string*) {
                const std::uint64_t cwant = (*registers)[0] * (*registers)[1];
                (*registers)[0] =
                    heap->Callocate((*registers)[0], (*registers)[1]);
                if ((*registers)[0] == 0) {
                    std::fprintf(stderr,
                                 "ISOMR_CALLOC0 size=%llx lr=%llx\n",
                                 (unsigned long long)cwant,
                                 (unsigned long long)
                                     registers->callee_saved_general.back());
                }
                heap->AnnotateAllocationFromGuest(
                    (*registers)[0],
                    registers->callee_saved_general.back(),
                    registers->callee_saved_general[10],
                    registers->stack_pointer, imports);
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "realloc",
            [imports, heap](Aarch64ImportRegistry::Registers* registers,
                            std::string* handler_error) {
                const std::uint64_t rptr = (*registers)[0];
                const std::uint64_t rwant = (*registers)[1];
                std::uint64_t result = 0;
                if (!heap->Reallocate((*registers)[0], (*registers)[1],
                                      &result, handler_error)) {
                    std::fprintf(stderr,
                                 "ISOMR_REALLOC_FAIL ptr=%llx size=%llx "
                                 "lr=%llx err=%s\n",
                                 (unsigned long long)rptr,
                                 (unsigned long long)rwant,
                                 (unsigned long long)
                                     registers->callee_saved_general.back(),
                                 handler_error ? handler_error->c_str()
                                               : "?");
                    return false;
                }
                if (result == 0 && rwant != 0) {
                    std::fprintf(stderr,
                                 "ISOMR_REALLOC0 ptr=%llx size=%llx lr=%llx "
                                 "path=%s\n",
                                 (unsigned long long)rptr,
                                 (unsigned long long)rwant,
                                 (unsigned long long)
                                     registers->callee_saved_general.back(),
                                 heap->LastReallocZeroPath() != nullptr
                                     ? heap->LastReallocZeroPath()
                                     : "unknown");
                    const auto snapshot =
                        heap->LastAllocationFailureSnapshot();
                    if (snapshot) {
                        std::fprintf(
                            stderr,
                            "ISOMR_HEAP_FAIL reason=%d req=%llx "
                            "norm=%llx cap=%llx alloc=%llx "
                            "count=%llu free_ext=%llu largest_free=%llx "
                            "committed=%llx\n",
                            static_cast<int>(snapshot->reason),
                            static_cast<unsigned long long>(
                                snapshot->requested_size),
                            static_cast<unsigned long long>(
                                snapshot->normalized_size),
                            static_cast<unsigned long long>(
                                snapshot->capacity),
                            static_cast<unsigned long long>(
                                snapshot->allocated_bytes),
                            static_cast<unsigned long long>(
                                snapshot->allocation_count),
                            static_cast<unsigned long long>(
                                snapshot->free_extent_count),
                            static_cast<unsigned long long>(
                                snapshot->largest_free_extent),
                            static_cast<unsigned long long>(
                                snapshot->committed_bytes));
                    }
                }
                (*registers)[0] = result;
                heap->AnnotateAllocationFromGuest(
                    result, registers->callee_saved_general.back(),
                    registers->callee_saved_general[10],
                    registers->stack_pointer, imports);
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "posix_memalign",
            [imports, heap](Aarch64ImportRegistry::Registers* registers,
                            std::string* handler_error) {
                constexpr std::uint64_t kPointerSize =
                    sizeof(std::uint64_t);
                constexpr std::uint64_t kOutOfMemory = 12;
                constexpr std::uint64_t kInvalidArgument = 22;
                const std::uint64_t output = (*registers)[0];
                const std::uint64_t alignment = (*registers)[1];
                const std::uint64_t size = (*registers)[2];
                std::fprintf(stderr,
                             "ISOMR_POSIX_MEMALIGN out=%llx align=%llx "
                             "size=%llx\n",
                             static_cast<unsigned long long>(output),
                             static_cast<unsigned long long>(alignment),
                             static_cast<unsigned long long>(size));
                if (alignment < kPointerSize ||
                    (alignment % kPointerSize) != 0 ||
                    (alignment & (alignment - 1)) != 0) {
                    (*registers)[0] = kInvalidArgument;
                    return true;
                }
                const std::uint64_t allocation =
                    heap->AllocateAligned(size, alignment);
                if (allocation == 0) {
                    const auto snapshot =
                        heap->LastAllocationFailureSnapshot();
                    if (snapshot) {
                        std::fprintf(
                            stderr,
                            "ISOMR_HEAP_FAIL reason=%d req=%llx "
                            "norm=%llx align=%llx cap=%llx alloc=%llx "
                            "count=%llu free_ext=%llu largest_free=%llx "
                            "committed=%llx\n",
                            static_cast<int>(snapshot->reason),
                            static_cast<unsigned long long>(
                                snapshot->requested_size),
                            static_cast<unsigned long long>(
                                snapshot->normalized_size),
                            static_cast<unsigned long long>(
                                snapshot->alignment),
                            static_cast<unsigned long long>(
                                snapshot->capacity),
                            static_cast<unsigned long long>(
                                snapshot->allocated_bytes),
                            static_cast<unsigned long long>(
                                snapshot->allocation_count),
                            static_cast<unsigned long long>(
                                snapshot->free_extent_count),
                            static_cast<unsigned long long>(
                                snapshot->largest_free_extent),
                            static_cast<unsigned long long>(
                                snapshot->committed_bytes));
                    }
                }
                std::fprintf(stderr, "ISOMR_POSIX_MEMALIGN_RESULT ptr=%llx\n",
                             static_cast<unsigned long long>(allocation));
                if (allocation == 0) {
                    (*registers)[0] = kOutOfMemory;
                    return true;
                }
                if (!heap->Write(output, &allocation, sizeof(allocation))) {
                    std::string ignored;
                    (void)heap->Release(allocation, &ignored);
                    if (handler_error != nullptr)
                        *handler_error =
                            "guest posix_memalign output is invalid";
                    return false;
                }
                heap->AnnotateAllocationFromGuest(
                    allocation, registers->callee_saved_general.back(),
                    registers->callee_saved_general[10],
                    registers->stack_pointer, imports);
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    // memalign/aligned_alloc take (alignment, size) and return the pointer
    // directly; il2cpp/Unity call memalign for aligned scratch buffers.
    const auto aligned_alloc_handler =
        [imports, heap](Aarch64ImportRegistry::Registers* registers,
                        std::string* handler_error) {
            (void)handler_error;
            const std::uint64_t alignment = (*registers)[0];
            const std::uint64_t size = (*registers)[1];
            // Diagnostic: CTR startup allocates ~100KB blocks in a runaway
            // loop (~80k times). Unwind the guest frame chain to find the
            // deserialize caller driving it.
            if (size >= 0xF0000 && size <= 0x110000) {
                static std::uint64_t big_alloc_probe = 0;
                if ((big_alloc_probe++ % 500) == 0) {
                    std::string frames;
                    char fb[16];
                    std::uint64_t fp =
                        registers->callee_saved_general[10];
                    for (int d = 0; d < 12 && fp != 0; ++d) {
                        std::uint64_t fr[2] = {0, 0};
                        if (!heap->Read(fp, fr, sizeof(fr)) || fr[1] == 0)
                            break;
                        std::snprintf(fb, sizeof(fb), "%llx ",
                                      (unsigned long long)fr[1]);
                        frames += fb;
                        if (fr[0] <= fp || (fr[0] & 15U) != 0) break;
                        fp = fr[0];
                    }
                    std::fprintf(stderr,
                                 "ISOMR_BIGALLOC_CALLER n=%llu size=%llx "
                                 "align=%llx lr=%llx frames=%s\n",
                                 (unsigned long long)big_alloc_probe,
                                 (unsigned long long)size,
                                 (unsigned long long)alignment,
                                 (unsigned long long)
                                     registers->callee_saved_general[11],
                                 frames.c_str());
                }
            }
            const std::uint64_t allocation =
                heap->AllocateAligned(size, alignment);
            if (allocation != 0) {
                heap->AnnotateAllocationFromGuest(
                    allocation, registers->callee_saved_general.back(),
                    registers->callee_saved_general[10],
                    registers->stack_pointer, imports);
            }
            (*registers)[0] = allocation;
            return true;
        };
    if (!imports->Register("memalign", aligned_alloc_handler, error))
        return false;
    if (!imports->Register("aligned_alloc", aligned_alloc_handler, error))
        return false;
    return imports->Register(
        "free",
        [heap](Aarch64ImportRegistry::Registers* registers,
               std::string* handler_error) {
            // Tolerate double-free / invalid-pointer frees: the engine's
            // C++ code sometimes frees pointers that were allocated through
            // a different allocator path (e.g. GetStringUtfChars buffers).
            // Returning success keeps the boot alive.
            const std::uint64_t free_ptr = (*registers)[0];
            if ((free_ptr & 0xffffffffull) == 0x1475fd0ull ||
                (free_ptr & 0xfffff000ull) == 0x1475000ull) {
                std::fprintf(stderr,
                             "ISOMR_SLTABLE_FREE ptr=%llx lr=%llx\n",
                             static_cast<unsigned long long>(free_ptr),
                             static_cast<unsigned long long>(
                                 (*registers)[30]));
                std::fflush(stderr);
            }
            if (!heap->Release(free_ptr, handler_error)) {
                static std::size_t free_fail_count = 0;
                if (free_fail_count < 20) {
                    ++free_fail_count;
                    std::fprintf(stderr,
                                 "ISOMR_FREE_TOLERATED ptr=%llx\n",
                                 static_cast<unsigned long long>(
                                     (*registers)[0]));
                    std::fflush(stderr);
                }
                if (handler_error != nullptr) handler_error->clear();
            }
            (*registers)[0] = 0;
            return true;
        },
        error);
}

}  // namespace isomr
