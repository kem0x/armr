// Standalone host-side exercise for the chat-head overlay substrate. Not an
// Android run — it creates the real Win32 layered bubble/panel and, with
// --selftest, drives a genuine drag + tap through SendInput so the drag, snap,
// expand, and dismiss paths are verified end-to-end without a human.
//
//   ArmrChatHeadSmoke                      interactive demo bubble
//   ArmrChatHeadSmoke --x 200 --y 300       place the bubble
//   ArmrChatHeadSmoke --seconds 10          auto-quit after N seconds
//   ArmrChatHeadSmoke --selftest            scripted drag+tap, exit 0/1

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>

#include "../ui/chat_head_overlay.h"

namespace {

struct Args {
    int x = -1, y = -1;
    int seconds = 0;        // auto-quit after N seconds (0 = run until closed)
    bool selftest = false;
    bool expand = false;    // start with the chat panel expanded
    int badge = 3;
    std::wstring glyph = L"A";
    std::wstring title = L"Chat head";
};

void Stage(const char* stage, const char* kv) {
    std::printf("ISOMR_CHATHEAD stage=%s %s\n", stage, kv ? kv : "");
    std::fflush(stdout);
}

Args Parse(int argc, wchar_t** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::wstring s = argv[i];
        auto next = [&](int* out) {
            if (i + 1 < argc) *out = _wtoi(argv[++i]);
        };
        if (s == L"--x") next(&a.x);
        else if (s == L"--y") next(&a.y);
        else if (s == L"--seconds") next(&a.seconds);
        else if (s == L"--badge") next(&a.badge);
        else if (s == L"--selftest") a.selftest = true;
        else if (s == L"--expand") a.expand = true;
        else if (s == L"--glyph" && i + 1 < argc) a.glyph = argv[++i];
        else if (s == L"--title" && i + 1 < argc) a.title = argv[++i];
    }
    return a;
}

// Drive the real cursor to a screen pixel and press/release the left button.
// SetCursorPos + mouse_event take raw screen coordinates — far more reliable
// than SendInput's 0-65535 normalization for injecting input at a known point.
void MoveMouse(int sx, int sy) { ::SetCursorPos(sx, sy); }
void LeftDown() { ::mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0); }
void LeftUp() { ::mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0); }

// Bubble-centre screen coords for a window.
void Centre(HWND hwnd, int* sx, int* sy) {
    RECT b;
    ::GetWindowRect(hwnd, &b);
    *sx = (b.left + b.right) / 2;
    *sy = (b.top + b.bottom) / 2;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Args args = Parse(argc, argv);
    HINSTANCE inst = ::GetModuleHandleW(nullptr);

    isomr::ChatHeadOverlay overlay;
    if (!overlay.Create(inst)) {
        Stage("create", "result=fail");
        return 1;
    }
    Stage("create", "result=pass");

    overlay.SetGlyph(args.glyph);
    overlay.SetTitle(args.title);
    overlay.SetBadgeCount(args.badge);

    bool expand_seen = false, moved_seen = false;
    isomr::ChatHeadCallbacks cb;
    cb.on_tap = [&] {
        Stage("event", "tap");
        overlay.SetExpanded(true);
    };
    cb.on_expand_changed = [](bool e) {
        Stage("event", e ? "expanded" : "collapsed");
    };
    cb.on_moved = [&](int x, int y) {
        moved_seen = true;  // the drag genuinely relocated the bubble
        std::printf("ISOMR_CHATHEAD event=moved x=%d y=%d\n", x, y);
        std::fflush(stdout);
    };
    overlay.set_callbacks(cb);
    // Honest placeholder content — this is a substrate demo, not a real chat.
    overlay.AddMessage(false, L"Chat-head substrate demo");
    overlay.AddMessage(false, L"addView \x2192 Win32 overlay");
    overlay.AddMessage(true, L"tap the bubble to expand");

    // Where to place the bubble: explicit --x/--y, else right-edge-ish.
    int sx = args.x, sy = args.y;
    if (sx < 0 || sy < 0) {
        RECT work;
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        sx = work.right - 90;
        sy = work.top + (work.bottom - work.top) / 3;
    }
    overlay.Show(sx, sy);
    Stage("show", "result=pass");
    if (args.expand)
        overlay.SetExpanded(true);

    // Self-test: drive a real drag, release (snap), then a tap (expand).
    // Runs the window messages on a short timer so input lands between pumps.
    UINT_PTR quit_timer = 0, test_timer = 0;
    HWND host = overlay.bubble_hwnd();
    int test_step = 0;
    int dfx = 0, dfy = 0, dtx = 0, dty = 0;  // drag endpoints, screen coords

    if (args.seconds > 0)
        quit_timer = ::SetTimer(host, 100, args.seconds * 1000, nullptr);
    if (args.selftest)
        test_timer = ::SetTimer(host, 101, 120, nullptr);

    // Seed drag endpoints from the bubble's actual rect.
    {
        Centre(host, &dfx, &dfy);
        dtx = dfx + 220;  // well past the tap threshold
        dty = dfy + 60;
    }

    MSG msg;
    bool done = false;
    int exit_code = 0;
    while (!done && ::GetMessageW(&msg, nullptr, 0, 0)) {
        // Intercept only our own driver timers (100/101); the bubble's snap
        // timer and the panel's caret timer must reach their wndprocs.
        if (msg.message == WM_TIMER && msg.wParam == 100) {
            Stage("quit", "reason=seconds");
            break;
        }
        if (msg.message == WM_TIMER && msg.wParam == 101 && args.selftest) {
            auto rect_str = [&](char* buf, size_t n) {
                RECT b;
                ::GetWindowRect(host, &b);
                _snprintf_s(buf, n, _TRUNCATE, "pos=%ld,%ld,%ldx%ld", b.left,
                            b.top, b.right - b.left, b.bottom - b.top);
            };
            char posbuf[80];
            switch (test_step) {
            case 0:  // press on the bubble center
                MoveMouse(dfx, dfy);
                LeftDown();
                Stage("selftest", "step=press");
                break;
            case 1:  // drag well past the tap threshold
                MoveMouse(dtx, dty);
                Stage("selftest", "step=drag");
                break;
            case 2:  // release -> edge snap begins (~200ms)
                LeftUp();
                Stage("selftest", "step=release");
                break;
            case 3:
            case 4:
            case 5:
            case 6:  // let the snap animation fully settle before tapping
                break;
            case 7: {
                rect_str(posbuf, sizeof(posbuf));
                Stage("selftest-pos", posbuf);
                int ax, ay;
                Centre(host, &ax, &ay);
                MoveMouse(ax, ay);
                LeftDown();
                LeftUp();
                Stage("selftest", "step=tap");
                break;
            }
            case 8:  // let the tap land
                break;
            case 9:
                expand_seen = overlay.expanded();
                rect_str(posbuf, sizeof(posbuf));
                Stage("selftest-pos2", posbuf);
                ::KillTimer(host, test_timer);
                Stage("selftest-result",
                      (expand_seen && moved_seen) ? "result=pass"
                                                  : "result=fail");
                Stage("quit", "reason=selftest");
                done = true;
                break;
            }
            ++test_step;
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
        if (!overlay.running()) {
            Stage("quit", "reason=dismissed");
            break;
        }
    }

    if (args.selftest)
        exit_code = (expand_seen && moved_seen) ? 0 : 1;

    overlay.Destroy();
    return exit_code;
}
