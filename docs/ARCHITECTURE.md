# Architecture

ARMR runs the native (C/C++) part of an Android game inside an ordinary
Windows process. There is no Android system image, emulator, or virtual
machine. The game's 64-bit ARM libraries are loaded as-is, their code is
recompiled to x64 on the fly, and every call they make out to "Android" lands
in a Windows implementation that ARMR provides.

```text
ArmrLauncher.exe (picker) ──spawns──▶ ArmrCtrEntryProbe.exe --game <id>
                                          │
         games/<id>.profile ──────────────┤  paths, package identity, driver
         games/<id>.compat  ──────────────┤  JNI return-value policy
         games/<id>.lua     ──────────────┤  lifecycle script (optional)
                                          ▼
 ┌──────────────────────────── one Windows process ───────────────────────────┐
 │  ELF64 loader ─▶ guest address space + heap ─▶ Dynarmic JIT (A64 → x64)     │
 │        │                                            │                       │
 │        ▼                                            ▼                       │
 │  import registry: every symbol the game imports resolves to a host function │
 │   ├─ bionic libc: files, threads, futexes, time, stdio, locale, math, TLS…  │
 │   ├─ dynamic linker: dlopen/dlsym across the game's own libraries           │
 │   ├─ JNI: JavaVM/JNIEnv with a compatibility object model (no real JVM)     │
 │   ├─ GLES 2/3 + EGL ─▶ ANGLE ─▶ Direct3D 11                                 │
 │   ├─ OpenSL ES: buffers are accepted and paced, then discarded (no sound)   │
 │   └─ assets, OBB and app storage mapped onto apps/<id>/{payload,data}       │
 │                                                                             │
 │  Win32 game window: title bar, aspect-fit viewport, fullscreen, input       │
 └─────────────────────────────────────────────────────────────────────────────┘
```

Source lives in `native-runtime/win/`:

| Area | Files |
| --- | --- |
| Entry points | `aarch64/ctr_entry_probe.cpp` (`--game` host), `aarch64/candy_entry_probe.cpp`, `../../launcher/main.cpp` |
| Profiles and scripts | `aarch64/game_profile.*`, `aarch64/game_script.cpp` (LuaJIT host) |
| Loading | `aarch64/elf64_loader.cpp`, `elf64_modules.cpp`, `bionic_dynamic_loader.cpp` |
| Execution | `aarch64/aarch64_guest_session.cpp`, `aarch64_profile_runtime.cpp` (Dynarmic sessions, breakpoints, budgets) |
| Memory | `aarch64/guest_address_space.cpp`, `guest_heap.cpp`, `bionic_mappings.cpp` |
| libc | `aarch64/bionic_*.cpp` |
| Imports | `aarch64/import_registry.cpp` |
| Java/JNI | `aarch64/guest_jni.cpp` |
| Graphics | `aarch64/native_gles.cpp` |
| Audio (paced, silent) | `aarch64/native_opensles.cpp` |
| Window | `ui/game_window.cpp` |
| Host fast paths | `aarch64/host_inflate.*`, `pvz_inflate_host.cpp`, `candy_*_host.cpp` |

## Key ideas

**Native code only.** Most Android games keep their engine in native
libraries and use Java only as glue (lifecycle callbacks, a few platform
queries). ARMR runs the native libraries and fakes the Java side: `guest_jni`
hands the game a `JNIEnv` whose classes, methods, and objects are synthesized
on demand. Calls nothing has taught it to answer fail closed, unless a
per-game `.compat` file supplies a return value.

**Fail closed.** An unknown import, JNI method, or GL entry point stops the
run with an exact diagnostic instead of returning garbage. Most compatibility
work is reading that diagnostic and implementing the missing piece.

**Profiles, not hardcoded games.** A game is described by data in `games/`:
a `.profile` manifest (paths, package identity, engine driver), an optional
`.compat` JNI policy, and an optional Lua script that drives the lifecycle
(which exports to call in which order, frame loop, input). Adding a title on
an engine ARMR already drives is a data change. See
[ADDING-GAMES.md](ADDING-GAMES.md).

**Exact-version host fast paths.** Some hot guest routines (zlib inflate,
libvorbis decoding, libwebp 1.0.3, a per-byte stream loop) run far faster as
host code. Each fast path checks the exact instruction signature of the
routine it replaces and refuses to engage on any mismatch, so a game update
falls back to the slow-but-correct guest path instead of misbehaving.

## Limits

- **Per-game work.** Each game needs its lifecycle understood, and its
  scripts reference offsets inside that exact game version. A game update can
  break a profile.
- **No Google Play Services.** Sign-in, billing, and device attestation do
  not exist, so online and anti-cheat-protected games will not work.
- **No sound.** The OpenSL ES layer drives the game's audio callbacks at the
  right pace but does not yet hand samples to a Windows audio device.
- **Performance.** The JIT runs every guest load/store through a checked
  memory callback. Loading-heavy games spend minutes in startup, and some
  have multi-second hitches. A direct page-table mode that skips the
  callback exists only in the Candy host (`-DirectHeapPageTable`) and is not
  yet the default.
- **Single input path.** Pointer input is queued and delivered between guest
  calls, never reentrantly, so long guest calls delay input.
