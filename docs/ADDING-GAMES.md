# Adding a game

A game is described by files in `games/`. Nothing about a specific title is
compiled into the runtime except a few exact-version fast paths.

```text
games/<id>.profile   required  manifest: paths, package identity, driver
games/<id>.compat    optional  JNI return-value policy
games/<id>.lua       optional  lifecycle script (frame loop, input, repairs)
```

Before starting, check the game is a candidate: it needs a 64-bit ARM
(`lib/arm64-v8a/`) native engine. Java-only apps, games that require Google
Play Services sign-in or billing to start, and games with anti-tamper or
anti-cheat protection will not work.

## 1. Profile

`games/<id>.profile` is a flat `key = value` file (`#` starts a comment).

| Key | Required | Meaning |
| --- | --- | --- |
| `id` | yes | Must match the file name |
| `name`, `desc` | no | Shown in the launcher |
| `driver` | yes* | `pvz` (PopCap/EA Blast lifecycle) or `unity` (UnityPlayer `nativeRender` loop) |
| `payload_dir` | yes | Extracted package, relative to the repo root (`{localappdata}` is expanded) |
| `data_dir` | yes | Writable app storage, mounted as `/data` |
| `root_lib` | yes | Payload-relative path of the library to start from |
| `package_name`, `package_version` | yes / no | Reported to the game |
| `script` | no | Lua script under `games/` (defaults to `<id>.lua` if present) |
| `host_inflate` | no | `1` routes guest zlib inflate to the host decoder |
| `render_timeout` | no | Seconds before the host stops the frame loop |
| `exe`, `args` | no | Run a different host executable with an argument template (Candy Crush uses this) |
| `import_repack_stored` | no | Importer: rewrite a payload zip/OBB with entries stored uncompressed |
| `import_extract` | no | Importer: `<payload zip>\|<entry>\|<data file>` copies one entry into the data directory |

\* `driver` is not used when `exe`/`args` select a dedicated host.

Then stage a package with
`.\windows\Import-ArmrGame.ps1 -Game <id> -Package <file.apk|xapk>` and run
`.\windows\Start-ArmrGame.ps1 -Game <id>`.

## 2. Make it boot

Run the game and read the first failure. ARMR fails closed, so a missing
piece shows up as an exact diagnostic, for example an unregistered import
name, a JNI call with class/name/signature, or an unsupported GL entry point.
Typical fixes, in the order you should reach for them:

1. **A JNI query the game makes about its platform** (screen size, device
   tier, locale, "is this a tablet"): add a line to `games/<id>.compat`:

   ```text
   # kind|class|method|signature|value
   int|com/ea/blast/DisplayAndroidDelegate|GetCurrentWidth|()I|1280
   boolean|com/example/Platform|isTablet|()Z|false
   void|com/example/Analytics|*|*|
   ```

   `kind` is one of `void boolean int long float double string object
   array`, optionally prefixed `static-`. Empty or `*` matches anything; a
   trailing `*` is a prefix match. The first matching line wins. A user can
   also drop a `compat-overrides.txt` into the game's data directory, which
   is consulted first.
2. **Lifecycle**: decide which exported natives to call, in what order, and
   how to drive frames. Put that in `games/<id>.lua`. See `games/pvz1.lua`
   (an engine whose frames the host drives) and `games/ctr.lua` (Unity).
   Shared helpers live in `games/lib/`. The `armr` table exposed to scripts
   is implemented in `native-runtime/win/aarch64/game_script.cpp`.
3. **A genuinely missing runtime feature** (libc function, GL entry point,
   JNI behavior every game needs): implement it in `native-runtime/win/` and
   add coverage to the matching smoke test so the test gate keeps it.

Keep guest addresses out of scripts' logic. Put them in a per-game sites
table (see `games/pvz1.sites.lua`) and resolve them by name.

## 3. Prove it

Measure real progress, not counters:

- `ISOMR_PRESENT_PROBE ... lit=N` with `N > 0` means the game drew visible
  pixels into a presented frame.
- `ISOMR_CTR_FRAMES count=N` growing means the frame loop is advancing.
- Input works when a click or drag visibly changes game state.

Swap counts and host-painted test patterns are not evidence of rendering.
