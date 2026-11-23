cmake_minimum_required(VERSION 3.20)
project(ArmrAarch64Smoke LANGUAGES C CXX ASM)

option(ISOMR_BUILD_EXPERIMENTAL_CTR "Build the profile-driven game probe (ArmrCtrEntryProbe) used by --game titles" ON)
set(ISOMR_WEBP103_INCLUDE_DIR "" CACHE PATH
    "Optional exact libwebp 1.0.3 source root")
set(ISOMR_WEBP103_LIBRARY "" CACHE FILEPATH
    "Optional exact libwebp 1.0.3 static decoder library")
set(ISOMR_RAPIDJSON110_INCLUDE_DIR "" CACHE PATH
    "Exact RapidJSON 1.1.0 source include directory")

if((ISOMR_WEBP103_INCLUDE_DIR AND NOT ISOMR_WEBP103_LIBRARY) OR
   (ISOMR_WEBP103_LIBRARY AND NOT ISOMR_WEBP103_INCLUDE_DIR))
    message(FATAL_ERROR
        "ISOMR_WEBP103_INCLUDE_DIR and ISOMR_WEBP103_LIBRARY must be supplied together")
endif()
if(NOT EXISTS "${ISOMR_RAPIDJSON110_INCLUDE_DIR}/rapidjson/document.h")
    message(FATAL_ERROR
        "Exact RapidJSON 1.1.0 headers are required")
endif()
if(ISOMR_WEBP103_INCLUDE_DIR)
    foreach(isomr_webp103_file
            "${ISOMR_WEBP103_INCLUDE_DIR}/src/webp/decode.h"
            "${ISOMR_WEBP103_LIBRARY}")
        if(NOT EXISTS "${isomr_webp103_file}")
            message(FATAL_ERROR
                "Required exact libwebp 1.0.3 input is missing: ${isomr_webp103_file}")
        endif()
    endforeach()
endif()

if(NOT DEFINED ISOMR_DYNARMIC_SOURCE)
    message(FATAL_ERROR "ISOMR_DYNARMIC_SOURCE is required")
endif()
if(NOT DEFINED ISOMR_JAVA_HOME)
    message(FATAL_ERROR "ISOMR_JAVA_HOME is required")
endif()
if(NOT DEFINED ISOMR_ANGLE_ROOT)
    message(FATAL_ERROR "ISOMR_ANGLE_ROOT is required")
endif()

set(ISOMR_ANGLE_INCLUDE "${ISOMR_ANGLE_ROOT}/include")
set(ISOMR_ANGLE_EGL_IMPLIB "${ISOMR_ANGLE_ROOT}/lib/libEGL.lib")
set(ISOMR_ANGLE_GLES_IMPLIB "${ISOMR_ANGLE_ROOT}/lib/libGLESv2.lib")
set(ISOMR_ANGLE_EGL_DLL "${ISOMR_ANGLE_ROOT}/bin/libEGL.dll")
set(ISOMR_ANGLE_GLES_DLL "${ISOMR_ANGLE_ROOT}/bin/libGLESv2.dll")
set(ISOMR_VORBISFILE_DLL "${ISOMR_ANGLE_ROOT}/bin/vorbisfile.dll")
set(ISOMR_VORBIS_DLL "${ISOMR_ANGLE_ROOT}/bin/vorbis.dll")
set(ISOMR_OGG_DLL "${ISOMR_ANGLE_ROOT}/bin/ogg.dll")
# LuaJIT (vcpkg, same installed root) hosts the per-game lifecycle scripts
# under games/<id>.lua — see game_script.cpp.
set(ISOMR_LUAJIT_INCLUDE "${ISOMR_ANGLE_ROOT}/include/luajit")
set(ISOMR_LUAJIT_IMPLIB "${ISOMR_ANGLE_ROOT}/lib/lua51.lib")
set(ISOMR_LUAJIT_DLL "${ISOMR_ANGLE_ROOT}/bin/lua51.dll")
foreach(isomr_angle_file
        "${ISOMR_ANGLE_INCLUDE}/EGL/egl.h"
        "${ISOMR_ANGLE_INCLUDE}/GLES2/gl2.h"
        "${ISOMR_ANGLE_EGL_IMPLIB}"
        "${ISOMR_ANGLE_GLES_IMPLIB}"
        "${ISOMR_ANGLE_EGL_DLL}"
        "${ISOMR_ANGLE_GLES_DLL}"
        "${ISOMR_VORBISFILE_DLL}"
        "${ISOMR_VORBIS_DLL}"
        "${ISOMR_OGG_DLL}"
        "${ISOMR_LUAJIT_INCLUDE}/lua.h"
        "${ISOMR_LUAJIT_IMPLIB}"
        "${ISOMR_LUAJIT_DLL}")
    if(NOT EXISTS "${isomr_angle_file}")
        message(FATAL_ERROR "Required native runtime input is missing: ${isomr_angle_file}")
    endif()
