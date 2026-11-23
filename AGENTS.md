# Guide for contributors and coding agents

ARMR runs the native ARM64 part of Android games inside a Windows process.
Read `README.md` and `docs/ARCHITECTURE.md` first.

## Boundaries

- The only supported route is `native-runtime/win/`: a native Windows
  process. Do not add an Android OS image, emulator, VM, WSL, QEMU, or a
  remote rendering helper.
- Game files never enter git or a hosted model: packages, extracted payloads,
  saves, logs, traces, and captures stay local (see `docs/SECURITY.md`).
  Payload-dependent steps run locally.
- Keep the runtime game-agnostic. Per-game behavior belongs in `games/`
  (profile, `.compat`, Lua script, sites table). Exact-version fast paths
  must verify the instruction signature they replace and fail closed.

## Evidence

- Real progress: `ISOMR_PRESENT_PROBE ... lit=N` with `N > 0`, a growing
  `ISOMR_CTR_FRAMES count`, and input that visibly changes game state.
- Not progress: swap counters, host-painted test patterns, a flag being set.
- Before trusting an A/B result, confirm the two arms actually differed
  (print the effective config) and that the option has a consumer in source.
- Do not compare host addresses across runs (ASLR). Guest offsets into a
  payload are stable.
- Do not hide or blindly suppress crashes; fix the cause and keep the
  diagnostic.

## Before sending a change

1. `.\windows\Build-ArmrAarch64.ps1` (see `docs/BUILDING.md`).
2. `.\windows\Test-NativeAarch64.ps1` under PowerShell 7 must exit 0. It runs
   the zero-payload smoke tests under `/W4 /WX`.
3. If the change touches JNI, GLES, threading, or file handling, rerun the
   games you have locally and report frame and lit-pixel evidence.
