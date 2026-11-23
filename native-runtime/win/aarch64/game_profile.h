// Per-game launch profile. A .profile file under games/ is a flat
// `key = value` manifest (# comments) that describes one supported title so
// the entry probe needs no game-specific constants. Selecting a game is
// `ArmrCtrEntryProbe --game <id>`; adding one is dropping a manifest plus its
// payload/data directories and choosing the engine `driver` that owns its
// lifecycle.
#pragma once

#include <string>

namespace isomr {

// Engine driver that owns the game's lifecycle inside the probe. Adding a
// game on an already-supported engine is pure data; a brand-new engine adds a
// driver branch.
enum class GameDriver {
    kUnity,  // UnityPlayer.nativeRender loop (CTR / il2cpp titles).
    kPvz,    // PopCap Sexy/Lawn + Nimble lifecycle (PvZ Free and PvZ2-era).
};

struct GameProfile {
    std::string id;               // Manifest file stem, e.g. "pvz1".
    std::string display_name;     // Human-readable title.
    GameDriver driver = GameDriver::kUnity;
    // Repo-root-relative locations; resolved to absolute paths on load.
    std::string payload_dir;      // e.g. "apps/pvz1/payload".
    std::string data_dir;         // e.g. "apps/pvz1/data".
    std::string root_lib;         // Payload-relative, e.g. "lib/arm64-v8a/libpvz.so".
    std::string package_name;
    std::string package_version;
    std::string package_code_path = "/app";
    // Optional launcher metadata. `exe` names the probe binary relative to the
    // build output dir so a future title can point at a different probe;
    // `desc` is the one-line card subtitle the launcher shows.
    std::string exe = "ArmrCtrEntryProbe.exe";
    // Optional games/-relative Lua lifecycle script (e.g. "pvz1.lua").
    // When present it owns everything after the generic bootstrap.
    std::string script;
    std::string desc;
    bool host_inflate = false;    // Route guest zlib inflate through the host decoder.
    std::uint64_t render_timeout = 600;
};

// Parses a `key = value` manifest. Returns false with *error set on malformed
// input or a missing required field.
bool LoadGameProfile(const std::wstring& path, GameProfile* profile,
                     std::string* error);

// Locates the repository `games/` directory by walking up from the probe
// working directory and the executable's directory. Returns an empty path
// when no games directory is found.
std::wstring FindGamesDirectory();

// Resolves `games/<id>.profile` under the located games directory.
std::wstring ResolveGameProfilePath(const std::wstring& game_id);
}  // namespace isomr
