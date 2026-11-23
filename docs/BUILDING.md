# Building ARMR

ARMR builds natively on Windows with MSVC. There is no WSL, MinGW, or Linux
step anywhere in the build.

## Requirements

| Tool | Notes |
| --- | --- |
| Windows 10/11, x64 | |
| Visual Studio 2022 or newer | "Desktop development with C++" workload (MSVC, Windows SDK) |
| CMake 3.20+ and Ninja | Both must be on `PATH` (the VS installer can provide them) |
| PowerShell 7 (`pwsh`) | Windows PowerShell 5.1 also works for the build itself |
| Git | Used to verify pinned dependency revisions |
| JDK 17+ | Only `javac` is used, to compile one small JNI test class |
| [vcpkg](https://github.com/microsoft/vcpkg) | Supplies ANGLE, LuaJIT, Boost headers and Dear ImGui |

Install the vcpkg packages (x64-windows triplet):

```powershell
vcpkg install angle:x64-windows luajit:x64-windows boost-headers:x64-windows "imgui[dx11-binding,win32-binding]:x64-windows"
```

All four must live in the same vcpkg `installed\x64-windows` root, which you
pass as `-AngleRoot`.

## Pinned sources

Three source dependencies are pinned to exact revisions and checked by the
build. Put them under `native-runtime/win/third_party/` (git-ignored), or pass
their paths with the parameters shown further down.

```powershell
$tp = "native-runtime\win\third_party"

# Dynarmic (AArch64 -> x64 JIT)
git clone https://github.com/azahar-emu/dynarmic "$tp\dynarmic"
git -C "$tp\dynarmic" checkout 9d4582339990d4eae53f1dc7160686920fc2075c
git -C "$tp\dynarmic" submodule update --init --recursive

# libwebp 1.0.3 (exact version some games embed)
git clone https://github.com/webmproject/libwebp "$tp\libwebp-1.0.3"
git -C "$tp\libwebp-1.0.3" checkout 0fe1a89dbf1930fc2554dbe76adad5d962054ead

# RapidJSON 1.1.0
git clone https://github.com/Tencent/rapidjson "$tp\rapidjson-1.1.0"
git -C "$tp\rapidjson-1.1.0" checkout f54b0e47a08782a6131cc3d60f94d038fa6e0a51
```

ARMR needs one small Dynarmic patch
(`native-runtime/win/third_party/patches/dynarmic-a64-hook-hint-instructions.patch`,
which forwards the A64 hint-instruction hook option into the translator). The
build applies it to a clean checkout automatically and refuses a checkout
with any other local changes.

## Build

```powershell
.\windows\Build-ArmrAarch64.ps1 `
  -BoostInclude "<vcpkg>\installed\x64-windows\include" `
  -AngleRoot    "<vcpkg>\installed\x64-windows" `
  -JavaHome     "C:\Program Files\Eclipse Adoptium\jdk-21..."
```

`-DynarmicSource`, `-WebP103Source` and `-RapidJson110Source` default to the
`third_party` folders above. Every parameter can also come from an environment
variable (`ISOMR_DYNARMIC_SOURCE`, `ISOMR_BOOST_INCLUDE`, `ISOMR_ANGLE_ROOT`,
`JAVA_HOME`, `ISOMR_WEBP103_SOURCE`, `ISOMR_RAPIDJSON110_SOURCE`).

Outputs land in `native-runtime/win/build/aarch64/`:

| Executable | Purpose |
| --- | --- |
| `ArmrLauncher.exe` | Game picker GUI (run it through `Armr.cmd`) |
| `ArmrCtrEntryProbe.exe` | Profile-driven game host: `--game <id>` |
| `ArmrCandyEntryProbe.exe` | Dedicated Candy Crush host |
| `Armr*Smoke.exe` | Zero-payload self-tests used by the test gate |

`-Clean` wipes the build directory first. `-CoreOnly` skips the game host and
is what the test gate uses.

## Test gate

`windows\Test-NativeAarch64.ps1` takes the same parameters, rebuilds with
`-CoreOnly` under `/W4 /WX`, and runs every zero-payload smoke test (ELF
loading, the libc layer, threads, JNI, GLES, audio). It needs no game files.
Run it under PowerShell 7.

Rebuild without `-CoreOnly` afterwards to get the game host back.

## Troubleshooting

- **`\<folder> was unexpected at this time`**: a `PATH` entry contains a stray
  `"`. The build script strips quotes from `PATH` for its own process, but
  other tools may still trip on it; fix the entry in your environment.
- **"Dynarmic source has local changes"**: start from a clean checkout at the
  pinned revision; the build applies the ARMR patch itself.
- **Candy probe "provenance is not current"**: the Candy host refuses to run
  a binary built from different sources. Rebuild.
