// Native ARMR game window shell. See game_window.h and
// docs/ARMR-GAME-WINDOW-SPEC.md for the design contract.
//
// Host chrome is painted with GDI onto a double-buffered DC and is fully
// independent of the guest frame cadence. The guest surface is a separate
// child HWND owned by NativeGles; this shell only positions it inside the
// aspect-fit viewport and forwards input/close through the existing
// cooperative paths. No window procedure here performs a guest call, an EGL
// call, a synchronous runtime wait, or a private payload read.
#include "game_window.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace isomr {
namespace {

constexpr wchar_t kShellClassName[] = L"ArmrGameWindow";
constexpr wchar_t kToolbarClassName[] = L"ArmrGameToolbar";
constexpr wchar_t kPopoverClassName[] = L"ArmrGamePopover";

// Timer ids.
constexpr UINT_PTR kTimerToolbarHide = 1;
constexpr UINT_PTR kTimerCloseDeadline = 2;
constexpr UINT_PTR kTimerWaitHint = 3;

constexpr UINT kToolbarHideDelayMs = 1400;
constexpr UINT kWaitHintDelayMs = 3000;   // spec: "Waiting for game…" after 3 s
constexpr UINT kCloseDeadlineMs = 5000;   // offer Force close after this

// Palette (ARMR-GAME-WINDOW-SPEC §3).
constexpr COLORREF kFrame = RGB(0x17, 0x1E, 0x1B);       // #171E1B
constexpr COLORREF kTitleBar = RGB(0x20, 0x29, 0x24);    // #202924
constexpr COLORREF kTitleText = RGB(0xD2, 0xDB, 0xD5);   // #D2DBD5
constexpr COLORREF kSecondary = RGB(0x8D, 0xA2, 0x96);   // #8DA296
constexpr COLORREF kLetterbox = RGB(0x0E, 0x15, 0x12);   // #0E1512
constexpr COLORREF kToolbar = RGB(0x10, 0x18, 0x14);     // #101814 (opaque fallback)
constexpr COLORREF kToolbarGlyph = RGB(0xE2, 0xEA, 0xE4);// #E2EAE4
constexpr COLORREF kStatusText = RGB(0xA8, 0xB8, 0xAD);  // #A8B8AD
constexpr COLORREF kAccent = RGB(0xB5, 0xF5, 0x8A);      // #B5F58A
constexpr COLORREF kDanger = RGB(0xE0, 0x6C, 0x75);      // close hover

constexpr int kToolbarButtonCount = 3;  // fullscreen, audio, input-help

int MulDivDpi(int dip, UINT dpi) {
    return static_cast<int>(
        (static_cast<long long>(dip) * dpi + 48) / 96);
}

RECT InflateRectSafe(RECT rc, int dx, int dy) {
    rc.left -= dx; rc.right += dx; rc.top -= dy; rc.bottom += dy;
    return rc;
}

HFONT CreateUiFont(UINT dpi, int height_dip, int weight = FW_NORMAL) {
    const int h = -MulDivDpi(height_dip, dpi);
    return CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

}  // namespace

// ---------------------------------------------------------------------------
// Environment gate
// ---------------------------------------------------------------------------

bool GameWindow::IsEnabledByEnvironment() {
    wchar_t flag[8] = {0};
    const DWORD len = GetEnvironmentVariableW(L"ARMR_GAME_WINDOW", flag, 8);
    // Default on; ARMR_GAME_WINDOW=0 keeps the legacy standalone window.
    return !(len > 0 && flag[0] == L'0');
}

// ---------------------------------------------------------------------------
// Creation / teardown
// ---------------------------------------------------------------------------

bool GameWindow::Create(const std::wstring& display_name,
                        std::int32_t logical_width,
                        std::int32_t logical_height, std::string* error) {
    if (error == nullptr) return false;
    if (logical_width <= 0 || logical_height <= 0) {
        *error = "game window logical surface size is invalid";
        return false;
    }
    display_name_ = display_name.empty() ? L"Game" : display_name;
    logical_width_ = logical_width;
    logical_height_ = logical_height;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kShellClassName;
    wc.lpfnWndProc = &GameWindow::ShellProc;
    if (GetClassInfoExW(instance, kShellClassName, &wc) == 0) {
        if (RegisterClassExW(&wc) == 0) {
            *error = "could not register the ARMR game-window class";
            return false;
        }
    }
    wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = CreateSolidBrush(kToolbar);
    wc.lpszClassName = kToolbarClassName;
    wc.lpfnWndProc = &GameWindow::ToolbarProc;
    if (GetClassInfoExW(instance, kToolbarClassName, &wc) == 0)
        RegisterClassExW(&wc);
    wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = CreateSolidBrush(kFrame);
    wc.lpszClassName = kPopoverClassName;
    wc.lpfnWndProc = &GameWindow::PopoverProc;
    if (GetClassInfoExW(instance, kPopoverClassName, &wc) == 0)
        RegisterClassExW(&wc);

    LoadPrefs();

    // Size the shell so the aspect-fit viewport equals the logical surface
    // on first show (client = logical + title bar + status strip).
    dpi_ = static_cast<UINT>(GetDpiForSystem());
    const int tb = Scale(40), sb = Scale(28);
    const int wanted_client_w = logical_width_;
    const int wanted_client_h = logical_height_ + tb + sb;
    RECT wr{0, 0, wanted_client_w, wanted_client_h};
    // Adjust for a resizable borderless window: add the resize border so the
    // initial client matches the requested size.
    AdjustWindowRectExForDpi(&wr, WS_THICKFRAME | WS_POPUP, FALSE, 0, dpi_);

    shell_ = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_WINDOWEDGE, kShellClassName,
        display_name_.c_str(),
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
            WS_SYSMENU | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left,
        wr.bottom - wr.top, nullptr, nullptr, instance, this);
    if (shell_ == nullptr) {
        *error = "could not create the ARMR game window";
        return false;
    }

