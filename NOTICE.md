# Third-party notices

ARMR itself is MIT-licensed (see `LICENSE`). It builds against the following
third-party components, none of which are vendored in this repository; the
build fetches or locates them as described in `docs/BUILDING.md`.

| Component | Use | License |
| --- | --- | --- |
| [Dynarmic](https://github.com/azahar-emu/dynarmic) | AArch64 to x64 dynamic recompiler | 0BSD |
| [ANGLE](https://chromium.googlesource.com/angle/angle) | OpenGL ES / EGL on Direct3D 11 | BSD-3-Clause |
| [libwebp 1.0.3](https://github.com/webmproject/libwebp) | Exact-version WebP decoder for games that embed it | BSD-3-Clause |
| [RapidJSON 1.1.0](https://github.com/Tencent/rapidjson) | Exact-layout JSON parser mirror (off by default) | MIT |
| [Dear ImGui](https://github.com/ocornut/imgui) | Launcher UI | MIT |
| [Boost](https://www.boost.org/) | Headers used by Dynarmic | BSL-1.0 |

`native-runtime/win/third_party/patches/` contains a small patch to Dynarmic,
distributed under Dynarmic's license.

## Games and trademarks

ARMR does not include, download, or distribute any game, APK, OBB, asset, or
game library. You must supply your own legally obtained copy of each game.
Game names used in profiles and documentation (Plants vs. Zombies, Candy
Crush Saga, Cut the Rope) are trademarks of their respective owners and are
used only to identify compatibility. ARMR is not affiliated with or endorsed
by Electronic Arts, King, ZeptoLab, or Google.
