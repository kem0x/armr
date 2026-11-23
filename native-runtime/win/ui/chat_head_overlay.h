// Native chat-head overlay — a topmost, layered, frameless circular bubble
// plus an expandable chat panel, both rendered into per-pixel-alpha DIBs and
// presented via UpdateLayeredWindow. This is the Windows-side substrate for
// Android-style floating chat heads: it owns the HWNDs, drag/snap/dismiss
// interaction, badge, and the stub chat surface, and exposes callbacks a
// future guest bridge (e.g. a stubbed android.view.WindowManager.addView) can
// wire to real behavior. No Android runtime dependency — pure Win32 + GDI+.
#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace isomr {

// One rendered conversation entry in the expanded panel. Pure host-side stub
// data until a guest app supplies real messages through the bridge.
struct ChatHeadMessage {
    bool outgoing = false;
    std::wstring text;
};

// Host-side hooks. All are optional; an unset callback leaves the overlay in
// its standalone demo behavior (tap toggles panel, drop-on-target dismisses).
struct ChatHeadCallbacks {
    std::function<void()> on_tap;               // bubble tapped, no drag
    std::function<void(bool expanded)> on_expand_changed;
    std::function<void(int x, int y)> on_moved; // drag released at position
    std::function<void(const std::wstring&)> on_send;  // panel input committed
    std::function<void()> on_dismissed;         // dropped on close target
};

class ChatHeadOverlay {
public:
    ChatHeadOverlay() = default;
    ChatHeadOverlay(const ChatHeadOverlay&) = delete;
    ChatHeadOverlay& operator=(const ChatHeadOverlay&) = delete;
    ~ChatHeadOverlay();

    // Registers the window classes and creates the bubble + (hidden) panel and
    // close-target HWNDs. Initializes per-monitor DPI awareness and GDI+.
    bool Create(HINSTANCE instance);

    void Show(int x, int y);  // position the bubble's top-left, then show
    void Hide();
    void Destroy();

    void SetExpanded(bool expanded);
    bool expanded() const { return expanded_; }

    // Bubble contents.
    void SetGlyph(const std::wstring& glyph);    // centered text inside bubble
    void SetBadgeCount(int count);               // 0 hides the badge
    void SetTitle(const std::wstring& title);    // panel header text

    // Stub conversation the panel renders. A real bridge would push guest
    // messages here; the demo seeds a few honest placeholder lines.
    void AddMessage(bool outgoing, const std::wstring& text);
    void ClearMessages();

    void set_callbacks(ChatHeadCallbacks cb) { callbacks_ = std::move(cb); }

    HWND bubble_hwnd() const { return bubble_; }
    HWND panel_hwnd() const { return panel_; }
    bool running() const { return bubble_ != nullptr; }

private:
    static LRESULT CALLBACK BubbleProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK PanelProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK TargetProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleBubble(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandlePanel(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleTarget(HWND, UINT, WPARAM, LPARAM);

    // Rendering — rebuild the per-pixel DIB for one window and present it.
    void RenderBubble();
    void RenderPanel();
    void RenderTarget();
    void PresentLayered(HWND hwnd, HBITMAP dib);

    // Interaction helpers.
    void BeginDrag(POINT screen_pt);
    void UpdateDrag(POINT screen_pt);
    void EndDrag();
    void SnapToEdge();                  // animate bubble to nearest L/R edge
    void UpdatePanelPosition();         // keep panel anchored to the bubble
    void LayoutPanel();                 // place panel near the bubble
    bool OverCloseTarget() const;       // is the bubble over the dismiss target

    int Scale(int logical_px) const;    // logical → physical via current DPI
    void Rescale();                     // respond to a DPI change

    HWND bubble_ = nullptr;
    HWND panel_ = nullptr;
    HWND target_ = nullptr;
    HINSTANCE instance_ = nullptr;

    bool expanded_ = false;
    bool dragging_ = false;
    bool moved_ = false;                // drag exceeded the tap threshold
    bool snapping_ = false;
    POINT grab_offset_ = {0, 0};        // cursor minus bubble origin at grab
    POINT drag_start_ = {0, 0};

    int dpi_ = 96;
    int badge_count_ = 0;
    std::wstring glyph_ = L"A";         // default avatar initial
    std::wstring title_ = L"Chat";
    std::wstring input_;                // panel input field text
    bool input_focused_ = false;
    std::vector<ChatHeadMessage> messages_;
    ChatHeadCallbacks callbacks_;
};

}  // namespace isomr
