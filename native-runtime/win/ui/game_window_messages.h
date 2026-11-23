// Shared message contract between the NativeGles guest surface (child HWND)
// and the ARMR game-window shell (top-level host). The surface only ever
// posts these to the shell; the shell never calls back into the runtime.
// Keeping them in a dedicated header avoids a heavier include dependency in
// either direction.
#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Posted by the guest surface to the shell when the first genuine lit frame
// presents (advances the session state Starting -> Running).
constexpr UINT kArmrMsgFirstFrame = WM_APP + 0x701;

// Posted by the guest surface to the shell when the runtime requests close,
// so the shell can reflect "Closing…" while the cooperative teardown runs.
constexpr UINT kArmrMsgClosing = WM_APP + 0x702;

// Posted by the guest surface to the shell when the pointer hovers near the
// surface's right edge. The display child covers the shell's viewport, so its
// mouse moves never reach the shell's own edge-reveal hit-test; the child
// forwards the intent so the floating toolbar can still reveal over the game.
// wparam: client x, lparam: client y (display-child client coordinates).
constexpr UINT kArmrMsgEdgeHover = WM_APP + 0x703;
