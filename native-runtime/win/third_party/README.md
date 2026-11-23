# Third-party sources

Pinned sources live here locally and are git-ignored:

| Folder | Source | Revision |
| --- | --- | --- |
| `dynarmic/` | https://github.com/azahar-emu/dynarmic | `9d4582339990d4eae53f1dc7160686920fc2075c` |
| `libwebp-1.0.3/` | https://github.com/webmproject/libwebp | `0fe1a89dbf1930fc2554dbe76adad5d962054ead` (v1.0.3) |
| `rapidjson-1.1.0/` | https://github.com/Tencent/rapidjson | `f54b0e47a08782a6131cc3d60f94d038fa6e0a51` (v1.1.0) |

The build verifies each revision and refuses a mismatch. `patches/` holds the
one Dynarmic patch ARMR needs; the build applies it to a clean checkout. See
`docs/BUILDING.md` for clone commands.
