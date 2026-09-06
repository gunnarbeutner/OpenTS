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

# Lets a target's FOLDER place it in a Visual Studio solution folder. The bgfx submodule sets
# this as well, so declare it here rather than inherit it from a vendored build.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Visual Studio 2022 is the supported toolchain.
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
elseif(MSVC)
    if(MSVC_VERSION LESS 1930)
        message(FATAL_ERROR "OpenTS requires MSVC 19.30 or newer.")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR
            "OpenTS targets Win32 and x64. Reconfigure with -A Win32 or -A x64.")
    endif()
else()
    message(FATAL_ERROR
        "OpenTS requires the Visual Studio 2022 MSVC toolchain. "
        "For the unsupported clang-cl experiment, configure with "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang-cl-msvc.cmake.")
endif()

# CMAKE_CXX_COMPILER_ARCHITECTURE_ID is an MSVC answer; every other toolchain leaves it empty.
string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" OPENTS_ARCH)
if(NOT OPENTS_ARCH)
    if(EMSCRIPTEN)
        set(OPENTS_ARCH "wasm32")
    elseif(CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES MATCHES ";")
        string(TOLOWER "${CMAKE_OSX_ARCHITECTURES}" OPENTS_ARCH)
    elseif(CMAKE_SYSTEM_PROCESSOR)
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" OPENTS_ARCH)
    else()
        set(OPENTS_ARCH "unknown")
    endif()
endif()

# Only the Windows toolchains have a resource compiler.
if(NOT OPENTS_MACOS)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENTS_GAME_DIR "${CMAKE_SOURCE_DIR}/Run" CACHE PATH "Tiberian Sun game data directory")

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
    "-DOPENTS_ARCH=${OPENTS_ARCH}"
    -P "${CMAKE_SOURCE_DIR}/cmake/GitStamp.cmake"
)

execute_process(COMMAND ${CMAKE_COMMAND} ${OPENTS_STAMP_ARGS})

add_custom_target(OpenTSBuildStamp ALL
    COMMAND ${CMAKE_COMMAND} ${OPENTS_STAMP_ARGS}
    COMMENT "Stamping the build identity"
    VERBATIM
)

# The names a UI document may use for a string. Generated from the language header, which is
# where the identifiers are declared, so no list of them is kept by hand. It follows that
# header rather than HEAD, so it runs at configure time and again when the header changes.
set(OPENTS_STRING_NAME_HEADER "${OPENTS_GENERATED_DIR}/opents_stringnames.h")
set(OPENTS_LANGUAGE_HEADER "${CMAKE_SOURCE_DIR}/code/language/language.h")

set(OPENTS_STRING_NAME_ARGS
    "-DOPENTS_LANGUAGE_HEADER=${OPENTS_LANGUAGE_HEADER}"
    "-DOPENTS_STRING_NAME_HEADER=${OPENTS_STRING_NAME_HEADER}"
    -P "${CMAKE_SOURCE_DIR}/cmake/StringNames.cmake"
)

execute_process(COMMAND ${CMAKE_COMMAND} ${OPENTS_STRING_NAME_ARGS})

add_custom_command(
    OUTPUT "${OPENTS_STRING_NAME_HEADER}"
    COMMAND ${CMAKE_COMMAND} ${OPENTS_STRING_NAME_ARGS}
    DEPENDS "${OPENTS_LANGUAGE_HEADER}" "${CMAKE_SOURCE_DIR}/cmake/StringNames.cmake"
    COMMENT "Collecting the string identifier names"
    VERBATIM
)

add_custom_target(OpenTSStringNames DEPENDS "${OPENTS_STRING_NAME_HEADER}")

# macOS builds with clang against POSIX rather than the Windows SDK.
if(OPENTS_MACOS)
    set(OPENTS_POSIX ON)
endif()

# The SDL host in code/sdlhost.cpp answers code/hostwindow.h on macOS, which puts the
# executable back into the default build.
if(OPENTS_MACOS)
    set(OPENTS_HOST sdl)
endif()

add_subdirectory(thirdparty)
add_subdirectory(code)

enable_testing()

add_subdirectory(tests)