    // Rounded corners + native shadow over the frameless shell.
    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(shell_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                          sizeof(corner));
    MARGINS margins{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(shell_, &margins);
    const BOOL dark_caption = TRUE;
    DwmSetWindowAttribute(shell_, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          const_cast<BOOL*>(&dark_caption),
                          sizeof(dark_caption));
    dpi_ = static_cast<UINT>(GetDpiForWindow(shell_));

    CreateChrome();
    if (!RestorePlacement())
        ShowWindow(shell_, SW_SHOW);
    RecalcLayout();
    UpdateWindow(shell_);
    return true;
}

void GameWindow::SetCaps(const GameWindowCaps& caps) {
    caps_ = caps;
    RecalcLayout();
    if (toolbar_ != nullptr) InvalidateRect(toolbar_, nullptr, TRUE);
    if (shell_ != nullptr) InvalidateRect(shell_, nullptr, TRUE);
}

void GameWindow::CreateChrome() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    toolbar_ = CreateWindowExW(
        0, kToolbarClassName, L"", WS_CHILD,
        0, 0, 10, 10, shell_, nullptr, instance, this);
    if (toolbar_ != nullptr)
        SetWindowLongPtrW(toolbar_, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(this));
}

void GameWindow::AttachSurface(HWND surface_child) {
    surface_child_ = surface_child;
    if (surface_child_ != nullptr && shell_ != nullptr)
        SetParent(surface_child_, shell_);
    PositionSurface();
}

void GameWindow::ShowShell() {
    if (shell_ != nullptr) ShowWindow(shell_, SW_SHOW);
}

void GameWindow::Shutdown() {
    ClosePopover();
    if (toolbar_ != nullptr) { DestroyWindow(toolbar_); toolbar_ = nullptr; }
    if (shell_ != nullptr) {
        SavePlacement();
        SavePrefs();
        DestroyWindow(shell_);
        shell_ = nullptr;
    }
    surface_child_ = nullptr;
}

// ---------------------------------------------------------------------------
// Metrics / layout
// ---------------------------------------------------------------------------

int GameWindow::Scale(int dip) const { return MulDivDpi(dip, dpi_); }

RECT GameWindow::ComputeSurfaceRect(const RECT& viewport) const {
    const int vw = viewport.right - viewport.left;
    const int vh = viewport.bottom - viewport.top;
    if (vw <= 0 || vh <= 0 || logical_width_ <= 0 || logical_height_ <= 0)
        return RECT{viewport.left, viewport.top, viewport.left, viewport.top};
    // Contain-fit: largest logical-aspect rect inside the viewport.
    const double logical_aspect =
        static_cast<double>(logical_width_) / logical_height_;
    int dw = vw;
    int dh = static_cast<int>(dw / logical_aspect + 0.5);
    if (dh > vh) {
        dh = vh;
        dw = static_cast<int>(dh * logical_aspect + 0.5);
    }
    const int x = viewport.left + (vw - dw) / 2;
    const int y = viewport.top + (vh - dh) / 2;
    return RECT{x, y, x + dw, y + dh};
}

void GameWindow::RecalcLayout() {
    if (shell_ == nullptr) return;
    RECT client{};
    GetClientRect(shell_, &client);
    RECT viewport = client;
    viewport.top += Scale(40);            // title bar
    viewport.bottom -= Scale(28);         // status strip
    geometry_.version++;
    geometry_.dpi = static_cast<std::int32_t>(dpi_);
    geometry_.fullscreen = fullscreen_;
    geometry_.viewport = viewport;
    geometry_.surface = ComputeSurfaceRect(viewport);
    geometry_.logical_width = logical_width_;
    geometry_.logical_height = logical_height_;
    PositionSurface();
    LayoutToolbar();
    InvalidateRect(shell_, nullptr, TRUE);
}

