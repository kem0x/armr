#pragma once

#include <cstdint>

namespace isomr {

// The currently-executing guest thread's stack bounds, recorded by the
// session each time it begins running guest code on a host thread. Bionic's
// pthread_getattr_np / pthread_attr_getstack read these so the il2cpp Boehm
// GC can discover a thread's real stack range for conservative scanning.
inline thread_local std::uint64_t g_guest_thread_stack_base = 0;
inline thread_local std::uint64_t g_guest_thread_stack_size = 0;

}  // namespace isomr
