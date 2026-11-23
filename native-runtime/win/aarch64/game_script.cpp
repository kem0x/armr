// Per-game Lua lifecycle scripts — see game_script.h for the model.
#include "game_script.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <sstream>
#include <vector>

#include <Windows.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "aarch64_profile_runtime.h"
#include "bionic_runtime.h"
#include "bionic_threads.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "guest_jni.h"
#include "import_registry.h"
#include "native_gles.h"

namespace isomr {

// Defined in bionic_process.cpp; seeds a guest-visible environment variable
// (getenv/setenv round-trip through g_guest_env) so a game can be given a
// Boehm/GC tuning knob before its runtime initializes.
std::uint64_t GuestEnvSetVar(GuestAddressSpace* memory,
                             const std::string& name,
                             const std::string& value, bool overwrite);

namespace {

constexpr const char* kSelfKey = "armr.self";
constexpr const char* kStateMeta = "armr.state";

GameScript* SelfFrom(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kSelfKey);
    GameScript* self = static_cast<GameScript*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return self;
}

bool LuaError(lua_State* L, std::string* error, const char* what) {
    const char* message = lua_tostring(L, -1);
    if (error != nullptr)
        *error = std::string(what) + ": " + (message ? message : "?");
    lua_pop(L, 1);
    return false;
}

std::uint64_t CheckAddress(lua_State* L, int index) {
    return static_cast<std::uint64_t>(luaL_checknumber(L, index));
}

// ---- paused-state proxy -------------------------------------------------

struct StateBox {
    Aarch64PausedExecutionState* state = nullptr;
    bool live = false;
};

StateBox* CheckState(lua_State* L, int index) {
    StateBox* box = static_cast<StateBox*>(luaL_checkudata(L, index, kStateMeta));
    if (box == nullptr || !box->live || box->state == nullptr)
        luaL_error(L, "breakpoint state is only valid inside its callback");
    return box;
}

int StateIndex(lua_State* L) {
    StateBox* box = static_cast<StateBox*>(luaL_checkudata(L, 1, kStateMeta));
    const char* field = luaL_checkstring(L, 2);
    if (std::strcmp(field, "x") == 0) {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            StateBox* box = CheckState(L, 1);
            const int index = static_cast<int>(luaL_checkinteger(L, 2));
            if (index < 0 || index > 30)
                return luaL_error(L, "register index out of range");
            lua_pushinteger(L, static_cast<lua_Integer>(
                                   box->state->registers[index]));
            return 1;
        });
        return 1;
    }
    if (std::strcmp(field, "set_x") == 0) {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            StateBox* box = CheckState(L, 1);
            const int index = static_cast<int>(luaL_checkinteger(L, 2));
            if (index < 0 || index > 30)
                return luaL_error(L, "register index out of range");
            box->state->registers[index] = CheckAddress(L, 3);
            return 0;
        });
        return 1;
    }
    if (std::strcmp(field, "skip") == 0) {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            StateBox* box = CheckState(L, 1);
            box->state->program_counter += sizeof(std::uint32_t);
            return 0;
        });
        return 1;
    }
    if (std::strcmp(field, "ret") == 0) {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            StateBox* box = CheckState(L, 1);
            const std::uint64_t value =
                lua_gettop(L) >= 2 ? CheckAddress(L, 2) : 0;
            box->state->registers[0] = value;
            box->state->program_counter = box->state->registers[30];
            return 0;
        });
        return 1;
    }
    if (std::strcmp(field, "branch") == 0) {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            StateBox* box = CheckState(L, 1);
            box->state->program_counter = CheckAddress(L, 2);
            return 0;
        });
        return 1;
    }
    if (std::strcmp(field, "sp") == 0) {
        lua_pushinteger(L, static_cast<lua_Integer>(box->state->stack_pointer));
        return 1;
    }
    if (std::strcmp(field, "pc") == 0) {
        lua_pushinteger(L,
                        static_cast<lua_Integer>(box->state->program_counter));
        return 1;
    }
    if (std::strcmp(field, "lr") == 0) {
        lua_pushinteger(L,
                        static_cast<lua_Integer>(box->state->registers[30]));
        return 1;
    }
    return luaL_error(L, "unknown state field '%s'", field);
}

int StateNewIndex(lua_State* L) {
    StateBox* box = CheckState(L, 1);
    const char* field = luaL_checkstring(L, 2);
    if (std::strcmp(field, "sp") == 0) {
        box->state->stack_pointer = CheckAddress(L, 3);
        return 0;
    }
    if (std::strcmp(field, "pc") == 0) {
        box->state->program_counter = CheckAddress(L, 3);
        return 0;
    }
    return luaL_error(L, "unknown state field '%s'", field);
}

// ---- armr.* bindings ----------------------------------------------------

int LuaLog(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    std::fputs("LUA ", stdout);
    std::fputs(message, stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    return 0;
}

int LuaLogErr(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    std::fputs("LUA ", stderr);
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    return 0;
}

int LuaHex(lua_State* L) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "0x%llx",
                  static_cast<unsigned long long>(CheckAddress(L, 1)));
    lua_pushstring(L, buffer);
    return 1;
}

int LuaBand(lua_State* L) {
    const int top = lua_gettop(L);
    std::uint64_t value = CheckAddress(L, 1);
    for (int i = 2; i <= top; ++i) value &= CheckAddress(L, i);
    lua_pushinteger(L, static_cast<lua_Integer>(value));
    return 1;
}

int LuaBor(lua_State* L) {
    const int top = lua_gettop(L);
    std::uint64_t value = CheckAddress(L, 1);
    for (int i = 2; i <= top; ++i) value |= CheckAddress(L, i);
    lua_pushinteger(L, static_cast<lua_Integer>(value));
    return 1;
}

int LuaBxor(lua_State* L) {
    const int top = lua_gettop(L);
    std::uint64_t value = CheckAddress(L, 1);
    for (int i = 2; i <= top; ++i) value ^= CheckAddress(L, i);
    lua_pushinteger(L, static_cast<lua_Integer>(value));
    return 1;
}

int LuaShl(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(
                           CheckAddress(L, 1) << CheckAddress(L, 2)));
    return 1;
}

int LuaShr(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(
                           CheckAddress(L, 1) >> CheckAddress(L, 2)));
    return 1;
}

int LuaBnot(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(~CheckAddress(L, 1)));
    return 1;
}

BionicRuntime* Bionic(lua_State* L) {
    GameScript* self = SelfFrom(L);
    if (self == nullptr || self->runtime() == nullptr ||
        self->runtime()->bionic() == nullptr)
        luaL_error(L, "armr runtime is not bound");
    return self->runtime()->bionic();
}

int LuaEnv(lua_State* L) {
    const auto environment = Bionic(L)->jni().CurrentEnvironmentAddress();
    if (!environment) return 0;
    lua_pushinteger(L, static_cast<lua_Integer>(*environment));
    return 1;
}

int LuaVm(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(
                           Bionic(L)->jni().vm_address()));
    return 1;
}

int LuaBase(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(0x4000000000ULL));
    return 1;
}

int LuaModuleBase(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const char* name = luaL_checkstring(L, 1);
    const auto base = self->runtime()->GuestModuleBase(name);
    if (!base) return 0;
    lua_pushinteger(L, static_cast<lua_Integer>(*base));
    return 1;
}

int LuaFindExport(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const auto address = self->runtime()->FindExport(luaL_checkstring(L, 1));
    if (!address) return 0;
    lua_pushinteger(L, static_cast<lua_Integer>(*address));
    return 1;
}

int LuaFindNative(lua_State* L) {
    const auto found = Bionic(L)->jni().FindRegisteredNative(
        luaL_checkstring(L, 1), luaL_checkstring(L, 2),
        luaL_checkstring(L, 3));
    lua_pushboolean(L, found.has_value());
    return 1;
}

int LuaNativeAddr(lua_State* L) {
    const auto function =
        Bionic(L)->jni().FindRegisteredNativeFunction(
            luaL_checkstring(L, 1), luaL_checkstring(L, 2),
            luaL_checkstring(L, 3));
    if (!function) return 0;
    lua_pushinteger(L, static_cast<lua_Integer>(*function));
    return 1;
}

int LuaRegisteredNatives(lua_State* L) {
    const auto methods =
        lua_gettop(L) >= 1 && !lua_isnil(L, 1)
            ? Bionic(L)->jni().RegisteredNativesForClass(
                  luaL_checkstring(L, 1))
            : Bionic(L)->jni().AllRegisteredNatives();
    lua_newtable(L);
    int index = 1;
    for (const auto& method : methods) {
        lua_newtable(L);
        lua_pushstring(L, method.class_type.c_str());
        lua_setfield(L, -2, "class");
        lua_pushstring(L, method.name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushstring(L, method.signature.c_str());
        lua_setfield(L, -2, "sig");
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

// Parses an IDA-style signature string into a byte pattern + wildcard mask.
// Tokens are whitespace-separated hex bytes; "?", "??", "**", or "--" mark a
// wildcard byte. Returns false on a malformed token.
bool ParseSignature(const std::string& text, std::vector<std::uint8_t>* pattern,
                    std::vector<std::uint8_t>* mask, std::string* error) {
    pattern->clear();
    mask->clear();
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        if (token == "?" || token == "??" || token == "**" ||
            token == "--") {
            pattern->push_back(0);
            mask->push_back(0);
            continue;
        }
        unsigned value = 0;
        std::size_t consumed = 0;
        try {
            value = std::stoul(token, &consumed, 16);
        } catch (...) {
            consumed = 0;
        }
        if (consumed != token.size() || token.size() != 2 || value > 0xff) {
            if (error != nullptr)
                *error = "bad signature token '" + token + "'";
            return false;
        }
        pattern->push_back(static_cast<std::uint8_t>(value));
        mask->push_back(1);
    }
    if (pattern->empty()) {
        if (error != nullptr) *error = "empty signature";
        return false;
    }
    return true;
}

// armr.sigscan([module,] "AA BB ?? CC") -> {addr=..., matches=N} or nil.
// Resolves a guest site by byte signature instead of a fixed RVA so the
// site survives rebuilds that shift layout. `matches` surfaces ambiguity: a
// signature matching more than once is not a reliable pin.
int LuaSigScan(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::string module;
    int pattern_index = 1;
    if (lua_gettop(L) >= 2) {
        if (lua_isstring(L, 1)) module = lua_tostring(L, 1);
        pattern_index = 2;
    }
    const char* text = luaL_checkstring(L, pattern_index);
    std::vector<std::uint8_t> pattern, mask;
    std::string parse_error;
    if (!ParseSignature(text, &pattern, &mask, &parse_error))
        return luaL_error(L, "sigscan: %s", parse_error.c_str());
    std::size_t matches = 0;
    const auto found = self->runtime()->ScanModuleCode(
        module, pattern, mask, &matches);
    if (!found) return 0;
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(*found));
    lua_setfield(L, -2, "addr");
    lua_pushinteger(L, static_cast<lua_Integer>(matches));
    lua_setfield(L, -2, "matches");
    return 1;
}

// Collects a Lua {1..n} array into a uint64 vector. Entries are numbers;
// {f32=v}/{f64=v} tables carry bit-cast float values for callers that feed
// CallEntry argument vectors.
std::vector<std::uint64_t> TableToArgs(lua_State* L, int index) {
    std::vector<std::uint64_t> args;
    if (lua_istable(L, index)) {
        const int count = static_cast<int>(lua_objlen(L, index));
        args.reserve(static_cast<std::size_t>(count));
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, index, i);
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "f32");
                if (lua_isnumber(L, -1)) {
                    const float f =
                        static_cast<float>(lua_tonumber(L, -1));
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, &f, sizeof(bits));
                    args.push_back(bits);
                    lua_pop(L, 2);
                    continue;
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "f64");
                if (lua_isnumber(L, -1)) {
                    const double d = lua_tonumber(L, -1);
                    std::uint64_t bits = 0;
                    std::memcpy(&bits, &d, sizeof(bits));
                    args.push_back(bits);
                    lua_pop(L, 2);
                    continue;
                }
                lua_pop(L, 1);
                lua_pop(L, 1);
                continue;
            }
            args.push_back(
                static_cast<std::uint64_t>(lua_tonumber(L, -1)));
            lua_pop(L, 1);
        }
    }
    return args;
}

