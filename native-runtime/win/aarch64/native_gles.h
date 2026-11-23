#pragma once

#include <EGL/egl.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct HDC__;
struct HWND__;

namespace isomr {

class Aarch64ImportRegistry;
class GuestAddressSpace;
class GuestHeap;
struct NativeWindowState;

enum class NativePointerAction : std::uint8_t {
    kDown = 0,
    kUp = 1,
    kMove = 2,
    kCancel = 3,
};

struct NativePointerEvent {
    NativePointerAction action = NativePointerAction::kCancel;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t client_width = 0;
    std::int32_t client_height = 0;
    // Address-free monotonic timestamp captured when the owner thread accepts
    // the Win32 message. Guest delivery remains queued and non-reentrant.
    std::uint64_t enqueued_ns = 0;
};

enum class NativeKeyAction : std::uint8_t {
    // WM_CHAR committed text (UTF-16 code unit). Delivers a character.
    kChar = 0,
    // WM_KEYDOWN / WM_KEYUP virtual-key transitions (deliver a keycode).
    kKeyDown = 1,
    kKeyUp = 2,
};

struct NativeKeyEvent {
    NativeKeyAction action = NativeKeyAction::kChar;
    // UTF-16 code unit for kChar; Win32 virtual-key code for key transitions.
    std::uint32_t code = 0;
    std::uint64_t enqueued_ns = 0;
};

class NativeGles {
public:
    NativeGles(GuestHeap* heap, GuestAddressSpace* memory);
    ~NativeGles();

    NativeGles(const NativeGles&) = delete;
    NativeGles& operator=(const NativeGles&) = delete;

    bool Initialize(std::string* error);
    bool RegisterImports(Aarch64ImportRegistry* registry,
                         std::string* error);
    bool RegisterDeferredGlesImports(Aarch64ImportRegistry* registry,
                                     std::string* error);
    // Registers the guest-visible EGL + ANativeWindow surface lifecycle.
    // Unity's GfxDevice runs on a dedicated render thread and drives EGL
    // itself (eglCreateContext/eglCreateWindowSurface/eglMakeCurrent/
    // eglSwapBuffers), so each non-owner thread gets a shared host context and
    // its own surface; Present() then composites that surface to the window.
    bool RegisterGuestSurfaceImports(Aarch64ImportRegistry* registry,
                                     std::string* error);
    bool MakeCurrent(std::string* error);
    bool ClearCurrent(std::string* error);
    bool Show(std::string* error);
    // Requests the normal owner-window WM_CLOSE path. This is used by
    // bounded local lifecycle tests; production close still originates from
    // the user's Win32 window action.
    bool RequestClose(std::string* error);
    // Dispatches owner-thread Win32 messages without exposing or consuming
    // the pointer-event queue used by guest delivery.
    bool PumpOwnerMessages(std::string* error);
    bool PumpEvents(bool* close_requested,
                    std::vector<NativePointerEvent>* pointer_events,
                    std::string* error);
    bool AcknowledgePointerEvent(const NativePointerEvent& event,
                                 std::string* error);
    // Drains the queued text/key events the window procedure captured during
    // the most recent owner-thread pump. Unlike pointer events these are
    // fire-and-forget: delivery failure aborts the pump loop, so the queue is
    // consumed wholesale here. Owner thread only, non-reentrant.
    bool DrainKeyEvents(std::vector<NativeKeyEvent>* key_events,
                        std::string* error);
    bool Present(std::string_view source, std::string* error);
    bool Shutdown(std::string* error);

    // Process-wide present pacing: after each successful swap, wait until
    // 1/fps since the previous present. Zero disables pacing (default). The
    // executable's fast guest paths let the guest engine outrun real time,
    // and Candy's engine ties game speed to frames, so a 60 fps cap restores
    // normal speed. One NativeGles instance exists per process.
    static void SetProcessPresentPacingFps(double fps) {
        process_present_pacing_fps_.store(
            fps, std::memory_order_relaxed);
    }