endforeach()

function(isomr_stage_angle_runtime isomr_target)
    add_custom_command(TARGET ${isomr_target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ISOMR_ANGLE_EGL_DLL}" "$<TARGET_FILE_DIR:${isomr_target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ISOMR_ANGLE_GLES_DLL}" "$<TARGET_FILE_DIR:${isomr_target}>"
        VERBATIM)
endfunction()

list(PREPEND CMAKE_PREFIX_PATH "${ISOMR_ANGLE_ROOT}")
find_package(Vorbis CONFIG REQUIRED COMPONENTS File)

function(isomr_stage_luajit_runtime isomr_target)
    add_custom_command(TARGET ${isomr_target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ISOMR_LUAJIT_DLL}" "$<TARGET_FILE_DIR:${isomr_target}>"
        VERBATIM)
endfunction()

function(isomr_stage_vorbis_runtime isomr_target)
    foreach(isomr_vorbis_dll
            "${ISOMR_VORBISFILE_DLL}"
            "${ISOMR_VORBIS_DLL}"
            "${ISOMR_OGG_DLL}")
        add_custom_command(TARGET ${isomr_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${isomr_vorbis_dll}"
                    "$<TARGET_FILE_DIR:${isomr_target}>"
            VERBATIM)
    endforeach()
endfunction()

set(ISOMR_JNI_INCLUDE "${ISOMR_JAVA_HOME}/include")
set(ISOMR_JNI_WIN32_INCLUDE "${ISOMR_JNI_INCLUDE}/win32")
set(ISOMR_JAVAC "${ISOMR_JAVA_HOME}/bin/javac.exe")
set(ISOMR_PROBE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../../java-stubs/isomr/Probe.java")
set(ISOMR_PROBE_CLASSES "${CMAKE_CURRENT_BINARY_DIR}/java-classes")
foreach(isomr_jdk_file
        "${ISOMR_JNI_INCLUDE}/jni.h"
        "${ISOMR_JNI_WIN32_INCLUDE}/jni_md.h"
        "${ISOMR_JAVAC}"
        "${ISOMR_JAVA_HOME}/bin/server/jvm.dll"
        "${ISOMR_PROBE_SOURCE}")
    if(NOT EXISTS "${isomr_jdk_file}")
        message(FATAL_ERROR "Required desktop JNI input is missing: ${isomr_jdk_file}")
    endif()
endforeach()

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_LLVM OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)

add_subdirectory("${ISOMR_DYNARMIC_SOURCE}" dynarmic-build)

add_executable(ArmrAarch64Smoke aarch64_smoke.cpp)
target_compile_features(ArmrAarch64Smoke PRIVATE cxx_std_20)
target_link_libraries(ArmrAarch64Smoke PRIVATE dynarmic)

add_library(ArmrElf64Loader STATIC
    aarch64_guest_session.cpp
    aarch64_profile_runtime.cpp
    bionic_atexit.cpp
    bionic_assets.cpp
    bionic_dynamic_loader.cpp
    bionic_files.cpp
    bionic_format.cpp
    bionic_locales.cpp
    bionic_log.cpp
    bionic_jumps.cpp
    bionic_math.cpp
    bionic_numbers.cpp
    bionic_memory.cpp
    bionic_mappings.cpp
    bionic_memory_ops.cpp
    bionic_process.cpp
    bionic_properties.cpp
    bionic_random.cpp
    bionic_stdio.cpp
    bionic_runtime.cpp
    bionic_signals.cpp
    bionic_sync.cpp
    bionic_thread_state.cpp
    bionic_threads.cpp
    bionic_time.cpp
    bionic_tls.cpp
    candy_webp_host.cpp
    candy_vorbis_host.cpp
    candy_rapidjson_host.cpp
    pvz_inflate_host.cpp
    candy_entry_bridge.cpp
    elf64_loader.cpp
    elf64_modules.cpp
    game_profile.cpp
    game_script.cpp
    guest_address_space.cpp
    guest_function_table.cpp
    guest_heap.cpp
    guest_jni.cpp
    worker_execution_telemetry.h
    import_registry.cpp)
target_sources(ArmrElf64Loader PRIVATE native_gles.cpp native_opensles.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../ui/game_window.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../ui/chat_head_overlay.cpp")
target_compile_features(ArmrElf64Loader PUBLIC cxx_std_20)
target_include_directories(ArmrElf64Loader PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${ISOMR_ANGLE_INCLUDE}")
target_include_directories(ArmrElf64Loader SYSTEM PRIVATE
    "${ISOMR_RAPIDJSON110_INCLUDE_DIR}" "${ISOMR_LUAJIT_INCLUDE}")
target_link_libraries(ArmrElf64Loader PUBLIC
    Bcrypt Gdi32 Gdiplus Mincore Ole32 User32 Dwmapi dynarmic
    Vorbis::vorbisfile
    "${ISOMR_ANGLE_EGL_IMPLIB}" "${ISOMR_ANGLE_GLES_IMPLIB}"
    "${ISOMR_LUAJIT_IMPLIB}")

add_executable(ArmrNativeGlesSmoke native_gles_smoke.cpp)
target_compile_features(ArmrNativeGlesSmoke PRIVATE cxx_std_20)
target_link_libraries(ArmrNativeGlesSmoke PRIVATE ArmrElf64Loader)

# Chat-head overlay substrate smoke — pure Win32/GDI+, no guest payload.
add_executable(ArmrChatHeadSmoke chat_head_smoke.cpp)
target_compile_features(ArmrChatHeadSmoke PRIVATE cxx_std_20)
target_link_libraries(ArmrChatHeadSmoke PRIVATE ArmrElf64Loader Gdiplus)

add_executable(ArmrElf64Smoke elf64_smoke.cpp elf64_fixture.cpp)
target_compile_features(ArmrElf64Smoke PRIVATE cxx_std_20)
target_link_libraries(ArmrElf64Smoke PRIVATE ArmrElf64Loader dynarmic)
# The smoke's single 17k-line main() holds ~0.85 MiB of locals in an
# unoptimized frame; Dynarmic's one-time decode-table build then needs another
# ~0.15 MiB, which tips the 1 MiB default stack over (0xc00000fd).
if(MSVC)
    set_target_properties(ArmrElf64Smoke PROPERTIES
                          LINK_FLAGS "/STACK:134217728")
endif()
isomr_stage_vorbis_runtime(ArmrElf64Smoke)

add_executable(ArmrElf64Probe elf64_probe.cpp)
target_compile_features(ArmrElf64Probe PRIVATE cxx_std_20)
target_link_libraries(ArmrElf64Probe PRIVATE ArmrElf64Loader dynarmic)

add_executable(ArmrBionicJumpsSmoke
    bionic_jumps_fixture.cpp
    bionic_jumps_smoke.cpp)
target_compile_features(ArmrBionicJumpsSmoke PRIVATE cxx_std_20)
target_link_libraries(ArmrBionicJumpsSmoke PRIVATE ArmrElf64Loader dynarmic)

add_executable(ArmrProfileProbe profile_probe.cpp)
target_compile_features(ArmrProfileProbe PRIVATE cxx_std_20)
target_link_libraries(ArmrProfileProbe PRIVATE ArmrElf64Loader dynarmic)

add_executable(ArmrCandyEntrySmoke
    candy_entry_smoke.cpp
    candy_entry_fixture.cpp)
target_compile_features(ArmrCandyEntrySmoke PRIVATE cxx_std_20)
target_link_libraries(ArmrCandyEntrySmoke PRIVATE ArmrElf64Loader dynarmic)

add_executable(ArmrCandyEntryProbe candy_entry_probe.cpp)
target_compile_features(ArmrCandyEntryProbe PRIVATE cxx_std_20)
target_link_libraries(ArmrCandyEntryProbe PRIVATE ArmrElf64Loader dynarmic)
isomr_stage_vorbis_runtime(ArmrCandyEntryProbe)
if(MSVC)
    target_link_options(ArmrCandyEntryProbe PRIVATE
        "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'")
endif()

if(ISOMR_WEBP103_INCLUDE_DIR)
    add_library(ArmrWebP103 SHARED candy_webp_103_shim.cpp)
    target_compile_features(ArmrWebP103 PRIVATE cxx_std_20)
    target_include_directories(ArmrWebP103 PRIVATE
        "${ISOMR_WEBP103_INCLUDE_DIR}")
    target_link_libraries(ArmrWebP103 PRIVATE
        "${ISOMR_WEBP103_LIBRARY}")
    add_dependencies(ArmrCandyEntryProbe ArmrWebP103)
endif()

add_executable(ArmrBionicWaitableTimerSmoke bionic_waitable_timer_smoke.cpp)
target_compile_features(ArmrBionicWaitableTimerSmoke PRIVATE cxx_std_20)
target_link_libraries(ArmrBionicWaitableTimerSmoke PRIVATE ArmrElf64Loader)

if(ISOMR_BUILD_EXPERIMENTAL_CTR)
    target_sources(ArmrElf64Loader PRIVATE ctr_entry_bridge.cpp)
    add_executable(ArmrCtrEntryProbe ctr_entry_probe.cpp)
    target_compile_features(ArmrCtrEntryProbe PRIVATE cxx_std_20)
    target_link_libraries(ArmrCtrEntryProbe PRIVATE ArmrElf64Loader dynarmic)
    # PvZ's onTimerEvent callback runs the engine's whole consent/post-load
    # sequence inside one guest call; the deep guest recursion needs a
    # larger host stack than the 1 MiB default (0xc0000409 fastfail).
    if(MSVC)
        set_target_properties(ArmrCtrEntryProbe PROPERTIES
                              LINK_FLAGS "/STACK:134217728")
    endif()
    set(ISOMR_EXPERIMENTAL_CTR_TARGETS ArmrCtrEntryProbe)
endif()

add_executable(ArmrThreadSmoke thread_smoke.cpp)
target_compile_features(ArmrThreadSmoke PRIVATE cxx_std_20)
target_link_libraries(ArmrThreadSmoke PRIVATE dynarmic)

add_custom_command(
    OUTPUT "${ISOMR_PROBE_CLASSES}/isomr/Probe.class"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${ISOMR_PROBE_CLASSES}"
    COMMAND "${ISOMR_JAVAC}" -encoding UTF-8 -d "${ISOMR_PROBE_CLASSES}"
            "${ISOMR_PROBE_SOURCE}"
    DEPENDS "${ISOMR_PROBE_SOURCE}"
    VERBATIM)
add_custom_target(ArmrProbeClasses ALL
    DEPENDS "${ISOMR_PROBE_CLASSES}/isomr/Probe.class")

add_executable(ArmrGuestJniDesktopSmoke
    desktop_jvm.cpp
    guest_jni_desktop_smoke.cpp)
target_compile_features(ArmrGuestJniDesktopSmoke PRIVATE cxx_std_20)
target_include_directories(ArmrGuestJniDesktopSmoke PRIVATE
    "${ISOMR_JNI_INCLUDE}" "${ISOMR_JNI_WIN32_INCLUDE}")
target_link_libraries(ArmrGuestJniDesktopSmoke PRIVATE ArmrElf64Loader)
add_dependencies(ArmrGuestJniDesktopSmoke ArmrProbeClasses)

foreach(isomr_angle_target
        ArmrNativeGlesSmoke ArmrElf64Smoke ArmrElf64Probe
        ArmrBionicJumpsSmoke ArmrProfileProbe ArmrCandyEntrySmoke
        ArmrCandyEntryProbe ${ISOMR_EXPERIMENTAL_CTR_TARGETS}
        ArmrGuestJniDesktopSmoke)
    isomr_stage_angle_runtime(${isomr_angle_target})
    isomr_stage_luajit_runtime(${isomr_angle_target})
endforeach()

if(MSVC)
    foreach(isomr_target ArmrAarch64Smoke ArmrElf64Loader ArmrNativeGlesSmoke ArmrElf64Smoke ArmrElf64Probe ArmrBionicJumpsSmoke ArmrProfileProbe ArmrCandyEntrySmoke ArmrCandyEntryProbe ArmrBionicWaitableTimerSmoke ${ISOMR_EXPERIMENTAL_CTR_TARGETS} ArmrThreadSmoke ArmrGuestJniDesktopSmoke)
        target_compile_options(${isomr_target} PRIVATE /W4 /WX)
    endforeach()
    if(TARGET ArmrWebP103)
        target_compile_options(ArmrWebP103 PRIVATE /W4 /WX)
    endif()
endif()

# ---------------------------------------------------------------------------
# ArmrLauncher: native Dear ImGui launcher. Host-side tool only — it does not
# link the guest runtime; it enumerates games/*.profile and spawns the probe.
# Dear ImGui (with the Win32 + DX11 backends) comes from the same vcpkg root
# as ANGLE.
set(ISOMR_IMGUI_INCLUDE "${ISOMR_ANGLE_ROOT}/include")
set(ISOMR_IMGUI_IMPLIB "${ISOMR_ANGLE_ROOT}/lib/imgui.lib")
if(EXISTS "${ISOMR_IMGUI_IMPLIB}" AND
   EXISTS "${ISOMR_IMGUI_INCLUDE}/imgui_impl_dx11.h")
    add_executable(ArmrLauncher WIN32
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../launcher/main.cpp")
    target_compile_features(ArmrLauncher PRIVATE cxx_std_20)
    target_include_directories(ArmrLauncher PRIVATE "${ISOMR_IMGUI_INCLUDE}")
    target_link_libraries(ArmrLauncher PRIVATE "${ISOMR_IMGUI_IMPLIB}")
endif()