// Same, but produces typed Aarch64CallArgument entries: {f32=}/{f64=} tag
// float/vector slots, numbers are General64.
std::vector<Aarch64CallArgument> TableToCallArgs(lua_State* L, int index) {
    std::vector<Aarch64CallArgument> args;
    if (!lua_istable(L, index)) return args;
    const int count = static_cast<int>(lua_objlen(L, index));
    args.reserve(static_cast<std::size_t>(count));
    for (int i = 1; i <= count; ++i) {
        lua_rawgeti(L, index, i);
        if (lua_istable(L, -1)) {
            bool consumed = false;
            lua_getfield(L, -1, "f32");
            if (lua_isnumber(L, -1)) {
                const float f =
                    static_cast<float>(lua_tonumber(L, -1));
                std::uint32_t bits = 0;
                std::memcpy(&bits, &f, sizeof(bits));
                args.push_back(Aarch64CallArgument::Float32Bits(bits));
                consumed = true;
            }
            lua_pop(L, 1);
            if (!consumed) {
                lua_getfield(L, -1, "f64");
                if (lua_isnumber(L, -1)) {
                    const double d = lua_tonumber(L, -1);
                    std::uint64_t bits = 0;
                    std::memcpy(&bits, &d, sizeof(bits));
                    args.push_back(Aarch64CallArgument::Float64Bits(bits));
                    consumed = true;
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            if (consumed) continue;
            args.push_back(Aarch64CallArgument::General64(0));
            continue;
        }
        args.push_back(Aarch64CallArgument::General64(
            static_cast<std::uint64_t>(lua_tonumber(L, -1))));
        lua_pop(L, 1);
    }
    return args;
}

int LuaCallExport(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const char* name = luaL_checkstring(L, 1);
    const auto environment =
        Bionic(L)->jni().CurrentEnvironmentAddress();
    if (!environment)
        return luaL_error(L, "no current JNI environment");
    const auto values = TableToArgs(L, 2);
    std::array<std::uint64_t, 8> args{};
    args[0] = *environment;
    for (std::size_t i = 0; i < values.size() && i + 1 < args.size(); ++i)
        args[i + 1] = values[i];
    std::uint64_t result = 0;
    std::string error;
    bool called;
    {
        GameScript::ReleaseLock release(self);
        called = self->runtime()->CallExport(name, args, &result, &error);
    }
    if (!called) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(result));
    return 1;
}

int LuaCallExportEntry(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const char* name = luaL_checkstring(L, 1);
    const auto environment =
        Bionic(L)->jni().CurrentEnvironmentAddress();
    if (!environment)
        return luaL_error(L, "no current JNI environment");
    std::vector<Aarch64CallArgument> args;
    args.push_back(Aarch64CallArgument::General64(*environment));
    {
        auto values = TableToCallArgs(L, 2);
        for (auto& v : values) args.push_back(v);
    }
    Aarch64CallResult result{};
    std::string error;
    bool called;
    {
        GameScript::ReleaseLock release(self);
        called =
            self->runtime()->CallExportEntry(name, args, &result, &error);
    }
    if (!called) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(result.x0));
    return 1;
}

// armr.call_addr(guest_function, {a0..aN}) -> x0 | nil,err
// Entries may be {f32=v}/{f64=v} tables for float slots.
int LuaCallAddress(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const std::uint64_t function = CheckAddress(L, 1);
    std::vector<Aarch64CallArgument> args = TableToCallArgs(L, 2);
    Aarch64CallResult result{};
    std::string error;
    bool called;
    {
        GameScript::ReleaseLock release(self);
        called = self->runtime()->CallEntry(function, args, &result, &error);
    }
    if (!called) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(result.x0));
    return 1;
}

// armr.call_native(class, name, sig, {x2..}) -> x0 | nil,err
// env is prepended automatically (registered-natives convention).
int LuaCallNative(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const char* class_type = luaL_checkstring(L, 1);
    const char* name = luaL_checkstring(L, 2);
    const char* signature = luaL_checkstring(L, 3);
    const auto environment =
        Bionic(L)->jni().CurrentEnvironmentAddress();
    if (!environment)
        return luaL_error(L, "no current JNI environment");
    std::vector<Aarch64CallArgument> args;
    args.push_back(Aarch64CallArgument::General64(*environment));
    {
        auto values = TableToCallArgs(L, 4);
        for (auto& v : values) args.push_back(v);
    }
    Aarch64CallResult result{};
    std::string error;
    const bool trace = std::strcmp(name, "nativeRender") == 0;
    if (trace)
        std::fprintf(stderr, "LUA CN %s pre-unlock\n", name);
    bool called;
    {
        GameScript::ReleaseLock release(self);
        if (trace)
            std::fprintf(stderr, "LUA CN %s unlocked\n", name);
        called = self->runtime()->CallRegisteredNativeOnAttachedThread(
            class_type, name, signature, args, &result, &error);
        if (trace)
            std::fprintf(stderr, "LUA CN %s call-done called=%d\n", name,
                         (int)called);
    }
    if (trace)
        std::fprintf(stderr, "LUA CN %s relocked\n", name);
    if (!called) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(result.x0));
    return 1;
}

int LuaNewClass(lua_State* L) {
    std::uint64_t reference = 0;
    std::string error;
    if (!Bionic(L)->jni().CreateCompatibilityClassReference(
            luaL_checkstring(L, 1), &reference, &error))
        return luaL_error(L, "new_class failed: %s", error.c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(reference));
    return 1;
}

int LuaNewObject(lua_State* L) {
    std::uint64_t reference = 0;
    std::string error;
    if (!Bionic(L)->jni().CreateCompatibilityTypedObjectReference(
            luaL_checkstring(L, 1), &reference, &error))
        return luaL_error(L, "new_object failed: %s", error.c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(reference));
    return 1;
}

int LuaNewString(lua_State* L) {
    std::uint64_t reference = 0;
    std::string error;
    if (!Bionic(L)->jni().CreateCompatibilityStringReference(
            luaL_checkstring(L, 1), &reference, &error))
        return luaL_error(L, "new_string failed: %s", error.c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(reference));
    return 1;
}

int LuaNewMethod(lua_State* L) {
    const std::uint64_t declaring = CheckAddress(L, 1);
    std::uint64_t reference = 0;
    std::string error;
    if (!Bionic(L)->jni().CreateCompatibilityReflectedMethodReference(
            declaring, luaL_checkstring(L, 2), luaL_checkstring(L, 3),
            &reference, &error))
        return luaL_error(L, "new_method failed: %s", error.c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(reference));
    return 1;
}

int LuaNewObjectArray(lua_State* L) {
    std::uint64_t reference = 0;
    std::string error;
    if (!Bionic(L)->jni().CreateCompatibilityEmptyObjectArrayReference(
            &reference, &error))
        return luaL_error(L, "new_object_array failed: %s", error.c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(reference));
    return 1;
}

int LuaNewLong(lua_State* L) {
    const std::int64_t value = static_cast<std::int64_t>(lua_tonumber(L, 1));
    std::uint64_t reference = 0;
    std::string error;
    if (!Bionic(L)->jni().CreateCompatibilityLongReference(value, &reference,
                                                          &error))
        return luaL_error(L, "new_long failed: %s", error.c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(reference));
    return 1;
}

int LuaNewLongArray(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    std::vector<std::uint64_t> elements;
    const std::size_t count = lua_objlen(L, 1);
    elements.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, 1, static_cast<int>(i));
        elements.push_back(
            static_cast<std::uint64_t>(lua_tonumber(L, -1)));
        lua_pop(L, 1);
    }
    std::uint64_t reference = 0;
    std::string error;
    if (!Bionic(L)->jni().CreateCompatibilityLongArrayReference(
            std::move(elements), &reference, &error))
        return luaL_error(L, "new_long_array failed: %s", error.c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(reference));
    return 1;
}

int LuaNewIntArray(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    std::vector<std::uint32_t> elements;
    const std::size_t count = lua_objlen(L, 1);
    elements.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, 1, static_cast<int>(i));
        elements.push_back(
            static_cast<std::uint32_t>(lua_tointeger(L, -1)));
        lua_pop(L, 1);
    }
    std::uint64_t reference = 0;
    std::string error;
    if (!Bionic(L)->jni().CreateCompatibilityIntArrayReference(
            std::move(elements), &reference, &error))
        return luaL_error(L, "new_int_array failed: %s", error.c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(reference));
    return 1;
}