    // Process-wide initial host window client size. Zero keeps the built-in
    // 1280x720 default. The guest receives the same size through the bridge
    // initialize/screen-size stages, and pointer events are already scaled to
    // the live client rectangle, so any orientation works.
    static void SetProcessInitialSurfaceSize(std::int32_t width,
                                             std::int32_t height) {
        process_initial_surface_width_.store(
            width, std::memory_order_relaxed);
        process_initial_surface_height_.store(
            height, std::memory_order_relaxed);
    }

    // Process-wide host shell window for the ARMR game window. When set,
    // Initialize creates the input/present surface as a child of this shell
    // instead of a standalone top-level window, and creates a separate
    // fixed-size surface window for the EGL render target so the guest
    // logical size stays fixed while the host scales the presented image.
    // Must be called before Initialize on the owning thread.
    static void SetProcessHostWindow(HWND__* host_window) {
        process_host_window_.store(host_window, std::memory_order_relaxed);
    }

    // The fixed guest logical surface size Initialize will use (the process
    // initial surface size, defaulting to 1280x720). The host shell reads
    // this to size its aspect-fit viewport before the surface exists.
    static void ProcessLogicalSurfaceSize(std::int32_t* width,
                                          std::int32_t* height) {
        const std::int32_t w =
            process_initial_surface_width_.load(std::memory_order_relaxed);
        const std::int32_t h =
            process_initial_surface_height_.load(std::memory_order_relaxed);
        if (width != nullptr) *width = w > 0 ? w : 1280;
        if (height != nullptr) *height = h > 0 ? h : 720;
    }

    bool initialized() const { return initialized_; }
    bool is_current() const;
    bool hardware_accelerated() const { return hardware_accelerated_; }
    std::size_t guest_string_count() const {
        std::size_t count = 0;
        for (const std::uint64_t address : string_guest_addresses_)
            if (address != 0) ++count;
        return count;
    }
    std::size_t texture_count() const { return texture_names_.size(); }
    std::size_t renderbuffer_count() const {
        return renderbuffer_names_.size();
    }
    std::size_t framebuffer_count() const {
        return framebuffer_names_.size();
    }
    std::size_t shader_count() const { return shader_names_.size(); }
    std::size_t program_count() const { return program_names_.size(); }
    std::size_t vertex_attribute_pointer_count() const {
        return vertex_attribute_pointers_.size();
    }
    std::size_t draw_call_count() const { return draw_call_count_; }
    std::size_t draw_readback_count() const { return draw_readback_count_; }
    std::size_t inactive_enabled_attribute_skip_count() const {
        return inactive_enabled_attribute_skip_count_;
    }
    std::uint32_t bound_framebuffer() const { return bound_framebuffer_; }
    std::size_t last_sampled_pixel_count() const {
        return last_sampled_pixel_count_;
    }
    std::size_t last_lit_pixel_count() const {
        return last_lit_pixel_count_;
    }
    std::size_t last_nonzero_alpha_pixel_count() const {
        return last_nonzero_alpha_pixel_count_;
    }
    std::uint32_t last_max_rgb() const { return last_max_rgb_; }
    bool lit_pixel_observed() const { return lit_pixel_observed_; }
    bool window_visible() const;
    HWND__* window_handle() const { return window_; }
    // True once the hosted shell delivered a cooperative close to the guest
    // surface. Only meaningful in hosted mode — a standalone window keeps its
    // own close flag and is not reported here so headless/legacy keep-alives
    // are unchanged.
    bool window_close_requested() const;
    std::size_t pending_pointer_event_count() const;
    std::size_t present_count() const { return present_count_; }
    std::size_t lit_present_count() const { return lit_present_count_; }
    std::size_t present_readback_count() const {
        return present_readback_count_;
    }

private:
    static std::atomic<double> process_present_pacing_fps_;
    static std::atomic<std::int32_t> process_initial_surface_width_;
    static std::atomic<std::int32_t> process_initial_surface_height_;
    static std::atomic<HWND__*> process_host_window_;
    void PaceAfterSwap();