void GameWindow::PositionSurface() {
    if (surface_child_ == nullptr || shell_ == nullptr) return;
    const RECT& r = geometry_.surface;
    // The game surface is the base layer: it sits at the bottom of the child
    // z-order so the floating toolbar and popovers stay on top of it.
    SetWindowPos(surface_child_, HWND_BOTTOM, r.left, r.top,
                 r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void GameWindow::LayoutToolbar() {
    if (toolbar_ == nullptr || shell_ == nullptr) return;
    const int button = Scale(36);
    const int gap = Scale(4);
    const int pad = Scale(6);
    const int visible_buttons = 1 + (caps_.audio ? 1 : 0) + 1;
    const int h = visible_buttons * button + (visible_buttons - 1) * gap +
                  2 * pad;
    const int w = Scale(44);
    const RECT& vp = geometry_.viewport;
    const int x = vp.right - Scale(14) - w;
    const int y = vp.top + (vp.bottom - vp.top - h) / 2;
    // Position-only move (HWND_TOP): the toolbar stays hidden until revealed,
    // and floats above the game surface which sits at HWND_BOTTOM.
    SetWindowPos(toolbar_, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
    InvalidateRect(toolbar_, nullptr, TRUE);
}

// ---------------------------------------------------------------------------
// Session state
// ---------------------------------------------------------------------------

void GameWindow::SetSessionStateInternal(GameSessionState state) {
    if (session_state_ == state) return;
    session_state_ = state;
    if (shell_ != nullptr) {
        // Only the status strip region needs a repaint.
        RECT client{};
        GetClientRect(shell_, &client);
        RECT strip = client;
        strip.top = client.bottom - Scale(28);
        InvalidateRect(shell_, &strip, TRUE);
        // The Starting panel lives in the viewport; repaint it on
        // Starting/Waiting/Failed transitions too.
        if (state == GameSessionState::kStarting ||
            state == GameSessionState::kWaiting ||
            state == GameSessionState::kFailed)
            InvalidateRect(shell_, &geometry_.viewport, TRUE);
    }
}

void GameWindow::SetSessionState(GameSessionState state,
                                 const std::wstring& detail) {
    if (!detail.empty()) fail_detail_ = detail;
    if (state == GameSessionState::kClosing) {
        SetSessionStateInternal(state);
        StartCloseDeadlineTimer();
    } else {
        SetSessionStateInternal(state);
    }
}

void GameWindow::NotifyFirstFrame() {
    if (first_frame_seen_) return;
    first_frame_seen_ = true;
    SetSessionStateInternal(GameSessionState::kRunning);
    if (surface_child_ != nullptr) ShowWindow(surface_child_, SW_SHOW);
    if (shell_ != nullptr) InvalidateRect(shell_, &geometry_.viewport, TRUE);
}

const wchar_t* GameWindow::StatusLeftText() const {
    switch (session_state_) {
        case GameSessionState::kStarting:     return L"Starting game\x2026";
        case GameSessionState::kRunning:      return L"Running";
        case GameSessionState::kWaiting:      return L"Waiting for game\x2026";
        case GameSessionState::kClosing:      return L"Closing\x2026";
        case GameSessionState::kCloseDelayed: return L"The game is taking longer to close.";
        case GameSessionState::kFailed:       return L"Something went wrong.";
        case GameSessionState::kClosed:       return L"Closed";
    }
    return L"";
}

void GameWindow::StartCloseDeadlineTimer() {
    if (shell_ != nullptr)
        SetTimer(shell_, kTimerCloseDeadline, kCloseDeadlineMs, nullptr);
}

// ---------------------------------------------------------------------------
// Chrome painting
// ---------------------------------------------------------------------------

void GameWindow::DrawArmRMark(HDC dc, const RECT& rc) const {
    // Simple brand mark: accent rounded tile with a dark "A".
    HBRUSH tile = CreateSolidBrush(kAccent);
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, tile));
    HPEN pen = CreatePen(PS_SOLID, 1, kAccent);
    HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
    const int r = (rc.bottom - rc.top) / 3;
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
    DeleteObject(tile);
    HFONT font = CreateUiFont(dpi_, 11, FW_BOLD);
    HFONT old_font = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kTitleBar);
    DrawTextW(dc, L"A", 1, const_cast<RECT*>(&rc),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old_font);
    DeleteObject(font);
}

void GameWindow::PaintTitleBar(HDC dc, const RECT& rc) {
    HBRUSH brush = CreateSolidBrush(kTitleBar);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);

    int x = rc.left + Scale(14);
    const int mark = Scale(18);
    RECT mark_rc{x, rc.top + (rc.bottom - rc.top - mark) / 2,
                 x + mark, 0};
    mark_rc.bottom = mark_rc.top + mark;
    DrawArmRMark(dc, mark_rc);
    x += mark + Scale(10);

    HFONT font = CreateUiFont(dpi_, 13, FW_SEMIBOLD);
    HFONT old_font = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    // Reserve space for the three caption buttons on the right.
    const int caption_reserve = 3 * Scale(46) + Scale(8);
    RECT text_rc{x, rc.top, rc.right - caption_reserve, rc.bottom};
    SetTextColor(dc, kTitleText);
    DrawTextW(dc, display_name_.c_str(), -1, &text_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
    // Measure the drawn title to place the subdued " / ARMR" suffix.
    int title_w = 0;
    {
        RECT measure{0, 0, 0, 0};
        DrawTextW(dc, display_name_.c_str(), -1, &measure,
                  DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        title_w = measure.right;
    }
    HFONT suffix_font = CreateUiFont(dpi_, 13, FW_NORMAL);
    SelectObject(dc, suffix_font);
    SetTextColor(dc, kSecondary);
    RECT suffix_rc{text_rc.left + title_w + Scale(2), rc.top,
                   text_rc.right, rc.bottom};
    DrawTextW(dc, L" / ARMR", -1, &suffix_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old_font);
    DeleteObject(suffix_font);
    DeleteObject(font);

    // Caption buttons (min / max / close) — drawn as subtle glyphs.
    const int bw = Scale(46);
    int bx = rc.right - bw * 3;
    const wchar_t* glyphs[3] = {L"\x2013", L"\x25A2", L"\x2715"};
    for (int i = 0; i < 3; ++i) {
        RECT br{bx, rc.top, bx + bw, rc.bottom};
        if (hot_caption_ == i || (focus_visible_ && focus_index_ == 3 + i)) {
            HBRUSH hb = CreateSolidBrush(i == 2 ? kDanger : RGB(0x2C, 0x37, 0x31));
            FillRect(dc, &br, hb);
            DeleteObject(hb);
        }
        HFONT gfont = CreateUiFont(dpi_, 12, FW_NORMAL);
        HFONT ogf = static_cast<HFONT>(SelectObject(dc, gfont));
        SetTextColor(dc, kTitleText);
        DrawTextW(dc, glyphs[i], -1, &br,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, ogf);
        DeleteObject(gfont);
        bx += bw;
    }
}

void GameWindow::PaintStatusStrip(HDC dc, const RECT& rc) {
    HBRUSH brush = CreateSolidBrush(kFrame);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
    // Top hairline.
    HBRUSH line = CreateSolidBrush(RGB(0x2A, 0x34, 0x2E));
    RECT hairline{rc.left, rc.top, rc.right, rc.top + 1};
    FillRect(dc, &hairline, line);
    DeleteObject(line);

    HFONT font = CreateUiFont(dpi_, 12, FW_NORMAL);
    HFONT old_font = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kStatusText);
    RECT left{rc.left + Scale(14), rc.top, rc.right - Scale(140), rc.bottom};
    if (session_state_ == GameSessionState::kFailed &&
        !fail_detail_.empty()) {
        SetTextColor(dc, kDanger);
        std::wstring msg = StatusLeftText();
        msg += L"  ";
        msg += fail_detail_;
        DrawTextW(dc, msg.c_str(), -1, &left,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                      DT_NOPREFIX);
    } else {
        DrawTextW(dc, StatusLeftText(), -1, &left,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                      DT_NOPREFIX);
    }
    SetTextColor(dc, kSecondary);
    RECT right{rc.right - Scale(150), rc.top, rc.right - Scale(14), rc.bottom};
    DrawTextW(dc, L"Android \x00B7 Windows", -1, &right,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old_font);
    DeleteObject(font);
}