// armr.new_byte_array(string) -> ref — a [B array holding the Lua string's
// raw bytes (empty string yields a zero-length array).
int LuaNewByteArray(lua_State* L) {
    std::size_t length = 0;
    const char* bytes = luaL_checklstring(L, 1, &length);
    std::vector<std::uint8_t> elements(
        reinterpret_cast<const std::uint8_t*>(bytes),
        reinterpret_cast<const std::uint8_t*>(bytes) + length);
    std::uint64_t reference = 0;
    std::string error;
    if (!Bionic(L)->jni().CreateCompatibilityByteArrayReference(
            std::move(elements), &reference, &error))
        return luaL_error(L, "new_byte_array failed: %s", error.c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(reference));
    return 1;
}

// armr.new_motion_event(action, x, y [, time_ms]) -> ref — an
// android/view/MotionEvent carrying real coordinates for
// UnityPlayer.nativeInjectEvent.
int LuaNewMotionEvent(lua_State* L) {
    const int action = static_cast<int>(luaL_checkinteger(L, 1));
    const float x = static_cast<float>(luaL_checknumber(L, 2));
    const float y = static_cast<float>(luaL_checknumber(L, 3));
    const std::int64_t event_time_ms =
        lua_gettop(L) >= 4 && lua_isnumber(L, 4)
            ? static_cast<std::int64_t>(lua_tonumber(L, 4))
            : static_cast<std::int64_t>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
    std::uint64_t reference = 0;
    std::string error;
    if (!Bionic(L)->jni().CreateCompatibilityMotionEventReference(
            action, x, y, event_time_ms, &reference, &error))
        return luaL_error(L, "new_motion_event failed: %s", error.c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(reference));
    return 1;
}

// armr.take_unity_messages() -> {{go=,method=,data=},...} — drains the
// UnitySendMessage queue populated by compatibility stubs.
int LuaTakeUnityMessages(lua_State* L) {
    auto messages = Bionic(L)->jni().TakePendingUnityMessages();
    lua_createtable(L, static_cast<int>(messages.size()), 0);
    int index = 1;
    for (const auto& message : messages) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, message.game_object.c_str());
        lua_setfield(L, -2, "go");
        lua_pushstring(L, message.method.c_str());
        lua_setfield(L, -2, "method");
        lua_pushstring(L, message.data.c_str());
        lua_setfield(L, -2, "data");
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int LuaReleaseRef(lua_State* L) {
    std::string error;
    (void)Bionic(L)->jni().ReleaseCompatibilityLocalReference(
        CheckAddress(L, 1), &error);
    return 0;
}

// ---- guest memory --------------------------------------------------------

template <typename T>
int LuaReadInt(lua_State* L) {
    T value = 0;
    if (!Bionic(L)->address_space().Read(CheckAddress(L, 1), &value,
                                         sizeof(value))) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(value));
    return 1;
}

template <typename T>
int LuaWriteInt(lua_State* L) {
    T value = static_cast<T>(CheckAddress(L, 2));
    lua_pushboolean(
        L, Bionic(L)->address_space().Write(CheckAddress(L, 1), &value,
                                            sizeof(value)));
    return 1;
}

int LuaRead(lua_State* L) {
    const std::uint64_t address = CheckAddress(L, 1);
    const std::size_t size =
        static_cast<std::size_t>(luaL_checknumber(L, 2));
    if (size == 0 || size > (1U << 24))
        return luaL_error(L, "armr.read size out of range");
    std::string buffer(size, '\0');
    if (!Bionic(L)->address_space().Read(address, buffer.data(), size)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, buffer.data(), size);
    return 1;
}

int LuaWrite(lua_State* L) {
    const std::uint64_t address = CheckAddress(L, 1);
    std::size_t size = 0;
    const char* data = luaL_checklstring(L, 2, &size);
    if (size == 0) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(
        L, Bionic(L)->address_space().Write(address, data, size));
    return 1;
}

int LuaCStr(lua_State* L) {
    const std::uint64_t address = CheckAddress(L, 1);
    const std::size_t max =
        lua_gettop(L) >= 2 ? static_cast<std::size_t>(lua_tonumber(L, 2))
                           : 4096;
    std::string buffer(max, '\0');
    if (!Bionic(L)->address_space().Read(address, buffer.data(), max)) {
        lua_pushnil(L);
        return 1;
    }
    const std::size_t length = buffer.find('\0');
    lua_pushlstring(L, buffer.data(),
                    length == std::string::npos ? max : length);
    return 1;
}

int LuaHeapAlloc(lua_State* L) {
    const std::uint64_t size = CheckAddress(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(
                           Bionic(L)->heap().Callocate(1, size)));
    return 1;
}

int LuaHeapAllocAligned(lua_State* L) {
    const std::uint64_t size = CheckAddress(L, 1);
    const std::uint64_t alignment = CheckAddress(L, 2);
    lua_pushinteger(L,
                    static_cast<lua_Integer>(
                        Bionic(L)->heap().AllocateAligned(size, alignment)));
    return 1;
}

int LuaHeapBase(lua_State* L) {
    lua_pushinteger(L,
                    static_cast<lua_Integer>(Bionic(L)->heap().guest_base()));
    return 1;
}

int LuaHeapCapacity(lua_State* L) {
    lua_pushinteger(L,
                    static_cast<lua_Integer>(Bionic(L)->heap().capacity()));
    return 1;
}

// ---- execution breakpoints ----------------------------------------------

