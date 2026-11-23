// Native chat-head overlay — see chat_head_overlay.h for the contract. Pure
// Win32 + GDI+: two WS_EX_LAYERED topmost windows (a circular bubble and a
// rounded chat panel) presented via UpdateLayeredWindow, plus a small
// bottom-center dismiss target shown while dragging. The bubble is
// WS_EX_NOACTIVATE so it never steals focus; the panel is activatable so its
// input field can take the keyboard. This is the Windows substrate an Android
// chat head maps onto — a stubbed WindowManager.addView would drive these
// same HWNDs through ChatHeadCallbacks.

#include "chat_head_overlay.h"

// GDI+ needs IStream and the global `byte` typedef, both of which
// WIN32_LEAN_AND_MEAN excludes from <windows.h>. Pull them in explicitly.
#include <objidl.h>
#include <gdiplus.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "gdiplus.lib")

namespace isomr {
namespace {

// Logical (96-DPI) dimensions; every on-screen size goes through Scale().
constexpr int kBubbleDp = 60;       // collapsed bubble diameter
constexpr int kPanelWidthDp = 300;  // expanded conversation panel
constexpr int kPanelHeightDp = 430;
constexpr int kPanelRadiusDp = 16;
constexpr int kTargetDp = 64;       // dismiss drop-target diameter
constexpr int kEdgeMarginDp = 12;   // resting gap from the snapped screen edge
constexpr int kPanelGapDp = 8;      // gap between bubble and panel
constexpr int kHeaderHeightDp = 44;
constexpr int kInputHeightDp = 48;

constexpr UINT_PTR kSnapTimerId = 1;
constexpr UINT_PTR kCaretTimerId = 2;
constexpr UINT kSnapTimerMs = 15;
constexpr UINT kCaretBlinkMs = 530;
constexpr int kSnapSteps = 14;

const wchar_t kBubbleClass[] = L"ArmrChatHeadBubble";
const wchar_t kPanelClass[] = L"ArmrChatHeadPanel";
const wchar_t kTargetClass[] = L"ArmrChatHeadTarget";

// Accent palette — a Messenger-adjacent blue/violet gradient, chosen to read
// as a chat surface rather than impersonate any real app's branding.
const Gdiplus::Color kBubbleTop(255, 0x6E, 0x8F, 0xFF);
const Gdiplus::Color kBubbleBottom(255, 0x4A, 0x54, 0xE8);
const Gdiplus::Color kPanelBg(255, 0x1E, 0x20, 0x28);
const Gdiplus::Color kPanelHeader(255, 0x26, 0x28, 0x33);
const Gdiplus::Color kBubbleOut(255, 0x4A, 0x54, 0xE8);
const Gdiplus::Color kBubbleIn(255, 0x35, 0x38, 0x44);
const Gdiplus::Color kTextPrimary(255, 0xEC, 0xEC, 0xF1);
const Gdiplus::Color kTextSecondary(255, 0x9A, 0x9D, 0xAA);
const Gdiplus::Color kBadge(255, 0xE8, 0x4C, 0x4C);
const Gdiplus::Color kInputBg(255, 0x2A, 0x2C, 0x38);

// One-time GDI+ token (process-lifetime; fine for a host component).
ULONG_PTR& GdiplusToken() {
    static ULONG_PTR token = 0;
    return token;
}

// Renders `draw` into a fresh per-pixel-alpha GDI+ bitmap and presents it on
// `hwnd` at the window's current position via UpdateLayeredWindow. This is the
// single presentation path — every repaint routes through it.
void LayeredDraw(HWND hwnd, int w, int h,
                 const std::function<void(Gdiplus::Graphics&, int, int)>& draw) {
    if (!hwnd || w <= 0 || h <= 0)
        return;

    Gdiplus::Bitmap bmp(w, h, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        draw(g, w, h);
    }

    HBITMAP dib = nullptr;
    if (bmp.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &dib) != Gdiplus::Ok || !dib)
        return;

