// Per-game Lua lifecycle scripts. A game's `.profile` may name a `script=`
// file under games/; when present the entry probe hands control to that
// script's `main()` after the generic bootstrap (load, initializers,
// JNI_OnLoad, compat overrides). The script drives everything game-specific:
// lifecycle export order, Activity stand-ins, execution breakpoints that
// repair guest state, the frame loop, and input delivery. Adding a game
// becomes manifest + payload + script, with no probe rebuild.
//
// Threading: all Lua execution is serialized through a recursive mutex.
// Each OS thread that runs script code gets its own lua thread stack, so a
// breakpoint or JNI callback firing inside a guest call does not disturb a
// suspended frame on another thread. Bindings that run guest code or block
// (call_export, call_native, pump, present, ...) release the lock around the
// runtime call so other threads' callbacks can proceed — this is what lets a
// UI drain thread signal a latch a render thread is parked on.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace isomr {

class Aarch64ProfileRuntime;
struct ScriptedJniContext;

class GameScript {
public:
    GameScript();
    ~GameScript();
    GameScript(const GameScript&) = delete;
    GameScript& operator=(const GameScript&) = delete;

    // Loads and executes the script file. Errors are Lua syntax/runtime
    // failures at chunk level.
    bool Load(const std::wstring& path, std::string* error);
    // Must be called after Load and before any hook/binding use.
    void BindRuntime(Aarch64ProfileRuntime* runtime);
    bool loaded() const { return state_ != nullptr; }
    bool HasHook(const char* name);
    // Invokes the global Lua function `name` on the calling thread's
    // coroutine stack. Script `main()` typically never returns.
    bool CallHook(const char* name, std::string* error);
    // Waits for every armr.spawn'd thread to finish.
    void JoinThreads();
    // Cooperative stop: scripts poll armr.stop_requested() in long-running
    // spawned threads so JoinThreads() converges when main() returns.
    void RequestStop() { stop_requested_.store(true); }
    bool stop_requested() const { return stop_requested_.load(); }

    // Scripted JNI override consulted by GuestJni::ApplyCompatOverride while
    // the JNI mutex is held (locked-context ops only inside the handler).
    bool TryJniOverride(ScriptedJniContext& context);

    // Registration entry points for the armr.* bindings (called from static
    // lua_CFunction shims that recover `this` from the registry).
    void NoteBreakpointRef(int function_ref);
    void RegisterJniHandler(lua_State* L, int spec_index);
    void TrackSpawnedThread(std::thread thread);

    // Lock acquisition for bound functions and callbacks. Enter returns the
    // calling thread's lua_State (created on demand); Leave releases.
    lua_State* Enter();
    void Leave();
    // Releases one lock level around a runtime call that can block or
    // trigger nested callbacks; reacquires on destruction. Only releases at
    // the outermost Lua frame of this thread.
    struct ReleaseLock {
        explicit ReleaseLock(GameScript* script);
        ~ReleaseLock();
        GameScript* script;
        bool released = false;
    };

    Aarch64ProfileRuntime* runtime() const { return runtime_; }

private:
    static GameScript* Self(lua_State* L);
    void RegisterBindings(lua_State* L);

    struct JniHandler {
        std::string kind;
        std::string class_pattern;
        std::string name_pattern;
        std::string signature_pattern;
        int function_ref = -2;  // LUA_NOREF
    };

    lua_State* state_ = nullptr;
    Aarch64ProfileRuntime* runtime_ = nullptr;
    std::atomic<bool> stop_requested_{false};
    std::recursive_mutex lock_;
    std::unordered_map<std::thread::id, lua_State*> thread_states_;
    std::unordered_map<std::thread::id, int> thread_refs_;
    std::unordered_map<std::thread::id, int> lua_depth_;
    std::vector<JniHandler> jni_handlers_;
    std::vector<std::thread> spawned_threads_;
    std::mutex spawned_threads_lock_;
    // Registry refs for breakpoint callbacks, kept alive for the session.
    std::vector<int> breakpoint_function_refs_;

public:
    // The breakpoint bridge: run a Lua callback for an exec_bp registration.
    bool RunBreakpointCallback(int function_ref,
                               struct Aarch64PausedExecutionState* state,
                               std::string* error);
};

}  // namespace isomr