    bool GetString(std::uint32_t name, std::uint64_t* result,
                   std::string* error);
    bool GetStringIndexed(std::uint32_t name, std::uint32_t index,
                          std::uint64_t* result, std::string* error);
    bool GetInteger(std::uint32_t name, std::uint64_t output,
                    std::string* error);
    bool GenerateTextures(std::uint64_t count, std::uint64_t output,
                          std::string* error);
    bool DeleteTextures(std::uint64_t count, std::uint64_t input,
                        std::string* error);
    bool ActiveTexture(std::uint32_t texture, std::string* error);
    bool BindTexture(std::uint32_t target, std::uint32_t texture,
                     std::string* error);
    // Bound texture name on the active unit for an upload target: either
    // GL_TEXTURE_2D or a cube-face target (POSITIVE_X..NEGATIVE_Z). Returns 0
    // when the target is not an upload target or nothing is bound.
    std::uint32_t BoundTextureForUploadTarget(std::uint32_t target) const;
    bool TextureParameter(std::uint32_t target, std::uint32_t name,
                          std::int32_t value, std::string* error);
    bool GenerateRenderbuffers(std::uint64_t count, std::uint64_t output,
                               std::string* error);
    bool BindRenderbuffer(std::uint32_t target, std::uint32_t renderbuffer,
                          std::string* error);
    bool RenderbufferStorage(std::uint32_t target,
                             std::uint32_t internal_format,
                             std::int32_t width, std::int32_t height,
                             std::string* error);
    bool DeleteRenderbuffers(std::uint64_t count, std::uint64_t input,
                             std::string* error);
    bool GenerateFramebuffers(std::uint64_t count, std::uint64_t output,
                              std::string* error);
    bool BindFramebuffer(std::uint32_t target, std::uint32_t framebuffer,
                         std::string* error);
    bool FramebufferTexture2d(std::uint32_t target,
                              std::uint32_t attachment,
                              std::uint32_t texture_target,
                              std::uint32_t texture, std::int32_t level,
                              std::string* error);
    bool FramebufferRenderbuffer(std::uint32_t target,
                                 std::uint32_t attachment,
                                 std::uint32_t renderbuffer_target,
                                 std::uint32_t renderbuffer,
                                 std::string* error);
    bool CheckFramebufferStatus(std::uint32_t target, std::uint64_t* result,
                                std::string* error);
    bool DeleteFramebuffers(std::uint64_t count, std::uint64_t input,
                            std::string* error);
    bool Viewport(std::int32_t x, std::int32_t y, std::int32_t width,
                  std::int32_t height, std::string* error);
    bool DepthRange(float near_value, float far_value, std::string* error);
    bool ClearColor(float red, float green, float blue, float alpha,
                    std::string* error);
    bool ClearDepth(float depth, std::string* error);
    bool ClearStencil(std::int32_t stencil, std::string* error);
    bool StencilMask(std::uint32_t mask, std::string* error);
    bool StencilFunction(std::uint32_t function, std::int32_t reference,
                         std::uint32_t mask, std::string* error);
    bool StencilOperation(std::uint32_t sfail, std::uint32_t dpfail,
                          std::uint32_t dppass, std::string* error);
    bool StencilFunctionSeparate(std::uint32_t face, std::uint32_t function,
                                 std::int32_t reference, std::uint32_t mask,
                                 std::string* error);
    bool StencilOperationSeparate(std::uint32_t face, std::uint32_t sfail,
                                  std::uint32_t dpfail, std::uint32_t dppass,
                                  std::string* error);
    bool Clear(std::uint32_t mask, std::string* error);
    bool SetCapability(std::uint32_t capability, bool enabled,
                       std::string* error);
    bool ColorMask(bool red, bool green, bool blue, bool alpha,
                   std::string* error);
    bool DepthMask(bool enabled, std::string* error);
    bool BlendFunction(std::uint32_t source, std::uint32_t destination,
                       std::string* error);
    bool BlendColor(float red, float green, float blue, float alpha,
                    std::string* error);
    bool BlendFunctionSeparate(std::uint32_t source_rgb,
                               std::uint32_t destination_rgb,
                               std::uint32_t source_alpha,
                               std::uint32_t destination_alpha,
                               std::string* error);
    bool BlendEquation(std::uint32_t mode, std::string* error);
    bool BlendEquationSeparate(std::uint32_t rgb_mode,
                               std::uint32_t alpha_mode,
                               std::string* error);
    bool DepthFunction(std::uint32_t function, std::string* error);
    bool CullFace(std::uint32_t mode, std::string* error);
    bool FrontFace(std::uint32_t winding, std::string* error);
    bool Scissor(std::int32_t x, std::int32_t y, std::int32_t width,
                 std::int32_t height, std::string* error);
    bool SetVertexAttributeArray(std::uint32_t index, bool enabled,
                                 std::string* error);
    bool VertexAttributePointer(std::uint32_t index, std::int32_t size,
                                std::uint32_t type, bool normalized,
                                std::int32_t stride, std::uint64_t pointer,
                                std::string* error);
    bool VertexAttribute2fv(std::uint32_t index, std::uint64_t values,
                            std::string* error);
    bool VertexAttribute3fv(std::uint32_t index, std::uint64_t values,
                            std::string* error);
    bool VertexAttribute4fv(std::uint32_t index, std::uint64_t values,
                            std::string* error);
    bool DrawElements(std::uint32_t mode, std::int32_t count,
                      std::uint32_t type, std::uint64_t indices,
                      std::string* error);
    bool PixelStore(std::uint32_t name, std::int32_t value,
                    std::string* error);
    bool TexImage2d(std::uint32_t target, std::int32_t level,
                    std::int32_t internal_format, std::int32_t width,
                    std::int32_t height, std::int32_t border,
                    std::uint32_t format, std::uint32_t type,
                    std::uint64_t stack_pointer, std::string* error);
    bool TexSubImage2d(std::uint32_t target, std::int32_t level,
                       std::int32_t x_offset, std::int32_t y_offset,
                       std::int32_t width, std::int32_t height,
                       std::uint32_t format, std::uint32_t type,
                       std::uint64_t stack_pointer, std::string* error);
    bool CreateShader(std::uint32_t type, std::uint64_t* result,
                      std::string* error);
    bool ShaderSource(std::uint32_t shader, std::int32_t count,
                      std::uint64_t strings, std::uint64_t lengths,
                      std::string* error);
    bool CompileShader(std::uint32_t shader, std::string* error);
    bool GetShaderInteger(std::uint32_t shader, std::uint32_t name,
                          std::uint64_t output, std::string* error);
    bool GetShaderInfoLog(std::uint32_t shader, std::int32_t buffer_size,
                          std::uint64_t output_length,
                          std::uint64_t output_log, std::string* error);
    bool DeleteShader(std::uint32_t shader, std::string* error);
    void ReleaseProgramShaderAttachments(std::uint32_t program);
    bool GetProcAddress(const Aarch64ImportRegistry* registry,
                        std::uint64_t name, std::uint64_t* result,
                        std::string* error);
    bool CreateProgram(std::uint64_t* result, std::string* error);
    bool AttachShader(std::uint32_t program, std::uint32_t shader,
                      std::string* error);
    bool BindAttribLocation(std::uint32_t program, std::uint32_t index,
                            std::uint64_t name, std::string* error);
    bool LinkProgram(std::uint32_t program, std::string* error);
    bool GetProgramInteger(std::uint32_t program, std::uint32_t name,
                           std::uint64_t output, std::string* error);
    bool GetProgramInfoLog(std::uint32_t program, std::int32_t buffer_size,
                           std::uint64_t output_length,
                           std::uint64_t output_log, std::string* error);
    bool GetActiveVariable(bool uniform, std::uint32_t program,
                           std::uint32_t index, std::int32_t buffer_size,
                           std::uint64_t output_length,
                           std::uint64_t output_size,
                           std::uint64_t output_type,
                           std::uint64_t output_name, std::string* error);
    bool GetVariableLocation(bool uniform, std::uint32_t program,
                             std::uint64_t name, std::uint64_t* result,
                             std::string* error);
    bool UseProgram(std::uint32_t program, std::string* error);
    bool Uniform1i(std::int32_t location, std::int32_t value,
                   std::string* error);
    bool Uniform1f(std::int32_t location, float value, std::string* error);
    bool Uniform1fv(std::int32_t location, std::int32_t count,
                    std::uint64_t values, std::string* error);
    bool Uniform2fv(std::int32_t location, std::int32_t count,
                    std::uint64_t values, std::string* error);
    bool Uniform4f(std::int32_t location, float x, float y, float z, float w,
                   std::string* error);
    bool Uniform4fv(std::int32_t location, std::int32_t count,
                    std::uint64_t values, std::string* error);
    bool UniformMatrix4fv(std::int32_t location, std::int32_t count,
                          bool transpose, std::uint64_t values,
                          std::string* error);
    bool DeleteProgram(std::uint32_t program, std::string* error);
    bool DeletePrograms(std::string* error);
    bool DeleteShaders(std::string* error);
    bool DestroyHostObjects(std::string* error);
    void DestroyHostObjects() {
        (void)DestroyHostObjects(nullptr);
    }