    HDC screen = ::GetDC(nullptr);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP old = static_cast<HBITMAP>(::SelectObject(mem, dib));
    RECT rc;
    ::GetWindowRect(hwnd, &rc);
    POINT dst = {rc.left, rc.top};
    POINT src = {0, 0};
    SIZE sz = {w, h};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    ::UpdateLayeredWindow(hwnd, screen, &dst, &sz, mem, &src, 0, &blend,
                          ULW_ALPHA);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::DeleteObject(dib);
    ::ReleaseDC(nullptr, screen);
}

// A rounded-rectangle GraphicsPath for the panel silhouette.
void RoundRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r,
                   float radius) {
    float d = radius * 2.0f;
    path.Reset();
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

// Distance-squared containment for a centered circular hit region.
bool InsideCircle(int px, int py, int cx, int cy, int radius) {
    int dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= radius * radius;
}

}  // namespace

ChatHeadOverlay::~ChatHeadOverlay() {
    Destroy();
}

bool ChatHeadOverlay::Create(HINSTANCE instance) {
    instance_ = instance;

    // Per-monitor DPI awareness must be set before any window is created.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!GdiplusToken()) {
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&GdiplusToken(), &in, nullptr);
    }

    // Bubble class: message-only style is handled per-window below.
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance_;
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.lpfnWndProc = BubbleProc;
    wc.lpszClassName = kBubbleClass;
    if (!::RegisterClassExW(&wc) &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    WNDCLASSEXW pc = wc;
    pc.lpfnWndProc = PanelProc;
    pc.lpszClassName = kPanelClass;
    if (!::RegisterClassExW(&pc) &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    WNDCLASSEXW tc = wc;
    tc.lpfnWndProc = TargetProc;
    tc.lpszClassName = kTargetClass;
    if (!::RegisterClassExW(&tc) &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    int bsz = Scale(kBubbleDp);
    // Bubble: layered + topmost + toolwindow (no taskbar button) + no-activate.
    bubble_ = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kBubbleClass, L"chat-head", WS_POPUP, 0, 0, bsz, bsz, nullptr, nullptr,
        instance_, this);
    if (!bubble_)
        return false;

    // Panel: layered + topmost + toolwindow; activatable (no NOACTIVATE) so
    // its input field can hold keyboard focus. Created hidden.
    panel_ = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, kPanelClass,
        L"chat-head-panel", WS_POPUP, 0, 0, Scale(kPanelWidthDp),
        Scale(kPanelHeightDp), nullptr, nullptr, instance_, this);
    if (!panel_)
        return false;

    // Dismiss drop-target: hidden until a drag begins.
    int tsz = Scale(kTargetDp);
    target_ = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kTargetClass, L"chat-head-target", WS_POPUP, 0, 0, tsz, tsz, nullptr,
        nullptr, instance_, this);
    if (!target_)
        return false;

    return true;
}

int ChatHeadOverlay::Scale(int logical_px) const {
    return ::MulDiv(logical_px, dpi_, 96);
}