void GameWindow::PaintViewportBackdrop(HDC dc, const RECT& rc) {
    // Letterbox bars around the aspect-fit surface.
    HBRUSH brush = CreateSolidBrush(kLetterbox);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);

    if (session_state_ == GameSessionState::kStarting ||
        (session_state_ == GameSessionState::kWaiting)) {
        // Host startup / waiting panel — honest, no invented percentage.
        HFONT font = CreateUiFont(dpi_, 15, FW_SEMIBOLD);
        HFONT old_font = static_cast<HFONT>(SelectObject(dc, font));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kTitleText);
        RECT center = geometry_.surface;
        DrawTextW(dc, StatusLeftText(), -1, &center,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old_font);
        DeleteObject(font);
    }
}

void GameWindow::PaintShell() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(shell_, &ps);
    RECT client{};
    GetClientRect(shell_, &client);
    // Double-buffer to avoid flicker.
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HBITMAP old_bmp = static_cast<HBITMAP>(SelectObject(mem, bmp));

    // Frame background.
    HBRUSH frame = CreateSolidBrush(kFrame);
    FillRect(mem, &client, frame);
    DeleteObject(frame);

    RECT title = client;
    title.bottom = client.top + Scale(40);
    PaintTitleBar(mem, title);

    RECT status = client;
    status.top = client.bottom - Scale(28);
    PaintStatusStrip(mem, status);

    PaintViewportBackdrop(mem, geometry_.viewport);

    BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(shell_, &ps);
}

// ---------------------------------------------------------------------------
// Toolbar + popovers
// ---------------------------------------------------------------------------

void GameWindow::SetToolbarVisible(bool visible) {
    if (toolbar_ == nullptr) return;
    if (visible) {
        toolbar_visible_ = true;
        ShowWindow(toolbar_, SW_SHOWNA);
        SetTimer(shell_, kTimerToolbarHide, kToolbarHideDelayMs, nullptr);
    } else if (!toolbar_pinned_) {
        toolbar_visible_ = false;
        ShowWindow(toolbar_, SW_HIDE);
    }
}

void GameWindow::UpdateToolbarButtons() {
    if (toolbar_ != nullptr) InvalidateRect(toolbar_, nullptr, TRUE);
}

void GameWindow::OnToolbarButton(int index) {
    // Enabled-button list mirrors LayoutToolbar/PaintToolbar ordering:
    // [fullscreen] [audio?] [input-help].
    int mapped = index;  // physical slot
    int order = 0;
    int slot_of[3] = {-1, -1, -1};
    slot_of[0] = order++;                       // fullscreen always
    if (caps_.audio) slot_of[1] = order++;      // audio if capable
    slot_of[2] = order++;                       // input-help always
    // Translate the visual index back to the physical slot.
    mapped = 0;
    for (int s = 0; s < 3; ++s)
        if (slot_of[s] == index) { mapped = s; break; }

    if (mapped == 0) {
        ToggleFullscreen();
    } else if (mapped == 1) {
        OpenPopover(1);   // volume/mute
    } else {
        OpenPopover(2);   // input help
    }
}

void GameWindow::OpenPopover(int index) {
    ClosePopover();
    if (shell_ == nullptr) return;
    const int w = Scale(240);
    const int h = index == 1 ? Scale(120) : Scale(150);
    RECT tr{};
    if (toolbar_ != nullptr) GetWindowRect(toolbar_, &tr);
    POINT tl{tr.left, tr.top};
    ScreenToClient(shell_, &tl);
    int x = tl.x - w - Scale(8);
    int y = tl.y;
    RECT client{};
    GetClientRect(shell_, &client);
    if (x < Scale(8)) x = Scale(8);
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    open_popover_index_ = index;
    popover_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kPopoverClassName, L"",
        WS_CHILD | WS_BORDER, x, y, w, h, shell_, nullptr, instance, this);
    if (popover_ != nullptr) {
        SetWindowLongPtrW(popover_, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(this));
        // Float above the game surface (which sits at HWND_BOTTOM): a new
        // child is not guaranteed to land on top, so raise it explicitly.
        SetWindowPos(popover_, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(popover_, SW_SHOWNA);
        InvalidateRect(popover_, nullptr, TRUE);
    }
}

void GameWindow::ClosePopover() {
    if (popover_ != nullptr) { DestroyWindow(popover_); popover_ = nullptr; }
    open_popover_index_ = -1;
}

void GameWindow::ShowPopoverHelpText(const std::wstring& text) {
    // Repaint-driven; stored detail is rendered by PaintPopover.
    fail_detail_ = text;  // reuse as transient help text store
    if (popover_ != nullptr) InvalidateRect(popover_, nullptr, TRUE);
}

