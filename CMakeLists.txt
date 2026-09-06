cmake_minimum_required(VERSION 3.23)

project(OpenTS VERSION 0.2.0 LANGUAGES C CXX)

# A SemVer prerelease label, which project() cannot carry because it takes numbers only. Set it
# to "beta1" and the version reads 0.1.0-beta1; leave it empty outside a prerelease series. The
# manual's release registry is checked against both this and the project version.
set(OPENTS_VERSION_PRERELEASE "")

# Marks a build published under the version it declares, so that it is named by that version
# alone. Every other build names the commit it came from as well.
option(OPENTS_OFFICIAL_BUILD "Build as an official release of the declared version" OFF)

option(OPENTS_EXPERIMENTAL_CLANG_CL "Build with clang-cl using the MSVC ABI" OFF)

set(OPENTS_MOVIE_FORMAT "VQA" CACHE STRING "Movie container selected by this build: VQA or MP4")
set_property(CACHE OPENTS_MOVIE_FORMAT PROPERTY STRINGS VQA MP4)
string(TOUPPER "${OPENTS_MOVIE_FORMAT}" OPENTS_MOVIE_FORMAT)

if(NOT OPENTS_MOVIE_FORMAT MATCHES "^(VQA|MP4)$")
    message(FATAL_ERROR "OPENTS_MOVIE_FORMAT must be VQA or MP4")
endif()

if(OPENTS_MOVIE_FORMAT STREQUAL "MP4" AND NOT EMSCRIPTEN)
    message(FATAL_ERROR "OPENTS_MOVIE_FORMAT=MP4 is available only for the WebAssembly target")
endif()

# Visual Studio 2022 is the supported toolchain. Emscripten configures the WebAssembly
# target, whose state docs/BUILDING.md records.
if(NOT EMSCRIPTEN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
       AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        if(NOT OPENTS_EXPERIMENTAL_CLANG_CL)
            message(FATAL_ERROR
                "The unsupported clang-cl experiment must be configured with "
                "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang-cl-msvc.cmake.")
        endif()
    elseif(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # The native macOS target is in progress and unsupported; docs/BUILDING.md records
        # its state.
        set(OPENTS_MACOS ON)
    elseif(NOT MSVC OR MSVC_VERSION LESS 1930)
        message(FATAL_ERROR
            "OpenTS requires the Visual Studio 2022 MSVC toolchain, or the Emscripten "
            "toolchain for the in-progress WebAssembly target. "
            "For the unsupported clang-cl experiment, configure with "
            "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang-cl-msvc.cmake.")
    endif()
endif()

# Only the Windows toolchains have a resource compiler; the other targets reach the
# language resources another way.
if(NOT EMSCRIPTEN AND NOT OPENTS_MACOS)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TS_RUN_DIR "${CMAKE_SOURCE_DIR}/Run" CACHE PATH "Tiberian Sun run directory")

#
# ---------------------------------------------------------
# Build identity stamp
# ---------------------------------------------------------
#
# The version resources, the version displays, and the debug log's opening banner all report
# the version and commit that produced the binary. Stamping runs once here so the headers exist
# before the first compile, and again per build through OpenTSBuildStamp so that committing does
# not require a reconfigure to be reflected.
set(OPENTS_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(OPENTS_VERSION_HEADER "${OPENTS_GENERATED_DIR}/opents_version.h")
set(OPENTS_STAMP_HEADER "${OPENTS_GENERATED_DIR}/opents_build.h")
file(MAKE_DIRECTORY "${OPENTS_GENERATED_DIR}")

set(OPENTS_STAMP_ARGS
    "-DOPENTS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
    "-DOPENTS_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
    "-DOPENTS_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
    "-DOPENTS_VERSION_PRERELEASE=${OPENTS_VERSION_PRERELEASE}"
    "-DOPENTS_OFFICIAL_BUILD=${OPENTS_OFFICIAL_BUILD}"
    "-DOPENTS_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DOPENTS_VERSION_HEADER=${OPENTS_VERSION_HEADER}"
    "-DOPENTS_STAMP_HEADER=${OPENTS_STAMP_HEADER}"
    -P "${CMAKE_SOURCE_DIR}/cmake/GitStamp.cmake"
)

execute_process(COMMAND ${CMAKE_COMMAND} ${OPENTS_STAMP_ARGS})

add_custom_target(OpenTSBuildStamp ALL
    COMMAND ${CMAKE_COMMAND} ${OPENTS_STAMP_ARGS}
    COMMENT "Stamping the build identity"
    VERBATIM
)

# The Win32 substitute stands in for the Windows SDK and the MSVC C runtime where they are
# absent; every include site selects it through this definition.
if(EMSCRIPTEN OR OPENTS_MACOS)
    set(OPENTS_WIN32_SUBSTITUTE ON)
    add_compile_definitions(OPENTS_WIN32_SUBSTITUTE)
endif()

# The page host in code/browser.cpp answers code/browser.h under Emscripten, which puts
# the executable back into the default build.
if(EMSCRIPTEN)
    set(OPENTS_HOST page)
endif()

add_subdirectory(thirdparty)
add_subdirectory(code)
add_subdirectory(wasm)

enable_testing()

# The localized strings ship in a Win32 resource DLL, which the WebAssembly target has no way
# to build or load. It compiles the same resource script into the directory the executable
# carries instead; code/CMakeLists.txt does that.
if(NOT OPENTS_WIN32_SUBSTITUTE)
    add_subdirectory(code/language)
endif()

add_subdirectory(tests)
