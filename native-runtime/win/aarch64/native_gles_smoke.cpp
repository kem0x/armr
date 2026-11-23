#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "bionic_runtime.h"
#include "guest_heap.h"
#include "guest_address_space.h"
#include "import_registry.h"
#include "native_gles.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

int Fail(const char* stage, const std::string& error = {}) {
    std::fprintf(stderr, "ISOMR_NATIVE_GLES failure=%s error=%s\n", stage,
                 error.c_str());
    return 1;
}

}  // namespace

int main() {
    constexpr std::uint32_t kGlVendor = 0x1f00;
    constexpr std::uint32_t kGlRenderer = 0x1f01;
    constexpr std::uint32_t kGlVersion = 0x1f02;
    constexpr std::uint32_t kGlExtensions = 0x1f03;
    constexpr std::uint32_t kGlShadingLanguageVersion = 0x8b8c;
    constexpr std::uint32_t kGlMaxTextureSize = 0x0d33;
    constexpr std::uint32_t kGlMaxCubeMapTextureSize = 0x851c;
    constexpr std::uint32_t kGlMaxTextureImageUnits = 0x8872;
    constexpr std::uint32_t kGlMaxVertexAttribs = 0x8869;
    constexpr std::uint32_t kGlMaxVaryingVectors = 0x8dfc;
    constexpr std::uint32_t kGlMaxVertexUniformVectors = 0x8dfb;
    constexpr std::uint32_t kGlMaxFragmentUniformVectors = 0x8dfd;
    constexpr std::uint32_t kGlMaxCombinedTextureImageUnits = 0x8b4d;
    constexpr std::uint32_t kGlMaxRenderbufferSize = 0x84e8;
    constexpr std::uint32_t kGlFramebufferBinding = 0x8ca6;
    constexpr std::uint32_t kGlMaxViewportDimensions = 0x0d3a;
    isomr::GuestHeap heap(0x740000000000ULL, 64 * 1024);
    isomr::GuestAddressSpace memory(&heap);
    isomr::Aarch64ImportRegistry imports(0x6a0000000000ULL, 512);
    isomr::NativeGles gles(&heap, &memory);
    std::string error;
    if (!gles.Initialize(&error) ||
        !gles.RegisterImports(&imports, &error) || !imports.Seal(&error) ||
        !gles.initialized() || !gles.hardware_accelerated() ||
        !gles.is_current() || gles.window_visible() ||
        !imports.Resolve("glGetString") ||
        !imports.Resolve("glGetIntegerv") ||
        !imports.Resolve("glGenTextures") ||
        !imports.Resolve("glDeleteTextures") ||
        !imports.Resolve("glActiveTexture") ||
        !imports.Resolve("glBindTexture") ||
        !imports.Resolve("glTexParameteri") ||
        !imports.Resolve("glGenRenderbuffers") ||
        !imports.Resolve("glBindRenderbuffer") ||
        !imports.Resolve("glRenderbufferStorage") ||
        !imports.Resolve("glDeleteRenderbuffers") ||
        !imports.Resolve("glGenFramebuffers") ||
        !imports.Resolve("glBindFramebuffer") ||
        !imports.Resolve("glFramebufferTexture2D") ||
        !imports.Resolve("glFramebufferRenderbuffer") ||
        !imports.Resolve("glCheckFramebufferStatus") ||
        !imports.Resolve("glDeleteFramebuffers") ||
        !imports.Resolve("glViewport") ||
        !imports.Resolve("glDepthRangef") ||
        !imports.Resolve("glClearColor") ||
        !imports.Resolve("glClearDepthf") ||
        !imports.Resolve("glClearStencil") ||
        !imports.Resolve("glStencilMask") ||
        !imports.Resolve("glClear") ||
        !imports.Resolve("glEnable") ||
        !imports.Resolve("glDisable") ||
        !imports.Resolve("glColorMask") ||
        !imports.Resolve("glDepthMask") ||
        !imports.Resolve("glBlendFunc") ||
        !imports.Resolve("glBlendFuncSeparate") ||
        !imports.Resolve("glBlendEquation") ||
        !imports.Resolve("glBlendEquationSeparate") ||
        !imports.Resolve("glDepthFunc") ||
        !imports.Resolve("glCullFace") ||
        !imports.Resolve("glFrontFace") ||
        !imports.Resolve("glScissor") ||
        !imports.Resolve("glEnableVertexAttribArray") ||
        !imports.Resolve("glDisableVertexAttribArray") ||
        !imports.Resolve("glVertexAttribPointer") ||
        !imports.Resolve("glVertexAttrib2fv") ||
        !imports.Resolve("glVertexAttrib3fv") ||
        !imports.Resolve("glVertexAttrib4fv") ||
        !imports.Resolve("glPixelStorei") ||
        !imports.Resolve("glTexImage2D") ||
        !imports.Resolve("glTexSubImage2D") ||
        !imports.Resolve("glCreateShader") ||
        !imports.Resolve("glShaderSource") ||
        !imports.Resolve("glCompileShader") ||
        !imports.Resolve("glGetShaderiv") ||
        !imports.Resolve("glGetShaderInfoLog") ||
        !imports.Resolve("glDeleteShader") ||
        !imports.Resolve("eglGetProcAddress") ||
        !imports.Resolve("glCreateProgram") ||
        !imports.Resolve("glAttachShader") ||
        !imports.Resolve("glBindAttribLocation") ||
        !imports.Resolve("glLinkProgram") ||
        !imports.Resolve("glGetProgramiv") ||
        !imports.Resolve("glGetProgramInfoLog") ||
        !imports.Resolve("glGetActiveUniform") ||
        !imports.Resolve("glGetActiveAttrib") ||
        !imports.Resolve("glGetUniformLocation") ||
        !imports.Resolve("glGetAttribLocation") ||
        !imports.Resolve("glUseProgram") ||
        !imports.Resolve("glUniform1i") ||
        !imports.Resolve("glUniform1f") ||
        !imports.Resolve("glUniform1fv") ||
        !imports.Resolve("glUniform2fv") ||
        !imports.Resolve("glUniform4f") ||
        !imports.Resolve("glUniform4fv") ||
        !imports.Resolve("glUniformMatrix4fv") ||
        !imports.Resolve("glDrawElements") ||
        !imports.Resolve("glDeleteProgram"))
        return Fail("initialize", error);

    constexpr char kKnownProcName[] = "glUniform2fv";
    constexpr char kKnown1ProcName[] = "glUniform1fv";
    constexpr char kUnknownProcName[] = "glArmrMissing";
    const std::uint64_t proc_name_address =
        heap.Allocate(sizeof(kKnownProcName));
    isomr::Aarch64ImportRegistry::Registers proc_registers{};
    proc_registers[0] = proc_name_address;
    if (proc_name_address == 0 ||
        !heap.Write(proc_name_address, kKnownProcName,
                    sizeof(kKnownProcName)) ||
        !imports.Dispatch("eglGetProcAddress", &proc_registers, &error) ||
        proc_registers[0] != *imports.Resolve("glUniform2fv") ||
        !heap.Write(proc_name_address, kKnown1ProcName,
                    sizeof(kKnown1ProcName)))
        return Fail("get-proc-address-known", error);
    proc_registers[0] = proc_name_address;
    if (!imports.Dispatch("eglGetProcAddress", &proc_registers, &error) ||
        proc_registers[0] != *imports.Resolve("glUniform1fv"))
        return Fail("get-proc-address-known-1", error);
    if (!heap.Write(proc_name_address, kUnknownProcName,
                    sizeof(kUnknownProcName)))
        return Fail("get-proc-address-known", error);
    proc_registers[0] = proc_name_address;
    if (!imports.Dispatch("eglGetProcAddress", &proc_registers, &error) ||
        proc_registers[0] != 0)
        return Fail("get-proc-address-unknown", error);
    proc_registers[0] = 1;
    error.clear();
    if (imports.Dispatch("eglGetProcAddress", &proc_registers, &error) ||
        error.find("invalid range") == std::string::npos ||
        !heap.Release(proc_name_address, &error))
        return Fail("get-proc-address-name", error);

    isomr::Aarch64ImportRegistry::Registers framebuffer_registers{};
    framebuffer_registers[0] = GL_FRAMEBUFFER;
    framebuffer_registers[1] = 0;
    GLint framebuffer_binding = -1;
    if (!imports.Dispatch("glBindFramebuffer", &framebuffer_registers,
                          &error))
        return Fail("bind-default-framebuffer", error);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_binding);
    if (glGetError() != GL_NO_ERROR || framebuffer_binding != 0)
        return Fail("bind-default-framebuffer-driver-state");
    framebuffer_registers[0] = 0xffffffffU;
    error.clear();
    if (imports.Dispatch("glBindFramebuffer", &framebuffer_registers,
                         &error) ||
        error.find("target is not implemented") == std::string::npos)
        return Fail("bind-framebuffer-target", error);
    framebuffer_registers[0] = GL_FRAMEBUFFER;
    framebuffer_registers[1] = 1;
    error.clear();
    if (imports.Dispatch("glBindFramebuffer", &framebuffer_registers,
                         &error) ||
        error.find("framebuffer is not owned") == std::string::npos)
        return Fail("bind-framebuffer-ownership", error);

    isomr::Aarch64ImportRegistry::Registers viewport_registers{};
    viewport_registers[0] = static_cast<std::uint32_t>(-1);
    viewport_registers[1] = static_cast<std::uint32_t>(-2);
    viewport_registers[2] = 4;
    viewport_registers[3] = 3;
    if (!imports.Dispatch("glViewport", &viewport_registers, &error))
        return Fail("viewport", error);
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (glGetError() != GL_NO_ERROR || viewport[0] != -1 ||
        viewport[1] != -2 || viewport[2] != 4 || viewport[3] != 3)
        return Fail("viewport-driver-state");
    viewport_registers[2] = static_cast<std::uint32_t>(-1);
    error.clear();
    if (imports.Dispatch("glViewport", &viewport_registers, &error) ||
        error.find("dimensions are invalid") == std::string::npos)
        return Fail("viewport-dimensions", error);

    isomr::Aarch64ImportRegistry::Registers depth_range_registers{};
    const float near_value = -1.0F;
    const float far_value = 2.0F;
    std::memcpy(depth_range_registers.vector[0].data(), &near_value,
                sizeof(near_value));
    std::memcpy(depth_range_registers.vector[1].data(), &far_value,
                sizeof(far_value));
    if (!imports.Dispatch("glDepthRangef", &depth_range_registers, &error))
        return Fail("depth-range", error);
    GLfloat depth_range[2]{};
    glGetFloatv(GL_DEPTH_RANGE, depth_range);
    if (glGetError() != GL_NO_ERROR || depth_range[0] != 0.0F ||
        depth_range[1] != 1.0F)
        return Fail("depth-range-driver-state");

    isomr::Aarch64ImportRegistry::Registers clear_color_registers{};
    constexpr std::array<float, 4> kClearColor{-1.0F, 0.25F, 2.0F, 0.75F};
    for (std::size_t index = 0; index < kClearColor.size(); ++index)
        std::memcpy(clear_color_registers.vector[index].data(),
                    &kClearColor[index], sizeof(kClearColor[index]));
    if (!imports.Dispatch("glClearColor", &clear_color_registers, &error))
        return Fail("clear-color", error);
    std::array<GLfloat, 4> clear_color{};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color.data());
    if (glGetError() != GL_NO_ERROR || clear_color[0] != -1.0F ||
        clear_color[1] != 0.25F || clear_color[2] != 2.0F ||
        clear_color[3] != 0.75F)
        return Fail("clear-color-driver-state");
    isomr::Aarch64ImportRegistry::Registers clear_depth_registers{};
    constexpr float kClearDepth = 0.25F;
    clear_depth_registers[0] = UINT64_MAX;
    std::memcpy(clear_depth_registers.vector[0].data(), &kClearDepth,
                sizeof(kClearDepth));
    if (!imports.Dispatch("glClearDepthf", &clear_depth_registers, &error))
        return Fail("clear-depth", error);
    GLfloat clear_depth = 0.0F;
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clear_depth);
    if (glGetError() != GL_NO_ERROR || clear_depth != kClearDepth ||
        gles.draw_call_count() != 0 || gles.lit_pixel_observed())
        return Fail("clear-depth-driver-state");
    isomr::Aarch64ImportRegistry::Registers clear_stencil_registers{};
    clear_stencil_registers[0] = 0x5a;
    if (!imports.Dispatch("glClearStencil", &clear_stencil_registers,
                          &error))
        return Fail("clear-stencil", error);
    GLint clear_stencil = 0;
    glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &clear_stencil);
    if (glGetError() != GL_NO_ERROR || clear_stencil != 0x5a)
        return Fail("clear-stencil-driver-state");
    isomr::Aarch64ImportRegistry::Registers stencil_mask_registers{};
    stencil_mask_registers[0] = 0xa5;
    if (!imports.Dispatch("glStencilMask", &stencil_mask_registers,
                          &error))
        return Fail("stencil-mask", error);
    GLint stencil_mask = 0;
    glGetIntegerv(GL_STENCIL_WRITEMASK, &stencil_mask);
    if (glGetError() != GL_NO_ERROR || stencil_mask != 0xa5)
        return Fail("stencil-mask-driver-state");
    isomr::Aarch64ImportRegistry::Registers clear_registers{};
    clear_registers[0] = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
    if (!imports.Dispatch("glClear", &clear_registers, &error))
        return Fail("clear", error);
    std::array<std::uint8_t, 4> clear_pixel{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                 clear_pixel.data());
    if (glGetError() != GL_NO_ERROR || clear_pixel[0] != 0 ||
        clear_pixel[1] < 63 || clear_pixel[1] > 64 ||
        clear_pixel[2] != 255 || clear_pixel[3] < 191 ||
        clear_pixel[3] > 192)
        return Fail("clear-driver-state");
    clear_registers[0] = 0x80000000U;
    error.clear();
    if (imports.Dispatch("glClear", &clear_registers, &error) ||
        error.find("mask is invalid") == std::string::npos)
        return Fail("clear-mask", error);

    isomr::Aarch64ImportRegistry::Registers capability_registers{};
    capability_registers[0] = GL_BLEND;
    if (!imports.Dispatch("glEnable", &capability_registers, &error) ||
        glIsEnabled(GL_BLEND) != GL_TRUE || glGetError() != GL_NO_ERROR)
        return Fail("enable-capability", error);
    capability_registers[0] = GL_BLEND;
    if (!imports.Dispatch("glDisable", &capability_registers, &error) ||
        glIsEnabled(GL_BLEND) != GL_FALSE || glGetError() != GL_NO_ERROR)
        return Fail("disable-capability", error);
    capability_registers[0] = 0xffffffffU;
    error.clear();
    if (imports.Dispatch("glDisable", &capability_registers, &error) ||
        error.find("host glDisable failed with GL error") ==
            std::string::npos)
        return Fail("disable-capability-enum", error);

    isomr::Aarch64ImportRegistry::Registers mask_registers{};
    mask_registers[0] = 1;
    mask_registers[1] = 0;
    mask_registers[2] = 0x101;
    mask_registers[3] = 0x100;
    if (!imports.Dispatch("glColorMask", &mask_registers, &error))
        return Fail("color-mask", error);
    GLboolean color_mask[4]{};
    glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
    if (glGetError() != GL_NO_ERROR || color_mask[0] != GL_TRUE ||
        color_mask[1] != GL_FALSE || color_mask[2] != GL_TRUE ||
        color_mask[3] != GL_FALSE)
        return Fail("color-mask-driver-state");
    mask_registers = {};
    if (!imports.Dispatch("glDepthMask", &mask_registers, &error))
        return Fail("depth-mask", error);
    GLboolean depth_mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    if (glGetError() != GL_NO_ERROR || depth_mask != GL_FALSE)
        return Fail("depth-mask-driver-state");
    mask_registers = {};
    mask_registers[0] = 1;
    mask_registers[1] = 1;
    mask_registers[2] = 1;
    mask_registers[3] = 1;
    if (!imports.Dispatch("glColorMask", &mask_registers, &error) ||
        !imports.Dispatch("glDepthMask", &mask_registers, &error))
        return Fail("write-mask-restore", error);

    isomr::Aarch64ImportRegistry::Registers blend_registers{};
    blend_registers[0] = GL_SRC_ALPHA;
    blend_registers[1] = GL_ONE_MINUS_SRC_ALPHA;
    if (!imports.Dispatch("glBlendFunc", &blend_registers, &error))
        return Fail("blend-function", error);
    GLint blend_source_rgb = 0;
    GLint blend_destination_rgb = 0;
    GLint blend_source_alpha = 0;
    GLint blend_destination_alpha = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blend_source_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blend_destination_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_source_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_destination_alpha);
    if (glGetError() != GL_NO_ERROR || blend_source_rgb != GL_SRC_ALPHA ||
        blend_destination_rgb != GL_ONE_MINUS_SRC_ALPHA ||
        blend_source_alpha != GL_SRC_ALPHA ||
        blend_destination_alpha != GL_ONE_MINUS_SRC_ALPHA)
        return Fail("blend-function-driver-state");
    blend_registers = {};
    blend_registers[0] = GL_ONE;
    blend_registers[1] = GL_ZERO;
    blend_registers[2] = GL_SRC_ALPHA;
    blend_registers[3] = GL_ONE_MINUS_SRC_ALPHA;
    if (!imports.Dispatch("glBlendFuncSeparate", &blend_registers, &error))
        return Fail("blend-function-separate", error);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blend_source_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blend_destination_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_source_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_destination_alpha);
    if (glGetError() != GL_NO_ERROR || blend_source_rgb != GL_ONE ||
        blend_destination_rgb != GL_ZERO ||
        blend_source_alpha != GL_SRC_ALPHA ||
        blend_destination_alpha != GL_ONE_MINUS_SRC_ALPHA)
        return Fail("blend-function-separate-driver-state");
    blend_registers = {};
    blend_registers[0] = GL_FUNC_SUBTRACT;
    if (!imports.Dispatch("glBlendEquation", &blend_registers, &error))
        return Fail("blend-equation", error);
    GLint blend_equation_rgb = 0;
    GLint blend_equation_alpha = 0;
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_equation_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_equation_alpha);
    if (glGetError() != GL_NO_ERROR ||
        blend_equation_rgb != GL_FUNC_SUBTRACT ||
        blend_equation_alpha != GL_FUNC_SUBTRACT)
        return Fail("blend-equation-driver-state");
    blend_registers[0] = GL_FUNC_ADD;
    blend_registers[1] = GL_FUNC_REVERSE_SUBTRACT;
    if (!imports.Dispatch("glBlendEquationSeparate", &blend_registers,
                          &error))
        return Fail("blend-equation-separate", error);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_equation_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_equation_alpha);
    if (glGetError() != GL_NO_ERROR || blend_equation_rgb != GL_FUNC_ADD ||
        blend_equation_alpha != GL_FUNC_REVERSE_SUBTRACT)
        return Fail("blend-equation-separate-driver-state");
    blend_registers = {};
    blend_registers[0] = 0xffffffffU;
    blend_registers[1] = GL_ONE;
    error.clear();
    if (imports.Dispatch("glBlendFunc", &blend_registers, &error) ||
        error.find("host glBlendFunc failed with GL error") ==
            std::string::npos)
        return Fail("blend-function-enum", error);
    blend_registers = {};
    blend_registers[0] = GL_ONE;
    blend_registers[1] = GL_ZERO;
    if (!imports.Dispatch("glBlendFunc", &blend_registers, &error))
        return Fail("blend-function-restore", error);
    blend_registers = {};
    blend_registers[0] = GL_FUNC_ADD;
    if (!imports.Dispatch("glBlendEquation", &blend_registers, &error))
        return Fail("blend-equation-restore", error);

    isomr::Aarch64ImportRegistry::Registers raster_registers{};
    raster_registers[0] = GL_GEQUAL;
    if (!imports.Dispatch("glDepthFunc", &raster_registers, &error))
        return Fail("depth-function", error);
    GLint depth_function = 0;
    glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
    if (glGetError() != GL_NO_ERROR || depth_function != GL_GEQUAL)
        return Fail("depth-function-driver-state");
    raster_registers[0] = GL_FRONT;
    if (!imports.Dispatch("glCullFace", &raster_registers, &error))
        return Fail("cull-face", error);
    GLint cull_face = 0;
    glGetIntegerv(GL_CULL_FACE_MODE, &cull_face);
    if (glGetError() != GL_NO_ERROR || cull_face != GL_FRONT)
        return Fail("cull-face-driver-state");
    raster_registers[0] = GL_CW;
    if (!imports.Dispatch("glFrontFace", &raster_registers, &error))
        return Fail("front-face", error);
    GLint front_face = 0;
    glGetIntegerv(GL_FRONT_FACE, &front_face);
    if (glGetError() != GL_NO_ERROR || front_face != GL_CW)
        return Fail("front-face-driver-state");
    raster_registers = {};
    raster_registers[0] = static_cast<std::uint32_t>(-1);
    raster_registers[1] = static_cast<std::uint32_t>(-2);
    raster_registers[2] = 4;
    raster_registers[3] = 3;
    if (!imports.Dispatch("glScissor", &raster_registers, &error))
        return Fail("scissor", error);
    GLint scissor_box[4]{};
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
    if (glGetError() != GL_NO_ERROR || scissor_box[0] != -1 ||
        scissor_box[1] != -2 || scissor_box[2] != 4 ||
        scissor_box[3] != 3)
        return Fail("scissor-driver-state");
    raster_registers[2] = static_cast<std::uint32_t>(-1);
    error.clear();
    if (imports.Dispatch("glScissor", &raster_registers, &error) ||
        error.find("dimensions are invalid") == std::string::npos)
        return Fail("scissor-dimensions", error);
    raster_registers = {};
    raster_registers[0] = 0xffffffffU;
    error.clear();
    if (imports.Dispatch("glDepthFunc", &raster_registers, &error) ||
        error.find("host glDepthFunc failed with GL error") ==
            std::string::npos)
        return Fail("depth-function-enum", error);
    raster_registers[0] = GL_LESS;
    if (!imports.Dispatch("glDepthFunc", &raster_registers, &error))
        return Fail("depth-function-restore", error);
    raster_registers[0] = GL_BACK;
    if (!imports.Dispatch("glCullFace", &raster_registers, &error))
        return Fail("cull-face-restore", error);
    raster_registers[0] = GL_CCW;
    if (!imports.Dispatch("glFrontFace", &raster_registers, &error))
        return Fail("front-face-restore", error);

    isomr::Aarch64ImportRegistry::Registers vertex_attribute_registers{};
    if (!imports.Dispatch("glEnableVertexAttribArray",
                          &vertex_attribute_registers, &error))
        return Fail("enable-vertex-attribute", error);
    GLint vertex_attribute_enabled = GL_FALSE;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                        &vertex_attribute_enabled);
    if (glGetError() != GL_NO_ERROR ||
        vertex_attribute_enabled != GL_TRUE)
        return Fail("enable-vertex-attribute-driver-state");
    if (!imports.Dispatch("glDisableVertexAttribArray",
                          &vertex_attribute_registers, &error))
        return Fail("disable-vertex-attribute", error);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                        &vertex_attribute_enabled);
    if (glGetError() != GL_NO_ERROR ||
        vertex_attribute_enabled != GL_FALSE)
        return Fail("disable-vertex-attribute-driver-state");
    GLint maximum_vertex_attributes = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maximum_vertex_attributes);
    vertex_attribute_registers[0] =
        static_cast<std::uint32_t>(maximum_vertex_attributes);
    error.clear();
    if (maximum_vertex_attributes <= 0 ||
        imports.Dispatch("glEnableVertexAttribArray",
                         &vertex_attribute_registers, &error) ||
        error.find("host glEnableVertexAttribArray failed with GL error") ==
            std::string::npos)
        return Fail("enable-vertex-attribute-index", error);

    const std::uint64_t vertex_data_address = heap.Allocate(4 * sizeof(float));
    isomr::Aarch64ImportRegistry::Registers vertex_pointer_registers{};
    vertex_pointer_registers[0] = 0;
    vertex_pointer_registers[1] = 4;
    vertex_pointer_registers[2] = GL_FLOAT;
    vertex_pointer_registers[3] = 0;
    vertex_pointer_registers[4] = 0;
    vertex_pointer_registers[5] = vertex_data_address;
    if (vertex_data_address == 0 ||
        !imports.Dispatch("glVertexAttribPointer",
                          &vertex_pointer_registers, &error) ||
        gles.vertex_attribute_pointer_count() != 1)
        return Fail("vertex-attribute-pointer", error);
    vertex_pointer_registers[1] = 5;
    error.clear();
    if (imports.Dispatch("glVertexAttribPointer",
                         &vertex_pointer_registers, &error) ||
        error.find("size is out of range") == std::string::npos ||
        gles.vertex_attribute_pointer_count() != 1)
        return Fail("vertex-attribute-pointer-size", error);
    vertex_pointer_registers[1] = 4;
    vertex_pointer_registers[2] = GL_INT;
    error.clear();
    if (imports.Dispatch("glVertexAttribPointer",
                         &vertex_pointer_registers, &error) ||
        error.find("type is not implemented") == std::string::npos)
        return Fail("vertex-attribute-pointer-type", error);
    vertex_pointer_registers[2] = GL_FLOAT;
    vertex_pointer_registers[4] = static_cast<std::uint32_t>(-1);
    error.clear();
    if (imports.Dispatch("glVertexAttribPointer",
                         &vertex_pointer_registers, &error) ||
        error.find("stride is invalid") == std::string::npos)
        return Fail("vertex-attribute-pointer-stride", error);
    vertex_pointer_registers[4] = 0;
    vertex_pointer_registers[5] = 1;
    error.clear();
    if (imports.Dispatch("glVertexAttribPointer",
                         &vertex_pointer_registers, &error) ||
        error.find("guest pointer is invalid") == std::string::npos ||
        !heap.Release(vertex_data_address, &error))
        return Fail("vertex-attribute-pointer-address", error);

    constexpr std::array<float, 4> kConstantAttribute{0.25F, 0.5F, 0.75F,
                                                       1.0F};
    const std::uint64_t constant_attribute_address =
        heap.Allocate(sizeof(kConstantAttribute));
    isomr::Aarch64ImportRegistry::Registers constant_attribute_registers{};
    constant_attribute_registers[0] = 0;
    constant_attribute_registers[1] = constant_attribute_address;
    std::array<GLfloat, 4> actual_constant_attribute{};
    if (constant_attribute_address == 0 ||
        !heap.Write(constant_attribute_address, kConstantAttribute.data(),
                    sizeof(kConstantAttribute)) ||
        !imports.Dispatch("glVertexAttrib4fv",
                          &constant_attribute_registers, &error))
        return Fail("vertex-attribute-constant", error);
    glGetVertexAttribfv(0, GL_CURRENT_VERTEX_ATTRIB,
                        actual_constant_attribute.data());
    if (glGetError() != GL_NO_ERROR ||
        actual_constant_attribute != kConstantAttribute)
        return Fail("vertex-attribute-constant-driver-state");
    constant_attribute_registers[1] = 1;
    error.clear();
    if (imports.Dispatch("glVertexAttrib4fv",
                         &constant_attribute_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos ||
        !heap.Release(constant_attribute_address, &error))
        return Fail("vertex-attribute-constant-address", error);

    constexpr std::array<float, 3> kConstantAttribute3{0.125F, 0.375F,
                                                       0.625F};
    const std::uint64_t constant_attribute3_address =
        heap.Allocate(sizeof(kConstantAttribute3));
    constant_attribute_registers = {};
    constant_attribute_registers[0] = 0;
    constant_attribute_registers[1] = constant_attribute3_address;
    actual_constant_attribute = {};
    if (constant_attribute3_address == 0 ||
        !heap.Write(constant_attribute3_address,
                    kConstantAttribute3.data(), sizeof(kConstantAttribute3)) ||
        !imports.Dispatch("glVertexAttrib3fv",
                          &constant_attribute_registers, &error))
        return Fail("vertex-attribute-constant3", error);
    glGetVertexAttribfv(0, GL_CURRENT_VERTEX_ATTRIB,
                        actual_constant_attribute.data());
    const std::array<GLfloat, 4> expected_constant_attribute3{
        kConstantAttribute3[0], kConstantAttribute3[1],
        kConstantAttribute3[2], 1.0F};
    if (glGetError() != GL_NO_ERROR ||
        actual_constant_attribute != expected_constant_attribute3)
        return Fail("vertex-attribute-constant3-driver-state");
    constant_attribute_registers[0] =
        std::numeric_limits<std::uint32_t>::max();
    error.clear();
    if (imports.Dispatch("glVertexAttrib3fv",
                         &constant_attribute_registers, &error) ||
        error.find("index is out of range") == std::string::npos)
        return Fail("vertex-attribute-constant3-index", error);
    constant_attribute_registers[0] = 0;
    constant_attribute_registers[1] = 0;
    error.clear();
    if (imports.Dispatch("glVertexAttrib3fv",
                         &constant_attribute_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("vertex-attribute-constant3-null", error);
    // A vector read past the arena end must fail closed. (Reads that only
    // cross into adjacent committed extents legitimately succeed — freed
    // guest memory stays mapped, matching device behavior.)
    constant_attribute_registers[1] =
        heap.guest_base() + heap.capacity() + 2 * sizeof(float);
    error.clear();
    if (imports.Dispatch("glVertexAttrib3fv",
                         &constant_attribute_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("vertex-attribute-constant3-range", error);
    constant_attribute_registers[1] = 1;
    error.clear();
    if (imports.Dispatch("glVertexAttrib3fv",
                         &constant_attribute_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos ||
        !heap.Release(constant_attribute3_address, &error))
        return Fail("vertex-attribute-constant3-address", error);

    constexpr std::array<float, 2> kConstantAttribute2{0.0625F, 0.4375F};
    const std::uint64_t constant_attribute2_address =
        heap.Allocate(sizeof(kConstantAttribute2));
    constant_attribute_registers = {};
    constant_attribute_registers[0] = 0;
    constant_attribute_registers[1] = constant_attribute2_address;
    actual_constant_attribute = {};
    if (constant_attribute2_address == 0 ||
        !heap.Write(constant_attribute2_address,
                    kConstantAttribute2.data(), sizeof(kConstantAttribute2)) ||
        !imports.Dispatch("glVertexAttrib2fv",
                          &constant_attribute_registers, &error))
        return Fail("vertex-attribute-constant2", error);
    glGetVertexAttribfv(0, GL_CURRENT_VERTEX_ATTRIB,
                        actual_constant_attribute.data());
    const std::array<GLfloat, 4> expected_constant_attribute2{
        kConstantAttribute2[0], kConstantAttribute2[1], 0.0F, 1.0F};
    if (glGetError() != GL_NO_ERROR ||
        actual_constant_attribute != expected_constant_attribute2)
        return Fail("vertex-attribute-constant2-driver-state");
    constant_attribute_registers[0] =
        std::numeric_limits<std::uint32_t>::max();
    error.clear();
    if (imports.Dispatch("glVertexAttrib2fv",
                         &constant_attribute_registers, &error) ||
        error.find("index is out of range") == std::string::npos)
        return Fail("vertex-attribute-constant2-index", error);
    constant_attribute_registers[0] = 0;
    constant_attribute_registers[1] = 0;
    error.clear();
    if (imports.Dispatch("glVertexAttrib2fv",
                         &constant_attribute_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("vertex-attribute-constant2-null", error);
    // A vector read past the arena end must fail closed. (Reads that only
    // cross into adjacent committed extents legitimately succeed — freed
    // guest memory stays mapped, matching device behavior.)
    constant_attribute_registers[1] =
        heap.guest_base() + heap.capacity() + 3 * sizeof(float);
    error.clear();
    if (imports.Dispatch("glVertexAttrib2fv",
                         &constant_attribute_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("vertex-attribute-constant2-range", error);
    constant_attribute_registers[1] = 1;
    error.clear();
    if (imports.Dispatch("glVertexAttrib2fv",
                         &constant_attribute_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos ||
        !heap.Release(constant_attribute2_address, &error))
        return Fail("vertex-attribute-constant2-address", error);

    isomr::Aarch64ImportRegistry::Registers shader_registers{};
    shader_registers[0] = 0x8b31;
    if (!imports.Dispatch("glCreateShader", &shader_registers, &error) ||
        shader_registers[0] == 0 || gles.shader_count() != 1)
        return Fail("create-shader", error);
    const std::uint32_t shader_name =
        static_cast<std::uint32_t>(shader_registers[0]);
    shader_registers = {};
    shader_registers[0] = 0xffffffffU;
    error.clear();
    if (imports.Dispatch("glCreateShader", &shader_registers, &error) ||
        shader_registers[0] != 0 ||
        error.find("type is invalid") == std::string::npos ||
        gles.shader_count() != 1)
        return Fail("create-shader-type", error);

    constexpr char kShaderPiece0[] = "attribute vec4 a;";
    constexpr char kShaderPiece1[] = "void main(){gl_Position=a;}";
    const std::uint64_t shader_piece0 = heap.Allocate(sizeof(kShaderPiece0));
    const std::uint64_t shader_piece1 = heap.Allocate(sizeof(kShaderPiece1));
    const std::uint64_t shader_string_array =
        heap.Allocate(2 * sizeof(std::uint64_t));
    const std::uint64_t shader_length_array =
        heap.Allocate(2 * sizeof(std::int32_t));
    const std::uint64_t shader_strings[2] = {shader_piece0, shader_piece1};
    const std::int32_t shader_lengths[2] = {
        static_cast<std::int32_t>(sizeof(kShaderPiece0) - 1), -1};
    if (shader_piece0 == 0 || shader_piece1 == 0 ||
        shader_string_array == 0 || shader_length_array == 0 ||
        !heap.Write(shader_piece0, kShaderPiece0, sizeof(kShaderPiece0)) ||
        !heap.Write(shader_piece1, kShaderPiece1, sizeof(kShaderPiece1)) ||
        !heap.Write(shader_string_array, shader_strings,
                    sizeof(shader_strings)) ||
        !heap.Write(shader_length_array, shader_lengths,
                    sizeof(shader_lengths)))
        return Fail("shader-source-staging");
    shader_registers = {};
    shader_registers[0] = shader_name;
    shader_registers[1] = 2;
    shader_registers[2] = shader_string_array;
    shader_registers[3] = shader_length_array;
    if (!imports.Dispatch("glShaderSource", &shader_registers, &error))
        return Fail("shader-source", error);
    shader_registers = {};
    shader_registers[0] = shader_name;
    if (!imports.Dispatch("glCompileShader", &shader_registers, &error))
        return Fail("compile-shader", error);
    shader_registers[0] = 0xffffffffU;
    error.clear();
    if (imports.Dispatch("glCompileShader", &shader_registers, &error) ||
        error.find("shader is not owned") == std::string::npos)
        return Fail("compile-shader-ownership", error);
    const std::uint64_t shader_status_address =
        heap.Allocate(sizeof(std::int32_t));
    shader_registers = {};
    shader_registers[0] = shader_name;
    shader_registers[1] = 0x8b81;
    shader_registers[2] = shader_status_address;
    std::int32_t shader_status = 0;
    if (shader_status_address == 0 ||
        !imports.Dispatch("glGetShaderiv", &shader_registers, &error) ||
        !heap.Read(shader_status_address, &shader_status,
                   sizeof(shader_status)) ||
        shader_status != GL_TRUE)
        return Fail("shader-compile-status", error);
    // Unknown pnames pass through to the host and surface as a recoverable
    // GL_INVALID_ENUM via the guest glGetError, matching real drivers.
    shader_registers[0] = shader_name;
    shader_registers[1] = 0xffffffffU;
    shader_registers[2] = shader_status_address;
    error.clear();
    shader_status = 0;
    isomr::Aarch64ImportRegistry::Registers get_error_registers{};
    std::string get_error_error;
    if (!imports.Dispatch("glGetShaderiv", &shader_registers, &error) ||
        !heap.Read(shader_status_address, &shader_status,
                   sizeof(shader_status)) ||
        shader_status != 0 ||
        !imports.Dispatch("glGetError", &get_error_registers,
                          &get_error_error) ||
        get_error_registers[0] != GL_INVALID_ENUM)
        return Fail("shader-status-enum", error);
    shader_registers[0] = shader_name;
    shader_registers[1] = 0x8b81;
    shader_registers[2] = 1;
    error.clear();
    if (imports.Dispatch("glGetShaderiv", &shader_registers, &error) ||
        error.find("guest output is invalid") == std::string::npos ||
        !heap.Release(shader_status_address, &error))
        return Fail("shader-status-output", error);
    const std::uint64_t shader_log_length_address =
        heap.Allocate(sizeof(std::int32_t));
    constexpr std::int32_t kShaderLogCapacity = 64;
    const std::uint64_t shader_log_address =
        heap.Allocate(kShaderLogCapacity);
    shader_registers = {};
    shader_registers[0] = shader_name;
    shader_registers[1] = kShaderLogCapacity;
    shader_registers[2] = shader_log_length_address;
    shader_registers[3] = shader_log_address;
    std::int32_t shader_log_length = -1;
    char shader_log_terminator = 1;
    if (shader_log_length_address == 0 || shader_log_address == 0 ||
        !imports.Dispatch("glGetShaderInfoLog", &shader_registers, &error) ||
        !heap.Read(shader_log_length_address, &shader_log_length,
                   sizeof(shader_log_length)) ||
        shader_log_length < 0 || shader_log_length >= kShaderLogCapacity ||
        !heap.Read(shader_log_address + shader_log_length,
                   &shader_log_terminator, sizeof(shader_log_terminator)) ||
        shader_log_terminator != '\0')
        return Fail("shader-info-log", error);
    shader_registers[0] = shader_name;
    shader_registers[3] = 1;
    error.clear();
    if (imports.Dispatch("glGetShaderInfoLog", &shader_registers, &error) ||
        error.find("guest log is invalid") == std::string::npos ||
        !heap.Release(shader_log_address, &error) ||
        !heap.Release(shader_log_length_address, &error))
        return Fail("shader-info-log-output", error);
    shader_registers = {};
    shader_registers[0] = shader_name;
    shader_registers[1] = 2;
    shader_registers[2] = shader_string_array;
    shader_registers[3] = shader_length_array;
    shader_registers[0] = 0xffffffffU;
    error.clear();
    if (imports.Dispatch("glShaderSource", &shader_registers, &error) ||
        error.find("shader is not owned") == std::string::npos)
        return Fail("shader-source-ownership", error);
    shader_registers[0] = shader_name;
    shader_registers[2] = 1;
    error.clear();
    if (imports.Dispatch("glShaderSource", &shader_registers, &error) ||
        error.find("string array is unreadable") == std::string::npos)
        return Fail("shader-source-pointers", error);
    if (!heap.Release(shader_length_array, &error) ||
        !heap.Release(shader_string_array, &error) ||
        !heap.Release(shader_piece1, &error) ||
        !heap.Release(shader_piece0, &error))
        return Fail("shader-source-cleanup", error);

    shader_registers = {};
    shader_registers[0] = 0x8b30;
    if (!imports.Dispatch("glCreateShader", &shader_registers, &error) ||
        shader_registers[0] == 0 || gles.shader_count() != 2)
        return Fail("create-fragment-shader", error);
    const std::uint32_t fragment_shader_name =
        static_cast<std::uint32_t>(shader_registers[0]);
    const char* fragment_source =
        "precision mediump float;uniform vec4 color;uniform vec2 offset;"
        "uniform float intensity;"
        "void main(){gl_FragColor=(color+vec4(offset,0.0,0.0))*intensity;}";
    glShaderSource(fragment_shader_name, 1, &fragment_source, nullptr);
    glCompileShader(fragment_shader_name);
    GLint fragment_status = GL_FALSE;
    glGetShaderiv(fragment_shader_name, GL_COMPILE_STATUS,
                  &fragment_status);
    if (glGetError() != GL_NO_ERROR || fragment_status != GL_TRUE)
        return Fail("fragment-shader-driver-setup");

    isomr::Aarch64ImportRegistry::Registers program_registers{};
    if (!imports.Dispatch("glCreateProgram", &program_registers, &error) ||
        program_registers[0] == 0 || gles.program_count() != 1)
        return Fail("create-program", error);
    const std::uint32_t program_name =
        static_cast<std::uint32_t>(program_registers[0]);
    program_registers = {};
    program_registers[0] = program_name;
    program_registers[1] = shader_name;
    if (!imports.Dispatch("glAttachShader", &program_registers, &error))
        return Fail("attach-vertex-shader", error);
    program_registers[0] = program_name;
    program_registers[1] = fragment_shader_name;
    if (!imports.Dispatch("glAttachShader", &program_registers, &error))
        return Fail("attach-fragment-shader", error);
    constexpr char kAttributeName[] = "a";
    const std::uint64_t attribute_name_address =
        heap.Allocate(sizeof(kAttributeName));
    program_registers = {};
    program_registers[0] = program_name;
    program_registers[1] = 0;
    program_registers[2] = attribute_name_address;
    if (attribute_name_address == 0 ||
        !heap.Write(attribute_name_address, kAttributeName,
                    sizeof(kAttributeName)) ||
        !imports.Dispatch("glBindAttribLocation", &program_registers,
                          &error))
        return Fail("bind-attrib-location", error);
    program_registers = {};
    program_registers[0] = program_name;
    if (!imports.Dispatch("glLinkProgram", &program_registers, &error))
        return Fail("link-program", error);
    const std::uint64_t program_status_address =
        heap.Allocate(sizeof(std::int32_t));
    program_registers = {};
    program_registers[0] = program_name;
    program_registers[1] = GL_LINK_STATUS;
    program_registers[2] = program_status_address;
    std::int32_t program_status = GL_FALSE;
    if (program_status_address == 0 ||
        !imports.Dispatch("glGetProgramiv", &program_registers, &error) ||
        !heap.Read(program_status_address, &program_status,
                   sizeof(program_status)) ||
        program_status != GL_TRUE)
        return Fail("program-link-status", error);
    program_registers = {};
    program_registers[0] = program_name;
    program_registers[1] = GL_ACTIVE_UNIFORMS;
    program_registers[2] = program_status_address;
    std::int32_t active_uniform_count = 0;
    if (!imports.Dispatch("glGetProgramiv", &program_registers, &error) ||
        !heap.Read(program_status_address, &active_uniform_count,
                   sizeof(active_uniform_count)) ||
        active_uniform_count != 3)
        return Fail("program-active-uniform-count", error);
    constexpr std::int32_t kVariableNameCapacity = 64;
    const std::uint64_t variable_name_address =
        heap.Allocate(kVariableNameCapacity);
    const std::uint64_t variable_length_address =
        heap.Allocate(sizeof(std::int32_t));
    const std::uint64_t variable_size_address =
        heap.Allocate(sizeof(std::int32_t));
    const std::uint64_t variable_type_address =
        heap.Allocate(sizeof(std::uint32_t));
    std::int32_t variable_length = 0;
    std::int32_t variable_size = 0;
    std::uint32_t variable_type = 0;
    char variable_name[kVariableNameCapacity]{};
    if (variable_name_address == 0 || variable_length_address == 0 ||
        variable_size_address == 0 || variable_type_address == 0)
        return Fail("active-uniform-allocations");
    bool found_color_uniform = false;
    bool found_offset_uniform = false;
    bool found_intensity_uniform = false;
    for (std::uint32_t index = 0; index < 3; ++index) {
        program_registers = {};
        program_registers[0] = program_name;
        program_registers[1] = index;
        program_registers[2] = kVariableNameCapacity;
        program_registers[3] = variable_length_address;
        program_registers[4] = variable_size_address;
        program_registers[5] = variable_type_address;
        program_registers[6] = variable_name_address;
        std::memset(variable_name, 0, sizeof(variable_name));
        if (!imports.Dispatch("glGetActiveUniform", &program_registers,
                              &error) ||
            !heap.Read(variable_length_address, &variable_length,
                       sizeof(variable_length)) ||
            !heap.Read(variable_size_address, &variable_size,
                       sizeof(variable_size)) ||
            !heap.Read(variable_type_address, &variable_type,
                       sizeof(variable_type)) ||
            !heap.Read(variable_name_address, variable_name,
                       sizeof(variable_name)) ||
            variable_size != 1)
            return Fail("active-uniform", error);
        if (variable_length == 5 && variable_type == GL_FLOAT_VEC4 &&
            std::strcmp(variable_name, "color") == 0) {
            found_color_uniform = true;
        } else if (variable_length == 6 &&
                   variable_type == GL_FLOAT_VEC2 &&
                   std::strcmp(variable_name, "offset") == 0) {
            found_offset_uniform = true;
        } else if (variable_length == 9 && variable_type == GL_FLOAT &&
                   std::strcmp(variable_name, "intensity") == 0) {
            found_intensity_uniform = true;
        } else {
            return Fail("active-uniform-identity");
        }
    }
    if (!found_color_uniform || !found_offset_uniform ||
        !found_intensity_uniform)
        return Fail("active-uniform-set");
    constexpr char kUniformName[] = "color";
    const std::uint64_t uniform_name_address =
        heap.Allocate(sizeof(kUniformName));
    program_registers = {};
    program_registers[0] = program_name;
    program_registers[1] = uniform_name_address;
    if (uniform_name_address == 0 ||
        !heap.Write(uniform_name_address, kUniformName,
                    sizeof(kUniformName)) ||
        !imports.Dispatch("glGetUniformLocation", &program_registers,
                          &error) ||
        program_registers[0] ==
            std::numeric_limits<std::uint64_t>::max())
        return Fail("uniform-location", error);
    const std::int32_t uniform_location =
        static_cast<std::int32_t>(program_registers[0]);
    constexpr char kUniform2Name[] = "offset";
    const std::uint64_t uniform2_name_address =
        heap.Allocate(sizeof(kUniform2Name));
    program_registers = {};
    program_registers[0] = program_name;
    program_registers[1] = uniform2_name_address;
    if (uniform2_name_address == 0 ||
        !heap.Write(uniform2_name_address, kUniform2Name,
                    sizeof(kUniform2Name)) ||
        !imports.Dispatch("glGetUniformLocation", &program_registers,
                          &error) ||
        program_registers[0] ==
            std::numeric_limits<std::uint64_t>::max())
        return Fail("uniform-2-location", error);
    const std::int32_t uniform2_location =
        static_cast<std::int32_t>(program_registers[0]);
    constexpr char kUniform1Name[] = "intensity";
    const std::uint64_t uniform1_name_address =
        heap.Allocate(sizeof(kUniform1Name));
    program_registers = {};
    program_registers[0] = program_name;
    program_registers[1] = uniform1_name_address;
    if (uniform1_name_address == 0 ||
        !heap.Write(uniform1_name_address, kUniform1Name,
                    sizeof(kUniform1Name)) ||
        !imports.Dispatch("glGetUniformLocation", &program_registers,
                          &error) ||
        program_registers[0] ==
            std::numeric_limits<std::uint64_t>::max())
        return Fail("uniform-1-location", error);
    const std::int32_t uniform1_location =
        static_cast<std::int32_t>(program_registers[0]);
    if (uniform1_location != 2)
        return Fail("uniform-1-location", error);
    program_registers = {};
    program_registers[0] = program_name;
    program_registers[1] = 0;
    program_registers[2] = kVariableNameCapacity;
    program_registers[3] = variable_length_address;
    program_registers[4] = variable_size_address;
    program_registers[5] = variable_type_address;
    program_registers[6] = variable_name_address;
    if (!imports.Dispatch("glGetActiveAttrib", &program_registers,
                          &error) ||
        !heap.Read(variable_length_address, &variable_length,
                   sizeof(variable_length)) ||
        !heap.Read(variable_size_address, &variable_size,
                   sizeof(variable_size)) ||
        !heap.Read(variable_type_address, &variable_type,
                   sizeof(variable_type)) ||
        !heap.Read(variable_name_address, variable_name, 2) ||
        variable_length != 1 || variable_size != 1 ||
        variable_type != GL_FLOAT_VEC4 || variable_name[0] != 'a' ||
        variable_name[1] != '\0')
        return Fail("active-attrib", error);
    program_registers = {};
    program_registers[0] = program_name;
    program_registers[1] = attribute_name_address;
    if (!imports.Dispatch("glGetAttribLocation", &program_registers,
                          &error) ||
        program_registers[0] != 0)
        return Fail("attrib-location", error);
    constexpr std::int32_t kProgramLogCapacity = 64;
    const std::uint64_t program_log_length_address =
        heap.Allocate(sizeof(std::int32_t));
    const std::uint64_t program_log_address =
        heap.Allocate(kProgramLogCapacity);
    program_registers = {};
    program_registers[0] = program_name;
    program_registers[1] = kProgramLogCapacity;
    program_registers[2] = program_log_length_address;
    program_registers[3] = program_log_address;
    std::int32_t program_log_length = -1;
    char program_log_terminator = 1;
    if (program_log_length_address == 0 || program_log_address == 0 ||
        !imports.Dispatch("glGetProgramInfoLog", &program_registers,
                          &error) ||
        !heap.Read(program_log_length_address, &program_log_length,
                   sizeof(program_log_length)) ||
        program_log_length < 0 ||
        program_log_length >= kProgramLogCapacity ||
        !heap.Read(program_log_address + program_log_length,
                   &program_log_terminator,
                   sizeof(program_log_terminator)) ||
        program_log_terminator != '\0')
        return Fail("program-info-log", error);
    program_registers = {};
    program_registers[0] = program_name;
    if (!imports.Dispatch("glUseProgram", &program_registers, &error))
        return Fail("use-program", error);
    GLint current_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    if (current_program != static_cast<GLint>(program_name))
        return Fail("use-program-driver-state");
    isomr::Aarch64ImportRegistry::Registers uniform_registers{};
    uniform_registers[0] = static_cast<std::uint32_t>(-1);
    uniform_registers[1] = 7;
    if (!imports.Dispatch("glUniform1i", &uniform_registers, &error))
        return Fail("uniform-1i", error);
    const float uniform_scalar = 0.5F;
    uniform_registers = {};
    uniform_registers[0] = static_cast<std::uint32_t>(-1);
    std::memcpy(uniform_registers.vector[0].data(), &uniform_scalar,
                sizeof(uniform_scalar));
    if (!imports.Dispatch("glUniform1f", &uniform_registers, &error))
        return Fail("uniform-1f", error);
    constexpr std::array<float, 1> kUniform1fv{0.375F};
    const std::uint64_t uniform1_values_address =
        heap.Allocate(sizeof(kUniform1fv));
    uniform_registers = {};
    uniform_registers[0] = static_cast<std::uint32_t>(uniform1_location);
    uniform_registers[1] = 1;
    uniform_registers[2] = uniform1_values_address;
    if (uniform1_values_address == 0 ||
        !heap.Write(uniform1_values_address, kUniform1fv.data(),
                    sizeof(kUniform1fv)) ||
        !imports.Dispatch("glUniform1fv", &uniform_registers, &error))
        return Fail("uniform-1fv", error);
    GLfloat actual_uniform1 = 0.0F;
    glGetUniformfv(program_name, uniform1_location, &actual_uniform1);
    if (glGetError() != GL_NO_ERROR || actual_uniform1 != kUniform1fv[0])
        return Fail("uniform-1fv-driver-state", error);
    uniform_registers[0] = static_cast<std::uint32_t>(-1);
    uniform_registers[1] = 1;
    uniform_registers[2] = 1;
    if (!imports.Dispatch("glUniform1fv", &uniform_registers, &error))
        return Fail("uniform-1fv-location-minus-one", error);
    constexpr std::uint32_t kMaximumUniform1Count = 16U * 1024U * 1024U;
    uniform_registers[0] = static_cast<std::uint32_t>(-1);
    uniform_registers[1] = kMaximumUniform1Count;
    uniform_registers[2] = 0;
    if (!imports.Dispatch("glUniform1fv", &uniform_registers, &error))
        return Fail("uniform-1fv-count-boundary", error);
    uniform_registers[1] = kMaximumUniform1Count + 1U;
    error.clear();
    if (imports.Dispatch("glUniform1fv", &uniform_registers, &error) ||
        error.find("count is out of range") == std::string::npos)
        return Fail("uniform-1fv-count-overflow", error);
    uniform_registers[0] = static_cast<std::uint32_t>(uniform1_location);
    uniform_registers[1] = 0;
    uniform_registers[2] = 1;
    if (!imports.Dispatch("glUniform1fv", &uniform_registers, &error))
        return Fail("uniform-1fv-zero-count", error);
    uniform_registers[1] = static_cast<std::uint32_t>(-1);
    uniform_registers[2] = uniform1_values_address;
    error.clear();
    if (imports.Dispatch("glUniform1fv", &uniform_registers, &error) ||
        error.find("count is out of range") == std::string::npos)
        return Fail("uniform-1fv-negative-count", error);
    uniform_registers[1] =
        static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
    error.clear();
    if (imports.Dispatch("glUniform1fv", &uniform_registers, &error) ||
        error.find("count is out of range") == std::string::npos)
        return Fail("uniform-1fv-large-count", error);
    uniform_registers[1] = 1;
    uniform_registers[2] = 0;
    error.clear();
    if (imports.Dispatch("glUniform1fv", &uniform_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("uniform-1fv-null", error);
    uniform_registers[2] = 1;
    error.clear();
    if (imports.Dispatch("glUniform1fv", &uniform_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("uniform-1fv-address", error);
    uniform_registers[2] =
        std::numeric_limits<std::uint64_t>::max() - sizeof(float);
    error.clear();
    if (imports.Dispatch("glUniform1fv", &uniform_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("uniform-1fv-wrapped-address", error);
    // A read past the arena end must fail closed; reads into adjacent
    // committed extents legitimately succeed (freed memory stays mapped).
    uniform_registers[2] = heap.guest_base() + heap.capacity() + 13;
    error.clear();
    if (imports.Dispatch("glUniform1fv", &uniform_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("uniform-1fv-range", error);
    actual_uniform1 = 0.0F;
    glGetUniformfv(program_name, uniform1_location, &actual_uniform1);
    if (glGetError() != GL_NO_ERROR || actual_uniform1 != kUniform1fv[0])
        return Fail("uniform-1fv-noop-preserves-driver-state", error);
    constexpr std::array<float, 2> kUniform2fv{0.05F, -0.05F};
    const std::uint64_t uniform2_values_address =
        heap.Allocate(sizeof(kUniform2fv));
    uniform_registers = {};
    uniform_registers[0] = static_cast<std::uint32_t>(uniform2_location);
    uniform_registers[1] = 1;
    uniform_registers[2] = uniform2_values_address;
    if (uniform2_values_address == 0 ||
        !heap.Write(uniform2_values_address, kUniform2fv.data(),
                    sizeof(kUniform2fv)) ||
        !imports.Dispatch("glUniform2fv", &uniform_registers, &error))
        return Fail("uniform-2fv", error);
    std::array<GLfloat, 2> actual_uniform2{};
    glGetUniformfv(program_name, uniform2_location, actual_uniform2.data());
    if (glGetError() != GL_NO_ERROR || actual_uniform2 != kUniform2fv)
        return Fail("uniform-2fv-driver-state");
    uniform_registers[0] = static_cast<std::uint32_t>(-1);
    uniform_registers[2] = 1;
    if (!imports.Dispatch("glUniform2fv", &uniform_registers, &error))
        return Fail("uniform-2fv-location-minus-one", error);
    constexpr std::uint32_t kMaximumUniform2Count = 8U * 1024U * 1024U;
    uniform_registers[0] = static_cast<std::uint32_t>(-1);
    uniform_registers[1] = kMaximumUniform2Count;
    uniform_registers[2] = 0;
    if (!imports.Dispatch("glUniform2fv", &uniform_registers, &error))
        return Fail("uniform-2fv-count-boundary", error);
    uniform_registers[1] = kMaximumUniform2Count + 1U;
    error.clear();
    if (imports.Dispatch("glUniform2fv", &uniform_registers, &error) ||
        error.find("count is out of range") == std::string::npos)
        return Fail("uniform-2fv-count-overflow", error);
    uniform_registers[0] = static_cast<std::uint32_t>(uniform2_location);
    uniform_registers[1] = 0;
    uniform_registers[2] = 0;
    if (!imports.Dispatch("glUniform2fv", &uniform_registers, &error))
        return Fail("uniform-2fv-zero-count", error);
    uniform_registers[1] = static_cast<std::uint32_t>(-1);
    uniform_registers[2] = uniform2_values_address;
    error.clear();
    if (imports.Dispatch("glUniform2fv", &uniform_registers, &error) ||
        error.find("count is out of range") == std::string::npos)
        return Fail("uniform-2fv-negative-count", error);
    uniform_registers[1] =
        static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
    error.clear();
    if (imports.Dispatch("glUniform2fv", &uniform_registers, &error) ||
        error.find("count is out of range") == std::string::npos)
        return Fail("uniform-2fv-large-count", error);
    uniform_registers[1] = 1;
    uniform_registers[2] = 0;
    error.clear();
    if (imports.Dispatch("glUniform2fv", &uniform_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("uniform-2fv-null", error);
    uniform_registers[2] = 1;
    error.clear();
    if (imports.Dispatch("glUniform2fv", &uniform_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("uniform-2fv-address", error);
    uniform_registers[2] =
        std::numeric_limits<std::uint64_t>::max() - sizeof(float);
    error.clear();
    if (imports.Dispatch("glUniform2fv", &uniform_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("uniform-2fv-wrapped-address", error);
    // A read past the arena end must fail closed; reads into adjacent
    // committed extents legitimately succeed (freed memory stays mapped).
    uniform_registers[2] = heap.guest_base() + heap.capacity() + 12;
    error.clear();
    if (imports.Dispatch("glUniform2fv", &uniform_registers, &error) ||
        error.find("guest values are invalid") == std::string::npos)
        return Fail("uniform-2fv-range", error);
    actual_uniform2 = {};
    glGetUniformfv(program_name, uniform2_location, actual_uniform2.data());
    if (glGetError() != GL_NO_ERROR || actual_uniform2 != kUniform2fv)
        return Fail("uniform-2fv-noop-preserves-driver-state");
    constexpr std::array<float, 4> kUniform4f{0.1F, 0.2F, 0.3F, 0.4F};
    uniform_registers = {};
    uniform_registers[0] = static_cast<std::uint32_t>(uniform_location);
    for (std::size_t index = 0; index < kUniform4f.size(); ++index)
        std::memcpy(uniform_registers.vector[index].data(),
                    &kUniform4f[index], sizeof(kUniform4f[index]));
    if (!imports.Dispatch("glUniform4f", &uniform_registers, &error))
        return Fail("uniform-4f", error);
    std::array<GLfloat, 4> actual_uniform{};
    glGetUniformfv(program_name, uniform_location, actual_uniform.data());
    if (glGetError() != GL_NO_ERROR || actual_uniform != kUniform4f)
        return Fail("uniform-4f-driver-state");
    constexpr std::array<float, 4> kUniform4fv{0.6F, 0.7F, 0.8F, 0.9F};
    const std::uint64_t uniform_values_address =
        heap.Allocate(sizeof(kUniform4fv));
    uniform_registers = {};
    uniform_registers[0] = static_cast<std::uint32_t>(uniform_location);
    uniform_registers[1] = 1;
    uniform_registers[2] = uniform_values_address;
    if (uniform_values_address == 0 ||
        !heap.Write(uniform_values_address, kUniform4fv.data(),
                    sizeof(kUniform4fv)) ||
        !imports.Dispatch("glUniform4fv", &uniform_registers, &error))
        return Fail("uniform-4fv", error);
    glGetUniformfv(program_name, uniform_location, actual_uniform.data());
    if (glGetError() != GL_NO_ERROR || actual_uniform != kUniform4fv)
        return Fail("uniform-4fv-driver-state");
    constexpr std::array<float, 16> kUniformMatrix{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::uint64_t uniform_matrix_address =
        heap.Allocate(sizeof(kUniformMatrix));
    uniform_registers = {};
    uniform_registers[0] = static_cast<std::uint32_t>(-1);
    uniform_registers[1] = 1;
    uniform_registers[2] = 0;
    uniform_registers[3] = uniform_matrix_address;
    if (uniform_matrix_address == 0 ||
        !heap.Write(uniform_matrix_address, kUniformMatrix.data(),
                    sizeof(kUniformMatrix)) ||
        !imports.Dispatch("glUniformMatrix4fv", &uniform_registers, &error))
        return Fail("uniform-matrix-4fv", error);
    uniform_registers[2] = 1;
    error.clear();
    if (imports.Dispatch("glUniformMatrix4fv", &uniform_registers, &error) ||
        error.find("transpose must be false") == std::string::npos ||
        !heap.Release(uniform_matrix_address, &error) ||
        !heap.Release(uniform_values_address, &error) ||
        !heap.Release(uniform1_values_address, &error) ||
        !heap.Release(uniform2_values_address, &error))
        return Fail("uniform-matrix-4fv-transpose", error);

    constexpr std::array<float, 12> kDrawVertices{
        -1.0F, -1.0F, 0.0F, 1.0F,
         3.0F, -1.0F, 0.0F, 1.0F,
        -1.0F,  3.0F, 0.0F, 1.0F};
    constexpr std::array<std::uint16_t, 3> kDrawIndices{0, 1, 2};
    const std::uint64_t draw_vertices_address =
        heap.Allocate(sizeof(kDrawVertices));
    const std::uint64_t draw_indices_address =
        heap.Allocate(sizeof(kDrawIndices));
    constexpr std::array<float, 4> kInactiveVertices{0, 0, 0, 1};
    const std::uint64_t inactive_vertices_address =
        heap.Allocate(sizeof(kInactiveVertices));
    vertex_pointer_registers = {};
    vertex_pointer_registers[0] = 1;
    vertex_pointer_registers[1] = 4;
    vertex_pointer_registers[2] = GL_FLOAT;
    vertex_pointer_registers[4] = sizeof(kInactiveVertices);
    vertex_pointer_registers[5] = inactive_vertices_address;
    vertex_attribute_registers = {};
    vertex_attribute_registers[0] = 1;
    if (inactive_vertices_address == 0 ||
        !heap.Write(inactive_vertices_address, kInactiveVertices.data(),
                    sizeof(kInactiveVertices)) ||
        !imports.Dispatch("glVertexAttribPointer", &vertex_pointer_registers,
                          &error) ||
        !imports.Dispatch("glEnableVertexAttribArray",
                          &vertex_attribute_registers, &error) ||
        !heap.Release(inactive_vertices_address, &error))
        return Fail("draw-elements-inactive-attribute-setup", error);
    vertex_pointer_registers = {};
    vertex_pointer_registers[0] = 0;
    vertex_pointer_registers[1] = 4;
    vertex_pointer_registers[2] = GL_FLOAT;
    vertex_pointer_registers[5] = draw_vertices_address;
    vertex_attribute_registers = {};
    isomr::Aarch64ImportRegistry::Registers draw_registers{};
    draw_registers[0] = GL_TRIANGLES;
    draw_registers[1] = static_cast<std::uint32_t>(kDrawIndices.size());
    draw_registers[2] = GL_UNSIGNED_SHORT;
    draw_registers[3] = draw_indices_address;
    viewport_registers = {};
    viewport_registers[2] = 1;
    viewport_registers[3] = 1;
    error.clear();
    if (draw_vertices_address == 0 || draw_indices_address == 0 ||
        !heap.Write(draw_vertices_address, kDrawVertices.data(),
                    sizeof(kDrawVertices)) ||
        !heap.Write(draw_indices_address, kDrawIndices.data(),
                    sizeof(kDrawIndices)) ||
        !imports.Dispatch("glVertexAttribPointer", &vertex_pointer_registers,
                          &error) ||
        !imports.Dispatch("glEnableVertexAttribArray",
                          &vertex_attribute_registers, &error) ||
        !imports.Dispatch("glViewport", &viewport_registers, &error))
        return Fail("draw-elements-setup", error);
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    if (glGetError() != GL_NO_ERROR)
        return Fail("draw-elements-black-baseline");
    if (!imports.Dispatch("glDrawElements", &draw_registers, &error) ||
        gles.draw_call_count() != 1 || gles.draw_readback_count() != 0 ||
        gles.lit_pixel_observed() ||
        gles.last_sampled_pixel_count() != 0 ||
        gles.last_lit_pixel_count() != 0 ||
        gles.last_nonzero_alpha_pixel_count() != 0 ||
        gles.inactive_enabled_attribute_skip_count() == 0)
        return Fail("draw-elements", error);
    std::array<std::uint8_t, 4> drawn_pixel{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                 drawn_pixel.data());
    if (glGetError() != GL_NO_ERROR || drawn_pixel[0] == 0 ||
        drawn_pixel[1] == 0 || drawn_pixel[2] == 0)
        return Fail("draw-elements-driver-state");
    if (!SetEnvironmentVariableW(L"ISOMR_DRAW_CAPTURE_BMP", L"NUL"))
        return Fail("draw-elements-capture-request");
    error.clear();
    const bool draw_capture_ok =
        imports.Dispatch("glDrawElements", &draw_registers, &error);
    const bool draw_capture_unset =
        SetEnvironmentVariableW(L"ISOMR_DRAW_CAPTURE_BMP", nullptr) != FALSE;
    if (!draw_capture_ok || !draw_capture_unset ||
        gles.draw_call_count() != 2 || gles.draw_readback_count() != 1 ||
        gles.lit_pixel_observed() ||
        gles.last_sampled_pixel_count() != 0)
        return Fail("draw-elements-explicit-capture", error);
    draw_registers[3] = 1;
    error.clear();
    if (imports.Dispatch("glDrawElements", &draw_registers, &error) ||
        error.find("guest indices are unreadable") == std::string::npos ||
        gles.draw_call_count() != 2)
        return Fail("draw-elements-indices", error);
    draw_registers[2] = GL_FLOAT;
    draw_registers[3] = draw_indices_address;
    error.clear();
    if (imports.Dispatch("glDrawElements", &draw_registers, &error) ||
        error.find("index type is not implemented") == std::string::npos ||
        !heap.Release(draw_indices_address, &error) ||
        !heap.Release(draw_vertices_address, &error))
        return Fail("draw-elements-index-type", error);
    draw_registers[1] = 0;
    draw_registers[2] = GL_UNSIGNED_SHORT;
    draw_registers[3] = 1;
    error.clear();
    if (!imports.Dispatch("glDrawElements", &draw_registers, &error) ||
        gles.draw_call_count() != 3)
        return Fail("draw-elements-zero-count", error);
    program_registers[0] = 0xffffffffU;
    error.clear();
    if (imports.Dispatch("glUseProgram", &program_registers, &error) ||
        error.find("program is not owned") == std::string::npos)
        return Fail("use-program-ownership", error);
    const std::uint64_t delete_shader_status_address =
        heap.Allocate(sizeof(std::int32_t));
    shader_registers = {};
    shader_registers[0] = shader_name;
    if (delete_shader_status_address == 0 ||
        !imports.Dispatch("glDeleteShader", &shader_registers, &error) ||
        gles.shader_count() != 2)
        return Fail("delete-attached-vertex-shader", error);
    shader_registers[0] = fragment_shader_name;
    if (!imports.Dispatch("glDeleteShader", &shader_registers, &error) ||
        gles.shader_count() != 2)
        return Fail("delete-attached-fragment-shader", error);
    shader_registers = {};
    shader_registers[0] = shader_name;
    shader_registers[1] = GL_DELETE_STATUS;
    shader_registers[2] = delete_shader_status_address;
    std::int32_t shader_delete_status = GL_FALSE;
    if (!imports.Dispatch("glGetShaderiv", &shader_registers, &error) ||
        !heap.Read(delete_shader_status_address, &shader_delete_status,
                   sizeof(shader_delete_status)) ||
        shader_delete_status != GL_TRUE ||
        glIsShader(shader_name) != GL_TRUE ||
        glIsShader(fragment_shader_name) != GL_TRUE ||
        glGetError() != GL_NO_ERROR)
        return Fail("delete-attached-shader-remains-usable", error);
    shader_registers = {};
    shader_registers[0] = shader_name;
    if (!imports.Dispatch("glDeleteShader", &shader_registers, &error) ||
        gles.shader_count() != 2)
        return Fail("delete-attached-shader-repeat", error);
    program_registers[0] = program_name;
    if (!imports.Dispatch("glDeleteProgram", &program_registers, &error) ||
        gles.program_count() != 1)
        return Fail("delete-current-program", error);
    GLint delete_status = GL_FALSE;
    glGetProgramiv(program_name, GL_DELETE_STATUS, &delete_status);
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    if (glGetError() != GL_NO_ERROR || delete_status != GL_TRUE ||
        current_program != static_cast<GLint>(program_name))
        return Fail("delete-current-program-driver-state");
    uniform_registers = {};
    uniform_registers[0] = static_cast<std::uint32_t>(-1);
    uniform_registers[1] = 7;
    if (!imports.Dispatch("glUniform1i", &uniform_registers, &error))
        return Fail("delete-current-program-remains-usable", error);
    program_registers[0] = 0;
    if (!imports.Dispatch("glUseProgram", &program_registers, &error))
        return Fail("use-program-zero", error);
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    const GLboolean program_exists = glIsProgram(program_name);
    const GLboolean vertex_shader_exists = glIsShader(shader_name);
    const GLboolean fragment_shader_exists =
        glIsShader(fragment_shader_name);
    if (glGetError() != GL_NO_ERROR || current_program != 0 ||
        program_exists != GL_FALSE ||
        vertex_shader_exists != GL_FALSE ||
        fragment_shader_exists != GL_FALSE ||
        gles.program_count() != 0 || gles.shader_count() != 0)
        return Fail("delete-current-program-release");
    shader_registers = {};
    shader_registers[0] = shader_name;
    error.clear();
    if (imports.Dispatch("glDeleteShader", &shader_registers, &error) ||
        error.find("shader is not owned") == std::string::npos)
        return Fail("delete-released-shader-ownership", error);
    uniform_registers = {};
    uniform_registers[0] = static_cast<std::uint32_t>(uniform2_location);
    error.clear();
    if (imports.Dispatch("glUniform2fv", &uniform_registers, &error) ||
        error.find("owned current program and context") == std::string::npos)
        return Fail("uniform-2fv-no-program", error);
    uniform_registers = {};
    uniform_registers[0] = static_cast<std::uint32_t>(uniform1_location);
    uniform_registers[1] = 1;
    error.clear();
    if (imports.Dispatch("glUniform1fv", &uniform_registers, &error) ||
        error.find("owned current program and context") == std::string::npos)
        return Fail("uniform-1fv-no-program", error);
    program_registers[0] = program_name;
    error.clear();
    if (imports.Dispatch("glDeleteProgram", &program_registers, &error) ||
        error.find("program is not owned") == std::string::npos)
        return Fail("delete-program-ownership", error);
    if (!heap.Release(delete_shader_status_address, &error) ||
        !heap.Release(program_log_address, &error) ||
        !heap.Release(program_log_length_address, &error) ||
        !heap.Release(uniform1_name_address, &error) ||
        !heap.Release(uniform2_name_address, &error) ||
        !heap.Release(uniform_name_address, &error) ||
        !heap.Release(variable_type_address, &error) ||
        !heap.Release(variable_size_address, &error) ||
        !heap.Release(variable_length_address, &error) ||
        !heap.Release(variable_name_address, &error) ||
        !heap.Release(program_status_address, &error) ||
        !heap.Release(attribute_name_address, &error))
        return Fail("delete-program", error);

    const GLubyte* current_vendor = glGetString(GL_VENDOR);
    isomr::Aarch64ImportRegistry::Registers registers{};
    registers[0] = kGlVendor;
    if (current_vendor == nullptr ||
        !imports.Dispatch("glGetString", &registers, &error) ||
        registers[0] == 0 || !heap.ContainsAddress(registers[0]) ||
        gles.guest_string_count() != 1)
        return Fail("vendor", error);
    const std::uint64_t vendor_address = registers[0];
    const std::size_t vendor_length =
        std::strlen(reinterpret_cast<const char*>(current_vendor));
    std::string guest_vendor(vendor_length, '\0');
    char guest_terminator = 1;
    if (!heap.Read(vendor_address, guest_vendor.data(), guest_vendor.size()) ||
        !heap.Read(vendor_address + vendor_length, &guest_terminator, 1) ||
        guest_terminator != '\0' ||
        guest_vendor != reinterpret_cast<const char*>(current_vendor) ||
        vendor_address == reinterpret_cast<std::uintptr_t>(current_vendor))
        return Fail("provenance");

    registers[0] = kGlVendor;
    if (!imports.Dispatch("glGetString", &registers, &error) ||
        registers[0] != vendor_address || gles.guest_string_count() != 1)
        return Fail("lifetime", error);

    const GLubyte* current_renderer = glGetString(GL_RENDERER);
    registers[0] = kGlRenderer;
    if (current_renderer == nullptr ||
        !imports.Dispatch("glGetString", &registers, &error) ||
        registers[0] == 0 || registers[0] == vendor_address ||
        !heap.ContainsAddress(registers[0]) ||
        gles.guest_string_count() != 2)
        return Fail("renderer", error);
    const std::uint64_t renderer_address = registers[0];
    const std::size_t renderer_length =
        std::strlen(reinterpret_cast<const char*>(current_renderer));
    std::string guest_renderer(renderer_length, '\0');
    if (!heap.Read(renderer_address, guest_renderer.data(),
                   guest_renderer.size()) ||
        guest_renderer != reinterpret_cast<const char*>(current_renderer) ||
        renderer_address ==
            reinterpret_cast<std::uintptr_t>(current_renderer))
        return Fail("renderer-provenance");

    const GLubyte* current_version = glGetString(GL_VERSION);
    registers[0] = kGlVersion;
    if (current_version == nullptr ||
        !imports.Dispatch("glGetString", &registers, &error) ||
        registers[0] == 0 || registers[0] == vendor_address ||
        registers[0] == renderer_address ||
        !heap.ContainsAddress(registers[0]) ||
        gles.guest_string_count() != 3)
        return Fail("version", error);
    const std::size_t version_length =
        std::strlen(reinterpret_cast<const char*>(current_version));
    std::string guest_version(version_length, '\0');
    if (!heap.Read(registers[0], guest_version.data(), guest_version.size()) ||
        guest_version != reinterpret_cast<const char*>(current_version))
        return Fail("version-provenance");

    const GLubyte* current_shading_version =
        glGetString(static_cast<GLenum>(kGlShadingLanguageVersion));
    registers[0] = kGlShadingLanguageVersion;
    if (current_shading_version == nullptr ||
        !imports.Dispatch("glGetString", &registers, &error) ||
        registers[0] == 0 || !heap.ContainsAddress(registers[0]) ||
        gles.guest_string_count() != 4)
        return Fail("shading-version", error);
    const std::size_t shading_version_length =
        std::strlen(reinterpret_cast<const char*>(current_shading_version));
    std::string guest_shading_version(shading_version_length, '\0');
    if (!heap.Read(registers[0], guest_shading_version.data(),
                   guest_shading_version.size()) ||
        guest_shading_version !=
            reinterpret_cast<const char*>(current_shading_version))
        return Fail("shading-version-provenance");

    const GLubyte* current_extensions = glGetString(GL_EXTENSIONS);
    registers[0] = kGlExtensions;
    error.clear();
    if (current_extensions == nullptr ||
        !imports.Dispatch("glGetString", &registers, &error) ||
        registers[0] == 0 || !heap.ContainsAddress(registers[0]) ||
        gles.guest_string_count() != 5)
        return Fail("extensions", error);
    const std::size_t extensions_length =
        std::strlen(reinterpret_cast<const char*>(current_extensions));
    std::string guest_extensions(extensions_length, '\0');
    if (!heap.Read(registers[0], guest_extensions.data(),
                   guest_extensions.size()) ||
        guest_extensions != reinterpret_cast<const char*>(current_extensions))
        return Fail("extensions-provenance");

    while (glGetError() != GL_NO_ERROR) {
    }
    registers[0] = 0xffffffffU;
    error.clear();
    if (!imports.Dispatch("glGetString", &registers, &error) ||
        registers[0] != 0 || glGetError() != GL_INVALID_ENUM ||
        gles.guest_string_count() != 5)
        return Fail("invalid", error);

    const std::uint64_t integer_address = heap.Allocate(sizeof(std::int32_t));
    GLint host_max_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &host_max_texture_size);
    registers = {};
    registers[0] = kGlMaxTextureSize;
    registers[1] = integer_address;
    std::int32_t guest_max_texture_size = 0;
    if (integer_address == 0 || host_max_texture_size <= 0 ||
        !imports.Dispatch("glGetIntegerv", &registers, &error) ||
        registers[0] != 0 ||
        !heap.Read(integer_address, &guest_max_texture_size,
                   sizeof(guest_max_texture_size)) ||
        guest_max_texture_size != host_max_texture_size)
        return Fail("max-texture-size", error);
    GLint host_max_cube_map_texture_size = 0;
    glGetIntegerv(static_cast<GLenum>(kGlMaxCubeMapTextureSize),
                  &host_max_cube_map_texture_size);
    registers = {};
    registers[0] = kGlMaxCubeMapTextureSize;
    registers[1] = integer_address;
    std::int32_t guest_max_cube_map_texture_size = 0;
    if (host_max_cube_map_texture_size <= 0 ||
        !imports.Dispatch("glGetIntegerv", &registers, &error) ||
        !heap.Read(integer_address, &guest_max_cube_map_texture_size,
                   sizeof(guest_max_cube_map_texture_size)) ||
        guest_max_cube_map_texture_size != host_max_cube_map_texture_size)
        return Fail("max-cube-map-texture-size", error);
    GLint host_max_texture_image_units = 0;
    glGetIntegerv(static_cast<GLenum>(kGlMaxTextureImageUnits),
                  &host_max_texture_image_units);
    registers = {};
    registers[0] = kGlMaxTextureImageUnits;
    registers[1] = integer_address;
    std::int32_t guest_max_texture_image_units = 0;
    if (host_max_texture_image_units <= 0 ||
        !imports.Dispatch("glGetIntegerv", &registers, &error) ||
        !heap.Read(integer_address, &guest_max_texture_image_units,
                   sizeof(guest_max_texture_image_units)) ||
        guest_max_texture_image_units != host_max_texture_image_units)
        return Fail("max-texture-image-units", error);
    GLint host_max_vertex_attribs = 0;
    glGetIntegerv(static_cast<GLenum>(kGlMaxVertexAttribs),
                  &host_max_vertex_attribs);
    registers = {};
    registers[0] = kGlMaxVertexAttribs;
    registers[1] = integer_address;
    std::int32_t guest_max_vertex_attribs = 0;
    if (host_max_vertex_attribs <= 0 ||
        !imports.Dispatch("glGetIntegerv", &registers, &error) ||
        !heap.Read(integer_address, &guest_max_vertex_attribs,
                   sizeof(guest_max_vertex_attribs)) ||
        guest_max_vertex_attribs != host_max_vertex_attribs)
        return Fail("max-vertex-attribs", error);
    GLint host_max_varying_vectors = 0;
    glGetIntegerv(static_cast<GLenum>(kGlMaxVaryingVectors),
                  &host_max_varying_vectors);
    registers = {};
    registers[0] = kGlMaxVaryingVectors;
    registers[1] = integer_address;
    std::int32_t guest_max_varying_vectors = 0;
    if (host_max_varying_vectors <= 0 ||
        !imports.Dispatch("glGetIntegerv", &registers, &error) ||
        !heap.Read(integer_address, &guest_max_varying_vectors,
                   sizeof(guest_max_varying_vectors)) ||
        guest_max_varying_vectors != host_max_varying_vectors)
        return Fail("max-varying-vectors", error);
    GLint host_max_vertex_uniform_vectors = 0;
    glGetIntegerv(static_cast<GLenum>(kGlMaxVertexUniformVectors),
                  &host_max_vertex_uniform_vectors);
    registers = {};
    registers[0] = kGlMaxVertexUniformVectors;
    registers[1] = integer_address;
    std::int32_t guest_max_vertex_uniform_vectors = 0;
    if (host_max_vertex_uniform_vectors <= 0 ||
        !imports.Dispatch("glGetIntegerv", &registers, &error) ||
        !heap.Read(integer_address, &guest_max_vertex_uniform_vectors,
                   sizeof(guest_max_vertex_uniform_vectors)) ||
        guest_max_vertex_uniform_vectors != host_max_vertex_uniform_vectors)
        return Fail("max-vertex-uniform-vectors", error);
    GLint host_max_fragment_uniform_vectors = 0;
    glGetIntegerv(static_cast<GLenum>(kGlMaxFragmentUniformVectors),
                  &host_max_fragment_uniform_vectors);
    registers = {};
    registers[0] = kGlMaxFragmentUniformVectors;
    registers[1] = integer_address;
    std::int32_t guest_max_fragment_uniform_vectors = 0;
    if (host_max_fragment_uniform_vectors <= 0 ||
        !imports.Dispatch("glGetIntegerv", &registers, &error) ||
        !heap.Read(integer_address, &guest_max_fragment_uniform_vectors,
                   sizeof(guest_max_fragment_uniform_vectors)) ||
        guest_max_fragment_uniform_vectors !=
            host_max_fragment_uniform_vectors)
        return Fail("max-fragment-uniform-vectors", error);
    GLint host_max_combined_texture_image_units = 0;
    glGetIntegerv(static_cast<GLenum>(kGlMaxCombinedTextureImageUnits),
                  &host_max_combined_texture_image_units);
    registers = {};
    registers[0] = kGlMaxCombinedTextureImageUnits;
    registers[1] = integer_address;
    std::int32_t guest_max_combined_texture_image_units = 0;
    if (host_max_combined_texture_image_units <= 0 ||
        !imports.Dispatch("glGetIntegerv", &registers, &error) ||
        !heap.Read(integer_address,
                   &guest_max_combined_texture_image_units,
                   sizeof(guest_max_combined_texture_image_units)) ||
        guest_max_combined_texture_image_units !=
            host_max_combined_texture_image_units)
        return Fail("max-combined-texture-image-units", error);
    GLint host_max_renderbuffer_size = 0;
    glGetIntegerv(static_cast<GLenum>(kGlMaxRenderbufferSize),
                  &host_max_renderbuffer_size);
    registers = {};
    registers[0] = kGlMaxRenderbufferSize;
    registers[1] = integer_address;
    std::int32_t guest_max_renderbuffer_size = 0;
    if (host_max_renderbuffer_size <= 0 ||
        !imports.Dispatch("glGetIntegerv", &registers, &error) ||
        !heap.Read(integer_address, &guest_max_renderbuffer_size,
                   sizeof(guest_max_renderbuffer_size)) ||
        guest_max_renderbuffer_size != host_max_renderbuffer_size)
        return Fail("max-renderbuffer-size", error);
    GLint host_framebuffer_binding = -1;
    glGetIntegerv(static_cast<GLenum>(kGlFramebufferBinding),
                  &host_framebuffer_binding);
    registers = {};
    registers[0] = kGlFramebufferBinding;
    registers[1] = integer_address;
    std::int32_t guest_framebuffer_binding = -1;
    if (host_framebuffer_binding < 0 ||
        !imports.Dispatch("glGetIntegerv", &registers, &error) ||
        !heap.Read(integer_address, &guest_framebuffer_binding,
                   sizeof(guest_framebuffer_binding)) ||
        guest_framebuffer_binding != host_framebuffer_binding)
        return Fail("framebuffer-binding", error);
    // GL_MAX_VIEWPORT_DIMS is a two-value pass-through query: the guest sees
    // the host ANGLE values verbatim.
    GLint host_viewport_dims[2] = {0, 0};
    glGetIntegerv(static_cast<GLenum>(kGlMaxViewportDimensions),
                  host_viewport_dims);
    registers = {};
    registers[0] = kGlMaxViewportDimensions;
    registers[1] = integer_address;
    error.clear();
    std::int32_t guest_viewport_dims[2] = {0, 0};
    if (host_viewport_dims[0] <= 0 ||
        !imports.Dispatch("glGetIntegerv", &registers, &error) ||
        !heap.Read(integer_address, guest_viewport_dims,
                   sizeof(guest_viewport_dims)) ||
        guest_viewport_dims[0] != host_viewport_dims[0] ||
        guest_viewport_dims[1] != host_viewport_dims[1])
        return Fail("integer-unsupported", error);
    registers = {};
    registers[0] = kGlMaxTextureSize;
    registers[1] = 1;
    error.clear();
    if (imports.Dispatch("glGetIntegerv", &registers, &error) ||
        error.find("guest output is invalid") == std::string::npos)
        return Fail("integer-address", error);
    if (!heap.Release(integer_address, &error))
        return Fail("integer-cleanup", error);

    registers = {};
    registers[0] = GL_TEXTURE1;
    if (!imports.Dispatch("glActiveTexture", &registers, &error))
        return Fail("active-texture", error);
    GLint active_texture = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    if (glGetError() != GL_NO_ERROR || active_texture != GL_TEXTURE1)
        return Fail("active-texture-driver-state");
    registers[0] = 0xffffffffU;
    error.clear();
    if (imports.Dispatch("glActiveTexture", &registers, &error) ||
        error.find("unit is out of range") == std::string::npos)
        return Fail("active-texture-unit", error);
    registers[0] = GL_TEXTURE0;
    if (!imports.Dispatch("glActiveTexture", &registers, &error))
        return Fail("active-texture-restore", error);

    const std::uint64_t texture_address =
        heap.Allocate(4 * sizeof(std::uint32_t));
    registers = {};
    registers[0] = 2;
    registers[1] = texture_address;
    std::uint32_t texture_names[2]{};
    if (texture_address == 0 ||
        !imports.Dispatch("glGenTextures", &registers, &error) ||
        registers[0] != 0 ||
        !heap.Read(texture_address, texture_names, sizeof(texture_names)) ||
        texture_names[0] == 0 || texture_names[1] == 0 ||
        texture_names[0] == texture_names[1] || gles.texture_count() != 2)
        return Fail("generate-textures", error);
    registers = {};
    registers[0] = 1;
    registers[1] = 1;
    error.clear();
    if (imports.Dispatch("glGenTextures", &registers, &error) ||
        error.find("guest output is invalid") == std::string::npos ||
        gles.texture_count() != 2)
        return Fail("generate-textures-address", error);
    registers = {};
    registers[0] = 4097;
    registers[1] = texture_address;
    error.clear();
    if (imports.Dispatch("glGenTextures", &registers, &error) ||
        error.find("count is out of range") == std::string::npos ||
        gles.texture_count() != 2)
        return Fail("generate-textures-count", error);
    registers = {};
    if (!imports.Dispatch("glGenTextures", &registers, &error) ||
        gles.texture_count() != 2)
        return Fail("generate-textures-zero", error);
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = texture_names[0];
    GLint bound_texture = 0;
    if (!imports.Dispatch("glBindTexture", &registers, &error))
        return Fail("bind-texture", error);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_texture);
    if (bound_texture != static_cast<GLint>(texture_names[0]))
        return Fail("bind-texture-state");
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = GL_TEXTURE_MIN_FILTER;
    registers[2] = GL_NEAREST;
    if (!imports.Dispatch("glTexParameteri", &registers, &error))
        return Fail("texture-parameter", error);
    GLint texture_min_filter = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        &texture_min_filter);
    if (glGetError() != GL_NO_ERROR || texture_min_filter != GL_NEAREST)
        return Fail("texture-parameter-driver-state");
    // Unsupported targets/GLES1-only parameters are translation no-ops:
    // dispatch succeeds and the host INVALID_ENUM is swallowed.
    registers[0] = 0xffffffffU;
    error.clear();
    if (!imports.Dispatch("glTexParameteri", &registers, &error))
        return Fail("texture-parameter-target", error);
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = 0xffffffffU;
    error.clear();
    if (imports.Dispatch("glBindTexture", &registers, &error) ||
        error.find("texture is not owned") == std::string::npos)
        return Fail("bind-texture-ownership", error);
    // GL_TEXTURE_CUBE_MAP is a supported ES2 binding target; cube-face
    // upload targets share the same binding point in the runtime.
    registers = {};
    registers[0] = 0x8513;
    registers[1] = texture_names[0];
    error.clear();
    if (!imports.Dispatch("glBindTexture", &registers, &error))
        return Fail("bind-texture-target", error);
    // A genuinely invalid target raises a recoverable guest GL error
    // instead of aborting the dispatch.
    registers = {};
    registers[0] = 0xdead;
    registers[1] = texture_names[0];
    error.clear();
    isomr::Aarch64ImportRegistry::Registers bind_error_registers{};
    std::string bind_error_error;
    if (!imports.Dispatch("glBindTexture", &registers, &error) ||
        !imports.Dispatch("glGetError", &bind_error_registers,
                          &bind_error_error) ||
        bind_error_registers[0] == 0)
        return Fail("bind-texture-target-invalid", error);
    registers = {};
    registers[0] = GL_PACK_ALIGNMENT;
    registers[1] = 1;
    GLint pack_alignment = 0;
    if (!imports.Dispatch("glPixelStorei", &registers, &error))
        return Fail("pixel-store", error);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
    if (pack_alignment != 1) return Fail("pixel-store-state");
    registers = {};
    registers[0] = GL_PACK_ALIGNMENT;
    registers[1] = 3;
    error.clear();
    if (imports.Dispatch("glPixelStorei", &registers, &error) ||
        error.find("alignment is invalid") == std::string::npos)
        return Fail("pixel-store-value", error);
    registers = {};
    registers[0] = GL_UNPACK_ALIGNMENT;
    registers[1] = 1;
    GLint unpack_alignment = 0;
    if (!imports.Dispatch("glPixelStorei", &registers, &error))
        return Fail("pixel-store-unpack", error);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    if (unpack_alignment != 1) return Fail("pixel-store-unpack-state");

    const std::uint64_t pixel_address = heap.Allocate(4);
    const std::uint64_t stack_argument_address =
        heap.Allocate(sizeof(std::uint64_t));
    const std::uint8_t expected_pixel[4]{17, 34, 51, 68};
    if (pixel_address == 0 || stack_argument_address == 0 ||
        !heap.Write(pixel_address, expected_pixel, sizeof(expected_pixel)) ||
        !heap.Write(stack_argument_address, &pixel_address,
                    sizeof(pixel_address)))
        return Fail("tex-image-staging");
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = 0;
    registers[2] = GL_RGBA;
    registers[3] = 1;
    registers[4] = 1;
    registers[5] = 0;
    registers[6] = GL_RGBA;
    registers[7] = GL_UNSIGNED_BYTE;
    registers.stack_pointer = stack_argument_address;
    if (!imports.Dispatch("glTexImage2D", &registers, &error))
        return Fail("tex-image", error);
    GLuint framebuffer = 0;
    std::uint8_t actual_pixel[4]{};
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture_names[0], 0);
    const GLenum framebuffer_status =
        glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual_pixel);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    if (framebuffer == 0 || framebuffer_status != GL_FRAMEBUFFER_COMPLETE ||
        glGetError() != GL_NO_ERROR ||
        std::memcmp(actual_pixel, expected_pixel, sizeof(actual_pixel)) != 0)
        return Fail("tex-image-driver-state");
    const std::uint8_t expected_sub_pixel[4]{85, 102, 119, 136};
    if (!heap.Write(pixel_address, expected_sub_pixel,
                    sizeof(expected_sub_pixel)) ||
        !heap.Write(stack_argument_address, &pixel_address,
                    sizeof(pixel_address)))
        return Fail("tex-sub-image-staging");
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = 0;
    registers[2] = 0;
    registers[3] = 0;
    registers[4] = 1;
    registers[5] = 1;
    registers[6] = GL_RGBA;
    registers[7] = GL_UNSIGNED_BYTE;
    registers.stack_pointer = stack_argument_address;
    if (!imports.Dispatch("glTexSubImage2D", &registers, &error))
        return Fail("tex-sub-image", error);
    actual_pixel[0] = 0;
    actual_pixel[1] = 0;
    actual_pixel[2] = 0;
    actual_pixel[3] = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture_names[0], 0);
    const GLenum sub_framebuffer_status =
        glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual_pixel);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    if (framebuffer == 0 ||
        sub_framebuffer_status != GL_FRAMEBUFFER_COMPLETE ||
        glGetError() != GL_NO_ERROR ||
        std::memcmp(actual_pixel, expected_sub_pixel,
                    sizeof(actual_pixel)) != 0)
        return Fail("tex-sub-image-driver-state");
    registers[0] = GL_TEXTURE_2D;
    registers.stack_pointer = 1;
    error.clear();
    if (imports.Dispatch("glTexSubImage2D", &registers, &error) ||
        error.find("stack argument is unreadable") == std::string::npos)
        return Fail("tex-sub-image-stack", error);
    const std::uint64_t invalid_sub_pixels = 1;
    if (!heap.Write(stack_argument_address, &invalid_sub_pixels,
                    sizeof(invalid_sub_pixels)))
        return Fail("tex-sub-image-invalid-staging");
    registers[0] = GL_TEXTURE_2D;
    registers.stack_pointer = stack_argument_address;
    error.clear();
    if (imports.Dispatch("glTexSubImage2D", &registers, &error) ||
        error.find("guest pixels are unreadable") == std::string::npos ||
        !heap.Write(stack_argument_address, &pixel_address,
                    sizeof(pixel_address)))
        return Fail("tex-sub-image-pixels", error);

    const std::uint8_t expected_rgb[3]{5, 10, 15};
    if (!heap.Write(pixel_address, expected_rgb, sizeof(expected_rgb)))
        return Fail("tex-image-rgb-staging");
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = 0;
    registers[2] = GL_RGB;
    registers[3] = 1;
    registers[4] = 1;
    registers[5] = 0;
    registers[6] = GL_RGB;
    registers[7] = GL_UNSIGNED_BYTE;
    registers.stack_pointer = stack_argument_address;
    if (!imports.Dispatch("glTexImage2D", &registers, &error))
        return Fail("tex-image-rgb", error);
    const std::uint8_t expected_alpha = 23;
    if (!heap.Write(pixel_address, &expected_alpha, sizeof(expected_alpha)))
        return Fail("tex-image-alpha-staging");
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = 0;
    registers[2] = GL_ALPHA;
    registers[3] = 1;
    registers[4] = 1;
    registers[5] = 0;
    registers[6] = GL_ALPHA;
    registers[7] = GL_UNSIGNED_BYTE;
    registers.stack_pointer = stack_argument_address;
    if (!imports.Dispatch("glTexImage2D", &registers, &error))
        return Fail("tex-image-alpha", error);
    const std::uint64_t invalid_zero_dimension_pixels = 1;
    if (!heap.Write(stack_argument_address, &invalid_zero_dimension_pixels,
                    sizeof(invalid_zero_dimension_pixels)))
        return Fail("tex-image-zero-dimension-staging");
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = 0;
    registers[2] = GL_RGBA;
    registers[3] = 0;
    registers[4] = 1;
    registers[5] = 0;
    registers[6] = GL_RGBA;
    registers[7] = GL_UNSIGNED_BYTE;
    registers.stack_pointer = stack_argument_address;
    if (!imports.Dispatch("glTexImage2D", &registers, &error))
        return Fail("tex-image-zero-width-no-guest-read", error);
    registers[0] = GL_TEXTURE_2D;
    registers[3] = 1;
    registers[4] = 0;
    if (!imports.Dispatch("glTexImage2D", &registers, &error))
        return Fail("tex-image-zero-height-no-guest-read", error);
    const std::uint64_t null_pixels = 0;
    if (!heap.Write(stack_argument_address, &null_pixels,
                    sizeof(null_pixels)))
        return Fail("tex-image-null-staging");
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = 0;
    registers[2] = GL_RGBA;
    registers[3] = 2;
    registers[4] = 2;
    registers[5] = 0;
    registers[6] = GL_RGBA;
    registers[7] = GL_UNSIGNED_BYTE;
    registers.stack_pointer = stack_argument_address;
    if (!imports.Dispatch("glTexImage2D", &registers, &error))
        return Fail("tex-image-null-allocation", error);

    const std::uint64_t renderbuffer_address =
        heap.Allocate(3 * sizeof(std::uint32_t));
    const std::uint64_t framebuffer_address =
        heap.Allocate(sizeof(std::uint32_t));
    std::uint32_t renderbuffer_names[3]{};
    std::uint32_t owned_framebuffer = 0;
    registers = {};
    registers[0] = 3;
    registers[1] = renderbuffer_address;
    if (renderbuffer_address == 0 || framebuffer_address == 0 ||
        !imports.Dispatch("glGenRenderbuffers", &registers, &error) ||
        !heap.Read(renderbuffer_address, renderbuffer_names,
                   sizeof(renderbuffer_names)) ||
        renderbuffer_names[0] == 0 || renderbuffer_names[1] == 0 ||
        renderbuffer_names[2] == 0 ||
        renderbuffer_names[0] == renderbuffer_names[1] ||
        renderbuffer_names[0] == renderbuffer_names[2] ||
        renderbuffer_names[1] == renderbuffer_names[2] ||
        gles.renderbuffer_count() != 3)
        return Fail("generate-renderbuffers", error);
    registers = {};
    registers[0] = GL_RENDERBUFFER;
    registers[1] = renderbuffer_names[0];
    if (!imports.Dispatch("glBindRenderbuffer", &registers, &error))
        return Fail("bind-renderbuffer-depth", error);
    registers = {};
    registers[0] = GL_RENDERBUFFER;
    registers[1] = GL_DEPTH_COMPONENT16;
    registers[2] = 2;
    registers[3] = 2;
    if (!imports.Dispatch("glRenderbufferStorage", &registers, &error))
        return Fail("renderbuffer-storage-depth", error);
    registers = {};
    registers[0] = GL_RENDERBUFFER;
    registers[1] = renderbuffer_names[1];
    if (!imports.Dispatch("glBindRenderbuffer", &registers, &error))
        return Fail("bind-renderbuffer-stencil", error);
    registers = {};
    registers[0] = GL_RENDERBUFFER;
    registers[1] = GL_STENCIL_INDEX8;
    registers[2] = 2;
    registers[3] = 2;
    if (!imports.Dispatch("glRenderbufferStorage", &registers, &error))
        return Fail("renderbuffer-storage-stencil", error);
    registers = {};
    registers[0] = GL_RENDERBUFFER;
    registers[1] = renderbuffer_names[2];
    if (!imports.Dispatch("glBindRenderbuffer", &registers, &error))
        return Fail("bind-renderbuffer-packed", error);
    registers = {};
    registers[0] = GL_RENDERBUFFER;
    registers[1] = 0x88f0;
    registers[2] = 2;
    registers[3] = 2;
    if (!imports.Dispatch("glRenderbufferStorage", &registers, &error))
        return Fail("renderbuffer-storage-packed", error);
    registers = {};
    registers[0] = GL_RENDERBUFFER;
    registers[1] = renderbuffer_names[1];
    if (!imports.Dispatch("glBindRenderbuffer", &registers, &error))
        return Fail("bind-renderbuffer-stencil-restore", error);
    GLint renderbuffer_binding = 0;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer_binding);
    if (glGetError() != GL_NO_ERROR ||
        renderbuffer_binding !=
            static_cast<GLint>(renderbuffer_names[1]))
        return Fail("renderbuffer-driver-state");

    const std::uint32_t unknown_renderbuffer = 0xffffffffU;
    if (!heap.Write(renderbuffer_address, &unknown_renderbuffer,
                    sizeof(unknown_renderbuffer)))
        return Fail("delete-unknown-renderbuffer-staging");
    registers = {};
    registers[0] = 1;
    registers[1] = renderbuffer_address;
    if (!imports.Dispatch("glDeleteRenderbuffers", &registers, &error) ||
        gles.renderbuffer_count() != 3)
        return Fail("delete-unknown-renderbuffer", error);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer_binding);
    if (glGetError() != GL_NO_ERROR ||
        renderbuffer_binding !=
            static_cast<GLint>(renderbuffer_names[1]))
        return Fail("delete-unknown-renderbuffer-preserves-binding");

    registers = {};
    registers[0] = 1;
    registers[1] = framebuffer_address;
    if (!imports.Dispatch("glGenFramebuffers", &registers, &error) ||
        !heap.Read(framebuffer_address, &owned_framebuffer,
                   sizeof(owned_framebuffer)) ||
        owned_framebuffer == 0 || gles.framebuffer_count() != 1)
        return Fail("generate-framebuffer", error);
    registers = {};
    registers[0] = GL_FRAMEBUFFER;
    registers[1] = owned_framebuffer;
    if (!imports.Dispatch("glBindFramebuffer", &registers, &error))
        return Fail("bind-owned-framebuffer", error);
    registers = {};
    registers[0] = GL_FRAMEBUFFER;
    registers[1] = GL_COLOR_ATTACHMENT0;
    registers[2] = GL_TEXTURE_2D;
    registers[3] = texture_names[0];
    registers[4] = 0;
    if (!imports.Dispatch("glFramebufferTexture2D", &registers, &error))
        return Fail("framebuffer-texture", error);
    registers = {};
    registers[0] = GL_FRAMEBUFFER;
    registers[1] = GL_DEPTH_ATTACHMENT;
    registers[2] = GL_RENDERBUFFER;
    registers[3] = renderbuffer_names[0];
    if (!imports.Dispatch("glFramebufferRenderbuffer", &registers, &error))
        return Fail("framebuffer-depth", error);
    registers = {};
    registers[0] = GL_FRAMEBUFFER;
    registers[1] = GL_STENCIL_ATTACHMENT;
    registers[2] = GL_RENDERBUFFER;
    registers[3] = renderbuffer_names[1];
    if (!imports.Dispatch("glFramebufferRenderbuffer", &registers, &error))
        return Fail("framebuffer-stencil", error);
    const GLenum host_framebuffer_status =
        glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (glGetError() != GL_NO_ERROR || host_framebuffer_status == 0)
        return Fail("framebuffer-driver-status");
    registers = {};
    registers[0] = GL_FRAMEBUFFER;
    error.clear();
    if (!imports.Dispatch("glCheckFramebufferStatus", &registers, &error) ||
        registers[0] != host_framebuffer_status)
        return Fail("framebuffer-status", error);
    registers = {};
    registers[0] = 0xffffffffU;
    error.clear();
    if (imports.Dispatch("glCheckFramebufferStatus", &registers, &error) ||
        error.find("target is not implemented") == std::string::npos)
        return Fail("framebuffer-status-target", error);
    const std::uint32_t unknown_framebuffer = 0xffffffffU;
    if (!heap.Write(framebuffer_address, &unknown_framebuffer,
                    sizeof(unknown_framebuffer)))
        return Fail("delete-unknown-framebuffer-staging");
    registers = {};
    registers[0] = 1;
    registers[1] = framebuffer_address;
    if (!imports.Dispatch("glDeleteFramebuffers", &registers, &error) ||
        gles.framebuffer_count() != 1 ||
        gles.bound_framebuffer() != owned_framebuffer)
        return Fail("delete-unknown-framebuffer", error);
    framebuffer_binding = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_binding);
    if (glGetError() != GL_NO_ERROR ||
        framebuffer_binding != static_cast<GLint>(owned_framebuffer))
        return Fail("delete-unknown-framebuffer-preserves-binding");
    if (!heap.Write(framebuffer_address, &owned_framebuffer,
                    sizeof(owned_framebuffer)))
        return Fail("delete-framebuffer-staging");
    registers = {};
    registers[0] = 1;
    registers[1] = framebuffer_address;
    if (!imports.Dispatch("glDeleteFramebuffers", &registers, &error) ||
        gles.framebuffer_count() != 0 || gles.bound_framebuffer() != 0)
        return Fail("delete-framebuffer", error);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_binding);
    if (glGetError() != GL_NO_ERROR || framebuffer_binding != 0 ||
        glIsFramebuffer(owned_framebuffer) != GL_FALSE ||
        glGetError() != GL_NO_ERROR)
        return Fail("delete-framebuffer-driver-unbind");
    if (!heap.Write(renderbuffer_address, renderbuffer_names,
                    sizeof(renderbuffer_names)))
        return Fail("delete-renderbuffers-staging");
    registers = {};
    registers[0] = 3;
    registers[1] = renderbuffer_address;
    if (!imports.Dispatch("glDeleteRenderbuffers", &registers, &error) ||
        gles.renderbuffer_count() != 0)
        return Fail("delete-renderbuffers", error);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer_binding);
    if (glGetError() != GL_NO_ERROR || renderbuffer_binding != 0 ||
        glIsRenderbuffer(renderbuffer_names[1]) != GL_FALSE ||
        glGetError() != GL_NO_ERROR)
        return Fail("delete-renderbuffers-driver-unbind");
    registers = {};
    registers[0] = GL_RENDERBUFFER;
    registers[1] = GL_DEPTH_COMPONENT16;
    registers[2] = 1;
    registers[3] = 1;
    error.clear();
    if (imports.Dispatch("glRenderbufferStorage", &registers, &error) ||
        error.find("no owned renderbuffer bound") == std::string::npos ||
        !heap.Release(framebuffer_address, &error) ||
        !heap.Release(renderbuffer_address, &error))
        return Fail("delete-renderbuffers-binding-cleanup", error);

    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[2] = GL_RGBA;
    registers[3] = 1;
    registers[4] = 1;
    registers[6] = GL_RGBA;
    registers[7] = GL_UNSIGNED_BYTE;
    registers.stack_pointer = 1;
    error.clear();
    if (imports.Dispatch("glTexImage2D", &registers, &error) ||
        error.find("stack argument is unreadable") == std::string::npos)
        return Fail("tex-image-stack", error);
    registers[0] = GL_TEXTURE_2D;
    registers.stack_pointer = stack_argument_address;
    const std::uint64_t invalid_pixels = 1;
    if (!heap.Write(stack_argument_address, &invalid_pixels,
                    sizeof(invalid_pixels)))
        return Fail("tex-image-invalid-staging");
    error.clear();
    if (imports.Dispatch("glTexImage2D", &registers, &error) ||
        error.find("guest pixels are unreadable") == std::string::npos)
        return Fail("tex-image-pixels", error);
    if (!heap.Write(stack_argument_address, &pixel_address,
                    sizeof(pixel_address)) ||
        !heap.Release(stack_argument_address, &error) ||
        !heap.Release(pixel_address, &error))
        return Fail("tex-image-cleanup", error);

    registers = {};
    registers[0] = GL_PACK_ALIGNMENT;
    registers[1] = 4;
    if (!imports.Dispatch("glPixelStorei", &registers, &error))
        return Fail("pixel-store-restore", error);
    registers = {};
    registers[0] = GL_UNPACK_ALIGNMENT;
    registers[1] = 4;
    if (!imports.Dispatch("glPixelStorei", &registers, &error))
        return Fail("pixel-store-unpack-restore", error);
    // Unknown pixel-store pnames soft-fail like real GLES2: dispatch
    // succeeds and the host INVALID_ENUM is pending for guest glGetError.
    registers = {};
    registers[0] = 0xffffffffU;
    registers[1] = 1;
    error.clear();
    isomr::Aarch64ImportRegistry::Registers store_error_registers{};
    std::string store_error_error;
    if (!imports.Dispatch("glPixelStorei", &registers, &error) ||
        !imports.Dispatch("glGetError", &store_error_registers,
                          &store_error_error) ||
        store_error_registers[0] != GL_INVALID_ENUM)
        return Fail("pixel-store-enum", error);

    registers = {};
    registers[0] = GL_TEXTURE1;
    if (!imports.Dispatch("glActiveTexture", &registers, &error))
        return Fail("delete-textures-active-unit1", error);
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = texture_names[0];
    if (!imports.Dispatch("glBindTexture", &registers, &error))
        return Fail("delete-textures-bind-unit1", error);
    registers = {};
    registers[0] = GL_TEXTURE0;
    if (!imports.Dispatch("glActiveTexture", &registers, &error))
        return Fail("delete-textures-active-unit0", error);

    const std::array<std::uint32_t, 4> texture_delete_names{
        0, 0xffffffffU, texture_names[0], texture_names[0]};
    if (!heap.Write(texture_address, texture_delete_names.data(),
                    sizeof(texture_delete_names)))
        return Fail("delete-textures-input", error);
    registers = {};
    registers[0] = texture_delete_names.size();
    registers[1] = texture_address;
    if (!imports.Dispatch("glDeleteTextures", &registers, &error) ||
        gles.texture_count() != 1)
        return Fail("delete-textures-mixed", error);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_texture);
    if (glGetError() != GL_NO_ERROR || bound_texture != 0 ||
        glIsTexture(texture_names[0]) != GL_FALSE ||
        glGetError() != GL_NO_ERROR)
        return Fail("delete-textures-driver-unbind-unit0");
    registers = {};
    registers[0] = GL_TEXTURE1;
    if (!imports.Dispatch("glActiveTexture", &registers, &error))
        return Fail("delete-textures-check-unit1", error);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_texture);
    if (glGetError() != GL_NO_ERROR || bound_texture != 0)
        return Fail("delete-textures-driver-unbind-unit1");
    registers[0] = GL_TEXTURE0;
    if (!imports.Dispatch("glActiveTexture", &registers, &error))
        return Fail("delete-textures-restore-unit0", error);
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = texture_names[0];
    error.clear();
    if (imports.Dispatch("glBindTexture", &registers, &error) ||
        error.find("texture is not owned") == std::string::npos ||
        gles.texture_count() != 1)
        return Fail("delete-textures-rebind", error);
    // With nothing bound the host raises an error that the runtime treats
    // as a soft no-op/pending guest error rather than a dispatch failure.
    registers = {};
    registers[0] = GL_TEXTURE_2D;
    registers[1] = GL_TEXTURE_MIN_FILTER;
    registers[2] = GL_NEAREST;
    error.clear();
    if (!imports.Dispatch("glTexParameteri", &registers, &error))
        return Fail("delete-textures-binding-cleanup", error);
    registers[0] = 1;
    registers[1] = 1;
    error.clear();
    if (imports.Dispatch("glDeleteTextures", &registers, &error) ||
        error.find("guest input is invalid") == std::string::npos ||
        gles.texture_count() != 1)
        return Fail("delete-textures-address", error);
    registers[0] = 4097;
    registers[1] = texture_address;
    error.clear();
    if (imports.Dispatch("glDeleteTextures", &registers, &error) ||
        error.find("count is out of range") == std::string::npos ||
        gles.texture_count() != 1)
        return Fail("delete-textures-count", error);
    registers = {};
    if (!imports.Dispatch("glDeleteTextures", &registers, &error) ||
        gles.texture_count() != 1)
        return Fail("delete-textures-zero", error);
    if (!heap.Release(texture_address, &error))
        return Fail("delete-textures-input-cleanup", error);
    error.clear();

    shader_registers[0] = 0x8b30;
    if (!imports.Dispatch("glCreateShader", &shader_registers, &error) ||
        shader_registers[0] == 0 || gles.shader_count() != 1)
        return Fail("create-shutdown-shader", error);
    program_registers = {};
    if (!imports.Dispatch("glCreateProgram", &program_registers, &error) ||
        program_registers[0] == 0 || gles.program_count() != 1)
        return Fail("create-shutdown-program", error);

    bool close_requested = true;
    std::vector<isomr::NativePointerEvent> pointer_events;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 1280, 720);
    glClearColor(0.25F, 0.5F, 0.75F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    if (glGetError() != GL_NO_ERROR || !gles.Show(&error) ||
        !gles.window_visible())
        return Fail("show", error);
    HWND pointer_window = FindWindowW(L"ArmrNativeGlesWindow", L"ARMR");
    DWORD pointer_process = 0;
    if (pointer_window == nullptr ||
        GetWindowThreadProcessId(pointer_window, &pointer_process) == 0 ||
        pointer_process != GetCurrentProcessId())
        return Fail("pointer-window");
    (void)SetActiveWindow(pointer_window);
    (void)SetFocus(pointer_window);
    if (!PostMessageW(pointer_window, WM_LBUTTONDOWN, MK_LBUTTON,
                      MAKELPARAM(10, 20)) ||
        !PostMessageW(pointer_window, WM_MOUSEMOVE, MK_LBUTTON,
                      MAKELPARAM(30, 40)) ||
        !PostMessageW(pointer_window, WM_MOUSEMOVE, MK_LBUTTON,
                      MAKELPARAM(50, 60)) ||
        !PostMessageW(pointer_window, WM_LBUTTONUP, 0,
                      MAKELPARAM(70, 80)) ||
        !gles.PumpOwnerMessages(&error))
        return Fail("pointer-inter-slice-dispatch", error);
    // The dispatch-only operation models an inter-slice callback. It must
    // leave pointer events queued until normal post-call guest delivery.
    if (!gles.PumpEvents(&close_requested, &pointer_events, &error) ||
        close_requested || pointer_events.size() != 3 ||
        gles.pending_pointer_event_count() != 3 ||
        pointer_events[0].action != isomr::NativePointerAction::kDown ||
        pointer_events[0].x != 10 || pointer_events[0].y != 20 ||
        pointer_events[1].action != isomr::NativePointerAction::kMove ||
        pointer_events[1].x != 50 || pointer_events[1].y != 60 ||
        pointer_events[2].action != isomr::NativePointerAction::kUp ||
        pointer_events[2].x != 70 || pointer_events[2].y != 80 ||
        pointer_events[0].client_width != 1280 ||
        pointer_events[0].client_height != 720 ||
        pointer_events[0].enqueued_ns == 0 ||
        pointer_events[1].enqueued_ns < pointer_events[0].enqueued_ns ||
        pointer_events[2].enqueued_ns < pointer_events[1].enqueued_ns ||
        GetCapture() != nullptr)
        return Fail("pointer-down-move-up", error);
    for (const isomr::NativePointerEvent& event : pointer_events) {
        if (!gles.AcknowledgePointerEvent(event, &error))
            return Fail("pointer-acknowledge", error);
    }
    if (gles.pending_pointer_event_count() != 0)
        return Fail("pointer-acknowledge-count", error);
    if (!PostMessageW(pointer_window, WM_LBUTTONDOWN, MK_LBUTTON,
                      MAKELPARAM(85, 95)))
        return Fail("pointer-wm-quit-post-down");
    if (!PostThreadMessageW(GetCurrentThreadId(), WM_QUIT, 37, 0))
        return Fail("pointer-wm-quit-post-thread");
    MSG posted_quit{};
    const BOOL quit_found = PeekMessageW(
        &posted_quit, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE);
    if (!quit_found || posted_quit.message != WM_QUIT ||
        posted_quit.wParam != 37)
        return Fail("pointer-wm-quit-post-verify");
    if (!gles.PumpOwnerMessages(&error)) {
        MSG ignored_quit{};
        (void)PeekMessageW(&ignored_quit, nullptr, WM_QUIT, WM_QUIT,
                           PM_REMOVE);
        return Fail("pointer-wm-quit-dispatch", error);
    }
    MSG quit_residue{};
    if (PeekMessageW(&quit_residue, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE))
        return Fail("pointer-wm-quit-residue");
    // The dispatch-only pump surfaces close state without consuming the
    // queued down/cancel pair. A second normal pump must expose that pair
    // exactly once and must not synthesize any pointer input.
    if (!gles.PumpEvents(&close_requested, &pointer_events, &error) ||
        !close_requested || pointer_events.size() != 2 ||
        gles.pending_pointer_event_count() != 2 ||
        pointer_events[0].action != isomr::NativePointerAction::kDown ||
        pointer_events[0].x != 85 || pointer_events[0].y != 95 ||
        pointer_events[1].action != isomr::NativePointerAction::kCancel ||
        pointer_events[1].x != 85 || pointer_events[1].y != 95 ||
        GetCapture() != nullptr) {
        const auto event_action = [&](std::size_t index) {
            return index < pointer_events.size()
                       ? static_cast<int>(pointer_events[index].action)
                       : -1;
        };
        const auto event_x = [&](std::size_t index) {
            return index < pointer_events.size() ? pointer_events[index].x
                                                 : -1;
        };
        const auto event_y = [&](std::size_t index) {
            return index < pointer_events.size() ? pointer_events[index].y
                                                 : -1;
        };
        std::fprintf(
            stderr,
            "ISOMR_NATIVE_GLES wm-quit close=%d events=%zu first=%d,%d,%d second=%d,%d,%d error=%s\n",
            close_requested ? 1 : 0, pointer_events.size(), event_action(0),
            event_x(0), event_y(0), event_action(1), event_x(1), event_y(1),
            error.c_str());
        return Fail("pointer-wm-quit-close-cancel", error);
    }
    for (const isomr::NativePointerEvent& event : pointer_events) {
        if (!gles.AcknowledgePointerEvent(event, &error))
            return Fail("pointer-wm-quit-acknowledge", error);
    }
    if (gles.pending_pointer_event_count() != 0)
        return Fail("pointer-wm-quit-acknowledge-count", error);
    bool foreign_pump_result = true;
    std::string foreign_pump_error;
    std::thread foreign_pump([&] {
        bool foreign_close_requested = false;
        std::vector<isomr::NativePointerEvent> foreign_pointer_events;
        foreign_pump_result = gles.PumpEvents(
            &foreign_close_requested, &foreign_pointer_events,
            &foreign_pump_error);
    });
    foreign_pump.join();
    if (foreign_pump_result ||
        foreign_pump_error.find("owner thread") == std::string::npos)
        return Fail("pointer-owner-thread", foreign_pump_error);
    const LONG_PTR owner_window_state =
        GetWindowLongPtrW(pointer_window, GWLP_USERDATA);
    bool foreign_shutdown_result = true;
    std::string foreign_shutdown_error;
    std::thread foreign_shutdown([&] {
        foreign_shutdown_result = gles.Shutdown(&foreign_shutdown_error);
    });
    foreign_shutdown.join();
    if (foreign_shutdown_result ||
        foreign_shutdown_error.find("owner thread") == std::string::npos ||
        owner_window_state == 0 || !IsWindow(pointer_window) ||
        gles.window_handle() != pointer_window ||
        GetWindowLongPtrW(pointer_window, GWLP_USERDATA) !=
            owner_window_state ||
        !gles.initialized() || !gles.is_current())
        return Fail("shutdown-foreign-owner-preserved",
                    foreign_shutdown_error);
    if (!gles.Present("zero-payload-smoke", &error) ||
        gles.present_count() != 1 || gles.lit_present_count() != 1 ||
        gles.present_readback_count() != 1 ||
        !gles.lit_pixel_observed() ||
        gles.last_sampled_pixel_count() != 1280U * 720U ||
        gles.last_lit_pixel_count() == 0)
        return Fail("first-present", error);
    glClearColor(0.75F, 0.25F, 0.5F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    // Presents 1-3 are all sampled, so this lit frame is read back and
    // counted as a second lit present.
    if (glGetError() != GL_NO_ERROR ||
        !gles.Present("zero-payload-smoke", &error) ||
        gles.present_count() != 2 || gles.lit_present_count() != 2 ||
        gles.present_readback_count() != 2)
        return Fail("second-present", error);
    if (!SetEnvironmentVariableW(L"ISOMR_PRESENT_CAPTURE_INDEX", L"3") ||
        !SetEnvironmentVariableW(L"ISOMR_PRESENT_CAPTURE_BMP", L"NUL"))
        return Fail("present-capture-request");
    error.clear();
    const bool present_capture_ok =
        gles.Present("zero-payload-smoke", &error);
    const bool present_index_unset =
        SetEnvironmentVariableW(L"ISOMR_PRESENT_CAPTURE_INDEX", nullptr) !=
        FALSE;
    const bool present_path_unset =
        SetEnvironmentVariableW(L"ISOMR_PRESENT_CAPTURE_BMP", nullptr) !=
        FALSE;
    if (!present_capture_ok || !present_index_unset ||
        !present_path_unset || gles.present_count() != 3 ||
        gles.lit_present_count() != 3 ||
        gles.present_readback_count() != 3)
        return Fail("present-explicit-capture", error);

    bool foreign_close_result = true;
    std::string foreign_close_error;
    std::thread foreign_close([&] {
        foreign_close_result = gles.RequestClose(&foreign_close_error);
    });
    foreign_close.join();
    if (foreign_close_result ||
        foreign_close_error.find("owner thread") == std::string::npos)
        return Fail("close-request-foreign", foreign_close_error);
    (void)SendMessageW(pointer_window, WM_LBUTTONDOWN, MK_LBUTTON,
                       MAKELPARAM(90, 100));
    if (!gles.RequestClose(&error))
        return Fail("close-request-owner", error);
    if (!gles.PumpEvents(&close_requested, &pointer_events, &error) ||
        !close_requested || pointer_events.size() != 2 ||
        pointer_events[0].action != isomr::NativePointerAction::kDown ||
        pointer_events[1].action != isomr::NativePointerAction::kCancel ||
        pointer_events[1].x != 90 || pointer_events[1].y != 100 ||
        GetCapture() != nullptr || gles.window_visible())
        return Fail("pointer-close-cancel", error);

    if (!gles.ClearCurrent(&error) || gles.is_current())
        return Fail("clear-current", error);
    registers[0] = kGlVendor;
    error.clear();
    if (imports.Dispatch("glGetString", &registers, &error) ||
        registers[0] != 0 ||
        error.find("current native GPU context") == std::string::npos)
        return Fail("no-current", error);
    registers = {};
    error.clear();
    if (imports.Dispatch("glDeleteTextures", &registers, &error) ||
        error.find("current native GPU context") == std::string::npos ||
        gles.texture_count() != 1)
        return Fail("delete-textures-no-current", error);
    if (!gles.MakeCurrent(&error) || !gles.is_current() ||
        !gles.Shutdown(&error) || heap.allocation_count() != 0 ||
        eglGetCurrentContext() != EGL_NO_CONTEXT ||
        gles.window_handle() != nullptr || IsWindow(pointer_window))
        return Fail("shutdown", error);
    error.clear();
    if (gles.RequestClose(&error) ||
        error.find("state is invalid") == std::string::npos)
        return Fail("close-request-after-shutdown", error);

    const auto unused_executor_factory =
        [](std::uint64_t,
           std::string*) -> isomr::BionicThreads::GuestCall {
        return {};
    };
    isomr::BionicRuntime retry_runtime(
        0x750000000000ULL, 256 * 1024, 0x6b0000000000ULL,
        unused_executor_factory);
    std::string runtime_shutdown_error;
    if (!retry_runtime.Initialize(&runtime_shutdown_error))
        return Fail("runtime-shutdown-initialize",
                    runtime_shutdown_error);

    bool runtime_foreign_shutdown_result = true;
    std::string runtime_foreign_shutdown_error;
    std::thread runtime_foreign_shutdown([&] {
        runtime_foreign_shutdown_result =
            retry_runtime.Shutdown(&runtime_foreign_shutdown_error);
    });
    runtime_foreign_shutdown.join();
    if (runtime_foreign_shutdown_result ||
        runtime_foreign_shutdown_error.find("owner thread") ==
            std::string::npos ||
        !retry_runtime.gles().initialized() ||
        !retry_runtime.gles().is_current())
        return Fail("runtime-shutdown-foreign-owner",
                    runtime_foreign_shutdown_error);

    runtime_shutdown_error.clear();
    if (!retry_runtime.Shutdown(&runtime_shutdown_error) ||
        retry_runtime.heap().allocation_count() != 0)
        return Fail("runtime-shutdown-owner-retry",
                    runtime_shutdown_error);
    runtime_shutdown_error.clear();
    if (retry_runtime.Shutdown(&runtime_shutdown_error) ||
        runtime_shutdown_error.find("already shut down") ==
            std::string::npos)
        return Fail("runtime-shutdown-repeat",
                    runtime_shutdown_error);

    std::puts("ISOMR_NATIVE_GLES strings=vendor,renderer,version,shading-language,extensions guest-owned provenance=current-context invalid=null-error");
    std::puts("ISOMR_NATIVE_GLES state=max-texture-size,max-cube-map-texture-size,max-texture-image-units,max-vertex-attribs,max-varying-vectors,max-vertex-uniform-vectors,max-fragment-uniform-vectors,max-combined-texture-image-units,max-renderbuffer-size,framebuffer-binding guest-write=checked unsupported=fail-closed");
    std::puts("ISOMR_NATIVE_GLES resources=textures generation=driver active-unit=checked binding=texture2d-owned-per-unit parameters=integer-driver-state upload=image-subimage-alpha-rgb-rgba8-guest-copy-stack9-checked-null-allocation-zero-width-height-no-guest-read delete=zero-unknown-duplicate-owned-bound-all-units renderbuffers=generation-bind-depth16-stencil8-depth24-stencil8-storage-delete-unknown-noop-owned-bound-unbind framebuffers=generation-owned-bind-texture-renderbuffer-status-delete-unknown-noop-owned-bound-unbind viewport=driver-state depth-range=clamped-driver-state clear=color-depth-stencil-fp-abi-mask-checked-driver-state capabilities=enable-disable-driver-state write-masks=color-depth-stencil-driver-state blend=func-separate-equation-separate-driver-state raster=depth-func,cull-face,front-face,scissor-driver-state vertex-attrib-arrays=enable-disable-driver-state vertex-pointers=guest-metadata-checked draw-elements=active-program-attributes-only-inactive-stale-skip-zero-count-no-read-client-indices-attributes-bounded-host-staging-black-baseline-no-normal-readback-explicit-capture-census vertex-constant=2fv,3fv,4fv-guest-copy-driver-state shaders=create-source-compile-status-log-delete-pending-attached-usable-release-on-program-delete programs=create-attach-bind-attrib-link-status-log-reflect-uniform-attrib-locations-use-delete uniforms=1i,1f,1fv,2fv,4f,4fv,matrix4fv-checked-driver-state driver-owned driver-state=verified guest-write=checked rollback=pass ownership=guest-shutdown");
    std::puts("ISOMR_NATIVE_GLES pixel-store=pack-unpack-alignment-1,2,4,8 state=driver unsupported=fail-closed");
    std::puts("ISOMR_NATIVE_GLES proc-address=known-guest-cell unknown=null guest-name=checked");
    std::puts("ISOMR_NATIVE_GLES input=win32-left-pointer ordered=down-move-up move=coalesced inter-slice=dispatch-only-queued-until-call-complete capture=press-release cancel=wm-close-wm-quit close=instance-owned owner-thread=checked request=owner-pass-foreign-postshutdown-reject teardown=foreign-refused-owner-destroyed");
    std::puts("ISOMR_NATIVE_GLES context=angle-d3d11-hardware-es2 window=hidden-by-default-explicit-show fixed-client=1280x720 pump=nonblocking present=first-lit-sampled-second-after-lit-no-readback-explicit-capture-readback-zero-payload-smoke absence=fail-closed cleanup=pass");
    std::puts("ISOMR_NATIVE_GLES runtime-shutdown=late-owner-refusal-retry-pass repeat=reject cleanup=pass");
    std::puts("ISOMR_NATIVE_GLES=pass");
    return 0;
}
