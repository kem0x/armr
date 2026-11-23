#include "game_profile.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace isomr {
namespace {

std::string Trim(std::string value) {
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(),
                             [&](unsigned char c) { return !is_space(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](unsigned char c) { return !is_space(c); })
                    .base(),
                value.end());
    return value;
}

bool ParseBool(const std::string& value) {
    return value == "1" || value == "true" || value == "yes" ||
           value == "on";
}

bool ApplyField(const std::string& key, const std::string& value,
                GameProfile* profile, std::string* error) {
    if (key == "id") {
        profile->id = value;
    } else if (key == "name") {
        profile->display_name = value;
    } else if (key == "driver") {
        if (value == "pvz") {
            profile->driver = GameDriver::kPvz;
        } else if (value == "unity") {
            profile->driver = GameDriver::kUnity;
        } else {
            *error = "unknown driver '" + value + "'";
            return false;
        }
    } else if (key == "payload_dir") {
        profile->payload_dir = value;
    } else if (key == "data_dir") {
        profile->data_dir = value;
    } else if (key == "root_lib") {
        profile->root_lib = value;
    } else if (key == "package_name") {
        profile->package_name = value;
    } else if (key == "package_version") {
        profile->package_version = value;
    } else if (key == "package_code_path") {
        profile->package_code_path = value;
    } else if (key == "exe") {
        profile->exe = value;
    } else if (key == "script") {
        profile->script = value;
    } else if (key == "desc") {
        profile->desc = value;
    } else if (key == "host_inflate") {
        profile->host_inflate = ParseBool(value);
    } else if (key == "render_timeout") {
        try {
            profile->render_timeout = std::stoull(value);
        } catch (const std::exception&) {
            *error = "invalid render_timeout '" + value + "'";
            return false;
        }
    } else {
        // Unknown keys are ignored so manifests can carry forward-only
        // metadata without breaking older probes.
        return true;
    }
    return true;
}

std::wstring NarrowToWide(const std::string& value) {
    std::wstring wide;
    wide.reserve(value.size());
    for (const char c : value) wide.push_back(static_cast<wchar_t>(c));
    return wide;
}

}  // namespace

bool LoadGameProfile(const std::wstring& path, GameProfile* profile,
                     std::string* error) {
    std::ifstream in(path);
    if (!in) {
        *error = "cannot open game profile";
        return false;
    }
    GameProfile parsed;
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const std::string trimmed = Trim(line);
        if (trimmed.empty()) continue;
        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            *error = "line " + std::to_string(line_no) + " is not key=value";
            return false;
        }
        const std::string key = Trim(trimmed.substr(0, eq));
        const std::string value = Trim(trimmed.substr(eq + 1));
        if (!ApplyField(key, value, &parsed, error)) {
            *error = "line " + std::to_string(line_no) + ": " + *error;
            return false;
        }
    }
    if (parsed.payload_dir.empty() || parsed.data_dir.empty() ||
        parsed.root_lib.empty() || parsed.package_name.empty()) {
        *error =
            "profile requires payload_dir, data_dir, root_lib, package_name";
        return false;
    }
    *profile = parsed;
    return true;
}

std::wstring FindGamesDirectory() {
    // Walk up from the current working directory and the executable's
    // directory looking for a `games` folder.
    wchar_t cwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    wchar_t exe_dir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_dir, MAX_PATH);
    for (int start = 0; start < 2; ++start) {
        std::wstring dir = start == 0 ? std::wstring(cwd) : std::wstring(exe_dir);
        for (;;) {
            const std::wstring candidate = dir + L"\\games";
            const DWORD attr = GetFileAttributesW(candidate.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES &&
                (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                return candidate;
            }
            const std::size_t slash = dir.find_last_of(L"\\/");
            if (slash == std::wstring::npos || slash == 0) break;
            dir = dir.substr(0, slash);
        }
    }
    return std::wstring();
}

std::wstring ResolveGameProfilePath(const std::wstring& game_id) {
    const std::wstring games = FindGamesDirectory();
    if (games.empty()) return std::wstring();
    return games + L"\\" + game_id + L".profile";
}
}  // namespace isomr