void GameWindow::PaintPopover(HWND pop) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(pop, &ps);
    RECT rc{};
    GetClientRect(pop, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(0x1B, 0x24, 0x1F));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    // Subtle outline.
    HBRUSH edge = CreateSolidBrush(RGB(0x3A, 0x46, 0x3F));
    FrameRect(dc, &rc, edge);
    DeleteObject(edge);

    HFONT title = CreateUiFont(dpi_, 12, FW_SEMIBOLD);
    HFONT body = CreateUiFont(dpi_, 11, FW_NORMAL);
    HFONT old = static_cast<HFONT>(SelectObject(dc, title));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kTitleText);
    RECT r{rc.left + Scale(12), rc.top + Scale(10), rc.right - Scale(12),
           rc.bottom - Scale(10)};
    if (open_popover_index_ == 1) {
        // Volume popover (only when a real audio capability exists).
        DrawTextW(dc, L"Volume", -1, &r,
                  DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(dc, body);
        SetTextColor(dc, kStatusText);
        RECT row = r;
        row.top += Scale(26);
        wchar_t label[64];
        std::swprintf(label, 64, L"%s   %d%%",
                      pref_muted_ ? L"Muted" : L"Level", pref_volume_);
        DrawTextW(dc, label, -1, &row, DT_LEFT | DT_TOP | DT_SINGLELINE);
        // Slider track.
        row.top += Scale(22);
        RECT track{row.left, row.top + Scale(4), row.right,
                   row.top + Scale(8)};
        HBRUSH tk = CreateSolidBrush(RGB(0x2C, 0x37, 0x31));
        FillRect(dc, &track, tk);
        DeleteObject(tk);
        const int fillw =
            MulDivDpi(static_cast<int>(track.right - track.left),
                      static_cast<UINT>(pref_volume_)) / 100;
        RECT fill{track.left, track.top, track.left + fillw, track.bottom};
        HBRUSH fb = CreateSolidBrush(pref_muted_ ? kSecondary : kAccent);
        FillRect(dc, &fill, fb);
        DeleteObject(fb);
        RECT hint{row.left, track.bottom + Scale(10), row.right,
                  rc.bottom - Scale(8)};
        SetTextColor(dc, kSecondary);
        DrawTextW(dc, L"Click track to set \x00B7 M to mute", -1, &hint,
                  DT_LEFT | DT_TOP | DT_SINGLELINE);
    } else {
        // Input help — honest description of the live input model, not a
        // remapping surface claim.
        DrawTextW(dc, L"Controls", -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(dc, body);
        SetTextColor(dc, kStatusText);
        RECT body_rc = r;
        body_rc.top += Scale(24);
        DrawTextW(dc,
                  L"Pointer and keyboard input are sent to the game.\n"
                  L"Alt+Enter or F11 toggles fullscreen.\n"
                  L"Alt reveals the toolbar.\n"
                  L"Esc returns focus to the game.",
                  -1, &body_rc, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    SelectObject(dc, old);
    DeleteObject(title);
    DeleteObject(body);
    EndPaint(pop, &ps);
}

void GameWindow::PaintToolbar() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(toolbar_, &ps);
    RECT rc{};
    GetClientRect(toolbar_, &rc);
    // Opaque pill + subtle 12% white outline.
    HBRUSH bg = CreateSolidBrush(kToolbar);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    HBRUSH edge = CreateSolidBrush(RGB(0x33, 0x3F, 0x39));
    FrameRect(dc, &rc, edge);
    DeleteObject(edge);

    const int button = Scale(36);
    const int gap = Scale(4);
    const int pad = Scale(6);
    int y = rc.top + pad;
    int slot = 0;
    for (int s = 0; s < kToolbarButtonCount; ++s) {
        if (s == 1 && !caps_.audio) continue;  // audio only when capable
        RECT br{rc.left + pad, y, rc.right - pad, y + button};
        if (hot_toolbar_ == slot ||
            (focus_visible_ && focus_index_ == slot)) {
            HBRUSH hb = CreateSolidBrush(RGB(0x2C, 0x37, 0x31));
            FillRect(dc, &br, hb);
            DeleteObject(hb);
        }
        // Simple vector glyphs in the toolbar glyph colour.
        HPEN pen = CreatePen(PS_SOLID, MulDivDpi(1, dpi_), kToolbarGlyph);
        HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
        HBRUSH old_brush =
            static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        const int cx = (br.left + br.right) / 2;
        const int cy = (br.top + br.bottom) / 2;
        const int g = Scale(6);
        if (s == 0) {
            // Fullscreen: four corner arrows.
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2) {
                    MoveToEx(dc, cx, cy, nullptr);
                    LineTo(dc, cx + sx * g, cy + sy * g);
                }
            Rectangle(dc, cx - g - 2, cy - g - 2, cx + g + 2, cy + g + 2);
        } else if (s == 1) {
            // Speaker + volume waves.
            RECT sp{cx - g - 2, cy - g / 2 - 2, cx - g / 2, cy + g / 2 + 2};
            MoveToEx(dc, cx - g - 2, cy - 2, nullptr);
            LineTo(dc, cx - g / 2, cy - g / 2 - 2);
            LineTo(dc, cx - g / 2, cy + g / 2 + 2);
            LineTo(dc, cx - g - 2, cy + 2);
            LineTo(dc, cx - g - 2, cy - 2);
            Arc(dc, cx - g / 2, cy - g, cx + g + 4, cy + g, cx + g / 2 + 2,
                cy - g / 2, cx + g / 2 + 2, cy + g / 2);
            (void)sp;
        } else {
            // Keyboard / input: rounded rect with key dots.
            RoundRect(dc, cx - g - 3, cy - g / 2 - 3, cx + g + 3,
                      cy + g / 2 + 3, 3, 3);
            for (int k = -1; k <= 1; ++k)
                SetPixel(dc, cx + k * (g / 2 + 1), cy, kToolbarGlyph);
        }
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
        y += button + gap;
        ++slot;
    }
    EndPaint(toolbar_, &ps);
}

// ---------------------------------------------------------------------------
// Fullscreen / placement / prefs
// ---------------------------------------------------------------------------

void GameWindow::EnterFullscreen() {
    if (fullscreen_ || shell_ == nullptr) return;
    has_saved_placement_ = GetWindowPlacement(shell_, &saved_placement_) != 0;
    HMONITOR mon = MonitorFromWindow(shell_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    SetWindowLongPtrW(shell_, GWL_STYLE,
                      WS_POPUP | WS_SYSMENU | WS_CLIPCHILDREN);
    SetWindowPos(shell_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    fullscreen_ = true;
    geometry_.fullscreen = true;
    RecalcLayout();
}

void GameWindow::ExitFullscreen() {
    if (!fullscreen_ || shell_ == nullptr) return;
    SetWindowLongPtrW(shell_, GWL_STYLE,
                      WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX |
                          WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN);
    SetWindowPos(shell_, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_SHOWWINDOW);
    if (has_saved_placement_)
        SetWindowPlacement(shell_, &saved_placement_);
    fullscreen_ = false;
    geometry_.fullscreen = false;
    RecalcLayout();
}

void GameWindow::ToggleFullscreen() {
    if (fullscreen_) ExitFullscreen(); else EnterFullscreen();
}

void GameWindow::SavePlacement() {
    if (shell_ != nullptr && !fullscreen_)
        GetWindowPlacement(shell_, &saved_placement_);
}

bool GameWindow::RestorePlacement() {
    // Apply a persisted placement if one was loaded; else show centered.
    if (!has_saved_placement_) return false;
    saved_placement_.length = sizeof(saved_placement_);
    return SetWindowPlacement(shell_, &saved_placement_) != 0;
}

// Preferences: a minimal versioned ui.json under %LOCALAPPDATA%\Armr.
// Host-UI settings only — never touches game saves.
void GameWindow::LoadPrefs() {
    wchar_t dir[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring path = std::wstring(dir) + L"\\Armr\\ui.json";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) return;
    char buf[1024]{};
    const size_t rd = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[rd] = '\0';
    // Tolerant parse: look for "key":value pairs we understand.
    const std::string s(buf, rd);
    const std::string mp = "\"muted\":";
    if (const size_t p = s.find(mp); p != std::string::npos) {
        const size_t v = p + mp.size();
        pref_muted_ = s.compare(v, 4, "true") == 0 ||
                      s.compare(v, 1, "1") == 0;
    }
    const std::string vp = "\"volume\":";
    if (const size_t p = s.find(vp); p != std::string::npos) {
        const int volume = std::atoi(s.c_str() + p + vp.size());
        if (volume >= 0 && volume <= 100) pref_volume_ = volume;
    }
    // Persisted window rect: "window":{"x":N,"y":N,"w":N,"h":N}. Rebuild a
    // normal-position placement so the shell reopens where it was left.
    const std::string wp = "\"window\":";
    if (const size_t p = s.find(wp); p != std::string::npos) {
        const size_t wb = s.find('{', p + wp.size());
        const size_t we = s.find('}', wb == std::string::npos ? p : wb);
        if (wb != std::string::npos && we != std::string::npos) {
            const std::string wobj = s.substr(wb, we - wb);
            const auto field = [&wobj](const char* key, long* out) {
                const std::string k = std::string("\"") + key + "\":";
                const size_t q = wobj.find(k);
                if (q == std::string::npos) return false;
                *out = std::atol(wobj.c_str() + q + k.size());
                return true;
            };
            long x = 0, y = 0, w = 0, h = 0;
            if (field("x", &x) && field("y", &y) && field("w", &w) &&
                field("h", &h) && w >= 200 && h >= 200) {
                saved_placement_.length = sizeof(saved_placement_);
                saved_placement_.showCmd = SW_SHOWNORMAL;
                saved_placement_.rcNormalPosition =
                    RECT{static_cast<LONG>(x), static_cast<LONG>(y),
                         static_cast<LONG>(x + w), static_cast<LONG>(y + h)};
                has_saved_placement_ = true;
            }
        }
    }
}

void GameWindow::SavePrefs() {
    wchar_t dir[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring folder = std::wstring(dir) + L"\\Armr";
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring path = folder + L"\\ui.json";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || f == nullptr) return;
    // Persist the restored (non-fullscreen) window rect so the shell reopens
    // where the user left it. saved_placement_ holds rcNormalPosition once
    // SavePlacement ran.
    std::fprintf(
        f,
        "{\n  \"version\":1,\n  \"muted\":%s,\n  \"volume\":%d,\n"
        "  \"window\":{\"x\":%ld,\"y\":%ld,\"w\":%ld,\"h\":%ld}\n}\n",
        pref_muted_ ? "true" : "false", pref_volume_,
        static_cast<long>(saved_placement_.rcNormalPosition.left),
        static_cast<long>(saved_placement_.rcNormalPosition.top),
        static_cast<long>(saved_placement_.rcNormalPosition.right -
                          saved_placement_.rcNormalPosition.left),
        static_cast<long>(saved_placement_.rcNormalPosition.bottom -
                          saved_placement_.rcNormalPosition.top));
    fclose(f);
}

// ---------------------------------------------------------------------------
// Input / focus
// ---------------------------------------------------------------------------

void GameWindow::UpdateCaptionButtons(POINT pt) {
    if (shell_ == nullptr) return;
    RECT rc{};
    GetClientRect(shell_, &rc);
    const int bw = Scale(46);
    const int bx0 = rc.right - bw * 3;
    int hot = -1;
    if (pt.y >= rc.top && pt.y < rc.top + Scale(40)) {
        if (pt.x >= bx0 && pt.x < bx0 + bw) hot = 0;
        else if (pt.x >= bx0 + bw && pt.x < bx0 + bw * 2) hot = 1;
        else if (pt.x >= bx0 + bw * 2 && pt.x < bx0 + bw * 3) hot = 2;
    }
    if (hot != hot_caption_) {
        hot_caption_ = hot;
        RECT title = rc;
        title.bottom = rc.top + Scale(40);
        InvalidateRect(shell_, &title, TRUE);
    }
}

void GameWindow::SetFocusTo(int focus_index) {
    focus_index_ = focus_index;
    focus_visible_ = focus_index >= 0;
    InvalidateRect(shell_, nullptr, TRUE);
    if (toolbar_ != nullptr) InvalidateRect(toolbar_, nullptr, TRUE);
}

void GameWindow::CycleFocus(int dir) {
    // Focus order: toolbar buttons (0..n-1) then caption buttons.
    const int toolbar_slots = 1 + (caps_.audio ? 1 : 0) + 1;
    const int max = toolbar_slots + 3;  // +3 caption buttons
    int next = focus_index_ < 0 ? 0 : (focus_index_ + dir + max) % max;
    SetFocusTo(next);
    if (next < toolbar_slots) SetToolbarVisible(true);
}

void GameWindow::RequestClose() {
    shell_close_requested_ = true;
    SetSessionState(GameSessionState::kClosing);
    // Forward to the guest surface's cooperative close path. The surface
    // child (NativeGles window) turns this into close_requested for the
    // runtime — nothing is torn down synchronously here.
    if (surface_child_ != nullptr)
        SendMessageW(surface_child_, WM_CLOSE, 0, 0);
    else if (shell_ != nullptr)
        DestroyWindow(shell_);
}

// ---------------------------------------------------------------------------
// Shell window procedure
// ---------------------------------------------------------------------------

LRESULT GameWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wp,
                                  LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
            // Frameless: client covers the whole window (keeps WS_THICKFRAME
            // resize + DWM shadow/rounded corners).
            if (wp != 0) return 0;
            break;
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            if (!fullscreen_) {
                const int border = Scale(6);
                const bool left = pt.x < border;
                const bool right = pt.x >= rc.right - border;
                const bool top = pt.y < border;
                const bool bottom = pt.y >= rc.bottom - border;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }
            // Title bar -> caption drag (except over caption buttons).
            if (pt.y < rc.top + Scale(40)) {
                const int bw = Scale(46);
                if (pt.x >= rc.right - bw * 3) return HTCLIENT;
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_PAINT:
            PaintShell();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            RecalcLayout();
            return 0;
        case WM_MOVE:
            PositionSurface();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = Scale(320);
            mmi->ptMinTrackSize.y = Scale(240);
            if (fullscreen_) {
                HMONITOR mon =
                    MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{sizeof(mi)};
                GetMonitorInfoW(mon, &mi);
                mmi->ptMaxSize.x = mi.rcMonitor.right - mi.rcMonitor.left;
                mmi->ptMaxSize.y = mi.rcMonitor.bottom - mi.rcMonitor.top;
                mmi->ptMaxPosition.x = mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcMonitor.top;
            }
            return 0;
        }
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            const RECT* suggested = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RecalcLayout();
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            UpdateCaptionButtons(pt);
            // Reveal the floating toolbar near the right edge.
            RECT rc{};
            GetClientRect(hwnd, &rc);
            if (pt.x >= rc.right - Scale(90) &&
                pt.y >= geometry_.viewport.top &&
                pt.y < geometry_.viewport.bottom) {
                SetToolbarVisible(true);
                SetTimer(hwnd, kTimerToolbarHide, kToolbarHideDelayMs,
                         nullptr);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            UpdateCaptionButtons(pt);
            if (hot_caption_ >= 0) {
                pressed_caption_ = hot_caption_;
                SetCapture(hwnd);
                return 0;
            }
            ClosePopover();
            return 0;
        }
        case WM_LBUTTONUP: {
            if (pressed_caption_ >= 0) {
                const int which = pressed_caption_;
                pressed_caption_ = -1;
                ReleaseCapture();
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                UpdateCaptionButtons(pt);
                if (hot_caption_ == which) {
                    if (which == 0)
                        ShowWindow(hwnd, SW_MINIMIZE);
                    else if (which == 1)
                        ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE
                                                       : SW_MAXIMIZE);
                    else
                        RequestClose();
                }
                return 0;
            }
            return 0;
        }
        case WM_TIMER:
            if (wp == kTimerToolbarHide) {
                KillTimer(hwnd, kTimerToolbarHide);
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                RECT rc{};
                GetClientRect(hwnd, &rc);
                RECT tr{};
                if (toolbar_ != nullptr) {
                    GetWindowRect(toolbar_, &tr);
                    POINT t{tr.left, tr.top};
                    ScreenToClient(hwnd, &t);
                    RECT toolbar_client{t.x, t.y,
                                        t.x + (tr.right - tr.left),
                                        t.y + (tr.bottom - tr.top)};
                    if (!PtInRect(&toolbar_client, pt))
                        SetToolbarVisible(false);
                }
                return 0;
            }
            if (wp == kTimerCloseDeadline) {
                KillTimer(hwnd, kTimerCloseDeadline);
                if (session_state_ == GameSessionState::kClosing)
                    SetSessionStateInternal(GameSessionState::kCloseDelayed);
                return 0;
            }
            if (wp == kTimerWaitHint) {
                KillTimer(hwnd, kTimerWaitHint);
                if (session_state_ == GameSessionState::kRunning)
                    SetSessionStateInternal(GameSessionState::kWaiting);
                return 0;
            }
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const DWORD vk = static_cast<DWORD>(wp);
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            // Fullscreen shortcuts.
            if (vk == VK_F11 || (vk == VK_RETURN && alt)) {
                ToggleFullscreen();
                return 0;
            }
            if (vk == VK_MENU) {  // bare Alt reveals the toolbar
                SetToolbarVisible(true);
                return 0;
            }
            if (vk == VK_TAB) {
                CycleFocus((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
                return 0;
            }
            if (vk == VK_ESCAPE) {
                if (popover_ != nullptr) { ClosePopover(); return 0; }
                if (focus_visible_) { SetFocusTo(-1); return 0; }
                if (fullscreen_) { ExitFullscreen(); return 0; }
                return 0;  // else let it fall to the game
            }
            if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP ||
                vk == VK_DOWN) {
                if (focus_index_ >= 0 && popover_ != nullptr &&
                    open_popover_index_ == 1) {
                    pref_volume_ = std::clamp(
                        pref_volume_ +
                            (vk == VK_RIGHT || vk == VK_UP ? 5 : -5),
                        0, 100);
                    InvalidateRect(popover_, nullptr, TRUE);
                    return 0;
                }
            }
            if (vk == L'M' && popover_ != nullptr &&
                open_popover_index_ == 1) {
                pref_muted_ = !pref_muted_;
                InvalidateRect(popover_, nullptr, TRUE);
                return 0;
            }
            break;
        }
        case WM_SYSCHAR:
        case WM_CHAR:
            // Swallow the syschar beep for our handled accelerators.
            if (msg == WM_SYSCHAR) return 0;
            break;
        case WM_SETFOCUS:
            focus_visible_ = true;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_KILLFOCUS:
            focus_visible_ = false;
            SetToolbarVisible(false);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case kArmrMsgFirstFrame:
            NotifyFirstFrame();
            return 0;
        case kArmrMsgClosing:
            // The runtime requested close; reflect the cooperative close in
            // the status strip and arm the close-deadline timer.
            SetSessionState(GameSessionState::kClosing);
            return 0;
        case kArmrMsgEdgeHover:
            // The display child (which fills the viewport) forwarded a hover
            // near its right edge; reveal the floating toolbar over the game.
            SetToolbarVisible(true);
            SetTimer(hwnd, kTimerToolbarHide, kToolbarHideDelayMs, nullptr);
            return 0;
        case WM_CLOSE:
            // Cooperative close: user clicked X / Alt+F4 / sysmenu. Route to
            // the same path as the close button — never DestroyWindow here.
            RequestClose();
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_CLOSE) { RequestClose(); return 0; }
            break;
        case WM_NCDESTROY:
            shell_ = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT GameWindow::HandleToolbarMessage(HWND hwnd, UINT msg, WPARAM wp,
                                         LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            PaintToolbar();
            return 0;
        case WM_MOUSEMOVE: {
            // Track which button is hot; keep the pill pinned while hovered.
            const int button = Scale(36);
            const int gap = Scale(4);
            const int pad = Scale(6);
            int y = pad;
            int slot = 0;
            int hot = -1;
            const int my = GET_Y_LPARAM(lp);
            for (int s = 0; s < kToolbarButtonCount; ++s) {
                if (s == 1 && !caps_.audio) continue;
                if (my >= y && my < y + button) hot = slot;
                y += button + gap;
                ++slot;
            }
            if (hot != hot_toolbar_) {
                hot_toolbar_ = hot;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            // Keep visible while the cursor is over the pill.
            SetTimer(shell_, kTimerToolbarHide, kToolbarHideDelayMs, nullptr);
            // Track leave to re-arm the hide timer.
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const int my = GET_Y_LPARAM(lp);
            const int button = Scale(36);
            const int gap = Scale(4);
            const int pad = Scale(6);
            int y = pad;
            int slot = 0;
            for (int s = 0; s < kToolbarButtonCount; ++s) {
                if (s == 1 && !caps_.audio) continue;
                if (my >= y && my < y + button) {
                    OnToolbarButton(slot);
                    break;
                }
                y += button + gap;
                ++slot;
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            hot_toolbar_ = -1;
            InvalidateRect(hwnd, nullptr, TRUE);
            SetTimer(shell_, kTimerToolbarHide, kToolbarHideDelayMs, nullptr);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT GameWindow::HandlePopoverMessage(HWND hwnd, UINT msg, WPARAM wp,
                                         LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            PaintPopover(hwnd);
            return 0;
        case WM_LBUTTONDOWN: {
            if (open_popover_index_ == 1) {
                // Volume slider click: map the click x to 0..100 across the
                // painted track.
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const int x = GET_X_LPARAM(lp);
                const int y = GET_Y_LPARAM(lp);
                RECT track{Scale(12), Scale(58), rc.right - Scale(12),
                           Scale(62)};
                if (y >= track.top - Scale(6) && y <= track.bottom + Scale(6)) {
                    const int tw =
                        static_cast<int>(track.right - track.left);
                    pref_volume_ = std::clamp(
                        tw > 0
                            ? static_cast<int>(x - track.left) * 100 / tw
                            : 0,
                        0, 100);
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        }
        case WM_KILLFOCUS:
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Static procedures: retrieve the instance and dispatch.
LRESULT CALLBACK GameWindow::ShellProc(HWND hwnd, UINT msg, WPARAM wp,
                                       LPARAM lp) {
    GameWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<GameWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) self->shell_ = hwnd;
    } else {
        self = reinterpret_cast<GameWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr) return self->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK GameWindow::ToolbarProc(HWND hwnd, UINT msg, WPARAM wp,
                                         LPARAM lp) {
    auto* self = reinterpret_cast<GameWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) return self->HandleToolbarMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK GameWindow::PopoverProc(HWND hwnd, UINT msg, WPARAM wp,
                                       LPARAM lp) {
    auto* self = reinterpret_cast<GameWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) return self->HandlePopoverMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace isomr