// armr.exec_bp(address, spec) — spec is a function or
// {name=, sp=, skip=, diag=, run=function(st)...end}. `sp` is subtracted
// from the guest stack pointer and `skip` (default true) advances pc past
// the breakpointed instruction before `run` executes — matching the probe's
// historical prologue-emulation order.
int LuaExecBreakpoint(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const std::uint64_t address = CheckAddress(L, 1);
    std::string name;
    std::uint64_t sp_adjust = 0;
    bool skip = true;
    bool diag = false;
    bool stp = false;
    int function_ref = LUA_NOREF;
    if (lua_isfunction(L, 2)) {
        lua_pushvalue(L, 2);
        function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "run");
        if (!lua_isfunction(L, -1))
            return luaL_error(L, "exec_bp spec.run must be a function");
        function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_getfield(L, 2, "name");
        if (lua_isstring(L, -1)) name = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "sp");
        if (lua_isnumber(L, -1))
            sp_adjust = static_cast<std::uint64_t>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 2, "skip");
        if (lua_isboolean(L, -1)) skip = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "diag");
        if (lua_isboolean(L, -1)) diag = lua_toboolean(L, -1);
        lua_pop(L, 1);
        // stp=true emulates `stp x29, x30, [sp, #-imm]!` after the sp
        // adjust: the fp/lr pair is written to the new frame base.
        lua_getfield(L, 2, "stp");
        if (lua_isboolean(L, -1)) stp = lua_toboolean(L, -1);
        lua_pop(L, 1);
    } else {
        return luaL_error(L, "exec_bp spec must be a function or table");
    }
    if (diag) {
        wchar_t flag[8] = {};
        const DWORD length =
            GetEnvironmentVariableW(L"ISOMR_PVZ_DIAG", flag, 8);
        if (!(length > 0 && flag[0] != L'0')) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    self->NoteBreakpointRef(function_ref);
    std::string error;
    const bool added = self->runtime()->AddMainOwnerThreadExecutionBreakpoint(
        address,
        [self, function_ref, sp_adjust, skip, stp](
            Aarch64PausedExecutionState* state, std::string* callback_error) {
            if (state == nullptr) {
                if (callback_error != nullptr)
                    *callback_error = "breakpoint state is null";
                return false;
            }
            state->stack_pointer -= sp_adjust;
            if (stp) {
                auto& space = self->runtime()->bionic()->address_space();
                (void)space.Write(state->stack_pointer,
                                  &state->registers[29],
                                  sizeof(std::uint64_t));
                (void)space.Write(state->stack_pointer + 8,
                                  &state->registers[30],
                                  sizeof(std::uint64_t));
            }
            if (skip) state->program_counter += sizeof(std::uint32_t);
            return self->RunBreakpointCallback(function_ref, state,
                                               callback_error);
        },
        &error);
    if (!added)
        return luaL_error(L, "exec_bp install failed: %s", error.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int LuaResolveCallable(lua_State* L) {
    const auto callable = Bionic(L)->imports().ResolveCallable(
        luaL_checkstring(L, 1));
    if (!callable) return 0;
    lua_pushinteger(L, static_cast<lua_Integer>(*callable));
    return 1;
}

// ---- window / present / input --------------------------------------------

int LuaShowWindow(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::string error;
    bool shown;
    {
        GameScript::ReleaseLock release(self);
        shown = Bionic(L)->gles().Show(&error);
    }
    if (!shown) return luaL_error(L, "show_window failed: %s", error.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int LuaPresent(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const char* tag = lua_gettop(L) >= 1 ? luaL_checkstring(L, 1) : "frame";
    std::string error;
    bool ok;
    {
        GameScript::ReleaseLock release(self);
        if (!Bionic(L)->gles().BindWindowTarget(&error)) {
            ok = false;
        } else {
            ok = Bionic(L)->gles().Present(tag, &error);
        }
    }
    if (!ok) return luaL_error(L, "present failed: %s", error.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

// armr.pump() -> {close=bool, pointers={{action,x,y,w,h}...},
//                 keys={{action,code}...}} — pumps the owner window,
// draining both pointer and key queues.
int LuaPump(lua_State* L) {
    GameScript* self = SelfFrom(L);
    bool close_requested = false;
    std::vector<NativePointerEvent> pointer_events;
    std::vector<NativeKeyEvent> key_events;
    std::string error;
    {
        GameScript::ReleaseLock release(self);
        if (!Bionic(L)->gles().PumpEvents(&close_requested, &pointer_events,
                                          &error))
            return luaL_error(L, "pump failed: %s", error.c_str());
        (void)Bionic(L)->gles().DrainKeyEvents(&key_events, &error);
    }
    lua_newtable(L);
    lua_pushboolean(L, close_requested);
    lua_setfield(L, -2, "close");
    lua_newtable(L);
    int index = 1;
    for (const NativePointerEvent& event : pointer_events) {
        lua_newtable(L);
        lua_pushinteger(L, static_cast<int>(event.action));
        lua_setfield(L, -2, "action");
        lua_pushinteger(L, event.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, event.y);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, event.client_width);
        lua_setfield(L, -2, "w");
        lua_pushinteger(L, event.client_height);
        lua_setfield(L, -2, "h");
        lua_pushinteger(L, static_cast<lua_Integer>(event.enqueued_ns));
        lua_setfield(L, -2, "ns");
        lua_rawseti(L, -2, index++);
    }
    lua_setfield(L, -2, "pointers");
    lua_newtable(L);
    index = 1;
    for (const NativeKeyEvent& event : key_events) {
        lua_newtable(L);
        lua_pushinteger(L, static_cast<int>(event.action));
        lua_setfield(L, -2, "action");
        lua_pushinteger(L, event.code);
        lua_setfield(L, -2, "code");
        lua_rawseti(L, -2, index++);
    }
    lua_setfield(L, -2, "keys");
    return 1;
}

int LuaPumpOwner(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::string error;
    bool ok;
    {
        GameScript::ReleaseLock release(self);
        ok = Bionic(L)->gles().PumpOwnerMessages(&error);
    }
    if (!ok) return luaL_error(L, "pump_owner failed: %s", error.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

// armr.set_slice_callback(budget, fn) — install the owner-thread inter-slice
// callback; fn() runs mid-guest-call every `budget` instructions and returns
// false to fail the call. Used for message pumping + worker-failure reaping.
int LuaSetSliceCallback(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const std::uint64_t budget = CheckAddress(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    const int function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    self->NoteBreakpointRef(function_ref);  // keep the ref alive
    std::string error;
    const bool ok = self->runtime()->SetMainOwnerThreadInterSliceCallback(
        budget,
        [self, function_ref](std::string* callback_error) {
            lua_State* T = self->Enter();
            lua_rawgeti(T, LUA_REGISTRYINDEX, function_ref);
            bool result = true;
            if (lua_pcall(T, 0, 1, 0) != 0) {
                const char* message = lua_tostring(T, -1);
                if (callback_error != nullptr)
                    *callback_error = std::string("slice callback: ") +
                                      (message ? message : "?");
                lua_pop(T, 1);
                result = false;
            } else {
                result = !lua_isboolean(T, -1) || lua_toboolean(T, -1);
                lua_pop(T, 1);
            }
            self->Leave();
            return result;
        },
        &error);
    if (!ok)
        return luaL_error(L, "set_slice_callback failed: %s", error.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int LuaAckPointer(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    NativePointerEvent event{};
    lua_getfield(L, 1, "action");
    event.action = static_cast<NativePointerAction>(
        static_cast<int>(lua_tointeger(L, -1)));
    lua_getfield(L, 1, "x");
    event.x = static_cast<std::int32_t>(lua_tointeger(L, -1));
    lua_getfield(L, 1, "y");
    event.y = static_cast<std::int32_t>(lua_tointeger(L, -1));
    lua_getfield(L, 1, "w");
    event.client_width = static_cast<std::int32_t>(lua_tointeger(L, -1));
    lua_getfield(L, 1, "h");
    event.client_height = static_cast<std::int32_t>(lua_tointeger(L, -1));
    lua_getfield(L, 1, "ns");
    event.enqueued_ns = static_cast<std::uint64_t>(lua_tointeger(L, -1));
    lua_pop(L, 6);
    std::string error;
    if (!Bionic(L)->gles().AcknowledgePointerEvent(event, &error)) {
        // A failed ack leaves the event queued; the next pump redelivers it.
        // Surface the miss so a stuck/spamming pointer is diagnosable rather
        // than a silent no-op.
        static std::uint64_t ack_failures = 0;
        const std::uint64_t n = ++ack_failures;
        if (n <= 8 || n % 1000 == 0) {
            std::fprintf(stderr,
                         "ISOMR_INPUT_ACK_FAIL count=%llu err=%s\n",
                         static_cast<unsigned long long>(n), error.c_str());
            std::fflush(stderr);
        }
    }
    return 0;
}

// ---- time / environment --------------------------------------------------

int LuaSleep(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const std::uint64_t ms = CheckAddress(L, 1);
    {
        GameScript::ReleaseLock release(self);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    return 0;
}

int LuaNowMs(lua_State* L) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    lua_pushinteger(L, static_cast<lua_Integer>(
                           std::chrono::duration_cast<std::chrono::milliseconds>(
                               now)
                               .count()));
    return 1;
}

int LuaEnvFlag(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    wchar_t wide[64] = {};
    for (int i = 0; name[i] != '\0' && i < 63; ++i)
        wide[i] = static_cast<wchar_t>(name[i]);
    wchar_t flag[16] = {};
    const DWORD length = GetEnvironmentVariableW(wide, flag, 16);
    lua_pushboolean(L, length > 0 && flag[0] != L'0');
    return 1;
}

// armr.env_int("ARMR_RENDER_TIMEOUT", 600) -> integer
int LuaEnvInt(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const int fallback = static_cast<int>(luaL_optinteger(L, 2, 0));
    wchar_t wide[64] = {};
    for (int i = 0; name[i] != '\0' && i < 63; ++i)
        wide[i] = static_cast<wchar_t>(name[i]);
    wchar_t flag[32] = {};
    const DWORD length = GetEnvironmentVariableW(wide, flag, 32);
    lua_pushinteger(L, length > 0 ? _wtoi64(flag) : fallback);
    return 1;
}

// armr.guest_setenv("NAME", "value") -> errno (0 on success)
// Seeds a guest-visible env var; call before the payload runtime inits.
int LuaGuestSetenv(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const char* value = luaL_checkstring(L, 2);
    GameScript* self = SelfFrom(L);
    std::uint64_t result = 22;
    if (self != nullptr && self->runtime() != nullptr &&
        self->runtime()->bionic() != nullptr) {
        result = GuestEnvSetVar(&self->runtime()->bionic()->address_space(),
                                name ? name : "", value ? value : "", true);
    }
    lua_pushinteger(L, static_cast<lua_Integer>(result));
    return 1;
}

int LuaDiag(lua_State* L) {
    wchar_t flag[8] = {};
    const DWORD length =
        GetEnvironmentVariableW(L"ISOMR_PVZ_DIAG", flag, 8);
    lua_pushboolean(L, length > 0 && flag[0] != L'0');
    return 1;
}

// ---- threads / UI tasks / pending queues ----------------------------------

int LuaSpawn(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    const int function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    const std::string thread_name(name);
    std::thread spawned([self, function_ref, thread_name] {
        lua_State* T = self->Enter();
        lua_rawgeti(T, LUA_REGISTRYINDEX, function_ref);
        if (lua_pcall(T, 0, 0, 0) != 0) {
            const char* message = lua_tostring(T, -1);
            std::fprintf(stderr, "LUA thread %s error: %s\n",
                         thread_name.c_str(), message ? message : "?");
            std::fflush(stderr);
            lua_pop(T, 1);
        }
        self->Leave();
    });
    self->TrackSpawnedThread(std::move(spawned));
    lua_pushboolean(L, 1);
    return 1;
}

int LuaAttachThread(lua_State* L) {
    std::string error;
    if (!Bionic(L)->AttachCurrentThread(&error))
        return luaL_error(L, "attach_thread failed: %s", error.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int LuaDetachThread(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::string error;
    // Bionic runs a thread's TLS destructors on exit, in bounded passes
    // (PTHREAD_DESTRUCTOR_ITERATIONS) since a destructor may re-set TLS keys.
    // Invoke each pending destructor as a guest call on the attached session
    // before the runtime detaches the thread.
    for (std::size_t pass = 0; pass < 4; ++pass) {
        const auto calls = Bionic(L)->tls().DrainCurrentDestructorPass();
        if (calls.empty()) break;
        for (const auto& call : calls) {
            std::vector<Aarch64CallArgument> args;
            args.push_back(Aarch64CallArgument::General64(call.value));
            Aarch64CallResult result{};
            bool called;
            {
                GameScript::ReleaseLock release(self);
                called = self->runtime()->CallAddressOnAttachedThread(
                    call.function, args, &result, &error);
            }
            if (!called) {
                // A destructor that resolves to non-executable guest memory
                // cannot run; log it and drop it rather than blocking detach.
                std::fprintf(stderr,
                             "ISOMR_TLS_DESTRUCTOR_SKIP function=0x%llx "
                             "value=0x%llx err=%s\n",
                             (unsigned long long)call.function,
                             (unsigned long long)call.value,
                             error.c_str());
                std::fflush(stderr);
                error.clear();
            }
            if (!Bionic(L)->tls().CompleteCurrentDestructorCall())
                return luaL_error(
                    L, "detach_thread TLS destructor completion invalid");
        }
    }
    // After the bounded passes real bionic exits the thread even if a
    // destructor re-armed a key; drop any remainder so the detach proceeds.
    if (Bionic(L)->tls().current_thread_destructor_count() != 0)
        (void)Bionic(L)->tls().DiscardCurrentThreadDestructors();
    if (!Bionic(L)->DetachCurrentThread(&error))
        return luaL_error(L, "detach_thread failed: %s", error.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int LuaTakeUiTasks(lua_State* L) {
    GameScript* self = SelfFrom(L);
    // TakeReadyPendingUiTasks can wait on the JNI mutex while a guest JNI
    // call is in flight; release the script lock so spawned lua threads
    // (watchdog, handler worker) keep running if this stalls.
    std::vector<GuestJni::PendingUiTaskInfo> tasks;
    {
        GameScript::ReleaseLock release(self);
        tasks = Bionic(L)->jni().TakeReadyPendingUiTasks();
    }
    lua_newtable(L);
    int index = 1;
    for (const auto& task : tasks) {
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(task.native_handle));
        lua_setfield(L, -2, "handle");
        lua_pushinteger(L, static_cast<lua_Integer>(
                               task.argument_reference));
        lua_setfield(L, -2, "arg");
        if (!task.dispatch_class.empty()) {
            lua_pushstring(L, task.dispatch_class.c_str());
            lua_setfield(L, -2, "class");
            lua_pushstring(L, task.dispatch_name.c_str());
            lua_setfield(L, -2, "name");
            lua_pushstring(L, task.dispatch_signature.c_str());
            lua_setfield(L, -2, "sig");
        }
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

// armr.take_display_listeners() — DisplayListener proxies captured by
// DisplayManager.registerDisplayListener; returns {ref=, handle=} entries.
int LuaTakeDisplayListeners(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> listeners;
    {
        GameScript::ReleaseLock release(self);
        listeners = Bionic(L)->jni().TakePendingDisplayListeners();
    }
    lua_newtable(L);
    int index = 1;
    for (const auto& [ref, handle] : listeners) {
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(ref));
        lua_setfield(L, -2, "ref");
        lua_pushinteger(L, static_cast<lua_Integer>(handle));
        lua_setfield(L, -2, "handle");
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

// armr.notify_refresh_period(ref, cookie, period, appOffset, sfOffset) —
// calls SwappyDisplayManager.nOnRefreshPeriodChanged(JJJJ)V directly so the
// frame pacer learns a real refresh period without a live Android
// Choreographer/DisplayManager callback.
int LuaNotifyRefreshPeriod(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const std::uint64_t listener_ref = CheckAddress(L, 1);
    const std::uint64_t cookie = CheckAddress(L, 2);
    const std::uint64_t period =
        static_cast<std::uint64_t>(luaL_checkinteger(L, 3));
    const std::uint64_t app_offset =
        static_cast<std::uint64_t>(luaL_optinteger(L, 4, 0));
    const std::uint64_t sf_offset =
        static_cast<std::uint64_t>(luaL_optinteger(L, 5, 0));
    GuestJni& jni = Bionic(L)->jni();
    const auto environment = jni.CurrentEnvironmentAddress();
    constexpr const char* kManagerClass =
        "com/google/androidgamesdk/SwappyDisplayManager";
    constexpr const char* kName = "nOnRefreshPeriodChanged";
    constexpr const char* kSig = "(JJJJ)V";
    if (!environment ||
        !jni.FindRegisteredNative(kManagerClass, kName, kSig)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Aarch64CallResult result{};
    std::string error;
    bool called;
    {
        GameScript::ReleaseLock release(self);
        called = self->runtime()->CallRegisteredNativeOnAttachedThread(
            kManagerClass, kName, kSig,
            {Aarch64CallArgument::General64(*environment),
             Aarch64CallArgument::General64(listener_ref),
             Aarch64CallArgument::General64(cookie),
             Aarch64CallArgument::General64(period),
             Aarch64CallArgument::General64(app_offset),
             Aarch64CallArgument::General64(sf_offset)},
            &result, &error);
    }
    if (jni.has_pending_thrown_exception()) jni.ClearPendingException();
    if (!called) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// armr.notify_supported_periods(ref, cookie, jlongs_ref, ints_ref) —
// calls SwappyDisplayManager.nSetSupportedRefreshPeriods(J[J[I)V, the
// onDisplayAdded-path native that marks the refresh provider valid.
int LuaNotifySupportedPeriods(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const std::uint64_t listener_ref = CheckAddress(L, 1);
    const std::uint64_t cookie = CheckAddress(L, 2);
    const std::uint64_t longs_ref = CheckAddress(L, 3);
    const std::uint64_t ints_ref = CheckAddress(L, 4);
    GuestJni& jni = Bionic(L)->jni();
    const auto environment = jni.CurrentEnvironmentAddress();
    constexpr const char* kManagerClass =
        "com/google/androidgamesdk/SwappyDisplayManager";
    constexpr const char* kName = "nSetSupportedRefreshPeriods";
    constexpr const char* kSig = "(J[J[I)V";
    if (!environment ||
        !jni.FindRegisteredNative(kManagerClass, kName, kSig)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Aarch64CallResult result{};
    std::string error;
    bool called;
    {
        GameScript::ReleaseLock release(self);
        called = self->runtime()->CallRegisteredNativeOnAttachedThread(
            kManagerClass, kName, kSig,
            {Aarch64CallArgument::General64(*environment),
             Aarch64CallArgument::General64(listener_ref),
             Aarch64CallArgument::General64(cookie),
             Aarch64CallArgument::General64(longs_ref),
             Aarch64CallArgument::General64(ints_ref)},
            &result, &error);
    }
    if (jni.has_pending_thrown_exception()) jni.ClearPendingException();
    if (!called) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// armr.find_interface_proxy(type) — returns {ref=, handle=} for a JNIBridge
// proxy implementing the given interface (e.g. Swappy's
// com/google/androidgamesdk/ChoreographerCallback), or nil.
int LuaFindInterfaceProxy(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const char* type = luaL_checkstring(L, 1);
    std::optional<std::pair<std::uint64_t, std::uint64_t>> found;
    {
        GameScript::ReleaseLock release(self);
        found = Bionic(L)->jni().FindInterfaceProxy(type != nullptr ? type
                                                                  : "");
    }
    if (!found) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(found->first));
    lua_setfield(L, -2, "ref");
    lua_pushinteger(L, static_cast<lua_Integer>(found->second));
    lua_setfield(L, -2, "handle");
    return 1;
}

// armr.take_choreographer_callbacks() — ChoreographerCallback objects
// constructed with a (J)V cookie; returns {ref=, handle=} entries.
int LuaTakeChoreographerCallbacks(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> callbacks;
    {
        GameScript::ReleaseLock release(self);
        callbacks = Bionic(L)->jni().TakeChoreographerCallbacks();
    }
    lua_newtable(L);
    int index = 1;
    for (const auto& [ref, handle] : callbacks) {
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(ref));
        lua_setfield(L, -2, "ref");
        lua_pushinteger(L, static_cast<lua_Integer>(handle));
        lua_setfield(L, -2, "handle");
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

// armr.take_frame_callbacks() — FrameCallback objects posted through
// Choreographer.postFrameCallback: {ref=, handle=} entries. Each entry is a
// one-shot pending callback — drain and deliver doFrame(J)V once per entry.
int LuaTakeFrameCallbacks(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> callbacks;
    {
        GameScript::ReleaseLock release(self);
        callbacks = Bionic(L)->jni().TakePendingFrameCallbacks();
    }
    lua_newtable(L);
    int index = 1;
    for (const auto& [ref, handle] : callbacks) {
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(ref));
        lua_setfield(L, -2, "ref");
        lua_pushinteger(L, static_cast<lua_Integer>(handle));
        lua_setfield(L, -2, "handle");
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

// armr.sync_sweep() -> count — opt-in lost-wake sweep. Only active while the
// sync-poll cap is armed (via ISOMR_SYNC_POLL_MS at launch or
// armr.set_sync_poll at runtime); otherwise returns 0. Wakes every registered
// futex/condition/mutex/rwlock waiter so each re-checks its predicate,
// self-healing a missed job-dispatch signal. Returns waiters woken.
int LuaSyncSweep(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::size_t woken = 0;
    {
        GameScript::ReleaseLock release(self);
        woken = Bionic(L)->sync().WakeAllWaiters();
    }
    lua_pushinteger(L, static_cast<lua_Integer>(woken));
    return 1;
}

// armr.served_count() -> n — completed content-traced guest reads
// (#1 catalog.json, #2+ asset bundles). Boot-milestone event source for
// arming the sync poll without depending on drain-tick rates.
int LuaServedCount(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::uint64_t n = 0;
    {
        GameScript::ReleaseLock release(self);
        n = Bionic(L)->files().ServedContentCount();
    }
    lua_pushinteger(L, static_cast<lua_Integer>(n));
    return 1;
}

// armr.set_sync_poll(ms) — arm (or disarm with 0) the lost-wake poll cap at
// runtime. The boot catalog phase pays 4-5 cores of spurious re-wakes if the
// cap is set from launch, but load-phase wedges self-heal with it armed, so
// scripts arm it once boot milestones have passed. Also activates
// armr.sync_sweep(). ms is clamped to [0, 60000).
int LuaSetSyncPoll(lua_State* L) {
    const lua_Integer ms = luaL_checkinteger(L, 1);
    SetSyncPollMilliseconds(ms > 0 ? static_cast<std::uint32_t>(ms) : 0);
    return 0;
}

// armr.futex_waiters() -> string — dumps every futex with live waiters: its
// address, waiter count, and the word re-read through the sync layer's own
// ReadWord. A parked waiter whose word already passed its expected value is a
// lost wake; a word still holding the pre-signal value is a structural stall.
int LuaFutexWaiters(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::string description;
    {
        GameScript::ReleaseLock release(self);
        description = Bionic(L)->sync().DescribeFutexWaiters();
    }
    lua_pushlstring(L, description.data(), description.size());
    return 1;
}

// armr.locks_describe() -> string — dumps contended locks (a held mutex with
// waiting threads, or a contested rwlock) with the owning host thread id, so
// an all-parked wedge names the lock whose holder is itself blocked.
int LuaLocksDescribe(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::string description;
    {
        GameScript::ReleaseLock release(self);
        description = Bionic(L)->sync().DescribeLocks();
    }
    lua_pushlstring(L, description.data(), description.size());
    return 1;
}

// armr.notify_choreographer(ref, cookie, frame_nanos) — calls
// ChoreographerCallback.nOnChoreographer(JJ)V directly so Swappy's frame
// pacer receives the vsync tick that a real Android Choreographer delivers
// once per refresh.
int LuaNotifyChoreographer(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const std::uint64_t callback_ref = CheckAddress(L, 1);
    const std::uint64_t cookie = CheckAddress(L, 2);
    const std::uint64_t frame_nanos =
        static_cast<std::uint64_t>(luaL_checkinteger(L, 3));
    GuestJni& jni = Bionic(L)->jni();
    const auto environment = jni.CurrentEnvironmentAddress();
    constexpr const char* kCallbackClass =
        "com/google/androidgamesdk/ChoreographerCallback";
    constexpr const char* kName = "nOnChoreographer";
    constexpr const char* kSig = "(JJ)V";
    if (!environment ||
        !jni.FindRegisteredNative(kCallbackClass, kName, kSig)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Aarch64CallResult result{};
    std::string error;
    bool called;
    {
        GameScript::ReleaseLock release(self);
        called = self->runtime()->CallRegisteredNativeOnAttachedThread(
            kCallbackClass, kName, kSig,
            {Aarch64CallArgument::General64(*environment),
             Aarch64CallArgument::General64(callback_ref),
             Aarch64CallArgument::General64(cookie),
             Aarch64CallArgument::General64(frame_nanos)},
            &result, &error);
    }
    if (jni.has_pending_thrown_exception()) jni.ClearPendingException();
    if (!called) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int LuaPendingUiCount(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::size_t count = 0;
    {
        GameScript::ReleaseLock release(self);
        count = Bionic(L)->jni().pending_ui_task_count();
    }
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 1;
}

int LuaTakeScheduledEvents(lua_State* L) {
    const auto events = Bionic(L)->jni().TakePendingScheduledEvents();
    lua_newtable(L);
    int index = 1;
    for (const auto& event : events) {
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(event[0]));
        lua_rawseti(L, -2, 1);
        lua_pushinteger(L, static_cast<lua_Integer>(event[1]));
        lua_rawseti(L, -2, 2);
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int LuaTakeConsent(lua_State* L) {
    const auto completions =
        Bionic(L)->jni().TakePendingConsentCompletions();
    lua_newtable(L);
    int index = 1;
    for (const auto& [is_form, handle] : completions) {
        lua_newtable(L);
        lua_pushboolean(L, is_form);
        lua_setfield(L, -2, "form");
        lua_pushinteger(L, static_cast<lua_Integer>(handle));
        lua_setfield(L, -2, "handle");
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int LuaTakeNetworker(lua_State* L) {
    const auto contexts =
        Bionic(L)->jni().TakePendingNetworkerCompletions();
    lua_newtable(L);
    int index = 1;
    for (const std::uint64_t context : contexts) {
        lua_pushinteger(L, static_cast<lua_Integer>(context));
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

// Shared JNIBridge.invoke dispatch: calls the registered
// invoke(handle, interfaceClass, reflectedMethod, args) on the attached
// thread, swallowing the pending NoSuchMethodError the Unity dispatcher
// leaves after interface probing. argument_reference != 0 is passed as the
// single element of the args array (e.g. the Message for handleMessage).
int InvokePeer(lua_State* L, std::uint64_t native_handle,
               const char* interface_class, const char* method_name,
               const char* method_signature, std::uint64_t argument_reference) {
    GameScript* self = SelfFrom(L);
    GuestJni& jni = Bionic(L)->jni();
    const auto environment = jni.CurrentEnvironmentAddress();
    constexpr const char* kBridgeClass = "bitter/jnibridge/JNIBridge";
    constexpr const char* kInvokeSig =
        "(JLjava/lang/Class;Ljava/lang/reflect/Method;[Ljava/lang/Object;)"
        "Ljava/lang/Object;";
    if (!environment ||
        !jni.FindRegisteredNative(kBridgeClass, "invoke", kInvokeSig)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::uint64_t bridge_class = 0, interface_ref = 0;
    std::uint64_t reflected_method = 0, arguments = 0;
    std::string error;
    const auto cleanup = [&] {
        std::string ignored;
        if (arguments != 0)
            (void)jni.ReleaseCompatibilityLocalReference(arguments, &ignored);
        if (reflected_method != 0)
            (void)jni.ReleaseCompatibilityLocalReference(reflected_method,
                                                         &ignored);
        if (interface_ref != 0)
            (void)jni.ReleaseCompatibilityLocalReference(interface_ref,
                                                         &ignored);
        if (bridge_class != 0)
            (void)jni.ReleaseCompatibilityLocalReference(bridge_class,
                                                         &ignored);
    };
    if (!jni.CreateCompatibilityClassReference(kBridgeClass, &bridge_class,
                                               &error) ||
        !jni.CreateCompatibilityClassReference(interface_class,
                                               &interface_ref, &error) ||
        !jni.CreateCompatibilityReflectedMethodReference(
            interface_ref, method_name, method_signature, &reflected_method,
            &error)) {
        cleanup();
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    const bool arguments_created =
        argument_reference != 0
            ? jni.CreateCompatibilitySingleObjectArrayReference(
                  argument_reference, &arguments, &error)
            : jni.CreateCompatibilityEmptyObjectArrayReference(&arguments,
                                                               &error);
    if (!arguments_created) {
        cleanup();
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    Aarch64CallResult result{};
    bool called;
    {
        GameScript::ReleaseLock release(self);
        called = self->runtime()->CallRegisteredNativeOnAttachedThread(
            kBridgeClass, "invoke", kInvokeSig,
            {Aarch64CallArgument::General64(*environment),
             Aarch64CallArgument::General64(bridge_class),
             Aarch64CallArgument::General64(native_handle),
             Aarch64CallArgument::General64(interface_ref),
             Aarch64CallArgument::General64(reflected_method),
             Aarch64CallArgument::General64(arguments)},
            &result, &error);
    }
    if (jni.has_pending_thrown_exception()) jni.ClearPendingException();
    cleanup();
    if (!called) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// armr.invoke_runnable(handle) — mirrors CtrEntryBridge::InvokeRunnable:
// JNIBridge.invoke(handle, Runnable.class, run()V, emptyArgs) on the
// attached thread.
int LuaInvokeRunnable(lua_State* L) {
    const std::uint64_t native_handle = CheckAddress(L, 1);
    return InvokePeer(L, native_handle, "java/lang/Runnable", "run", "()V",
                      0);
}

// armr.invoke_peer(handle, class, name, sig[, arg]) — generalized JNIBridge
// invoke for non-Runnable callbacks such as Handler.Callback.handleMessage.
int LuaInvokePeer(lua_State* L) {
    const std::uint64_t native_handle = CheckAddress(L, 1);
    const char* interface_class = luaL_checkstring(L, 2);
    const char* method_name = luaL_checkstring(L, 3);
    const char* method_signature = luaL_checkstring(L, 4);
    const std::uint64_t argument_reference =
        lua_gettop(L) >= 5 && !lua_isnil(L, 5)
            ? static_cast<std::uint64_t>(lua_tointeger(L, 5))
            : 0;
    return InvokePeer(L, native_handle, interface_class, method_name,
                      method_signature, argument_reference);
}

int LuaHasException(lua_State* L) {
    GameScript* self = SelfFrom(L);
    bool pending = false;
    {
        GameScript::ReleaseLock release(self);
        pending = Bionic(L)->jni().has_pending_thrown_exception();
    }
    lua_pushboolean(L, pending);
    return 1;
}

int LuaClearException(lua_State* L) {
    GameScript* self = SelfFrom(L);
    GameScript::ReleaseLock release(self);
    Bionic(L)->jni().ClearPendingException();
    return 0;
}

int LuaJournal(lua_State* L) {
    const int count = lua_gettop(L) >= 1
                          ? static_cast<int>(lua_tointeger(L, 1))
                          : 32;
    lua_pushstring(L, Bionic(L)->imports()
                          .RecentDispatchJournal(count)
                          .c_str());
    return 1;
}

int LuaJniNotes(lua_State* L) {
    GameScript* self = SelfFrom(L);
    const int count = lua_gettop(L) >= 1
                          ? static_cast<int>(lua_tointeger(L, 1))
                          : 32;
    std::string notes;
    {
        GameScript::ReleaseLock release(self);
        notes = Bionic(L)->imports().RecentGlobalJniCallNotes(count);
    }
    lua_pushstring(L, notes.c_str());
    return 1;
}

int LuaProxies(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::string description;
    {
        GameScript::ReleaseLock release(self);
        description = Bionic(L)->jni().DescribeInterfaceProxies();
    }
    lua_pushstring(L, description.c_str());
    return 1;
}

int LuaThreadsDescribe(lua_State* L) {
    GameScript* self = SelfFrom(L);
    std::string description;
    {
        GameScript::ReleaseLock release(self);
        description = Bionic(L)->threads().DescribeThreads();
    }
    lua_pushstring(L, description.c_str());
    return 1;
}

int LuaGuestPcs(lua_State* L) {
    GameScript* self = SelfFrom(L);
    if (self == nullptr || self->runtime() == nullptr)
        luaL_error(L, "armr runtime is not bound");
    std::string description;
    {
        GameScript::ReleaseLock release(self);
        description = self->runtime()->DescribeWorkerGuestPcs();
    }
    lua_pushstring(L, description.c_str());
    return 1;
}

// armr.thread_pointer() — the guest TLS/errno address of the calling attached
// thread, which is also the key its call_native sessions register under.
int LuaThreadPointer(lua_State* L) {
    const auto tp = Bionic(L)->thread_state().CurrentErrnoAddress();
    if (!tp) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(*tp));
    return 1;
}

// armr.halt_session(tp) — force the live session registered under guest
// thread key tp to halt at the next dynarmic boundary, surfacing its stalled
// PC and recent-import tail through the CallEntry error path.
int LuaHaltSession(lua_State* L) {
    GameScript* self = SelfFrom(L);
    if (self == nullptr || self->runtime() == nullptr)
        luaL_error(L, "armr runtime is not bound");
    const std::uint64_t tp = CheckAddress(L, 1);
    lua_pushboolean(
        L, self->runtime()->HaltSignalSession(tp) ? 1 : 0);
    return 1;
}

int LuaThreadsCreate(lua_State* L) {
    const int result = Bionic(L)->threads().Create(
        CheckAddress(L, 1), CheckAddress(L, 2), CheckAddress(L, 3),
        CheckAddress(L, 4));
    lua_pushinteger(L, result);
    return 1;
}

int LuaThreadsCheckFailure(lua_State* L) {
    std::string error;
    const bool failed =
        Bionic(L)->threads().CheckCompletedWorkerFailure(&error);
    lua_pushboolean(L, failed);
    if (failed) lua_pushstring(L, error.c_str());
    return failed ? 2 : 1;
}

int LuaThreadsAckFailure(lua_State* L) {
    std::string error;
    (void)Bionic(L)->threads().AcknowledgeCompletedWorkerFailure(&error);
    return 0;
}

int LuaRequestStop(lua_State* L) {
    SelfFrom(L)->RequestStop();
    return 0;
}

int LuaStopRequested(lua_State* L) {
    // A hosted game-window close is a cooperative quit: once the shell has
    // delivered the close to the guest surface, the script's post-frame
    // keep-alive should unwind so the process exits instead of leaving a
    // ghosted window. Standalone/headless launches keep their keep-alive.
    const bool window_quit = Bionic(L)->gles().window_close_requested();
    const bool stop = SelfFrom(L)->stop_requested() || window_quit;
    lua_pushboolean(L, stop);
    return 1;
}

int LuaExit(lua_State* L) {
    const int code = static_cast<int>(luaL_checkinteger(L, 1));
    std::fflush(nullptr);
    ExitProcess(static_cast<UINT>(code));
}

// ---- scripted JNI overrides ----------------------------------------------

// armr.jni{kind=, class=, name=, sig=, fn=function(ctx)...end}
int LuaJni(lua_State* L) {
    SelfFrom(L)->RegisterJniHandler(L, 1);
    return 0;
}

// Maps a scripted JNI handler's return value onto the guest register result
// using the call kind. Numbers are refs/int results; strings become String
// refs for `string` kind and typed objects for `object`/`array` kinds; a
// table may carry {ref=} or {type=}; nil/false means unhandled.
bool ApplyJniResult(lua_State* L, ScriptedJniContext& context, int index) {
    const char* kind = context.kind;
    if (std::strncmp(kind, "static-", 7) == 0) kind += 7;
    const std::string k = kind;
    if (k == "void") {
        context.SetResult(0);
        return true;
    }
    if (k == "boolean" || k == "int" || k == "long" || k == "short" ||
        k == "byte" || k == "char") {
        if (lua_isboolean(L, index)) {
            context.SetResult(lua_toboolean(L, index) ? 1 : 0);
            return true;
        }
        if (lua_isnumber(L, index)) {
            context.SetResult(static_cast<std::uint64_t>(
                lua_tointeger(L, index)));
            return true;
        }
        return false;
    }
    if (k == "float") {
        if (!lua_isnumber(L, index)) return false;
        context.SetFloatResult(
            static_cast<float>(lua_tonumber(L, index)));
        return true;
    }
    if (k == "double") {
        if (!lua_isnumber(L, index)) return false;
        context.SetDoubleResult(lua_tonumber(L, index));
        return true;
    }
    if (k == "string") {
        if (!lua_isstring(L, index)) return false;
        std::uint64_t reference = 0;
        std::string error;
        if (!context.CreateString(lua_tostring(L, index), &reference,
                                  &error)) {
            if (context.call_error != nullptr)
                *context.call_error = "jni string create failed: " + error;
            return false;
        }
        context.SetResult(reference);
        return true;
    }
    if (k == "object" || k == "array") {
        if (lua_isnumber(L, index)) {
            context.SetResult(static_cast<std::uint64_t>(
                lua_tointeger(L, index)));
            return true;
        }
        const char* type = nullptr;
        if (lua_isstring(L, index)) {
            type = lua_tostring(L, index);
        } else if (lua_istable(L, index)) {
            lua_getfield(L, index, "ref");
            if (lua_isnumber(L, -1)) {
                context.SetResult(static_cast<std::uint64_t>(
                    lua_tointeger(L, -1)));
                lua_pop(L, 1);
                return true;
            }
            lua_pop(L, 1);
            lua_getfield(L, index, "type");
            if (lua_isstring(L, -1)) type = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        if (type == nullptr) return false;
        std::uint64_t reference = 0;
        std::string error;
        if (!context.CreateObject(type, &reference, &error)) {
            if (context.call_error != nullptr)
                *context.call_error = "jni object create failed: " + error;
            return false;
        }
        context.SetResult(reference);
        return true;
    }
    // Unknown kind: numeric or boolean return still applies generically.
    if (lua_isnumber(L, index)) {
        context.SetResult(
            static_cast<std::uint64_t>(lua_tointeger(L, index)));
        return true;
    }
    if (lua_isboolean(L, index)) {
        context.SetResult(lua_toboolean(L, index) ? 1 : 0);
        return true;
    }
    return false;
}

}  // namespace

// ---- GameScript -----------------------------------------------------------

GameScript::GameScript() = default;

GameScript::~GameScript() {
    JoinThreads();
    if (state_ != nullptr) lua_close(state_);
}

GameScript::ReleaseLock::ReleaseLock(GameScript* script) : script(script) {
    const auto id = std::this_thread::get_id();
    const auto it = script->lua_depth_.find(id);
    if (it != script->lua_depth_.end() && it->second == 1) {
        script->lock_.unlock();
        released = true;
    }
}

GameScript::ReleaseLock::~ReleaseLock() {
    if (released) script->lock_.lock();
}

lua_State* GameScript::Enter() {
    lock_.lock();
    ++lua_depth_[std::this_thread::get_id()];
    const auto id = std::this_thread::get_id();
    const auto found = thread_states_.find(id);
    if (found != thread_states_.end()) return found->second;
    lua_State* T = lua_newthread(state_);
    const int ref = luaL_ref(state_, LUA_REGISTRYINDEX);
    thread_states_[id] = T;
    thread_refs_[id] = ref;
    return T;
}

void GameScript::Leave() {
    const auto id = std::this_thread::get_id();
    const auto it = lua_depth_.find(id);
    if (it != lua_depth_.end() && --it->second <= 0) lua_depth_.erase(it);
    lock_.unlock();
}

bool GameScript::Load(const std::wstring& path, std::string* error) {
    state_ = luaL_newstate();
    if (state_ == nullptr) {
        if (error != nullptr) *error = "luaL_newstate failed";
        return false;
    }
    luaL_openlibs(state_);
    lua_pushlightuserdata(state_, this);
    lua_setfield(state_, LUA_REGISTRYINDEX, kSelfKey);
    RegisterBindings(state_);
    std::string narrow;
    narrow.reserve(path.size());
    for (const wchar_t wc : path)
        narrow.push_back(static_cast<char>(wc));
    if (luaL_dofile(state_, narrow.c_str()) != 0)
        return LuaError(state_, error, "script load");
    return true;
}

void GameScript::BindRuntime(Aarch64ProfileRuntime* runtime) {
    runtime_ = runtime;
}

bool GameScript::HasHook(const char* name) {
    lua_State* T = Enter();
    lua_getglobal(T, name);
    const bool present = lua_isfunction(T, -1);
    lua_pop(T, 1);
    Leave();
    return present;
}

bool GameScript::CallHook(const char* name, std::string* error) {
    lua_State* T = Enter();
    lua_getglobal(T, name);
    if (!lua_isfunction(T, -1)) {
        lua_pop(T, 1);
        Leave();
        if (error != nullptr)
            *error = std::string("script hook '") + name +
                     "' is not a function";
        return false;
    }
    const int status = lua_pcall(T, 0, 0, 0);
    Leave();
    if (status != 0) {
        // The error object was already popped by the failed call path on T;
        // refetch through the shared state stack.
        const char* message = lua_tostring(T, -1);
        if (error != nullptr)
            *error = std::string("script hook '") + name +
                     "' failed: " + (message ? message : "?");
        lua_pop(T, 1);
        return false;
    }
    return true;
}

void GameScript::JoinThreads() {
    std::vector<std::thread> threads;
    {
        const std::lock_guard guard(spawned_threads_lock_);
        threads.swap(spawned_threads_);
    }
    for (std::thread& thread : threads)
        if (thread.joinable()) thread.join();
}

bool GameScript::RunBreakpointCallback(
    int function_ref, Aarch64PausedExecutionState* state,
    std::string* error) {
    lua_State* T = Enter();
    lua_rawgeti(T, LUA_REGISTRYINDEX, function_ref);
    StateBox* box = static_cast<StateBox*>(
        lua_newuserdata(T, sizeof(StateBox)));
    box->state = state;
    box->live = true;
    luaL_getmetatable(T, kStateMeta);
    lua_setmetatable(T, -2);
    const int status = lua_pcall(T, 1, 1, 0);
    box->live = false;
    bool handled = true;
    if (status != 0) {
        const bool ok = LuaError(T, error, "breakpoint callback");
        (void)ok;
        handled = false;
    } else {
        handled = !lua_isboolean(T, -1) || lua_toboolean(T, -1);
        lua_pop(T, 1);
    }
    Leave();
    if (!handled && error != nullptr && error->empty())
        *error = "breakpoint callback returned false";
    return handled;
}

void GameScript::NoteBreakpointRef(int function_ref) {
    breakpoint_function_refs_.push_back(function_ref);
}

void GameScript::TrackSpawnedThread(std::thread thread) {
    const std::lock_guard guard(spawned_threads_lock_);
    spawned_threads_.push_back(std::move(thread));
}

void GameScript::RegisterJniHandler(lua_State* L, int spec_index) {
    luaL_checktype(L, spec_index, LUA_TTABLE);
    JniHandler handler;
    const auto field = [&](const char* key) -> std::string {
        lua_getfield(L, spec_index, key);
        std::string value =
            lua_isstring(L, -1) ? lua_tostring(L, -1) : std::string();
        lua_pop(L, 1);
        return value;
    };
    handler.kind = field("kind");
    handler.class_pattern = field("class");
    handler.name_pattern = field("name");
    handler.signature_pattern = field("sig");
    lua_getfield(L, spec_index, "fn");
    if (!lua_isfunction(L, -1))
        luaL_error(L, "armr.jni spec.fn must be a function");
    handler.function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    jni_handlers_.push_back(std::move(handler));
}

bool GameScript::TryJniOverride(ScriptedJniContext& context) {
    if (jni_handlers_.empty()) return false;
    lua_State* T = Enter();
    bool handled = false;
    for (const JniHandler& handler : jni_handlers_) {
        // pattern match: empty or '*' matches all, trailing '*' = prefix.
        const auto matches = [](const std::string& pattern,
                                std::string_view value) {
            if (pattern.empty() || pattern == "*") return true;
            if (!pattern.empty() && pattern.back() == '*')
                return value.starts_with(
                    pattern.substr(0, pattern.size() - 1));
            std::string p = pattern, v(value);
            for (auto& c : p)
                if (c == '.') c = '/';
            for (auto& c : v)
                if (c == '.') c = '/';
            if (p.size() >= 2 && p.front() == 'L' && p.back() == ';')
                p = p.substr(1, p.size() - 2);
            if (v.size() >= 2 && v.front() == 'L' && v.back() == ';')
                v = v.substr(1, v.size() - 2);
            return p == v;
        };
        const char* queried = context.kind;
        const char* canonical =
            std::strncmp(queried, "static-", 7) == 0 ? queried + 7 : queried;
        std::string handler_kind = handler.kind;
        const char* handler_canonical =
            std::strncmp(handler_kind.c_str(), "static-", 7) == 0
                ? handler_kind.c_str() + 7
                : handler_kind.c_str();
        if (!matches(handler_canonical, canonical)) continue;
        if (!matches(handler.class_pattern, context.class_type)) continue;
        if (!matches(handler.name_pattern, context.name)) continue;
        if (!matches(handler.signature_pattern, context.signature)) continue;

        lua_rawgeti(T, LUA_REGISTRYINDEX, handler.function_ref);
        // Build the ctx table the handler sees.
        lua_newtable(T);
        lua_pushstring(T, context.kind);
        lua_setfield(T, -2, "kind");
        lua_pushstring(T, std::string(context.class_type).c_str());
        lua_setfield(T, -2, "class");
        lua_pushstring(T, std::string(context.name).c_str());
        lua_setfield(T, -2, "name");
        lua_pushstring(T, std::string(context.signature).c_str());
        lua_setfield(T, -2, "sig");
        lua_pushinteger(T, static_cast<lua_Integer>(context.receiver));
        lua_setfield(T, -2, "receiver");
        lua_pushinteger(T, static_cast<lua_Integer>(context.method_ref));
        lua_setfield(T, -2, "method");
        // ctx:arg(i) reads a va_list argument through the context.
        lua_pushlightuserdata(T, &context);
        lua_pushcclosure(
            T,
            [](lua_State* L) -> int {
                auto* ctx = static_cast<ScriptedJniContext*>(
                    lua_touserdata(L, lua_upvalueindex(1)));
                const int index = static_cast<int>(luaL_checkinteger(L, 1));
                std::uint64_t value = 0;
                std::string error;
                if (ctx == nullptr ||
                    !ctx->Arg(static_cast<std::size_t>(index), &value,
                              &error))
                    return luaL_error(L, "jni arg read failed: %s",
                                      error.c_str());
                lua_pushinteger(L, static_cast<lua_Integer>(value));
                return 1;
            },
            1);
        lua_setfield(T, -2, "arg");
        // ctx:new_string / ctx:new_object — locked-context factories.
        lua_pushlightuserdata(T, &context);
        lua_pushcclosure(
            T,
            [](lua_State* L) -> int {
                auto* ctx = static_cast<ScriptedJniContext*>(
                    lua_touserdata(L, lua_upvalueindex(1)));
                std::uint64_t reference = 0;
                std::string error;
                if (ctx == nullptr ||
                    !ctx->CreateString(luaL_checkstring(L, 1), &reference,
                                       &error))
                    return luaL_error(L, "jni new_string failed: %s",
                                      error.c_str());
                lua_pushinteger(L, static_cast<lua_Integer>(reference));
                return 1;
            },
            1);
        lua_setfield(T, -2, "new_string");
        lua_pushlightuserdata(T, &context);
        lua_pushcclosure(
            T,
            [](lua_State* L) -> int {
                auto* ctx = static_cast<ScriptedJniContext*>(
                    lua_touserdata(L, lua_upvalueindex(1)));
                std::uint64_t reference = 0;
                std::string error;
                if (ctx == nullptr ||
                    !ctx->CreateObject(luaL_checkstring(L, 1), &reference,
                                       &error))
                    return luaL_error(L, "jni new_object failed: %s",
                                      error.c_str());
                lua_pushinteger(L, static_cast<lua_Integer>(reference));
                return 1;
            },
            1);
        lua_setfield(T, -2, "new_object");
        lua_pushlightuserdata(T, &context);
        lua_pushcclosure(
            T,
            [](lua_State* L) -> int {
                auto* ctx = static_cast<ScriptedJniContext*>(
                    lua_touserdata(L, lua_upvalueindex(1)));
                std::string value;
                std::string error;
                if (ctx == nullptr ||
                    !ctx->ReadString(CheckAddress(L, 1), &value, &error)) {
                    lua_pushnil(L);
                    return 1;
                }
                lua_pushlstring(L, value.data(), value.size());
                return 1;
            },
            1);
        lua_setfield(T, -2, "read_string");
        lua_pushlightuserdata(T, &context);
        lua_pushcclosure(
            T,
            [](lua_State* L) -> int {
                auto* ctx = static_cast<ScriptedJniContext*>(
                    lua_touserdata(L, lua_upvalueindex(1)));
                std::string error;
                if (ctx == nullptr ||
                    !ctx->WriteObjectString(CheckAddress(L, 1),
                                            luaL_checkstring(L, 2), &error))
                    return luaL_error(L, "jni set_obj_string failed: %s",
                                      error.c_str());
                return 0;
            },
            1);
        lua_setfield(T, -2, "set_obj_string");
        lua_pushlightuserdata(T, &context);
        lua_pushcclosure(
            T,
            [](lua_State* L) -> int {
                auto* ctx = static_cast<ScriptedJniContext*>(
                    lua_touserdata(L, lua_upvalueindex(1)));
                if (ctx == nullptr) {
                    lua_pushnil(L);
                    return 1;
                }
                const auto type = ctx->ObjectType(CheckAddress(L, 1));
                if (!type) {
                    lua_pushnil(L);
                    return 1;
                }
                lua_pushlstring(L, type->data(), type->size());
                return 1;
            },
            1);
        lua_setfield(T, -2, "obj_type");
        if (lua_pcall(T, 1, 1, 0) != 0) {
            const char* message = lua_tostring(T, -1);
            if (context.call_error != nullptr)
                *context.call_error = std::string("scripted jni '") +
                                      std::string(context.name) +
                                      "': " + (message ? message : "?");
            lua_pop(T, 1);
            Leave();
            return false;
        }
        if (lua_isnil(T, -1)) {
            lua_pop(T, 1);
            continue;  // handler declined; try the next registration.
        }
        handled = ApplyJniResult(T, context, -1);
        lua_pop(T, 1);
        break;
    }
    Leave();
    return handled;
}

void GameScript::RegisterBindings(lua_State* L) {
    luaL_newmetatable(L, kStateMeta);
    lua_pushcfunction(L, StateIndex);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, StateNewIndex);
    lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);

    static const struct {
        const char* name;
        lua_CFunction fn;
    } kBindings[] = {
        {"log", LuaLog},
        {"log_err", LuaLogErr},
        {"hex", LuaHex},
        {"band", LuaBand},
        {"bor", LuaBor},
        {"bxor", LuaBxor},
        {"bnot", LuaBnot},
        {"shl", LuaShl},
        {"shr", LuaShr},
        {"env", LuaEnv},
        {"vm", LuaVm},
        {"base", LuaBase},
        {"module_base", LuaModuleBase},
        {"sigscan", LuaSigScan},
        {"find_export", LuaFindExport},
        {"find_native", LuaFindNative},
        {"native_addr", LuaNativeAddr},
        {"registered_natives", LuaRegisteredNatives},
        {"call_export", LuaCallExport},
        {"call_export_entry", LuaCallExportEntry},
        {"call_addr", LuaCallAddress},
        {"call_native", LuaCallNative},
        {"new_class", LuaNewClass},
        {"new_object", LuaNewObject},
        {"new_string", LuaNewString},
        {"new_method", LuaNewMethod},
        {"new_object_array", LuaNewObjectArray},
        {"new_long", LuaNewLong},
        {"new_long_array", LuaNewLongArray},
        {"new_int_array", LuaNewIntArray},
        {"new_byte_array", LuaNewByteArray},
        {"new_motion_event", LuaNewMotionEvent},
        {"take_unity_messages", LuaTakeUnityMessages},
        {"release_ref", LuaReleaseRef},
        {"r8", LuaReadInt<std::uint8_t>},
        {"r16", LuaReadInt<std::uint16_t>},
        {"r32", LuaReadInt<std::uint32_t>},
        {"r64", LuaReadInt<std::uint64_t>},
        {"w8", LuaWriteInt<std::uint8_t>},
        {"w16", LuaWriteInt<std::uint16_t>},
        {"w32", LuaWriteInt<std::uint32_t>},
        {"w64", LuaWriteInt<std::uint64_t>},
        {"read", LuaRead},
        {"write", LuaWrite},
        {"cstr", LuaCStr},
        {"heap_alloc", LuaHeapAlloc},
        {"heap_alloc_aligned", LuaHeapAllocAligned},
        {"heap_base", LuaHeapBase},
        {"heap_capacity", LuaHeapCapacity},
        {"exec_bp", LuaExecBreakpoint},
        {"resolve_callable", LuaResolveCallable},
        {"show_window", LuaShowWindow},
        {"present", LuaPresent},
        {"pump", LuaPump},
        {"pump_owner", LuaPumpOwner},
        {"set_slice_callback", LuaSetSliceCallback},
        {"ack_pointer", LuaAckPointer},
        {"sleep", LuaSleep},
        {"now_ms", LuaNowMs},
        {"env_flag", LuaEnvFlag},
        {"env_int", LuaEnvInt},
        {"guest_setenv", LuaGuestSetenv},
        {"diag", LuaDiag},
        {"spawn", LuaSpawn},
        {"request_stop", LuaRequestStop},
        {"stop_requested", LuaStopRequested},
        {"attach_thread", LuaAttachThread},
        {"detach_thread", LuaDetachThread},
        {"take_ui_tasks", LuaTakeUiTasks},
        {"pending_ui_count", LuaPendingUiCount},
        {"take_scheduled_events", LuaTakeScheduledEvents},
        {"take_consent", LuaTakeConsent},
        {"take_display_listeners", LuaTakeDisplayListeners},
        {"notify_refresh_period", LuaNotifyRefreshPeriod},
        {"notify_supported_periods", LuaNotifySupportedPeriods},
        {"find_interface_proxy", LuaFindInterfaceProxy},
        {"take_choreographer_callbacks", LuaTakeChoreographerCallbacks},
        {"take_frame_callbacks", LuaTakeFrameCallbacks},
        {"sync_sweep", LuaSyncSweep},
        {"set_sync_poll", LuaSetSyncPoll},
        {"served_count", LuaServedCount},
        {"futex_waiters", LuaFutexWaiters},
        {"locks_describe", LuaLocksDescribe},
        {"notify_choreographer", LuaNotifyChoreographer},
        {"take_networker", LuaTakeNetworker},
        {"invoke_runnable", LuaInvokeRunnable},
        {"invoke_peer", LuaInvokePeer},
        {"has_exception", LuaHasException},
        {"clear_exception", LuaClearException},
        {"journal", LuaJournal},
        {"jni_notes", LuaJniNotes},
        {"proxies", LuaProxies},
        {"threads_describe", LuaThreadsDescribe},
        {"guest_pcs", LuaGuestPcs},
        {"thread_pointer", LuaThreadPointer},
        {"halt_session", LuaHaltSession},
        {"threads_create", LuaThreadsCreate},
        {"threads_check_failure", LuaThreadsCheckFailure},
        {"threads_ack_failure", LuaThreadsAckFailure},
        {"exit", LuaExit},
        {"jni", LuaJni},
    };
    lua_newtable(L);
    for (const auto& binding : kBindings) {
        lua_pushcfunction(L, binding.fn);
        lua_setfield(L, -2, binding.name);
    }
    lua_setglobal(L, "armr");
}

}  // namespace isomr
