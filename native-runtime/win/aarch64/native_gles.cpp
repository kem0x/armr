#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES2/gl2.h>
// GL_EXT_read_format_bgra; the GLES2 headers may not declare it but ANGLE's
// D3D11 backend stores the backbuffer natively as BGRA so this is the
// zero-conversion readback format for the GDI present path.
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
// GLES3 vertex-array objects are exported by the ANGLE libGLESv2 we
// link, but the GLES2 headers do not declare them; cocos2d loads these
// entry points through eglGetProcAddress at runtime.
extern "C" {
void APIENTRY glGenVertexArrays(GLsizei count, GLuint* arrays);
void APIENTRY glBindVertexArray(GLuint array);
void APIENTRY glDeleteVertexArrays(GLsizei count, GLuint* arrays);
// GLES3 indexed-string query exported by the ANGLE libGLESv2 we link; the
// GLES2 headers do not declare it. Unity resolves it through dlsym and calls
// it to enumerate extensions when it detects an ES3-capable function surface.
const GLubyte* APIENTRY glGetStringi(GLenum name, GLuint index);
// GLES3 immutable texture storage, exported by the ANGLE libGLESv2 we link
// (GL_EXT_texture_storage is advertised). The GLES2 headers do not declare
// these; Unity's GLES3 device allocates render-target storage through them.
void APIENTRY glTexStorage2D(GLenum target, GLsizei levels,
                             GLenum internalformat, GLsizei width,
                             GLsizei height);
void APIENTRY glTexStorage3D(GLenum target, GLsizei levels,
                             GLenum internalformat, GLsizei width,
                             GLsizei height, GLsizei depth);
// GLES3 surface resolved by Unity's GfxDeviceGLES3 through
// dlsym/eglGetProcAddress: buffer mapping/copy, indexed bindings, instanced
// and base-vertex draws, 3D/array textures, sync fences, samplers,
// uniform-block plumbing, per-index state, and unsigned uniforms. The GLES2
// headers do not declare them; ANGLE's libGLESv2 exports the full set.
typedef khronos_int64_t GLint64;
typedef khronos_uint64_t GLuint64;
typedef struct __GLsync* GLsync;
void* APIENTRY glMapBufferRange(GLenum target, GLintptr offset,
                                GLsizeiptr length, GLbitfield access);
GLboolean APIENTRY glUnmapBuffer(GLenum target);
void APIENTRY glFlushMappedBufferRange(GLenum target, GLintptr offset,
                                       GLsizeiptr length);
void APIENTRY glGetBufferPointerv(GLenum target, GLenum pname,
                                  void** params);
void APIENTRY glCopyBufferSubData(GLenum read_target, GLenum write_target,
                                  GLintptr read_offset,
                                  GLintptr write_offset, GLsizeiptr size);
void APIENTRY glBufferStorageEXT(GLenum target, GLsizeiptr size,
                                 const void* data, GLbitfield flags);
void APIENTRY glBindBufferBase(GLenum target, GLuint index, GLuint buffer);
void APIENTRY glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                                GLintptr offset, GLsizeiptr size);
void APIENTRY glGetIntegeri_v(GLenum target, GLuint index, GLint* data);
void APIENTRY glGetInteger64i_v(GLenum target, GLuint index, GLint64* data);
void APIENTRY glGetInteger64v(GLenum pname, GLint64* data);
void APIENTRY glDrawElementsInstanced(GLenum mode, GLsizei count,
                                      GLenum type, const void* indices,
                                      GLsizei instance_count);
void APIENTRY glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                                    GLsizei instance_count);
void APIENTRY glDrawElementsBaseVertex(GLenum mode, GLsizei count,
                                       GLenum type, const void* indices,
                                       GLint base_vertex);
void APIENTRY glDrawRangeElements(GLenum mode, GLuint start, GLuint end,
                                  GLsizei count, GLenum type,
                                  const void* indices);
void APIENTRY glDrawRangeElementsBaseVertex(GLenum mode, GLuint start,
                                            GLuint end, GLsizei count,
                                            GLenum type,
                                            const void* indices,
                                            GLint base_vertex);
void APIENTRY glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count,
                                                GLenum type,
                                                const void* indices,
                                                GLsizei instance_count,
                                                GLint base_vertex);
void APIENTRY glVertexAttribIPointer(GLuint index, GLint size, GLenum type,
                                     GLsizei stride, const void* pointer);
void APIENTRY glVertexAttribDivisor(GLuint index, GLuint divisor);
void APIENTRY glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z,
                                GLint w);
void APIENTRY glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z,
                                 GLuint w);
void APIENTRY glVertexAttribI4iv(GLuint index, const GLint* values);
void APIENTRY glVertexAttribI4uiv(GLuint index, const GLuint* values);
void APIENTRY glBlitFramebuffer(GLint src_x0, GLint src_y0, GLint src_x1,
                                GLint src_y1, GLint dst_x0, GLint dst_y0,
                                GLint dst_x1, GLint dst_y1,
                                GLbitfield mask, GLenum filter);
void APIENTRY glReadBuffer(GLenum source);
void APIENTRY glDrawBuffers(GLsizei count, const GLenum* buffers);
void APIENTRY glInvalidateFramebuffer(GLenum target, GLsizei count,
                                      const GLenum* attachments);
void APIENTRY glInvalidateSubFramebuffer(GLenum target, GLsizei count,
                                         const GLenum* attachments, GLint x,
                                         GLint y, GLsizei width,
                                         GLsizei height);
void APIENTRY glTexImage3D(GLenum target, GLint level, GLint internalformat,
                           GLsizei width, GLsizei height, GLsizei depth,
                           GLint border, GLenum format, GLenum type,
                           const void* pixels);
void APIENTRY glTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                              GLint yoffset, GLint zoffset, GLsizei width,
                              GLsizei height, GLsizei depth, GLenum format,
                              GLenum type, const void* pixels);
void APIENTRY glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                  GLint yoffset, GLint zoffset, GLint x,
                                  GLint y, GLsizei width, GLsizei height);
void APIENTRY glCompressedTexImage3D(GLenum target, GLint level,
                                     GLenum internalformat, GLsizei width,
                                     GLsizei height, GLsizei depth,
                                     GLint border, GLsizei image_size,
                                     const void* data);
void APIENTRY glCompressedTexSubImage3D(GLenum target, GLint level,
                                        GLint xoffset, GLint yoffset,
                                        GLint zoffset, GLsizei width,
                                        GLsizei height, GLsizei depth,
                                        GLenum format, GLsizei image_size,
                                        const void* data);
GLsync APIENTRY glFenceSync(GLenum condition, GLbitfield flags);
GLenum APIENTRY glClientWaitSync(GLsync sync, GLbitfield flags,
                                 GLuint64 timeout);
void APIENTRY glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout);
void APIENTRY glDeleteSync(GLsync sync);
GLboolean APIENTRY glIsSync(GLsync sync);
void APIENTRY glGetSynciv(GLsync sync, GLenum pname, GLsizei count,
                          GLsizei* length, GLint* values);
GLuint APIENTRY glGetUniformBlockIndex(GLuint program,
                                       const GLchar* block_name);
void APIENTRY glUniformBlockBinding(GLuint program, GLuint block_index,
                                    GLuint block_binding);
void APIENTRY glGetActiveUniformBlockiv(GLuint program, GLuint block_index,
                                        GLenum pname, GLint* params);
void APIENTRY glGetActiveUniformBlockName(GLuint program, GLuint block_index,
                                          GLsizei buf_size, GLsizei* length,
                                          GLchar* name);
void APIENTRY glGetUniformIndices(GLuint program, GLsizei uniform_count,
                                  const GLchar* const* uniform_names,
                                  GLuint* uniform_indices);
void APIENTRY glGetActiveUniformsiv(GLuint program, GLsizei uniform_count,
                                    const GLuint* uniform_indices,
                                    GLenum pname, GLint* params);
void APIENTRY glGenSamplers(GLsizei count, GLuint* samplers);
void APIENTRY glDeleteSamplers(GLsizei count, const GLuint* samplers);
void APIENTRY glBindSampler(GLuint unit, GLuint sampler);
GLboolean APIENTRY glIsSampler(GLuint sampler);
void APIENTRY glSamplerParameteri(GLuint sampler, GLenum pname, GLint param);
void APIENTRY glSamplerParameterf(GLuint sampler, GLenum pname,
                                  GLfloat param);
void APIENTRY glSamplerParameteriv(GLuint sampler, GLenum pname,
                                   const GLint* params);
void APIENTRY glSamplerParameterfv(GLuint sampler, GLenum pname,
                                   const GLfloat* params);
void APIENTRY glGetSamplerParameteriv(GLuint sampler, GLenum pname,
                                      GLint* params);
void APIENTRY glGetSamplerParameterfv(GLuint sampler, GLenum pname,
                                      GLfloat* params);
void APIENTRY glClearBufferfv(GLenum buffer, GLint drawbuffer,
                              const GLfloat* value);
void APIENTRY glClearBufferiv(GLenum buffer, GLint drawbuffer,
                              const GLint* value);
void APIENTRY glClearBufferuiv(GLenum buffer, GLint drawbuffer,
                               const GLuint* value);
void APIENTRY glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth,
                              GLint stencil);
void APIENTRY glColorMaski(GLuint index, GLboolean red, GLboolean green,
                           GLboolean blue, GLboolean alpha);
void APIENTRY glEnablei(GLenum target, GLuint index);
void APIENTRY glDisablei(GLenum target, GLuint index);
GLboolean APIENTRY glIsEnabledi(GLenum target, GLuint index);
void APIENTRY glUniform1ui(GLint location, GLuint x);
void APIENTRY glUniform2ui(GLint location, GLuint x, GLuint y);
void APIENTRY glUniform3ui(GLint location, GLuint x, GLuint y, GLuint z);
void APIENTRY glUniform4ui(GLint location, GLuint x, GLuint y, GLuint z,
                           GLuint w);
void APIENTRY glUniform1uiv(GLint location, GLsizei count,
                            const GLuint* value);
void APIENTRY glUniform2uiv(GLint location, GLsizei count,
                            const GLuint* value);
void APIENTRY glUniform3uiv(GLint location, GLsizei count,
                            const GLuint* value);
void APIENTRY glUniform4uiv(GLint location, GLsizei count,
                            const GLuint* value);
GLint APIENTRY glGetFragDataLocation(GLuint program, const GLchar* name);
void APIENTRY glGetUniformuiv(GLuint program, GLint location, GLuint* params);
void APIENTRY glGetVertexAttribIiv(GLuint index, GLenum pname, GLint* params);
void APIENTRY glGetVertexAttribIuiv(GLuint index, GLenum pname,
                                    GLuint* params);
void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level,
                                       GLenum pname, GLint* params);
void APIENTRY glGetTexLevelParameterfv(GLenum target, GLint level,
                                       GLenum pname, GLfloat* params);
void APIENTRY glGetInternalformativ(GLenum target, GLenum internalformat,
                                    GLenum pname, GLsizei count,
                                    GLint* params);
void APIENTRY glProgramParameteri(GLuint program, GLenum pname, GLint value);
void APIENTRY glGetProgramBinary(GLuint program, GLsizei buf_size,
                                 GLsizei* length, GLenum* binary_format,
                                 void* binary);
void APIENTRY glProgramBinary(GLuint program, GLenum binary_format,
                              const void* binary, GLsizei length);
void APIENTRY glTexBuffer(GLenum target, GLenum internalformat,
                          GLuint buffer);
}

#include "native_gles.h"

#include "guest_address_space.h"
#include "guest_heap.h"
#include "import_registry.h"
#include "ui/game_window_messages.h"

#include <chrono>
#include <set>
#include <thread>
#include <cerrno>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace isomr {

struct NativeWindowState {
    std::vector<NativePointerEvent> pointer_events;
    std::vector<NativeKeyEvent> key_events;
    std::string input_error;
    std::int32_t last_x = 0;
    std::int32_t last_y = 0;
    std::int32_t last_client_width = 0;
    std::int32_t last_client_height = 0;
    bool pointer_active = false;
    bool close_requested = false;
    // Hosted (ARMR game window) mode: the fixed guest logical surface size.
    // When set, pointer coordinates captured in the display child's client
    // space are transformed into this logical space before they are queued,
    // so the guest always sees stable logical coordinates regardless of how
    // the host scales the presented image. Zero in standalone mode.
    std::int32_t logical_width = 0;
    std::int32_t logical_height = 0;
    // Optional host-shell HWND to notify on first lit frame and on close so
    // the shell can advance its session-state model. No synchronous coupling:
    // the surface only posts; the shell never calls back into the runtime.
    HWND host_notify = nullptr;
};

namespace {

constexpr wchar_t kWindowClassName[] = L"ArmrNativeGlesWindow";
// Fixed-size render-target host used in hosted (ARMR game window) mode. It is
// never shown: the guest renders into its EGL surface, and Present read-backs
// + GDI-blits the frame onto the aspect-fit display child. Keeping it off the
// visible tree keeps the guest logical size fixed while the host scales.
constexpr wchar_t kSurfaceClassName[] = L"ArmrNativeGlesSurface";
constexpr int kInitialSurfaceWidth = 1280;
constexpr int kInitialSurfaceHeight = 720;
constexpr DWORD kWindowStyle =
    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr std::size_t kMaximumPointerEvents = 256;

std::uint64_t SteadyClockNanos() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void RecordWindowInputError(NativeWindowState* state, std::string message) {
    if (state != nullptr && state->input_error.empty())
        state->input_error = std::move(message);
}

bool AppendPointerEvent(NativeWindowState* state, NativePointerAction action,
                        std::int32_t x, std::int32_t y,
                        std::int32_t client_width,
                        std::int32_t client_height) {
    if (state == nullptr || client_width <= 0 || client_height <= 0) {
        RecordWindowInputError(
            state, "native GLES pointer event has invalid client dimensions");
        return false;
    }
    const NativePointerEvent event{action, x, y, client_width,
                                   client_height, SteadyClockNanos()};
    if (action == NativePointerAction::kMove &&
        !state->pointer_events.empty() &&
        state->pointer_events.back().action == NativePointerAction::kMove) {
        state->pointer_events.back() = event;
        return true;
    }
    if (state->pointer_events.size() >= kMaximumPointerEvents) {
        RecordWindowInputError(state,
                               "native GLES pointer event queue overflowed");
        return false;
    }
    state->pointer_events.push_back(event);
    return true;
}

bool AppendKeyEvent(NativeWindowState* state, NativeKeyAction action,
                    std::uint32_t code) {
    if (state == nullptr) {
        RecordWindowInputError(state, "native GLES key event has no window");
        return false;
    }
    if (state->key_events.size() >= kMaximumPointerEvents) {
        RecordWindowInputError(state,
                               "native GLES key event queue overflowed");
        return false;
    }
    state->key_events.push_back(
        {action, code, SteadyClockNanos()});
    return true;
}

bool AppendMessagePointerEvent(HWND window, NativeWindowState* state,
                               NativePointerAction action, LPARAM lparam) {
    RECT client{};
    if (!GetClientRect(window, &client) || client.right <= client.left ||
        client.bottom <= client.top) {
        RecordWindowInputError(
            state, "native GLES could not read pointer client dimensions");
        return false;
    }
    const std::int32_t x = static_cast<std::int32_t>(
        static_cast<std::int16_t>(LOWORD(lparam)));
    const std::int32_t y = static_cast<std::int32_t>(
        static_cast<std::int16_t>(HIWORD(lparam)));
    const std::int32_t client_w = client.right - client.left;
    const std::int32_t client_h = client.bottom - client.top;
    // Hosted mode: map display-child client coordinates into the fixed guest
    // logical surface space so the guest sees stable coordinates under host
    // scaling/letterboxing.
    std::int32_t gx = x;
    std::int32_t gy = y;
    if (state != nullptr && state->logical_width > 0 &&
        state->logical_height > 0) {
        gx = static_cast<std::int32_t>(
            (static_cast<std::int64_t>(x) * state->logical_width) / client_w);
        gy = static_cast<std::int32_t>(
            (static_cast<std::int64_t>(y) * state->logical_height) /
            client_h);
        state->last_x = gx;
        state->last_y = gy;
        state->last_client_width = state->logical_width;
        state->last_client_height = state->logical_height;
        return AppendPointerEvent(state, action, gx, gy, state->logical_width,
                                  state->logical_height);
    }
    state->last_x = x;
    state->last_y = y;
    state->last_client_width = client_w;
    state->last_client_height = client_h;
    return AppendPointerEvent(state, action, x, y, client_w, client_h);
}

void CancelActivePointer(HWND window, NativeWindowState* state) {
    if (state == nullptr || !state->pointer_active) return;
    state->pointer_active = false;
    (void)AppendPointerEvent(state, NativePointerAction::kCancel,
                             state->last_x, state->last_y,
                             state->last_client_width,
                             state->last_client_height);
    if (GetCapture() == window && !ReleaseCapture())
        RecordWindowInputError(
            state, "native GLES could not release Win32 pointer capture");
}

LRESULT CALLBACK NativeGlesWindowProcedure(HWND window, UINT message,
                                           WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* state = create != nullptr
                          ? static_cast<NativeWindowState*>(
                                create->lpCreateParams)
                          : nullptr;
        if (state == nullptr) return FALSE;
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(state)) == 0 &&
            GetLastError() != ERROR_SUCCESS)
            return FALSE;
    }
    auto* state = reinterpret_cast<NativeWindowState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_LBUTTONDOWN:
        if (state == nullptr || state->pointer_active) return 0;
        // Focus is best-effort: a background process is often denied
        // Win32 focus, and a touch-first game window still has to receive
        // its pointer events. Log the miss as a plain diagnostic so it
        // cannot fail the event pump; capture remains required.
        (void)SetFocus(window);
        if (GetFocus() != window) {
            std::fprintf(stderr,
                         "ISOMR_INPUT_FOCUS_MISS focus_denied=1\n");
            std::fflush(stderr);
        }
        (void)SetCapture(window);
        if (GetCapture() != window) {
            RecordWindowInputError(
                state, "native GLES could not capture the Win32 pointer");
            return 0;
        }
        state->pointer_active = true;
        if (!AppendMessagePointerEvent(window, state,
                                       NativePointerAction::kDown, lparam))
            CancelActivePointer(window, state);
        return 0;
    case WM_MOUSEMOVE:
        if (state != nullptr && state->pointer_active)
            (void)AppendMessagePointerEvent(
                window, state, NativePointerAction::kMove, lparam);
        // The display child fills the shell's viewport, so a pointer hovering
        // near the right edge is over this child rather than the shell and the
        // shell never sees its own edge-reveal hit-test. Forward the intent so
        // the floating toolbar can reveal over the game.
        if (state != nullptr && state->host_notify != nullptr &&
            !state->pointer_active) {
            RECT rc{};
            if (GetClientRect(window, &rc)) {
                const int x = static_cast<std::int16_t>(LOWORD(lparam));
                const int edge = (rc.right - rc.left) - 90;
                if (x >= edge)
                    PostMessageW(state->host_notify, kArmrMsgEdgeHover,
                                 static_cast<WPARAM>(x),
                                 static_cast<LPARAM>(
                                     static_cast<std::int16_t>(
                                         HIWORD(lparam))));
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (state == nullptr || !state->pointer_active) return 0;
        (void)AppendMessagePointerEvent(window, state,
                                        NativePointerAction::kUp, lparam);
        state->pointer_active = false;
        if (GetCapture() == window && !ReleaseCapture())
            RecordWindowInputError(
                state, "native GLES could not release Win32 pointer capture");
        return 0;
    case WM_CHAR:
        // Committed text (already translated, dead-key-aware). wparam is the
        // UTF-16 code unit the guest's text field wants.
        if (state != nullptr)
            (void)AppendKeyEvent(state, NativeKeyAction::kChar,
                                 static_cast<std::uint32_t>(wparam));
        return 0;
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN: {
        // Shell-level window-management accelerators must keep working while
        // the game surface holds keyboard focus. Forward F11, Alt+Enter, and
        // a bare Alt to the host shell for handling and do not also queue
        // them as guest input; every other key goes to the game.
        const DWORD vk = static_cast<DWORD>(wparam);
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (state != nullptr && state->host_notify != nullptr &&
            (vk == VK_F11 || (vk == VK_RETURN && alt) || vk == VK_MENU)) {
            PostMessageW(state->host_notify, message, wparam, lparam);
            return 0;
        }
        if (state != nullptr)
            (void)AppendKeyEvent(state, NativeKeyAction::kKeyDown,
                                 static_cast<std::uint32_t>(wparam));
        return 0;
    }
    case WM_SYSKEYUP:
    case WM_KEYUP:
        if (state != nullptr)
            (void)AppendKeyEvent(state, NativeKeyAction::kKeyUp,
                                 static_cast<std::uint32_t>(wparam));
        return 0;
    case WM_KILLFOCUS:
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        CancelActivePointer(window, state);
        return 0;
    case WM_CLOSE:
        CancelActivePointer(window, state);
        if (state != nullptr) {
            state->close_requested = true;
            if (state->host_notify != nullptr) {
                // Hosted by the ARMR game window: keep the last genuine frame
                // visible and tell the shell so it can show "Closing…"; the
                // shell owns teardown, so do not hide the surface here.
                PostMessageW(state->host_notify, kArmrMsgClosing, 0, 0);
            } else {
                ShowWindow(window, SW_HIDE);
            }
        }
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

// Minimal host for the fixed-size EGL render target. It carries no input or
// state — every message defers to DefWindowProc. The window is created
// hidden/off-screen; only its ANGLE surface matters.
LRESULT CALLBACK NativeGlesSurfaceProcedure(HWND window, UINT message,
                                            WPARAM wparam, LPARAM lparam) {
    (void)window;
    return DefWindowProcW(window, message, wparam, lparam);
}

bool EnsureWindowClass(std::string* error) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW existing{};
    if (GetClassInfoW(instance, kWindowClassName, &existing)) {
        if ((existing.style & CS_OWNDC) == 0) {
            SetError(error,
                     "native GLES window class has no owned device context");
            return false;
        }
    } else {
        WNDCLASSW window_class{};
        window_class.style = CS_OWNDC;
        window_class.lpfnWndProc = NativeGlesWindowProcedure;
        window_class.hInstance = instance;
        window_class.lpszClassName = kWindowClassName;
        if (RegisterClassW(&window_class) == 0 &&
            !(GetLastError() == ERROR_CLASS_ALREADY_EXISTS &&
              GetClassInfoW(instance, kWindowClassName, &existing) &&
              (existing.style & CS_OWNDC) != 0)) {
            SetError(error,
                     "native GLES could not register its Win32 window class");
            return false;
        }
    }
    // The off-screen render-target host shares the same class requirements.
    if (!GetClassInfoW(instance, kSurfaceClassName, &existing)) {
        WNDCLASSW surface_class{};
        surface_class.style = CS_OWNDC;
        surface_class.lpfnWndProc = NativeGlesSurfaceProcedure;
        surface_class.hInstance = instance;
        surface_class.lpszClassName = kSurfaceClassName;
        if (RegisterClassW(&surface_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            SetError(
                error,
                "native GLES could not register its surface window class");
            return false;
        }
    }
    return true;
}

void ClearGlErrors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

bool CheckGlCall(const char* name, std::string* error) {
    const GLenum host_error = glGetError();
    if (host_error == GL_NO_ERROR) return true;
    SetError(error, std::string("host ") + name + " failed with GL error " +
                        std::to_string(host_error));
    return false;
}

bool WriteFrameBmp(const wchar_t* path, const char* capture_source,
                   const std::vector<std::uint8_t>& rgba, std::size_t width,
                   std::size_t height, std::string* error) {
    constexpr std::size_t kRgbChannels = 3;
    const std::size_t unpadded_row_bytes = width * kRgbChannels;
    const std::size_t row_bytes = (unpadded_row_bytes + 3U) & ~3U;
    const std::size_t pixel_bytes = row_bytes * height;
    const std::size_t header_bytes =
        sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    if (width > static_cast<std::size_t>((std::numeric_limits<LONG>::max)()) ||
        height >
            static_cast<std::size_t>((std::numeric_limits<LONG>::max)()) ||
        pixel_bytes >
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)()) -
                header_bytes) {
        SetError(error, "native GLES draw capture is out of BMP range");
        return false;
    }

    BITMAPFILEHEADER file_header{};
    file_header.bfType = 0x4d42;
    file_header.bfOffBits = static_cast<DWORD>(header_bytes);
    file_header.bfSize = static_cast<DWORD>(header_bytes + pixel_bytes);
    BITMAPINFOHEADER info_header{};
    info_header.biSize = sizeof(info_header);
    info_header.biWidth = static_cast<LONG>(width);
    info_header.biHeight = static_cast<LONG>(height);
    info_header.biPlanes = 1;
    info_header.biBitCount = 24;
    info_header.biCompression = BI_RGB;
    info_header.biSizeImage = static_cast<DWORD>(pixel_bytes);

    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"wb") != 0 || file == nullptr) {
        SetError(error, "native GLES could not open the draw-capture file");
        return false;
    }
    bool ok = std::fwrite(&file_header, sizeof(file_header), 1, file) == 1 &&
              std::fwrite(&info_header, sizeof(info_header), 1, file) == 1;
    std::vector<std::uint8_t> row(row_bytes, 0);
    for (std::size_t y = 0; ok && y < height; ++y) {
        const std::size_t source_row = y * width * 4U;
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t source = source_row + x * 4U;
            const std::size_t destination = x * kRgbChannels;
            row[destination] = rgba[source + 2U];
            row[destination + 1U] = rgba[source + 1U];
            row[destination + 2U] = rgba[source];
        }
        ok = std::fwrite(row.data(), 1, row.size(), file) == row.size();
    }
    ok = std::fclose(file) == 0 && ok;
    if (!ok) {
        SetError(error, "native GLES could not write the draw-capture file");
        return false;
    }
    std::printf("ISOMR_FRAME_CAPTURE source=%s format=bmp width=%zu "
                "height=%zu path=%ls\n",
                capture_source, width, height, path);
    return true;
}

bool CaptureRequestedFrame(const wchar_t* capture_variable,
                           const char* capture_source,
                           const std::vector<std::uint8_t>& rgba,
                           std::size_t width, std::size_t height,
                           std::string* error) {
    constexpr std::size_t kMaximumPathCharacters = 32768;
    std::vector<wchar_t> path(kMaximumPathCharacters, L'\0');
    SetLastError(ERROR_SUCCESS);
    const DWORD path_length = GetEnvironmentVariableW(
        capture_variable, path.data(), static_cast<DWORD>(path.size()));
    if (path_length == 0) {
        const DWORD environment_error = GetLastError();
        if (environment_error == ERROR_SUCCESS ||
            environment_error == ERROR_ENVVAR_NOT_FOUND)
            return true;
        SetError(error, "native GLES could not read the draw-capture path");
        return false;
    }
    if (path_length >= path.size()) {
        SetError(error, "native GLES draw-capture path is too long");
        return false;
    }
    return WriteFrameBmp(path.data(), capture_source, rgba, width, height,
                         error);
}

// Present the read-back default framebuffer to the Win32 window via GDI. The
// caller supplies BGRA pixels (bottom-up, matching glReadPixels order): a
// 32bpp BI_RGB DIB is B,G,R,A so no further conversion is needed. Used when
// the ANGLE window surface's D3D11 present does not composite onto the
// visible HWND so the genuine guest framebuffer still reaches the window.
// The destination is stretched to the target window's client rectangle so a
// fixed-size guest logical surface scales into an aspect-fit display child.
void BlitPixelsToWindow(HDC device_context,
                        const std::uint8_t* bgra,
                        std::int32_t width, std::int32_t height) {
    if (device_context == nullptr || bgra == nullptr || width <= 0 ||
        height <= 0)
        return;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = height;  // bottom-up matches glReadPixels order
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    // Stretch the fixed-size guest frame to whatever client area the
    // destination window currently has (the aspect-fit display child).
    int dest_w = width;
    int dest_h = height;
    if (HWND target = WindowFromDC(device_context)) {
        RECT client{};
        if (GetClientRect(target, &client) && client.right > client.left &&
            client.bottom > client.top) {
            dest_w = client.right - client.left;
            dest_h = client.bottom - client.top;
        }
    }
    SetStretchBltMode(device_context, HALFTONE);
    SetBrushOrgEx(device_context, 0, 0, nullptr);
    StretchDIBits(device_context, 0, 0, dest_w, dest_h, 0, 0, width, height,
                  bgra, &info, DIB_RGB_COLORS, SRCCOPY);
}

bool FrameCaptureRequested(const wchar_t* capture_variable,
                           bool* requested,
                           std::string* error) {
    constexpr std::size_t kMaximumPathCharacters = 32768;
    if (capture_variable == nullptr || requested == nullptr) return false;
    *requested = false;
    std::vector<wchar_t> path(kMaximumPathCharacters, L'\0');
    SetLastError(ERROR_SUCCESS);
    const DWORD path_length = GetEnvironmentVariableW(
        capture_variable, path.data(), static_cast<DWORD>(path.size()));
    if (path_length == 0) {
        const DWORD environment_error = GetLastError();
        if (environment_error == ERROR_SUCCESS ||
            environment_error == ERROR_ENVVAR_NOT_FOUND)
            return true;
        SetError(error, "native GLES could not read the draw-capture path");
        return false;
    }
    if (path_length >= path.size()) {
        SetError(error, "native GLES draw-capture path is too long");
        return false;
    }
    *requested = true;
    return true;
}

bool RequestedPresentCapture(std::size_t* present,
                             bool* requested,
                             std::string* error) {
    constexpr wchar_t kCapturePresentVariable[] =
        L"ISOMR_PRESENT_CAPTURE_INDEX";
    std::array<wchar_t, 32> text{};
    if (present == nullptr || requested == nullptr) return false;
    *present = 0;
    *requested = false;
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetEnvironmentVariableW(
        kCapturePresentVariable, text.data(),
        static_cast<DWORD>(text.size()));
    if (length == 0) {
        const DWORD environment_error = GetLastError();
        if (environment_error == ERROR_SUCCESS ||
            environment_error == ERROR_ENVVAR_NOT_FOUND)
            return true;
        SetError(error,
                 "native GLES could not read the present-capture index");
        return false;
    }
    if (length >= text.size()) {
        SetError(error, "native GLES present-capture index is too long");
        return false;
    }
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long long value = std::wcstoull(text.data(), &end, 10);
    if (errno != 0 || end == text.data() || end == nullptr ||
        *end != L'\0' || value == 0 ||
        value > (std::numeric_limits<std::size_t>::max)()) {
        SetError(error, "native GLES present-capture index is invalid");
        return false;
    }
    *present = static_cast<std::size_t>(value);
    *requested = true;
    return true;
}

// ISOMR_PRESENT_CAPTURE_EVERY=N requests a rolling capture: every Nth present
// writes the framebuffer to ISOMR_PRESENT_CAPTURE_BMP, overwriting the file so
// the current guest screen can be inspected on demand without a restart. A
// present index of N always fires on the Nth present even without the index
// variable. Read once and cached so the hot present path does not re-read the
// environment every frame.
std::size_t RequestedPresentCaptureEvery() {
    static const std::size_t every = [] {
        wchar_t text[16]{};
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetEnvironmentVariableW(
            L"ISOMR_PRESENT_CAPTURE_EVERY", text,
            static_cast<DWORD>(std::size(text)));
        if (length == 0 || length >= std::size(text)) return std::size_t{0};
        const unsigned long long value = std::wcstoull(text, nullptr, 10);
        if (value == 0 || value > (std::numeric_limits<std::size_t>::max)())
            return std::size_t{0};
        return static_cast<std::size_t>(value);
    }();
    return every;
}

float FloatArgument(const Aarch64ImportRegistry::Registers& registers,
                    std::size_t index) {
    float value = 0.0F;
    static_assert(sizeof(value) <= sizeof(registers.vector[index]));
    std::memcpy(&value, registers.vector[index].data(), sizeof(value));
    return value;
}

}  // namespace

// Defined next to the texture upload implementation below; used by the ES3
// 3D-upload registrations inside RegisterDeferredGlesImports.
static std::size_t GlPixelComponentCount(std::uint32_t format);
static std::size_t GlPixelComponentSize(std::uint32_t type);

std::atomic<double> NativeGles::process_present_pacing_fps_{0.0};
std::atomic<std::int32_t> NativeGles::process_initial_surface_width_{0};
std::atomic<std::int32_t> NativeGles::process_initial_surface_height_{0};
std::atomic<HWND__*> NativeGles::process_host_window_{nullptr};

void NativeGles::PaceAfterSwap() {
    const double fps =
        process_present_pacing_fps_.load(std::memory_order_relaxed);
    if (fps < 1.0) return;
    using clock = std::chrono::steady_clock;
    static clock::time_point next_present_time{};
    const clock::time_point now = clock::now();
    if (next_present_time == clock::time_point{}) {
        next_present_time = now +
            std::chrono::nanoseconds(static_cast<long long>(1e9 / fps));
        return;
    }
    if (now < next_present_time) {
        std::this_thread::sleep_until(next_present_time);
        while (clock::now() < next_present_time) {
            std::this_thread::yield();
        }
        next_present_time += std::chrono::nanoseconds(
            static_cast<long long>(1e9 / fps));
    } else {
        // Fell behind (a long guest call): resync instead of bursting.
        next_present_time = now +
            std::chrono::nanoseconds(static_cast<long long>(1e9 / fps));
    }
}

NativeGles::NativeGles(GuestHeap* heap, GuestAddressSpace* memory)
    : heap_(heap), memory_(memory) {}

NativeGles::~NativeGles() {
    if (shutdown_) return;
    const bool owns_host_objects =
        window_state_ != nullptr || window_ != nullptr ||
        device_context_ != nullptr || display_ != EGL_NO_DISPLAY ||
        surface_ != EGL_NO_SURFACE ||
        rendering_context_ != EGL_NO_CONTEXT;
    if (owns_host_objects &&
        (owner_thread_id_ == 0 ||
         owner_thread_id_ != GetCurrentThreadId())) {
        // A live HWND may still dispatch through GWLP_USERDATA. Leaking the
        // state and host handles is the only safe destructor fallback because
        // destruction cannot report or recover from a foreign owner thread.
        (void)window_state_.release();
        return;
    }
    std::string ignored;
    if (Shutdown(&ignored)) return;
    if (rendering_context_ != EGL_NO_CONTEXT &&
        eglGetCurrentContext() == rendering_context_) {
        (void)DeletePrograms(&ignored);
        (void)DeleteShaders(&ignored);
        if (!framebuffer_names_.empty()) {
            const std::vector<GLuint> framebuffers(framebuffer_names_.begin(),
                                                   framebuffer_names_.end());
            glDeleteFramebuffers(static_cast<GLsizei>(framebuffers.size()),
                                 framebuffers.data());
            framebuffer_names_.clear();
        }
        if (!renderbuffer_names_.empty()) {
            const std::vector<GLuint> renderbuffers(
                renderbuffer_names_.begin(), renderbuffer_names_.end());
            glDeleteRenderbuffers(static_cast<GLsizei>(renderbuffers.size()),
                                  renderbuffers.data());
            renderbuffer_names_.clear();
        }
        if (!texture_names_.empty()) {
            const std::vector<GLuint> textures(texture_names_.begin(),
                                               texture_names_.end());
            glDeleteTextures(static_cast<GLsizei>(textures.size()),
                             textures.data());
            texture_names_.clear();
        }
        (void)eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                             EGL_NO_CONTEXT);
    }
    if (heap_ != nullptr) {
        for (std::uint64_t& address : string_guest_addresses_) {
            if (address == 0) continue;
            (void)heap_->Release(address, &ignored);
            address = 0;
        }
    }
    if (!DestroyHostObjects(&ignored) && window_ != nullptr &&
        window_state_ != nullptr && IsWindow(window_) &&
        reinterpret_cast<NativeWindowState*>(
            GetWindowLongPtrW(window_, GWLP_USERDATA)) ==
            window_state_.get()) {
        (void)window_state_.release();
    }
}

bool NativeGles::Initialize(std::string* error) {
    if (initialized_ || shutdown_) {
        SetError(error, shutdown_ ? "native GLES is already shut down"
                                  : "native GLES is already initialized");
        return false;
    }
    if (heap_ == nullptr || memory_ == nullptr) {
        SetError(error, "native GLES guest memory is unavailable");
        return false;
    }
    if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
        SetError(error,
                 "native GLES initialization found a foreign current context");
        return false;
    }
    if (!EnsureWindowClass(error)) return false;

    const std::int32_t logical_width =
        process_initial_surface_width_.load(std::memory_order_relaxed) > 0
            ? process_initial_surface_width_.load(std::memory_order_relaxed)
            : kInitialSurfaceWidth;
    const std::int32_t logical_height =
        process_initial_surface_height_.load(std::memory_order_relaxed) > 0
            ? process_initial_surface_height_.load(std::memory_order_relaxed)
            : kInitialSurfaceHeight;
    const HWND host_window =
        process_host_window_.load(std::memory_order_relaxed);
    hosted_ = host_window != nullptr;

    RECT window_rectangle{0, 0, logical_width, logical_height};
    if (!hosted_ &&
        !AdjustWindowRectEx(&window_rectangle, kWindowStyle, FALSE, 0)) {
        SetError(error, "native GLES could not size its Win32 window");
        return false;
    }
    window_state_ = std::make_unique<NativeWindowState>();
    owner_thread_id_ = GetCurrentThreadId();
    if (hosted_) {
        // The display child is the input + GDI present target, aspect-fit by
        // the host shell. The guest still sees the fixed logical size.
        window_state_->logical_width = logical_width;
        window_state_->logical_height = logical_height;
        window_state_->host_notify = host_window;
        window_ = CreateWindowExW(
            0, kWindowClassName, L"ARMR",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, logical_width,
            logical_height, host_window, nullptr, GetModuleHandleW(nullptr),
            window_state_.get());
        if (window_ == nullptr) {
            window_state_.reset();
            owner_thread_id_ = 0;
            SetError(
                error,
                "native GLES could not create its hosted display window");
            return false;
        }
        // The EGL render target lives on a fixed-size window parked off the
        // visible client area so the guest logical surface never resizes.
        surface_window_ = CreateWindowExW(
            0, kSurfaceClassName, L"", WS_CHILD | WS_VISIBLE,
            -(logical_width + 64), 0, logical_width, logical_height, window_,
            nullptr, GetModuleHandleW(nullptr), nullptr);
        if (surface_window_ == nullptr) {
            SetError(
                error,
                "native GLES could not create its fixed render-target host");
            DestroyHostObjects();
            return false;
        }
    } else {
        window_ = CreateWindowExW(
            0, kWindowClassName, L"ARMR", kWindowStyle,
            CW_USEDEFAULT, CW_USEDEFAULT,
            window_rectangle.right - window_rectangle.left,
            window_rectangle.bottom - window_rectangle.top, nullptr, nullptr,
            GetModuleHandleW(nullptr), window_state_.get());
        if (window_ == nullptr) {
            window_state_.reset();
            owner_thread_id_ = 0;
            SetError(error,
                     "native GLES could not create its Win32 context window");
            return false;
        }
        surface_window_ = window_;
    }
    device_context_ = GetDC(window_);
    if (device_context_ == nullptr) {
        SetError(error, "native GLES could not acquire a Win32 device context");
        DestroyHostObjects();
        return false;
    }

    const char* client_extensions =
        eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (client_extensions == nullptr ||
        std::strstr(client_extensions, "EGL_ANGLE_platform_angle") ==
            nullptr) {
        SetError(error,
                 "native GLES requires ANGLE platform display support");
        DestroyHostObjects();
        return false;
    }
    const auto get_platform_display =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (get_platform_display == nullptr) {
        SetError(error,
                 "native GLES could not resolve ANGLE platform display");
        DestroyHostObjects();
        return false;
    }
    const EGLint display_attributes[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE,
        EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE,
        EGL_NONE};
    display_ = get_platform_display(
        EGL_PLATFORM_ANGLE_ANGLE,
        reinterpret_cast<void*>(device_context_), display_attributes);
    EGLint major = 0;
    EGLint minor = 0;
    if (display_ == EGL_NO_DISPLAY ||
        eglInitialize(display_, &major, &minor) != EGL_TRUE) {
        SetError(error,
                 "native GLES could not initialize ANGLE D3D11 hardware");
        DestroyHostObjects();
        return false;
    }
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        SetError(error, "native GLES could not bind the OpenGL ES API");
        DestroyHostObjects();
        return false;
    }
    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE};
    EGLint config_count = 0;
    if (eglChooseConfig(display_, config_attributes, &config_, 1,
                        &config_count) != EGL_TRUE ||
        config_count != 1 || config_ == nullptr) {
        SetError(error,
                 "native GLES could not choose an ANGLE window config");
        DestroyHostObjects();
        return false;
    }
    surface_ = eglCreateWindowSurface(display_, config_, surface_window_,
                                      nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        SetError(error,
                 "native GLES could not create its ANGLE window surface");
        DestroyHostObjects();
        return false;
    }
    EGLint surface_width = 0;
    EGLint surface_height = 0;
    if (eglQuerySurface(display_, surface_, EGL_WIDTH, &surface_width) !=
            EGL_TRUE ||
        eglQuerySurface(display_, surface_, EGL_HEIGHT, &surface_height) !=
            EGL_TRUE ||
        surface_width <= 0 || surface_height <= 0 ||
        surface_width > 4096 || surface_height > 4096) {
        SetError(error,
                 "native GLES could not verify its bounded ANGLE surface");
        DestroyHostObjects();
        return false;
    }
    surface_width_ = static_cast<std::int32_t>(surface_width);
    surface_height_ = static_cast<std::int32_t>(surface_height);
    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    rendering_context_ = eglCreateContext(
        display_, config_, EGL_NO_CONTEXT, context_attributes);
    if (rendering_context_ == EGL_NO_CONTEXT) {
        SetError(error,
                 "native GLES could not create its ANGLE ES2 context");
        DestroyHostObjects();
        return false;
    }
    if (eglMakeCurrent(display_, surface_, surface_, rendering_context_) !=
        EGL_TRUE) {
        SetError(error,
                 "native GLES could not make its ANGLE context current");
        DestroyHostObjects();
        return false;
    }
    if (eglSwapInterval(display_, 1) != EGL_TRUE) {
        SetError(error, "native GLES could not set its ANGLE swap interval");
        DestroyHostObjects();
        return false;
    }
    GLboolean initial_depth_mask = GL_FALSE;
    ClearGlErrors();
    glGetBooleanv(GL_DEPTH_WRITEMASK, &initial_depth_mask);
    glDepthMask(GL_TRUE);
    glClearDepthf(1.0F);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDepthMask(initial_depth_mask);
    if (!CheckGlCall("initial ANGLE depth clear", error)) {
        DestroyHostObjects();
        return false;
    }
    // On Android the GLSurfaceView/EGL surface sizing sets the initial
    // viewport after the surface becomes current; nothing else does it,
    // and a degenerate 0x0 viewport clips every guest draw to nothing.
    ClearGlErrors();
    glViewport(0, 0, static_cast<GLsizei>(surface_width_),
               static_cast<GLsizei>(surface_height_));
    if (!CheckGlCall("initial ANGLE viewport", error)) {
        DestroyHostObjects();
        return false;
    }
    // The ANGLE window surface's D3D11 present does not composite onto the
    // visible HWND on this path, so present the read-back default framebuffer
    // to the window via GDI as well. ISOMR_GDI_PRESENT=0 disables it.
    {
        wchar_t present_flag[8] = {0};
        const DWORD flag_length = GetEnvironmentVariableW(
            L"ISOMR_GDI_PRESENT", present_flag, 8);
        gdi_present_ = !(flag_length > 0 && present_flag[0] == L'0');
        std::fprintf(stderr, "ISOMR_GDI_PRESENT active=%d\n",
                     gdi_present_ ? 1 : 0);
        std::fflush(stderr);
    }
    hardware_accelerated_ = true;
    initialized_ = true;
    return true;
}

bool NativeGles::RegisterImports(Aarch64ImportRegistry* registry,
                                 std::string* error) {
    if (!initialized_ || shutdown_ || registry == nullptr) {
        SetError(error, "native GLES is unavailable for import registration");
        return false;
    }
    if (!registry->Register(
            "glGetString",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                std::uint64_t result = 0;
                if (!GetString(static_cast<std::uint32_t>((*registers)[0]),
                               &result, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetStringi",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                std::uint64_t result = 0;
                if (!GetStringIndexed(
                        static_cast<std::uint32_t>((*registers)[0]),
                        static_cast<std::uint32_t>((*registers)[1]),
                        &result, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetIntegerv",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t name =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint64_t output = (*registers)[1];
                (*registers)[0] = 0;
                return GetInteger(name, output, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGenTextures",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint64_t count = (*registers)[0];
                const std::uint64_t output = (*registers)[1];
                (*registers)[0] = 0;
                return GenerateTextures(count, output, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDeleteTextures",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint64_t count = (*registers)[0];
                const std::uint64_t input = (*registers)[1];
                (*registers)[0] = 0;
                return DeleteTextures(count, input, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glActiveTexture",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t texture =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return ActiveTexture(texture, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBindTexture",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t target =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t texture =
                    static_cast<std::uint32_t>((*registers)[1]);
                (*registers)[0] = 0;
                return BindTexture(target, texture, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glTexParameteri",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t target =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t name =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::int32_t value =
                    static_cast<std::int32_t>((*registers)[2]);
                (*registers)[0] = 0;
                return TextureParameter(target, name, value,
                                        dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGenRenderbuffers",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint64_t count = (*registers)[0];
                const std::uint64_t output = (*registers)[1];
                (*registers)[0] = 0;
                return GenerateRenderbuffers(count, output, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBindRenderbuffer",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t target =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t renderbuffer =
                    static_cast<std::uint32_t>((*registers)[1]);
                (*registers)[0] = 0;
                return BindRenderbuffer(target, renderbuffer,
                                        dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glRenderbufferStorage",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t target =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t internal_format =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::int32_t width =
                    static_cast<std::int32_t>((*registers)[2]);
                const std::int32_t height =
                    static_cast<std::int32_t>((*registers)[3]);
                (*registers)[0] = 0;
                return RenderbufferStorage(target, internal_format, width,
                                           height, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDeleteRenderbuffers",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint64_t count = (*registers)[0];
                const std::uint64_t input = (*registers)[1];
                (*registers)[0] = 0;
                return DeleteRenderbuffers(count, input, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGenFramebuffers",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint64_t count = (*registers)[0];
                const std::uint64_t output = (*registers)[1];
                (*registers)[0] = 0;
                return GenerateFramebuffers(count, output, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBindFramebuffer",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t target =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t framebuffer =
                    static_cast<std::uint32_t>((*registers)[1]);
                (*registers)[0] = 0;
                return BindFramebuffer(target, framebuffer, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glFramebufferTexture2D",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t target =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t attachment =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::uint32_t texture_target =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::uint32_t texture =
                    static_cast<std::uint32_t>((*registers)[3]);
                const std::int32_t level =
                    static_cast<std::int32_t>((*registers)[4]);
                (*registers)[0] = 0;
                return FramebufferTexture2d(target, attachment,
                                            texture_target, texture, level,
                                            dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glFramebufferRenderbuffer",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t target =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t attachment =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::uint32_t renderbuffer_target =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::uint32_t renderbuffer =
                    static_cast<std::uint32_t>((*registers)[3]);
                (*registers)[0] = 0;
                return FramebufferRenderbuffer(
                    target, attachment, renderbuffer_target, renderbuffer,
                    dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glCheckFramebufferStatus",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t target =
                    static_cast<std::uint32_t>((*registers)[0]);
                std::uint64_t result = 0;
                if (!CheckFramebufferStatus(target, &result,
                                            dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glDeleteFramebuffers",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint64_t count = (*registers)[0];
                const std::uint64_t input = (*registers)[1];
                (*registers)[0] = 0;
                return DeleteFramebuffers(count, input, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glViewport",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::int32_t x =
                    static_cast<std::int32_t>((*registers)[0]);
                const std::int32_t y =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::int32_t width =
                    static_cast<std::int32_t>((*registers)[2]);
                const std::int32_t height =
                    static_cast<std::int32_t>((*registers)[3]);
                (*registers)[0] = 0;
                return Viewport(x, y, width, height, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDepthRangef",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const float near_value = FloatArgument(*registers, 0);
                const float far_value = FloatArgument(*registers, 1);
                (*registers)[0] = 0;
                return DepthRange(near_value, far_value, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glClearColor",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const float red = FloatArgument(*registers, 0);
                const float green = FloatArgument(*registers, 1);
                const float blue = FloatArgument(*registers, 2);
                const float alpha = FloatArgument(*registers, 3);
                (*registers)[0] = 0;
                return ClearColor(red, green, blue, alpha, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glClearDepthf",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const float depth = FloatArgument(*registers, 0);
                (*registers)[0] = 0;
                return ClearDepth(depth, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glClearStencil",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::int32_t stencil =
                    static_cast<std::int32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return ClearStencil(stencil, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glStencilMask",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mask =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return StencilMask(mask, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glStencilFunc",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t function =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t reference =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint32_t mask =
                    static_cast<std::uint32_t>((*registers)[2]);
                (*registers)[0] = 0;
                return StencilFunction(function, reference, mask,
                                       dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glStencilOp",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t sfail =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t dpfail =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::uint32_t dppass =
                    static_cast<std::uint32_t>((*registers)[2]);
                (*registers)[0] = 0;
                return StencilOperation(sfail, dpfail, dppass,
                                        dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glStencilFuncSeparate",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t face =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t function =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::int32_t reference =
                    static_cast<std::int32_t>((*registers)[2]);
                const std::uint32_t mask =
                    static_cast<std::uint32_t>((*registers)[3]);
                (*registers)[0] = 0;
                return StencilFunctionSeparate(face, function, reference,
                                               mask, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glStencilOpSeparate",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t face =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t sfail =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::uint32_t dpfail =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::uint32_t dppass =
                    static_cast<std::uint32_t>((*registers)[3]);
                (*registers)[0] = 0;
                return StencilOperationSeparate(face, sfail, dpfail, dppass,
                                                dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glClear",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mask =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return Clear(mask, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glEnable",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t capability =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return SetCapability(capability, true, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDisable",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t capability =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return SetCapability(capability, false, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glColorMask",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const bool red =
                    static_cast<std::uint8_t>((*registers)[0]) != 0;
                const bool green =
                    static_cast<std::uint8_t>((*registers)[1]) != 0;
                const bool blue =
                    static_cast<std::uint8_t>((*registers)[2]) != 0;
                const bool alpha =
                    static_cast<std::uint8_t>((*registers)[3]) != 0;
                (*registers)[0] = 0;
                return ColorMask(red, green, blue, alpha, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDepthMask",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const bool enabled =
                    static_cast<std::uint8_t>((*registers)[0]) != 0;
                (*registers)[0] = 0;
                return DepthMask(enabled, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBlendColor",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const float red = FloatArgument(*registers, 0);
                const float green = FloatArgument(*registers, 1);
                const float blue = FloatArgument(*registers, 2);
                const float alpha = FloatArgument(*registers, 3);
                (*registers)[0] = 0;
                return BlendColor(red, green, blue, alpha, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBlendFunc",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t source =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t destination =
                    static_cast<std::uint32_t>((*registers)[1]);
                (*registers)[0] = 0;
                return BlendFunction(source, destination, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBlendFuncSeparate",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t source_rgb =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t destination_rgb =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::uint32_t source_alpha =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::uint32_t destination_alpha =
                    static_cast<std::uint32_t>((*registers)[3]);
                (*registers)[0] = 0;
                return BlendFunctionSeparate(
                    source_rgb, destination_rgb, source_alpha,
                    destination_alpha, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBlendEquation",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mode =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return BlendEquation(mode, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBlendEquationSeparate",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t rgb_mode =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t alpha_mode =
                    static_cast<std::uint32_t>((*registers)[1]);
                (*registers)[0] = 0;
                return BlendEquationSeparate(rgb_mode, alpha_mode,
                                             dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDepthFunc",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t function =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return DepthFunction(function, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glCullFace",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mode =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return CullFace(mode, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glFrontFace",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t winding =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return FrontFace(winding, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glScissor",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::int32_t x =
                    static_cast<std::int32_t>((*registers)[0]);
                const std::int32_t y =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::int32_t width =
                    static_cast<std::int32_t>((*registers)[2]);
                const std::int32_t height =
                    static_cast<std::int32_t>((*registers)[3]);
                (*registers)[0] = 0;
                return Scissor(x, y, width, height, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glEnableVertexAttribArray",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t index =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return SetVertexAttributeArray(index, true, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDisableVertexAttribArray",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t index =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return SetVertexAttributeArray(index, false, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttribPointer",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t index =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t size =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint32_t type =
                    static_cast<std::uint32_t>((*registers)[2]);
                const bool normalized =
                    static_cast<std::uint8_t>((*registers)[3]) != 0;
                const std::int32_t stride =
                    static_cast<std::int32_t>((*registers)[4]);
                const std::uint64_t pointer = (*registers)[5];
                (*registers)[0] = 0;
                return VertexAttributePointer(index, size, type, normalized,
                                              stride, pointer,
                                              dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttrib2fv",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t index =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint64_t values = (*registers)[1];
                (*registers)[0] = 0;
                return VertexAttribute2fv(index, values, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttrib3fv",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t index =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint64_t values = (*registers)[1];
                (*registers)[0] = 0;
                return VertexAttribute3fv(index, values, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttrib4fv",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t index =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint64_t values = (*registers)[1];
                (*registers)[0] = 0;
                return VertexAttribute4fv(index, values, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glPixelStorei",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t name =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t value =
                    static_cast<std::int32_t>((*registers)[1]);
                (*registers)[0] = 0;
                return PixelStore(name, value, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glTexImage2D",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* dispatch_error) {
            const std::uint32_t target =
                static_cast<std::uint32_t>((*registers)[0]);
            const std::int32_t level =
                static_cast<std::int32_t>((*registers)[1]);
            const std::int32_t internal_format =
                static_cast<std::int32_t>((*registers)[2]);
            const std::int32_t width =
                static_cast<std::int32_t>((*registers)[3]);
            const std::int32_t height =
                static_cast<std::int32_t>((*registers)[4]);
            const std::int32_t border =
                static_cast<std::int32_t>((*registers)[5]);
            const std::uint32_t format =
                static_cast<std::uint32_t>((*registers)[6]);
            const std::uint32_t type =
                static_cast<std::uint32_t>((*registers)[7]);
            (*registers)[0] = 0;
            return TexImage2d(target, level, internal_format, width, height,
                              border, format, type,
                              registers->stack_pointer, dispatch_error);
        },
        error))
        return false;
    if (!registry->Register(
            "glCreateShader",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                std::uint64_t result = 0;
                if (!CreateShader(
                        static_cast<std::uint32_t>((*registers)[0]), &result,
                        dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glTexSubImage2D",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t target =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t level =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::int32_t x_offset =
                    static_cast<std::int32_t>((*registers)[2]);
                const std::int32_t y_offset =
                    static_cast<std::int32_t>((*registers)[3]);
                const std::int32_t width =
                    static_cast<std::int32_t>((*registers)[4]);
                const std::int32_t height =
                    static_cast<std::int32_t>((*registers)[5]);
                const std::uint32_t format =
                    static_cast<std::uint32_t>((*registers)[6]);
                const std::uint32_t type =
                    static_cast<std::uint32_t>((*registers)[7]);
            (*registers)[0] = 0;
            return TexSubImage2d(
                target, level, x_offset, y_offset, width, height,
                format, type, registers->stack_pointer, dispatch_error);
            },
            error))
        return false;
    // glTexStorage2D/3D (ES3 core; EXT aliases for pre-ES3 drivers). Unity's
    // GLES3 device resolves these via dlsym/eglGetProcAddress and allocates
    // every render-target texture through them — when the name is missing the
    // guest silently skips storage definition and later attaches an empty
    // texture, surfacing as GL_INVALID_FRAMEBUFFER_OPERATION at glClear.
    const auto tex_storage =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* dispatch_error, bool three_dimensional) {
            constexpr std::uint32_t kGlTexture2d = 0x0de1;
            constexpr std::uint32_t kGlTextureCubeMap = 0x8513;
            constexpr std::uint32_t kGlTexture3d = 0x806f;
            constexpr std::uint32_t kGlTexture2dArray = 0x8c1a;
            const std::uint32_t target =
                static_cast<std::uint32_t>((*registers)[0]);
            const std::int32_t levels =
                static_cast<std::int32_t>((*registers)[1]);
            const std::uint32_t internal_format =
                static_cast<std::uint32_t>((*registers)[2]);
            const std::int32_t width =
                static_cast<std::int32_t>((*registers)[3]);
            const std::int32_t height =
                static_cast<std::int32_t>((*registers)[4]);
            const std::int32_t depth =
                three_dimensional
                    ? static_cast<std::int32_t>((*registers)[5])
                    : 0;
            (*registers)[0] = 0;
            const bool valid_target =
                three_dimensional
                    ? (target == kGlTexture3d || target == kGlTexture2dArray)
                    : (target == kGlTexture2d || target == kGlTextureCubeMap);
            if (!valid_target || levels < 1 || levels > 16 || width < 1 ||
                height < 1 || (three_dimensional && depth < 1) ||
                width > 16384 || height > 16384 || depth > 16384) {
                SetError(dispatch_error,
                         "native GLES glTexStorage contract is not "
                         "implemented: target=" + std::to_string(target) +
                             " levels=" + std::to_string(levels));
                return false;
            }
            if (!initialized_ || shutdown_ ||
                eglGetCurrentContext() == EGL_NO_CONTEXT) {
                SetError(dispatch_error,
                         "glTexStorage requires a context current on the "
                         "calling thread");
                return false;
            }
            const std::uint32_t bound_texture =
                BoundTextureForUploadTarget(target);
            if (bound_texture == 0 ||
                !texture_names_.contains(bound_texture)) {
                SetError(dispatch_error,
                         "native GLES glTexStorage has no owned texture "
                         "bound");
                return false;
            }
            ClearGlErrors();
            if (three_dimensional)
                glTexStorage3D(static_cast<GLenum>(target),
                               static_cast<GLsizei>(levels),
                               static_cast<GLenum>(internal_format),
                               static_cast<GLsizei>(width),
                               static_cast<GLsizei>(height),
                               static_cast<GLsizei>(depth));
            else
                glTexStorage2D(static_cast<GLenum>(target),
                               static_cast<GLsizei>(levels),
                               static_cast<GLenum>(internal_format),
                               static_cast<GLsizei>(width),
                               static_cast<GLsizei>(height));
            if (!CheckGlCall(three_dimensional ? "glTexStorage3D"
                                               : "glTexStorage2D",
                             dispatch_error))
                return false;
            // Immutable storage defines every level at once; record each so
            // glTexSubImage*/FBO-attach completeness checks see real dims.
            const std::uint32_t face_base =
                target == kGlTextureCubeMap ? 0x8515u : target;
            const std::uint32_t face_count =
                target == kGlTextureCubeMap ? 6u : 1u;
            for (std::uint32_t face = 0; face < face_count; ++face) {
                for (std::int32_t level = 0; level < levels; ++level) {
                    const std::int32_t mip_w = std::max(1, width >> level);
                    const std::int32_t mip_h = std::max(1, height >> level);
                    texture_2d_defined_levels_[bound_texture].insert(
                        ((face_base + face) << 16) |
                        static_cast<std::uint32_t>(level));
                    texture_level_images_.insert_or_assign(
                        (static_cast<std::uint64_t>(bound_texture) << 32) |
                            static_cast<std::uint32_t>(level),
                        TextureLevelImage{false, internal_format, mip_w,
                                          mip_h});
                }
            }
            std::fprintf(stderr,
                         "ISOMR_TEXSTORAGE tgt=%x tex=%u levels=%d ifmt=%x "
                         "%dx%dx%d\n",
                         target, bound_texture, levels, internal_format,
                         width, height, depth);
            std::fflush(stderr);
            return true;
        };
    for (const char* name :
         {"glTexStorage2D", "glTexStorage2DEXT", "glTexStorage3D",
          "glTexStorage3DEXT"}) {
        const bool three_d = name[12] == '3';
        if (!registry->Register(
                name,
                [tex_storage, three_d](
                    Aarch64ImportRegistry::Registers* registers,
                    std::string* dispatch_error) {
                    return tex_storage(registers, dispatch_error, three_d);
                },
                error))
            return false;
    }
    if (!registry->Register(
            "glShaderSource",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t shader =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint64_t strings = (*registers)[2];
                const std::uint64_t lengths = (*registers)[3];
                (*registers)[0] = 0;
                return ShaderSource(shader, count, strings, lengths,
                                    dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glCompileShader",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t shader =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return CompileShader(shader, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetShaderiv",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t shader =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t name =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                return GetShaderInteger(shader, name, output,
                                        dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetShaderInfoLog",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t shader =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t buffer_size =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint64_t output_length = (*registers)[2];
                const std::uint64_t output_log = (*registers)[3];
                (*registers)[0] = 0;
                return GetShaderInfoLog(shader, buffer_size, output_length,
                                        output_log, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDeleteShader",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t shader =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return DeleteShader(shader, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "eglGetProcAddress",
            [this, registry](Aarch64ImportRegistry::Registers* registers,
                             std::string* dispatch_error) {
                std::uint64_t result = 0;
                if (!GetProcAddress(registry, (*registers)[0], &result,
                                    dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glCreateProgram",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                std::uint64_t result = 0;
                if (!CreateProgram(&result, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glAttachShader",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t program =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t shader =
                    static_cast<std::uint32_t>((*registers)[1]);
                (*registers)[0] = 0;
                return AttachShader(program, shader, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBindAttribLocation",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t program =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t index =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::uint64_t name = (*registers)[2];
                (*registers)[0] = 0;
                return BindAttribLocation(program, index, name,
                                          dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glLinkProgram",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t program =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return LinkProgram(program, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetProgramiv",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t program =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t name =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                return GetProgramInteger(program, name, output,
                                         dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetProgramInfoLog",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t program =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t buffer_size =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint64_t output_length = (*registers)[2];
                const std::uint64_t output_log = (*registers)[3];
                (*registers)[0] = 0;
                return GetProgramInfoLog(program, buffer_size,
                                         output_length, output_log,
                                         dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetActiveUniform",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t program =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t index =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::int32_t buffer_size =
                    static_cast<std::int32_t>((*registers)[2]);
                const std::uint64_t output_length = (*registers)[3];
                const std::uint64_t output_size = (*registers)[4];
                const std::uint64_t output_type = (*registers)[5];
                const std::uint64_t output_name = (*registers)[6];
                (*registers)[0] = 0;
                return GetActiveVariable(
                    true, program, index, buffer_size, output_length,
                    output_size, output_type, output_name, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetActiveAttrib",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t program =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t index =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::int32_t buffer_size =
                    static_cast<std::int32_t>((*registers)[2]);
                const std::uint64_t output_length = (*registers)[3];
                const std::uint64_t output_size = (*registers)[4];
                const std::uint64_t output_type = (*registers)[5];
                const std::uint64_t output_name = (*registers)[6];
                (*registers)[0] = 0;
                return GetActiveVariable(
                    false, program, index, buffer_size, output_length,
                    output_size, output_type, output_name, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetUniformLocation",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                std::uint64_t result = 0;
                if (!GetVariableLocation(
                        true, static_cast<std::uint32_t>((*registers)[0]),
                        (*registers)[1], &result, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetAttribLocation",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                std::uint64_t result = 0;
                if (!GetVariableLocation(
                        false, static_cast<std::uint32_t>((*registers)[0]),
                        (*registers)[1], &result, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glUseProgram",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t program =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return UseProgram(program, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform1i",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::int32_t location =
                    static_cast<std::int32_t>((*registers)[0]);
                const std::int32_t value =
                    static_cast<std::int32_t>((*registers)[1]);
                (*registers)[0] = 0;
                return Uniform1i(location, value, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform1f",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::int32_t location =
                    static_cast<std::int32_t>((*registers)[0]);
                const float value = FloatArgument(*registers, 0);
                (*registers)[0] = 0;
                return Uniform1f(location, value, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform1fv",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::int32_t location =
                    static_cast<std::int32_t>((*registers)[0]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint64_t values = (*registers)[2];
                (*registers)[0] = 0;
                return Uniform1fv(location, count, values, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform2fv",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::int32_t location =
                    static_cast<std::int32_t>((*registers)[0]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint64_t values = (*registers)[2];
                (*registers)[0] = 0;
                return Uniform2fv(location, count, values, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform4f",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::int32_t location =
                    static_cast<std::int32_t>((*registers)[0]);
                const float x = FloatArgument(*registers, 0);
                const float y = FloatArgument(*registers, 1);
                const float z = FloatArgument(*registers, 2);
                const float w = FloatArgument(*registers, 3);
                (*registers)[0] = 0;
                return Uniform4f(location, x, y, z, w, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform4fv",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::int32_t location =
                    static_cast<std::int32_t>((*registers)[0]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint64_t values = (*registers)[2];
                (*registers)[0] = 0;
                return Uniform4fv(location, count, values, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniformMatrix4fv",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::int32_t location =
                    static_cast<std::int32_t>((*registers)[0]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[1]);
                const bool transpose =
                    static_cast<std::uint8_t>((*registers)[2]) != 0;
                const std::uint64_t values = (*registers)[3];
                (*registers)[0] = 0;
                return UniformMatrix4fv(location, count, transpose, values,
                                        dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDrawElements",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mode =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint32_t type =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::uint64_t indices = (*registers)[3];
                (*registers)[0] = 0;
                return DrawElements(mode, count, type, indices,
                                    dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDeleteProgram",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t program =
                    static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return DeleteProgram(program, dispatch_error);
            },
            error))
        return false;
    if (!RegisterDeferredGlesImports(registry, error)) return false;
    return RegisterGuestSurfaceImports(registry, error);
}

// GLES1-era entry points the PvZ renderer links against that the core
// registration above does not cover. Each forwards to the owned ANGLE
// context; guest pointers are translated through the guest address space.
bool NativeGles::RegisterDeferredGlesImports(Aarch64ImportRegistry* registry,
                                             std::string* error) {
    if (registry == nullptr) {
        SetError(error, "native GLES import registry is unavailable");
        return false;
    }
    const auto gate = [this](const char* name, std::string* dispatch_error) {
        // PvZ creates and binds its own ANGLE context through the egl*
        // imports; this layer's internal context is not involved. Host GL
        // calls route through ANGLE's per-thread current context, so only
        // require that one exists.
        if (shutdown_ || eglGetCurrentContext() == EGL_NO_CONTEXT) {
            SetError(dispatch_error,
                     std::string(name) +
                         " requires a context current on the calling thread");
            return false;
        }
        ClearGlErrors();
        return true;
    };
    // GLES1-only hint targets have no GLES2/3 meaning; accept them without
    // touching the host context.
    constexpr GLenum kPerspectiveCorrectionHint = 0x8190;
    constexpr GLenum kFogHint = 0x8191;
    constexpr GLenum kPointSmoothHint = 0x8B61;
    constexpr GLenum kLineSmoothHint = 0x8B62;
    constexpr std::size_t kMaxQueryElements = 256;
    constexpr std::size_t kMaxBufferData = 64U * 1024U * 1024U;

    if (!registry->Register(
            "glHint",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLenum mode = static_cast<GLenum>((*registers)[1]);
                (*registers)[0] = 0;
                if (target == kPerspectiveCorrectionHint ||
                    target == kFogHint || target == kPointSmoothHint ||
                    target == kLineSmoothHint)
                    return true;
                if (!gate("glHint", dispatch_error)) return false;
                glHint(target, mode);
                return CheckGlCall("glHint", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glLineWidth",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const float width = FloatArgument(*registers, 0);
                (*registers)[0] = 0;
                if (!gate("glLineWidth", dispatch_error)) return false;
                glLineWidth(width);
                return CheckGlCall("glLineWidth", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glPolygonOffset",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const float factor = FloatArgument(*registers, 0);
                const float units = FloatArgument(*registers, 1);
                (*registers)[0] = 0;
                if (!gate("glPolygonOffset", dispatch_error)) return false;
                glPolygonOffset(factor, units);
                return CheckGlCall("glPolygonOffset", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glSampleCoverage",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const float value = FloatArgument(*registers, 0);
                const GLboolean invert =
                    static_cast<GLboolean>((*registers)[1] & 0xffU);
                (*registers)[0] = 0;
                if (!gate("glSampleCoverage", dispatch_error)) return false;
                glSampleCoverage(value, invert);
                return CheckGlCall("glSampleCoverage", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glStencilMaskSeparate",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum face = static_cast<GLenum>((*registers)[0]);
                const GLuint mask = static_cast<GLuint>((*registers)[1]);
                (*registers)[0] = 0;
                if (!gate("glStencilMaskSeparate", dispatch_error))
                    return false;
                glStencilMaskSeparate(face, mask);
                return CheckGlCall("glStencilMaskSeparate", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glTexParameterf",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const float param = FloatArgument(*registers, 2);
                (*registers)[0] = 0;
                if (!gate("glTexParameterf", dispatch_error)) return false;
                glTexParameterf(target, pname, param);
                return CheckGlCall("glTexParameterf", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glTexParameterfv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t params = (*registers)[2];
                (*registers)[0] = 0;
                GLfloat value = 0.0F;
                if (params != 0 &&
                    !memory_->Read(params, &value, sizeof(value))) {
                    SetError(dispatch_error,
                             "glTexParameterfv params are unreadable");
                    return false;
                }
                if (!gate("glTexParameterfv", dispatch_error)) return false;
                glTexParameterfv(target, pname, params != 0 ? &value : nullptr);
                return CheckGlCall("glTexParameterfv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glTexParameteriv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t params = (*registers)[2];
                (*registers)[0] = 0;
                GLint value = 0;
                if (params != 0 &&
                    !memory_->Read(params, &value, sizeof(value))) {
                    SetError(dispatch_error,
                             "glTexParameteriv params are unreadable");
                    return false;
                }
                if (!gate("glTexParameteriv", dispatch_error)) return false;
                glTexParameteriv(target, pname, params != 0 ? &value : nullptr);
                return CheckGlCall("glTexParameteriv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBindBuffer",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLuint buffer = static_cast<GLuint>((*registers)[1]);
                (*registers)[0] = 0;
                if (!gate("glBindBuffer", dispatch_error)) return false;
                if (target == 0x8892 /* GL_ARRAY_BUFFER */)
                    bound_array_buffer_ = buffer;
                glBindBuffer(target, buffer);
                return CheckGlCall("glBindBuffer", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBufferData",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLsizeiptr size =
                    static_cast<GLsizeiptr>((*registers)[1]);
                const std::uint64_t data = (*registers)[2];
                const GLenum usage = static_cast<GLenum>((*registers)[3]);
                (*registers)[0] = 0;
                if (size < 0 || static_cast<std::uint64_t>(size) >
                                        kMaxBufferData) {
                    SetError(dispatch_error,
                             "glBufferData size exceeds the supported bound");
                    return false;
                }
                std::vector<std::uint8_t> host(
                    static_cast<std::size_t>(size));
                if (data != 0 && size != 0 &&
                    !memory_->Read(data, host.data(), host.size())) {
                    // Dump the owning buffer object (caller sp+0x70) stream
                    // fields to see whether +0x320 was populated or holds
                    // garbage.
                    std::uint64_t object = 0;
                    std::uint64_t f0 = 0, f2f4 = 0, f2f8 = 0, f304 = 0,
                                f308 = 0, f310 = 0, f318 = 0, f31c = 0,
                                f320 = 0, f33c = 0, f348 = 0, f350 = 0,
                                f08 = 0, f40 = 0, f100 = 0, f200 = 0;
                    if (memory_->Read(registers->stack_pointer + 0x70,
                                      &object, sizeof(object))) {
                        memory_->Read(object + 0x0, &f0, 8);
                        memory_->Read(object + 0x08, &f08, 8);
                        memory_->Read(object + 0x40, &f40, 8);
                        memory_->Read(object + 0x100, &f100, 8);
                        memory_->Read(object + 0x200, &f200, 8);
                        memory_->Read(object + 0x2f4, &f2f4, 8);
                        memory_->Read(object + 0x2f8, &f2f8, 8);
                        memory_->Read(object + 0x304, &f304, 8);
                        memory_->Read(object + 0x308, &f308, 8);
                        memory_->Read(object + 0x310, &f310, 8);
                        memory_->Read(object + 0x318, &f318, 8);
                        memory_->Read(object + 0x31c, &f31c, 8);
                        memory_->Read(object + 0x320, &f320, 8);
                        memory_->Read(object + 0x33c, &f33c, 8);
                        memory_->Read(object + 0x348, &f348, 8);
                        memory_->Read(object + 0x350, &f350, 8);
                    }
                    std::uint64_t helper = 0, helper_fn = 0;
                    if (memory_->Read(registers->stack_pointer + 0x20,
                                      &helper, sizeof(helper))) {
                        memory_->Read(helper, &helper_fn, 8);
                    }
                    // Scan the guest stack frame for the real DrawNode
                    // (vptr 0x4002e81dd0) to see whether sp+0x70 was
                    // overwritten or the import sp differs from init's.
                    std::uint64_t found_this_slot = 0;
                    for (std::uint64_t so = 0; so < 0x140; so += 8) {
                        std::uint64_t cand = 0;
                        if (!memory_->Read(registers->stack_pointer + so,
                                           &cand, 8) ||
                            cand < 0x710000000000ULL ||
                            cand >= 0x710000000000ULL + 0x40000000ULL)
                            continue;
                        std::uint64_t vt = 0;
                        if (memory_->Read(cand, &vt, 8) &&
                            vt == 0x4002e81dd0ULL) {
                            found_this_slot = so;
                            break;
                        }
                    }
                    char buffer_data_detail[560];
                    std::snprintf(
                        buffer_data_detail, sizeof(buffer_data_detail),
                        "glBufferData data is unreadable data=%llx size=%d "
                        "target=%x usage=%x lr=%llx obj=%llx vt0=%llx "
                        "f08=%llx f40=%llx f100=%llx f200=%llx "
                        "f2f4=%llx f2f8=%llx f304=%llx c308=%llx d310=%llx "
                        "c318=%llx u31c=%llx d320=%llx c33c=%llx d348=%llx "
                        "f350=%llx helper=%llx hfn=%llx sp=%llx thislot=%llx",
                        static_cast<unsigned long long>(data),
                        static_cast<int>(size), static_cast<unsigned>(target),
                        static_cast<unsigned>(usage),
                        static_cast<unsigned long long>(
                            registers->callee_saved_general[11]),
                        static_cast<unsigned long long>(object),
                        static_cast<unsigned long long>(f0),
                        static_cast<unsigned long long>(f08),
                        static_cast<unsigned long long>(f40),
                        static_cast<unsigned long long>(f100),
                        static_cast<unsigned long long>(f200),
                        static_cast<unsigned long long>(f2f4),
                        static_cast<unsigned long long>(f2f8),
                        static_cast<unsigned long long>(f304),
                        static_cast<unsigned long long>(f308),
                        static_cast<unsigned long long>(f310),
                        static_cast<unsigned long long>(f318),
                        static_cast<unsigned long long>(f31c),
                        static_cast<unsigned long long>(f320),
                        static_cast<unsigned long long>(f33c),
                        static_cast<unsigned long long>(f348),
                        static_cast<unsigned long long>(f350),
                        static_cast<unsigned long long>(helper),
                        static_cast<unsigned long long>(helper_fn),
                        static_cast<unsigned long long>(
                            registers->stack_pointer),
                        static_cast<unsigned long long>(found_this_slot));
                    SetError(dispatch_error, buffer_data_detail);
                    return false;
                }
                if (!gate("glBufferData", dispatch_error)) return false;
                {
                    static std::size_t logged_buffers = 0;
                    if (logged_buffers < 200) {
                        ++logged_buffers;
                        const float* vertex_floats =
                            reinterpret_cast<const float*>(host.data());
                        std::uint64_t nonzero = 0;
                        for (std::size_t index = 0;
                             index < host.size(); ++index)
                            nonzero += host[index] != 0;
                        std::fprintf(stderr,
                                     "ISOMR_BUFFERDATA target=%x size=%d "
                                     "usage=%x nonzero=%zu floats:",
                                     static_cast<unsigned>(target),
                                     static_cast<int>(size),
                                     static_cast<unsigned>(usage),
                                     nonzero);
                        for (std::size_t index = 0;
                             index < 9 && index < host.size() / sizeof(float);
                             ++index)
                            std::fprintf(stderr, " %.3f", vertex_floats[index]);
                        std::fprintf(stderr, "\n");
                    }
                }
                glBufferData(target, size,
                             data != 0 && size != 0 ? host.data() : nullptr,
                             usage);
                return CheckGlCall("glBufferData", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBufferSubData",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLintptr offset = static_cast<GLintptr>((*registers)[1]);
                const GLsizeiptr size =
                    static_cast<GLsizeiptr>((*registers)[2]);
                const std::uint64_t data = (*registers)[3];
                (*registers)[0] = 0;
                if (size < 0 || static_cast<std::uint64_t>(size) >
                                        kMaxBufferData) {
                    SetError(dispatch_error,
                             "glBufferSubData size exceeds the supported bound");
                    return false;
                }
                std::vector<std::uint8_t> host(
                    static_cast<std::size_t>(size));
                if (data != 0 && size != 0 &&
                    !memory_->Read(data, host.data(), host.size())) {
                    SetError(dispatch_error,
                             "glBufferSubData data is unreadable");
                    return false;
                }
                if (!gate("glBufferSubData", dispatch_error)) return false;
                {
                    static std::size_t logged_subs = 0;
                    if (logged_subs < 30) {
                        ++logged_subs;
                        const float* floats =
                            reinterpret_cast<const float*>(host.data());
                        std::fprintf(stderr,
                                     "ISOMR_BUFFERSUB target=%x off=%d "
                                     "size=%d head=",
                                     target, static_cast<int>(offset),
                                     static_cast<int>(size));
                        for (std::size_t index = 0;
                             index < 6 &&
                             index < host.size() / sizeof(float);
                             ++index)
                            std::fprintf(stderr, " %.3f", floats[index]);
                        std::fprintf(stderr, "\n");
                    }
                }
                glBufferSubData(target, offset, size,
                                data != 0 && size != 0 ? host.data() : nullptr);
                // Write through to the mapped guest storage (if mapped) so
                // the guest's own reads of the mapped pointer see the same
                // data the host GL buffer holds.
                {
                    const auto mapped = mapped_buffer_storage_.find(static_cast<std::uint32_t>(target));
                    if (mapped != mapped_buffer_storage_.end() &&
                        offset >= 0 &&
                        static_cast<std::uint64_t>(offset + size) <=
                            mapped->second.second) {
                        (void)memory_->Write(
                            mapped->second.first +
                                static_cast<std::uint64_t>(offset),
                            host.data(), host.size());
                    }
                }
                return CheckGlCall("glBufferSubData", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGenBuffers",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLsizei count =
                    static_cast<GLsizei>((*registers)[0]);
                const std::uint64_t output = (*registers)[1];
                (*registers)[0] = 0;
                if (count < 0) {
                    SetError(dispatch_error, "glGenBuffers count is negative");
                    return false;
                }
                std::vector<GLuint> names(static_cast<std::size_t>(count), 0);
                if (!gate("glGenBuffers", dispatch_error)) return false;
                glGenBuffers(count, names.data());
                if (!CheckGlCall("glGenBuffers", dispatch_error)) return false;
                if (count != 0 && output != 0 &&
                    !memory_->Write(output, names.data(),
                                    names.size() * sizeof(GLuint))) {
                    SetError(dispatch_error,
                             "glGenBuffers output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGenVertexArrays",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLsizei count =
                    static_cast<GLsizei>((*registers)[0]);
                const std::uint64_t output = (*registers)[1];
                (*registers)[0] = 0;
                if (count < 0) {
                    SetError(dispatch_error,
                             "glGenVertexArrays count is negative");
                    return false;
                }
                std::vector<GLuint> names(static_cast<std::size_t>(count),
                                          0);
                if (!gate("glGenVertexArrays", dispatch_error))
                    return false;
                glGenVertexArrays(count, names.data());
                if (!CheckGlCall("glGenVertexArrays", dispatch_error))
                    return false;
                for (GLuint name : names)
                    if (name != 0) vertex_array_names_.insert(name);
                if (count != 0 && output != 0 &&
                    !memory_->Write(output, names.data(),
                                    names.size() * sizeof(GLuint))) {
                    SetError(dispatch_error,
                             "glGenVertexArrays output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glBindVertexArray",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint array = static_cast<GLuint>((*registers)[0]);
                (*registers)[0] = 0;
                if (array != 0 && !vertex_array_names_.contains(array)) {
                    SetError(dispatch_error,
                             "glBindVertexArray name is not owned");
                    return false;
                }
                if (!gate("glBindVertexArray", dispatch_error))
                    return false;
                glBindVertexArray(array);
                return CheckGlCall("glBindVertexArray", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDeleteVertexArrays",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLsizei count =
                    static_cast<GLsizei>((*registers)[0]);
                const std::uint64_t names = (*registers)[1];
                (*registers)[0] = 0;
                if (count < 0) {
                    SetError(dispatch_error,
                             "glDeleteVertexArrays count is negative");
                    return false;
                }
                std::vector<GLuint> owned(
                    static_cast<std::size_t>(count), 0);
                if (count != 0 && names != 0 &&
                    !memory_->Read(names, owned.data(),
                                   owned.size() * sizeof(GLuint))) {
                    SetError(dispatch_error,
                             "glDeleteVertexArrays names are unreadable");
                    return false;
                }
                for (GLuint name : owned) vertex_array_names_.erase(name);
                if (!gate("glDeleteVertexArrays", dispatch_error))
                    return false;
                glDeleteVertexArrays(count,
                                     count != 0 ? owned.data() : nullptr);
                return CheckGlCall("glDeleteVertexArrays", dispatch_error);
            },
            error))
        return false;
    // cocos2d-android exposes the vertex-array entry points as exported
    // function-pointer variables (weak data symbols the guest links
    // against): glGenVertexArraysOESEXT and siblings. Register each as a
    // data address whose pointee is this registry's callable trampoline
    // so Renderer::setupVBOAndVAO's indirect call lands in the handlers
    // above.
    // cocos2d's loader assigns the OESEXT pointer variables from
    // eglGetProcAddress; answer every naming variant it may query so
    // its own assignment keeps a working trampoline.
    for (const char* alias : {"glGenVertexArraysOES", "glGenVertexArraysEXT",
                              "glBindVertexArrayOES", "glBindVertexArrayEXT",
                              "glDeleteVertexArraysOES",
                              "glDeleteVertexArraysEXT"}) {
        const char* target =
            alias[2] == 'B'
                ? "glBindVertexArray"
                : (alias[2] == 'D' ? "glDeleteVertexArrays"
                                   : "glGenVertexArrays");
        if (!registry->Register(
                alias,
                [registry, target](Aarch64ImportRegistry::Registers*
                                       registers,
                                   std::string* dispatch_error) {
                    return registry->Dispatch(target, registers,
                                              dispatch_error);
                },
                error))
            return false;
    }
    vao_trampoline_names_ = {"glGenVertexArraysOESEXT",
                             "glBindVertexArrayOESEXT",
                             "glDeleteVertexArraysOESEXT"};
    vao_trampoline_targets_ = {"glGenVertexArrays", "glBindVertexArray",
                               "glDeleteVertexArrays"};
    for (std::size_t index = 0; index < vao_trampoline_names_.size();
         ++index) {
        heap_->SetAllocDebugTag("gles-vao");
        const std::uint64_t slot = heap_->Allocate(sizeof(std::uint64_t));
        heap_->SetAllocDebugTag(nullptr);
        if (slot == 0) {
            SetError(error,
                     "native GLES VAO function-pointer slot failed");
            return false;
        }
        vao_slots_.push_back(slot);
        if (!registry->RegisterDataAddress(vao_trampoline_names_[index],
                                           slot, error))
            return false;
    }
    if (!registry->Register(
            "glDeleteBuffers",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLsizei count =
                    static_cast<GLsizei>((*registers)[0]);
                const std::uint64_t names = (*registers)[1];
                (*registers)[0] = 0;
                if (count < 0) {
                    SetError(dispatch_error,
                             "glDeleteBuffers count is negative");
                    return false;
                }
                std::vector<GLuint> host(static_cast<std::size_t>(count), 0);
                if (count != 0 && names != 0 &&
                    !memory_->Read(names, host.data(),
                                   host.size() * sizeof(GLuint))) {
                    SetError(dispatch_error,
                             "glDeleteBuffers names are unreadable");
                    return false;
                }
                if (!gate("glDeleteBuffers", dispatch_error)) return false;
                glDeleteBuffers(count, host.data());
                return CheckGlCall("glDeleteBuffers", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glIsBuffer",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint buffer = static_cast<GLuint>((*registers)[0]);
                if (!gate("glIsBuffer", dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = glIsBuffer(buffer);
                return true;
            },
            error))
        return false;
    // Mapped buffer access needs the returned pointer to live in guest
    // address space; report no mapping so guests fall back to
    // glBufferSubData, which the compat runtime supports.
    if (!registry->Register(
            "glMapBufferOES",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                (*registers)[0] = 0;
                if (!initialized_ || shutdown_ || !is_current()) {
                    SetError(dispatch_error,
                             "glMapBufferOES requires the owned current "
                             "native GPU context");
                    return false;
                }
                // Query the buffer size, allocate guest-visible storage,
                // and return the guest address as the mapped pointer.
                GLint buffer_size = 0;
                glGetBufferParameteriv(target, GL_BUFFER_SIZE, &buffer_size);
                ClearGlErrors();
                if (buffer_size <= 0) {
                    SetError(dispatch_error,
                             "glMapBufferOES buffer size is invalid");
                    return false;
                }
                const auto mapped = mapped_buffer_storage_.find(static_cast<std::uint32_t>(target));
                if (mapped != mapped_buffer_storage_.end()) {
                    // Already mapped — return the existing address.
                    (*registers)[0] = mapped->second.first;
                    return true;
                }
                const std::uint64_t guest_address =
                    heap_->AllocateAligned(
                        static_cast<std::uint64_t>(buffer_size), 16);
                if (guest_address == 0) {
                    SetError(dispatch_error,
                             "glMapBufferOES storage allocation failed");
                    return false;
                }
                const std::vector<std::uint8_t> zeros(
                    static_cast<std::size_t>(buffer_size), 0);
                (void)memory_->Write(guest_address, zeros.data(),
                                     zeros.size());
                mapped_buffer_storage_[static_cast<std::uint32_t>(target)] = {guest_address,
                                                  static_cast<std::uint64_t>(
                                                      buffer_size)};
                (*registers)[0] = guest_address;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glUnmapBufferOES",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                (*registers)[0] = GL_FALSE;
                if (!initialized_ || shutdown_ || !is_current()) {
                    SetError(dispatch_error,
                             "glUnmapBufferOES requires the owned current "
                             "native GPU context");
                    return false;
                }
                const auto mapped = mapped_buffer_storage_.find(static_cast<std::uint32_t>(target));
                if (mapped == mapped_buffer_storage_.end()) {
                    (*registers)[0] = GL_FALSE;
                    return true;
                }
                // Flush the guest-written block into the host GL buffer.
                const std::uint64_t guest_address = mapped->second.first;
                const std::uint64_t block_size = mapped->second.second;
                std::vector<std::uint8_t> data(
                    static_cast<std::size_t>(block_size));
                if (memory_->Read(guest_address, data.data(), data.size())) {
                    glBufferSubData(target, 0,
                                    static_cast<GLsizeiptr>(block_size),
                                    data.data());
                    ClearGlErrors();
                }
                mapped_buffer_storage_.erase(mapped);
                (*registers)[0] = GL_TRUE;
                return true;
            },
            error))
        return false;
    // ------------------------------------------------------------------
    // GLES3 surface. Unity's GfxDeviceGLES3 resolves these through
    // dlsym/eglGetProcAddress; a missing name resolves NULL and disables the
    // whole upload/draw path that depends on it (buffer streaming, copy
    // staging, instanced/base-vertex draws, fences, samplers, uniform
    // blocks, 3D textures, indexed state, unsigned uniforms).
    // ------------------------------------------------------------------
    const auto buffer_binding_pname = [](std::uint32_t target) -> GLenum {
        switch (target) {
            case 0x8892: return 0x8894;  // ARRAY_BUFFER
            case 0x8893: return 0x8895;  // ELEMENT_ARRAY_BUFFER
            case 0x8f36: return 0x8f36;  // COPY_READ_BUFFER
            case 0x8f37: return 0x8f37;  // COPY_WRITE_BUFFER
            case 0x8a11: return 0x8a28;  // UNIFORM_BUFFER
            case 0x88eb: return 0x88ed;  // PIXEL_PACK_BUFFER
            case 0x88ec: return 0x88ef;  // PIXEL_UNPACK_BUFFER
            case 0x8c8e: return 0x8c8f;  // TRANSFORM_FEEDBACK_BUFFER
            case 0x92c0: return 0x92c1;  // ATOMIC_COUNTER_BUFFER
            case 0x90d2: return 0x90d3;  // SHADER_STORAGE_BUFFER
            case 0x8f3f: return 0x8f43;  // DRAW_INDIRECT_BUFFER
            case 0x90ee: return 0x90ef;  // DISPATCH_INDIRECT_BUFFER
            default: return 0;
        }
    };
    const auto read_stack_arg = [this](
                                    Aarch64ImportRegistry::Registers*
                                        registers,
                                    std::size_t index,
                                    std::uint64_t* out) {
        return memory_->Read(registers->stack_pointer + index * 8, out, 8);
    };

    if (!registry->Register(
            "glMapBufferRange",
            [this, gate, buffer_binding_pname](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const std::uint64_t offset = (*registers)[1];
                const std::uint64_t length = (*registers)[2];
                const std::uint32_t access =
                    static_cast<std::uint32_t>((*registers)[3]);
                (*registers)[0] = 0;
                if (!gate("glMapBufferRange", dispatch_error)) return false;
                const GLenum binding = buffer_binding_pname(target);
                if (binding == 0 || length == 0 ||
                    length > (std::numeric_limits<std::uint64_t>::max)() -
                                 offset) {
                    LatchInvalidValueError();
                    return true;
                }
                GLint bound_buffer = 0;
                glGetIntegerv(binding, &bound_buffer);
                GLint buffer_size = 0;
                if (bound_buffer != 0)
                    glGetBufferParameteriv(target, 0x8764 /*GL_BUFFER_SIZE*/,
                                           &buffer_size);
                (void)glGetError();
                if (bound_buffer == 0 || buffer_size <= 0 ||
                    offset + length >
                        static_cast<std::uint64_t>(buffer_size)) {
                    LatchInvalidOperationError();
                    return true;
                }
                const auto existing =
                    mapped_range_buffers_.find(
                        static_cast<std::uint32_t>(bound_buffer));
                if (existing != mapped_range_buffers_.end()) {
                    // Re-map of an already mapped buffer: return the live
                    // range (GL_MAP_INVALIDATE-style callers expect a fresh
                    // writable view; the staging block already exists).
                    (*registers)[0] = existing->second.guest_address;
                    return true;
                }
                const std::uint64_t guest_address = heap_->AllocateAligned(
                    length, 16);
                if (guest_address == 0) {
                    SetError(dispatch_error,
                             "glMapBufferRange guest staging allocation "
                             "failed");
                    return false;
                }
                if ((access & 0x1 /*GL_MAP_READ_BIT*/) != 0 &&
                    (access & (0x4 | 0x8)) == 0) {
                    // No glGetBufferSubData on this host ES3 surface —
                    // deliver zeros rather than stale garbage; Unity only
                    // maps for write.
                    static bool read_map_logged = false;
                    if (!read_map_logged) {
                        read_map_logged = true;
                        std::fprintf(
                            stderr,
                            "ISOMR_GLES_MAP_READ zero-fill buffer=%d "
                            "offset=%llu length=%llu\n",
                            bound_buffer,
                            static_cast<unsigned long long>(offset),
                            static_cast<unsigned long long>(length));
                        std::fflush(stderr);
                    }
                    const std::vector<std::uint8_t> zeros(
                        static_cast<std::size_t>(length), 0);
                    (void)memory_->Write(guest_address, zeros.data(),
                                         zeros.size());
                }
                MappedRange record;
                record.target = target;
                record.offset = offset;
                record.length = length;
                record.access = access;
                record.guest_address = guest_address;
                mapped_range_buffers_[static_cast<std::uint32_t>(
                    bound_buffer)] = record;
                (*registers)[0] = guest_address;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glUnmapBuffer",
            [this, gate, buffer_binding_pname](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                (*registers)[0] = GL_FALSE;
                if (!gate("glUnmapBuffer", dispatch_error)) return false;
                const GLenum binding = buffer_binding_pname(target);
                GLint bound_buffer = 0;
                if (binding != 0) {
                    glGetIntegerv(binding, &bound_buffer);
                    (void)glGetError();
                }
                auto mapped =
                    bound_buffer != 0
                        ? mapped_range_buffers_.find(
                              static_cast<std::uint32_t>(bound_buffer))
                        : mapped_range_buffers_.end();
                if (mapped == mapped_range_buffers_.end()) {
                    // The binding may have moved; find any record for this
                    // target (buffers are shared objects).
                    for (auto it = mapped_range_buffers_.begin();
                         it != mapped_range_buffers_.end(); ++it) {
                        if (it->second.target ==
                            static_cast<std::uint32_t>(target)) {
                            mapped = it;
                            break;
                        }
                    }
                }
                if (mapped == mapped_range_buffers_.end()) {
                    LatchInvalidOperationError();
                    return true;
                }
                const MappedRange record = mapped->second;
                mapped_range_buffers_.erase(mapped);
                (*registers)[0] = GL_TRUE;
                if ((record.access & 0x2 /*GL_MAP_WRITE_BIT*/) != 0 &&
                    record.length != 0) {
                    std::vector<std::uint8_t> data(
                        static_cast<std::size_t>(record.length));
                    if (!memory_->Read(record.guest_address, data.data(),
                                       data.size())) {
                        SetError(dispatch_error,
                                 "glUnmapBuffer mapped guest range is "
                                 "unreadable");
                        return false;
                    }
                    glBufferSubData(static_cast<GLenum>(record.target),
                                    static_cast<GLintptr>(record.offset),
                                    static_cast<GLsizeiptr>(record.length),
                                    data.data());
                    const GLenum flush_error = glGetError();
                    if (flush_error != GL_NO_ERROR) {
                        if (pending_guest_gl_error_ == 0)
                            pending_guest_gl_error_ = flush_error;
                        std::fprintf(
                            stderr,
                            "ISOMR_GLES_MAP_FLUSH err=0x%x buffer=%u "
                            "offset=%llu length=%llu\n",
                            flush_error, bound_buffer,
                            static_cast<unsigned long long>(record.offset),
                            static_cast<unsigned long long>(record.length));
                        std::fflush(stderr);
                    }
                }
                (void)heap_->Release(record.guest_address, nullptr);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glFlushMappedBufferRange",
            [this, gate, buffer_binding_pname](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const std::uint64_t offset = (*registers)[1];
                const std::uint64_t length = (*registers)[2];
                (*registers)[0] = 0;
                if (!gate("glFlushMappedBufferRange", dispatch_error))
                    return false;
                const GLenum binding = buffer_binding_pname(target);
                GLint bound_buffer = 0;
                if (binding != 0) {
                    glGetIntegerv(binding, &bound_buffer);
                    (void)glGetError();
                }
                const auto mapped =
                    bound_buffer != 0
                        ? mapped_range_buffers_.find(
                              static_cast<std::uint32_t>(bound_buffer))
                        : mapped_range_buffers_.end();
                if (mapped == mapped_range_buffers_.end() ||
                    offset + length > mapped->second.length) {
                    LatchInvalidValueError();
                    return true;
                }
                if (length != 0 &&
                    (mapped->second.access & 0x2 /*MAP_WRITE*/) != 0) {
                    std::vector<std::uint8_t> data(
                        static_cast<std::size_t>(length));
                    if (!memory_->Read(mapped->second.guest_address + offset,
                                       data.data(), data.size())) {
                        SetError(dispatch_error,
                                 "glFlushMappedBufferRange guest range is "
                                 "unreadable");
                        return false;
                    }
                    glBufferSubData(
                        static_cast<GLenum>(mapped->second.target),
                        static_cast<GLintptr>(mapped->second.offset + offset),
                        static_cast<GLsizeiptr>(length), data.data());
                    const GLenum flush_error = glGetError();
                    if (flush_error != GL_NO_ERROR &&
                        pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = flush_error;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetBufferPointerv",
            [this, gate, buffer_binding_pname](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                if (!gate("glGetBufferPointerv", dispatch_error)) return false;
                if (pname != 0x88bd /*GL_BUFFER_MAP_POINTER*/ ||
                    output == 0) {
                    LatchInvalidValueError();
                    return true;
                }
                const GLenum binding = buffer_binding_pname(target);
                GLint bound_buffer = 0;
                if (binding != 0) {
                    glGetIntegerv(binding, &bound_buffer);
                    (void)glGetError();
                }
                std::uint64_t pointer = 0;
                const auto mapped =
                    bound_buffer != 0
                        ? mapped_range_buffers_.find(
                              static_cast<std::uint32_t>(bound_buffer))
                        : mapped_range_buffers_.end();
                if (mapped != mapped_range_buffers_.end())
                    pointer = mapped->second.guest_address;
                if (!memory_->Write(output, &pointer, sizeof(pointer))) {
                    SetError(dispatch_error,
                             "glGetBufferPointerv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glBufferStorage",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const std::uint64_t size = (*registers)[1];
                const std::uint64_t data = (*registers)[2];
                const GLbitfield flags =
                    static_cast<GLbitfield>((*registers)[3]);
                (*registers)[0] = 0;
                if (!gate("glBufferStorage", dispatch_error)) return false;
                if (size > 256U * 1024U * 1024U) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<std::uint8_t> payload;
                const void* host_data = nullptr;
                if (data != 0 && size != 0) {
                    payload.resize(static_cast<std::size_t>(size));
                    if (!memory_->Read(data, payload.data(),
                                       payload.size())) {
                        SetError(dispatch_error,
                                 "glBufferStorage guest data is unreadable");
                        return false;
                    }
                    host_data = payload.data();
                }
                glBufferStorageEXT(target,
                                   static_cast<GLsizeiptr>(size), host_data,
                                   flags);
                const GLenum storage_error = glGetError();
                if (storage_error == GL_NO_ERROR) return true;
                // EXT_buffer_storage unsupported on this backend: fall back
                // to mutable glBufferData with an equivalent usage.
                if (storage_error != 0x0500 /*GL_INVALID_ENUM*/) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = storage_error;
                    return true;
                }
                glBufferData(target, static_cast<GLsizeiptr>(size), host_data,
                             0x88e4 /*GL_STATIC_DRAW*/);
                return CheckGlCall("glBufferStorage fallback", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glCopyBufferSubData",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum read_target =
                    static_cast<GLenum>((*registers)[0]);
                const GLenum write_target =
                    static_cast<GLenum>((*registers)[1]);
                const GLintptr read_offset =
                    static_cast<GLintptr>((*registers)[2]);
                const GLintptr write_offset =
                    static_cast<GLintptr>((*registers)[3]);
                const GLsizeiptr size =
                    static_cast<GLsizeiptr>((*registers)[4]);
                (*registers)[0] = 0;
                if (!gate("glCopyBufferSubData", dispatch_error)) return false;
                glCopyBufferSubData(read_target, write_target, read_offset,
                                    write_offset, size);
                const GLenum copy_error = glGetError();
                if (copy_error != GL_NO_ERROR &&
                    pending_guest_gl_error_ == 0)
                    pending_guest_gl_error_ = copy_error;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glBindBufferBase",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glBindBufferBase", dispatch_error)) return false;
                glBindBufferBase(static_cast<GLenum>((*registers)[0]),
                                 static_cast<GLuint>((*registers)[1]),
                                 static_cast<GLuint>((*registers)[2]));
                (*registers)[0] = 0;
                return CheckGlCall("glBindBufferBase", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glBindBufferRange",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glBindBufferRange", dispatch_error)) return false;
                glBindBufferRange(static_cast<GLenum>((*registers)[0]),
                                  static_cast<GLuint>((*registers)[1]),
                                  static_cast<GLuint>((*registers)[2]),
                                  static_cast<GLintptr>((*registers)[3]),
                                  static_cast<GLsizeiptr>((*registers)[4]));
                (*registers)[0] = 0;
                return CheckGlCall("glBindBufferRange", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetIntegeri_v",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum pname = static_cast<GLenum>((*registers)[0]);
                const GLuint index = static_cast<GLuint>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                if (!gate("glGetIntegeri_v", dispatch_error)) return false;
                GLint value = 0;
                glGetIntegeri_v(pname, index, &value);
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (output != 0 &&
                    !memory_->Write(output, &value, sizeof(value))) {
                    SetError(dispatch_error,
                             "glGetIntegeri_v output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetInteger64i_v",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum pname = static_cast<GLenum>((*registers)[0]);
                const GLuint index = static_cast<GLuint>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                if (!gate("glGetInteger64i_v", dispatch_error)) return false;
                GLint64 value = 0;
                glGetInteger64i_v(pname, index, &value);
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (output != 0 &&
                    !memory_->Write(output, &value, sizeof(value))) {
                    SetError(dispatch_error,
                             "glGetInteger64i_v output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetInteger64v",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum pname = static_cast<GLenum>((*registers)[0]);
                const std::uint64_t output = (*registers)[1];
                (*registers)[0] = 0;
                if (!gate("glGetInteger64v", dispatch_error)) return false;
                GLint64 value = 0;
                glGetInteger64v(pname, &value);
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (output != 0 &&
                    !memory_->Write(output, &value, sizeof(value))) {
                    SetError(dispatch_error,
                             "glGetInteger64v output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    // ES3 draw variants.
    if (!registry->Register(
            "glDrawArraysInstanced",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum mode = static_cast<GLenum>((*registers)[0]);
                const GLint first = static_cast<GLint>((*registers)[1]);
                const GLsizei count = static_cast<GLsizei>((*registers)[2]);
                const GLsizei instances =
                    static_cast<GLsizei>((*registers)[3]);
                (*registers)[0] = 0;
                if (!gate("glDrawArraysInstanced", dispatch_error))
                    return false;
                {
                    GLint host_program = 0;
                    glGetIntegerv(GL_CURRENT_PROGRAM, &host_program);
                    (void)glGetError();
                    if (host_program != 0 &&
                        program_names_.contains(
                            static_cast<std::uint32_t>(host_program)))
                        bound_program_ =
                            static_cast<std::uint32_t>(host_program);
                }
                if (bound_array_buffer_ == 0 && count > 0) {
                    if (first < 0 || bound_program_ == 0 ||
                        !program_names_.contains(bound_program_)) {
                        LatchInvalidOperationError();
                        ++draw_call_count_;
                        return true;
                    }
                    const std::uint64_t maximum_vertex =
                        static_cast<std::uint64_t>(first) +
                        static_cast<std::uint64_t>(count) - 1;
                    if (maximum_vertex >
                        std::numeric_limits<std::uint32_t>::max()) {
                        LatchInvalidValueError();
                        ++draw_call_count_;
                        return true;
                    }
                    if (!StageClientVertexAttributes(
                            static_cast<std::uint32_t>(maximum_vertex), 0,
                            dispatch_error))
                        return false;
                }
                EnsureDrawableViewport();
                glDrawArraysInstanced(mode, first, count, instances);
                LatchDrawGlError("glDrawArraysInstanced");
                ++draw_call_count_;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glDrawElementsInstanced",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mode =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint32_t type =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::uint64_t indices = (*registers)[3];
                const std::int32_t instances =
                    static_cast<std::int32_t>((*registers)[4]);
                (*registers)[0] = 0;
                return DrawIndexedEs3(mode, count, type, indices, instances,
                                      false, 0, false, 0, 0,
                                      "glDrawElementsInstanced",
                                      dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDrawElementsBaseVertex",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mode =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint32_t type =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::uint64_t indices = (*registers)[3];
                const std::int32_t base_vertex =
                    static_cast<std::int32_t>((*registers)[4]);
                (*registers)[0] = 0;
                return DrawIndexedEs3(mode, count, type, indices, 0, true,
                                      base_vertex, false, 0, 0,
                                      "glDrawElementsBaseVertex",
                                      dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDrawElementsBaseVertexOES",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mode =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint32_t type =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::uint64_t indices = (*registers)[3];
                const std::int32_t base_vertex =
                    static_cast<std::int32_t>((*registers)[4]);
                (*registers)[0] = 0;
                return DrawIndexedEs3(mode, count, type, indices, 0, true,
                                      base_vertex, false, 0, 0,
                                      "glDrawElementsBaseVertexOES",
                                      dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDrawRangeElements",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mode =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t range_start =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::uint32_t range_end =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[3]);
                const std::uint32_t type =
                    static_cast<std::uint32_t>((*registers)[4]);
                const std::uint64_t indices = (*registers)[5];
                (*registers)[0] = 0;
                return DrawIndexedEs3(mode, count, type, indices, 0, false, 0,
                                      true, range_start, range_end,
                                      "glDrawRangeElements", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDrawRangeElementsBaseVertex",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mode =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::uint32_t range_start =
                    static_cast<std::uint32_t>((*registers)[1]);
                const std::uint32_t range_end =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[3]);
                const std::uint32_t type =
                    static_cast<std::uint32_t>((*registers)[4]);
                const std::uint64_t indices = (*registers)[5];
                const std::int32_t base_vertex =
                    static_cast<std::int32_t>((*registers)[6]);
                (*registers)[0] = 0;
                return DrawIndexedEs3(mode, count, type, indices, 0, true,
                                      base_vertex, true, range_start,
                                      range_end,
                                      "glDrawRangeElementsBaseVertex",
                                      dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDrawElementsInstancedBaseVertex",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mode =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint32_t type =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::uint64_t indices = (*registers)[3];
                const std::int32_t instances =
                    static_cast<std::int32_t>((*registers)[4]);
                const std::int32_t base_vertex =
                    static_cast<std::int32_t>((*registers)[5]);
                (*registers)[0] = 0;
                return DrawIndexedEs3(mode, count, type, indices, instances,
                                      true, base_vertex, false, 0, 0,
                                      "glDrawElementsInstancedBaseVertex",
                                      dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDrawElementsInstancedBaseVertexOES",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::uint32_t mode =
                    static_cast<std::uint32_t>((*registers)[0]);
                const std::int32_t count =
                    static_cast<std::int32_t>((*registers)[1]);
                const std::uint32_t type =
                    static_cast<std::uint32_t>((*registers)[2]);
                const std::uint64_t indices = (*registers)[3];
                const std::int32_t instances =
                    static_cast<std::int32_t>((*registers)[4]);
                const std::int32_t base_vertex =
                    static_cast<std::int32_t>((*registers)[5]);
                (*registers)[0] = 0;
                return DrawIndexedEs3(mode, count, type, indices, instances,
                                      true, base_vertex, false, 0, 0,
                                      "glDrawElementsInstancedBaseVertexOES",
                                      dispatch_error);
            },
            error))
        return false;
    // Integer attribute pointers feed the same per-attribute state table as
    // glVertexAttribPointer so client-array draws stage them identically;
    // when an array buffer is bound the offset forwards to the host.
    if (!registry->Register(
            "glVertexAttribIPointer",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint index = static_cast<GLuint>((*registers)[0]);
                const GLint size = static_cast<GLint>((*registers)[1]);
                const GLenum type = static_cast<GLenum>((*registers)[2]);
                const GLsizei stride = static_cast<GLsizei>((*registers)[3]);
                const std::uint64_t pointer = (*registers)[4];
                (*registers)[0] = 0;
                if (!gate("glVertexAttribIPointer", dispatch_error))
                    return false;
                GLint array_buffer = 0;
                glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
                (void)glGetError();
                VertexAttributePointerState state;
                state.size = size;
                state.type = type;
                state.normalized = false;
                state.stride = stride;
                state.pointer = pointer;
                state.buffer_offset = array_buffer != 0;
                state.integer_attrib = true;
                vertex_attribute_pointers_[index] = state;
                glVertexAttribIPointer(
                    index, size, type, stride,
                    reinterpret_cast<const GLvoid*>(
                        static_cast<std::uintptr_t>(pointer)));
                return CheckGlCall("glVertexAttribIPointer", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttribDivisor",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glVertexAttribDivisor", dispatch_error))
                    return false;
                glVertexAttribDivisor(static_cast<GLuint>((*registers)[0]),
                                      static_cast<GLuint>((*registers)[1]));
                (*registers)[0] = 0;
                return CheckGlCall("glVertexAttribDivisor", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttribI4i",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glVertexAttribI4i", dispatch_error)) return false;
                glVertexAttribI4i(static_cast<GLuint>((*registers)[0]),
                                  static_cast<GLint>((*registers)[1]),
                                  static_cast<GLint>((*registers)[2]),
                                  static_cast<GLint>((*registers)[3]),
                                  static_cast<GLint>((*registers)[4]));
                (*registers)[0] = 0;
                return CheckGlCall("glVertexAttribI4i", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttribI4ui",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glVertexAttribI4ui", dispatch_error)) return false;
                glVertexAttribI4ui(static_cast<GLuint>((*registers)[0]),
                                   static_cast<GLuint>((*registers)[1]),
                                   static_cast<GLuint>((*registers)[2]),
                                   static_cast<GLuint>((*registers)[3]),
                                   static_cast<GLuint>((*registers)[4]));
                (*registers)[0] = 0;
                return CheckGlCall("glVertexAttribI4ui", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttribI4iv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint index = static_cast<GLuint>((*registers)[0]);
                const std::uint64_t values = (*registers)[1];
                (*registers)[0] = 0;
                GLint host_values[4] = {0, 0, 0, 0};
                if (!memory_->Read(values, host_values,
                                   sizeof(host_values))) {
                    SetError(dispatch_error,
                             "glVertexAttribI4iv values are unreadable");
                    return false;
                }
                if (!gate("glVertexAttribI4iv", dispatch_error)) return false;
                glVertexAttribI4iv(index, host_values);
                return CheckGlCall("glVertexAttribI4iv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttribI4uiv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint index = static_cast<GLuint>((*registers)[0]);
                const std::uint64_t values = (*registers)[1];
                (*registers)[0] = 0;
                GLuint host_values[4] = {0, 0, 0, 0};
                if (!memory_->Read(values, host_values,
                                   sizeof(host_values))) {
                    SetError(dispatch_error,
                             "glVertexAttribI4uiv values are unreadable");
                    return false;
                }
                if (!gate("glVertexAttribI4uiv", dispatch_error))
                    return false;
                glVertexAttribI4uiv(index, host_values);
                return CheckGlCall("glVertexAttribI4uiv", dispatch_error);
            },
            error))
        return false;
    // Framebuffer completeness paths.
    if (!registry->Register(
            "glBlitFramebuffer",
            [this, gate, read_stack_arg](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
                (*registers)[0] = 0;
                std::uint64_t mask = 0;
                std::uint64_t filter = 0;
                if (!read_stack_arg(registers, 0, &mask) ||
                    !read_stack_arg(registers, 1, &filter)) {
                    SetError(dispatch_error,
                             "glBlitFramebuffer stack arguments are "
                             "unreadable");
                    return false;
                }
                if (!gate("glBlitFramebuffer", dispatch_error)) return false;
                glBlitFramebuffer(static_cast<GLint>((*registers)[0]),
                                  static_cast<GLint>((*registers)[1]),
                                  static_cast<GLint>((*registers)[2]),
                                  static_cast<GLint>((*registers)[3]),
                                  static_cast<GLint>((*registers)[4]),
                                  static_cast<GLint>((*registers)[5]),
                                  static_cast<GLint>((*registers)[6]),
                                  static_cast<GLint>((*registers)[7]),
                                  static_cast<GLbitfield>(mask),
                                  static_cast<GLenum>(filter));
                const GLenum blit_error = glGetError();
                if (blit_error != GL_NO_ERROR &&
                    pending_guest_gl_error_ == 0)
                    pending_guest_gl_error_ = blit_error;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glReadBuffer",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glReadBuffer", dispatch_error)) return false;
                glReadBuffer(static_cast<GLenum>((*registers)[0]));
                (*registers)[0] = 0;
                return CheckGlCall("glReadBuffer", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDrawBuffers",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLsizei count = static_cast<GLsizei>((*registers)[0]);
                const std::uint64_t buffers = (*registers)[1];
                (*registers)[0] = 0;
                if (count < 0 || count > 16) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<GLenum> host_buffers(static_cast<size_t>(count));
                if (count != 0 &&
                    !memory_->Read(buffers, host_buffers.data(),
                                   host_buffers.size() * sizeof(GLenum))) {
                    SetError(dispatch_error,
                             "glDrawBuffers attachment list is unreadable");
                    return false;
                }
                if (!gate("glDrawBuffers", dispatch_error)) return false;
                glDrawBuffers(count, host_buffers.data());
                return CheckGlCall("glDrawBuffers", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glInvalidateFramebuffer",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLsizei count = static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t attachments = (*registers)[2];
                (*registers)[0] = 0;
                if (count < 0 || count > 16) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<GLenum> host_attachments(
                    static_cast<size_t>(count));
                if (count != 0 &&
                    !memory_->Read(attachments, host_attachments.data(),
                                   host_attachments.size() *
                                       sizeof(GLenum))) {
                    SetError(dispatch_error,
                             "glInvalidateFramebuffer list is unreadable");
                    return false;
                }
                if (!gate("glInvalidateFramebuffer", dispatch_error))
                    return false;
                glInvalidateFramebuffer(target, count,
                                        host_attachments.data());
                const GLenum inv_error = glGetError();
                if (inv_error != GL_NO_ERROR &&
                    pending_guest_gl_error_ == 0)
                    pending_guest_gl_error_ = inv_error;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glInvalidateSubFramebuffer",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLsizei count = static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t attachments = (*registers)[2];
                const GLint x = static_cast<GLint>((*registers)[3]);
                const GLint y = static_cast<GLint>((*registers)[4]);
                const GLsizei width = static_cast<GLsizei>((*registers)[5]);
                const GLsizei height = static_cast<GLsizei>((*registers)[6]);
                (*registers)[0] = 0;
                if (count < 0 || count > 16) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<GLenum> host_attachments(
                    static_cast<size_t>(count));
                if (count != 0 &&
                    !memory_->Read(attachments, host_attachments.data(),
                                   host_attachments.size() *
                                       sizeof(GLenum))) {
                    SetError(dispatch_error,
                             "glInvalidateSubFramebuffer list is unreadable");
                    return false;
                }
                if (!gate("glInvalidateSubFramebuffer", dispatch_error))
                    return false;
                glInvalidateSubFramebuffer(target, count,
                                           host_attachments.data(), x, y,
                                           width, height);
                const GLenum inv_error = glGetError();
                if (inv_error != GL_NO_ERROR &&
                    pending_guest_gl_error_ == 0)
                    pending_guest_gl_error_ = inv_error;
                return true;
            },
            error))
        return false;
    // 3D / array texture uploads. Pixel data reads honor
    // unpack_alignment_ like the 2D path; size = aligned(w*bpp) * h * d.
    const auto texel_upload_size =
        [this](std::int32_t width, std::int32_t height, std::int32_t depth,
               std::uint32_t format, std::uint32_t type,
               std::size_t* out) -> bool {
            constexpr std::size_t kMaximumUpload = 64U * 1024U * 1024U;
            if (width <= 0 || height <= 0 || depth <= 0) return false;
            const std::size_t components = GlPixelComponentCount(format);
            const std::size_t component = GlPixelComponentSize(type);
            const bool packed =
                (type == 0x8363 && format == 0x1907) ||
                ((type == 0x8033 || type == 0x8034) && format == 0x1908);
            if (!packed && (components == 0 || component == 0)) return false;
            const std::size_t bpp = packed ? 2 : components * component;
            const std::size_t tight_row =
                static_cast<std::size_t>(width) * bpp;
            const std::size_t alignment =
                static_cast<std::size_t>(unpack_alignment_);
            const std::size_t row_stride =
                (tight_row + alignment - 1) & ~(alignment - 1);
            const std::size_t slice =
                row_stride * (static_cast<std::size_t>(height) - 1) +
                tight_row;
            const std::size_t total =
                slice * (static_cast<std::size_t>(depth) - 1) + slice;
            if (total == 0 || total > kMaximumUpload) return false;
            *out = total;
            return true;
        };
    if (!registry->Register(
            "glTexImage3D",
            [this, gate, read_stack_arg, texel_upload_size](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLint level = static_cast<GLint>((*registers)[1]);
                const GLint internal_format =
                    static_cast<GLint>((*registers)[2]);
                const GLsizei width = static_cast<GLsizei>((*registers)[3]);
                const GLsizei height = static_cast<GLsizei>((*registers)[4]);
                const GLsizei depth = static_cast<GLsizei>((*registers)[5]);
                const GLint border = static_cast<GLint>((*registers)[6]);
                const GLenum format = static_cast<GLenum>((*registers)[7]);
                std::uint64_t type = 0;
                std::uint64_t pixels = 0;
                (*registers)[0] = 0;
                if (!read_stack_arg(registers, 0, &type) ||
                    !read_stack_arg(registers, 1, &pixels)) {
                    SetError(dispatch_error,
                             "glTexImage3D stack arguments are unreadable");
                    return false;
                }
                if (!gate("glTexImage3D", dispatch_error)) return false;
                std::vector<std::uint8_t> payload;
                const void* host_pixels = nullptr;
                if (pixels != 0) {
                    std::size_t byte_count = 0;
                    if (!texel_upload_size(width, height, depth, format,
                                           static_cast<std::uint32_t>(type),
                                           &byte_count)) {
                        LatchInvalidValueError();
                        return true;
                    }
                    payload.resize(byte_count);
                    if (!memory_->Read(pixels, payload.data(),
                                       payload.size())) {
                        SetError(dispatch_error,
                                 "glTexImage3D guest pixels are unreadable");
                        return false;
                    }
                    host_pixels = payload.data();
                }
                glTexImage3D(target, level, internal_format, width, height,
                             depth, border, format,
                             static_cast<GLenum>(type), host_pixels);
                return CheckGlCall("glTexImage3D", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glTexSubImage3D",
            [this, gate, read_stack_arg, texel_upload_size](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLint level = static_cast<GLint>((*registers)[1]);
                const GLint x = static_cast<GLint>((*registers)[2]);
                const GLint y = static_cast<GLint>((*registers)[3]);
                const GLint z = static_cast<GLint>((*registers)[4]);
                const GLsizei width = static_cast<GLsizei>((*registers)[5]);
                const GLsizei height = static_cast<GLsizei>((*registers)[6]);
                const GLsizei depth = static_cast<GLsizei>((*registers)[7]);
                std::uint64_t format = 0;
                std::uint64_t type = 0;
                std::uint64_t pixels = 0;
                (*registers)[0] = 0;
                if (!read_stack_arg(registers, 0, &format) ||
                    !read_stack_arg(registers, 1, &type) ||
                    !read_stack_arg(registers, 2, &pixels)) {
                    SetError(dispatch_error,
                             "glTexSubImage3D stack arguments are "
                             "unreadable");
                    return false;
                }
                if (!gate("glTexSubImage3D", dispatch_error)) return false;
                std::vector<std::uint8_t> payload;
                const void* host_pixels = nullptr;
                if (pixels != 0) {
                    std::size_t byte_count = 0;
                    if (!texel_upload_size(width, height, depth,
                                           static_cast<std::uint32_t>(format),
                                           static_cast<std::uint32_t>(type),
                                           &byte_count)) {
                        LatchInvalidValueError();
                        return true;
                    }
                    payload.resize(byte_count);
                    if (!memory_->Read(pixels, payload.data(),
                                       payload.size())) {
                        SetError(
                            dispatch_error,
                            "glTexSubImage3D guest pixels are unreadable");
                        return false;
                    }
                    host_pixels = payload.data();
                }
                glTexSubImage3D(target, level, x, y, z, width, height, depth,
                                static_cast<GLenum>(format),
                                static_cast<GLenum>(type), host_pixels);
                return CheckGlCall("glTexSubImage3D", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glCopyTexSubImage3D",
            [this, gate, read_stack_arg](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
                std::uint64_t height = 0;
                (*registers)[0] = 0;
                if (!read_stack_arg(registers, 0, &height)) {
                    SetError(dispatch_error,
                             "glCopyTexSubImage3D stack argument is "
                             "unreadable");
                    return false;
                }
                if (!gate("glCopyTexSubImage3D", dispatch_error))
                    return false;
                glCopyTexSubImage3D(static_cast<GLenum>((*registers)[0]),
                                    static_cast<GLint>((*registers)[1]),
                                    static_cast<GLint>((*registers)[2]),
                                    static_cast<GLint>((*registers)[3]),
                                    static_cast<GLint>((*registers)[4]),
                                    static_cast<GLint>((*registers)[5]),
                                    static_cast<GLint>((*registers)[6]),
                                    static_cast<GLsizei>((*registers)[7]),
                                    static_cast<GLsizei>(height));
                return CheckGlCall("glCopyTexSubImage3D", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glCompressedTexImage3D",
            [this, gate, read_stack_arg](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
                std::uint64_t data = 0;
                const GLsizei image_size =
                    static_cast<GLsizei>((*registers)[7]);
                (*registers)[0] = 0;
                if (!read_stack_arg(registers, 0, &data)) {
                    SetError(dispatch_error,
                             "glCompressedTexImage3D stack argument is "
                             "unreadable");
                    return false;
                }
                if (image_size < 0 ||
                    image_size > 64 * 1024 * 1024) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<std::uint8_t> payload(
                    static_cast<std::size_t>(image_size));
                if (image_size != 0 &&
                    !memory_->Read(data, payload.data(), payload.size())) {
                    SetError(dispatch_error,
                             "glCompressedTexImage3D data is unreadable");
                    return false;
                }
                if (!gate("glCompressedTexImage3D", dispatch_error))
                    return false;
                glCompressedTexImage3D(static_cast<GLenum>((*registers)[0]),
                                       static_cast<GLint>((*registers)[1]),
                                       static_cast<GLenum>((*registers)[2]),
                                       static_cast<GLsizei>((*registers)[3]),
                                       static_cast<GLsizei>((*registers)[4]),
                                       static_cast<GLsizei>((*registers)[5]),
                                       static_cast<GLint>((*registers)[6]),
                                       image_size,
                                       payload.empty() ? nullptr
                                                       : payload.data());
                return CheckGlCall("glCompressedTexImage3D", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glCompressedTexSubImage3D",
            [this, gate, read_stack_arg](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
                std::uint64_t format = 0;
                std::uint64_t image_size = 0;
                std::uint64_t data = 0;
                (*registers)[0] = 0;
                if (!read_stack_arg(registers, 0, &format) ||
                    !read_stack_arg(registers, 1, &image_size) ||
                    !read_stack_arg(registers, 2, &data)) {
                    SetError(dispatch_error,
                             "glCompressedTexSubImage3D stack arguments are "
                             "unreadable");
                    return false;
                }
                if (image_size > 64 * 1024 * 1024) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<std::uint8_t> payload(
                    static_cast<std::size_t>(image_size));
                if (image_size != 0 &&
                    !memory_->Read(data, payload.data(), payload.size())) {
                    SetError(dispatch_error,
                             "glCompressedTexSubImage3D data is unreadable");
                    return false;
                }
                if (!gate("glCompressedTexSubImage3D", dispatch_error))
                    return false;
                glCompressedTexSubImage3D(
                    static_cast<GLenum>((*registers)[0]),
                    static_cast<GLint>((*registers)[1]),
                    static_cast<GLint>((*registers)[2]),
                    static_cast<GLint>((*registers)[3]),
                    static_cast<GLint>((*registers)[4]),
                    static_cast<GLsizei>((*registers)[5]),
                    static_cast<GLsizei>((*registers)[6]),
                    static_cast<GLsizei>((*registers)[7]),
                    static_cast<GLenum>(format),
                    static_cast<GLsizei>(image_size),
                    payload.empty() ? nullptr : payload.data());
                return CheckGlCall("glCompressedTexSubImage3D",
                                   dispatch_error);
            },
            error))
        return false;
    // Sync objects: the host GLsync handle bits round-trip as the guest
    // handle; created/deleted objects are tracked for diagnostics.
    if (!registry->Register(
            "glFenceSync",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum condition =
                    static_cast<GLenum>((*registers)[0]);
                const GLbitfield flags =
                    static_cast<GLbitfield>((*registers)[1]);
                (*registers)[0] = 0;
                if (!gate("glFenceSync", dispatch_error)) return false;
                GLsync sync = glFenceSync(condition, flags);
                const GLenum fence_error = glGetError();
                if (fence_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = fence_error;
                    return true;
                }
                const std::uint64_t handle =
                    reinterpret_cast<std::uint64_t>(sync);
                sync_objects_.insert(handle);
                (*registers)[0] = handle;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glClientWaitSync",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLsync sync =
                    reinterpret_cast<GLsync>(
                        static_cast<std::uintptr_t>((*registers)[0]));
                const GLbitfield flags =
                    static_cast<GLbitfield>((*registers)[1]);
                const GLuint64 timeout =
                    static_cast<GLuint64>((*registers)[2]);
                (*registers)[0] = 0;
                if (!gate("glClientWaitSync", dispatch_error)) return false;
                if (sync == nullptr) {
                    LatchInvalidValueError();
                    (*registers)[0] = 0x911d /*GL_WAIT_FAILED*/;
                    return true;
                }
                const GLenum status = glClientWaitSync(sync, flags, timeout);
                const GLenum wait_error = glGetError();
                if (wait_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = wait_error;
                    (*registers)[0] = 0x911d /*GL_WAIT_FAILED*/;
                    return true;
                }
                (*registers)[0] = status;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glWaitSync",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLsync sync =
                    reinterpret_cast<GLsync>(
                        static_cast<std::uintptr_t>((*registers)[0]));
                const GLbitfield flags =
                    static_cast<GLbitfield>((*registers)[1]);
                const GLuint64 timeout =
                    static_cast<GLuint64>((*registers)[2]);
                (*registers)[0] = 0;
                if (!gate("glWaitSync", dispatch_error)) return false;
                if (sync == nullptr) {
                    LatchInvalidValueError();
                    return true;
                }
                glWaitSync(sync, flags, timeout);
                const GLenum wait_error = glGetError();
                if (wait_error != GL_NO_ERROR &&
                    pending_guest_gl_error_ == 0)
                    pending_guest_gl_error_ = wait_error;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glDeleteSync",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const std::uint64_t handle = (*registers)[0];
                (*registers)[0] = 0;
                if (!gate("glDeleteSync", dispatch_error)) return false;
                if (handle == 0) return true;
                sync_objects_.erase(handle);
                glDeleteSync(reinterpret_cast<GLsync>(
                    static_cast<std::uintptr_t>(handle)));
                (void)glGetError();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glIsSync",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const std::uint64_t handle = (*registers)[0];
                (*registers)[0] = GL_FALSE;
                if (!gate("glIsSync", dispatch_error)) return false;
                if (handle == 0) return true;
                (*registers)[0] = glIsSync(reinterpret_cast<GLsync>(
                    static_cast<std::uintptr_t>(handle)));
                (void)glGetError();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetSynciv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const std::uint64_t handle = (*registers)[0];
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const GLsizei count = static_cast<GLsizei>((*registers)[2]);
                const std::uint64_t length_out = (*registers)[3];
                const std::uint64_t values_out = (*registers)[4];
                (*registers)[0] = 0;
                if (!gate("glGetSynciv", dispatch_error)) return false;
                if (handle == 0 || count < 0 || count > 64) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<GLint> values(static_cast<std::size_t>(
                                              count == 0 ? 1 : count),
                                          0);
                GLsizei written = 0;
                glGetSynciv(
                    reinterpret_cast<GLsync>(
                        static_cast<std::uintptr_t>(handle)),
                    pname, count, &written, values.data());
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (length_out != 0 &&
                    !memory_->Write(length_out, &written, sizeof(written))) {
                    SetError(dispatch_error,
                             "glGetSynciv length output is unwritable");
                    return false;
                }
                if (values_out != 0 && written > 0 &&
                    !memory_->Write(values_out, values.data(),
                                    static_cast<std::size_t>(written) *
                                        sizeof(GLint))) {
                    SetError(dispatch_error,
                             "glGetSynciv values output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    // Uniform-block plumbing for the ES3 constant-buffer path.
    if (!registry->Register(
            "glGetUniformBlockIndex",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const std::uint64_t name = (*registers)[1];
                (*registers)[0] = 0xffffffffU;
                if (!gate("glGetUniformBlockIndex", dispatch_error))
                    return false;
                std::uint64_t name_length = 0;
                if (!memory_->BoundedStringLength(name, 4096, &name_length,
                                                  dispatch_error))
                    return false;
                std::string host_name(
                    static_cast<std::size_t>(name_length), '\0');
                if (name_length != 0 &&
                    !memory_->Read(name, host_name.data(),
                                   host_name.size())) {
                    SetError(dispatch_error,
                             "glGetUniformBlockIndex name is unreadable");
                    return false;
                }
                (*registers)[0] =
                    glGetUniformBlockIndex(program, host_name.c_str());
                (void)glGetError();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glUniformBlockBinding",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glUniformBlockBinding", dispatch_error))
                    return false;
                glUniformBlockBinding(static_cast<GLuint>((*registers)[0]),
                                      static_cast<GLuint>((*registers)[1]),
                                      static_cast<GLuint>((*registers)[2]));
                (*registers)[0] = 0;
                return CheckGlCall("glUniformBlockBinding", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetActiveUniformBlockiv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const GLuint block_index =
                    static_cast<GLuint>((*registers)[1]);
                const GLenum pname = static_cast<GLenum>((*registers)[2]);
                const std::uint64_t output = (*registers)[3];
                (*registers)[0] = 0;
                if (!gate("glGetActiveUniformBlockiv", dispatch_error))
                    return false;
                if (output == 0) {
                    LatchInvalidValueError();
                    return true;
                }
                // GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES returns one index
                // per active uniform; every other pname returns a scalar.
                std::vector<GLint> values(64, 0);
                if (pname != 0x8a43 /*GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES*/)
                    values.resize(4);
                glGetActiveUniformBlockiv(program, block_index, pname,
                                          values.data());
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (!memory_->Write(output, values.data(),
                                    values.size() * sizeof(GLint))) {
                    SetError(dispatch_error,
                             "glGetActiveUniformBlockiv output is "
                             "unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetActiveUniformBlockName",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const GLuint block_index =
                    static_cast<GLuint>((*registers)[1]);
                const GLsizei buf_size =
                    static_cast<GLsizei>((*registers)[2]);
                const std::uint64_t length_out = (*registers)[3];
                const std::uint64_t name_out = (*registers)[4];
                (*registers)[0] = 0;
                if (!gate("glGetActiveUniformBlockName", dispatch_error))
                    return false;
                if (buf_size < 0 || buf_size > 4096 || name_out == 0) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<GLchar> host_name(static_cast<std::size_t>(
                                                  buf_size == 0 ? 1
                                                                : buf_size),
                                              0);
                GLsizei written = 0;
                glGetActiveUniformBlockName(program, block_index, buf_size,
                                            &written, host_name.data());
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (buf_size != 0 &&
                    !memory_->Write(name_out, host_name.data(),
                                    host_name.size())) {
                    SetError(dispatch_error,
                             "glGetActiveUniformBlockName output is "
                             "unwritable");
                    return false;
                }
                if (length_out != 0 &&
                    !memory_->Write(length_out, &written, sizeof(written))) {
                    SetError(dispatch_error,
                             "glGetActiveUniformBlockName length output is "
                             "unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetUniformIndices",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const GLsizei count = static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t names = (*registers)[2];
                const std::uint64_t indices_out = (*registers)[3];
                (*registers)[0] = 0;
                if (!gate("glGetUniformIndices", dispatch_error)) return false;
                if (count < 0 || count > 256 ||
                    (count != 0 && (names == 0 || indices_out == 0))) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<std::uint64_t> name_addresses(
                    static_cast<std::size_t>(count));
                if (count != 0 &&
                    !memory_->Read(names, name_addresses.data(),
                                   name_addresses.size() * 8)) {
                    SetError(dispatch_error,
                             "glGetUniformIndices name list is unreadable");
                    return false;
                }
                std::vector<std::string> strings;
                std::vector<const GLchar*> pointers;
                strings.reserve(static_cast<std::size_t>(count));
                for (const std::uint64_t address : name_addresses) {
                    std::uint64_t name_length = 0;
                    if (!memory_->BoundedStringLength(address, 4096,
                                                      &name_length,
                                                      dispatch_error))
                        return false;
                    std::string value(static_cast<std::size_t>(name_length),
                                      '\0');
                    if (name_length != 0 &&
                        !memory_->Read(address, value.data(), value.size())) {
                        SetError(dispatch_error,
                                 "glGetUniformIndices name is unreadable");
                        return false;
                    }
                    strings.push_back(std::move(value));
                }
                for (const std::string& value : strings)
                    pointers.push_back(value.c_str());
                std::vector<GLuint> indices(static_cast<std::size_t>(count),
                                            0);
                if (count != 0)
                    glGetUniformIndices(program, count, pointers.data(),
                                        indices.data());
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (count != 0 &&
                    !memory_->Write(indices_out, indices.data(),
                                    indices.size() * sizeof(GLuint))) {
                    SetError(dispatch_error,
                             "glGetUniformIndices output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetActiveUniformsiv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const GLsizei count = static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t indices = (*registers)[2];
                const GLenum pname = static_cast<GLenum>((*registers)[3]);
                const std::uint64_t output = (*registers)[4];
                (*registers)[0] = 0;
                if (!gate("glGetActiveUniformsiv", dispatch_error))
                    return false;
                if (count < 0 || count > 4096 ||
                    (count != 0 && (indices == 0 || output == 0))) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<GLuint> host_indices(
                    static_cast<std::size_t>(count));
                if (count != 0 &&
                    !memory_->Read(indices, host_indices.data(),
                                   host_indices.size() * sizeof(GLuint))) {
                    SetError(dispatch_error,
                             "glGetActiveUniformsiv indices are unreadable");
                    return false;
                }
                std::vector<GLint> values(static_cast<std::size_t>(count),
                                          0);
                if (count != 0)
                    glGetActiveUniformsiv(program, count, host_indices.data(),
                                          pname, values.data());
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (count != 0 &&
                    !memory_->Write(output, values.data(),
                                    values.size() * sizeof(GLint))) {
                    SetError(dispatch_error,
                             "glGetActiveUniformsiv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    // Sampler objects.
    if (!registry->Register(
            "glGenSamplers",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLsizei count = static_cast<GLsizei>((*registers)[0]);
                const std::uint64_t output = (*registers)[1];
                (*registers)[0] = 0;
                if (count < 0 || count > 4096) {
                    LatchInvalidValueError();
                    return true;
                }
                if (!gate("glGenSamplers", dispatch_error)) return false;
                std::vector<GLuint> names(static_cast<std::size_t>(count), 0);
                if (count != 0) glGenSamplers(count, names.data());
                if (!CheckGlCall("glGenSamplers", dispatch_error))
                    return false;
                for (const GLuint name : names) sampler_names_.insert(name);
                if (count != 0 &&
                    !memory_->Write(output, names.data(),
                                    names.size() * sizeof(GLuint))) {
                    SetError(dispatch_error,
                             "glGenSamplers output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glDeleteSamplers",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLsizei count = static_cast<GLsizei>((*registers)[0]);
                const std::uint64_t input = (*registers)[1];
                (*registers)[0] = 0;
                if (count < 0 || count > 4096) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<GLuint> names(static_cast<std::size_t>(count), 0);
                if (count != 0 &&
                    !memory_->Read(input, names.data(),
                                   names.size() * sizeof(GLuint))) {
                    SetError(dispatch_error,
                             "glDeleteSamplers input is unreadable");
                    return false;
                }
                if (!gate("glDeleteSamplers", dispatch_error)) return false;
                if (count != 0)
                    glDeleteSamplers(count, names.data());
                for (const GLuint name : names) sampler_names_.erase(name);
                (void)glGetError();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glBindSampler",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glBindSampler", dispatch_error)) return false;
                glBindSampler(static_cast<GLuint>((*registers)[0]),
                              static_cast<GLuint>((*registers)[1]));
                (*registers)[0] = 0;
                return CheckGlCall("glBindSampler", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glIsSampler",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint sampler = static_cast<GLuint>((*registers)[0]);
                if (!gate("glIsSampler", dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = glIsSampler(sampler);
                (void)glGetError();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glSamplerParameteri",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glSamplerParameteri", dispatch_error))
                    return false;
                glSamplerParameteri(static_cast<GLuint>((*registers)[0]),
                                    static_cast<GLenum>((*registers)[1]),
                                    static_cast<GLint>((*registers)[2]));
                (*registers)[0] = 0;
                return CheckGlCall("glSamplerParameteri", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glSamplerParameterf",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glSamplerParameterf", dispatch_error))
                    return false;
                glSamplerParameterf(
                    static_cast<GLuint>((*registers)[0]),
                    static_cast<GLenum>((*registers)[1]),
                    *reinterpret_cast<const float*>(
                        &(*registers)[2]));
                (*registers)[0] = 0;
                return CheckGlCall("glSamplerParameterf", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glSamplerParameteriv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint sampler = static_cast<GLuint>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t params = (*registers)[2];
                (*registers)[0] = 0;
                GLint host_params[4] = {0, 0, 0, 0};
                if (!memory_->Read(params, host_params,
                                   sizeof(host_params))) {
                    SetError(dispatch_error,
                             "glSamplerParameteriv params are unreadable");
                    return false;
                }
                if (!gate("glSamplerParameteriv", dispatch_error))
                    return false;
                glSamplerParameteriv(sampler, pname, host_params);
                return CheckGlCall("glSamplerParameteriv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glSamplerParameterfv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint sampler = static_cast<GLuint>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t params = (*registers)[2];
                (*registers)[0] = 0;
                GLfloat host_params[4] = {0, 0, 0, 0};
                if (!memory_->Read(params, host_params,
                                   sizeof(host_params))) {
                    SetError(dispatch_error,
                             "glSamplerParameterfv params are unreadable");
                    return false;
                }
                if (!gate("glSamplerParameterfv", dispatch_error))
                    return false;
                glSamplerParameterfv(sampler, pname, host_params);
                return CheckGlCall("glSamplerParameterfv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetSamplerParameteriv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint sampler = static_cast<GLuint>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                if (!gate("glGetSamplerParameteriv", dispatch_error))
                    return false;
                GLint params[4] = {0, 0, 0, 0};
                glGetSamplerParameteriv(sampler, pname, params);
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (output != 0 &&
                    !memory_->Write(output, params, sizeof(params))) {
                    SetError(dispatch_error,
                             "glGetSamplerParameteriv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetSamplerParameterfv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint sampler = static_cast<GLuint>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                if (!gate("glGetSamplerParameterfv", dispatch_error))
                    return false;
                GLfloat params[4] = {0, 0, 0, 0};
                glGetSamplerParameterfv(sampler, pname, params);
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (output != 0 &&
                    !memory_->Write(output, params, sizeof(params))) {
                    SetError(dispatch_error,
                             "glGetSamplerParameterfv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    // glClearBuffer* family.
    if (!registry->Register(
            "glClearBufferfv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum buffer = static_cast<GLenum>((*registers)[0]);
                const GLint drawbuffer = static_cast<GLint>((*registers)[1]);
                const std::uint64_t value = (*registers)[2];
                (*registers)[0] = 0;
                GLfloat host_value[4] = {0, 0, 0, 0};
                if (!memory_->Read(value, host_value, sizeof(host_value))) {
                    SetError(dispatch_error,
                             "glClearBufferfv value is unreadable");
                    return false;
                }
                if (!gate("glClearBufferfv", dispatch_error)) return false;
                glClearBufferfv(buffer, drawbuffer, host_value);
                return CheckGlCall("glClearBufferfv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glClearBufferiv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum buffer = static_cast<GLenum>((*registers)[0]);
                const GLint drawbuffer = static_cast<GLint>((*registers)[1]);
                const std::uint64_t value = (*registers)[2];
                (*registers)[0] = 0;
                GLint host_value[4] = {0, 0, 0, 0};
                if (!memory_->Read(value, host_value, sizeof(host_value))) {
                    SetError(dispatch_error,
                             "glClearBufferiv value is unreadable");
                    return false;
                }
                if (!gate("glClearBufferiv", dispatch_error)) return false;
                glClearBufferiv(buffer, drawbuffer, host_value);
                return CheckGlCall("glClearBufferiv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glClearBufferuiv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum buffer = static_cast<GLenum>((*registers)[0]);
                const GLint drawbuffer = static_cast<GLint>((*registers)[1]);
                const std::uint64_t value = (*registers)[2];
                (*registers)[0] = 0;
                GLuint host_value[4] = {0, 0, 0, 0};
                if (!memory_->Read(value, host_value, sizeof(host_value))) {
                    SetError(dispatch_error,
                             "glClearBufferuiv value is unreadable");
                    return false;
                }
                if (!gate("glClearBufferuiv", dispatch_error)) return false;
                glClearBufferuiv(buffer, drawbuffer, host_value);
                return CheckGlCall("glClearBufferuiv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glClearBufferfi",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glClearBufferfi", dispatch_error)) return false;
                const GLenum buffer = static_cast<GLenum>((*registers)[0]);
                const GLint drawbuffer = static_cast<GLint>((*registers)[1]);
                GLfloat depth = 0.0f;
                std::memcpy(&depth, &(*registers)[2], sizeof(depth));
                glClearBufferfi(buffer, drawbuffer, depth,
                                static_cast<GLint>((*registers)[3]));
                (*registers)[0] = 0;
                return CheckGlCall("glClearBufferfi", dispatch_error);
            },
            error))
        return false;
    // Indexed state.
    if (!registry->Register(
            "glColorMaski",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glColorMaski", dispatch_error)) return false;
                glColorMaski(static_cast<GLuint>((*registers)[0]),
                             static_cast<GLboolean>((*registers)[1]),
                             static_cast<GLboolean>((*registers)[2]),
                             static_cast<GLboolean>((*registers)[3]),
                             static_cast<GLboolean>((*registers)[4]));
                (*registers)[0] = 0;
                return CheckGlCall("glColorMaski", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glEnablei",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glEnablei", dispatch_error)) return false;
                glEnablei(static_cast<GLenum>((*registers)[0]),
                          static_cast<GLuint>((*registers)[1]));
                (*registers)[0] = 0;
                return CheckGlCall("glEnablei", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDisablei",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glDisablei", dispatch_error)) return false;
                glDisablei(static_cast<GLenum>((*registers)[0]),
                           static_cast<GLuint>((*registers)[1]));
                (*registers)[0] = 0;
                return CheckGlCall("glDisablei", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glIsEnabledi",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glIsEnabledi", dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] =
                    glIsEnabledi(static_cast<GLenum>((*registers)[0]),
                                 static_cast<GLuint>((*registers)[1]));
                (void)glGetError();
                return true;
            },
            error))
        return false;
    // Unsigned uniforms.
    if (!registry->Register(
            "glUniform1ui",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glUniform1ui", dispatch_error)) return false;
                glUniform1ui(static_cast<GLint>((*registers)[0]),
                             static_cast<GLuint>((*registers)[1]));
                (*registers)[0] = 0;
                return CheckGlCall("glUniform1ui", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform2ui",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glUniform2ui", dispatch_error)) return false;
                glUniform2ui(static_cast<GLint>((*registers)[0]),
                             static_cast<GLuint>((*registers)[1]),
                             static_cast<GLuint>((*registers)[2]));
                (*registers)[0] = 0;
                return CheckGlCall("glUniform2ui", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform3ui",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glUniform3ui", dispatch_error)) return false;
                glUniform3ui(static_cast<GLint>((*registers)[0]),
                             static_cast<GLuint>((*registers)[1]),
                             static_cast<GLuint>((*registers)[2]),
                             static_cast<GLuint>((*registers)[3]));
                (*registers)[0] = 0;
                return CheckGlCall("glUniform3ui", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform4ui",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glUniform4ui", dispatch_error)) return false;
                glUniform4ui(static_cast<GLint>((*registers)[0]),
                             static_cast<GLuint>((*registers)[1]),
                             static_cast<GLuint>((*registers)[2]),
                             static_cast<GLuint>((*registers)[3]),
                             static_cast<GLuint>((*registers)[4]));
                (*registers)[0] = 0;
                return CheckGlCall("glUniform4ui", dispatch_error);
            },
            error))
        return false;
    const auto uniform_uiv =
        [this, gate](const char* name, int width,
                     void (*call)(GLint, GLsizei, const GLuint*),
                     Aarch64ImportRegistry::Registers* registers,
                     std::string* dispatch_error) -> bool {
        const GLint location = static_cast<GLint>((*registers)[0]);
        const GLsizei count = static_cast<GLsizei>((*registers)[1]);
        const std::uint64_t value = (*registers)[2];
        (*registers)[0] = 0;
        if (count < 0 || count > 4096) {
            LatchInvalidValueError();
            return true;
        }
        std::vector<GLuint> host_value(
            static_cast<std::size_t>(count) *
                static_cast<std::size_t>(width),
            0);
        if (count != 0 &&
            !memory_->Read(value, host_value.data(),
                           host_value.size() * sizeof(GLuint))) {
            SetError(dispatch_error,
                     std::string(name) + " values are unreadable");
            return false;
        }
        if (!gate(name, dispatch_error)) return false;
        call(location, count, host_value.data());
        return CheckGlCall(name, dispatch_error);
    };
    if (!registry->Register(
            "glUniform1uiv",
            [this, uniform_uiv](Aarch64ImportRegistry::Registers* registers,
                                std::string* dispatch_error) {
                return uniform_uiv("glUniform1uiv", 1, glUniform1uiv,
                                   registers, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform2uiv",
            [this, uniform_uiv](Aarch64ImportRegistry::Registers* registers,
                                std::string* dispatch_error) {
                return uniform_uiv("glUniform2uiv", 2, glUniform2uiv,
                                   registers, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform3uiv",
            [this, uniform_uiv](Aarch64ImportRegistry::Registers* registers,
                                std::string* dispatch_error) {
                return uniform_uiv("glUniform3uiv", 3, glUniform3uiv,
                                   registers, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform4uiv",
            [this, uniform_uiv](Aarch64ImportRegistry::Registers* registers,
                                std::string* dispatch_error) {
                return uniform_uiv("glUniform4uiv", 4, glUniform4uiv,
                                   registers, dispatch_error);
            },
            error))
        return false;
    // Remaining ES3 queries.
    if (!registry->Register(
            "glGetFragDataLocation",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const std::uint64_t name = (*registers)[1];
                (*registers)[0] = static_cast<std::uint64_t>(-1);
                if (!gate("glGetFragDataLocation", dispatch_error))
                    return false;
                std::uint64_t name_length = 0;
                if (!memory_->BoundedStringLength(name, 4096, &name_length,
                                                  dispatch_error))
                    return false;
                std::string host_name(static_cast<std::size_t>(name_length),
                                      '\0');
                if (name_length != 0 &&
                    !memory_->Read(name, host_name.data(),
                                   host_name.size())) {
                    SetError(dispatch_error,
                             "glGetFragDataLocation name is unreadable");
                    return false;
                }
                const GLint location =
                    glGetFragDataLocation(program, host_name.c_str());
                (void)glGetError();
                (*registers)[0] = static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(location));
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetUniformuiv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const GLint location = static_cast<GLint>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                if (!gate("glGetUniformuiv", dispatch_error)) return false;
                GLuint params[16] = {};
                glGetUniformuiv(program, location, params);
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (output != 0 &&
                    !memory_->Write(output, params, sizeof(params))) {
                    SetError(dispatch_error,
                             "glGetUniformuiv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetVertexAttribIiv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint index = static_cast<GLuint>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                if (!gate("glGetVertexAttribIiv", dispatch_error))
                    return false;
                GLint params[4] = {0, 0, 0, 0};
                glGetVertexAttribIiv(index, pname, params);
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (output != 0 &&
                    !memory_->Write(output, params, sizeof(params))) {
                    SetError(dispatch_error,
                             "glGetVertexAttribIiv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetVertexAttribIuiv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint index = static_cast<GLuint>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                if (!gate("glGetVertexAttribIuiv", dispatch_error))
                    return false;
                GLuint params[4] = {0, 0, 0, 0};
                glGetVertexAttribIuiv(index, pname, params);
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (output != 0 &&
                    !memory_->Write(output, params, sizeof(params))) {
                    SetError(dispatch_error,
                             "glGetVertexAttribIuiv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetTexLevelParameteriv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLint level = static_cast<GLint>((*registers)[1]);
                const GLenum pname = static_cast<GLenum>((*registers)[2]);
                const std::uint64_t output = (*registers)[3];
                (*registers)[0] = 0;
                if (!gate("glGetTexLevelParameteriv", dispatch_error))
                    return false;
                GLint params[4] = {0, 0, 0, 0};
                glGetTexLevelParameteriv(target, level, pname, params);
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (output != 0 &&
                    !memory_->Write(output, params, sizeof(params))) {
                    SetError(dispatch_error,
                             "glGetTexLevelParameteriv output is "
                             "unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetTexLevelParameterfv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLint level = static_cast<GLint>((*registers)[1]);
                const GLenum pname = static_cast<GLenum>((*registers)[2]);
                const std::uint64_t output = (*registers)[3];
                (*registers)[0] = 0;
                if (!gate("glGetTexLevelParameterfv", dispatch_error))
                    return false;
                GLfloat params[4] = {0, 0, 0, 0};
                glGetTexLevelParameterfv(target, level, pname, params);
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (output != 0 &&
                    !memory_->Write(output, params, sizeof(params))) {
                    SetError(dispatch_error,
                             "glGetTexLevelParameterfv output is "
                             "unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetInternalformativ",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLenum internal_format =
                    static_cast<GLenum>((*registers)[1]);
                const GLenum pname = static_cast<GLenum>((*registers)[2]);
                const GLsizei count = static_cast<GLsizei>((*registers)[3]);
                const std::uint64_t output = (*registers)[4];
                (*registers)[0] = 0;
                if (!gate("glGetInternalformativ", dispatch_error))
                    return false;
                if (count < 0 || count > 64) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<GLint> params(static_cast<std::size_t>(
                                              count == 0 ? 1 : count),
                                          0);
                if (count != 0)
                    glGetInternalformativ(target, internal_format, pname,
                                          count, params.data());
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (output != 0 && count != 0 &&
                    !memory_->Write(output, params.data(),
                                    params.size() * sizeof(GLint))) {
                    SetError(dispatch_error,
                             "glGetInternalformativ output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glProgramParameteri",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glProgramParameteri", dispatch_error))
                    return false;
                glProgramParameteri(static_cast<GLuint>((*registers)[0]),
                                    static_cast<GLenum>((*registers)[1]),
                                    static_cast<GLint>((*registers)[2]));
                (*registers)[0] = 0;
                return CheckGlCall("glProgramParameteri", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetProgramBinary",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const GLsizei buf_size = static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t length_out = (*registers)[2];
                const std::uint64_t format_out = (*registers)[3];
                const std::uint64_t binary_out = (*registers)[4];
                (*registers)[0] = 0;
                if (!gate("glGetProgramBinary", dispatch_error)) return false;
                if (buf_size < 0 || buf_size > 64 * 1024 * 1024) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<std::uint8_t> binary(
                    static_cast<std::size_t>(buf_size == 0 ? 1 : buf_size));
                GLsizei written = 0;
                GLenum format = 0;
                glGetProgramBinary(program, buf_size, &written, &format,
                                   binary.data());
                const GLenum query_error = glGetError();
                if (query_error != GL_NO_ERROR) {
                    if (pending_guest_gl_error_ == 0)
                        pending_guest_gl_error_ = query_error;
                    return true;
                }
                if (length_out != 0 &&
                    !memory_->Write(length_out, &written, sizeof(written))) {
                    SetError(dispatch_error,
                             "glGetProgramBinary length is unwritable");
                    return false;
                }
                if (format_out != 0 &&
                    !memory_->Write(format_out, &format, sizeof(format))) {
                    SetError(dispatch_error,
                             "glGetProgramBinary format is unwritable");
                    return false;
                }
                if (binary_out != 0 && written > 0 &&
                    !memory_->Write(binary_out, binary.data(),
                                    static_cast<std::size_t>(written))) {
                    SetError(dispatch_error,
                             "glGetProgramBinary payload is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glProgramBinary",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const GLenum format = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t binary = (*registers)[2];
                const GLsizei length = static_cast<GLsizei>((*registers)[3]);
                (*registers)[0] = 0;
                if (length < 0 || length > 64 * 1024 * 1024) {
                    LatchInvalidValueError();
                    return true;
                }
                std::vector<std::uint8_t> payload(
                    static_cast<std::size_t>(length));
                if (length != 0 &&
                    !memory_->Read(binary, payload.data(), payload.size())) {
                    SetError(dispatch_error,
                             "glProgramBinary payload is unreadable");
                    return false;
                }
                if (!gate("glProgramBinary", dispatch_error)) return false;
                glProgramBinary(program, format, payload.data(), length);
                return CheckGlCall("glProgramBinary", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glTexBuffer",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                if (!gate("glTexBuffer", dispatch_error)) return false;
                glTexBuffer(static_cast<GLenum>((*registers)[0]),
                            static_cast<GLenum>((*registers)[1]),
                            static_cast<GLuint>((*registers)[2]));
                (*registers)[0] = 0;
                return CheckGlCall("glTexBuffer", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glGetBooleanv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum pname = static_cast<GLenum>((*registers)[0]);
                const std::uint64_t output = (*registers)[1];
                (*registers)[0] = 0;
                GLboolean value = GL_FALSE;
                if (!gate("glGetBooleanv", dispatch_error)) return false;
                glGetBooleanv(pname, &value);
                if (!CheckGlCall("glGetBooleanv", dispatch_error))
                    return false;
                if (output != 0 &&
                    !memory_->Write(output, &value, sizeof(value))) {
                    SetError(dispatch_error,
                             "glGetBooleanv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetFloatv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum pname = static_cast<GLenum>((*registers)[0]);
                const std::uint64_t output = (*registers)[1];
                (*registers)[0] = 0;
                GLfloat value = 0.0F;
                if (!gate("glGetFloatv", dispatch_error)) return false;
                glGetFloatv(pname, &value);
                if (!CheckGlCall("glGetFloatv", dispatch_error)) return false;
                if (output != 0 &&
                    !memory_->Write(output, &value, sizeof(value))) {
                    SetError(dispatch_error,
                             "glGetFloatv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetError",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                if (!initialized_ || shutdown_ || !is_current()) {
                    (*registers)[0] = static_cast<std::uint64_t>(
                        GL_NO_ERROR);
                    return true;
                }
                // Report a guest-visible error we recorded (e.g. an ES3 pname
                // probe we soft-failed) before delegating to the host queue.
                if (pending_guest_gl_error_ != 0) {
                    (*registers)[0] = static_cast<std::uint64_t>(
                        pending_guest_gl_error_);
                    pending_guest_gl_error_ = 0;
                    return true;
                }
                (*registers)[0] = static_cast<std::uint64_t>(glGetError());
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetTexParameterfv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                GLfloat value = 0.0F;
                if (!gate("glGetTexParameterfv", dispatch_error)) return false;
                glGetTexParameterfv(target, pname, &value);
                if (!CheckGlCall("glGetTexParameterfv", dispatch_error))
                    return false;
                if (output != 0 &&
                    !memory_->Write(output, &value, sizeof(value))) {
                    SetError(dispatch_error,
                             "glGetTexParameterfv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetTexParameteriv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                GLint value = 0;
                if (!gate("glGetTexParameteriv", dispatch_error)) return false;
                glGetTexParameteriv(target, pname, &value);
                if (!CheckGlCall("glGetTexParameteriv", dispatch_error))
                    return false;
                if (output != 0 &&
                    !memory_->Write(output, &value, sizeof(value))) {
                    SetError(dispatch_error,
                             "glGetTexParameteriv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetBufferParameteriv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                GLint value = 0;
                if (!gate("glGetBufferParameteriv", dispatch_error))
                    return false;
                glGetBufferParameteriv(target, pname, &value);
                if (!CheckGlCall("glGetBufferParameteriv", dispatch_error))
                    return false;
                if (output != 0 &&
                    !memory_->Write(output, &value, sizeof(value))) {
                    SetError(dispatch_error,
                             "glGetBufferParameteriv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetUniformfv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const GLint location = static_cast<GLint>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                std::vector<GLfloat> values(kMaxQueryElements, 0.0F);
                if (!gate("glGetUniformfv", dispatch_error)) return false;
                glGetUniformfv(program, location, values.data());
                if (!CheckGlCall("glGetUniformfv", dispatch_error))
                    return false;
                if (output != 0 &&
                    !memory_->Write(output, values.data(),
                                    values.size() * sizeof(GLfloat))) {
                    SetError(dispatch_error,
                             "glGetUniformfv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetUniformiv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const GLint location = static_cast<GLint>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                std::vector<GLint> values(kMaxQueryElements, 0);
                if (!gate("glGetUniformiv", dispatch_error)) return false;
                glGetUniformiv(program, location, values.data());
                if (!CheckGlCall("glGetUniformiv", dispatch_error))
                    return false;
                if (output != 0 &&
                    !memory_->Write(output, values.data(),
                                    values.size() * sizeof(GLint))) {
                    SetError(dispatch_error,
                             "glGetUniformiv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetVertexAttribfv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint index = static_cast<GLuint>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                std::vector<GLfloat> values(4, 0.0F);
                if (!gate("glGetVertexAttribfv", dispatch_error)) return false;
                glGetVertexAttribfv(index, pname, values.data());
                if (!CheckGlCall("glGetVertexAttribfv", dispatch_error))
                    return false;
                if (output != 0 &&
                    !memory_->Write(output, values.data(),
                                    values.size() * sizeof(GLfloat))) {
                    SetError(dispatch_error,
                             "glGetVertexAttribfv output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetVertexAttribPointerv",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                // Pointer queries return host pointers the guest cannot
                // dereference; report a null pointer value.
                (*registers)[0] = 0;
                const std::uint64_t output = (*registers)[2];
                if (output != 0) {
                    std::uint64_t zero = 0;
                    (void)memory_->Write(output, &zero, sizeof(zero));
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetShaderSource",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint shader = static_cast<GLuint>((*registers)[0]);
                const GLsizei max_size =
                    static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t length_out = (*registers)[2];
                const std::uint64_t source_out = (*registers)[3];
                (*registers)[0] = 0;
                if (max_size < 0) {
                    SetError(dispatch_error,
                             "glGetShaderSource size is negative");
                    return false;
                }
                std::vector<GLchar> source(
                    static_cast<std::size_t>(max_size) + 1, '\0');
                GLsizei length = 0;
                if (!gate("glGetShaderSource", dispatch_error)) return false;
                glGetShaderSource(shader, max_size, &length, source.data());
                if (!CheckGlCall("glGetShaderSource", dispatch_error))
                    return false;
                if (length_out != 0 &&
                    !memory_->Write(length_out, &length, sizeof(length))) {
                    SetError(dispatch_error,
                             "glGetShaderSource length is unwritable");
                    return false;
                }
                if (source_out != 0 && length > 0 &&
                    !memory_->Write(source_out, source.data(),
                                    static_cast<std::size_t>(length))) {
                    SetError(dispatch_error,
                             "glGetShaderSource source is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetAttachedShaders",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const GLsizei max_count =
                    static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t count_out = (*registers)[2];
                const std::uint64_t shaders_out = (*registers)[3];
                (*registers)[0] = 0;
                if (max_count < 0) {
                    SetError(dispatch_error,
                             "glGetAttachedShaders count is negative");
                    return false;
                }
                std::vector<GLuint> names(
                    static_cast<std::size_t>(max_count), 0);
                GLsizei count = 0;
                if (!gate("glGetAttachedShaders", dispatch_error))
                    return false;
                glGetAttachedShaders(program, max_count, &count,
                                     names.data());
                if (!CheckGlCall("glGetAttachedShaders", dispatch_error))
                    return false;
                if (count_out != 0 &&
                    !memory_->Write(count_out, &count, sizeof(count))) {
                    SetError(dispatch_error,
                             "glGetAttachedShaders count is unwritable");
                    return false;
                }
                if (shaders_out != 0 && count > 0 &&
                    !memory_->Write(shaders_out, names.data(),
                                    static_cast<std::size_t>(count) *
                                        sizeof(GLuint))) {
                    SetError(dispatch_error,
                             "glGetAttachedShaders shaders are unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetShaderPrecisionFormat",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum shader_type =
                    static_cast<GLenum>((*registers)[0]);
                const GLenum precision_type =
                    static_cast<GLenum>((*registers)[1]);
                const std::uint64_t range_out = (*registers)[2];
                const std::uint64_t precision_out = (*registers)[3];
                (*registers)[0] = 0;
                GLint range[2] = {0, 0};
                GLint precision = 0;
                if (!gate("glGetShaderPrecisionFormat", dispatch_error))
                    return false;
                glGetShaderPrecisionFormat(shader_type, precision_type,
                                           range, &precision);
                if (!CheckGlCall("glGetShaderPrecisionFormat",
                                 dispatch_error))
                    return false;
                if (range_out != 0 &&
                    !memory_->Write(range_out, range, sizeof(range))) {
                    SetError(dispatch_error,
                             "glGetShaderPrecisionFormat range is unwritable");
                    return false;
                }
                if (precision_out != 0 &&
                    !memory_->Write(precision_out, &precision,
                                    sizeof(precision))) {
                    SetError(
                        dispatch_error,
                        "glGetShaderPrecisionFormat precision is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetFramebufferAttachmentParameteriv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLenum attachment =
                    static_cast<GLenum>((*registers)[1]);
                const GLenum pname = static_cast<GLenum>((*registers)[2]);
                const std::uint64_t output = (*registers)[3];
                (*registers)[0] = 0;
                GLint value = 0;
                if (!gate("glGetFramebufferAttachmentParameteriv",
                          dispatch_error))
                    return false;
                glGetFramebufferAttachmentParameteriv(target, attachment,
                                                      pname, &value);
                if (!CheckGlCall("glGetFramebufferAttachmentParameteriv",
                                 dispatch_error))
                    return false;
                if (output != 0 &&
                    !memory_->Write(output, &value, sizeof(value))) {
                    SetError(
                        dispatch_error,
                        "glGetFramebufferAttachmentParameteriv is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glGetRenderbufferParameteriv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLenum pname = static_cast<GLenum>((*registers)[1]);
                const std::uint64_t output = (*registers)[2];
                (*registers)[0] = 0;
                GLint value = 0;
                if (!gate("glGetRenderbufferParameteriv", dispatch_error))
                    return false;
                glGetRenderbufferParameteriv(target, pname, &value);
                if (!CheckGlCall("glGetRenderbufferParameteriv",
                                 dispatch_error))
                    return false;
                if (output != 0 &&
                    !memory_->Write(output, &value, sizeof(value))) {
                    SetError(dispatch_error,
                             "glGetRenderbufferParameteriv is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glIsTexture",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint texture = static_cast<GLuint>((*registers)[0]);
                if (!gate("glIsTexture", dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = glIsTexture(texture);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glIsEnabled",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum capability =
                    static_cast<GLenum>((*registers)[0]);
                if (!gate("glIsEnabled", dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = glIsEnabled(capability);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glIsProgram",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                if (!gate("glIsProgram", dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = glIsProgram(program);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glIsShader",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint shader = static_cast<GLuint>((*registers)[0]);
                if (!gate("glIsShader", dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = glIsShader(shader);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glIsFramebuffer",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint framebuffer =
                    static_cast<GLuint>((*registers)[0]);
                if (!gate("glIsFramebuffer", dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = glIsFramebuffer(framebuffer);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glIsRenderbuffer",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint renderbuffer =
                    static_cast<GLuint>((*registers)[0]);
                if (!gate("glIsRenderbuffer", dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = glIsRenderbuffer(renderbuffer);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glDrawArrays",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                         std::string* dispatch_error) {
                const GLenum mode = static_cast<GLenum>((*registers)[0]);
                const GLint first = static_cast<GLint>((*registers)[1]);
                const GLsizei count = static_cast<GLsizei>((*registers)[2]);
                (*registers)[0] = 0;
                if (!gate("glDrawArrays", dispatch_error)) return false;
                // Re-sync the tracked program from the host context — the
                // binding is per-context while bound_program_ tracks the
                // last write across all contexts.
                {
                    GLint host_program = 0;
                    glGetIntegerv(GL_CURRENT_PROGRAM, &host_program);
                    (void)glGetError();
                    if (host_program != 0 &&
                        program_names_.contains(
                            static_cast<std::uint32_t>(host_program)))
                        bound_program_ =
                            static_cast<std::uint32_t>(host_program);
                }
                // With no array buffer bound the guest attribs carry
                // client-memory pointers: stage them like the DrawElements
                // path so the host draw reads real vertex data instead of
                // a stale buffer-bound attribute state.
                if (bound_array_buffer_ == 0 && count > 0) {
                    if (first < 0 ||
                        bound_program_ == 0 ||
                        !program_names_.contains(bound_program_)) {
                        if (pending_guest_gl_error_ == 0)
                            pending_guest_gl_error_ =
                                0x0502 /*GL_INVALID_OPERATION*/;
                        static std::size_t reported_arrays_no_program = 0;
                        if (reported_arrays_no_program < 50) {
                            ++reported_arrays_no_program;
                            std::fprintf(
                                stderr,
                                "ISOMR_GLES_DRAW_ERROR name=glDrawArrays "
                                "err=no-program-or-bad-first ctx=%p "
                                "tracked=%u draws=%zu\n",
                                eglGetCurrentContext(), bound_program_,
                                draw_call_count_);
                            std::fflush(stderr);
                        }
                        ++draw_call_count_;
                        return true;
                    }
                    const std::uint64_t maximum_vertex =
                        static_cast<std::uint64_t>(first) +
                        static_cast<std::uint64_t>(count) - 1;
                    if (maximum_vertex >
                        std::numeric_limits<std::uint32_t>::max()) {
                        SetError(dispatch_error,
                                 "native GLES glDrawArrays vertex range is "
                                 "oversized");
                        return false;
                    }
                    if (!StageClientVertexAttributes(
                            static_cast<std::uint32_t>(maximum_vertex), 0,
                            dispatch_error))
                        return false;
                }
                EnsureDrawableViewport();
                glDrawArrays(mode, first, count);
                LatchDrawGlError("glDrawArrays");
                ++draw_call_count_;
                // Draw-state snapshot for the first draws and then sparsely:
                // target/viewport/program/texture plus a center-pixel
                // readback in the draw's own framebuffer, so a black present
                // can be attributed to the draw target or the draw content.
                if (draw_call_count_ <= 10 ||
                    (draw_call_count_ % 1000) == 0) {
                    GLint viewport[4] = {0, 0, 0, 0};
                    GLint scissor_box[4] = {0, 0, 0, 0};
                    GLint scissor_enabled = 0;
                    GLint current_program = 0;
                    GLint texture_binding = 0;
                    GLint blend_enabled = 0;
                    GLfloat clear_color[4] = {0, 0, 0, 0};
                    GLint blend_src_rgb = 0;
                    GLint blend_dst_rgb = 0;
                    GLint blend_src_alpha = 0;
                    GLint blend_dst_alpha = 0;
                    ClearGlErrors();
                    glGetIntegerv(GL_VIEWPORT, viewport);
                    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
                    glGetIntegerv(GL_SCISSOR_TEST, &scissor_enabled);
                    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
                    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_binding);
                    glGetIntegerv(GL_BLEND, &blend_enabled);
                    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color);
                    glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
                    glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
                    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha);
                    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);
                    GLint attrib_enabled[4] = {0, 0, 0, 0};
                    GLint attrib_size[4] = {0, 0, 0, 0};
                    GLint attrib_type[4] = {0, 0, 0, 0};
                    GLint attrib_stride[4] = {0, 0, 0, 0};
                    GLint attrib_normalized[4] = {0, 0, 0, 0};
                    GLint attrib_buffer[4] = {-1, -1, -1, -1};
                    GLvoid* attrib_pointer[4] = {nullptr, nullptr, nullptr,
                                                 nullptr};
                    for (GLuint index = 0; index < 4; ++index) {
                        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                                            &attrib_enabled[index]);
                        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE,
                                            &attrib_size[index]);
                        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE,
                                            &attrib_type[index]);
                        glGetVertexAttribiv(index,
                                            GL_VERTEX_ATTRIB_ARRAY_STRIDE,
                                            &attrib_stride[index]);
                        glGetVertexAttribiv(index,
                                            GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
                                            &attrib_normalized[index]);
                        glGetVertexAttribiv(
                            index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,
                            &attrib_buffer[index]);
                        glGetVertexAttribPointerv(
                            index, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                            &attrib_pointer[index]);
                    }
                    if (current_program != 0) {
                        GLint active_uniforms = 0;
                        glGetProgramiv(current_program, GL_ACTIVE_UNIFORMS,
                                      &active_uniforms);
                        ClearGlErrors();
                        for (GLint uniform = 0;
                             uniform < active_uniforms && uniform < 8;
                             ++uniform) {
                            GLint uniform_size = 0;
                            GLenum uniform_type = 0;
                            GLchar uniform_name[64] = {};
                            glGetActiveUniform(
                                current_program, uniform,
                                sizeof(uniform_name) - 1, nullptr,
                                &uniform_size, &uniform_type, uniform_name);
                            ClearGlErrors();
                            const GLint location = glGetUniformLocation(
                                current_program, uniform_name);
                            ClearGlErrors();
                            // A mat4 uniform writes 16 floats; size the
                            // staging buffer for the largest supported type
                            // so glGetUniformfv cannot overflow it.
                            GLfloat values[16] = {};
                            if (location >= 0) {
                                glGetUniformfv(current_program, location,
                                               values);
                                ClearGlErrors();
                            }
                            std::fprintf(stderr,
                                         "ISOMR_DRAW_UNIFORM n=%zu "
                                         "prog=%d name=%s loc=%d "
                                         "type=%x size=%d "
                                         "v=[%.3f %.3f %.3f %.3f "
                                         "%.3f %.3f %.3f %.3f]\n",
                                         draw_call_count_, current_program,
                                         uniform_name, location,
                                         static_cast<unsigned>(uniform_type),
                                         uniform_size, values[0], values[1],
                                         values[2], values[3], values[4],
                                         values[5], values[6], values[7]);
                        }
                    }
                    ClearGlErrors();
                    std::uint8_t samples[5][4] = {};
                    const GLint sample_xs[5] = {
                        viewport[2] > 0 ? viewport[0] + viewport[2] / 2 : 0,
                        viewport[2] > 0 ? viewport[0] + viewport[2] / 4 : 0,
                        viewport[2] > 0
                            ? viewport[0] + (3 * viewport[2]) / 4
                            : 0,
                        viewport[2] > 0 ? viewport[0] + viewport[2] / 4 : 0,
                        viewport[2] > 0
                            ? viewport[0] + (3 * viewport[2]) / 4
                            : 0};
                    const GLint sample_ys[5] = {
                        viewport[3] > 0 ? viewport[1] + viewport[3] / 2 : 0,
                        viewport[3] > 0 ? viewport[1] + viewport[3] / 4 : 0,
                        viewport[3] > 0 ? viewport[1] + viewport[3] / 4 : 0,
                        viewport[3] > 0
                            ? viewport[1] + (3 * viewport[3]) / 4
                            : 0,
                        viewport[3] > 0
                            ? viewport[1] + (3 * viewport[3]) / 4
                            : 0};
                    ClearGlErrors();
                    for (std::size_t sample = 0; sample < 5; ++sample) {
                        glReadPixels(sample_xs[sample], sample_ys[sample], 1,
                                     1, GL_RGBA, GL_UNSIGNED_BYTE,
                                     samples[sample]);
                    }
                    ClearGlErrors();
                    std::fprintf(stderr,
                                 "ISOMR_DRAW_STATE n=%zu tid=%lu owner=%lu "
                                 "mode=%u "
                                 "count=%d "
                                 "fbo=%u vp=[%d %d %d %d] "
                                 "scissor=[%d %d %d %d]/%d prog=%d tex=%d "
                                 "blend=%d blendfn=[%x %x %x %x] "
                                 "clear=[%.2f %.2f %.2f %.2f]\n",
                                 draw_call_count_,
                                 GetCurrentThreadId(), owner_thread_id_,
                                 mode, count,
                                 bound_framebuffer_, viewport[0], viewport[1],
                                 viewport[2], viewport[3], scissor_box[0],
                                 scissor_box[1], scissor_box[2], scissor_box[3],
                                 scissor_enabled != 0 ? 1 : 0, current_program,
                                 texture_binding, blend_enabled != 0 ? 1 : 0,
                                 blend_src_rgb, blend_dst_rgb, blend_src_alpha,
                                 blend_dst_alpha, clear_color[0],
                                 clear_color[1], clear_color[2],
                                 clear_color[3]);
                    for (GLuint index = 0; index < 4; ++index) {
                        std::fprintf(stderr,
                                     "ISOMR_DRAW_ATTRIB n=%zu i=%u en=%d "
                                     "size=%d type=%x stride=%d norm=%d "
                                     "buf=%d ptr=%p\n",
                                     draw_call_count_, index,
                                     attrib_enabled[index], attrib_size[index],
                                     attrib_type[index], attrib_stride[index],
                                     attrib_normalized[index],
                                     attrib_buffer[index],
                                     attrib_pointer[index]);
                    }
                    // Does the bound ARRAY_BUFFER actually have storage on
                    // the ANGLE side? glBufferData(target=ARRAY_BUFFER) was
                    // never dispatched through the import path, so if
                    // GL_BUFFER_SIZE is 0 here the game uploads vertex data
                    // through a path we never intercept and every draw
                    // consumes an empty buffer (degenerate triangles, black
                    // present). Client-pointer draws (vbo==0) read the first
                    // vertex directly — the guest heap is identity-mapped, so
                    // the pointer is a valid host address.
                    {
                        GLint vbo = 0;
                        ClearGlErrors();
                        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vbo);
                        GLint buffer_size = -1;
                        if (vbo != 0)
                            glGetBufferParameteriv(GL_ARRAY_BUFFER,
                                                   GL_BUFFER_SIZE,
                                                   &buffer_size);
                        static std::size_t vbo_size_logs = 0;
                        if (vbo_size_logs < 20) {
                            ++vbo_size_logs;
                            std::fprintf(stderr,
                                         "ISOMR_VBO_SIZE n=%zu vbo=%d "
                                         "size=%d\n",
                                         draw_call_count_, vbo, buffer_size);
                            if (vbo == 0 && attrib_pointer[0] != nullptr) {
                                const std::uint8_t* client =
                                    static_cast<const std::uint8_t*>(
                                        attrib_pointer[0]);
                                std::fprintf(stderr,
                                             "ISOMR_VERT0 ptr=%p bytes=%02x "
                                             "%02x %02x %02x %02x %02x %02x "
                                             "%02x %02x %02x %02x %02x\n",
                                             attrib_pointer[0], client[0],
                                             client[1], client[2], client[3],
                                             client[4], client[5], client[6],
                                             client[7], client[8], client[9],
                                             client[10], client[11]);
                            }
                            std::fflush(stderr);
                        }
                    }
                    std::fprintf(
                        stderr,
                        "ISOMR_DRAW_SAMPLES n=%zu c=[%u %u %u %u] "
                        "q1=[%u %u %u %u] q2=[%u %u %u %u] q3=[%u %u %u %u] "
                        "q4=[%u %u %u %u]\n",
                        draw_call_count_, samples[0][0], samples[0][1],
                        samples[0][2], samples[0][3], samples[1][0],
                        samples[1][1], samples[1][2], samples[1][3],
                        samples[2][0], samples[2][1], samples[2][2],
                        samples[2][3], samples[3][0], samples[3][1],
                        samples[3][2], samples[3][3], samples[4][0],
                        samples[4][1], samples[4][2], samples[4][3]);
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glFinish",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                (*registers)[0] = 0;
                if (!gate("glFinish", dispatch_error)) return false;
                glFinish();
                return CheckGlCall("glFinish", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glFlush",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                (*registers)[0] = 0;
                if (!gate("glFlush", dispatch_error)) return false;
                glFlush();
                return CheckGlCall("glFlush", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glReadPixels",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint x = static_cast<GLint>((*registers)[0]);
                const GLint y = static_cast<GLint>((*registers)[1]);
                const GLsizei width = static_cast<GLsizei>((*registers)[2]);
                const GLsizei height = static_cast<GLsizei>((*registers)[3]);
                const GLenum format = static_cast<GLenum>((*registers)[4]);
                const GLenum type = static_cast<GLenum>((*registers)[5]);
                const std::uint64_t pixels = (*registers)[6];
                (*registers)[0] = 0;
                if (width < 0 || height < 0 ||
                    static_cast<std::uint64_t>(width) *
                            static_cast<std::uint64_t>(height) * 4U >
                        kMaxBufferData) {
                    SetError(dispatch_error,
                             "glReadPixels size exceeds the supported bound");
                    return false;
                }
                std::vector<std::uint8_t> host(
                    static_cast<std::size_t>(width) *
                    static_cast<std::size_t>(height) * 4U);
                if (!gate("glReadPixels", dispatch_error)) return false;
                glReadPixels(x, y, width, height, format, type, host.data());
                if (!CheckGlCall("glReadPixels", dispatch_error)) return false;
                if (pixels != 0 &&
                    !memory_->Write(pixels, host.data(), host.size())) {
                    SetError(dispatch_error,
                             "glReadPixels output is unwritable");
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glCopyTexImage2D",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLint level = static_cast<GLint>((*registers)[1]);
                const GLenum internal_format =
                    static_cast<GLenum>((*registers)[2]);
                const GLint x = static_cast<GLint>((*registers)[3]);
                const GLint y = static_cast<GLint>((*registers)[4]);
                const GLsizei width = static_cast<GLsizei>((*registers)[5]);
                const GLsizei height = static_cast<GLsizei>((*registers)[6]);
                const GLint border = static_cast<GLint>((*registers)[7]);
                (*registers)[0] = 0;
                if (!gate("glCopyTexImage2D", dispatch_error)) return false;
                glCopyTexImage2D(target, level, internal_format, x, y, width,
                                 height, border);
                return CheckGlCall("glCopyTexImage2D", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glCopyTexSubImage2D",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLint level = static_cast<GLint>((*registers)[1]);
                const GLint x_offset = static_cast<GLint>((*registers)[2]);
                const GLint y_offset = static_cast<GLint>((*registers)[3]);
                const GLint x = static_cast<GLint>((*registers)[4]);
                const GLint y = static_cast<GLint>((*registers)[5]);
                const GLsizei width = static_cast<GLsizei>((*registers)[6]);
                const GLsizei height = static_cast<GLsizei>((*registers)[7]);
                (*registers)[0] = 0;
                if (!gate("glCopyTexSubImage2D", dispatch_error))
                    return false;
                glCopyTexSubImage2D(target, level, x_offset, y_offset, x, y,
                                    width, height);
                return CheckGlCall("glCopyTexSubImage2D", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glCompressedTexImage2D",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLint level = static_cast<GLint>((*registers)[1]);
                const GLenum internal_format =
                    static_cast<GLenum>((*registers)[2]);
                const GLsizei width = static_cast<GLsizei>((*registers)[3]);
                const GLsizei height = static_cast<GLsizei>((*registers)[4]);
                const GLint border = static_cast<GLint>((*registers)[5]);
                const GLsizei image_size =
                    static_cast<GLsizei>((*registers)[6]);
                const std::uint64_t data = (*registers)[7];
                (*registers)[0] = 0;
                {
                    static std::size_t logged_ctex = 0;
                    if (logged_ctex < 80) {
                        ++logged_ctex;
                        std::fprintf(stderr,
                                     "ISOMR_CTEXIMAGE tgt=%x lv=%d ifmt=%x "
                                     "%dx%d size=%d data=%llx\n",
                                     target, level, internal_format, width,
                                     height, image_size,
                                     static_cast<unsigned long long>(data));
                        std::fflush(stderr);
                    }
                }
                if (image_size < 0 || static_cast<std::uint64_t>(image_size) >
                                              kMaxBufferData) {
                    SetError(dispatch_error,
                             "glCompressedTexImage2D size exceeds the "
                             "supported bound");
                    return false;
                }
                std::vector<std::uint8_t> host(
                    static_cast<std::size_t>(image_size));
                if (data != 0 && image_size != 0 &&
                    !memory_->Read(data, host.data(), host.size())) {
                    SetError(dispatch_error,
                             "glCompressedTexImage2D data is unreadable");
                    return false;
                }
                if (!gate("glCompressedTexImage2D", dispatch_error))
                    return false;
                glCompressedTexImage2D(target, level, internal_format, width,
                                       height, border, image_size,
                                       data != 0 ? host.data() : nullptr);
                if (!CheckGlCall("glCompressedTexImage2D", dispatch_error))
                    return false;
                const std::uint32_t defined_texture =
                    BoundTextureForUploadTarget(target);
                if (defined_texture != 0) {
                    texture_level_images_.insert_or_assign(
                        (static_cast<std::uint64_t>(defined_texture) << 32) |
                            static_cast<std::uint32_t>(level),
                        TextureLevelImage{true, internal_format, width,
                                          height});
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glCompressedTexSubImage2D",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                const GLint level = static_cast<GLint>((*registers)[1]);
                const GLint x_offset = static_cast<GLint>((*registers)[2]);
                const GLint y_offset = static_cast<GLint>((*registers)[3]);
                const GLsizei width = static_cast<GLsizei>((*registers)[4]);
                const GLsizei height = static_cast<GLsizei>((*registers)[5]);
                const GLenum format = static_cast<GLenum>((*registers)[6]);
                const GLsizei image_size =
                    static_cast<GLsizei>((*registers)[7]);
                // The 9th AAPCS argument arrives on the caller's stack at
                // [sp]; registers[8] is not an argument register.
                std::uint64_t data = 0;
                if (registers->stack_pointer == 0 ||
                    !memory_->Read(registers->stack_pointer, &data,
                                   sizeof(data))) {
                    SetError(dispatch_error,
                             "glCompressedTexSubImage2D stack data argument "
                             "is unreadable");
                    return false;
                }
                (*registers)[0] = 0;
                if (image_size < 0 || static_cast<std::uint64_t>(image_size) >
                                              kMaxBufferData) {
                    SetError(
                        dispatch_error,
                        "glCompressedTexSubImage2D size exceeds the "
                        "supported bound");
                    return false;
                }
                std::vector<std::uint8_t> host(
                    static_cast<std::size_t>(image_size));
                if (data != 0 && image_size != 0 &&
                    !memory_->Read(data, host.data(), host.size())) {
                    char detail[192];
                    std::snprintf(
                        detail, sizeof(detail),
                        "glCompressedTexSubImage2D data is unreadable "
                        "data=%llx size=%d fmt=%x %dx%d level=%d region=%s",
                        static_cast<unsigned long long>(data),
                        static_cast<int>(image_size),
                        static_cast<unsigned>(format),
                        static_cast<int>(width), static_cast<int>(height),
                        static_cast<int>(level),
                        memory_ != nullptr
                            ? memory_->DescribeRange(data, 1).c_str()
                            : "no-memory");
                    SetError(dispatch_error, detail);
                    return false;
                }
                if (!gate("glCompressedTexSubImage2D", dispatch_error))
                    return false;
                const std::uint32_t bound_texture =
                    BoundTextureForUploadTarget(target);
                const std::uint64_t level_key =
                    (static_cast<std::uint64_t>(bound_texture) << 32) |
                    static_cast<std::uint32_t>(level);
                const auto level_state =
                    texture_level_images_.find(level_key);
                if (bound_texture != 0 &&
                    level_state == texture_level_images_.end() &&
                    x_offset == 0 && y_offset == 0) {
                    // The level was never defined; a full-extent sub-image is
                    // equivalent to CompressedTexImage2D storage plus data.
                    glCompressedTexImage2D(target, level, format, width,
                                           height, 0, image_size,
                                           data != 0 ? host.data()
                                                     : nullptr);
                    if (!CheckGlCall(
                            "glCompressedTexSubImage2D-as-CompressedTexImage2D",
                            dispatch_error))
                        return false;
                    texture_level_images_.insert_or_assign(
                        level_key, TextureLevelImage{true, format, width,
                                                     height});
                    return true;
                }
                ClearGlErrors();
                glCompressedTexSubImage2D(target, level, x_offset, y_offset,
                                          width, height, format, image_size,
                                          data != 0 ? host.data() : nullptr);
                const GLenum sub_error = glGetError();
                if (sub_error == GL_NO_ERROR) return true;
                char detail[224];
                std::snprintf(
                    detail, sizeof(detail),
                    "glCompressedTexSubImage2D host GL error %x tex=%u "
                    "%dx%d@%d,%d level=%d fmt=%x size=%d defined=%s",
                    static_cast<unsigned>(sub_error), bound_texture,
                    static_cast<int>(width), static_cast<int>(height),
                    static_cast<int>(x_offset), static_cast<int>(y_offset),
                    static_cast<int>(level), static_cast<unsigned>(format),
                    static_cast<int>(image_size),
                    level_state == texture_level_images_.end()
                        ? "no"
                        : level_state->second.compressed
                              ? "compressed"
                              : "uncompressed");
                SetError(dispatch_error, detail);
                return false;
            },
            error))
        return false;
    if (!registry->Register(
            "glGenerateMipmap",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLenum target = static_cast<GLenum>((*registers)[0]);
                (*registers)[0] = 0;
                if (!gate("glGenerateMipmap", dispatch_error)) return false;
                glGenerateMipmap(target);
                return CheckGlCall("glGenerateMipmap", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glReleaseShaderCompiler",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                (*registers)[0] = 0;
                if (!gate("glReleaseShaderCompiler", dispatch_error))
                    return false;
                glReleaseShaderCompiler();
                return CheckGlCall("glReleaseShaderCompiler", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glShaderBinary",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLsizei count = static_cast<GLsizei>((*registers)[0]);
                const std::uint64_t shaders = (*registers)[1];
                const GLenum format = static_cast<GLenum>((*registers)[2]);
                const std::uint64_t binary = (*registers)[3];
                const GLsizei length = static_cast<GLsizei>((*registers)[4]);
                (*registers)[0] = 0;
                if (count < 0 || length < 0 ||
                    static_cast<std::uint64_t>(length) > kMaxBufferData) {
                    SetError(dispatch_error,
                             "glShaderBinary size exceeds the supported bound");
                    return false;
                }
                std::vector<GLuint> names(static_cast<std::size_t>(count), 0);
                if (count != 0 && shaders != 0 &&
                    !memory_->Read(shaders, names.data(),
                                   names.size() * sizeof(GLuint))) {
                    SetError(dispatch_error,
                             "glShaderBinary shaders are unreadable");
                    return false;
                }
                std::vector<std::uint8_t> host(
                    static_cast<std::size_t>(length));
                if (length != 0 && binary != 0 &&
                    !memory_->Read(binary, host.data(), host.size())) {
                    SetError(dispatch_error,
                             "glShaderBinary binary is unreadable");
                    return false;
                }
                if (!gate("glShaderBinary", dispatch_error)) return false;
                glShaderBinary(count, names.data(), format, host.data(),
                               length);
                return CheckGlCall("glShaderBinary", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glValidateProgram",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                (*registers)[0] = 0;
                if (!gate("glValidateProgram", dispatch_error)) return false;
                glValidateProgram(program);
                return CheckGlCall("glValidateProgram", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glDetachShader",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint program = static_cast<GLuint>((*registers)[0]);
                const GLuint shader = static_cast<GLuint>((*registers)[1]);
                (*registers)[0] = 0;
                if (!gate("glDetachShader", dispatch_error)) return false;
                glDetachShader(program, shader);
                if (!CheckGlCall("glDetachShader", dispatch_error))
                    return false;
                // Keep attachment bookkeeping in sync: without this a later
                // glDeleteShader sees a phantom program reference, parks the
                // name in delete_pending_shader_names_, and the host reuses
                // the freed name for the next glCreateShader — which then
                // trips the duplicate-name check.
                const auto attached = shader_program_names_.find(shader);
                if (attached != shader_program_names_.end()) {
                    attached->second.erase(program);
                    if (attached->second.empty())
                        shader_program_names_.erase(attached);
                }
                if (delete_pending_shader_names_.erase(shader) != 0 &&
                    shader_program_names_.find(shader) ==
                        shader_program_names_.end())
                    shader_names_.erase(shader);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform1iv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const GLsizei count = static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t value = (*registers)[2];
                (*registers)[0] = 0;
                if (count < 0 ||
                    static_cast<std::size_t>(count) > kMaxQueryElements) {
                    SetError(dispatch_error,
                             "glUniform1iv count exceeds the supported bound");
                    return false;
                }
                std::vector<GLint> host(static_cast<std::size_t>(count), 0);
                if (count != 0 && value != 0 &&
                    !memory_->Read(value, host.data(),
                                   host.size() * sizeof(GLint))) {
                    SetError(dispatch_error, "glUniform1iv value is unreadable");
                    return false;
                }
                if (!gate("glUniform1iv", dispatch_error)) return false;
                glUniform1iv(location, count, host.data());
                return CheckGlCall("glUniform1iv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform2f",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const float x = FloatArgument(*registers, 1);
                const float y = FloatArgument(*registers, 2);
                (*registers)[0] = 0;
                if (!gate("glUniform2f", dispatch_error)) return false;
                glUniform2f(location, x, y);
                return CheckGlCall("glUniform2f", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform2i",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const GLint x = static_cast<GLint>((*registers)[1]);
                const GLint y = static_cast<GLint>((*registers)[2]);
                (*registers)[0] = 0;
                if (!gate("glUniform2i", dispatch_error)) return false;
                glUniform2i(location, x, y);
                return CheckGlCall("glUniform2i", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform2iv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const GLsizei count = static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t value = (*registers)[2];
                (*registers)[0] = 0;
                if (count < 0 ||
                    static_cast<std::size_t>(count) * 2U > kMaxQueryElements) {
                    SetError(dispatch_error,
                             "glUniform2iv count exceeds the supported bound");
                    return false;
                }
                std::vector<GLint> host(static_cast<std::size_t>(count) * 2U,
                                        0);
                if (count != 0 && value != 0 &&
                    !memory_->Read(value, host.data(),
                                   host.size() * sizeof(GLint))) {
                    SetError(dispatch_error, "glUniform2iv value is unreadable");
                    return false;
                }
                if (!gate("glUniform2iv", dispatch_error)) return false;
                glUniform2iv(location, count, host.data());
                return CheckGlCall("glUniform2iv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform3f",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const float x = FloatArgument(*registers, 1);
                const float y = FloatArgument(*registers, 2);
                const float z = FloatArgument(*registers, 3);
                (*registers)[0] = 0;
                if (!gate("glUniform3f", dispatch_error)) return false;
                glUniform3f(location, x, y, z);
                return CheckGlCall("glUniform3f", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform3fv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const GLsizei count = static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t value = (*registers)[2];
                (*registers)[0] = 0;
                if (count < 0 ||
                    static_cast<std::size_t>(count) * 3U > kMaxQueryElements) {
                    SetError(dispatch_error,
                             "glUniform3fv count exceeds the supported bound");
                    return false;
                }
                std::vector<GLfloat> host(
                    static_cast<std::size_t>(count) * 3U, 0.0F);
                if (count != 0 && value != 0 &&
                    !memory_->Read(value, host.data(),
                                   host.size() * sizeof(GLfloat))) {
                    SetError(dispatch_error, "glUniform3fv value is unreadable");
                    return false;
                }
                if (!gate("glUniform3fv", dispatch_error)) return false;
                glUniform3fv(location, count, host.data());
                return CheckGlCall("glUniform3fv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform3i",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const GLint x = static_cast<GLint>((*registers)[1]);
                const GLint y = static_cast<GLint>((*registers)[2]);
                const GLint z = static_cast<GLint>((*registers)[3]);
                (*registers)[0] = 0;
                if (!gate("glUniform3i", dispatch_error)) return false;
                glUniform3i(location, x, y, z);
                return CheckGlCall("glUniform3i", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform3iv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const GLsizei count = static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t value = (*registers)[2];
                (*registers)[0] = 0;
                if (count < 0 ||
                    static_cast<std::size_t>(count) * 3U > kMaxQueryElements) {
                    SetError(dispatch_error,
                             "glUniform3iv count exceeds the supported bound");
                    return false;
                }
                std::vector<GLint> host(static_cast<std::size_t>(count) * 3U,
                                        0);
                if (count != 0 && value != 0 &&
                    !memory_->Read(value, host.data(),
                                   host.size() * sizeof(GLint))) {
                    SetError(dispatch_error, "glUniform3iv value is unreadable");
                    return false;
                }
                if (!gate("glUniform3iv", dispatch_error)) return false;
                glUniform3iv(location, count, host.data());
                return CheckGlCall("glUniform3iv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform4i",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const GLint x = static_cast<GLint>((*registers)[1]);
                const GLint y = static_cast<GLint>((*registers)[2]);
                const GLint z = static_cast<GLint>((*registers)[3]);
                const GLint w = static_cast<GLint>((*registers)[4]);
                (*registers)[0] = 0;
                if (!gate("glUniform4i", dispatch_error)) return false;
                glUniform4i(location, x, y, z, w);
                return CheckGlCall("glUniform4i", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniform4iv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const GLsizei count = static_cast<GLsizei>((*registers)[1]);
                const std::uint64_t value = (*registers)[2];
                (*registers)[0] = 0;
                if (count < 0 ||
                    static_cast<std::size_t>(count) * 4U > kMaxQueryElements) {
                    SetError(dispatch_error,
                             "glUniform4iv count exceeds the supported bound");
                    return false;
                }
                std::vector<GLint> host(static_cast<std::size_t>(count) * 4U,
                                        0);
                if (count != 0 && value != 0 &&
                    !memory_->Read(value, host.data(),
                                   host.size() * sizeof(GLint))) {
                    SetError(dispatch_error, "glUniform4iv value is unreadable");
                    return false;
                }
                if (!gate("glUniform4iv", dispatch_error)) return false;
                glUniform4iv(location, count, host.data());
                return CheckGlCall("glUniform4iv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniformMatrix2fv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const GLsizei count = static_cast<GLsizei>((*registers)[1]);
                const GLboolean transpose =
                    static_cast<GLboolean>((*registers)[2] & 0xffU);
                const std::uint64_t value = (*registers)[3];
                (*registers)[0] = 0;
                if (count < 0 ||
                    static_cast<std::size_t>(count) * 4U > kMaxQueryElements) {
                    SetError(
                        dispatch_error,
                        "glUniformMatrix2fv count exceeds the supported bound");
                    return false;
                }
                std::vector<GLfloat> host(static_cast<std::size_t>(count) * 4U,
                                          0.0F);
                if (count != 0 && value != 0 &&
                    !memory_->Read(value, host.data(),
                                   host.size() * sizeof(GLfloat))) {
                    SetError(dispatch_error,
                             "glUniformMatrix2fv value is unreadable");
                    return false;
                }
                if (!gate("glUniformMatrix2fv", dispatch_error)) return false;
                glUniformMatrix2fv(location, count, transpose, host.data());
                return CheckGlCall("glUniformMatrix2fv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glUniformMatrix3fv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLint location = static_cast<GLint>((*registers)[0]);
                const GLsizei count = static_cast<GLsizei>((*registers)[1]);
                const GLboolean transpose =
                    static_cast<GLboolean>((*registers)[2] & 0xffU);
                const std::uint64_t value = (*registers)[3];
                (*registers)[0] = 0;
                if (count < 0 ||
                    static_cast<std::size_t>(count) * 9U > kMaxQueryElements) {
                    SetError(
                        dispatch_error,
                        "glUniformMatrix3fv count exceeds the supported bound");
                    return false;
                }
                std::vector<GLfloat> host(static_cast<std::size_t>(count) * 9U,
                                          0.0F);
                if (count != 0 && value != 0 &&
                    !memory_->Read(value, host.data(),
                                   host.size() * sizeof(GLfloat))) {
                    SetError(dispatch_error,
                             "glUniformMatrix3fv value is unreadable");
                    return false;
                }
                if (!gate("glUniformMatrix3fv", dispatch_error)) return false;
                glUniformMatrix3fv(location, count, transpose, host.data());
                return CheckGlCall("glUniformMatrix3fv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttrib1f",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint index = static_cast<GLuint>((*registers)[0]);
                const float x = FloatArgument(*registers, 1);
                (*registers)[0] = 0;
                if (!gate("glVertexAttrib1f", dispatch_error)) return false;
                glVertexAttrib1f(index, x);
                return CheckGlCall("glVertexAttrib1f", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttrib1fv",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint index = static_cast<GLuint>((*registers)[0]);
                const std::uint64_t value = (*registers)[1];
                (*registers)[0] = 0;
                GLfloat host[1] = {0.0F};
                if (value != 0 &&
                    !memory_->Read(value, host, sizeof(host))) {
                    SetError(dispatch_error,
                             "glVertexAttrib1fv value is unreadable");
                    return false;
                }
                if (!gate("glVertexAttrib1fv", dispatch_error)) return false;
                glVertexAttrib1fv(index, host);
                return CheckGlCall("glVertexAttrib1fv", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttrib2f",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint index = static_cast<GLuint>((*registers)[0]);
                const float x = FloatArgument(*registers, 1);
                const float y = FloatArgument(*registers, 2);
                (*registers)[0] = 0;
                if (!gate("glVertexAttrib2f", dispatch_error)) return false;
                glVertexAttrib2f(index, x, y);
                return CheckGlCall("glVertexAttrib2f", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttrib3f",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint index = static_cast<GLuint>((*registers)[0]);
                const float x = FloatArgument(*registers, 1);
                const float y = FloatArgument(*registers, 2);
                const float z = FloatArgument(*registers, 3);
                (*registers)[0] = 0;
                if (!gate("glVertexAttrib3f", dispatch_error)) return false;
                glVertexAttrib3f(index, x, y, z);
                return CheckGlCall("glVertexAttrib3f", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "glVertexAttrib4f",
            [this, gate](Aarch64ImportRegistry::Registers* registers,
                          std::string* dispatch_error) {
                const GLuint index = static_cast<GLuint>((*registers)[0]);
                const float x = FloatArgument(*registers, 1);
                const float y = FloatArgument(*registers, 2);
                const float z = FloatArgument(*registers, 3);
                const float w = FloatArgument(*registers, 4);
                (*registers)[0] = 0;
                if (!gate("glVertexAttrib4f", dispatch_error)) return false;
                glVertexAttrib4f(index, x, y, z, w);
                return CheckGlCall("glVertexAttrib4f", dispatch_error);
            },
            error))
        return false;
    return true;
}

// Guest-visible EGL + ANativeWindow surface lifecycle. Single-threaded games
// (Candy) never call these: the owner thread keeps the one pre-made
// window surface + context bound at init. Unity's GfxDevice instead drives
// the whole EGL lifecycle on a dedicated render thread, so these handlers mint
// a shared host context + a pbuffer surface per calling thread and bind them
// there; Present() then read-backs + GDI-composites that surface to the
// window, so a pbuffer is sufficient (the ANGLE window surface's own present
// does not composite on this path regardless). EGL-level failures return
// EGL_FALSE/EGL_NO_* so the guest can see a real error rather than abort.
bool NativeGles::RegisterGuestSurfaceImports(Aarch64ImportRegistry* registry,
                                             std::string* error) {
    if (registry == nullptr) {
        SetError(error, "native GLES surface import registry is null");
        return false;
    }
    using Registers = Aarch64ImportRegistry::Registers;

    // Read a guest EGL_NONE-terminated EGLint attribute list. A null guest
    // address means no list.
    const auto read_attrib_list =
        [this](std::uint64_t attribute_address, std::vector<EGLint>* out,
               std::string* read_error) -> bool {
        out->clear();
        if (attribute_address == 0) return true;
        for (std::size_t i = 0; i < 64; ++i) {
            EGLint value = 0;
            if (!memory_->Read(attribute_address + i * sizeof(EGLint), &value,
                               sizeof(value))) {
                SetError(read_error,
                         "native GLES attribute list is unreadable");
                return false;
            }
            out->push_back(value);
            if (value == EGL_NONE) return true;
        }
        SetError(read_error, "native GLES attribute list is unterminated");
        return false;
    };
    // Write an EGLint to a guest out-pointer when it is non-null.
    const auto write_guest_int =
        [this](std::uint64_t address, EGLint value,
               std::string* write_error) -> bool {
        if (address == 0) return true;
        if (!memory_->Write(address, &value, sizeof(value))) {
            SetError(write_error, "native GLES integer output is invalid");
            return false;
        }
        return true;
    };
    // Write a guest-visible pointer handle to an out-pointer when non-null.
    const auto write_guest_ptr =
        [this](std::uint64_t address, std::uint64_t value,
               std::string* write_error) -> bool {
        if (address == 0) return true;
        if (!memory_->Write(address, &value, sizeof(value))) {
            SetError(write_error, "native GLES pointer output is invalid");
            return false;
        }
        return true;
    };
    // Publish a NUL-terminated string into guest memory (cached per site).
    const auto publish_string =
        [this](const char* text, std::uint64_t* cache,
               std::string* write_error) -> std::uint64_t {
        if (*cache != 0) return *cache;
        const std::size_t length = std::strlen(text) + 1;
        const std::uint64_t address = heap_->Allocate(length);
        if (address == 0 || !memory_->Write(address, text, length)) {
            if (address != 0) (void)heap_->Release(address, nullptr);
            SetError(write_error, "native GLES string publish failed");
            return 0;
        }
        *cache = address;
        return address;
    };
    // Create the render-thread surface (a pbuffer sized to the host window).
    const auto create_render_surface = [this]() -> EGLSurface {
        const EGLint attributes[] = {EGL_WIDTH,
                                     static_cast<EGLint>(surface_width_),
                                     EGL_HEIGHT,
                                     static_cast<EGLint>(surface_height_),
                                     EGL_NONE};
        return eglCreatePbufferSurface(display_, config_, attributes);
    };
    // The single guest ANativeWindow struct address, lazily allocated. The
    // guest only ever passes it back through eglCreateWindowSurface and the
    // ANativeWindow_* accessors, so the layout only needs to be a stable,
    // non-null token carrying the window geometry.
    const auto ensure_native_window =
        [this](std::string* alloc_error) -> std::uint64_t {
        if (guest_native_window_ != 0) return guest_native_window_;
        const std::uint64_t address = heap_->Allocate(64);
        if (address == 0) {
            SetError(alloc_error, "native GLES ANativeWindow alloc failed");
            return 0;
        }
        std::array<EGLint, 16> fields{};
        fields[0] = 0x41524D52;  // 'ARMR' magic
        fields[1] = static_cast<EGLint>(surface_width_);
        fields[2] = static_cast<EGLint>(surface_height_);
        fields[3] = 1;  // WINDOW_FORMAT_RGBA_8888
        if (!memory_->Write(address, fields.data(),
                            fields.size() * sizeof(EGLint))) {
            (void)heap_->Release(address, nullptr);
            SetError(alloc_error, "native GLES ANativeWindow init failed");
            return 0;
        }
        guest_native_window_ = address;
        return address;
    };

    if (!registry->Register(
            "eglGetDisplay",
            [this](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] =
                    reinterpret_cast<std::uint64_t>(display_);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglGetCurrentDisplay",
            [this](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] =
                    reinterpret_cast<std::uint64_t>(eglGetCurrentDisplay());
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglInitialize",
            [this, write_guest_int](Registers* registers,
                                    std::string* dispatch_error) {
                if (!write_guest_int((*registers)[1], 1, dispatch_error) ||
                    !write_guest_int((*registers)[2], 4, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglGetError",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] =
                    static_cast<std::uint64_t>(eglGetError());
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglQueryString",
            [this, publish_string](Registers* registers,
                                   std::string* dispatch_error) {
                const EGLint name = static_cast<EGLint>((*registers)[1]);
                const char* text = "ARMR";
                std::uint64_t* cache = &guest_egl_vendor_string_;
                if (name == EGL_VERSION) {
                    text = "1.5 ARMR";
                    cache = &guest_egl_version_string_;
                } else if (name == EGL_EXTENSIONS) {
                    text = "EGL_KHR_fence_sync EGL_KHR_cl_event2 "
                           "EGL_ANDROID_native_fence_sync";
                    cache = &guest_egl_extensions_string_;
                } else if (name == EGL_CLIENT_APIS) {
                    text = "OpenGL_ES";
                    cache = &guest_egl_apis_string_;
                }
                const std::uint64_t address =
                    publish_string(text, cache, dispatch_error);
                if (address == 0) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = address;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglBindAPI",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglChooseConfig",
            [this, write_guest_int, write_guest_ptr](
                Registers* registers, std::string* dispatch_error) {
                const std::uint64_t configs = (*registers)[2];
                const EGLint config_size = static_cast<EGLint>((*registers)[3]);
                const std::uint64_t num_config = (*registers)[4];
                if (configs != 0 && config_size > 0 &&
                    !write_guest_ptr(
                        configs,
                        reinterpret_cast<std::uint64_t>(config_),
                        dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                if (!write_guest_int(num_config, configs != 0 ? 1 : 1,
                                     dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglGetConfigs",
            [this, write_guest_int, write_guest_ptr](
                Registers* registers, std::string* dispatch_error) {
                const std::uint64_t configs = (*registers)[1];
                const EGLint config_size = static_cast<EGLint>((*registers)[2]);
                const std::uint64_t num_config = (*registers)[3];
                if (configs != 0 && config_size > 0 &&
                    !write_guest_ptr(
                        configs,
                        reinterpret_cast<std::uint64_t>(config_),
                        dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                if (!write_guest_int(num_config, 1, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglGetConfigAttrib",
            [this, write_guest_int](Registers* registers,
                                    std::string* dispatch_error) {
                const EGLint attribute = static_cast<EGLint>((*registers)[2]);
                const std::uint64_t value_out = (*registers)[3];
                EGLint value = 0;
                if (eglGetConfigAttrib(display_, config_, attribute,
                                       &value) != EGL_TRUE) {
                    (*registers)[0] = EGL_FALSE;
                    return true;
                }
                if (!write_guest_int(value_out, value, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglCreateContext",
            [this, read_attrib_list](Registers* registers,
                                     std::string* dispatch_error) {
                if (!initialized_ || shutdown_ ||
                    display_ == EGL_NO_DISPLAY || config_ == nullptr) {
                    (*registers)[0] = 0;
                    return true;
                }
                std::vector<EGLint> attributes;
                if (!read_attrib_list((*registers)[3], &attributes,
                                      dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                const std::uint64_t share_raw = (*registers)[2];
                const EGLContext share =
                    (share_raw != 0 &&
                     share_raw !=
                         reinterpret_cast<std::uint64_t>(EGL_NO_CONTEXT))
                        ? reinterpret_cast<EGLContext>(share_raw)
                        : rendering_context_;
                EGLContext context = eglCreateContext(
                    display_, config_, share,
                    attributes.empty() ? nullptr : attributes.data());
                if (context == EGL_NO_CONTEXT) {
                    const EGLint es2[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                          EGL_NONE};
                    context = eglCreateContext(display_, config_, share, es2);
                }
                if (context == EGL_NO_CONTEXT) {
                    (*registers)[0] = 0;
                    return true;
                }
                (*registers)[0] = reinterpret_cast<std::uint64_t>(context);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglCreateWindowSurface",
            [this, create_render_surface](Registers* registers,
                                          std::string* dispatch_error) {
                (void)dispatch_error;
                const std::uint64_t window_token = (*registers)[2];
                if (!initialized_ || shutdown_ ||
                    display_ == EGL_NO_DISPLAY) {
                    (*registers)[0] = 0;
                    return true;
                }
                // A null/other window token is tolerated, but only our
                // ANativeWindow produces a renderable surface.
                if (window_token != guest_native_window_ &&
                    guest_native_window_ != 0) {
                    (*registers)[0] = reinterpret_cast<std::uint64_t>(
                        EGL_NO_SURFACE);
                    return true;
                }
                const EGLSurface surface = create_render_surface();
                if (surface == EGL_NO_SURFACE) {
                    (*registers)[0] = reinterpret_cast<std::uint64_t>(
                        EGL_NO_SURFACE);
                    return true;
                }
                (*registers)[0] = reinterpret_cast<std::uint64_t>(surface);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglCreatePbufferSurface",
            [this, create_render_surface](Registers* registers,
                                          std::string* dispatch_error) {
                (void)dispatch_error;
                if (!initialized_ || shutdown_ ||
                    display_ == EGL_NO_DISPLAY) {
                    (*registers)[0] = 0;
                    return true;
                }
                const EGLSurface surface = create_render_surface();
                (*registers)[0] = reinterpret_cast<std::uint64_t>(surface);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglMakeCurrent",
            [this](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                const std::uint64_t draw_raw = (*registers)[1];
                const std::uint64_t read_raw = (*registers)[2];
                const std::uint64_t context_raw = (*registers)[3];
                const std::uint32_t tid = GetCurrentThreadId();
                if (!initialized_ || shutdown_ ||
                    display_ == EGL_NO_DISPLAY) {
                    (*registers)[0] = EGL_FALSE;
                    return true;
                }
                if (context_raw == 0 ||
                    context_raw ==
                        reinterpret_cast<std::uint64_t>(EGL_NO_CONTEXT)) {
                    (void)eglMakeCurrent(display_, EGL_NO_SURFACE,
                                         EGL_NO_SURFACE, EGL_NO_CONTEXT);
                    std::scoped_lock lock(guest_egl_mutex_);
                    guest_render_contexts_.erase(tid);
                    guest_render_surfaces_.erase(tid);
                    (*registers)[0] = EGL_TRUE;
                    return true;
                }
                const EGLContext context =
                    reinterpret_cast<EGLContext>(context_raw);
                EGLSurface draw = reinterpret_cast<EGLSurface>(draw_raw);
                EGLSurface read = reinterpret_cast<EGLSurface>(read_raw);
                // Owner thread binding the pre-made window pair.
                if (tid == owner_thread_id_ &&
                    context == rendering_context_) {
                    if (draw == EGL_NO_SURFACE ||
                        draw == reinterpret_cast<EGLSurface>(0))
                        draw = surface_;
                    if (read == EGL_NO_SURFACE ||
                        read == reinterpret_cast<EGLSurface>(0))
                        read = surface_;
                    if (eglMakeCurrent(display_, draw, read,
                                       rendering_context_) != EGL_TRUE) {
                        (*registers)[0] = EGL_FALSE;
                        return true;
                    }
                    (*registers)[0] = EGL_TRUE;
                    return true;
                }
                // Render thread: bind the caller-created surface + shared ctx.
                if (eglMakeCurrent(display_, draw, read, context) !=
                    EGL_TRUE) {
                    (*registers)[0] = EGL_FALSE;
                    return true;
                }
                ClearGlErrors();
                glViewport(0, 0, static_cast<GLsizei>(surface_width_),
                           static_cast<GLsizei>(surface_height_));
                {
                    std::scoped_lock lock(guest_egl_mutex_);
                    guest_render_contexts_[tid] = context;
                    guest_render_surfaces_[tid] = draw;
                }
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglGetCurrentContext",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] =
                    reinterpret_cast<std::uint64_t>(eglGetCurrentContext());
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglGetCurrentSurface",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                const EGLint readdraw = static_cast<EGLint>((*registers)[0]);
                (*registers)[0] = reinterpret_cast<std::uint64_t>(
                    eglGetCurrentSurface(readdraw));
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglSwapBuffers",
            [this](Registers* registers, std::string* dispatch_error) {
                if (!is_current()) {
                    (*registers)[0] = EGL_FALSE;
                    return true;
                }
                (*registers)[0] = EGL_TRUE;
                return Present("eglSwapBuffers", dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "eglSwapInterval",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                const EGLint interval = static_cast<EGLint>((*registers)[1]);
                (void)eglSwapInterval(eglGetCurrentDisplay(), interval);
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglQuerySurface",
            [this, write_guest_int](Registers* registers,
                                    std::string* dispatch_error) {
                const EGLSurface surface =
                    reinterpret_cast<EGLSurface>((*registers)[1]);
                const EGLint attribute = static_cast<EGLint>((*registers)[2]);
                const std::uint64_t value_out = (*registers)[3];
                EGLint value = 0;
                if (attribute == EGL_WIDTH)
                    value = static_cast<EGLint>(surface_width_);
                else if (attribute == EGL_HEIGHT)
                    value = static_cast<EGLint>(surface_height_);
                else if (surface != EGL_NO_SURFACE &&
                         eglQuerySurface(display_, surface, attribute,
                                         &value) != EGL_TRUE) {
                    (*registers)[0] = EGL_FALSE;
                    return true;
                }
                if (!write_guest_int(value_out, value, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglQueryContext",
            [write_guest_int](Registers* registers,
                              std::string* dispatch_error) {
                const EGLint attribute = static_cast<EGLint>((*registers)[2]);
                const std::uint64_t value_out = (*registers)[3];
                const EGLint value =
                    (attribute == EGL_CONTEXT_CLIENT_VERSION) ? 2 : 0;
                if (!write_guest_int(value_out, value, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglDestroySurface",
            [this](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                const EGLSurface surface =
                    reinterpret_cast<EGLSurface>((*registers)[1]);
                if (surface != EGL_NO_SURFACE && surface != surface_)
                    (void)eglDestroySurface(display_, surface);
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglDestroyContext",
            [this](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                const EGLContext context =
                    reinterpret_cast<EGLContext>((*registers)[1]);
                if (context != EGL_NO_CONTEXT && context != rendering_context_)
                    (void)eglDestroyContext(display_, context);
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglReleaseThread",
            [this](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (void)eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                     EGL_NO_CONTEXT);
                const std::uint32_t tid = GetCurrentThreadId();
                std::scoped_lock lock(guest_egl_mutex_);
                const auto ctx = guest_render_contexts_.find(tid);
                if (ctx != guest_render_contexts_.end() &&
                    ctx->second != EGL_NO_CONTEXT &&
                    ctx->second != rendering_context_) {
                    (void)eglDestroyContext(display_, ctx->second);
                }
                const auto surf = guest_render_surfaces_.find(tid);
                if (surf != guest_render_surfaces_.end() &&
                    surf->second != EGL_NO_SURFACE &&
                    surf->second != surface_) {
                    (void)eglDestroySurface(display_, surf->second);
                }
                guest_render_contexts_.erase(tid);
                guest_render_surfaces_.erase(tid);
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglTerminate",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglWaitClient",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (void)eglWaitClient();
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglWaitGL",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "eglWaitNative",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = EGL_TRUE;
                return true;
            },
            error))
        return false;
    // ANativeWindow surface handoff (libandroid). ANativeWindow_fromSurface
    // returns the single host-backed guest window token; the accessors report
    // its geometry. The guest only ever routes the token back through
    // eglCreateWindowSurface.
    if (!registry->Register(
            "ANativeWindow_fromSurface",
            [this, ensure_native_window](Registers* registers,
                                         std::string* dispatch_error) {
                const std::uint64_t window =
                    ensure_native_window(dispatch_error);
                if (window == 0) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = window;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ANativeWindow_acquire",
            [ensure_native_window](Registers* registers,
                                   std::string* dispatch_error) {
                (void)ensure_native_window(dispatch_error);
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ANativeWindow_release",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ANativeWindow_getWidth",
            [this](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(surface_width_));
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ANativeWindow_getHeight",
            [this](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(surface_height_));
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ANativeWindow_getFormat",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 1;  // WINDOW_FORMAT_RGBA_8888
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ANativeWindow_setBuffersGeometry",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ANativeWindow_query",
            [this, write_guest_int](Registers* registers,
                                    std::string* dispatch_error) {
                const EGLint what = static_cast<EGLint>((*registers)[1]);
                const std::uint64_t value_out = (*registers)[2];
                EGLint value = 0;
                if (what == 0 /*NATIVE_WINDOW_WIDTH*/)
                    value = static_cast<EGLint>(surface_width_);
                else if (what == 1 /*NATIVE_WINDOW_HEIGHT*/)
                    value = static_cast<EGLint>(surface_height_);
                else if (what == 3 /*NATIVE_WINDOW_FORMAT*/)
                    value = 1;
                if (!write_guest_int(value_out, value, dispatch_error)) {
                    (*registers)[0] = 0;
                    return false;
                }
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ANativeWindow_lock",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ANativeWindow_unlockAndPost",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    // NDK sensor API (libandroid). Some Unity builds query the sensor
    // manager during startup; reporting "no sensors present" is a
    // legal device state, so the manager token is a nonzero sentinel while
    // every enumeration answers empty.
    if (!registry->Register(
            "ASensorManager_getInstance",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 1;  // nonzero opaque manager token
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensorManager_getSensorList",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;  // zero sensors enumerated
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensorManager_getDefaultSensor",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;  // no sensor of that type
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensorManager_createEventQueue",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;  // queue unavailable -> sensor-less path
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensorManager_destroyEventQueue",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensorEventQueue_enableSensor",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(-1));
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensorEventQueue_disableSensor",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensorEventQueue_setEventRate",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensorEventQueue_hasEvents",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;  // queue always empty
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensorEventQueue_getEvents",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;  // no events read
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensor_getMinDelay",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensor_getName",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;  // null name
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensor_getResolution",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensor_getType",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ASensor_getVendor",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;  // null vendor
                return true;
            },
            error))
        return false;
    // NDK looper API (libandroid). The Choreographer/sensor threads poll a
    // looper for wakeups; reporting a timeout after a short real wait keeps
    // the guest polling loop honest without parking the jit forever.
    if (!registry->Register(
            "ALooper_prepare",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 1;  // nonzero opaque looper token
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ALooper_forThread",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 1;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ALooper_acquire",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ALooper_release",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ALooper_wake",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "ALooper_pollOnce",
            [](Registers* registers, std::string* dispatch_error) {
                (void)dispatch_error;
                const std::int64_t timeout_ms =
                    static_cast<std::int64_t>((*registers)[0]);
                if (timeout_ms != 0) {
                    const std::uint32_t wait_ms =
                        (timeout_ms < 0 || timeout_ms > 4)
                            ? 4
                            : static_cast<std::uint32_t>(timeout_ms);
                    Sleep(wait_ms);
                }
                (*registers)[0] = static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(-3));  // ALOOPER_POLL_TIMEOUT
                return true;
            },
            error))
        return false;
    return true;
}

bool NativeGles::MakeCurrent(std::string* error) {
    if (shutdown_ || display_ == EGL_NO_DISPLAY ||
        surface_ == EGL_NO_SURFACE ||
        rendering_context_ == EGL_NO_CONTEXT) {
        SetError(error, "native GLES context is unavailable");
        return false;
    }
    if (owner_thread_id_ != GetCurrentThreadId()) {
        SetError(error, "native GLES context belongs to a different thread");
        return false;
    }
    if (eglMakeCurrent(display_, surface_, surface_, rendering_context_) !=
        EGL_TRUE) {
        SetError(error,
                 "native GLES could not make its ANGLE context current");
        return false;
    }
    // ANGLE re-initializes the viewport on every context bind; on Android
    // the GLThread's surface sizing would set it next, so restore it here
    // or the guest draws clip to a 0x0 viewport.
    ClearGlErrors();
    glViewport(0, 0, static_cast<GLsizei>(surface_width_),
               static_cast<GLsizei>(surface_height_));
    (void)CheckGlCall("viewport restore on make-current", error);
    return true;
}

bool NativeGles::ClearCurrent(std::string* error) {
    if (shutdown_ || display_ == EGL_NO_DISPLAY ||
        rendering_context_ == EGL_NO_CONTEXT) {
        SetError(error, "native GLES context is unavailable");
        return false;
    }
    if (owner_thread_id_ != GetCurrentThreadId() || !is_current()) {
        SetError(error, "native GLES context is not current on its owner thread");
        return false;
    }
    if (eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT) != EGL_TRUE) {
        SetError(error, "native GLES could not clear its current context");
        return false;
    }
    return true;
}

bool NativeGles::Show(std::string* error) {
    if (!initialized_ || shutdown_ || window_ == nullptr) {
        SetError(error, "native GLES window is unavailable");
        return false;
    }
    if (owner_thread_id_ != GetCurrentThreadId()) {
        SetError(error, "native GLES window belongs to a different thread");
        return false;
    }
    if (hosted_) {
        // Surface the host shell so the display child can become visible.
        HWND parent = GetParent(window_);
        if (parent != nullptr) ShowWindow(parent, SW_SHOW);
    }
    ShowWindow(window_, SW_SHOW);
    if (!UpdateWindow(window_) || !IsWindowVisible(window_)) {
        SetError(error, "native GLES could not show its Win32 window");
        return false;
    }
    return true;
}

bool NativeGles::RequestClose(std::string* error) {
    if (!initialized_ || shutdown_ || window_ == nullptr ||
        window_state_ == nullptr) {
        SetError(error, "native GLES close request state is invalid");
        return false;
    }
    if (owner_thread_id_ != GetCurrentThreadId()) {
        SetError(error,
                 "native GLES close request requires its owner thread");
        return false;
    }
    (void)SendMessageW(window_, WM_CLOSE, 0, 0);
    if (!window_state_->close_requested) {
        SetError(error,
                 "native GLES close request was not accepted by its Win32 window");
        return false;
    }
    return true;
}

bool NativeGles::window_close_requested() const {
    return hosted_ && window_state_ != nullptr &&
           window_state_->close_requested;
}

bool NativeGles::PumpOwnerMessages(std::string* error) {
    if (!initialized_ || shutdown_ || window_ == nullptr ||
        window_state_ == nullptr) {
        SetError(error, "native GLES owner message pump state is invalid");
        return false;
    }
    if (owner_thread_id_ != GetCurrentThreadId()) {
        SetError(error,
                 "native GLES owner message pump requires its owner thread");
        return false;
    }
    MSG message{};
    // Pump every message on the owner thread's queue. In hosted mode the
    // display child, its fixed render-target host, and the ARMR shell chrome
    // (title bar, status strip, control pill, popovers) all live on this one
    // thread; a window-filtered peek would starve the chrome. In standalone
    // mode the only window is window_ itself, so this is equivalent.
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            CancelActivePointer(window_, window_state_.get());
            window_state_->close_requested = true;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    // WM_QUIT is a thread message and is not reliably returned by the HWND-
    // filtered loop above. Drain it after pending instance messages so a
    // queued pointer-down is followed by the required cancel before close.
    while (PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
        CancelActivePointer(window_, window_state_.get());
        window_state_->close_requested = true;
    }
    if (!window_state_->input_error.empty()) {
        SetError(error, window_state_->input_error);
        return false;
    }
    return true;
}

bool NativeGles::PumpEvents(
    bool* close_requested,
    std::vector<NativePointerEvent>* pointer_events,
    std::string* error) {
    if (!initialized_ || shutdown_ || window_ == nullptr ||
        window_state_ == nullptr || close_requested == nullptr ||
        pointer_events == nullptr) {
        SetError(error, "native GLES event pump arguments are invalid");
        return false;
    }
    if (owner_thread_id_ != GetCurrentThreadId()) {
        SetError(error, "native GLES event pump requires its owner thread");
        return false;
    }
    *close_requested = false;
    pointer_events->clear();
    if (!PumpOwnerMessages(error)) return false;
    *close_requested = window_state_->close_requested;
    *pointer_events = window_state_->pointer_events;
    return true;
}

bool NativeGles::AcknowledgePointerEvent(
    const NativePointerEvent& event, std::string* error) {
    if (!initialized_ || shutdown_ || window_ == nullptr ||
        window_state_ == nullptr) {
        SetError(error,
                 "native GLES pointer acknowledgement state is invalid");
        return false;
    }
    if (owner_thread_id_ != GetCurrentThreadId()) {
        SetError(error,
                 "native GLES pointer acknowledgement requires its owner "
                 "thread");
        return false;
    }
    if (window_state_->pointer_events.empty()) {
        SetError(error,
                 "native GLES pointer acknowledgement queue is empty");
        return false;
    }
    const NativePointerEvent& pending =
        window_state_->pointer_events.front();
    if (pending.action != event.action || pending.x != event.x ||
        pending.y != event.y ||
        pending.client_width != event.client_width ||
        pending.client_height != event.client_height ||
        pending.enqueued_ns != event.enqueued_ns) {
        SetError(error,
                 "native GLES pointer acknowledgement is out of order");
        return false;
    }
    window_state_->pointer_events.erase(
        window_state_->pointer_events.begin());
    return true;
}

bool NativeGles::DrainKeyEvents(std::vector<NativeKeyEvent>* key_events,
                                std::string* error) {
    if (!initialized_ || shutdown_ || window_ == nullptr ||
        window_state_ == nullptr || key_events == nullptr) {
        SetError(error, "native GLES key drain state is invalid");
        return false;
    }
    if (owner_thread_id_ != GetCurrentThreadId()) {
        SetError(error,
                 "native GLES key drain requires its owner thread");
        return false;
    }
    *key_events = window_state_->key_events;
    window_state_->key_events.clear();
    return true;
}

std::size_t NativeGles::pending_pointer_event_count() const {
    return window_state_ != nullptr ? window_state_->pointer_events.size()
                                    : 0;
}

bool NativeGles::Present(std::string_view source, std::string* error) {
    constexpr std::size_t kChannels = 4;
    constexpr std::size_t kMaximumReadback = 64U * 1024U * 1024U;
    if (!initialized_ || shutdown_ || !is_current() || source.empty() ||
        source.size() > 64 || source.find_first_of("\r\n") !=
                                std::string_view::npos) {
        SetError(error, "native GLES present arguments are invalid");
        return false;
    }
    std::size_t sampled = 0;
    std::size_t lit = 0;
    std::size_t nonzero_alpha = 0;
    std::uint32_t maximum_rgb = 0;
    const std::size_t next_present = present_count_ + 1;
    std::size_t capture_present = 0;
    bool capture_requested = false;
    if (!RequestedPresentCapture(&capture_present, &capture_requested,
                                 error))
        return false;
    const std::size_t capture_every = RequestedPresentCaptureEvery();
    const bool capture_this_present =
        (capture_requested && capture_present == next_present) ||
        (capture_every != 0 && (next_present % capture_every) == 0);
    const bool sample_present =
        (next_present <= 3 || (next_present % 120) == 0) ||
        capture_this_present;
    const bool read_present_fb0 =
        (sample_present || gdi_present_) && bound_framebuffer_ == 0;
    const std::size_t width = static_cast<std::size_t>(surface_width_);
    const std::size_t height = static_cast<std::size_t>(surface_height_);
    if (read_present_fb0 &&
        (surface_width_ <= 0 || surface_height_ <= 0 ||
         width > kMaximumReadback / kChannels ||
         height > kMaximumReadback / (width * kChannels))) {
        SetError(error, "native GLES present surface is out of range");
        return false;
    }
    // GDI present path: read the backbuffer in BGRA when the driver offers
    // it (ANGLE's D3D11 backbuffer is natively BGRA) so the blit needs no
    // per-pixel swizzle. Falls back to RGBA + an in-place host swizzle.
    if (gdi_present_ && bound_framebuffer_ == 0) {
        try {
            present_bgra_.resize(width * height * kChannels);
        } catch (const std::bad_alloc&) {
            SetError(error, "native GLES present readback allocation failed");
            return false;
        }
        bool got_bgra = false;
        if (bgra_read_supported_) {
            ClearGlErrors();
            glReadPixels(0, 0, static_cast<GLsizei>(surface_width_),
                         static_cast<GLsizei>(surface_height_), GL_BGRA_EXT,
                         GL_UNSIGNED_BYTE, present_bgra_.data());
            got_bgra = glGetError() == GL_NO_ERROR;
            if (!got_bgra) {
                bgra_read_supported_ = false;
                ClearGlErrors();
            }
        }
        if (!got_bgra) {
            glReadPixels(0, 0, static_cast<GLsizei>(surface_width_),
                         static_cast<GLsizei>(surface_height_), GL_RGBA,
                         GL_UNSIGNED_BYTE, present_bgra_.data());
            if (!CheckGlCall("glReadPixels(present probe)", error))
                return false;
            for (std::size_t i = 0; i < width * height; ++i) {
                const std::size_t s = i * kChannels;
                const std::uint8_t r = present_bgra_[s];
                present_bgra_[s] = present_bgra_[s + 2U];
                present_bgra_[s + 2U] = r;
                present_bgra_[s + 3U] = 0xFF;
            }
        }
        // In hosted mode the display child shares the shell with the floating
        // toolbar and popovers. device_context_ was acquired once at init, so
        // its WS_CLIPSIBLINGS clip is a stale snapshot that predates those
        // siblings; re-acquire the DC each present so the clip excludes the
        // currently-visible floating chrome and the blit cannot overpaint it.
        HDC blit_dc =
            hosted_
                ? GetDCEx(window_, nullptr, DCX_CLIPSIBLINGS)
                : device_context_;
        if (blit_dc != nullptr) {
            BlitPixelsToWindow(blit_dc, present_bgra_.data(),
                               static_cast<std::int32_t>(width),
                               static_cast<std::int32_t>(height));
            if (hosted_) ReleaseDC(window_, blit_dc);
        }
    }
    // Sample/capture path (rare): a separate RGBA readback so the lit/white
    // metrics and BMP dumps interpret channels correctly.
    const std::vector<std::uint8_t>* pixels = nullptr;
    if ((sample_present || capture_this_present) &&
        bound_framebuffer_ == 0) {
        try {
            present_pixels_.resize(width * height * kChannels);
        } catch (const std::bad_alloc&) {
            SetError(error, "native GLES present readback allocation failed");
            return false;
        }
        ClearGlErrors();
        glReadPixels(0, 0, static_cast<GLsizei>(surface_width_),
                     static_cast<GLsizei>(surface_height_), GL_RGBA,
                     GL_UNSIGNED_BYTE, present_pixels_.data());
        if (!CheckGlCall("glReadPixels(present probe)", error)) return false;
        pixels = &present_pixels_;
    }
    if (sample_present && pixels != nullptr) {
        ++present_readback_count_;
        sampled = width * height;
        std::size_t near_white = 0;
        for (std::size_t pixel = 0; pixel < pixels->size();
             pixel += kChannels) {
            const std::uint32_t red = (*pixels)[pixel];
            const std::uint32_t green = (*pixels)[pixel + 1];
            const std::uint32_t blue = (*pixels)[pixel + 2];
            const std::uint32_t rgb = red + green + blue;
            if (rgb > 12U) ++lit;
            if (red > 240U && green > 240U && blue > 240U) ++near_white;
            if ((*pixels)[pixel + 3] != 0) ++nonzero_alpha;
            if (rgb > maximum_rgb) maximum_rgb = rgb;
        }
        std::printf("ISOMR_PRESENT_WHITE near_white=%zu of=%zu\n",
                    near_white, sampled);
    }
    if (capture_this_present && pixels != nullptr &&
        !CaptureRequestedFrame(L"ISOMR_PRESENT_CAPTURE_BMP",
                               "guest-present", *pixels, width, height,
                               error))
        return false;
    const std::size_t draw_delta = draw_call_count_ - last_present_draw_count_;
    // Swap the surface bound on the calling thread: the owner thread presents
    // surface_ directly; a guest render thread presents its own surface.
    const EGLSurface swap_surface = eglGetCurrentSurface(EGL_DRAW);
    if (swap_surface != EGL_NO_SURFACE &&
        eglSwapBuffers(display_, swap_surface) != EGL_TRUE) {
        SetError(error, "native GLES eglSwapBuffers failed with EGL error " +
                            std::to_string(eglGetError()));
        return false;
    }
    ++present_count_;
    PaceAfterSwap();
    if (sampled != 0) {
        last_sampled_pixel_count_ = sampled;
        last_lit_pixel_count_ = lit;
        last_nonzero_alpha_pixel_count_ = nonzero_alpha;
        last_max_rgb_ = maximum_rgb;
        if (lit != 0) {
            lit_pixel_observed_ = true;
            ++lit_present_count_;
            // Tell the host shell the first genuine frame is up so it can
            // drop the startup panel and show "Running". Post-only, no
            // synchronous coupling.
            if (hosted_ && lit_present_count_ == 1 &&
                window_state_ != nullptr &&
                window_state_->host_notify != nullptr) {
                PostMessageW(window_state_->host_notify, kArmrMsgFirstFrame,
                             0, 0);
            }
        }
    }
    last_present_draw_count_ = draw_call_count_;
    if (sample_present) {
        std::printf(
            "ISOMR_PRESENT_PROBE source=%.*s present=%zu draws=%zu "
            "draw-delta=%zu framebuffer=%u sampled=%zu lit=%zu alpha=%zu "
            "max-rgb=%u\n",
            static_cast<int>(source.size()), source.data(), present_count_,
            draw_call_count_, draw_delta, bound_framebuffer_, sampled, lit,
            nonzero_alpha, maximum_rgb);
    }
    return true;
}

bool NativeGles::is_current() const {
    if (!initialized_ || shutdown_ || eglGetCurrentDisplay() != display_)
        return false;
    const std::uint32_t tid = GetCurrentThreadId();
    const EGLContext context = eglGetCurrentContext();
    const EGLSurface draw = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface read = eglGetCurrentSurface(EGL_READ);
    if (tid == owner_thread_id_)
        return context == rendering_context_ && draw == surface_ &&
               read == surface_;
    // A guest render thread (Unity GfxDevice) bound its own shared context and
    // surface via eglMakeCurrent; accept that pair as current too.
    if (context == EGL_NO_CONTEXT || draw == EGL_NO_SURFACE || draw != read)
        return false;
    std::scoped_lock lock(guest_egl_mutex_);
    const auto context_it = guest_render_contexts_.find(tid);
    const auto surface_it = guest_render_surfaces_.find(tid);
    return context_it != guest_render_contexts_.end() &&
           context_it->second == context &&
           surface_it != guest_render_surfaces_.end() &&
           surface_it->second == draw;
}

bool NativeGles::window_visible() const {
    return initialized_ && !shutdown_ && window_ != nullptr &&
           IsWindowVisible(window_) != FALSE;
}

bool NativeGles::GetString(std::uint32_t name, std::uint64_t* result,
                           std::string* error) {
    constexpr std::uint32_t kGlVendor = 0x1f00;
    constexpr std::uint32_t kGlRenderer = 0x1f01;
    constexpr std::uint32_t kGlVersion = 0x1f02;
    constexpr std::uint32_t kGlExtensions = 0x1f03;
    constexpr std::uint32_t kGlShadingLanguageVersion = 0x8b8c;
    constexpr std::size_t kMaximumHostString = 64 * 1024;
    if (result == nullptr) {
        SetError(error, "native GLES string result is null");
        return false;
    }
    *result = 0;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glGetString requires the owned current native GPU context");
        return false;
    }
    std::size_t string_index = 0;
    GLenum host_name = GL_VENDOR;
    if (name == kGlRenderer) {
        string_index = 1;
        host_name = GL_RENDERER;
    } else if (name == kGlVersion) {
        string_index = 2;
        host_name = GL_VERSION;
    } else if (name == kGlShadingLanguageVersion) {
        string_index = 3;
        host_name = static_cast<GLenum>(kGlShadingLanguageVersion);
    } else if (name == kGlExtensions) {
        string_index = 4;
        host_name = GL_EXTENSIONS;
    } else if (name != kGlVendor) {
        (void)glGetString(static_cast<GLenum>(name));
        return true;
    }
    if (string_guest_addresses_[string_index] != 0) {
        *result = string_guest_addresses_[string_index];
        return true;
    }

    const GLubyte* host_string = glGetString(host_name);
    if (host_string == nullptr) {
        SetError(error, "host glGetString returned null for enum " +
                            std::to_string(name));
        return false;
    }
    const char* text = reinterpret_cast<const char*>(host_string);
    std::size_t length = 0;
    while (length < kMaximumHostString && text[length] != '\0') ++length;
    if (length == 0 || length == kMaximumHostString) {
        SetError(error,
                 "host glGetString returned an invalid string for enum " +
                     std::to_string(name));
        return false;
    }
    const std::uint64_t address = heap_->Allocate(length + 1);
    if (address == 0 || !heap_->Write(address, text, length + 1)) {
        if (address != 0) {
            std::string ignored;
            (void)heap_->Release(address, &ignored);
        }
        SetError(error,
                 "native GLES could not copy host string into guest memory");
        return false;
    }
    string_guest_addresses_[string_index] = address;
    *result = address;
    return true;
}

bool NativeGles::GetStringIndexed(std::uint32_t name, std::uint32_t index,
                                  std::uint64_t* result, std::string* error) {
    constexpr std::uint32_t kGlExtensions = 0x1f03;
    if (result == nullptr) {
        SetError(error, "native GLES glGetStringi result is null");
        return false;
    }
    *result = 0;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glGetStringi requires the owned current native GPU context");
        return false;
    }
    if (name != kGlExtensions) {
        // Only GL_EXTENSIONS is a valid ES3 glGetStringi target; surface the
        // real host error for any other pname so the caller sees it.
        (void)glGetStringi(static_cast<GLenum>(name), index);
        return true;
    }
    const auto cached = indexed_string_guest_addresses_.find(index);
    if (cached != indexed_string_guest_addresses_.end()) {
        *result = cached->second;
        return true;
    }
    const GLubyte* host_extensions = glGetString(GL_EXTENSIONS);
    if (host_extensions == nullptr) {
        SetError(error,
                 "host glGetString returned null for GL_EXTENSIONS");
        return false;
    }
    const std::string_view all =
        reinterpret_cast<const char*>(host_extensions);
    // GL_EXTENSIONS is a single space-delimited token list; select the index-th
    // token. An out-of-range index is an error the caller detects via NULL.
    std::size_t cursor = 0;
    std::string_view selected;
    for (std::uint32_t i = 0; i <= index; ++i) {
        while (cursor < all.size() && all[cursor] == ' ') ++cursor;
        if (cursor >= all.size()) break;
        const std::size_t end = all.find(' ', cursor);
        const std::size_t stop =
            end == std::string_view::npos ? all.size() : end;
        if (i == index) selected = all.substr(cursor, stop - cursor);
        cursor = stop;
    }
    if (selected.empty()) {
        // Out-of-range index: return NULL and leave the host GL error pending.
        (void)glGetStringi(static_cast<GLenum>(name), index);
        return true;
    }
    const std::uint64_t address = heap_->Allocate(selected.size() + 1);
    if (address == 0 ||
        !heap_->Write(address, selected.data(), selected.size()) ||
        !heap_->Write(address + selected.size(), "\0", 1)) {
        if (address != 0) {
            std::string ignored;
            (void)heap_->Release(address, &ignored);
        }
        SetError(error,
                 "native GLES could not copy indexed string into guest memory");
        return false;
    }
    indexed_string_guest_addresses_.emplace(index, address);
    *result = address;
    return true;
}

bool NativeGles::GetInteger(std::uint32_t name, std::uint64_t output,
                            std::string* error) {
    constexpr std::uint32_t kGlMaxTextureUnits = 0x84d8;
    constexpr std::uint32_t kGlViewport = 0x0ba2;
    // ES3+ context queries that are invalid under our ES2 host context:
    // GL_MAJOR_VERSION, GL_MINOR_VERSION, GL_NUM_EXTENSIONS. Pass them through
    // so the host raises GL_INVALID_ENUM and the caller falls back to the ES2
    // glGetString(GL_VERSION)/glGetString(GL_EXTENSIONS) path after glGetError.
    constexpr std::uint32_t kGlMajorVersion = 0x821b;
    constexpr std::uint32_t kGlMinorVersion = 0x821c;
    constexpr std::uint32_t kGlNumExtensions = 0x821d;
    // GLES1 fixed-function limits have no host equivalent; the PvZ
    // renderer probes them to size fallback loops, and zero means the
    // fixed-function feature is unavailable.
    constexpr std::uint32_t kGlMaxLights = 0x0d31;
    constexpr std::uint32_t kGlMaxClipPlanes = 0x0d32;
    constexpr std::uint32_t kGlMaxModelviewStackDepth = 0x0d36;
    constexpr std::uint32_t kGlMaxProjectionStackDepth = 0x0d38;
    constexpr std::uint32_t kGlMaxTextureStackDepth = 0x0d39;
    constexpr std::uint32_t kGlMaxPaletteMatricesOes = 0x896a;
    constexpr std::uint32_t kGlMaxVertexUnitsOes = 0x8654;
    if (!initialized_ || shutdown_ ||
        eglGetCurrentContext() == EGL_NO_CONTEXT) {
        SetError(error,
                 "glGetIntegerv requires a context current on the calling "
                 "thread");
        return false;
    }
    if (name == kGlMajorVersion || name == kGlMinorVersion ||
        name == kGlNumExtensions) {
        // Forward to the host so ANGLE records the real ES2 GL_INVALID_ENUM,
        // then capture it in pending_guest_gl_error_ so the caller's glGetError
        // surfaces it even if an intervening host call clears the host queue.
        GLint es3_value = 0;
        glGetIntegerv(static_cast<GLenum>(name), &es3_value);
        const GLenum es3_error = glGetError();
        if (es3_error != GL_NO_ERROR) pending_guest_gl_error_ = es3_error;
        if (output == 0 ||
            !memory_->Write(output, &es3_value, sizeof(es3_value))) {
            SetError(error,
                     "native GLES glGetIntegerv guest output is invalid");
            return false;
        }
        return true;
    }
    switch (name) {
        case kGlMaxTextureUnits:
        case kGlMaxLights:
        case kGlMaxClipPlanes:
        case kGlMaxModelviewStackDepth:
        case kGlMaxProjectionStackDepth:
        case kGlMaxTextureStackDepth:
        case kGlMaxPaletteMatricesOes:
        case kGlMaxVertexUnitsOes: {
            // Only the fixed-function texture-unit count is emulated with a
            // usable value; the remaining fixed-function stages report zero
            // (unsupported) so callers take their shader paths.
            const std::int32_t guest_value =
                name == kGlMaxTextureUnits ? 1 : 0;
            if (output == 0 ||
                !memory_->Write(output, &guest_value,
                                sizeof(guest_value))) {
                SetError(error,
                         "native GLES glGetIntegerv guest output is invalid");
                return false;
            }
            return true;
        }
        default:
            break;
    }
    // Pass every remaining pname through to the host ANGLE context. Real GLES2
    // semantics: a supported pname returns its value, an unsupported one raises
    // GL_INVALID_ENUM which the caller detects through glGetError. We treat a
    // host error as a soft failure — write zero and leave the error pending —
    // rather than a hard dispatch failure, so probing callers keep running.
    constexpr std::size_t kGuestSingleValue = sizeof(GLint);
    std::size_t value_count = 1;
    switch (name) {
        // Multi-value vector pnames.
        case kGlViewport:           // GL_VIEWPORT: x, y, w, h
        case 0x0c10:                // GL_SCISSOR_BOX
        case 0x0c22:                // GL_COLOR_CLEAR_VALUE
        case 0x0c23:                // GL_COLOR_WRITEMASK
        case 0x8005:                // GL_BLEND_COLOR
            value_count = 4;
            break;
        case 0x0b70:                // GL_DEPTH_RANGE
        case 0x846d:                // GL_ALIASED_POINT_SIZE_RANGE
        case 0x846e:                // GL_ALIASED_LINE_WIDTH_RANGE
        case 0x0d3a:                // GL_MAX_VIEWPORT_DIMS
            value_count = 2;
            break;
        default:
            value_count = 1;
            break;
    }
    GLint values[4] = {0, 0, 0, 0};
    ClearGlErrors();
    glGetIntegerv(static_cast<GLenum>(name), values);
    const GLenum host_error = glGetError();
    if (host_error != GL_NO_ERROR) {
        // Unsupported/invalid pname on this context: report zero and record the
        // host error so the guest's own glGetError surfaces it. This matches
        // real GLES2 behavior for ES3/extension probes.
        pending_guest_gl_error_ = host_error;
        GLint zero = 0;
        if (output == 0 || !memory_->Write(output, &zero, sizeof(zero))) {
            SetError(error,
                     "native GLES glGetIntegerv guest output is invalid");
            return false;
        }
        return true;
    }
    if (output == 0 ||
        !memory_->Write(output, values, value_count * kGuestSingleValue)) {
        SetError(error, "native GLES glGetIntegerv guest output is invalid");
        return false;
    }
    return true;
}

bool NativeGles::GenerateTextures(std::uint64_t count, std::uint64_t output,
                                  std::string* error) {
    constexpr std::uint64_t kMaximumCount = 4096;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glGenTextures requires the owned current native GPU context");
        return false;
    }
    if (count > kMaximumCount ||
        count > static_cast<std::uint64_t>(
                    std::numeric_limits<GLsizei>::max())) {
        SetError(error, "native GLES glGenTextures count is out of range");
        return false;
    }
    if (count == 0) return true;
    if (output == 0) {
        SetError(error, "native GLES glGenTextures guest output is invalid");
        return false;
    }
    std::vector<GLuint> names(static_cast<std::size_t>(count));
    ClearGlErrors();
    glGenTextures(static_cast<GLsizei>(count), names.data());
    if (!CheckGlCall("glGenTextures", error)) {
        glDeleteTextures(static_cast<GLsizei>(names.size()), names.data());
        return false;
    }
    for (const GLuint name : names) {
        if (name == 0 || texture_names_.contains(name)) {
            glDeleteTextures(static_cast<GLsizei>(names.size()), names.data());
            SetError(error,
                     "host glGenTextures returned an invalid texture name");
            return false;
        }
    }
    if (!memory_->Write(output, names.data(),
                        names.size() * sizeof(names.front()))) {
        glDeleteTextures(static_cast<GLsizei>(names.size()), names.data());
        SetError(error, "native GLES glGenTextures guest output is invalid");
        return false;
    }
    texture_names_.insert(names.begin(), names.end());
    return true;
}

bool NativeGles::DeleteTextures(std::uint64_t count, std::uint64_t input,
                                std::string* error) {
    constexpr std::uint64_t kMaximumCount = 4096;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glDeleteTextures requires the owned current native GPU "
                 "context");
        return false;
    }
    if (count > kMaximumCount ||
        count > static_cast<std::uint64_t>(
                    std::numeric_limits<GLsizei>::max())) {
        SetError(error, "native GLES glDeleteTextures count is out of range");
        return false;
    }
    if (count == 0) return true;
    std::vector<GLuint> names(static_cast<std::size_t>(count));
    if (input == 0 ||
        !memory_->Read(input, names.data(),
                       names.size() * sizeof(names.front()))) {
        SetError(error, "native GLES glDeleteTextures guest input is invalid");
        return false;
    }
    std::vector<GLuint> owned_names;
    owned_names.reserve(names.size());
    std::unordered_set<GLuint> unique_owned_names;
    for (const GLuint name : names) {
        if (name == 0 || !texture_names_.contains(name) ||
            !unique_owned_names.insert(name).second)
            continue;
        owned_names.push_back(name);
    }
    if (owned_names.empty()) return true;
    ClearGlErrors();
    glDeleteTextures(static_cast<GLsizei>(owned_names.size()),
                     owned_names.data());
    if (!CheckGlCall("glDeleteTextures", error)) return false;
    for (const GLuint name : owned_names) {
        texture_names_.erase(name);
        texture_2d_defined_levels_.erase(name);
        for (auto level = texture_level_images_.begin();
             level != texture_level_images_.end();) {
            if ((level->first >> 32) == name)
                level = texture_level_images_.erase(level);
            else
                ++level;
        }
    }
    for (auto binding = bound_texture_by_unit_target_.begin();
         binding != bound_texture_by_unit_target_.end();) {
        if (unique_owned_names.contains(binding->second))
            binding = bound_texture_by_unit_target_.erase(binding);
        else
            ++binding;
    }
    return true;
}

bool NativeGles::BindTexture(std::uint32_t target, std::uint32_t texture,
                             std::string* error) {
    constexpr std::uint32_t kGlTexture2d [[maybe_unused]] = 0x0de1;
    constexpr std::uint32_t kGlTextureCubeMap [[maybe_unused]] = 0x8513;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glBindTexture requires the owned current native GPU context");
        return false;
    }
    if (texture != 0 && !texture_names_.contains(texture)) {
        SetError(error, "native GLES glBindTexture texture is not owned");
        return false;
    }
    ClearGlErrors();
    glBindTexture(static_cast<GLenum>(target), static_cast<GLuint>(texture));
    const GLenum bind_error = glGetError();
    if (bind_error != GL_NO_ERROR) {
        // Unsupported target (e.g. GL_TEXTURE_3D without OES_texture_3d):
        // surface the host error to the guest via glGetError and leave the
        // binding unchanged rather than aborting the guest call.
        pending_guest_gl_error_ = static_cast<std::uint32_t>(bind_error);
        return true;
    }
    bound_texture_by_unit_target_.insert_or_assign(
        (static_cast<std::uint64_t>(active_texture_unit_) << 32) | target,
        texture);
    return true;
}

std::uint32_t NativeGles::BoundTextureForUploadTarget(
    std::uint32_t target) const {
    constexpr std::uint32_t kGlTextureCubeMap = 0x8513;
    constexpr std::uint32_t kGlTextureCubeMapPositiveX = 0x8515;
    constexpr std::uint32_t kGlTextureCubeMapNegativeZ = 0x851a;
    // Cube-face upload targets share the GL_TEXTURE_CUBE_MAP binding point;
    // every other upload target binds under its own name.
    const std::uint32_t bind_target =
        (target >= kGlTextureCubeMapPositiveX &&
         target <= kGlTextureCubeMapNegativeZ)
            ? kGlTextureCubeMap
            : target;
    const auto binding = bound_texture_by_unit_target_.find(
        (static_cast<std::uint64_t>(active_texture_unit_) << 32) |
        bind_target);
    return binding == bound_texture_by_unit_target_.end() ? 0
                                                          : binding->second;
}

bool NativeGles::ActiveTexture(std::uint32_t texture, std::string* error) {
    constexpr std::uint32_t kGlTexture0 = 0x84c0;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glActiveTexture requires the owned current native GPU "
                 "context");
        return false;
    }
    GLint maximum_units = 0;
    ClearGlErrors();
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maximum_units);
    if (!CheckGlCall("glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS)",
                     error))
        return false;
    if (maximum_units <= 0 || texture < kGlTexture0 ||
        texture - kGlTexture0 >= static_cast<std::uint32_t>(maximum_units)) {
        SetError(error,
                 "native GLES glActiveTexture unit is out of range");
        return false;
    }
    ClearGlErrors();
    glActiveTexture(static_cast<GLenum>(texture));
    if (!CheckGlCall("glActiveTexture", error)) return false;
    active_texture_unit_ = texture - kGlTexture0;
    return true;
}

bool NativeGles::TextureParameter(std::uint32_t target,
                                  std::uint32_t name, std::int32_t value,
                                  std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glTexParameteri requires the owned current native GPU "
                 "context");
        return false;
    }
    ClearGlErrors();
    glTexParameteri(static_cast<GLenum>(target), static_cast<GLenum>(name),
                    static_cast<GLint>(value));
    const GLenum param_error = glGetError();
    if (param_error == GL_NO_ERROR || param_error == GL_INVALID_ENUM) {
        // GLES1-only texture parameters (mipmap priorities, resident flags)
        // and unsupported targets (e.g. GL_TEXTURE_3D) have no ES2 meaning;
        // accept them as translation no-ops.
        return true;
    }
    // Surface any other host error (e.g. INVALID_OPERATION for an unbound
    // texture) to the guest via glGetError rather than aborting the call.
    pending_guest_gl_error_ = static_cast<std::uint32_t>(param_error);
    return true;
}

bool NativeGles::GenerateRenderbuffers(std::uint64_t count,
                                       std::uint64_t output,
                                       std::string* error) {
    constexpr std::uint64_t kMaximumCount = 4096;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glGenRenderbuffers requires the owned current native GPU "
                 "context");
        return false;
    }
    if (count > kMaximumCount ||
        count > static_cast<std::uint64_t>(
                    std::numeric_limits<GLsizei>::max())) {
        SetError(error,
                 "native GLES glGenRenderbuffers count is out of range");
        return false;
    }
    if (count == 0) return true;
    if (output == 0) {
        SetError(error,
                 "native GLES glGenRenderbuffers guest output is invalid");
        return false;
    }
    std::vector<GLuint> names(static_cast<std::size_t>(count));
    ClearGlErrors();
    glGenRenderbuffers(static_cast<GLsizei>(count), names.data());
    if (!CheckGlCall("glGenRenderbuffers", error)) {
        glDeleteRenderbuffers(static_cast<GLsizei>(names.size()),
                              names.data());
        return false;
    }
    for (const GLuint name : names) {
        if (name == 0 || renderbuffer_names_.contains(name)) {
            glDeleteRenderbuffers(static_cast<GLsizei>(names.size()),
                                  names.data());
            SetError(error,
                     "host glGenRenderbuffers returned an invalid name");
            return false;
        }
    }
    if (!memory_->Write(output, names.data(),
                        names.size() * sizeof(names.front()))) {
        glDeleteRenderbuffers(static_cast<GLsizei>(names.size()),
                              names.data());
        SetError(error,
                 "native GLES glGenRenderbuffers guest output is invalid");
        return false;
    }
    renderbuffer_names_.insert(names.begin(), names.end());
    return true;
}

bool NativeGles::BindRenderbuffer(std::uint32_t target,
                                  std::uint32_t renderbuffer,
                                  std::string* error) {
    constexpr std::uint32_t kGlRenderbuffer = 0x8d41;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glBindRenderbuffer requires the owned current native GPU "
                 "context");
        return false;
    }
    if (target != kGlRenderbuffer) {
        SetError(error,
                 "native GLES glBindRenderbuffer target is not implemented: " +
                     std::to_string(target));
        return false;
    }
    if (renderbuffer != 0 && !renderbuffer_names_.contains(renderbuffer)) {
        SetError(error,
                 "native GLES glBindRenderbuffer renderbuffer is not owned");
        return false;
    }
    ClearGlErrors();
    glBindRenderbuffer(static_cast<GLenum>(target),
                       static_cast<GLuint>(renderbuffer));
    if (!CheckGlCall("glBindRenderbuffer", error)) return false;
    bound_renderbuffer_ = renderbuffer;
    return true;
}

bool NativeGles::RenderbufferStorage(std::uint32_t target,
                                     std::uint32_t internal_format,
                                     std::int32_t width,
                                     std::int32_t height,
                                     std::string* error) {
    constexpr std::uint32_t kGlRenderbuffer = 0x8d41;
    constexpr std::uint32_t kGlDepthComponent16 = 0x81a5;
    constexpr std::uint32_t kGlStencilIndex8 = 0x8d48;
    constexpr std::uint32_t kGlDepth24Stencil8 = 0x88f0;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glRenderbufferStorage requires the owned current native "
                 "GPU context");
        return false;
    }
    if (target != kGlRenderbuffer) {
        SetError(
            error,
            "native GLES glRenderbufferStorage target is not implemented: " +
                std::to_string(target));
        return false;
    }
    if (internal_format != kGlDepthComponent16 &&
        internal_format != kGlStencilIndex8 &&
        internal_format != kGlDepth24Stencil8) {
        SetError(error,
                 "native GLES glRenderbufferStorage format is not "
                 "implemented: " +
                     std::to_string(internal_format));
        return false;
    }
    if (bound_renderbuffer_ == 0 ||
        !renderbuffer_names_.contains(bound_renderbuffer_)) {
        SetError(error,
                 "native GLES glRenderbufferStorage has no owned "
                 "renderbuffer bound");
        return false;
    }
    GLint maximum_size = 0;
    ClearGlErrors();
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maximum_size);
    if (!CheckGlCall("glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE)", error) ||
        maximum_size <= 0) {
        if (error != nullptr && error->empty())
            SetError(error,
                     "host returned an invalid maximum renderbuffer size");
        return false;
    }
    if (width < 0 || height < 0 || width > maximum_size ||
        height > maximum_size) {
        SetError(error,
                 "native GLES glRenderbufferStorage dimensions are out of "
                 "range");
        return false;
    }
    ClearGlErrors();
    glRenderbufferStorage(static_cast<GLenum>(target),
                          static_cast<GLenum>(internal_format),
                          static_cast<GLsizei>(width),
                          static_cast<GLsizei>(height));
    return CheckGlCall("glRenderbufferStorage", error);
}

bool NativeGles::DeleteRenderbuffers(std::uint64_t count,
                                     std::uint64_t input,
                                     std::string* error) {
    constexpr std::uint64_t kMaximumCount = 4096;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glDeleteRenderbuffers requires the owned current native "
                 "GPU context");
        return false;
    }
    if (count > kMaximumCount ||
        count > static_cast<std::uint64_t>(
                    std::numeric_limits<GLsizei>::max())) {
        SetError(error,
                 "native GLES glDeleteRenderbuffers count is out of range");
        return false;
    }
    if (count == 0) return true;
    std::vector<GLuint> names(static_cast<std::size_t>(count));
    if (input == 0 ||
        !memory_->Read(input, names.data(),
                       names.size() * sizeof(names.front()))) {
        SetError(error,
                 "native GLES glDeleteRenderbuffers guest input is invalid");
        return false;
    }
    std::vector<GLuint> owned_names;
    owned_names.reserve(names.size());
    std::unordered_set<GLuint> unique_owned_names;
    for (const GLuint name : names) {
        if (name == 0 || !renderbuffer_names_.contains(name) ||
            !unique_owned_names.insert(name).second)
            continue;
        owned_names.push_back(name);
    }
    if (owned_names.empty()) return true;
    ClearGlErrors();
    glDeleteRenderbuffers(static_cast<GLsizei>(owned_names.size()),
                          owned_names.data());
    if (!CheckGlCall("glDeleteRenderbuffers", error)) return false;
    for (const GLuint name : owned_names) {
        renderbuffer_names_.erase(name);
        if (bound_renderbuffer_ == name) bound_renderbuffer_ = 0;
    }
    return true;
}

bool NativeGles::GenerateFramebuffers(std::uint64_t count,
                                      std::uint64_t output,
                                      std::string* error) {
    constexpr std::uint64_t kMaximumCount = 4096;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glGenFramebuffers requires the owned current native GPU "
                 "context");
        return false;
    }
    if (count > kMaximumCount ||
        count > static_cast<std::uint64_t>(
                    std::numeric_limits<GLsizei>::max())) {
        SetError(error,
                 "native GLES glGenFramebuffers count is out of range");
        return false;
    }
    if (count == 0) return true;
    if (output == 0) {
        SetError(error,
                 "native GLES glGenFramebuffers guest output is invalid");
        return false;
    }
    std::vector<GLuint> names(static_cast<std::size_t>(count));
    ClearGlErrors();
    glGenFramebuffers(static_cast<GLsizei>(count), names.data());
    if (!CheckGlCall("glGenFramebuffers", error)) {
        glDeleteFramebuffers(static_cast<GLsizei>(names.size()), names.data());
        return false;
    }
    for (const GLuint name : names) {
        if (name == 0 || framebuffer_names_.contains(name)) {
            glDeleteFramebuffers(static_cast<GLsizei>(names.size()),
                                 names.data());
            SetError(error,
                     "host glGenFramebuffers returned an invalid name");
            return false;
        }
    }
    if (!memory_->Write(output, names.data(),
                        names.size() * sizeof(names.front()))) {
        glDeleteFramebuffers(static_cast<GLsizei>(names.size()), names.data());
        SetError(error,
                 "native GLES glGenFramebuffers guest output is invalid");
        return false;
    }
    framebuffer_names_.insert(names.begin(), names.end());
    return true;
}

bool NativeGles::BindFramebuffer(std::uint32_t target,
                                 std::uint32_t framebuffer,
                                 std::string* error) {
    constexpr std::uint32_t kGlFramebuffer = 0x8d40;
    constexpr std::uint32_t kGlReadFramebuffer = 0x8ca8;
    constexpr std::uint32_t kGlDrawFramebuffer = 0x8ca9;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(
            error,
            "glBindFramebuffer requires the owned current native GPU context");
        return false;
    }
    // ES2 has a single framebuffer binding point; the ES3/EXT split
    // GL_READ_FRAMEBUFFER / GL_DRAW_FRAMEBUFFER targets collapse onto it.
    if (target != kGlFramebuffer && target != kGlReadFramebuffer &&
        target != kGlDrawFramebuffer) {
        SetError(
            error,
            "native GLES glBindFramebuffer target is not implemented: " +
                std::to_string(target));
        return false;
    }
    if (framebuffer != 0 && !framebuffer_names_.contains(framebuffer)) {
        SetError(error,
                 "native GLES glBindFramebuffer framebuffer is not owned");
        return false;
    }
    ClearGlErrors();
    // Bind through the unified ES2 target; ANGLE's split-target support is not
    // guaranteed on an ES2 context, so map both ES3 targets to GL_FRAMEBUFFER.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    if (!CheckGlCall("glBindFramebuffer", error)) return false;
    bound_framebuffer_ = framebuffer;
    return true;
}

bool NativeGles::FramebufferTexture2d(std::uint32_t target,
                                      std::uint32_t attachment,
                                      std::uint32_t texture_target,
                                      std::uint32_t texture,
                                      std::int32_t level,
                                      std::string* error) {
    constexpr std::uint32_t kGlFramebuffer = 0x8d40;
    constexpr std::uint32_t kGlReadFramebuffer = 0x8ca8;
    constexpr std::uint32_t kGlDrawFramebuffer = 0x8ca9;
    constexpr std::uint32_t kGlColorAttachment0 = 0x8ce0;
    constexpr std::uint32_t kGlDepthAttachment = 0x8d00;
    constexpr std::uint32_t kGlStencilAttachment = 0x8d20;
    constexpr std::uint32_t kGlDepthStencilAttachment = 0x821a;
    constexpr std::uint32_t kGlTexture2d = 0x0de1;
    constexpr std::uint32_t kGlCubeMapPositiveX = 0x8515;
    constexpr std::uint32_t kGlCubeMapNegativeZ = 0x851a;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glFramebufferTexture2D requires the owned current native "
                 "GPU context");
        return false;
    }
    const bool valid_target = target == kGlFramebuffer ||
                              target == kGlReadFramebuffer ||
                              target == kGlDrawFramebuffer;
    const bool valid_attachment =
        (attachment >= kGlColorAttachment0 && attachment < 0x8cf0) ||
        attachment == kGlDepthAttachment ||
        attachment == kGlStencilAttachment ||
        attachment == kGlDepthStencilAttachment;
    const bool valid_texture_target =
        texture_target == kGlTexture2d ||
        (texture_target >= kGlCubeMapPositiveX &&
         texture_target <= kGlCubeMapNegativeZ);
    if (!valid_target || !valid_attachment || !valid_texture_target ||
        level < 0 || level > 16) {
        SetError(error,
                 "native GLES glFramebufferTexture2D contract is not "
                 "implemented: target=" + std::to_string(target) +
                     " attachment=" + std::to_string(attachment) +
                     " textarget=" + std::to_string(texture_target) +
                     " level=" + std::to_string(level));
        return false;
    }
    if (bound_framebuffer_ == 0 ||
        !framebuffer_names_.contains(bound_framebuffer_)) {
        SetError(error,
                 "native GLES glFramebufferTexture2D has no owned "
                 "framebuffer bound");
        return false;
    }
    if (texture != 0 && !texture_names_.contains(texture)) {
        SetError(error,
                 "native GLES glFramebufferTexture2D texture is not owned");
        return false;
    }
    ClearGlErrors();
    // ES3 read/draw targets collapse onto the ES2 unified binding, matching
    // glBindFramebuffer's mapping.
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, static_cast<GLenum>(attachment),
        static_cast<GLenum>(texture_target), static_cast<GLuint>(texture),
        static_cast<GLint>(level));
    const bool ok = CheckGlCall("glFramebufferTexture2D", error);
    if (ok) {
        // Diagnose FBO completeness at attach time — an attached texture
        // without defined storage silently makes the FBO incomplete and the
        // failure only surfaces later at glClear/glDraw (GL error 1286).
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        std::size_t defined_count = 0;
        const std::uint64_t tex_prefix =
            static_cast<std::uint64_t>(texture) << 32;
        for (const auto& [key, image] : texture_level_images_) {
            if ((key & 0xffffffff00000000ULL) == tex_prefix &&
                image.width > 0 && image.height > 0)
                ++defined_count;
        }
        std::fprintf(stderr,
                     "ISOMR_FBO_ATTACH fbo=%u att=0x%x ttgt=0x%x tex=%u "
                     "lv=%d status=0x%x defined=%zu\n",
                     bound_framebuffer_, attachment, texture_target, texture,
                     static_cast<int>(level), static_cast<int>(status),
                     defined_count);
        std::fflush(stderr);
    }
    return ok;
}

bool NativeGles::FramebufferRenderbuffer(std::uint32_t target,
                                         std::uint32_t attachment,
                                         std::uint32_t renderbuffer_target,
                                         std::uint32_t renderbuffer,
                                         std::string* error) {
    constexpr std::uint32_t kGlFramebuffer = 0x8d40;
    constexpr std::uint32_t kGlReadFramebuffer = 0x8ca8;
    constexpr std::uint32_t kGlDrawFramebuffer = 0x8ca9;
    constexpr std::uint32_t kGlRenderbuffer = 0x8d41;
    constexpr std::uint32_t kGlColorAttachment0 = 0x8ce0;
    constexpr std::uint32_t kGlDepthAttachment = 0x8d00;
    constexpr std::uint32_t kGlStencilAttachment = 0x8d20;
    constexpr std::uint32_t kGlDepthStencilAttachment = 0x821a;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glFramebufferRenderbuffer requires the owned current "
                 "native GPU context");
        return false;
    }
    const bool valid_target = target == kGlFramebuffer ||
                              target == kGlReadFramebuffer ||
                              target == kGlDrawFramebuffer;
    const bool valid_attachment =
        (attachment >= kGlColorAttachment0 && attachment < 0x8cf0) ||
        attachment == kGlDepthAttachment ||
        attachment == kGlStencilAttachment ||
        attachment == kGlDepthStencilAttachment;
    if (!valid_target || renderbuffer_target != kGlRenderbuffer ||
        !valid_attachment) {
        SetError(error,
                 "native GLES glFramebufferRenderbuffer contract is not "
                 "implemented: target=" + std::to_string(target) +
                     " attachment=" + std::to_string(attachment));
        return false;
    }
    if (bound_framebuffer_ == 0 ||
        !framebuffer_names_.contains(bound_framebuffer_)) {
        SetError(error,
                 "native GLES glFramebufferRenderbuffer has no owned "
                 "framebuffer bound");
        return false;
    }
    if (renderbuffer != 0 && !renderbuffer_names_.contains(renderbuffer)) {
        SetError(error,
                 "native GLES glFramebufferRenderbuffer renderbuffer is not "
                 "owned");
        return false;
    }
    ClearGlErrors();
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, static_cast<GLenum>(attachment),
        static_cast<GLenum>(renderbuffer_target),
        static_cast<GLuint>(renderbuffer));
    return CheckGlCall("glFramebufferRenderbuffer", error);
}

bool NativeGles::CheckFramebufferStatus(std::uint32_t target,
                                        std::uint64_t* result,
                                        std::string* error) {
    constexpr std::uint32_t kGlFramebuffer = 0x8d40;
    constexpr std::uint32_t kGlReadFramebuffer = 0x8ca8;
    constexpr std::uint32_t kGlDrawFramebuffer = 0x8ca9;
    if (!initialized_ || shutdown_ || !is_current() || result == nullptr) {
        SetError(error,
                 "glCheckFramebufferStatus requires the owned current "
                 "native GPU context and output");
        return false;
    }
    if (target != kGlFramebuffer && target != kGlReadFramebuffer &&
        target != kGlDrawFramebuffer) {
        SetError(error,
                 "native GLES glCheckFramebufferStatus target is not "
                 "implemented: " +
                     std::to_string(target));
        return false;
    }
    ClearGlErrors();
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (!CheckGlCall("glCheckFramebufferStatus", error)) return false;
    *result = static_cast<std::uint64_t>(status);
    return true;
}

bool NativeGles::DeleteFramebuffers(std::uint64_t count,
                                    std::uint64_t input,
                                    std::string* error) {
    constexpr std::uint64_t kMaximumCount = 4096;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glDeleteFramebuffers requires the owned current native "
                 "GPU context");
        return false;
    }
    if (count > kMaximumCount ||
        count > static_cast<std::uint64_t>(
                    std::numeric_limits<GLsizei>::max())) {
        SetError(error,
                 "native GLES glDeleteFramebuffers count is out of range");
        return false;
    }
    if (count == 0) return true;
    std::vector<GLuint> names(static_cast<std::size_t>(count));
    if (input == 0 ||
        !memory_->Read(input, names.data(),
                       names.size() * sizeof(names.front()))) {
        SetError(error,
                 "native GLES glDeleteFramebuffers guest input is invalid");
        return false;
    }
    std::vector<GLuint> owned_names;
    owned_names.reserve(names.size());
    std::unordered_set<GLuint> unique_owned_names;
    for (const GLuint name : names) {
        if (name == 0 || !framebuffer_names_.contains(name) ||
            !unique_owned_names.insert(name).second)
            continue;
        owned_names.push_back(name);
    }
    if (owned_names.empty()) return true;
    ClearGlErrors();
    glDeleteFramebuffers(static_cast<GLsizei>(owned_names.size()),
                         owned_names.data());
    if (!CheckGlCall("glDeleteFramebuffers", error)) return false;
    for (const GLuint name : owned_names) {
        framebuffer_names_.erase(name);
        if (bound_framebuffer_ == name) bound_framebuffer_ = 0;
    }
    return true;
}

bool NativeGles::BindScreenTarget(std::string* error) {
    if (screen_fbo_ == 0) {
        SetError(error, "native GLES screen FBO was never created");
        return false;
    }
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "native GLES screen FBO bind requires the owned current "
                 "context");
        return false;
    }
    ClearGlErrors();
    glBindFramebuffer(0x8D40 /* GL_FRAMEBUFFER */, screen_fbo_);
    bound_framebuffer_ = screen_fbo_;
    return CheckGlCall("bind screen FBO", error);
}

bool NativeGles::BindWindowTarget(std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "native GLES window bind requires the owned current context");
        return false;
    }
    ClearGlErrors();
    glBindFramebuffer(0x8D40 /* GL_FRAMEBUFFER */, 0);
    bound_framebuffer_ = 0;
    return CheckGlCall("bind window FBO", error);
}

std::uint32_t NativeGles::EnsureScreenFbo() {    if (screen_fbo_ != 0) return screen_fbo_;
    if (!initialized_ || shutdown_ || !is_current()) return 0;
    GLuint fbo = 0;
    ClearGlErrors();
    glGenFramebuffers(1, &fbo);
    std::string sink;
    if (!CheckGlCall("screen fbo glGenFramebuffers", &sink) || fbo == 0)
        return 0;
    framebuffer_names_.insert(fbo);
    screen_fbo_ = fbo;
    std::fprintf(stderr, "ISOMR_SCREEN_FBO created=%u\n", fbo);
    std::fflush(stderr);
    return screen_fbo_;
}

void NativeGles::EnsureDrawableViewport() {    if (!initialized_ || shutdown_ || !is_current()) return;
    GLint viewport[4] = {0, 0, 0, 0};
    ClearGlErrors();
    glGetIntegerv(GL_VIEWPORT, viewport);
    ClearGlErrors();
    static std::size_t logged_observations = 0;
    if (logged_observations < 12) {
        ++logged_observations;
        std::fprintf(stderr,
                     "ISOMR_VIEWPORT_OBSERVE draw-entry=[%d %d %d %d] "
                     "surface=%dx%d\n",
                     viewport[0], viewport[1], viewport[2], viewport[3],
                     surface_width_, surface_height_);
        std::fflush(stderr);
    }
    if (viewport[2] > 0 && viewport[3] > 0) return;
    glViewport(0, 0, static_cast<GLsizei>(surface_width_),
               static_cast<GLsizei>(surface_height_));
    static std::size_t logged_restores = 0;
    if (logged_restores < 6) {
        ++logged_restores;
        std::fprintf(stderr,
                     "ISOMR_VIEWPORT_RESTORE degenerate=[%d %d %d %d] "
                     "fixed=%dx%d\n",
                     viewport[0], viewport[1], viewport[2], viewport[3],
                     surface_width_, surface_height_);
        std::fflush(stderr);
    }
}

bool NativeGles::Viewport(std::int32_t x, std::int32_t y,
                          std::int32_t width, std::int32_t height,
                          std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glViewport requires the owned current native GPU context");
        return false;
    }
    if (width <= 0 || height <= 0) {
        if (width != 0 || height != 0) {
            SetError(error,
                     "native GLES glViewport dimensions are invalid");
            return false;
        }
        // The guest calls glViewport with degenerate dimensions when the
        // state machinery computes zeros from uninitialized data. A zero
        // viewport clips every draw → black framebuffer. Substitute the
        // surface dimensions so rendering continues.
        static std::size_t degenerate_logs = 0;
        if (degenerate_logs < 6) {
            ++degenerate_logs;
            std::fprintf(stderr,
                         "ISOMR_VIEWPORT_FIX degenerate=%dx%d → "
                         "surface=%dx%d\n",
                         width, height, surface_width_, surface_height_);
            std::fflush(stderr);
        }
        if (surface_width_ <= 0 || surface_height_ <= 0) return true;
        width = surface_width_;
        height = surface_height_;
    }
    ClearGlErrors();
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y),
               static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    if (!CheckGlCall("glViewport", error)) return false;
    static std::size_t logged_viewports = 0;
    if (logged_viewports < 12) {
        ++logged_viewports;
        std::fprintf(stderr,
                     "ISOMR_GLSURFACE viewport %dx%d@%d,%d (surface "
                     "%dx%d)\n",
                     width, height, x, y, surface_width_, surface_height_);
        std::fflush(stderr);
    }
    return true;
}

bool NativeGles::DepthRange(float near_value, float far_value,
                            std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(
            error,
            "glDepthRangef requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glDepthRangef(static_cast<GLfloat>(near_value),
                  static_cast<GLfloat>(far_value));
    return CheckGlCall("glDepthRangef", error);
}

bool NativeGles::ClearColor(float red, float green, float blue, float alpha,
                            std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glClearColor requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glClearColor(static_cast<GLfloat>(red), static_cast<GLfloat>(green),
                 static_cast<GLfloat>(blue), static_cast<GLfloat>(alpha));
    return CheckGlCall("glClearColor", error);
}

bool NativeGles::ClearDepth(float depth, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glClearDepthf requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glClearDepthf(static_cast<GLfloat>(depth));
    return CheckGlCall("glClearDepthf", error);
}

bool NativeGles::ClearStencil(std::int32_t stencil, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glClearStencil requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glClearStencil(static_cast<GLint>(stencil));
    return CheckGlCall("glClearStencil", error);
}

bool NativeGles::StencilMask(std::uint32_t mask, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glStencilMask requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glStencilMask(static_cast<GLuint>(mask));
    return CheckGlCall("glStencilMask", error);
}

bool NativeGles::StencilFunction(std::uint32_t function,
                                 std::int32_t reference,
                                 std::uint32_t mask, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glStencilFunc requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glStencilFunc(static_cast<GLenum>(function), static_cast<GLint>(reference),
                  static_cast<GLuint>(mask));
    return CheckGlCall("glStencilFunc", error);
}

bool NativeGles::StencilOperation(std::uint32_t sfail, std::uint32_t dpfail,
                                  std::uint32_t dppass, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glStencilOp requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glStencilOp(static_cast<GLenum>(sfail), static_cast<GLenum>(dpfail),
                static_cast<GLenum>(dppass));
    return CheckGlCall("glStencilOp", error);
}

bool NativeGles::StencilFunctionSeparate(std::uint32_t face,
                                         std::uint32_t function,
                                         std::int32_t reference,
                                         std::uint32_t mask,
                                         std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(
            error,
            "glStencilFuncSeparate requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glStencilFuncSeparate(static_cast<GLenum>(face),
                          static_cast<GLenum>(function),
                          static_cast<GLint>(reference),
                          static_cast<GLuint>(mask));
    return CheckGlCall("glStencilFuncSeparate", error);
}

bool NativeGles::StencilOperationSeparate(std::uint32_t face,
                                          std::uint32_t sfail,
                                          std::uint32_t dpfail,
                                          std::uint32_t dppass,
                                          std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(
            error,
            "glStencilOpSeparate requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glStencilOpSeparate(static_cast<GLenum>(face), static_cast<GLenum>(sfail),
                        static_cast<GLenum>(dpfail),
                        static_cast<GLenum>(dppass));
    return CheckGlCall("glStencilOpSeparate", error);
}

bool NativeGles::Clear(std::uint32_t mask, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glClear requires the owned current native GPU context");
        return false;
    }
    constexpr std::uint32_t kSupportedMask =
        GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    if ((mask & ~kSupportedMask) != 0) {
        SetError(error, "native GLES glClear mask is invalid");
        return false;
    }
    ClearGlErrors();
    glClear(static_cast<GLbitfield>(mask));
    return CheckGlCall("glClear", error);
}

bool NativeGles::SetCapability(std::uint32_t capability, bool enabled,
                               std::string* error) {
    const char* call_name = enabled ? "glEnable" : "glDisable";
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error, std::string(call_name) +
                            " requires the owned current native GPU context");
        return false;
    }
    // PvZ's TodGL was written against GLES1 fixed-function state
    // (GL_LIGHTING, GL_FOG, GL_ALPHA_TEST, GL_TEXTURE_GEN_*). The ES2
    // shader pipeline ignores those, and the desktop driver answers
    // GL_INVALID_ENUM; tolerate them instead of failing the session.
    static constexpr std::uint32_t kKnownFixedFunction[] = {
        0x0b44, 0x0b50, 0x0b60, 0x0b70, 0x0b71, 0x0bd0, 0x0bc0,
        0x0c60, 0x0c61, 0x0c62, 0x0c11, 0x0de1};
    for (const std::uint32_t known : kKnownFixedFunction) {
        if (capability == known) {
            ClearGlErrors();
            static std::set<std::uint32_t> tolerated;
            if (tolerated.insert(capability).second) {
                std::fprintf(stderr,
                             "ISOMR_GLES1_CAP tolerated=%x enabled=%d\n",
                             capability, enabled ? 1 : 0);
                std::fflush(stderr);
            }
            return true;
        }
    }
    ClearGlErrors();
    if (enabled)
        glEnable(static_cast<GLenum>(capability));
    else
        glDisable(static_cast<GLenum>(capability));
    return CheckGlCall(call_name, error);
}

bool NativeGles::ColorMask(bool red, bool green, bool blue, bool alpha,
                           std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glColorMask requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glColorMask(red ? GL_TRUE : GL_FALSE, green ? GL_TRUE : GL_FALSE,
                blue ? GL_TRUE : GL_FALSE, alpha ? GL_TRUE : GL_FALSE);
    return CheckGlCall("glColorMask", error);
}

bool NativeGles::DepthMask(bool enabled, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glDepthMask requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    return CheckGlCall("glDepthMask", error);
}

bool NativeGles::BlendFunction(std::uint32_t source,
                               std::uint32_t destination,
                               std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glBlendFunc requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glBlendFunc(static_cast<GLenum>(source),
                static_cast<GLenum>(destination));
    return CheckGlCall("glBlendFunc", error);
}

bool NativeGles::BlendColor(float red, float green, float blue, float alpha,
                            std::string* error) {
    if (shutdown_ || eglGetCurrentContext() == EGL_NO_CONTEXT) {
        SetError(error,
                 "glBlendColor requires a context current on the calling thread");
        return false;
    }
    ClearGlErrors();
    glBlendColor(red, green, blue, alpha);
    return CheckGlCall("glBlendColor", error);
}

bool NativeGles::BlendFunctionSeparate(std::uint32_t source_rgb,
                                       std::uint32_t destination_rgb,
                                       std::uint32_t source_alpha,
                                       std::uint32_t destination_alpha,
                                       std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glBlendFuncSeparate requires the owned current native GPU "
                 "context");
        return false;
    }
    ClearGlErrors();
    glBlendFuncSeparate(static_cast<GLenum>(source_rgb),
                        static_cast<GLenum>(destination_rgb),
                        static_cast<GLenum>(source_alpha),
                        static_cast<GLenum>(destination_alpha));
    return CheckGlCall("glBlendFuncSeparate", error);
}

bool NativeGles::BlendEquation(std::uint32_t mode, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(
            error,
            "glBlendEquation requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glBlendEquation(static_cast<GLenum>(mode));
    return CheckGlCall("glBlendEquation", error);
}

bool NativeGles::BlendEquationSeparate(std::uint32_t rgb_mode,
                                       std::uint32_t alpha_mode,
                                       std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glBlendEquationSeparate requires the owned current native "
                 "GPU context");
        return false;
    }
    ClearGlErrors();
    glBlendEquationSeparate(static_cast<GLenum>(rgb_mode),
                            static_cast<GLenum>(alpha_mode));
    return CheckGlCall("glBlendEquationSeparate", error);
}

bool NativeGles::DepthFunction(std::uint32_t function,
                               std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glDepthFunc requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glDepthFunc(static_cast<GLenum>(function));
    return CheckGlCall("glDepthFunc", error);
}

bool NativeGles::CullFace(std::uint32_t mode, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glCullFace requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glCullFace(static_cast<GLenum>(mode));
    return CheckGlCall("glCullFace", error);
}

bool NativeGles::FrontFace(std::uint32_t winding, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glFrontFace requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    glFrontFace(static_cast<GLenum>(winding));
    return CheckGlCall("glFrontFace", error);
}

bool NativeGles::Scissor(std::int32_t x, std::int32_t y,
                         std::int32_t width, std::int32_t height,
                         std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glScissor requires the owned current native GPU context");
        return false;
    }
    if (width < 0 || height < 0) {
        SetError(error, "native GLES glScissor dimensions are invalid");
        return false;
    }
    ClearGlErrors();
    glScissor(static_cast<GLint>(x), static_cast<GLint>(y),
              static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    return CheckGlCall("glScissor", error);
}

bool NativeGles::SetVertexAttributeArray(std::uint32_t index, bool enabled,
                                         std::string* error) {
    const char* call_name = enabled ? "glEnableVertexAttribArray"
                                    : "glDisableVertexAttribArray";
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error, std::string(call_name) +
                            " requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    if (enabled)
        glEnableVertexAttribArray(static_cast<GLuint>(index));
    else
        glDisableVertexAttribArray(static_cast<GLuint>(index));
    return CheckGlCall(call_name, error);
}

bool NativeGles::VertexAttributePointer(std::uint32_t index,
                                        std::int32_t size,
                                        std::uint32_t type, bool normalized,
                                        std::int32_t stride,
                                        std::uint64_t pointer,
                                        std::string* error) {
    constexpr std::uint32_t kGlByte = 0x1400;
    constexpr std::uint32_t kGlUnsignedByte = 0x1401;
    constexpr std::uint32_t kGlShort = 0x1402;
    constexpr std::uint32_t kGlUnsignedShort = 0x1403;
    constexpr std::uint32_t kGlFloat = 0x1406;
    constexpr std::uint32_t kGlFixed = 0x140c;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glVertexAttribPointer requires the owned current native "
                 "GPU context");
        return false;
    }
    GLint maximum_attributes = 0;
    ClearGlErrors();
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maximum_attributes);
    if (!CheckGlCall("glGetIntegerv(GL_MAX_VERTEX_ATTRIBS)", error))
        return false;
    if (maximum_attributes <= 0 ||
        index >= static_cast<std::uint32_t>(maximum_attributes)) {
        SetError(error,
                 "native GLES glVertexAttribPointer index is out of range");
        return false;
    }
    if (size < 1 || size > 4) {
        SetError(error,
                 "native GLES glVertexAttribPointer size is out of range");
        return false;
    }
    if (type != kGlByte && type != kGlUnsignedByte && type != kGlShort &&
        type != kGlUnsignedShort && type != kGlFloat && type != kGlFixed) {
        SetError(error,
                 "native GLES glVertexAttribPointer type is not implemented: " +
                     std::to_string(type));
        return false;
    }
    if (stride < 0) {
        SetError(error,
                 "native GLES glVertexAttribPointer stride is invalid");
        return false;
    }
    // With a VBO bound to GL_ARRAY_BUFFER the pointer argument is a
    // byte offset into that buffer, not a guest address (the
    // setupVBOAndVAO path). Only client-memory pointers must be
    // readable.
    std::uint8_t readable = 0;
    if (bound_array_buffer_ == 0 && pointer != 0 &&
        !memory_->Read(pointer, &readable, sizeof(readable))) {
        std::fprintf(stderr,
                     "ISOMR_VAP_REJECT index=%u size=%u type=%x stride=%d ptr=%llx boundbuf=%u\n",
                     index, size, type, stride,
                     static_cast<unsigned long long>(pointer),
                     bound_array_buffer_);
        std::fflush(stderr);
        SetError(error,
                 "native GLES glVertexAttribPointer guest pointer is invalid");
        return false;
    }
    vertex_attribute_pointers_.insert_or_assign(
        index, VertexAttributePointerState{size, type, normalized, stride,
                                           pointer,
                                           bound_array_buffer_ != 0});
    // With a VBO bound on the guest the pointer is a buffer offset:
    // forward it to the host so ANGLE reads vertex data from the bound
    // buffer instead of a stale host client pointer (a stale pointer
    // crashes ANGLE's dynamic-attribute copy at draw time). Client
    // pointers are staged at draw time by the DrawElements path.
    if (bound_array_buffer_ != 0) {
        ClearGlErrors();
        glVertexAttribPointer(
            static_cast<GLuint>(index), static_cast<GLint>(size),
            static_cast<GLenum>(type), normalized ? GL_TRUE : GL_FALSE,
            static_cast<GLsizei>(stride),
            reinterpret_cast<const GLvoid*>(pointer));
        return CheckGlCall("glVertexAttribPointer(host offset)", error);
    }
    return true;
}

bool NativeGles::VertexAttribute4fv(std::uint32_t index,
                                    std::uint64_t values,
                                    std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glVertexAttrib4fv requires the owned current native GPU "
                 "context");
        return false;
    }
    GLint maximum_attributes = 0;
    ClearGlErrors();
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maximum_attributes);
    if (!CheckGlCall("glGetIntegerv(GL_MAX_VERTEX_ATTRIBS)", error))
        return false;
    if (maximum_attributes <= 0 ||
        index >= static_cast<std::uint32_t>(maximum_attributes)) {
        SetError(error, "native GLES glVertexAttrib4fv index is out of range");
        return false;
    }
    std::array<GLfloat, 4> host_values{};
    if (values == 0 ||
        !memory_->Read(values, host_values.data(), sizeof(host_values))) {
        SetError(error,
                 "native GLES glVertexAttrib4fv guest values are invalid");
        return false;
    }
    ClearGlErrors();
    glVertexAttrib4fv(static_cast<GLuint>(index), host_values.data());
    return CheckGlCall("glVertexAttrib4fv", error);
}

bool NativeGles::VertexAttribute2fv(std::uint32_t index,
                                    std::uint64_t values,
                                    std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glVertexAttrib2fv requires the owned current native GPU "
                 "context");
        return false;
    }
    GLint maximum_attributes = 0;
    ClearGlErrors();
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maximum_attributes);
    if (!CheckGlCall("glGetIntegerv(GL_MAX_VERTEX_ATTRIBS)", error))
        return false;
    if (maximum_attributes <= 0 ||
        index >= static_cast<std::uint32_t>(maximum_attributes)) {
        SetError(error, "native GLES glVertexAttrib2fv index is out of range");
        return false;
    }
    std::array<GLfloat, 2> host_values{};
    if (values == 0 ||
        !memory_->Read(values, host_values.data(), sizeof(host_values))) {
        SetError(error,
                 "native GLES glVertexAttrib2fv guest values are invalid");
        return false;
    }
    ClearGlErrors();
    glVertexAttrib2fv(static_cast<GLuint>(index), host_values.data());
    return CheckGlCall("glVertexAttrib2fv", error);
}

bool NativeGles::VertexAttribute3fv(std::uint32_t index,
                                    std::uint64_t values,
                                    std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glVertexAttrib3fv requires the owned current native GPU "
                 "context");
        return false;
    }
    GLint maximum_attributes = 0;
    ClearGlErrors();
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maximum_attributes);
    if (!CheckGlCall("glGetIntegerv(GL_MAX_VERTEX_ATTRIBS)", error))
        return false;
    if (maximum_attributes <= 0 ||
        index >= static_cast<std::uint32_t>(maximum_attributes)) {
        SetError(error, "native GLES glVertexAttrib3fv index is out of range");
        return false;
    }
    std::array<GLfloat, 3> host_values{};
    if (values == 0 ||
        !memory_->Read(values, host_values.data(), sizeof(host_values))) {
        SetError(error,
                 "native GLES glVertexAttrib3fv guest values are invalid");
        return false;
    }
    ClearGlErrors();
    glVertexAttrib3fv(static_cast<GLuint>(index), host_values.data());
    return CheckGlCall("glVertexAttrib3fv", error);
}

bool NativeGles::StageClientVertexAttributes(std::uint32_t maximum_vertex,
                                             std::size_t initial_staging,
                                             std::string* error) {
    constexpr std::uint32_t kGlByte = 0x1400;
    constexpr std::uint32_t kGlUnsignedByte = 0x1401;
    constexpr std::uint32_t kGlShort = 0x1402;
    constexpr std::uint32_t kGlUnsignedShort = 0x1403;
    constexpr std::uint32_t kGlFloat = 0x1406;
    constexpr std::uint32_t kGlFixed = 0x140c;
    constexpr std::size_t kMaximumStaging = 64U * 1024U * 1024U;

    GLint maximum_attributes = 0;
    GLint array_buffer_binding = 0;
    ClearGlErrors();
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maximum_attributes);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer_binding);
    if (!CheckGlCall("client-attribute staging state query", error))
        return false;
    if (maximum_attributes <= 0 || array_buffer_binding != 0) {
        SetError(error,
                 "native GLES client-array staging requires no bound array "
                 "buffer");
        return false;
    }

    GLint active_attribute_count = 0;
    GLint active_attribute_name_capacity = 0;
    ClearGlErrors();
    glGetProgramiv(static_cast<GLuint>(bound_program_), GL_ACTIVE_ATTRIBUTES,
                   &active_attribute_count);
    glGetProgramiv(static_cast<GLuint>(bound_program_),
                   GL_ACTIVE_ATTRIBUTE_MAX_LENGTH,
                   &active_attribute_name_capacity);
    if (!CheckGlCall("draw active-attribute query", error)) return false;
    if (active_attribute_count < 0 || active_attribute_name_capacity < 0 ||
        active_attribute_name_capacity > 4096 ||
        (active_attribute_count != 0 &&
         active_attribute_name_capacity == 0)) {
        SetError(error,
                 "native GLES draw active-attribute metadata is invalid");
        return false;
    }
    std::unordered_set<std::uint32_t> active_attribute_locations;
    std::vector<char> active_attribute_name(
        static_cast<std::size_t>(active_attribute_name_capacity));
    for (GLint active_index = 0; active_index < active_attribute_count;
         ++active_index) {
        GLsizei name_length = 0;
        GLint array_size = 0;
        GLenum active_type = 0;
        ClearGlErrors();
        glGetActiveAttrib(static_cast<GLuint>(bound_program_),
                          static_cast<GLuint>(active_index),
                          active_attribute_name_capacity, &name_length,
                          &array_size, &active_type,
                          active_attribute_name.data());
        if (!CheckGlCall("glGetActiveAttrib(draw)", error)) return false;
        if (name_length <= 0 ||
            name_length >= active_attribute_name_capacity ||
            array_size <= 0) {
            SetError(error,
                     "native GLES draw active-attribute entry is invalid");
            return false;
        }
        const GLint location = glGetAttribLocation(
            static_cast<GLuint>(bound_program_), active_attribute_name.data());
        if (!CheckGlCall("glGetAttribLocation(draw)", error)) return false;
        if (location < 0) continue;
        std::uint32_t columns = 1;
        if (active_type == GL_FLOAT_MAT2)
            columns = 2;
        else if (active_type == GL_FLOAT_MAT3)
            columns = 3;
        else if (active_type == GL_FLOAT_MAT4)
            columns = 4;
        const std::uint64_t occupied =
            static_cast<std::uint64_t>(array_size) * columns;
        if (occupied > static_cast<std::uint64_t>(maximum_attributes) ||
            static_cast<std::uint64_t>(location) + occupied >
                static_cast<std::uint64_t>(maximum_attributes)) {
            SetError(error,
                     "native GLES draw active-attribute locations are out of range");
            return false;
        }
        for (std::uint64_t offset = 0; offset < occupied; ++offset)
            active_attribute_locations.insert(
                static_cast<std::uint32_t>(location) +
                static_cast<std::uint32_t>(offset));
    }

    std::vector<std::uint32_t> enabled_attributes;
    std::size_t total_staging = initial_staging;
    for (GLint attribute = 0; attribute < maximum_attributes; ++attribute) {
        GLint enabled = GL_FALSE;
        ClearGlErrors();
        glGetVertexAttribiv(static_cast<GLuint>(attribute),
                            GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        if (!CheckGlCall("glGetVertexAttribiv", error)) return false;
        if (enabled == GL_FALSE) {
            vertex_attribute_staging_.erase(
                static_cast<std::uint32_t>(attribute));
            continue;
        }
        const std::uint32_t checked_attribute =
            static_cast<std::uint32_t>(attribute);
        if (!active_attribute_locations.contains(checked_attribute)) {
            vertex_attribute_staging_.erase(checked_attribute);
            ++inactive_enabled_attribute_skip_count_;
            if (!inactive_enabled_attribute_reported_) {
                std::fprintf(stderr,
                             "ISOMR_GLES_DRAW_FILTER reason=enabled-inactive-program attribute=%u program=%u\n",
                             checked_attribute, bound_program_);
                inactive_enabled_attribute_reported_ = true;
            }
            continue;
        }
        const auto state_it = vertex_attribute_pointers_.find(checked_attribute);
        if (state_it == vertex_attribute_pointers_.end()) {
            SetError(error,
                     "native GLES draw enabled attribute has no recorded "
                     "pointer state");
            return false;
        }
        const VertexAttributePointerState& state = state_it->second;
        if (state.buffer_offset) {
            // The attribute references a bound buffer offset: the host
            // attribute already points at the right storage and must not
            // be repointed at staging memory.
            vertex_attribute_staging_.erase(checked_attribute);
            continue;
        }
        if (state.pointer == 0) {
            SetError(error,
                     "native GLES draw enabled attribute has no readable "
                     "guest pointer");
            return false;
        }
        std::size_t component_bytes = 0;
        if (state.type == kGlByte || state.type == kGlUnsignedByte)
            component_bytes = 1;
        else if (state.type == kGlShort ||
                 state.type == kGlUnsignedShort)
            component_bytes = 2;
        else if (state.type == kGlFloat || state.type == kGlFixed)
            component_bytes = 4;
        if (component_bytes == 0 || state.size < 1 || state.size > 4) {
            SetError(error,
                     "native GLES draw attribute format is invalid");
            return false;
        }
        const std::size_t element_bytes =
            component_bytes * static_cast<std::size_t>(state.size);
        const std::size_t effective_stride =
            state.stride == 0 ? element_bytes
                              : static_cast<std::size_t>(state.stride);
        if (maximum_vertex != 0 &&
            effective_stride >
                (kMaximumStaging - element_bytes) / maximum_vertex) {
            SetError(error,
                     "native GLES draw attribute span is oversized");
            return false;
        }
        const std::size_t span =
            static_cast<std::size_t>(maximum_vertex) * effective_stride +
            element_bytes;
        if (span > kMaximumStaging - total_staging) {
            SetError(error,
                     "native GLES draw total staging is oversized");
            return false;
        }
        total_staging += span;
        std::vector<std::uint8_t>& staging =
            vertex_attribute_staging_[checked_attribute];
        try {
            staging.resize(span);
        } catch (const std::bad_alloc&) {
            SetError(error,
                     "native GLES draw staging allocation failed");
            return false;
        }
        if (!memory_->Read(state.pointer, staging.data(), staging.size())) {
            SetError(error,
                     "native GLES draw guest attribute is unreadable"
                     " attribute=" +
                         std::to_string(checked_attribute) +
                         " span=" + std::to_string(span) +
                         " max-vertex=" + std::to_string(maximum_vertex) +
                         " size=" + std::to_string(state.size) +
                         " type=" + std::to_string(state.type) +
                         " stride=" + std::to_string(state.stride) +
                         " point-range=" +
                         memory_->DescribeRange(state.pointer, 1) +
                         " span-range=" +
                         memory_->DescribeRange(state.pointer, span));
            return false;
        }
        static std::size_t staged_dump_count = 0;
        if (staged_dump_count < 24) {
            ++staged_dump_count;
            const std::size_t shown =
                staging.size() < 24 ? staging.size() : 24;
            std::size_t nonzero = 0;
            for (const std::uint8_t byte : staging)
                if (byte != 0) ++nonzero;
            std::string hex;
            hex.reserve(shown * 3);
            for (std::size_t i = 0; i < shown; ++i) {
                char byte_text[4];
                std::snprintf(byte_text, sizeof(byte_text), "%02x ",
                              staging[i]);
                hex += byte_text;
            }
            std::fprintf(
                stderr,
                "ISOMR_STAGE attrib=%u ptr=%llx span=%zu nonzero=%zu "
                "stride=%d size=%d type=%x bytes=%s\n",
                checked_attribute,
                static_cast<unsigned long long>(state.pointer), span,
                nonzero, state.stride, state.size, state.type,
                hex.c_str());
            std::fflush(stderr);
        }
        enabled_attributes.push_back(checked_attribute);
    }

    ClearGlErrors();
    for (const std::uint32_t attribute : enabled_attributes) {
        const VertexAttributePointerState& state =
            vertex_attribute_pointers_.at(attribute);
        const std::vector<std::uint8_t>& staging =
            vertex_attribute_staging_.at(attribute);
        if (state.integer_attrib)
            glVertexAttribIPointer(
                static_cast<GLuint>(attribute),
                static_cast<GLint>(state.size),
                static_cast<GLenum>(state.type),
                static_cast<GLsizei>(state.stride), staging.data());
        else
            glVertexAttribPointer(
                static_cast<GLuint>(attribute),
                static_cast<GLint>(state.size),
                static_cast<GLenum>(state.type),
                state.normalized ? GL_TRUE : GL_FALSE,
                static_cast<GLsizei>(state.stride), staging.data());
    }
    return CheckGlCall("glVertexAttribPointer(draw staging)", error);
}

void NativeGles::LatchDrawGlError(const char* name) {
    const GLenum host_error = glGetError();
    if (host_error == GL_NO_ERROR) return;
    if (pending_guest_gl_error_ == 0) pending_guest_gl_error_ = host_error;
    GLint program = 0;
    GLint element_buffer = 0;
    GLint array_buffer = 0;
    GLint framebuffer = 0;
    GLint vao = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(0x8895 /*GL_ELEMENT_ARRAY_BUFFER_BINDING*/,
                  &element_buffer);
    glGetIntegerv(0x8894 /*GL_ARRAY_BUFFER_BINDING*/, &array_buffer);
    glGetIntegerv(0x8ca6 /*GL_DRAW_FRAMEBUFFER_BINDING*/, &framebuffer);
    glGetIntegerv(0x85b5 /*GL_VERTEX_ARRAY_BINDING*/, &vao);
    static std::size_t reported_draw_errors = 0;
    if (reported_draw_errors < 200) {
        ++reported_draw_errors;
        std::fprintf(
            stderr,
            "ISOMR_GLES_DRAW_ERROR name=%s err=0x%x ctx=%p program=%d elem=%d "
            "array=%d fbo=%d vao=%d tracked_program=%u draws=%zu\n",
            name, host_error, eglGetCurrentContext(), program, element_buffer,
            array_buffer, framebuffer, vao, bound_program_, draw_call_count_);
        std::fflush(stderr);
    }
    (void)glGetError();
}

void NativeGles::LatchInvalidOperationError() {
    if (pending_guest_gl_error_ == 0)
        pending_guest_gl_error_ = 0x0502 /*GL_INVALID_OPERATION*/;
}

void NativeGles::LatchInvalidValueError() {
    if (pending_guest_gl_error_ == 0)
        pending_guest_gl_error_ = 0x0501 /*GL_INVALID_VALUE*/;
}

bool NativeGles::DrawIndexedEs3(std::uint32_t mode, std::int32_t count,
                                std::uint32_t type, std::uint64_t indices,
                                std::int32_t instance_count,
                                bool has_base_vertex, std::int32_t base_vertex,
                                bool ranged, std::uint32_t range_start,
                                std::uint32_t range_end, const char* name,
                                std::string* error) {
    constexpr std::uint32_t kGlUnsignedByte = 0x1401;
    constexpr std::uint32_t kGlUnsignedShort = 0x1403;
    constexpr std::uint32_t kGlUnsignedInt = 0x1405;
    constexpr std::size_t kMaximumStaging = 64U * 1024U * 1024U;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error, std::string(name) +
                            " requires an owned current context");
        return false;
    }
    ClearGlErrors();
    GLint host_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &host_program);
    (void)glGetError();
    if (host_program != 0 &&
        program_names_.contains(static_cast<std::uint32_t>(host_program)))
        bound_program_ = static_cast<std::uint32_t>(host_program);
    if (bound_program_ == 0 ||
        !program_names_.contains(bound_program_) || count < 0 ||
        (ranged && range_end < range_start)) {
        if (pending_guest_gl_error_ == 0)
            pending_guest_gl_error_ = 0x0502 /*GL_INVALID_OPERATION*/;
        static std::size_t reported = 0;
        if (reported < 50) {
            ++reported;
            std::fprintf(stderr,
                         "ISOMR_GLES_DRAW_ERROR name=%s err=precondition "
                         "ctx=%p host_program=%d tracked=%u count=%d\n",
                         name, eglGetCurrentContext(), host_program,
                         bound_program_, count);
            std::fflush(stderr);
        }
        ++draw_call_count_;
        return true;
    }
    std::size_t index_element_bytes = 0;
    if (type == kGlUnsignedByte)
        index_element_bytes = 1;
    else if (type == kGlUnsignedShort)
        index_element_bytes = 2;
    else if (type == kGlUnsignedInt)
        index_element_bytes = 4;
    else {
        SetError(error, std::string(name) +
                            " index type is not implemented: " +
                            std::to_string(type));
        return false;
    }
    const std::size_t checked_count = static_cast<std::size_t>(count);
    if (checked_count > kMaximumStaging / index_element_bytes) {
        SetError(error, std::string(name) + " index data is oversized");
        return false;
    }
    GLint element_buffer_binding = 0;
    ClearGlErrors();
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element_buffer_binding);
    if (!CheckGlCall("draw-es3 state query", error)) return false;
    const auto invoke = [&](const void* host_indices) {
        const GLenum host_mode = static_cast<GLenum>(mode);
        const GLsizei host_count = static_cast<GLsizei>(count);
        const GLenum host_type = static_cast<GLenum>(type);
        if (ranged && has_base_vertex && instance_count > 0) {
            SetError(error, std::string(name) +
                                " ranged instanced base-vertex draws are "
                                "not a GLES entry point");
            return false;
        }
        if (ranged && has_base_vertex)
            glDrawRangeElementsBaseVertex(host_mode, range_start, range_end,
                                          host_count, host_type, host_indices,
                                          base_vertex);
        else if (ranged)
            glDrawRangeElements(host_mode, range_start, range_end, host_count,
                                host_type, host_indices);
        else if (instance_count > 0 && has_base_vertex)
            glDrawElementsInstancedBaseVertex(
                host_mode, host_count, host_type, host_indices,
                instance_count, base_vertex);
        else if (instance_count > 0)
            glDrawElementsInstanced(host_mode, host_count, host_type,
                                    host_indices, instance_count);
        else if (has_base_vertex)
            glDrawElementsBaseVertex(host_mode, host_count, host_type,
                                     host_indices, base_vertex);
        else
            glDrawElements(host_mode, host_count, host_type, host_indices);
        return true;
    };
    if (element_buffer_binding != 0) {
        EnsureDrawableViewport();
        const void* host_indices = reinterpret_cast<const GLvoid*>(
            static_cast<std::uintptr_t>(indices));
        if (!invoke(host_indices)) return false;
        LatchDrawGlError(name);
    } else {
        const std::size_t index_bytes =
            checked_count * index_element_bytes;
        std::vector<std::uint8_t> host_indices(index_bytes);
        if (index_bytes != 0 &&
            (indices == 0 ||
             !memory_->Read(indices, host_indices.data(),
                            host_indices.size()))) {
            SetError(error,
                     std::string(name) +
                         " guest indices are unreadable");
            return false;
        }
        std::uint32_t maximum_vertex = 0;
        for (std::size_t index = 0; index < checked_count; ++index) {
            std::uint32_t vertex = 0;
            std::memcpy(&vertex,
                        host_indices.data() + index * index_element_bytes,
                        index_element_bytes);
            if (vertex > maximum_vertex) maximum_vertex = vertex;
        }
        if (has_base_vertex && base_vertex > 0) {
            const std::uint64_t top = static_cast<std::uint64_t>(
                                          maximum_vertex) +
                                      static_cast<std::uint64_t>(base_vertex);
            if (top <= std::numeric_limits<std::uint32_t>::max())
                maximum_vertex = static_cast<std::uint32_t>(top);
        }
        if (!StageClientVertexAttributes(maximum_vertex, index_bytes, error))
            return false;
        EnsureDrawableViewport();
        if (!invoke(host_indices.empty() ? nullptr : host_indices.data()))
            return false;
        LatchDrawGlError(name);
    }
    ++draw_call_count_;
    return true;
}

bool NativeGles::DrawElements(std::uint32_t mode, std::int32_t count,
                              std::uint32_t type, std::uint64_t indices,
                              std::string* error) {
    constexpr std::uint32_t kGlUnsignedByte = 0x1401;
    constexpr std::uint32_t kGlUnsignedShort = 0x1403;
    constexpr std::uint32_t kGlUnsignedInt = 0x1405;
    constexpr std::size_t kMaximumStaging = 64U * 1024U * 1024U;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glDrawElements requires an owned current context");
        return false;
    }
    // The bound program is per-context GL state while bound_program_ is a
    // last-write-wins tracker across all contexts. Re-sync it from the host
    // so a draw on a shared context uses the program that context actually
    // has bound.
    ClearGlErrors();
    GLint host_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &host_program);
    (void)glGetError();
    if (host_program != 0 &&
        program_names_.contains(static_cast<std::uint32_t>(host_program)))
        bound_program_ = static_cast<std::uint32_t>(host_program);
    if (bound_program_ == 0 ||
        !program_names_.contains(bound_program_)) {
        // No program bound on this context: real GLES reports
        // GL_INVALID_OPERATION and the draw no-ops. Latch it — killing the
        // guest thread here would lose every subsequent command.
        if (pending_guest_gl_error_ == 0)
            pending_guest_gl_error_ = 0x0502 /*GL_INVALID_OPERATION*/;
        static std::size_t reported_no_program = 0;
        if (reported_no_program < 50) {
            ++reported_no_program;
            std::fprintf(
                stderr,
                "ISOMR_GLES_DRAW_ERROR name=glDrawElements err=no-program "
                "ctx=%p host_program=%d tracked=%u draws=%zu\n",
                eglGetCurrentContext(), host_program, bound_program_,
                draw_call_count_);
            std::fflush(stderr);
        }
        ++draw_call_count_;
        return true;
    }
    if (count < 0) {
        SetError(error, "native GLES glDrawElements count is out of range");
        return false;
    }

    std::size_t index_element_bytes = 0;
    if (type == kGlUnsignedByte)
        index_element_bytes = 1;
    else if (type == kGlUnsignedShort)
        index_element_bytes = 2;
    else if (type == kGlUnsignedInt)
        index_element_bytes = 4;
    else {
        SetError(error,
                 "native GLES glDrawElements index type is not implemented: " +
                     std::to_string(type));
        return false;
    }
    const std::size_t checked_count = static_cast<std::size_t>(count);
    if (checked_count > kMaximumStaging / index_element_bytes) {
        SetError(error,
                 "native GLES glDrawElements index data is oversized");
        return false;
    }
    if (checked_count == 0) {
        ClearGlErrors();
        glDrawElements(static_cast<GLenum>(mode), 0,
                       static_cast<GLenum>(type), nullptr);
        LatchDrawGlError("glDrawElements(count=0)");
        ++draw_call_count_;
        return true;
    }
    // Highest referenced vertex index, computed only for the client-index
    // path where the index array is readable. 0 for server-side draws.
    std::uint32_t maximum_vertex = 0;
    // Decide whether `indices` is a guest client-memory pointer or a byte
    // offset into a bound GL_ELEMENT_ARRAY_BUFFER before touching it. A bound
    // element buffer (the common VAO/VBO draw path) means indices is an
    // offset; reading it as a guest address would fault.
    GLint element_buffer_binding = 0;
    ClearGlErrors();
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,
                  &element_buffer_binding);
    if (!CheckGlCall("glDrawElements state query", error)) return false;
    if (element_buffer_binding != 0) {
        // Server-side indexed draw: the bound element buffer (and the VAO's
        // captured vertex attributes) already live on the host. Forward the
        // offset verbatim; no client staging is needed or valid here.
        EnsureDrawableViewport();
        glDrawElements(static_cast<GLenum>(mode), static_cast<GLsizei>(count),
                       static_cast<GLenum>(type),
                       reinterpret_cast<const GLvoid*>(
                           static_cast<std::uintptr_t>(indices)));
        LatchDrawGlError("glDrawElements");
    } else {
        const std::size_t index_bytes =
            checked_count * index_element_bytes;
        std::vector<std::uint8_t> host_indices(index_bytes);
        if (index_bytes != 0 &&
            (indices == 0 ||
             !memory_->Read(indices, host_indices.data(),
                            host_indices.size()))) {
            SetError(error,
                     "native GLES glDrawElements guest indices are unreadable");
            return false;
        }

        for (std::size_t index = 0; index < checked_count; ++index) {
            std::uint32_t vertex = 0;
            std::memcpy(&vertex,
                        host_indices.data() + index * index_element_bytes,
                        index_element_bytes);
            if (vertex > maximum_vertex) maximum_vertex = vertex;
        }

        if (!StageClientVertexAttributes(maximum_vertex, index_bytes, error))
            return false;
        EnsureDrawableViewport();
        glDrawElements(static_cast<GLenum>(mode),
                       static_cast<GLsizei>(count),
                       static_cast<GLenum>(type),
                       host_indices.empty() ? nullptr
                                            : host_indices.data());
        LatchDrawGlError("glDrawElements");
    }

    ++draw_call_count_;
    bool draw_capture_requested = false;
    if (!draw_capture_written_ && count > 0 &&
        !FrameCaptureRequested(L"ISOMR_DRAW_CAPTURE_BMP",
                               &draw_capture_requested, error))
        return false;
    if (!draw_capture_written_ && count > 0 && draw_capture_requested) {
        constexpr std::size_t kChannels = 4;
        constexpr std::size_t kMaximumReadback = 64U * 1024U * 1024U;
        const std::size_t checked_width =
            static_cast<std::size_t>(surface_width_);
        const std::size_t checked_height =
            static_cast<std::size_t>(surface_height_);
        if (surface_width_ <= 0 || surface_height_ <= 0 ||
            checked_width > kMaximumReadback / kChannels ||
            checked_height >
                kMaximumReadback / (checked_width * kChannels)) {
            SetError(error,
                     "native GLES draw probe surface is out of range");
            return false;
        }
        std::vector<std::uint8_t> pixels;
        try {
            pixels.resize(checked_width * checked_height * kChannels);
        } catch (const std::bad_alloc&) {
            SetError(error,
                     "native GLES draw probe allocation failed");
            return false;
        }
        ClearGlErrors();
        glReadPixels(0, 0, static_cast<GLsizei>(surface_width_),
                     static_cast<GLsizei>(surface_height_), GL_RGBA,
                     GL_UNSIGNED_BYTE, pixels.data());
        if (!CheckGlCall("glReadPixels(draw probe)", error)) return false;
        ++draw_readback_count_;
        std::size_t lit = 0;
        std::size_t nonzero_alpha = 0;
        std::uint32_t maximum_rgb = 0;
        for (std::size_t pixel = 0; pixel < pixels.size();
             pixel += kChannels) {
            const std::uint32_t rgb =
                static_cast<std::uint32_t>(pixels[pixel]) +
                static_cast<std::uint32_t>(pixels[pixel + 1]) +
                static_cast<std::uint32_t>(pixels[pixel + 2]);
            if (rgb > 12U) ++lit;
            if (pixels[pixel + 3] != 0) ++nonzero_alpha;
            if (rgb > maximum_rgb) maximum_rgb = rgb;
        }
        if (lit != 0 && !draw_capture_written_) {
            if (!CaptureRequestedFrame(L"ISOMR_DRAW_CAPTURE_BMP",
                                       "first-lit-draw", pixels,
                                       checked_width, checked_height, error))
                return false;
            draw_capture_written_ = true;
        }
        if (draw_call_count_ <= 24 || lit != 0 ||
            (draw_call_count_ % 997) == 0) {
            GLint current_program = 0;
            GLint linked = GL_FALSE;
            GLint viewport[4]{};
            GLint scissor[4]{};
            GLint depth_function = 0;
            GLfloat depth_range[2]{};
            GLint blend_source_rgb = 0;
            GLint blend_destination_rgb = 0;
            GLint blend_equation_rgb = 0;
            GLboolean color_mask[4]{};
            GLboolean depth_mask = GL_FALSE;
            ClearGlErrors();
            glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
            if (current_program > 0)
                glGetProgramiv(static_cast<GLuint>(current_program),
                               GL_LINK_STATUS, &linked);
            glGetIntegerv(GL_VIEWPORT, viewport);
            glGetIntegerv(GL_SCISSOR_BOX, scissor);
            glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
            glGetFloatv(GL_DEPTH_RANGE, depth_range);
            glGetIntegerv(GL_BLEND_SRC_RGB, &blend_source_rgb);
            glGetIntegerv(GL_BLEND_DST_RGB, &blend_destination_rgb);
            glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_equation_rgb);
            glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
            const GLboolean blend = glIsEnabled(GL_BLEND);
            const GLboolean depth_test = glIsEnabled(GL_DEPTH_TEST);
            const GLboolean cull_face = glIsEnabled(GL_CULL_FACE);
            const GLboolean scissor_test = glIsEnabled(GL_SCISSOR_TEST);
            const GLenum framebuffer_status =
                glCheckFramebufferStatus(GL_FRAMEBUFFER);
            const GLenum diagnostic_error = glGetError();
            const auto bound_texture =
                bound_texture_by_unit_target_.find(
                    (static_cast<std::uint64_t>(active_texture_unit_) << 32) |
                    0x0de1);
            std::printf(
                "ISOMR_DRAW_PROBE source=guest-draw kind=elements call=%zu "
                "width=%d height=%d sampled=%zu lit=%zu alpha=%zu max-rgb=%u "
                "mode=%u count=%d type=%u max-vertex=%u attributes=%zu "
                "program=%d linked=%d framebuffer=%u framebuffer-status=%u "
                "viewport=%d,%d,%d,%d scissor=%d,%d,%d,%d "
                "color-mask=%u,%u,%u,%u depth-mask=%u blend=%u depth=%u "
                "depth-func=%d depth-range=%.3f,%.3f blend-rgb=%d,%d,%d "
                "cull=%u scissor-test=%u texture-unit=%u texture2d=%u "
                "query-error=%u\n",
                draw_call_count_, surface_width_, surface_height_,
                checked_width * checked_height, lit, nonzero_alpha,
                maximum_rgb, mode, count, type, maximum_vertex,
                vertex_attribute_staging_.size(), current_program, linked,
                bound_framebuffer_,
                static_cast<unsigned int>(framebuffer_status), viewport[0],
                viewport[1], viewport[2], viewport[3], scissor[0], scissor[1],
                scissor[2], scissor[3],
                static_cast<unsigned int>(color_mask[0]),
                static_cast<unsigned int>(color_mask[1]),
                static_cast<unsigned int>(color_mask[2]),
                static_cast<unsigned int>(color_mask[3]),
                static_cast<unsigned int>(depth_mask),
                static_cast<unsigned int>(blend),
                static_cast<unsigned int>(depth_test),
                depth_function, static_cast<double>(depth_range[0]),
                static_cast<double>(depth_range[1]), blend_source_rgb,
                blend_destination_rgb, blend_equation_rgb,
                static_cast<unsigned int>(cull_face),
                static_cast<unsigned int>(scissor_test), active_texture_unit_,
                bound_texture == bound_texture_by_unit_target_.end()
                    ? 0U
                    : bound_texture->second,
                static_cast<unsigned int>(diagnostic_error));
        }
    }
    return true;
}

bool NativeGles::PixelStore(std::uint32_t name, std::int32_t value,
                            std::string* error) {
    constexpr std::uint32_t kGlPackAlignment = 0x0d05;
    constexpr std::uint32_t kGlUnpackAlignment = 0x0cf5;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glPixelStorei requires the owned current native GPU context");
        return false;
    }
    // The alignment pnames only accept {1,2,4,8}; other pixel-store pnames
    // (UNPACK_ROW_LENGTH, skip counts, image height, ...) take any non-negative
    // value. Forward every pname to the host and record its error rather than
    // failing the dispatch, so ES3/extension probes soft-fail like real GLES2.
    if ((name == kGlPackAlignment || name == kGlUnpackAlignment) &&
        value != 1 && value != 2 && value != 4 && value != 8) {
        SetError(error, "native GLES glPixelStorei alignment is invalid");
        return false;
    }
    ClearGlErrors();
    glPixelStorei(static_cast<GLenum>(name), static_cast<GLint>(value));
    const GLenum store_error = glGetError();
    if (store_error != GL_NO_ERROR) {
        // Record the error for the caller's glGetError and report success;
        // an unsupported pname is a soft failure, not a dispatch abort.
        pending_guest_gl_error_ = store_error;
        return true;
    }
    if (name == kGlUnpackAlignment) unpack_alignment_ = value;
    return true;
}

// Component (channel) count for a pixel format. Returns 0 for an
// unrecognized format.
static std::size_t GlPixelComponentCount(std::uint32_t format) {
    switch (format) {
        case 0x1903:  // GL_RED(_EXT)
        case 0x1906:  // GL_ALPHA
        case 0x1909:  // GL_LUMINANCE
            return 1;
        case 0x190a:  // GL_LUMINANCE_ALPHA
        case 0x8227:  // GL_RG(_EXT)
            return 2;
        case 0x1907:  // GL_RGB
            return 3;
        case 0x1908:  // GL_RGBA
        case 0x80e1:  // GL_BGRA(_EXT)
            return 4;
        default:
            return 0;
    }
}

// Bytes per single component for a pixel type. Returns 0 for packed/unknown
// types that must be handled by the caller.
static std::size_t GlPixelComponentSize(std::uint32_t type) {
    switch (type) {
        case 0x1401:  // GL_UNSIGNED_BYTE
            return 1;
        case 0x1403:  // GL_UNSIGNED_SHORT
        case 0x140b:  // GL_HALF_FLOAT(_OES)
            return 2;
        case 0x1405:  // GL_UNSIGNED_INT
        case 0x1406:  // GL_FLOAT
            return 4;
        default:
            return 0;
    }
}

// Sized internal format matching a (format, type) sub-upload, for implicit
// level definition. A bare unsized `format` implies UNSIGNED_BYTE-width
// storage, which rejects wider types (e.g. GL_RED + UNSIGNED_SHORT wants
// GL_R16_EXT, not GL_RED). Returns the unsized `format` when no clean sized
// match exists.
static std::int32_t GlImplicitInternalFormat(std::uint32_t format,
                                             std::uint32_t type) {
    constexpr std::uint32_t kRed = 0x1903;
    constexpr std::uint32_t kRg = 0x8227;
    constexpr std::uint32_t kRgb = 0x1907;
    constexpr std::uint32_t kRgba = 0x1908;
    constexpr std::uint32_t kBgra = 0x80e1;
    if (type == 0x1401) {  // GL_UNSIGNED_BYTE
        switch (format) {
            case kRed:  return 0x8229;  // GL_R8_EXT
            case kRg:   return 0x822B;  // GL_RG8_EXT
            case kRgb:  return 0x8051;  // GL_RGB8
            case kRgba:
            case kBgra: return 0x8058;  // GL_RGBA8
            default:    return static_cast<std::int32_t>(format);
        }
    }
    if (type == 0x1403) {  // GL_UNSIGNED_SHORT (EXT_texture_norm16)
        switch (format) {
            case kRed:  return 0x822A;  // GL_R16_EXT
            case kRg:   return 0x822C;  // GL_RG16_EXT
            case kRgb:  return 0x804B;  // GL_RGB16_EXT
            case kRgba:
            case kBgra: return 0x805B;  // GL_RGBA16_EXT
            default:    return static_cast<std::int32_t>(format);
        }
    }
    if (type == 0x140b) {  // GL_HALF_FLOAT(_OES)
        switch (format) {
            case kRed:  return 0x822D;  // GL_R16F_EXT
            case kRg:   return 0x822F;  // GL_RG16F_EXT
            case kRgb:  return 0x881B;  // GL_RGB16F_EXT
            case kRgba:
            case kBgra: return 0x881A;  // GL_RGBA16F_EXT
            default:    return static_cast<std::int32_t>(format);
        }
    }
    if (type == 0x1406) {  // GL_FLOAT
        switch (format) {
            case kRed:  return 0x822E;  // GL_R32F_EXT
            case kRg:   return 0x8230;  // GL_RG32F_EXT
            case kRgb:  return 0x8815;  // GL_RGB32F
            case kRgba:
            case kBgra: return 0x8814;  // GL_RGBA32F
            default:    return static_cast<std::int32_t>(format);
        }
    }
    return static_cast<std::int32_t>(format);
}

bool NativeGles::TexImage2d(std::uint32_t target, std::int32_t level,
                            std::int32_t internal_format,
                            std::int32_t width, std::int32_t height,
                            std::int32_t border, std::uint32_t format,
                            std::uint32_t type,
                            std::uint64_t stack_pointer,
                            std::string* error) {
    constexpr std::uint32_t kGlTexture2d = 0x0de1;
    constexpr std::uint32_t kGlRed [[maybe_unused]] = 0x1903;
    constexpr std::uint32_t kGlAlpha [[maybe_unused]] = 0x1906;
    constexpr std::uint32_t kGlRgb = 0x1907;
    constexpr std::uint32_t kGlRgba = 0x1908;
    constexpr std::uint32_t kGlLuminance [[maybe_unused]] = 0x1909;
    constexpr std::uint32_t kGlLuminanceAlpha [[maybe_unused]] = 0x190a;
    constexpr std::uint32_t kGlRg [[maybe_unused]] = 0x8227;
    constexpr std::uint32_t kGlUnsignedByte [[maybe_unused]] = 0x1401;
    constexpr std::size_t kMaximumUpload = 64U * 1024U * 1024U;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glTexImage2D requires the owned current native GPU context");
        return false;
    }
    {
        static std::size_t logged_teximage = 0;
        if (logged_teximage < 80) {
            ++logged_teximage;
            std::fprintf(stderr,
                         "ISOMR_TEXIMAGE_ENTRY tgt=%x lv=%d ifmt=%x "
                         "%dx%d fmt=%x type=%x\n",
                         target, level, internal_format, width, height,
                         format, type);
            std::fflush(stderr);
        }
    }
    if (target != kGlTexture2d && (target < 0x8515 || target > 0x851a)) {
        SetError(error,
                 "native GLES glTexImage2D target is not implemented: " +
                     std::to_string(target));
        return false;
    }
    // GLES1 legacy internal-format aliases: 1=LUMINANCE, 2=LUMINANCE_ALPHA,
    // 3=RGB, 4=RGBA. Mip levels are accepted (uploaded directly).
    std::uint32_t effective_format = format;
    if (internal_format == 1) effective_format = kGlLuminance;
    else if (internal_format == 2) effective_format = kGlLuminanceAlpha;
    else if (internal_format == 3) effective_format = kGlRgb;
    else if (internal_format == 4) effective_format = kGlRgba;
    const bool measured_format = GlPixelComponentCount(effective_format) != 0;
    constexpr std::uint32_t kGlUnsignedShort565 = 0x8363;
    constexpr std::uint32_t kGlUnsignedShort4444 = 0x8033;
    constexpr std::uint32_t kGlUnsignedShort5551 = 0x8034;
    const bool packed_type =
        type == kGlUnsignedShort565 || type == kGlUnsignedShort4444 ||
        type == kGlUnsignedShort5551;
    if (level < 0 || (!measured_format && !packed_type) ||
        (internal_format != static_cast<std::int32_t>(format) &&
         internal_format > 4) || border != 0 ||
        (GlPixelComponentSize(type) == 0 && !packed_type)) {
        SetError(error,
                 "native GLES glTexImage2D format contract is not implemented");
        return false;
    }
    const std::uint32_t bound_texture = BoundTextureForUploadTarget(target);
    if (width < 0 || height < 0 || bound_texture == 0 ||
        !texture_names_.contains(bound_texture)) {
        SetError(error,
                 bound_texture == 0
                     ? "native GLES glTexImage2D has no owned texture bound"
                     : "native GLES glTexImage2D dimensions are invalid");
        return false;
    }
    std::size_t byte_count = 0;
    if (width != 0 && height != 0) {
        const std::size_t bytes_per_pixel =
            packed_type ? 2U
                        : GlPixelComponentCount(effective_format) *
                              GlPixelComponentSize(type);
        const std::size_t checked_width = static_cast<std::size_t>(width);
        const std::size_t checked_height = static_cast<std::size_t>(height);
        if (checked_width > kMaximumUpload / bytes_per_pixel ||
            checked_height >
                kMaximumUpload / (checked_width * bytes_per_pixel)) {
            SetError(error, "native GLES glTexImage2D upload is oversized");
            return false;
        }
        const std::size_t tight_row = checked_width * bytes_per_pixel;
        const std::size_t alignment =
            static_cast<std::size_t>(unpack_alignment_);
        if (tight_row > std::numeric_limits<std::size_t>::max() -
                            (alignment - 1)) {
            SetError(error, "native GLES glTexImage2D row size overflowed");
            return false;
        }
        const std::size_t row_stride =
            (tight_row + alignment - 1) & ~(alignment - 1);
        if (checked_height > 1 &&
            row_stride >
                (kMaximumUpload - tight_row) / (checked_height - 1)) {
            SetError(error, "native GLES glTexImage2D upload is oversized");
            return false;
        }
        byte_count = row_stride * (checked_height - 1) + tight_row;
    }
    std::uint64_t guest_pixels = 0;
    if (stack_pointer == 0 ||
        !memory_->Read(stack_pointer, &guest_pixels, sizeof(guest_pixels))) {
        SetError(error,
                 "native GLES glTexImage2D stack argument is unreadable");
        return false;
    }
    std::vector<std::uint8_t> pixels;
    const void* host_pixels = nullptr;
    if (guest_pixels != 0 && byte_count != 0) {
        try {
            pixels.resize(byte_count);
        } catch (const std::bad_alloc&) {
            SetError(error,
                     "native GLES glTexImage2D upload allocation failed");
            return false;
        }
        if (!memory_->Read(guest_pixels, pixels.data(), pixels.size())) {
            SetError(error,
                     "native GLES glTexImage2D guest pixels are unreadable");
            return false;
        }
        host_pixels = pixels.data();
    }
    {
        static std::size_t logged_uploads = 0;
        if (logged_uploads < 60) {
            ++logged_uploads;
            std::size_t nonzero = 0;
            // Sample ~4096 bytes spread across the whole upload, not just the
            // first page: an atlas's top rows are legitimately transparent
            // padding, so a leading-window probe can read all-zero on a valid
            // texture. A strided scan across the full buffer is conclusive.
            const std::size_t probe_samples =
                pixels.size() < 4096 ? pixels.size() : 4096;
            const std::size_t step =
                probe_samples != 0
                    ? (pixels.size() / probe_samples > 0
                           ? pixels.size() / probe_samples
                           : 1)
                    : 1;
            std::size_t sampled = 0;
            for (std::size_t index = 0;
                 index < pixels.size() && sampled < probe_samples;
                 index += step, ++sampled)
                nonzero += pixels[index] != 0;
            std::fprintf(stderr,
                         "ISOMR_TEXUPLOAD kind=teximage tex=%u level=%d "
                         "%dx%d fmt=%x type=%x bytes=%zu probe_nonzero=%zu/"
                         "%zu\n",
                         bound_texture, level, width, height,
                         effective_format, type, pixels.size(), nonzero,
                         sampled);
        }
    }
    ClearGlErrors();
    glTexImage2D(static_cast<GLenum>(target), static_cast<GLint>(level),
                 static_cast<GLint>(internal_format),
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 static_cast<GLint>(border), static_cast<GLenum>(format),
                 static_cast<GLenum>(type), host_pixels);
    if (!CheckGlCall("glTexImage2D", error)) return false;
    if (bound_texture != 0) {
        texture_2d_defined_levels_[bound_texture].insert(
            (target << 16) | static_cast<std::uint32_t>(level));
        texture_level_images_.insert_or_assign(
            (static_cast<std::uint64_t>(bound_texture) << 32) |
                static_cast<std::uint32_t>(level),
            TextureLevelImage{false, format, width, height});
    }
    return true;
}

bool NativeGles::TexSubImage2d(std::uint32_t target, std::int32_t level,
                               std::int32_t x_offset,
                               std::int32_t y_offset, std::int32_t width,
                               std::int32_t height, std::uint32_t format,
                               std::uint32_t type,
                               std::uint64_t stack_pointer,
                               std::string* error) {
    constexpr std::uint32_t kGlTexture2d = 0x0de1;
    constexpr std::uint32_t kGlRed [[maybe_unused]] = 0x1903;
    constexpr std::uint32_t kGlAlpha [[maybe_unused]] = 0x1906;
    constexpr std::uint32_t kGlRgb = 0x1907;
    constexpr std::uint32_t kGlRgba = 0x1908;
    constexpr std::uint32_t kGlLuminance [[maybe_unused]] = 0x1909;
    constexpr std::uint32_t kGlLuminanceAlpha [[maybe_unused]] = 0x190a;
    constexpr std::uint32_t kGlRg [[maybe_unused]] = 0x8227;
    constexpr std::uint32_t kGlUnsignedByte [[maybe_unused]] = 0x1401;
    constexpr std::uint32_t kGlUnsignedShort565 = 0x8363;
    constexpr std::uint32_t kGlUnsignedShort4444 = 0x8033;
    constexpr std::uint32_t kGlUnsignedShort5551 = 0x8034;
    constexpr std::size_t kMaximumUpload = 64U * 1024U * 1024U;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glTexSubImage2D requires the owned current native GPU context");
        return false;
    }
    // PvZ1 streams its title/atlas textures as RGB565 (5-6-5 packed
    // shorts) in addition to the UNSIGNED_BYTE paths, and its alpha/luminance
    // masks as ALPHA/LUMINANCE/LUMINANCE_ALPHA single- and dual-channel
    // uploads.
    const bool measured_format = GlPixelComponentCount(format) != 0;
    const bool packed_short =
        (type == kGlUnsignedShort565 && format == kGlRgb) ||
        ((type == kGlUnsignedShort4444 || type == kGlUnsignedShort5551) &&
         format == kGlRgba);
    const bool measured_type =
        packed_short || GlPixelComponentSize(type) != 0;
    const bool upload_target =
        target == kGlTexture2d || (target >= 0x8515 && target <= 0x851a);
    if (!upload_target || level < 0 || !measured_format || !measured_type) {
        std::fprintf(stderr,
                     "ISOMR_TEXSUB_REJECT target=%x level=%d format=%x type=%x wh=%dx%d\n",
                     target, level, format, type, width, height);
        std::fflush(stderr);
        SetError(error,
                 "native GLES glTexSubImage2D format contract is not implemented");
        return false;
    }
    const std::uint32_t bound_texture = BoundTextureForUploadTarget(target);
    if (x_offset < 0 || y_offset < 0 || width <= 0 || height <= 0 ||
        bound_texture == 0 || !texture_names_.contains(bound_texture)) {
        SetError(error,
                 bound_texture == 0
                     ? "native GLES glTexSubImage2D has no owned texture bound"
                     : "native GLES glTexSubImage2D dimensions are invalid");
        return false;
    }
    // A glTexSubImage2D against a level the guest never defined raises
    // GL_INVALID_OPERATION on the host. Unity relies on the level existing,
    // so allocate it (sized to cover the sub-rectangle) before the
    // sub-upload, mirroring the implicit definition Unity's GLES drivers
    // observe. A later glTexImage2D simply redefines the level.
    auto& defined_levels = texture_2d_defined_levels_[bound_texture];
    const std::uint32_t face_key =
        (target << 16) | static_cast<std::uint32_t>(level);
    if (!defined_levels.contains(face_key)) {
        const std::int32_t internal_format =
            GlImplicitInternalFormat(format, type);
        ClearGlErrors();
        glTexImage2D(static_cast<GLenum>(target), static_cast<GLint>(level),
                     static_cast<GLint>(internal_format),
                     static_cast<GLsizei>(x_offset + width),
                     static_cast<GLsizei>(y_offset + height), 0,
                     static_cast<GLenum>(format), static_cast<GLenum>(type),
                     nullptr);
        if (!CheckGlCall("glTexImage2D", error)) return false;
        defined_levels.insert(face_key);
    }
    const std::size_t bytes_per_pixel =
        packed_short ? 2U
                     : GlPixelComponentCount(format) *
                           GlPixelComponentSize(type);
    const std::size_t checked_width = static_cast<std::size_t>(width);
    const std::size_t checked_height = static_cast<std::size_t>(height);
    if (checked_width > kMaximumUpload / bytes_per_pixel ||
        checked_height >
            kMaximumUpload / (checked_width * bytes_per_pixel)) {
        SetError(error, "native GLES glTexSubImage2D upload is oversized");
        return false;
    }
    const std::size_t tight_row = checked_width * bytes_per_pixel;
    const std::size_t alignment = static_cast<std::size_t>(unpack_alignment_);
    if (tight_row > std::numeric_limits<std::size_t>::max() -
                        (alignment - 1)) {
        SetError(error,
                 "native GLES glTexSubImage2D row size overflowed");
        return false;
    }
    const std::size_t row_stride =
        (tight_row + alignment - 1) & ~(alignment - 1);
    if (checked_height > 1 &&
        row_stride > (kMaximumUpload - tight_row) / (checked_height - 1)) {
        SetError(error, "native GLES glTexSubImage2D upload is oversized");
        return false;
    }
    const std::size_t byte_count =
        row_stride * (checked_height - 1) + tight_row;
    std::uint64_t guest_pixels = 0;
    if (stack_pointer == 0 ||
        !memory_->Read(stack_pointer, &guest_pixels, sizeof(guest_pixels))) {
        SetError(error,
                 "native GLES glTexSubImage2D stack argument is unreadable");
        return false;
    }
    std::vector<std::uint8_t> pixels;
    try {
        pixels.resize(byte_count);
    } catch (const std::bad_alloc&) {
        SetError(error,
                 "native GLES glTexSubImage2D upload allocation failed");
        return false;
    }
    if (guest_pixels == 0 ||
        !memory_->Read(guest_pixels, pixels.data(), pixels.size())) {
        SetError(error,
                 "native GLES glTexSubImage2D guest pixels are unreadable");
        return false;
    }
    {
        static std::size_t logged_uploads = 0;
        const bool tracked_target = bound_texture == 15;
        if (logged_uploads < 60 || tracked_target) {
            if (!tracked_target) ++logged_uploads;
            std::size_t nonzero = 0;
            const std::size_t probe_bytes =
                pixels.size() < 4096 ? pixels.size() : 4096;
            for (std::size_t index = 0; index < probe_bytes; ++index)
                nonzero += pixels[index] != 0;
            std::fprintf(stderr,
                         "ISOMR_TEXUPLOAD kind=texsub tex=%u %dx%d@%d,%d "
                         "fmt=%x type=%x bytes=%zu probe_nonzero=%zu/%zu\n",
                         bound_texture, width, height, x_offset, y_offset,
                         format, type, pixels.size(), nonzero, probe_bytes);
        }
    }
    ClearGlErrors();
    glTexSubImage2D(static_cast<GLenum>(target), static_cast<GLint>(level),
                    static_cast<GLint>(x_offset),
                    static_cast<GLint>(y_offset),
                    static_cast<GLsizei>(width),
                    static_cast<GLsizei>(height),
                    static_cast<GLenum>(format), static_cast<GLenum>(type),
                    pixels.data());
    return CheckGlCall("glTexSubImage2D", error);
}

bool NativeGles::CreateShader(std::uint32_t type, std::uint64_t* result,
                              std::string* error) {
    constexpr std::uint32_t kGlVertexShader = 0x8b31;
    constexpr std::uint32_t kGlFragmentShader = 0x8b30;
    if (result == nullptr) {
        SetError(error, "native GLES glCreateShader result is null");
        return false;
    }
    *result = 0;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glCreateShader requires the owned current native GPU context");
        return false;
    }
    if (type != kGlVertexShader && type != kGlFragmentShader) {
        SetError(error, "native GLES glCreateShader type is invalid: " +
                            std::to_string(type));
        return false;
    }
    ClearGlErrors();
    const GLuint shader = glCreateShader(static_cast<GLenum>(type));
    if (shader == 0 || !CheckGlCall("glCreateShader", error)) return false;
    if (shader_names_.contains(shader)) {
        SetError(error, "host glCreateShader returned a duplicate name");
        return false;
    }
    shader_names_.insert(shader);
    *result = shader;
    return true;
}

bool NativeGles::ShaderSource(std::uint32_t shader, std::int32_t count,
                              std::uint64_t strings,
                              std::uint64_t lengths,
                              std::string* error) {
    constexpr std::int32_t kMaximumStringCount = 1024;
    constexpr std::size_t kMaximumSourceBytes = 16U * 1024U * 1024U;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glShaderSource requires the owned current native GPU context");
        return false;
    }
    if (!shader_names_.contains(shader)) {
        SetError(error, "native GLES glShaderSource shader is not owned");
        return false;
    }
    if (count < 0 || count > kMaximumStringCount) {
        SetError(error, "native GLES glShaderSource count is out of range");
        return false;
    }
    if (count == 0) return true;
    if (strings == 0) {
        SetError(error,
                 "native GLES glShaderSource guest string array is invalid");
        return false;
    }

    const std::size_t checked_count = static_cast<std::size_t>(count);
    std::vector<std::uint64_t> guest_strings(checked_count);
    if (!memory_->Read(strings, guest_strings.data(),
                       guest_strings.size() * sizeof(guest_strings.front()))) {
        SetError(error,
                 "native GLES glShaderSource guest string array is unreadable");
        return false;
    }
    std::vector<std::int32_t> guest_lengths;
    if (lengths != 0) {
        guest_lengths.resize(checked_count);
        if (!memory_->Read(
                lengths, guest_lengths.data(),
                guest_lengths.size() * sizeof(guest_lengths.front()))) {
            SetError(error,
                     "native GLES glShaderSource guest length array is unreadable");
            return false;
        }
    }

    std::vector<std::vector<char>> source_storage;
    std::vector<const char*> host_strings;
    std::vector<GLint> host_lengths;
    try {
        source_storage.resize(checked_count);
        host_strings.resize(checked_count);
        host_lengths.resize(checked_count);
    } catch (const std::bad_alloc&) {
        SetError(error, "native GLES glShaderSource allocation failed");
        return false;
    }
    std::size_t total_bytes = 0;
    for (std::size_t index = 0; index < checked_count; ++index) {
        if (guest_strings[index] == 0) {
            SetError(error,
                     "native GLES glShaderSource guest string is null");
            return false;
        }
        std::uint64_t source_length = 0;
        if (lengths == 0 || guest_lengths[index] < 0) {
            if (!memory_->StringLength(guest_strings[index], &source_length,
                                       error))
                return false;
        } else {
            source_length =
                static_cast<std::uint32_t>(guest_lengths[index]);
        }
        if (source_length > static_cast<std::uint64_t>(kMaximumSourceBytes) ||
            total_bytes > kMaximumSourceBytes -
                              static_cast<std::size_t>(source_length)) {
            SetError(error,
                     "native GLES glShaderSource source exceeds its bound");
            return false;
        }
        const std::size_t checked_length =
            static_cast<std::size_t>(source_length);
        try {
            source_storage[index].resize(checked_length + 1, '\0');
        } catch (const std::bad_alloc&) {
            SetError(error, "native GLES glShaderSource allocation failed");
            return false;
        }
        if (checked_length != 0 &&
            !memory_->Read(guest_strings[index],
                           source_storage[index].data(), checked_length)) {
            SetError(error,
                     "native GLES glShaderSource guest string is unreadable");
            return false;
        }
        total_bytes += checked_length;
        host_strings[index] = source_storage[index].data();
        host_lengths[index] = static_cast<GLint>(checked_length);
    }

    {
        static std::size_t logged_shaders = 0;
        if (logged_shaders < 12) {
            ++logged_shaders;
            std::string source_text;
            for (std::size_t index = 0; index < source_storage.size(); ++index)
                source_text.append(
                    reinterpret_cast<const char*>(
                        source_storage[index].data()),
                    source_storage[index].size());
            const std::size_t kPreview = 400;
            std::size_t last_nonspace = 0;
            for (std::size_t index = 0;
                 index < source_text.size() && index < kPreview; ++index)
                if (source_text[index] != '\n' && source_text[index] != '\r')
                    source_text[last_nonspace++] =
                        source_text[index];
                else if (last_nonspace > 0 && source_text[last_nonspace - 1] != ' ')
                    source_text[last_nonspace++] = ' ';
            source_text.resize(last_nonspace);
            std::fprintf(stderr, "ISOMR_SHADER src=%u text=[%s]\n", shader,
                         source_text.c_str());
        }
    }
    ClearGlErrors();
    glShaderSource(static_cast<GLuint>(shader), static_cast<GLsizei>(count),
                   host_strings.data(), host_lengths.data());
    return CheckGlCall("glShaderSource", error);
}

bool NativeGles::CompileShader(std::uint32_t shader, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glCompileShader requires the owned current native GPU context");
        return false;
    }
    if (!shader_names_.contains(shader)) {
        SetError(error, "native GLES glCompileShader shader is not owned");
        return false;
    }
    ClearGlErrors();
    glCompileShader(static_cast<GLuint>(shader));
    return CheckGlCall("glCompileShader", error);
}

bool NativeGles::GetShaderInteger(std::uint32_t shader, std::uint32_t name,
                                  std::uint64_t output,
                                  std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glGetShaderiv requires the owned current native GPU context");
        return false;
    }
    if (!shader_names_.contains(shader)) {
        SetError(error, "native GLES glGetShaderiv shader is not owned");
        return false;
    }
    // Pass query pnames through to the host; an unknown enum becomes a
    // recoverable GL_INVALID_ENUM the guest reads via glGetError.
    GLint value = 0;
    ClearGlErrors();
    glGetShaderiv(static_cast<GLuint>(shader), static_cast<GLenum>(name),
                  &value);
    const GLenum host_error = glGetError();
    if (host_error != GL_NO_ERROR) {
        pending_guest_gl_error_ = static_cast<std::uint32_t>(host_error);
        value = 0;
    }
    const std::int32_t guest_value = value;
    if (output == 0 ||
        !memory_->Write(output, &guest_value, sizeof(guest_value))) {
        SetError(error, "native GLES glGetShaderiv guest output is invalid");
        return false;
    }
    return true;
}

bool NativeGles::GetShaderInfoLog(std::uint32_t shader,
                                  std::int32_t buffer_size,
                                  std::uint64_t output_length,
                                  std::uint64_t output_log,
                                  std::string* error) {
    constexpr std::int32_t kMaximumLogBytes = 1024 * 1024;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glGetShaderInfoLog requires the owned current native GPU context");
        return false;
    }
    if (!shader_names_.contains(shader)) {
        SetError(error, "native GLES glGetShaderInfoLog shader is not owned");
        return false;
    }
    if (buffer_size < 0 || buffer_size > kMaximumLogBytes) {
        SetError(error,
                 "native GLES glGetShaderInfoLog buffer size is out of range");
        return false;
    }
    if (buffer_size > 0 && output_log == 0) {
        SetError(error,
                 "native GLES glGetShaderInfoLog guest output is invalid");
        return false;
    }
    std::vector<char> host_log;
    try {
        host_log.resize(static_cast<std::size_t>(buffer_size), '\0');
    } catch (const std::bad_alloc&) {
        SetError(error, "native GLES glGetShaderInfoLog allocation failed");
        return false;
    }
    GLsizei actual_length = 0;
    ClearGlErrors();
    glGetShaderInfoLog(static_cast<GLuint>(shader),
                       static_cast<GLsizei>(buffer_size), &actual_length,
                       host_log.empty() ? nullptr : host_log.data());
    if (!CheckGlCall("glGetShaderInfoLog", error)) return false;
    if (actual_length < 0 || actual_length > buffer_size ||
        (buffer_size > 0 && actual_length == buffer_size)) {
        SetError(error,
                 "host glGetShaderInfoLog returned an invalid length");
        return false;
    }
    if (!host_log.empty() &&
        !memory_->Write(output_log, host_log.data(), host_log.size())) {
        SetError(error,
                 "native GLES glGetShaderInfoLog guest log is invalid");
        return false;
    }
    if (output_length != 0) {
        const std::int32_t guest_length = actual_length;
        if (!memory_->Write(output_length, &guest_length,
                            sizeof(guest_length))) {
            SetError(error,
                     "native GLES glGetShaderInfoLog guest length is invalid");
            return false;
        }
    }
    return true;
}

bool NativeGles::DeleteShader(std::uint32_t shader, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glDeleteShader requires the owned current native GPU context");
        return false;
    }
    if (!shader_names_.contains(shader)) {
        SetError(error, "native GLES glDeleteShader shader is not owned");
        return false;
    }
    if (delete_pending_shader_names_.contains(shader)) return true;
    ClearGlErrors();
    glDeleteShader(static_cast<GLuint>(shader));
    if (!CheckGlCall("glDeleteShader", error)) return false;
    const auto attachments = shader_program_names_.find(shader);
    if (attachments != shader_program_names_.end() &&
        !attachments->second.empty()) {
        delete_pending_shader_names_.insert(shader);
    } else {
        shader_names_.erase(shader);
    }
    return true;
}

void NativeGles::ReleaseProgramShaderAttachments(std::uint32_t program) {
    auto attachments = shader_program_names_.begin();
    while (attachments != shader_program_names_.end()) {
        attachments->second.erase(program);
        if (!attachments->second.empty()) {
            ++attachments;
            continue;
        }
        const std::uint32_t shader = attachments->first;
        attachments = shader_program_names_.erase(attachments);
        if (delete_pending_shader_names_.erase(shader) != 0)
            shader_names_.erase(shader);
    }
}

bool NativeGles::ResolveVaoTrampolines(
    const Aarch64ImportRegistry* registry, std::string* error) {
    if (vao_slots_.size() != vao_trampoline_targets_.size()) {
        SetError(error, "native GLES VAO slot registration is incomplete");
        return false;
    }
    for (std::size_t index = 0; index < vao_slots_.size(); ++index) {
        const std::optional<std::uint64_t> callable =
            registry->ResolveCallable(vao_trampoline_targets_[index]);
        if (!callable) {
            SetError(error,
                     "native GLES VAO trampoline missing for " +
                         vao_trampoline_targets_[index]);
            return false;
        }
        std::fprintf(stderr,
                     "ISOMR_VAO_SLOT %s slot=%llx trampoline=%llx\n",
                     vao_trampoline_targets_[index].c_str(),
                     static_cast<unsigned long long>(vao_slots_[index]),
                     static_cast<unsigned long long>(*callable));
        if (!heap_->Write(vao_slots_[index], &*callable,
                          sizeof(*callable))) {
            SetError(error, "native GLES VAO trampoline slot write failed");
            return false;
        }
    }
    return true;
}

bool NativeGles::GetProcAddress(const Aarch64ImportRegistry* registry,
                                std::uint64_t name,
                                std::uint64_t* result,
                                std::string* error) {
    constexpr std::uint64_t kMaximumNameLength = 1024;
    if (registry == nullptr || result == nullptr) {
        SetError(error, "native GLES eglGetProcAddress state is invalid");
        return false;
    }
    *result = 0;
    // eglGetProcAddress must work before a context is current: Unity's loader
    // resolves egl*/gl* entry points before (and independently of) binding one.
    if (!initialized_ || shutdown_) {
        SetError(error,
                 "eglGetProcAddress requires the initialized native GPU");
        return false;
    }
    if (name == 0) {
        SetError(error,
                 "native GLES eglGetProcAddress guest name is invalid");
        return false;
    }
    std::uint64_t name_length = 0;
    if (!memory_->BoundedStringLength(name, kMaximumNameLength,
                                      &name_length, error))
        return false;
    if (name_length == kMaximumNameLength) {
        SetError(error,
                 "native GLES eglGetProcAddress guest name exceeds its bound");
        return false;
    }
    std::string function_name(static_cast<std::size_t>(name_length), '\0');
    if (name_length != 0 &&
        !memory_->Read(name, function_name.data(), function_name.size())) {
        SetError(error,
                 "native GLES eglGetProcAddress guest name is unreadable");
        return false;
    }
    const std::optional<std::uint64_t> address =
        registry->Resolve(function_name);
    if (address.has_value()) *result = *address;
    {
        static std::size_t logged_procs = 0;
        const bool log = !address.has_value() || logged_procs < 400;
        if (log) {
            if (address.has_value()) ++logged_procs;
            std::fprintf(stderr, "ISOMR_GETPROC %s -> %s\n",
                         function_name.c_str(),
                         address.has_value() ? "hit" : "MISS");
            std::fflush(stderr);
        }
    }
    return true;
}

bool NativeGles::CreateProgram(std::uint64_t* result,
                               std::string* error) {
    if (result == nullptr) {
        SetError(error, "native GLES glCreateProgram result is null");
        return false;
    }
    *result = 0;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glCreateProgram requires the owned current native GPU context");
        return false;
    }
    ClearGlErrors();
    const GLuint program = glCreateProgram();
    if (program == 0 || !CheckGlCall("glCreateProgram", error)) return false;
    if (program_names_.contains(program)) {
        glDeleteProgram(program);
        SetError(error, "host glCreateProgram returned a duplicate name");
        return false;
    }
    program_names_.insert(program);
    *result = program;
    return true;
}

bool NativeGles::AttachShader(std::uint32_t program,
                              std::uint32_t shader,
                              std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glAttachShader requires the owned current native GPU context");
        return false;
    }
    if (!program_names_.contains(program)) {
        SetError(error, "native GLES glAttachShader program is not owned");
        return false;
    }
    if (!shader_names_.contains(shader)) {
        SetError(error, "native GLES glAttachShader shader is not owned");
        return false;
    }
    ClearGlErrors();
    glAttachShader(program, shader);
    if (!CheckGlCall("glAttachShader", error)) return false;
    shader_program_names_[shader].insert(program);
    return true;
}

bool NativeGles::BindAttribLocation(std::uint32_t program,
                                    std::uint32_t index,
                                    std::uint64_t name,
                                    std::string* error) {
    constexpr std::uint64_t kMaximumNameLength = 1024;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glBindAttribLocation requires the owned current native GPU context");
        return false;
    }
    if (!program_names_.contains(program)) {
        SetError(error,
                 "native GLES glBindAttribLocation program is not owned");
        return false;
    }
    if (name == 0) {
        SetError(error,
                 "native GLES glBindAttribLocation guest name is invalid");
        return false;
    }
    std::uint64_t name_length = 0;
    if (!memory_->BoundedStringLength(name, kMaximumNameLength,
                                      &name_length, error))
        return false;
    if (name_length == kMaximumNameLength) {
        SetError(error,
                 "native GLES glBindAttribLocation guest name exceeds its bound");
        return false;
    }
    std::string attribute_name(static_cast<std::size_t>(name_length) + 1,
                               '\0');
    if (name_length != 0 &&
        !memory_->Read(name, attribute_name.data(),
                       static_cast<std::size_t>(name_length))) {
        SetError(error,
                 "native GLES glBindAttribLocation guest name is unreadable");
        return false;
    }
    ClearGlErrors();
    glBindAttribLocation(program, index, attribute_name.data());
    return CheckGlCall("glBindAttribLocation", error);
}

bool NativeGles::LinkProgram(std::uint32_t program, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glLinkProgram requires the owned current native GPU context");
        return false;
    }
    if (!program_names_.contains(program)) {
        SetError(error, "native GLES glLinkProgram program is not owned");
        return false;
    }
    ClearGlErrors();
    glLinkProgram(program);
    return CheckGlCall("glLinkProgram", error);
}

bool NativeGles::GetProgramInteger(std::uint32_t program,
                                   std::uint32_t name,
                                   std::uint64_t output,
                                   std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glGetProgramiv requires the owned current native GPU context");
        return false;
    }
    if (!program_names_.contains(program)) {
        SetError(error, "native GLES glGetProgramiv program is not owned");
        return false;
    }
    // Query pnames pass through to the host: an unknown enum is a recoverable
    // GL_INVALID_ENUM the guest can read back via glGetError (program-binary
    // probes like GL_PROGRAM_BINARY_LENGTH are optional ES3 queries), not a
    // contract failure worth aborting the session over.
    GLint value = 0;
    ClearGlErrors();
    glGetProgramiv(program, name, &value);
    const GLenum host_error = glGetError();
    if (host_error != GL_NO_ERROR) {
        pending_guest_gl_error_ = static_cast<std::uint32_t>(host_error);
        value = 0;
    }
    const std::int32_t guest_value = value;
    if (output == 0 ||
        !memory_->Write(output, &guest_value, sizeof(guest_value))) {
        SetError(error, "native GLES glGetProgramiv guest output is invalid");
        return false;
    }
    return true;
}

bool NativeGles::GetProgramInfoLog(std::uint32_t program,
                                   std::int32_t buffer_size,
                                   std::uint64_t output_length,
                                   std::uint64_t output_log,
                                   std::string* error) {
    constexpr std::int32_t kMaximumLogBytes = 1024 * 1024;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glGetProgramInfoLog requires the owned current native GPU context");
        return false;
    }
    if (!program_names_.contains(program)) {
        SetError(error,
                 "native GLES glGetProgramInfoLog program is not owned");
        return false;
    }
    if (buffer_size < 0 || buffer_size > kMaximumLogBytes) {
        SetError(error,
                 "native GLES glGetProgramInfoLog buffer size is out of range");
        return false;
    }
    if (buffer_size > 0 && output_log == 0) {
        SetError(error,
                 "native GLES glGetProgramInfoLog guest output is invalid");
        return false;
    }
    std::vector<char> host_log;
    try {
        host_log.resize(static_cast<std::size_t>(buffer_size), '\0');
    } catch (const std::bad_alloc&) {
        SetError(error, "native GLES glGetProgramInfoLog allocation failed");
        return false;
    }
    GLsizei actual_length = 0;
    ClearGlErrors();
    glGetProgramInfoLog(program, buffer_size, &actual_length,
                        host_log.empty() ? nullptr : host_log.data());
    if (!CheckGlCall("glGetProgramInfoLog", error)) return false;
    if (actual_length < 0 || actual_length > buffer_size ||
        (buffer_size > 0 && actual_length == buffer_size)) {
        SetError(error,
                 "host glGetProgramInfoLog returned an invalid length");
        return false;
    }
    if (!host_log.empty() &&
        !memory_->Write(output_log, host_log.data(), host_log.size())) {
        SetError(error,
                 "native GLES glGetProgramInfoLog guest log is invalid");
        return false;
    }
    if (output_length != 0) {
        const std::int32_t guest_length = actual_length;
        if (!memory_->Write(output_length, &guest_length,
                            sizeof(guest_length))) {
            SetError(error,
                     "native GLES glGetProgramInfoLog guest length is invalid");
            return false;
        }
    }
    return true;
}

bool NativeGles::GetActiveVariable(bool uniform, std::uint32_t program,
                                   std::uint32_t index,
                                   std::int32_t buffer_size,
                                   std::uint64_t output_length,
                                   std::uint64_t output_size,
                                   std::uint64_t output_type,
                                   std::uint64_t output_name,
                                   std::string* error) {
    constexpr std::int32_t kMaximumNameBytes = 1024 * 1024;
    const char* call_name = uniform ? "glGetActiveUniform"
                                    : "glGetActiveAttrib";
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error, std::string(call_name) +
                            " requires the owned current native GPU context");
        return false;
    }
    if (!program_names_.contains(program)) {
        SetError(error, std::string("native GLES ") + call_name +
                            " program is not owned");
        return false;
    }
    if (buffer_size < 0 || buffer_size > kMaximumNameBytes) {
        SetError(error, std::string("native GLES ") + call_name +
                            " buffer size is out of range");
        return false;
    }
    if (buffer_size > 0 && output_name == 0) {
        SetError(error, std::string("native GLES ") + call_name +
                            " guest name output is invalid");
        return false;
    }
    std::vector<char> host_name;
    try {
        host_name.resize(static_cast<std::size_t>(buffer_size), '\0');
    } catch (const std::bad_alloc&) {
        SetError(error, std::string("native GLES ") + call_name +
                            " allocation failed");
        return false;
    }
    GLsizei actual_length = 0;
    GLint size = 0;
    GLenum type = 0;
    ClearGlErrors();
    if (uniform) {
        glGetActiveUniform(program, index, buffer_size, &actual_length, &size,
                           &type,
                           host_name.empty() ? nullptr : host_name.data());
    } else {
        glGetActiveAttrib(program, index, buffer_size, &actual_length, &size,
                          &type,
                          host_name.empty() ? nullptr : host_name.data());
    }
    if (!CheckGlCall(call_name, error)) return false;
    if (actual_length < 0 || actual_length > buffer_size || size < 0 ||
        (buffer_size > 0 && actual_length == buffer_size)) {
        SetError(error, std::string("host ") + call_name +
                            " returned invalid metadata");
        return false;
    }
    if (!host_name.empty() &&
        !memory_->Write(output_name, host_name.data(), host_name.size())) {
        SetError(error, std::string("native GLES ") + call_name +
                            " guest name output is invalid");
        return false;
    }
    const std::int32_t guest_length = actual_length;
    const std::int32_t guest_size = size;
    const std::uint32_t guest_type = type;
    if ((output_length != 0 &&
         !memory_->Write(output_length, &guest_length,
                         sizeof(guest_length))) ||
        (output_size != 0 &&
         !memory_->Write(output_size, &guest_size, sizeof(guest_size))) ||
        (output_type != 0 &&
         !memory_->Write(output_type, &guest_type, sizeof(guest_type)))) {
        SetError(error, std::string("native GLES ") + call_name +
                            " guest metadata output is invalid");
        return false;
    }
    return true;
}

bool NativeGles::GetVariableLocation(bool uniform,
                                     std::uint32_t program,
                                     std::uint64_t name,
                                     std::uint64_t* result,
                                     std::string* error) {
    constexpr std::uint64_t kMaximumNameLength = 1024;
    const char* call_name = uniform ? "glGetUniformLocation"
                                    : "glGetAttribLocation";
    if (result == nullptr) {
        SetError(error, std::string("native GLES ") + call_name +
                            " result is null");
        return false;
    }
    *result = 0;
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error, std::string(call_name) +
                            " requires the owned current native GPU context");
        return false;
    }
    if (!program_names_.contains(program)) {
        SetError(error, std::string("native GLES ") + call_name +
                            " program is not owned");
        return false;
    }
    if (name == 0) {
        SetError(error, std::string("native GLES ") + call_name +
                            " guest name is invalid");
        return false;
    }
    std::uint64_t name_length = 0;
    if (!memory_->BoundedStringLength(name, kMaximumNameLength,
                                      &name_length, error))
        return false;
    if (name_length == kMaximumNameLength) {
        SetError(error, std::string("native GLES ") + call_name +
                            " guest name exceeds its bound");
        return false;
    }
    std::string variable_name(static_cast<std::size_t>(name_length) + 1,
                              '\0');
    if (name_length != 0 &&
        !memory_->Read(name, variable_name.data(),
                       static_cast<std::size_t>(name_length))) {
        SetError(error, std::string("native GLES ") + call_name +
                            " guest name is unreadable");
        return false;
    }
    ClearGlErrors();
    const GLint location = uniform
                               ? glGetUniformLocation(program,
                                                      variable_name.data())
                               : glGetAttribLocation(program,
                                                     variable_name.data());
    if (!CheckGlCall(call_name, error)) return false;
    *result = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(location));
    return true;
}

bool NativeGles::UseProgram(std::uint32_t program, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glUseProgram requires the owned current native GPU context");
        return false;
    }
    if (program != 0 && !program_names_.contains(program)) {
        SetError(error, "native GLES glUseProgram program is not owned");
        return false;
    }
    const std::uint32_t previous_program = bound_program_;
    ClearGlErrors();
    glUseProgram(program);
    if (!CheckGlCall("glUseProgram", error)) return false;
    if (previous_program != program &&
        delete_pending_program_names_.erase(previous_program) != 0) {
        ReleaseProgramShaderAttachments(previous_program);
        program_names_.erase(previous_program);
    }
    bound_program_ = program;
    return true;
}

bool NativeGles::Uniform1i(std::int32_t location, std::int32_t value,
                           std::string* error) {
    if (!initialized_ || shutdown_ || !is_current() || bound_program_ == 0 ||
        !program_names_.contains(bound_program_)) {
        SetError(error,
                 "glUniform1i requires an owned current program and context");
        return false;
    }
    ClearGlErrors();
    glUniform1i(static_cast<GLint>(location), static_cast<GLint>(value));
    return CheckGlCall("glUniform1i", error);
}

bool NativeGles::Uniform1f(std::int32_t location, float value,
                           std::string* error) {
    if (!initialized_ || shutdown_ || !is_current() || bound_program_ == 0 ||
        !program_names_.contains(bound_program_)) {
        SetError(error,
                 "glUniform1f requires an owned current program and context");
        return false;
    }
    ClearGlErrors();
    glUniform1f(static_cast<GLint>(location), static_cast<GLfloat>(value));
    return CheckGlCall("glUniform1f", error);
}

bool NativeGles::Uniform1fv(std::int32_t location, std::int32_t count,
                            std::uint64_t values, std::string* error) {
    constexpr std::size_t kMaximumFloats =
        (64U * 1024U * 1024U) / sizeof(GLfloat);
    if (!initialized_ || shutdown_ || !is_current() || bound_program_ == 0 ||
        !program_names_.contains(bound_program_)) {
        SetError(error,
                 "glUniform1fv requires an owned current program and context");
        return false;
    }
    if (count < 0 ||
        static_cast<std::size_t>(count) > kMaximumFloats) {
        SetError(error, "native GLES glUniform1fv count is out of range");
        return false;
    }
    if (location == -1) return true;
    std::vector<GLfloat> host_values(static_cast<std::size_t>(count));
    if (!host_values.empty() &&
        (values == 0 ||
         !memory_->Read(values, host_values.data(),
                        host_values.size() * sizeof(host_values.front())))) {
        SetError(error, "native GLES glUniform1fv guest values are invalid");
        return false;
    }
    ClearGlErrors();
    glUniform1fv(static_cast<GLint>(location), static_cast<GLsizei>(count),
                 host_values.empty() ? nullptr : host_values.data());
    return CheckGlCall("glUniform1fv", error);
}

bool NativeGles::Uniform2fv(std::int32_t location, std::int32_t count,
                            std::uint64_t values, std::string* error) {
    constexpr std::size_t kMaximumFloats =
        (64U * 1024U * 1024U) / sizeof(GLfloat);
    if (!initialized_ || shutdown_ || !is_current() || bound_program_ == 0 ||
        !program_names_.contains(bound_program_)) {
        SetError(error,
                 "glUniform2fv requires an owned current program and context");
        return false;
    }
    if (count < 0 ||
        static_cast<std::size_t>(count) > kMaximumFloats / 2U) {
        SetError(error, "native GLES glUniform2fv count is out of range");
        return false;
    }
    if (location == -1) return true;
    std::vector<GLfloat> host_values(static_cast<std::size_t>(count) * 2U);
    if (!host_values.empty() &&
        (values == 0 ||
         !memory_->Read(values, host_values.data(),
                        host_values.size() * sizeof(host_values.front())))) {
        SetError(error, "native GLES glUniform2fv guest values are invalid");
        return false;
    }
    ClearGlErrors();
    glUniform2fv(static_cast<GLint>(location), static_cast<GLsizei>(count),
                 host_values.empty() ? nullptr : host_values.data());
    return CheckGlCall("glUniform2fv", error);
}

bool NativeGles::Uniform4f(std::int32_t location, float x, float y, float z,
                           float w, std::string* error) {
    if (!initialized_ || shutdown_ || !is_current() || bound_program_ == 0 ||
        !program_names_.contains(bound_program_)) {
        SetError(error,
                 "glUniform4f requires an owned current program and context");
        return false;
    }
    ClearGlErrors();
    glUniform4f(static_cast<GLint>(location), static_cast<GLfloat>(x),
                static_cast<GLfloat>(y), static_cast<GLfloat>(z),
                static_cast<GLfloat>(w));
    return CheckGlCall("glUniform4f", error);
}

bool NativeGles::Uniform4fv(std::int32_t location, std::int32_t count,
                            std::uint64_t values, std::string* error) {
    constexpr std::size_t kMaximumFloats =
        (64U * 1024U * 1024U) / sizeof(GLfloat);
    if (!initialized_ || shutdown_ || !is_current() || bound_program_ == 0 ||
        !program_names_.contains(bound_program_)) {
        SetError(error,
                 "glUniform4fv requires an owned current program and context");
        return false;
    }
    if (count < 0 ||
        static_cast<std::size_t>(count) > kMaximumFloats / 4U) {
        SetError(error, "native GLES glUniform4fv count is out of range");
        return false;
    }
    if (location == -1) return true;
    std::vector<GLfloat> host_values(static_cast<std::size_t>(count) * 4U);
    if (!host_values.empty() &&
        (values == 0 ||
         !memory_->Read(values, host_values.data(),
                        host_values.size() * sizeof(host_values.front())))) {
        SetError(error, "native GLES glUniform4fv guest values are invalid");
        return false;
    }
    ClearGlErrors();
    glUniform4fv(static_cast<GLint>(location), static_cast<GLsizei>(count),
                 host_values.empty() ? nullptr : host_values.data());
    return CheckGlCall("glUniform4fv", error);
}

bool NativeGles::UniformMatrix4fv(std::int32_t location,
                                  std::int32_t count, bool transpose,
                                  std::uint64_t values,
                                  std::string* error) {
    constexpr std::size_t kMaximumFloats =
        (64U * 1024U * 1024U) / sizeof(GLfloat);
    if (!initialized_ || shutdown_ || !is_current() || bound_program_ == 0 ||
        !program_names_.contains(bound_program_)) {
        SetError(error,
                 "glUniformMatrix4fv requires an owned current program and "
                 "context");
        return false;
    }
    if (transpose) {
        SetError(error,
                 "native GLES glUniformMatrix4fv transpose must be false");
        return false;
    }
    if (count < 0 ||
        static_cast<std::size_t>(count) > kMaximumFloats / 16U) {
        SetError(error,
                 "native GLES glUniformMatrix4fv count is out of range");
        return false;
    }
    if (location == -1) return true;
    std::vector<GLfloat> host_values(static_cast<std::size_t>(count) * 16U);
    if (!host_values.empty() &&
        (values == 0 ||
         !memory_->Read(values, host_values.data(),
                        host_values.size() * sizeof(host_values.front())))) {
        SetError(error,
                 "native GLES glUniformMatrix4fv guest values are invalid");
        return false;
    }
    ClearGlErrors();
    glUniformMatrix4fv(
        static_cast<GLint>(location), static_cast<GLsizei>(count), GL_FALSE,
        host_values.empty() ? nullptr : host_values.data());
    return CheckGlCall("glUniformMatrix4fv", error);
}

bool NativeGles::DeleteProgram(std::uint32_t program,
                               std::string* error) {
    if (!initialized_ || shutdown_ || !is_current()) {
        SetError(error,
                 "glDeleteProgram requires the owned current native GPU context");
        return false;
    }
    if (!program_names_.contains(program)) {
        SetError(error, "native GLES glDeleteProgram program is not owned");
        return false;
    }
    if (delete_pending_program_names_.contains(program)) return true;
    ClearGlErrors();
    glDeleteProgram(program);
    if (!CheckGlCall("glDeleteProgram", error)) return false;
    if (bound_program_ == program) {
        delete_pending_program_names_.insert(program);
    } else {
        ReleaseProgramShaderAttachments(program);
        program_names_.erase(program);
    }
    return true;
}

bool NativeGles::DeletePrograms(std::string* error) {
    if (bound_program_ != 0 && !UseProgram(0, error)) return false;
    if (program_names_.empty()) {
        delete_pending_program_names_.clear();
        return true;
    }
    ClearGlErrors();
    for (const std::uint32_t program : program_names_)
        glDeleteProgram(program);
    if (!CheckGlCall("glDeleteProgram", error)) return false;
    for (const std::uint32_t program : program_names_)
        ReleaseProgramShaderAttachments(program);
    program_names_.clear();
    delete_pending_program_names_.clear();
    bound_program_ = 0;
    return true;
}

bool NativeGles::DeleteShaders(std::string* error) {
    if (shader_names_.empty()) return true;
    ClearGlErrors();
    for (const std::uint32_t shader : shader_names_) glDeleteShader(shader);
    if (!CheckGlCall("glDeleteShader", error)) return false;
    shader_names_.clear();
    delete_pending_shader_names_.clear();
    shader_program_names_.clear();
    return true;
}

bool NativeGles::Shutdown(std::string* error) {
    if (shutdown_) {
        SetError(error, "native GLES is already shut down");
        return false;
    }
    if (owner_thread_id_ != 0 &&
        owner_thread_id_ != GetCurrentThreadId()) {
        SetError(error, "native GLES shutdown requires its owner thread");
        return false;
    }
    if (rendering_context_ != EGL_NO_CONTEXT && !is_current()) {
        SetError(error, "native GLES shutdown requires its current context");
        return false;
    }
    for (std::uint64_t& address : string_guest_addresses_) {
        if (address == 0) continue;
        if (!heap_->Release(address, error)) return false;
        address = 0;
    }
    if (!DeletePrograms(error) || !DeleteShaders(error)) return false;
    if (!framebuffer_names_.empty()) {
        const std::vector<GLuint> framebuffers(framebuffer_names_.begin(),
                                               framebuffer_names_.end());
        ClearGlErrors();
        glDeleteFramebuffers(static_cast<GLsizei>(framebuffers.size()),
                             framebuffers.data());
        if (!CheckGlCall("glDeleteFramebuffers", error)) return false;
        framebuffer_names_.clear();
    }
    if (!renderbuffer_names_.empty()) {
        const std::vector<GLuint> renderbuffers(renderbuffer_names_.begin(),
                                                renderbuffer_names_.end());
        ClearGlErrors();
        glDeleteRenderbuffers(static_cast<GLsizei>(renderbuffers.size()),
                              renderbuffers.data());
        if (!CheckGlCall("glDeleteRenderbuffers", error)) return false;
        renderbuffer_names_.clear();
    }
    if (!texture_names_.empty()) {
        const std::vector<GLuint> textures(texture_names_.begin(),
                                           texture_names_.end());
        glDeleteTextures(static_cast<GLsizei>(textures.size()),
                         textures.data());
        texture_names_.clear();
    }
    bound_texture_by_unit_target_.clear();
    active_texture_unit_ = 0;
    bound_renderbuffer_ = 0;
    bound_framebuffer_ = 0;
    vertex_attribute_pointers_.clear();
    vertex_attribute_staging_.clear();
    if (rendering_context_ != EGL_NO_CONTEXT &&
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT) != EGL_TRUE) {
        SetError(error,
                 "native GLES could not clear its context during shutdown");
        return false;
    }
    if (!DestroyHostObjects(error)) return false;
    for (std::uint64_t& slot : vao_slots_) {
        if (slot != 0 && !heap_->Release(slot, error)) return false;
        slot = 0;
    }
    vao_slots_.clear();
    initialized_ = false;
    shutdown_ = true;
    return true;
}

bool NativeGles::DestroyHostObjects(std::string* error) {
    const bool owns_host_objects =
        window_state_ != nullptr || window_ != nullptr ||
        device_context_ != nullptr || display_ != EGL_NO_DISPLAY ||
        surface_ != EGL_NO_SURFACE ||
        rendering_context_ != EGL_NO_CONTEXT;
    if (owns_host_objects &&
        (owner_thread_id_ == 0 ||
         owner_thread_id_ != GetCurrentThreadId())) {
        SetError(error,
                 "native GLES host teardown requires its owner thread");
        return false;
    }
    if (display_ != EGL_NO_DISPLAY &&
        eglGetCurrentContext() == rendering_context_) {
        if (eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT) != EGL_TRUE) {
            SetError(error,
                     "native GLES could not clear its context during host teardown");
            return false;
        }
    }
    if (display_ != EGL_NO_DISPLAY &&
        rendering_context_ != EGL_NO_CONTEXT) {
        if (eglDestroyContext(display_, rendering_context_) != EGL_TRUE) {
            SetError(error,
                     "native GLES could not destroy its ANGLE context");
            return false;
        }
        rendering_context_ = EGL_NO_CONTEXT;
    }
    if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) {
        if (eglDestroySurface(display_, surface_) != EGL_TRUE) {
            SetError(error,
                     "native GLES could not destroy its ANGLE surface");
            return false;
        }
        surface_ = EGL_NO_SURFACE;
    }
    // Render-thread shared contexts/surfaces (Unity). These may still be bound
    // to the render thread; eglDestroy* flags them for deletion once unbound,
    // so tearing them down here is safe and best-effort.
    {
        std::scoped_lock lock(guest_egl_mutex_);
        for (const auto& entry : guest_render_contexts_) {
            if (entry.second != EGL_NO_CONTEXT &&
                entry.second != rendering_context_) {
                (void)eglDestroyContext(display_, entry.second);
            }
        }
        for (const auto& entry : guest_render_surfaces_) {
            if (entry.second != EGL_NO_SURFACE && entry.second != surface_) {
                (void)eglDestroySurface(display_, entry.second);
            }
        }
        guest_render_contexts_.clear();
        guest_render_surfaces_.clear();
    }
    surface_width_ = 0;
    surface_height_ = 0;
    if (display_ != EGL_NO_DISPLAY) {
        if (eglTerminate(display_) != EGL_TRUE) {
            SetError(error,
                     "native GLES could not terminate its ANGLE display");
            return false;
        }
        display_ = EGL_NO_DISPLAY;
    }
    if (device_context_ != nullptr && window_ != nullptr) {
        if (ReleaseDC(window_, device_context_) == 0) {
            SetError(error,
                     "native GLES could not release its Win32 device context");
            return false;
        }
        device_context_ = nullptr;
    } else if (device_context_ != nullptr) {
        SetError(error,
                 "native GLES device context has no owning Win32 window");
        return false;
    }
    // In hosted mode the fixed render-target host is a separate window from
    // the display child; destroy it explicitly (it is also a child of window_
    // and would be destroyed with it, but the explicit order is clearer and
    // keeps the EGL surface destroyed before either HWND goes away).
    if (surface_window_ != nullptr && surface_window_ != window_) {
        if (IsWindow(surface_window_))
            (void)DestroyWindow(surface_window_);
        surface_window_ = nullptr;
    }
    if (window_ != nullptr) {
        if (!DestroyWindow(window_)) {
            SetError(error,
                     "native GLES could not destroy its Win32 window");
            return false;
        }
        window_ = nullptr;
    }
    surface_window_ = nullptr;
    window_state_.reset();
    hardware_accelerated_ = false;
    owner_thread_id_ = 0;
    return true;
}

}  // namespace isomr
