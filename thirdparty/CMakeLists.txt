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

#
# ---------------------------------------------------------
# LZO (LZO1X compression for saves, maps, and network blocks)
# ---------------------------------------------------------
#
# A vendored copy rather than a submodule; docs/BUILDING.md records why. It holds the three
# LZO1X-1 sources the engine calls and every header they reach.
#
# The library is built here rather than through its own CMakeLists.txt, which asks for
# compatibility with CMake 3.0 and is refused outright by CMake 4, and which also builds
# the test drivers and install rules that the engine does not ship.
file(GLOB LZO_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lzo/src/*.c")
add_library(lzo STATIC ${LZO_SOURCES})

# LZO reads a generated config.h only when LZO_HAVE_CONFIG_H is defined, and probes the
# target with its own preprocessor tests otherwise. Leaving it undefined keeps the
# vendored tree free of a configure step.
target_include_directories(lzo
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lzo/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lzo/src"
)


#
# ---------------------------------------------------------
# FreeType (glyph rasterization for RmlUi)
# ---------------------------------------------------------
#
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/freetype/CMakeLists.txt")
    message(FATAL_ERROR
        "thirdparty/freetype is empty. Fetch the submodules with:\n"
        "  git submodule update --init --recursive")
endif()

# Every dependency is linked into the executable, and RmlUi's own default is a shared
# library, so the standard option is answered here for both submodules below.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# FreeType reaches for compression and shaping libraries when it finds them on the host.
# The engine rasterizes plain TrueType faces, so every optional dependency is refused
# rather than left to whatever the build machine happens to have installed.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)

add_subdirectory(freetype)

# FreeType reports a damaged font by longjmp. The engine keeps unwinding inside wasm with
# -fwasm-exceptions so that JSPI has no invoke_ import to mistake for a yield, and longjmp
# has to travel the same way for the same reason.
if(EMSCRIPTEN)
    target_compile_options(freetype PRIVATE -sSUPPORT_LONGJMP=wasm)
endif()

# RmlUi looks for the target that the CMake FindFreetype module defines, and FreeType's own
# build does not define it. The alias supplies it from the submodule target.
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

#
# ---------------------------------------------------------
# RmlUi (document-based user interface)
# ---------------------------------------------------------
#
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RmlUi/CMakeLists.txt")
    message(FATAL_ERROR
        "thirdparty/RmlUi is empty. Fetch the submodules with:\n"
        "  git submodule update --init --recursive")
endif()

# The engine supplies its own render, system, and file interfaces, so none of RmlUi's
# backends or samples are built. Its debugger plugin comes with the core library.
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_HARFBUZZ_SAMPLE OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)

# RmlUi's dependency check is satisfied by the alias above, so the host is never probed.
# A system FreeType would otherwise be preferred over the pinned submodule.
set(CMAKE_DISABLE_FIND_PACKAGE_Freetype ON)

add_subdirectory(RmlUi)

#
# ---------------------------------------------------------
# Solution layout
# ---------------------------------------------------------
#

# Files a directory's targets, and those of every directory below it, under one solution
# folder. A target that already names a folder keeps it as a subfolder of that one, so the
# grouping bgfx gives its own targets survives.
function(opents_set_solution_folder directory folder)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(current ${target} FOLDER)
        if(current)
            set_target_properties(${target} PROPERTIES FOLDER "${folder}/${current}")
        else()
            set_target_properties(${target} PROPERTIES FOLDER "${folder}")
        endif()
    endforeach()

    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        opents_set_solution_folder("${subdirectory}" "${folder}")
    endforeach()
endfunction()

opents_set_solution_folder("${CMAKE_CURRENT_SOURCE_DIR}" "Thirdparty")