void ChatHeadOverlay::Rescale() {
    if (bubble_) {
        dpi_ = static_cast<int>(::GetDpiForWindow(bubble_));
        int bsz = Scale(kBubbleDp);
        ::SetWindowPos(bubble_, nullptr, 0, 0, bsz, bsz,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (panel_)
        ::SetWindowPos(panel_, nullptr, 0, 0, Scale(kPanelWidthDp),
                       Scale(kPanelHeightDp),
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (target_)
        ::SetWindowPos(target_, nullptr, 0, 0, Scale(kTargetDp),
                       Scale(kTargetDp),
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RenderBubble();
    if (expanded_)
        RenderPanel();
    RenderTarget();
}

void ChatHeadOverlay::Show(int x, int y) {
    if (!bubble_)
        return;
    ::SetWindowPos(bubble_, HWND_TOPMOST, x, y, 0, 0,
                   SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    RenderBubble();
    RenderTarget();  // pre-render so it can flash on instantly during a drag
}

void ChatHeadOverlay::Hide() {
    if (panel_)
        ::ShowWindow(panel_, SW_HIDE);
    if (target_)
        ::ShowWindow(target_, SW_HIDE);
    if (bubble_)
        ::ShowWindow(bubble_, SW_HIDE);
    expanded_ = false;
}

void ChatHeadOverlay::Destroy() {
    for (HWND* w : {&target_, &panel_, &bubble_}) {
        if (*w) {
            ::DestroyWindow(*w);
            *w = nullptr;
        }
    }
}

void ChatHeadOverlay::SetGlyph(const std::wstring& glyph) {
    glyph_ = glyph.empty() ? L"A" : glyph;
    RenderBubble();
}

void ChatHeadOverlay::SetBadgeCount(int count) {
    badge_count_ = std::max(0, count);
    RenderBubble();
}

void ChatHeadOverlay::SetTitle(const std::wstring& title) {
    title_ = title;
    if (expanded_)
        RenderPanel();
}

void ChatHeadOverlay::AddMessage(bool outgoing, const std::wstring& text) {
    messages_.push_back({outgoing, text});
    if (expanded_)
        RenderPanel();
}

void ChatHeadOverlay::ClearMessages() {
    messages_.clear();
    if (expanded_)
        RenderPanel();
}

void ChatHeadOverlay::SetExpanded(bool expanded) {
    if (expanded_ == expanded)
        return;
    expanded_ = expanded;
    if (expanded_) {
        LayoutPanel();
        ::ShowWindow(panel_, SW_SHOWNOACTIVATE);
        RenderPanel();
        // Activate so the input field can take the keyboard.
        ::SetForegroundWindow(panel_);
    } else {
        ::ShowWindow(panel_, SW_HIDE);
        input_focused_ = false;
    }
    RenderBubble();
    if (callbacks_.on_expand_changed)
        callbacks_.on_expand_changed(expanded_);
}

// Anchor the panel just above the bubble (or below if near the top), kept on
// screen horizontally.
void ChatHeadOverlay::LayoutPanel() {
    RECT b;
    ::GetWindowRect(bubble_, &b);
    int pw = Scale(kPanelWidthDp), ph = Scale(kPanelHeightDp);
    int gap = Scale(kPanelGapDp);
    int bcx = (b.left + b.right) / 2;

    int x = bcx - pw / 2;
    int y = b.top - ph - gap;            // prefer above the bubble
    if (y < 0)
        y = b.bottom + gap;              // flip below if off the top

    // Clamp horizontally to the work area.
    RECT work;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    x = std::clamp(x, static_cast<int>(work.left),
                   static_cast<int>(work.right) - pw);
    ::SetWindowPos(panel_, HWND_TOPMOST, x, y, pw, ph,
                   SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void ChatHeadOverlay::UpdatePanelPosition() {
    if (expanded_)
        LayoutPanel();
}

// ---- Drag, snap, dismiss -------------------------------------------------

void ChatHeadOverlay::BeginDrag(POINT screen_pt) {
    dragging_ = true;
    moved_ = false;
    snapping_ = false;
    ::KillTimer(bubble_, kSnapTimerId);
    drag_start_ = screen_pt;
    RECT b;
    ::GetWindowRect(bubble_, &b);
    grab_offset_.x = screen_pt.x - b.left;
    grab_offset_.y = screen_pt.y - b.top;
    ::SetCapture(bubble_);

    // Show the dismiss drop-target at bottom-center of the work area.
    RECT work;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int tsz = Scale(kTargetDp);
    int tx = (work.left + work.right - tsz) / 2;
    int ty = work.bottom - tsz - Scale(24);
    ::SetWindowPos(target_, HWND_TOPMOST, tx, ty, tsz, tsz,
                   SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void ChatHeadOverlay::UpdateDrag(POINT screen_pt) {
    if (!dragging_)
        return;
    int dx = screen_pt.x - drag_start_.x;
    int dy = screen_pt.y - drag_start_.y;
    int thresh = ::GetSystemMetrics(SM_CXDRAG);
    if (!moved_ && (std::abs(dx) > thresh || std::abs(dy) > thresh))
        moved_ = true;
    if (moved_) {
        ::SetWindowPos(bubble_, nullptr, screen_pt.x - grab_offset_.x,
                       screen_pt.y - grab_offset_.y, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        UpdatePanelPosition();
    }
}

bool ChatHeadOverlay::OverCloseTarget() const {
    if (!::IsWindowVisible(target_))
        return false;
    RECT b, t;
    ::GetWindowRect(bubble_, &b);
    ::GetWindowRect(target_, &t);
    int bcx = (b.left + b.right) / 2, bcy = (b.top + b.bottom) / 2;
    int tcx = (t.left + t.right) / 2, tcy = (t.top + t.bottom) / 2;
    int r = Scale(kTargetDp) / 2 + Scale(kBubbleDp) / 4;
    int dx = bcx - tcx, dy = bcy - tcy;
    return dx * dx + dy * dy <= r * r;
}

void ChatHeadOverlay::EndDrag() {
    if (!dragging_)
        return;
    dragging_ = false;
    ::ReleaseCapture();
    ::ShowWindow(target_, SW_HIDE);

    if (OverCloseTarget()) {
        Hide();
        if (callbacks_.on_dismissed)
            callbacks_.on_dismissed();
        return;
    }
    if (moved_) {
        SnapToEdge();
        RECT b;
        ::GetWindowRect(bubble_, &b);
        if (callbacks_.on_moved)
            callbacks_.on_moved(b.left, b.top);
    }
}

void ChatHeadOverlay::SnapToEdge() {
    RECT b, work;
    ::GetWindowRect(bubble_, &b);
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int bw = b.right - b.left;
    int margin = Scale(kEdgeMarginDp);
    int bcx = (b.left + b.right) / 2;
    int midx = (work.left + work.right) / 2;
    int target_x = (bcx < midx) ? work.left + margin
                                : work.right - bw - margin;
    int target_y = std::clamp<int>(b.top, work.top,
                                   work.bottom - (b.bottom - b.top));

    snapping_ = true;
    ::SetTimer(bubble_, kSnapTimerId, kSnapTimerMs, nullptr);
    // Stash the snap endpoints as window props so the timer can read them.
    ::SetPropW(bubble_, L"snapx",
               reinterpret_cast<HANDLE>(static_cast<intptr_t>(target_x)));
    ::SetPropW(bubble_, L"snapy",
               reinterpret_cast<HANDLE>(static_cast<intptr_t>(target_y)));
    ::SetPropW(bubble_, L"snapfx",
               reinterpret_cast<HANDLE>(static_cast<intptr_t>(b.left)));
    ::SetPropW(bubble_, L"snapfy",
               reinterpret_cast<HANDLE>(static_cast<intptr_t>(b.top)));
    ::SetPropW(bubble_, L"snapstep", reinterpret_cast<HANDLE>(0));
}

// ---- Window procedures ---------------------------------------------------

LRESULT CALLBACK ChatHeadOverlay::BubbleProc(HWND hwnd, UINT msg, WPARAM wp,
                                             LPARAM lp) {
    ChatHeadOverlay* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<ChatHeadOverlay*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(self));
        self->bubble_ = hwnd;
    } else {
        self = reinterpret_cast<ChatHeadOverlay*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self)
        return self->HandleBubble(hwnd, msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK ChatHeadOverlay::PanelProc(HWND hwnd, UINT msg, WPARAM wp,
                                            LPARAM lp) {
    ChatHeadOverlay* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<ChatHeadOverlay*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(self));
        self->panel_ = hwnd;
    } else {
        self = reinterpret_cast<ChatHeadOverlay*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self)
        return self->HandlePanel(hwnd, msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK ChatHeadOverlay::TargetProc(HWND hwnd, UINT msg, WPARAM wp,
                                             LPARAM lp) {
    ChatHeadOverlay* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<ChatHeadOverlay*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(self));
        self->target_ = hwnd;
    } else {
        self = reinterpret_cast<ChatHeadOverlay*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self)
        return self->HandleTarget(hwnd, msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ChatHeadOverlay::HandleBubble(HWND hwnd, UINT msg, WPARAM wp,
                                      LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST: {
        // Per-pixel circular hit test: only the disc itself is clickable; the
        // transparent corners pass through to the windows beneath.
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ::ScreenToClient(hwnd, &pt);
        int r = Scale(kBubbleDp) / 2;
        int cx = r, cy = r;
        if (InsideCircle(pt.x, pt.y, cx, cy, r))
            return HTCLIENT;
        return HTTRANSPARENT;
    }
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ::ClientToScreen(hwnd, &pt);
        BeginDrag(pt);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ::ClientToScreen(hwnd, &pt);
        UpdateDrag(pt);
        return 0;
    }
    case WM_LBUTTONUP: {
        bool was_drag = moved_;
        EndDrag();
        if (!was_drag) {
            if (callbacks_.on_tap)
                callbacks_.on_tap();
            else
                SetExpanded(!expanded_);
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == kSnapTimerId) {
            auto read = [&](const wchar_t* name) {
                return static_cast<int>(reinterpret_cast<intptr_t>(
                    ::GetPropW(hwnd, name)));
            };
            int tx = read(L"snapx"), ty = read(L"snapy");
            int fx = read(L"snapfx"), fy = read(L"snapfy");
            int step = read(L"snapstep") + 1;
            ::SetPropW(hwnd, L"snapstep",
                       reinterpret_cast<HANDLE>(static_cast<intptr_t>(step)));
            float t = std::min(1.0f, step / static_cast<float>(kSnapSteps));
            t = 1.0f - (1.0f - t) * (1.0f - t);  // ease-out
            int x = fx + static_cast<int>((tx - fx) * t);
            int y = fy + static_cast<int>((ty - fy) * t);
            ::SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                           SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            UpdatePanelPosition();
            if (t >= 1.0f) {
                ::KillTimer(hwnd, kSnapTimerId);
                for (const wchar_t* p : {L"snapx", L"snapy", L"snapfx",
                                         L"snapfy", L"snapstep"})
                    ::RemovePropW(hwnd, p);
                snapping_ = false;
            }
            return 0;
        }
        break;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        Rescale();
        return 0;
    }
    case WM_DESTROY:
        bubble_ = nullptr;
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ChatHeadOverlay::HandlePanel(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp) {
    int pw = Scale(kPanelWidthDp), ph = Scale(kPanelHeightDp);
    int input_h = Scale(kInputHeightDp);
    int pad = Scale(12);
    // Close button rect (top-right of header) and input rect (bottom).
    RECT close_rc = {pw - Scale(36), Scale(8), pw - Scale(8),
                     Scale(36)};
    RECT input_rc = {pad, ph - input_h, pw - pad, ph - Scale(8)};

    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ::ScreenToClient(hwnd, &pt);
        // Rounded-rect containment: outside the corner radius is transparent.
        int rad = Scale(kPanelRadiusDp);
        bool inside = true;
        if (pt.x < rad && pt.y < rad)
            inside = InsideCircle(pt.x, pt.y, rad, rad, rad);
        else if (pt.x > pw - rad && pt.y < rad)
            inside = InsideCircle(pt.x, pt.y, pw - rad, rad, rad);
        else if (pt.x < rad && pt.y > ph - rad)
            inside = InsideCircle(pt.x, pt.y, rad, ph - rad, rad);
        else if (pt.x > pw - rad && pt.y > ph - rad)
            inside = InsideCircle(pt.x, pt.y, pw - rad, ph - rad, rad);
        return inside ? HTCLIENT : HTTRANSPARENT;
    }
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (::PtInRect(&close_rc, pt)) {
            SetExpanded(false);
            return 0;
        }
        bool focus = ::PtInRect(&input_rc, pt) != FALSE;
        if (focus != input_focused_) {
            input_focused_ = focus;
            if (focus) {
                ::SetFocus(hwnd);
                ::SetTimer(hwnd, kCaretTimerId, kCaretBlinkMs, nullptr);
            } else {
                ::KillTimer(hwnd, kCaretTimerId);
            }
            RenderPanel();
        }
        if (focus)
            ::SetFocus(hwnd);
        return 0;
    }
    case WM_SETFOCUS:
        ::SetTimer(hwnd, kCaretTimerId, kCaretBlinkMs, nullptr);
        RenderPanel();
        return 0;
    case WM_KILLFOCUS:
        ::KillTimer(hwnd, kCaretTimerId);
        RenderPanel();
        return 0;
    case WM_TIMER:
        if (wp == kCaretTimerId) {
            // Toggle a caret-blink flag stored as a window prop.
            auto cur = reinterpret_cast<intptr_t>(::GetPropW(hwnd, L"caret"));
            ::SetPropW(hwnd, L"caret", reinterpret_cast<HANDLE>(
                       static_cast<intptr_t>(cur ? 0 : 1)));
            if (input_focused_ ||
                ::GetFocus() == hwnd)
                RenderPanel();
            return 0;
        }
        break;
    case WM_CHAR: {
        if (::GetFocus() != hwnd)
            break;
        if (wp == VK_RETURN) {
            if (!input_.empty()) {
                AddMessage(true, input_);
                if (callbacks_.on_send)
                    callbacks_.on_send(input_);
                input_.clear();
            }
        } else if (wp == VK_BACK) {
            if (!input_.empty())
                input_.pop_back();
        } else if (wp >= 0x20) {
            input_ += static_cast<wchar_t>(wp);
        }
        RenderPanel();
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            SetExpanded(false);
            return 0;
        }
        break;
    case WM_DESTROY:
        panel_ = nullptr;
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ChatHeadOverlay::HandleTarget(HWND hwnd, UINT msg, WPARAM wp,
                                      LPARAM lp) {
    if (msg == WM_NCHITTEST) {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ::ScreenToClient(hwnd, &pt);
        int r = Scale(kTargetDp) / 2;
        return InsideCircle(pt.x, pt.y, r, r, r) ? HTCLIENT : HTTRANSPARENT;
    }
    if (msg == WM_DESTROY) {
        target_ = nullptr;
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- Rendering -----------------------------------------------------------

void ChatHeadOverlay::RenderBubble() {
    int sz = Scale(kBubbleDp);
    std::wstring glyph = glyph_;
    int badge = badge_count_;
    LayeredDraw(bubble_, sz, sz,
                [&](Gdiplus::Graphics& g, int w, int h) {
                    float d = static_cast<float>(std::min(w, h));
                    float cx = w / 2.0f, cy = h / 2.0f;
                    float r = d / 2.0f - 2.0f;

                    // Soft drop shadow (concentric translucent discs).
                    for (int i = 4; i >= 1; --i) {
                        Gdiplus::SolidBrush sh(
                            Gdiplus::Color(18, 0, 0, 0));
                        float sr = r + i * 1.5f;
                        g.FillEllipse(&sh, cx - sr, cy - sr + i, sr * 2,
                                      sr * 2);
                    }

                    // Body: vertical gradient accent disc.
                    Gdiplus::RectF body(cx - r, cy - r, r * 2, r * 2);
                    Gdiplus::LinearGradientBrush grad(
                        body, kBubbleTop, kBubbleBottom,
                        Gdiplus::LinearGradientModeVertical);
                    g.FillEllipse(&grad, body);

                    // Thin rim + a soft top highlight.
                    Gdiplus::Pen rim(Gdiplus::Color(90, 255, 255, 255), 1.5f);
                    g.DrawEllipse(&rim, body);
                    Gdiplus::SolidBrush hi(Gdiplus::Color(60, 255, 255, 255));
                    g.FillEllipse(&hi, body.X + r * 0.25f,
                                  body.Y + r * 0.12f, r * 1.0f, r * 0.45f);

                    // Glyph (avatar initial), centered.
                    Gdiplus::FontFamily fam(L"Segoe UI");
                    Gdiplus::Font font(&fam, r * 0.95f, Gdiplus::FontStyleBold,
                                       Gdiplus::UnitPixel);
                    Gdiplus::StringFormat sf;
                    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
                    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255,
                                                             255));
                    Gdiplus::RectF all(0, 0, static_cast<float>(w),
                                       static_cast<float>(h));
                    g.DrawString(glyph.c_str(), -1, &font, all, &sf, &white);

                    // Notification badge, top-right.
                    if (badge > 0) {
                        float br = d * 0.20f;
                        float bx = w - br * 2 - 2, by = 2;
                        Gdiplus::SolidBrush red(kBadge);
                        Gdiplus::Pen ring(Gdiplus::Color(255, 0x1E, 0x20,
                                                         0x28), 2.0f);
                        g.FillEllipse(&red, bx, by, br * 2, br * 2);
                        g.DrawEllipse(&ring, bx, by, br * 2, br * 2);
                        Gdiplus::Font bf(&fam, br * 1.1f,
                                         Gdiplus::FontStyleBold,
                                         Gdiplus::UnitPixel);
                        wchar_t buf[8];
                        swprintf_s(buf, L"%d", std::min(badge, 99));
                        Gdiplus::RectF brc(bx, by - 1, br * 2, br * 2);
                        g.DrawString(buf, -1, &bf, brc, &sf, &white);
                    }
                });
}

void ChatHeadOverlay::RenderPanel() {
    int pw = Scale(kPanelWidthDp), ph = Scale(kPanelHeightDp);
    int header = Scale(kHeaderHeightDp), input_h = Scale(kInputHeightDp);
    int pad = Scale(12);
    int rad = Scale(kPanelRadiusDp);
    std::wstring title = title_;
    std::wstring input = input_;
    std::vector<ChatHeadMessage> msgs = messages_;
    bool focused = input_focused_ || (::GetFocus() == panel_);
    HWND panel = panel_;

    LayeredDraw(panel, pw, ph, [&](Gdiplus::Graphics& g, int w, int h) {
        // Float-typed geometry for GDI+ (REAL params).
        float fp = static_cast<float>(pad);
        float fw = static_cast<float>(w), fh = static_cast<float>(h);
        float fheader = static_cast<float>(header);
        float finput_h = static_cast<float>(input_h);

        Gdiplus::RectF bounds(0.5f, 0.5f, fw - 1.0f, fh - 1.0f);
        Gdiplus::GraphicsPath path;
        RoundRectPath(path, bounds, static_cast<float>(rad));

        Gdiplus::SolidBrush bg(kPanelBg);
        g.FillPath(&bg, &path);
        Gdiplus::Pen edge(Gdiplus::Color(60, 255, 255, 255), 1.0f);
        g.DrawPath(&edge, &path);

        // Clip everything to the rounded silhouette.
        g.SetClip(&path);

        // Header band.
        Gdiplus::SolidBrush hdr(kPanelHeader);
        g.FillRectangle(&hdr, Gdiplus::RectF(0, 0, fw, fheader));

        Gdiplus::FontFamily fam(L"Segoe UI");
        Gdiplus::Font titleFont(&fam, static_cast<float>(Scale(15)),
                                Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush tp(kTextPrimary), ts(kTextSecondary);
        Gdiplus::StringFormat sf;
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetAlignment(Gdiplus::StringAlignmentNear);
        sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        Gdiplus::RectF trc(fp, 0, fw - fp * 2 - Scale(36), fheader);
        g.DrawString(title.c_str(), -1, &titleFont, trc, &sf, &tp);

        // Close glyph (top-right).
        Gdiplus::Font xFont(&fam, static_cast<float>(Scale(14)),
                            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat cfmt;
        cfmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        cfmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF crc(fw - Scale(36), static_cast<float>(Scale(6)),
                           static_cast<float>(Scale(28)),
                           static_cast<float>(Scale(28)));
        g.DrawString(L"\x2715", -1, &xFont, crc, &cfmt, &ts);

        // Message list, laid out bottom-up above the input bar.
        int list_top = header + pad / 2;
        int list_bottom = h - input_h - pad / 2;
        int y = list_bottom;
        Gdiplus::Font mFont(&fam, static_cast<float>(Scale(13)),
                            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        int max_bubble = w - pad * 2 - Scale(40);
        for (int i = static_cast<int>(msgs.size()) - 1; i >= 0; --i) {
            const auto& m = msgs[i];
            Gdiplus::RectF measure(0, 0, static_cast<float>(max_bubble), 0);
            Gdiplus::RectF bound;
            g.MeasureString(m.text.c_str(), -1, &mFont, measure, &bound);
            int bw = static_cast<int>(bound.Width) + Scale(20);
            int bh = static_cast<int>(bound.Height) + Scale(12);
            bw = std::clamp(bw, Scale(40), max_bubble + Scale(20));
            y -= bh + Scale(6);
            if (y < list_top)
                break;
            int bx = m.outgoing ? (w - pad - bw) : pad;
            Gdiplus::GraphicsPath bp;
            RoundRectPath(bp,
                          Gdiplus::RectF(static_cast<float>(bx),
                                         static_cast<float>(y),
                                         static_cast<float>(bw),
                                         static_cast<float>(bh)),
                          static_cast<float>(Scale(12)));
            Gdiplus::SolidBrush mb(m.outgoing ? kBubbleOut : kBubbleIn);
            g.FillPath(&mb, &bp);
            Gdiplus::StringFormat msf;
            Gdiplus::RectF textRc(static_cast<float>(bx + Scale(10)),
                                  static_cast<float>(y + Scale(6)),
                                  static_cast<float>(bw - Scale(20)),
                                  static_cast<float>(bh - Scale(12)));
            g.DrawString(m.text.c_str(), -1, &mFont, textRc, &msf, &tp);
        }

        // Input bar.
        float fiy = fh - finput_h;
        Gdiplus::RectF irc(fp, fiy, fw - fp * 2.0f,
                           finput_h - static_cast<float>(Scale(8)));
        Gdiplus::GraphicsPath ipath;
        RoundRectPath(ipath, irc, irc.Height / 2.0f);
        Gdiplus::SolidBrush ib(kInputBg);
        g.FillPath(&ib, &ipath);
        Gdiplus::Pen iedge(focused ? Gdiplus::Color(200, 0x6E, 0x8F, 0xFF)
                                   : Gdiplus::Color(70, 255, 255, 255),
                         1.2f);
        g.DrawPath(&iedge, &ipath);

        Gdiplus::Font iFont(&fam, static_cast<float>(Scale(13)),
                            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::RectF itrc(fp + Scale(14), fiy,
                            fw - fp * 2 - static_cast<float>(Scale(28)),
                            finput_h - static_cast<float>(Scale(8)));
        Gdiplus::StringFormat isf;
        isf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        isf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        if (input.empty()) {
            g.DrawString(L"Message\x2026", -1, &iFont, itrc, &isf, &ts);
        } else {
            g.DrawString(input.c_str(), -1, &iFont, itrc, &isf, &tp);
            // Caret after the text when focused + blink flag set.
            auto blink = reinterpret_cast<intptr_t>(
                ::GetPropW(panel, L"caret"));
            if (focused && blink) {
                Gdiplus::RectF tb;
                g.MeasureString(input.c_str(), -1, &iFont, itrc, &tb);
                float cxr = itrc.X + tb.Width + 1.5f;
                float cy0 = fiy + static_cast<float>(Scale(8));
                float cy1 = fiy + finput_h - static_cast<float>(Scale(16));
                Gdiplus::Pen caret(Gdiplus::Color(255, 0x6E, 0x8F, 0xFF),
                                   1.5f);
                g.DrawLine(&caret, cxr, cy0, cxr, cy1);
            }
        }

        g.ResetClip();
    });
}

void ChatHeadOverlay::RenderTarget() {
    int sz = Scale(kTargetDp);
    LayeredDraw(target_, sz, sz, [&](Gdiplus::Graphics& g, int w, int h) {
        float d = static_cast<float>(std::min(w, h));
        float cx = w / 2.0f, cy = h / 2.0f, r = d / 2.0f - 2.0f;
        // Translucent dark disc with an "x" — the Android dismiss affordance.
        Gdiplus::SolidBrush disc(Gdiplus::Color(200, 0x2A, 0x2C, 0x38));
        g.FillEllipse(&disc, cx - r, cy - r, r * 2, r * 2);
        Gdiplus::Pen rim(Gdiplus::Color(140, 255, 255, 255), 1.5f);
        g.DrawEllipse(&rim, cx - r, cy - r, r * 2, r * 2);
        Gdiplus::FontFamily fam(L"Segoe UI");
        Gdiplus::Font xf(&fam, r * 1.1f, Gdiplus::FontStyleRegular,
                         Gdiplus::UnitPixel);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush white(Gdiplus::Color(230, 255, 255, 255));
        Gdiplus::RectF all(0, -2, static_cast<float>(w),
                           static_cast<float>(h));
        g.DrawString(L"\x2715", -1, &xf, all, &sf, &white);
    });
}

}  // namespace isomr