    GuestHeap* heap_ = nullptr;
    GuestAddressSpace* memory_ = nullptr;
    std::unique_ptr<NativeWindowState> window_state_;
    HWND__* window_ = nullptr;
    // Hosted mode: the EGL render target lives on this separate fixed-size
    // window so the guest logical size stays constant while the display
    // child (window_) is resized/scaled by the host shell. Equals window_
    // in standalone mode.
    HWND__* surface_window_ = nullptr;
    HDC__* device_context_ = nullptr;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext rendering_context_ = EGL_NO_CONTEXT;
    std::int32_t surface_width_ = 0;
    std::int32_t surface_height_ = 0;
    // A GL error the guest-visible glGetError must return before delegating to
    // the host. Set when an unsupported pname/query is forwarded and the host
    // error is consumed during validation; glGetError reports it once.
    std::uint32_t pending_guest_gl_error_ = 0;
    std::array<std::uint64_t, 5> string_guest_addresses_{};
    // Guest-copied per-extension strings for glGetStringi, keyed by index.
    std::unordered_map<std::uint32_t, std::uint64_t>
        indexed_string_guest_addresses_;
    std::unordered_set<std::uint32_t> texture_names_;
    // 2D texture name → set of defined (target, level) image faces, packed as
    // (target << 16) | level, whose storage was defined by
    // glTexImage2D/glCompressedTexImage2D. Unity issues glTexSubImage2D
    // uploads against faces it expects pre-defined; on an undefined face a
    // real driver returns GL_INVALID_OPERATION, so the sub-upload path
    // allocates the face first.
    std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>>
        texture_2d_defined_levels_;
    std::unordered_set<std::uint32_t> vertex_array_names_;
    std::vector<std::uint64_t> vao_slots_;
    std::uint32_t bound_array_buffer_ = 0;
    std::uint32_t screen_fbo_ = 0;
    bool screen_fbo_attached_ = false;
    // Mapped buffer storage: target → {guest address, size}. The guest
    // writes vertex/index data through the mapped guest address (JIT
    // stores land in the committed arena backing); glUnmapBufferOES
    // flushes the block into the host GL buffer.
    std::unordered_map<std::uint32_t, std::pair<std::uint64_t, std::uint64_t>>
        mapped_buffer_storage_;
    // ES3 glMapBufferRange state, keyed by the buffer object name bound at
    // map time. The guest-visible pointer aliases guest heap; the range is
    // flushed into the host buffer on glUnmapBuffer /
    // glFlushMappedBufferRange.
    struct MappedRange {
        std::uint32_t target = 0;
        std::uint64_t offset = 0;
        std::uint64_t length = 0;
        std::uint32_t access = 0;
        std::uint64_t guest_address = 0;
    };
    std::unordered_map<std::uint32_t, MappedRange> mapped_range_buffers_;
    // Host GLsync objects the guest currently holds, as opaque handle bits.
    std::unordered_set<std::uint64_t> sync_objects_;
    std::unordered_set<std::uint32_t> sampler_names_;
    std::vector<std::string> vao_trampoline_names_;
    std::vector<std::string> vao_trampoline_targets_;

public:
    // After the import registry seals, point the cocos2d OESEXT
    // function-pointer slots at the callable trampolines.
    bool ResolveVaoTrampolines(const Aarch64ImportRegistry* registry,
                               std::string* error);
    // Restore the viewport to the surface size when a context rebinding
    // reset it to degenerate dimensions (ANGLE resets on make-current).
    void EnsureDrawableViewport();
    // Screen render target for the EAMT device: the guest stub
    // GetGLViewSysFBO returns -1, which disables the EAMT offscreen
    // pipeline; the probe serves this FBO instead. The color attachment
    // (the screen image texture) is attached lazily when its upload is
    // observed.
    std::uint32_t EnsureScreenFbo();
    std::uint32_t screen_fbo() const { return screen_fbo_; }
    // Bind the screen FBO so a guest-side batch flush renders into the
    // screen image texture (tex15) instead of the window.
    bool BindScreenTarget(std::string* error);
    // Bind framebuffer 0 (the window) — restores the composite target.
    bool BindWindowTarget(std::string* error);
    // Stage every enabled, program-active client-memory vertex attribute
    // into host-side storage and repoint the host attributes at it.
    // `maximum_vertex` is the largest vertex index the draw will read;
    // `initial_staging` reserves staging budget already consumed by the
    // caller (index bytes for glDrawElements). Attribute states recorded as
    // buffer offsets are left untouched — the host attribute already
    // references the correct buffer. Shared by the glDrawElements and
    // glDrawArrays client-array paths.
    bool StageClientVertexAttributes(std::uint32_t maximum_vertex,
                                     std::size_t initial_staging,
                                     std::string* error);
    // Consume a host GL error after a draw call: log it with context, latch
    // it into pending_guest_gl_error_ so the guest's glGetError observes it,
    // and let the call succeed. A host GL error is guest-visible state (the
    // call no-ops on real GLES), not a shim failure — returning false here
    // would kill the guest thread over a recoverable driver condition.
    void LatchDrawGlError(const char* name);
    // Latch a guest-visible GL error into pending_guest_gl_error_ (as
    // glGetError reports it) without failing the dispatch. ES3 handlers
    // use these for validation failures real GLES surfaces through
    // glGetError rather than a process abort.
    void LatchInvalidOperationError();
    void LatchInvalidValueError();
    // ES3 indexed-draw variants (instanced / base-vertex / ranged). Shared
    // client-index staging and per-context program sync. instance_count > 0
    // selects the instanced host call; has_base_vertex selects the
    // base-vertex variant; ranged selects the glDrawRangeElements form.
    bool DrawIndexedEs3(std::uint32_t mode, std::int32_t count,
                        std::uint32_t type, std::uint64_t indices,
                        std::int32_t instance_count,
                        bool has_base_vertex, std::int32_t base_vertex,
                        bool ranged, std::uint32_t range_start,
                        std::uint32_t range_end, const char* name,
                        std::string* error);
    // Construct the EAMTGL20StateManager host-side at first use (the guest
    // constructor never runs on this route) and publish it into the device.
    void EnsureStateManagerConstructed();

private:
    std::unordered_set<std::uint32_t> renderbuffer_names_;
    std::unordered_set<std::uint32_t> framebuffer_names_;
    std::unordered_set<std::uint32_t> shader_names_;
    std::unordered_set<std::uint32_t> delete_pending_shader_names_;
    std::unordered_set<std::uint32_t> program_names_;
    std::unordered_set<std::uint32_t> delete_pending_program_names_;
    std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>>
        shader_program_names_;
    struct VertexAttributePointerState {
        std::int32_t size = 0;
        std::uint32_t type = 0;
        bool normalized = false;
        std::int32_t stride = 0;
        std::uint64_t pointer = 0;
        // True when the pointer was recorded while an array buffer was bound:
        // the value is a byte offset into that buffer, not a guest address.
        bool buffer_offset = false;
        // Set by glVertexAttribIPointer: staged repointing must use the
        // integer-attribute host entry point, not glVertexAttribPointer.
        bool integer_attrib = false;
    };
    std::unordered_map<std::uint32_t, VertexAttributePointerState>
        vertex_attribute_pointers_;
    std::unordered_map<std::uint32_t, std::vector<std::uint8_t>>
        vertex_attribute_staging_;
    // Bound texture name per (active texture unit, bind target), keyed
    // (unit << 32) | target. Covers GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP, and
    // any other target the host accepts.
    std::unordered_map<std::uint64_t, std::uint32_t>
        bound_texture_by_unit_target_;
    // Per-texture level image definition, keyed (texture << 32) | level.
    // Lets glCompressedTexSubImage2D distinguish "level never defined" from
    // a genuine format/alignment error instead of surfacing a bare
    // GL_INVALID_OPERATION from the host.
    struct TextureLevelImage {
        bool compressed = false;
        std::uint32_t format = 0;
        std::int32_t width = 0;
        std::int32_t height = 0;
    };
    std::unordered_map<std::uint64_t, TextureLevelImage> texture_level_images_;
    std::uint32_t active_texture_unit_ = 0;
    std::uint32_t bound_renderbuffer_ = 0;
    std::uint32_t bound_framebuffer_ = 0;
    std::uint32_t bound_program_ = 0;
    std::size_t draw_call_count_ = 0;
    std::size_t draw_readback_count_ = 0;
    std::size_t present_count_ = 0;
    std::size_t lit_present_count_ = 0;
    std::size_t present_readback_count_ = 0;
    std::size_t last_present_draw_count_ = 0;
    std::size_t inactive_enabled_attribute_skip_count_ = 0;
    bool inactive_enabled_attribute_reported_ = false;
    bool draw_capture_written_ = false;
    std::size_t last_sampled_pixel_count_ = 0;
    std::size_t last_lit_pixel_count_ = 0;
    std::size_t last_nonzero_alpha_pixel_count_ = 0;
    std::uint32_t last_max_rgb_ = 0;
    std::int32_t unpack_alignment_ = 4;
    std::uint32_t owner_thread_id_ = 0;
    // Guest EGL surface handoff (Unity render thread). config_ is the host
    // window config chosen at init, kept so guest eglCreateContext /
    // eglCreateWindowSurface / eglCreatePbufferSurface can mint shared host
    // objects on whichever guest thread drives the EGL lifecycle.
    // guest_render_contexts_/guest_render_surfaces_ map a host thread id to
    // the shared context / surface it created so is_current() accepts a
    // bound render-thread context in addition to the owner-thread pair.
    // guest_native_window_ is the guest-side ANativeWindow struct address
    // returned by ANativeWindow_fromSurface and handed back to
    // eglCreateWindowSurface.
    EGLConfig config_ = nullptr;
    std::uint64_t guest_native_window_ = 0;
    mutable std::mutex guest_egl_mutex_;
    std::unordered_map<std::uint32_t, EGLContext> guest_render_contexts_;
    std::unordered_map<std::uint32_t, EGLSurface> guest_render_surfaces_;
    std::uint64_t guest_egl_version_string_ = 0;
    std::uint64_t guest_egl_vendor_string_ = 0;
    std::uint64_t guest_egl_apis_string_ = 0;
    std::uint64_t guest_egl_extensions_string_ = 0;
    bool initialized_ = false;
    bool hardware_accelerated_ = false;
    bool lit_pixel_observed_ = false;
    // When true, Present blits the read-back default framebuffer to the Win32
    // window via GDI in addition to eglSwapBuffers. This is a workaround for
    // ANGLE window surfaces whose D3D11 present does not composite onto the
    // visible HWND (genuine guest fb0 reaches the window regardless).
    bool gdi_present_ = false;
    // Reused present readback scratch so the per-frame GDI present does not
    // allocate a full-frame buffer every present. present_pixels_ holds the
    // raw glReadPixels result; present_bgra_ holds the swizzled copy when the
    // direct GL_BGRA read is unavailable.
    std::vector<std::uint8_t> present_pixels_;
    std::vector<std::uint8_t> present_bgra_;
    // Set false once a GL_BGRA readback reports an error so the present path
    // stops retrying it and falls back to RGBA + host swizzle.
    bool bgra_read_supported_ = true;
    bool shutdown_ = false;
    // True when window_ is a child of an external host shell (the ARMR game
    // window) and surface_window_ carries the fixed-size EGL render target.
    bool hosted_ = false;
};

}  // namespace isomr
