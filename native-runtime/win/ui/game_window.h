// Native ARMR game window — the polished shell that hosts a genuine guest
// frame. Owns the top-level HWND, all host chrome (title bar, status strip,
// floating control pill, popovers), aspect-fit viewport geometry, host-side
// input transforms, fullscreen/placement, per-monitor DPI, and the versioned
// host-UI preferences file. The guest surface is a child HWND owned by
// NativeGles; this shell positions it and never makes a guest/EGL call from
// a window procedure.
#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "game_window_messages.h"

#include <cstdint>
#include <string>
#include <vector>

namespace isomr {

// Truthful lifecycle states per ARMR-GAME-WINDOW-SPEC §7. No invented
// loading percentages; each maps to one status-strip string.
enum class GameSessionState {
    kStarting,      // shell usable, "Starting game…" panel, no frame yet
    kRunning,       // genuine frames presenting
    kWaiting,       // long guest call, last genuine frame retained
    kClosing,       // cooperative close in flight, gameplay actions disabled
    kCloseDelayed,  // close past its deadline, force-close offered
    kFailed,        // short cause + diagnostic reference, Close offered
    kClosed,        // resources released, shell about to be destroyed
};

// Host actions the shell is allowed to surface, per spec §3.3 — a control is
// shown only when a real capability exists; unsupported controls are never
// drawn (no dead chrome, no false claims).
struct GameWindowCaps {
    bool fullscreen = true;
    bool audio = false;         // requires a real host audio sink
    bool input_mapping = false; // requires a real remapping surface
};

// Immutable geometry snapshot the UI consumes to position the surface child
// and transform input. Versioned so a stale snapshot can be detected.
struct GameWindowGeometry {
    std::uint32_t version = 0;
    std::int32_t dpi = 96;
    bool fullscreen = false;
    // Viewport = game region inside the client (excludes title bar + status).
    RECT viewport = {0, 0, 0, 0};
    // Aspect-fit destination rect for the surface child, in client coords.
    RECT surface = {0, 0, 0, 0};
    std::int32_t logical_width = 0;   // fixed guest logical surface width
    std::int32_t logical_height = 0;  // fixed guest logical surface height
};

class GameWindow {
public:
    GameWindow() = default;
    GameWindow(const GameWindow&) = delete;
    GameWindow& operator=(const GameWindow&) = delete;
    ~GameWindow() { Shutdown(); }

    // Creates and shows the top-level shell. `logical_width/height` is the
    // fixed guest surface size used for aspect-fit and input transforms.
    bool Create(const std::wstring& display_name, std::int32_t logical_width,
                std::int32_t logical_height, std::string* error);

    // Registers the NativeGles display child so layout can position it.
    void AttachSurface(HWND surface_child);

    // Immutable session-state snapshot the runtime publishes to the UI.
    void SetSessionState(GameSessionState state,
                         const std::wstring& detail = L"");

    // Called when the first genuine lit frame presents: Starting -> Running.
    void NotifyFirstFrame();

    void SetCaps(const GameWindowCaps& caps);
    GameWindowCaps caps() const { return caps_; }

    HWND hwnd() const { return shell_; }
    HWND surface_child() const { return surface_child_; }
    GameSessionState session_state() const { return session_state_; }
    bool first_frame_seen() const { return first_frame_seen_; }
    bool close_requested() const { return shell_close_requested_; }

    // Current immutable geometry snapshot.
    GameWindowGeometry geometry() const { return geometry_; }

    // Standard Windows verbs. Close is the cooperative lifecycle request —
    // it forwards to the guest surface's close path rather than destroying
    // anything synchronously.
    void RequestClose();
    void ShowShell();
    void Shutdown();

    // True while the shell top-level still exists.
    bool IsAlive() const { return shell_ != nullptr; }

    static bool IsEnabledByEnvironment();

private:
    // Layout / metrics (all stored in DIP, scaled to physical on use).
    int Scale(int dip) const;
    void RecalcLayout();
    RECT ComputeSurfaceRect(const RECT& viewport) const;
    void PositionSurface();
    void LayoutToolbar();

    // Window procedures.
    static LRESULT CALLBACK ShellProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK ToolbarProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK PopoverProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleToolbarMessage(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandlePopoverMessage(HWND, UINT, WPARAM, LPARAM);

    // Painting (host chrome only — never touches guest pixels).
    void PaintShell();
    void PaintTitleBar(HDC dc, const RECT& rc);
    void PaintStatusStrip(HDC dc, const RECT& rc);
    void PaintViewportBackdrop(HDC dc, const RECT& rc);
    void PaintToolbar();
    void PaintPopover(HWND pop);
    void DrawArmRMark(HDC dc, const RECT& rc) const;

    // Toolbar + popovers.
    void CreateChrome();
    void SetToolbarVisible(bool visible);
    void UpdateToolbarButtons();
    void OpenPopover(int index);
    void ClosePopover();
    void OnToolbarButton(int index);
    void ShowPopoverHelpText(const std::wstring& text);

    // Input / focus.
    void UpdateCaptionButtons(POINT pt);
    void SetFocusTo(int focus_index);
    void CycleFocus(int dir);

    // Fullscreen + placement + prefs.
    void EnterFullscreen();
    void ExitFullscreen();
    void ToggleFullscreen();
    void SavePlacement();
    bool RestorePlacement();
    void LoadPrefs();
    void SavePrefs();

    // Session-state helpers.
    const wchar_t* StatusLeftText() const;
    void SetSessionStateInternal(GameSessionState state);
    void StartCloseDeadlineTimer();

    HWND shell_ = nullptr;
    HWND surface_child_ = nullptr;   // NativeGles display child (not owned)
    HWND toolbar_ = nullptr;
    HWND popover_ = nullptr;
    int open_popover_index_ = -1;

    std::wstring display_name_;
    std::int32_t logical_width_ = 1280;
    std::int32_t logical_height_ = 720;
    GameWindowCaps caps_;
    GameSessionState session_state_ = GameSessionState::kStarting;
    std::wstring fail_detail_;
    bool first_frame_seen_ = false;
    bool shell_close_requested_ = false;

    UINT dpi_ = 96;
    bool fullscreen_ = false;
    WINDOWPLACEMENT saved_placement_{};
    bool has_saved_placement_ = false;
    GameWindowGeometry geometry_;

    // Toolbar hover-reveal state.
    bool toolbar_visible_ = false;
    bool toolbar_pinned_ = false;
    int hot_caption_ = -1;    // 0=min 1=max 2=close
    int pressed_caption_ = -1;
    int hot_toolbar_ = -1;
    int focus_index_ = -1;    // -1 = none, 0..2 toolbar, 3..5 caption buttons
    bool focus_visible_ = false;

    // Host prefs (persisted in ui.json — never game saves).
    bool pref_muted_ = false;
    int pref_volume_ = 80;      // 0..100
    bool pref_toolbar_pinned_ = false;
};

}  // namespace isomr
