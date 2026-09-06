#
# ---------------------------------------------------------
# bgfx (rendering backend)
# ---------------------------------------------------------
#
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bgfx.cmake/CMakeLists.txt")
    message(FATAL_ERROR
        "thirdparty/bgfx.cmake is empty. Fetch the submodules with:\n"
        "  git submodule update --init --recursive")
endif()

# bgfx builds shader and texture tools by default; the engine ships neither.
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_RENDERER_WEBGPU OFF CACHE BOOL "" FORCE)

if(MSVC)
    # The engine links the static CRT, so its dependencies must agree.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# bgfx chooses its renderer and its render thread per platform, so the Emscripten build asks
# for neither: it reaches the browser GPU through OpenGL ES 3, which Emscripten translates to
# WebGL 2, and it runs single-threaded, which is what a page without cross-origin isolation
# can load.

# Emscripten's toolchain reports an x86 processor, so bx adds -msse4.2 to every one of its
# translation units. clang lowers those intrinsics to WebAssembly SIMD, but only when the
# SIMD feature is enabled, and refuses the flags otherwise.
if(EMSCRIPTEN)
    add_compile_options(-msimd128)
endif()

# bgfx defaults to its OpenGL ES 2 path under Emscripten. The context is WebGL 2 either way,
# so ask for the renderer that matches it.
if(EMSCRIPTEN)
    set(BGFX_OPENGLES_VERSION 30 CACHE STRING "" FORCE)
endif()

add_subdirectory(bgfx.cmake)

# These libraries support the disabled texture tools and are not linked into OpenTS.
set_target_properties(bimg_decode bimg_encode PROPERTIES EXCLUDE_FROM_ALL TRUE)

if(OPENTS_EXPERIMENTAL_CLANG_CL AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # bx treats Clang with the MSVC CRT like a non-x86 compiler and erases
    # __stdcall. Restore it before Windows declarations are parsed.
    foreach(target bx bimg bgfx)
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:/FI${PROJECT_SOURCE_DIR}/thirdparty/bx-clang-compat.h>"
        )
    endforeach()

    # The ASTC encoder selects a 64-bit popcount intrinsic when SSE4.2 is enabled,
    # but that intrinsic is unavailable for the Win32 target. Its SSE2 path is portable.
    target_compile_definitions(bimg PRIVATE ASTCENC_POPCNT=0 ASTCENC_SSE=20)
endif()

#
# ---------------------------------------------------------
# miniaudio (audio device, resampling, decoders)
# ---------------------------------------------------------
#
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio/miniaudio.h")
    message(FATAL_ERROR
        "thirdparty/miniaudio is empty. Fetch the submodules with:\n"
        "  git submodule update --init --recursive")
endif()

# The library is built here rather than through its own CMakeLists.txt, which applies
# the MA_NO_* options privately; consumers of miniaudio.h must see the same set or the
# structures they share with the implementation change layout.
add_library(miniaudio STATIC "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio-impl.c")
target_include_directories(miniaudio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio")

# Only the device layer, the resampler, and the decoders are used. The engine, node
# graph, and resource manager are left out along with generation and encoding.
target_compile_definitions(miniaudio PUBLIC
    MA_NO_ENGINE
    MA_NO_NODE_GRAPH
    MA_NO_RESOURCE_MANAGER
    MA_NO_GENERATION
    MA_NO_ENCODING
)

if(WIN32)
    # Backends are loaded at run time, so no import library is needed for them.
    target_compile_definitions(miniaudio PUBLIC
        MA_ENABLE_ONLY_SPECIFIC_BACKENDS
        MA_ENABLE_WASAPI
        MA_ENABLE_DSOUND
        MA_ENABLE_WINMM
        MA_ENABLE_NULL
    )
endif()

if(EMSCRIPTEN)
    # A page reaches audio through Web Audio and nothing else, so the other backends are
    # left out rather than compiled and never selected. The library's own unlock handlers
    # resume the context on the first gesture, which is the only way a page may start it.
    #
    # AudioWorklets are not asked for: they need WASM workers and cross-origin isolation,
    # so the ScriptProcessorNode path is what a page without those headers can load.
    target_compile_definitions(miniaudio PUBLIC
        MA_ENABLE_ONLY_SPECIFIC_BACKENDS
        MA_ENABLE_WEBAUDIO
        MA_ENABLE_NULL
    )
endif()

if(MSVC)
    # Match the engine's floating-point model so the resampler behaves the same in both.
    target_compile_options(miniaudio PRIVATE /arch:SSE2 /fp:precise)
endif()
