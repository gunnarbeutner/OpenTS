# Limit the generated solution to supported configurations.
set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "" FORCE)

# OpenTS builds against 32-bit pointers, which Win32 x86 and Emscripten wasm32 both provide
# along with the 32-bit long the engine's layouts assume. The macOS target is the one
# LP64 host, and the substitute pins the Windows widths there.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT OPENTS_MACOS)
    message(FATAL_ERROR
        "OpenTS must be built with 32-bit pointers. Reconfigure with -A Win32, or with the "
        "Emscripten toolchain file for the WebAssembly target.")
endif()

#
# ---------------------------------------------------------
# Directories that build as their own project
# ---------------------------------------------------------
#
set(SUBPROJECT_DIRS
    "language"
    "vqalib"
)

#
# ---------------------------------------------------------
# Gather all potential OpenTS files
# ---------------------------------------------------------
#
file(GLOB_RECURSE OPENTS_RAW CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hh"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rc"
)

#
# ---------------------------------------------------------
# Remove files that belong to a subproject
# ---------------------------------------------------------
#
set(OPENTS_SRC "")
foreach(f ${OPENTS_RAW})
    set(EXCLUDED FALSE)
    foreach(dir ${SUBPROJECT_DIRS})
        if(f MATCHES "^${CMAKE_CURRENT_SOURCE_DIR}/${dir}/.*")
            set(EXCLUDED TRUE)
        endif()
    endforeach()

    # Resource and assembly files are Visual Studio toolchain inputs with no equivalent on
    # the other targets.
    if(OPENTS_WIN32_SUBSTITUTE AND f MATCHES "\\.(rc|asm)$")
        set(EXCLUDED TRUE)
    endif()

    # The VQA player's own audio handler goes with the player it is written against.
    if(OPENTS_MOVIE_FORMAT STREQUAL "MP4" AND f MATCHES "/(audiomovie|unvqtblc|vqa)\\.cpp$")
        set(EXCLUDED TRUE)
    endif()

    if(OPENTS_MOVIE_FORMAT STREQUAL "VQA" AND f MATCHES "/mp4\\.cpp$")
        set(EXCLUDED TRUE)
    endif()

    # The Win32 substitute is built once, as a library the engine and the harnesses link
    # on the toolchains that need it; MSVC has the SDK and links none of it.
    if(f MATCHES "/win32compat/")
        set(EXCLUDED TRUE)
    endif()

    if(NOT EXCLUDED)
        list(APPEND OPENTS_SRC "${f}")
    endif()
endforeach()

#
# ---------------------------------------------------------
# Language resources
# ---------------------------------------------------------
#
# The Visual Studio target builds language/language.rc into Language.dll and asks the module
# loader for the strings and dialog templates in it. The WebAssembly target has no module
# loader, so the same script is compiled into the resource directory the executable carries
# with it. The script stays the one source of the resources either way.
#
if(OPENTS_WIN32_SUBSTITUTE)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    set(OPENTS_LANGUAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/language")
    set(OPENTS_LANGUAGE_SOURCE "${OPENTS_GENERATED_DIR}/languageimage.cpp")

    add_custom_command(
        OUTPUT "${OPENTS_LANGUAGE_SOURCE}"
        COMMAND "${Python3_EXECUTABLE}"
            "${OPENTS_LANGUAGE_DIR}/rcimage.py"
            "${OPENTS_LANGUAGE_DIR}/language.rc"
            --include "${OPENTS_LANGUAGE_DIR}"
            --include "${OPENTS_GENERATED_DIR}"
            --source "${OPENTS_LANGUAGE_SOURCE}"
        DEPENDS
            "${OPENTS_LANGUAGE_DIR}/rcimage.py"
            "${OPENTS_LANGUAGE_DIR}/language.rc"
            "${OPENTS_LANGUAGE_DIR}/language.h"
            "${OPENTS_LANGUAGE_DIR}/language.hm"
            "${OPENTS_VERSION_HEADER}"
            "${OPENTS_STAMP_HEADER}"
        COMMENT "Compiling the language resources"
        VERBATIM
    )

    list(APPEND OPENTS_SRC "${OPENTS_LANGUAGE_SOURCE}")
endif()

#
# ---------------------------------------------------------
# Create actual OpenTS executable (Win32)
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Creating '${PROJECT_NAME}' executable...")

# The WIN32 marker selects the Windows GUI entry point and means nothing elsewhere. A
# substitute target links only with a host that answers the calls in code/browser.h, which
# a host branch supplies and names by setting OPENTS_HOST; without one the executable is
# left out of the default build and the substitute library and its harnesses still build.
if(OPENTS_WIN32_SUBSTITUTE AND NOT OPENTS_HOST)
    add_executable(OpenTS EXCLUDE_FROM_ALL ${OPENTS_SRC})
elseif(OPENTS_WIN32_SUBSTITUTE)
    add_executable(OpenTS ${OPENTS_SRC})
else()
    add_executable(OpenTS WIN32 ${OPENTS_SRC})
endif()

target_compile_features(OpenTS PRIVATE cxx_std_20)

#
# ---------------------------------------------------------
# Output paths
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Setting binary output paths...")

set_target_properties(OpenTS PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"

    # Built under the shipped names, so the executable, the symbol file the debug record
    # names, and the map reach the run directory without a rename.
    OUTPUT_NAME_DEBUG "GameD"
    OUTPUT_NAME_RELEASE "Game"
    PDB_NAME_DEBUG "GameD"
    PDB_NAME_RELEASE "Game"
)

set_target_properties(OpenTS PROPERTIES
    VS_DEBUGGER_COMMAND "${TS_RUN_DIR}/$<TARGET_FILE_NAME:OpenTS>"
    VS_DEBUGGER_WORKING_DIRECTORY "${TS_RUN_DIR}"
)

#
# ---------------------------------------------------------
# Compile flags
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Applying compiler flags...")

if(OPENTS_WIN32_SUBSTITUTE)

set(OPENTS_COMPILE_OPTIONS

    # WebAssembly arithmetic is strict IEEE-754: single precision stays single precision and
    # nothing is reassociated, which is what /arch:SSE2 and /fp:precise buy under MSVC. So the
    # floating-point model needs nothing asked for here, but no fast-math option may ever be
    # added: the simulation runs in lockstep across a network game and any relaxation
    # desynchronizes it.
    -fno-fast-math

    # MSVC does not optimize on the strict aliasing rule and the inherited code puns types
    # freely, so clang is held to the assumption the code was written under.
    -fno-strict-aliasing

    # AbstractClass offers twenty-four As_*Class helpers, each a dynamic_cast of `this`, and
    # the engine calls them on pointers that may be null. clang infers that a member's `this`
    # is nonnull and dereferenceable and so deletes the null test dynamic_cast is required to
    # make -- inline or out of line, since the inference is about `this` rather than about
    # inlining. The cast then reads a vtable at address zero, which is addressable memory in
    # WebAssembly, and traps. MSVC keeps the test, so this belongs to the clang target alone.
    -fno-delete-null-pointer-checks

    # The engine's wchar_t is 16 bits and the library's is 32, so no libc wide-string
    # routine may be called. That is not enough on its own: at -O1 the loop-idiom pass
    # recognizes a hand-written wide length scan and rewrites it back into a call to
    # wcslen, which then counts the wrong character width. Two such helpers, written
    # precisely to avoid the library, were compiled into calls to it.
    -fno-builtin-wcslen
    -fno-builtin-wcsnlen

    # MSVC's wchar_t is 16 bits and clang's is 32. The engine's structures are
    # layout-sensitive, so the narrow form is kept.
    -fshort-wchar

    # C++ exception handling, matching /EHsc. Emscripten compiles throws to traps otherwise.
    #
    # The WebAssembly form of it rather than the JavaScript one, and that is not a
    # preference: -fexceptions routes every call that can unwind through an invoke_ import,
    # and Emscripten declares every invoke_ import suspending whenever JSPI is on. A static
    # initializer would then try to suspend before main -- outside the promising boundary --
    # and trap. -fwasm-exceptions keeps the unwinding in wasm, where it belongs, and leaves
    # nothing for JSPI to mistake for a yield.
    $<$<BOOL:${EMSCRIPTEN}>:-fwasm-exceptions>
    $<$<BOOL:${OPENTS_MACOS}>:-fexceptions>

    # The inherited declarations carry MSVC's __declspec, which clang parses only when asked.
    -fdeclspec

    -g
    -fno-omit-frame-pointer

    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Release>:-O2>
)

else()

set(OPENTS_COMPILE_OPTIONS

    # ================= DEBUG =================
    $<$<CONFIG:Debug>:

        # ---------- C++ ----------
        # /arch:SSE2 -- IEEE-754 single precision, no x87 excess precision.
        # /fp:precise -- no reassociation of the engine's accumulations.
        # /utf-8 -- sources and literals are UTF-8 whatever the build machine's code page.
        $<$<COMPILE_LANGUAGE:CXX>:
            /Zi /Od /RTC1 /GR $<$<CXX_COMPILER_ID:MSVC>:/MP> /EHsc /Oy- /MTd /Zc:__cplusplus /arch:SSE2 /fp:precise /utf-8
        >

        # ---------- C ----------
        $<$<COMPILE_LANGUAGE:C>:
            /Zi /Od /RTC1 $<$<C_COMPILER_ID:MSVC>:/MP> /Oy- /arch:SSE2 /fp:precise /utf-8
        >
    >

    # ================= RELEASE =================
    $<$<CONFIG:Release>:

        # ---------- C++ ----------
        # /arch:SSE2 -- IEEE-754 single precision, no x87 excess precision.
        # /fp:precise -- no reassociation of the engine's accumulations.
        # /utf-8 -- sources and literals are UTF-8 whatever the build machine's code page.
        $<$<COMPILE_LANGUAGE:CXX>:
            /Zi /O2 /GF /GR $<$<CXX_COMPILER_ID:MSVC>:/MP> /EHsc /MT /Zc:__cplusplus /arch:SSE2 /fp:precise /utf-8
        >

        # ---------- C ----------
        $<$<COMPILE_LANGUAGE:C>:
            /Zi /O2 /GF $<$<C_COMPILER_ID:MSVC>:/MP> /arch:SSE2 /fp:precise /utf-8
        >
    >
)

endif()

target_compile_options(OpenTS PRIVATE ${OPENTS_COMPILE_OPTIONS})

#
# ---------------------------------------------------------
# Subprojects
# ---------------------------------------------------------
#
# The VQA player carries its own project, and hands a VQA build its include path along with
# the library. An MP4 build has neither the player sources nor the library in its target.
if(OPENTS_MOVIE_FORMAT STREQUAL "VQA")
    add_subdirectory(vqalib)
endif()

message(STATUS "${PROJECT_NAME}: Adding include paths...")
target_include_directories(OpenTS PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OPENTS_GENERATED_DIR}"
)

# The generated build stamp has to exist before anything compiles.
add_dependencies(OpenTS OpenTSBuildStamp)

# The Win32 substitute: the file layer, the window manager, the stock controls, the GDI
# residue, the timers, and the resource reader. It is
# compiled with the engine's own options, once, and every harness links this one build of
# it. What it needs from the host (the Browser_* calls and the frame clamp) resolves in the
# executable that links it.
if(OPENTS_WIN32_SUBSTITUTE)
    add_library(Win32Substitute STATIC
        win32compat/peresource.cpp
        win32compat/win32compat.cpp
        win32compat/win32ctrl.cpp
        win32compat/win32disk.cpp
        win32compat/win32gdi.cpp
        win32compat/win32process.cpp
        win32compat/win32timer.cpp
        win32compat/win32user.cpp
        win32compat/win32window.cpp
    )
    target_compile_features(Win32Substitute PUBLIC cxx_std_20)
    # The substitute's headers carry the SDK's names, so a substitute target finds
    # <windows.h> and the MSVC runtime headers here and the include sites stay as
    # they are written for MSVC.
    target_include_directories(Win32Substitute
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/win32compat"
        PRIVATE "${OPENTS_GENERATED_DIR}"
    )
    target_compile_options(Win32Substitute PRIVATE ${OPENTS_COMPILE_OPTIONS})
    target_compile_definitions(Win32Substitute PRIVATE WIN32 _WINDOWS _MBCS NOMINMAX)
    target_link_libraries(OpenTS PRIVATE Win32Substitute)
endif()

# Only the renderer's own translation unit sees bgfx, so its headers and the shaders it
# embeds are put on that file rather than on the whole target.
set(BGFX_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/bgfxbackend.cpp" PROPERTIES
    INCLUDE_DIRECTORIES
        "${BGFX_ROOT}/include;${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bx/include;${BGFX_ROOT}/examples/common/imgui"
    COMPILE_DEFINITIONS
        "BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,$<BOOL:${BX_CONFIG_DEBUG}>>"
)

if(MSVC)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/bgfxbackend.cpp" APPEND PROPERTY
        COMPILE_OPTIONS "/Zc:preprocessor"
    )
endif()

# bx rewrites __stdcall while its headers are being parsed by clang-cl.  Force the
# compatibility header into the renderer translation unit as well as bx/bgfx so the
# MSVC standard-library headers that follow still see the Win32 calling convention.
if(OPENTS_EXPERIMENTAL_CLANG_CL AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/bgfxbackend.cpp" APPEND PROPERTY
        COMPILE_OPTIONS "/FI${CMAKE_SOURCE_DIR}/thirdparty/bx-clang-compat.h"
    )
endif()

message(STATUS "${PROJECT_NAME}: Adding compilier definitions...")
target_compile_definitions(OpenTS PRIVATE
    WIN32
    _WINDOWS
    NOMINMAX
)

if(OPENTS_MOVIE_FORMAT STREQUAL "MP4")
    target_compile_definitions(OpenTS PRIVATE OPENTS_MP4_MOVIES)
endif()

#
# ---------------------------------------------------------
# Windows libraries and linker options
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Applying linker options...")

# Required libraries:
#  Comctl32.lib
#  DbgHelp.lib
#  DSound.lib
#  Version.lib
#  Winmm.lib
#  wsock32.lib
#  + all Win32 libs
#

target_link_libraries(OpenTS PRIVATE
    $<TARGET_FILE:bgfx>
    $<TARGET_FILE:bx>
    $<TARGET_FILE:bimg>
    miniaudio
)

if(OPENTS_MOVIE_FORMAT STREQUAL "VQA")
    target_link_libraries(OpenTS PRIVATE VQALib)
endif()

# The renderer is linked as a file rather than a target, so the frameworks bgfx reaches on
# macOS are named here.
if(OPENTS_MACOS)
    target_link_libraries(OpenTS PRIVATE
        "-framework Cocoa" "-framework Metal" "-framework QuartzCore" "-framework IOKit"
        "-framework CoreFoundation" "-framework CoreVideo" "-framework CoreMedia"
        "-framework VideoToolbox" "-framework Foundation"
    )
endif()

if (WIN32)
    target_link_libraries(OpenTS PRIVATE
        comctl32
        dbghelp
        iphlpapi
        psapi
        version
        winmm
        wsock32
        kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32
        ole32 oleaut32 uuid odbc32 odbccp32
    )
endif()

if (EMSCRIPTEN)
    target_link_options(OpenTS PRIVATE
        # bgfx's OpenGL ES 3 renderer reaches the page through WebGL 2 alone.
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2

        # The engine sizes its own allocations at startup rather than against a fixed heap.
        -sALLOW_MEMORY_GROWTH=1

        # main returns to the page while the engine is still running inside it, so the
        # runtime has to outlive the return.
        -sEXIT_RUNTIME=0

        # A saved game has to outlive the tab it was written in, so the directory the engine
        # saves into is mounted on IndexedDB. Only that directory is: the game data is
        # already on the page and must not be copied into a browser's database.
        -lidbfs.js

        # The page mounts that directory and reads it back before the engine starts, which
        # means the page rather than the runtime decides when main is called.
        # UTF8ToString reads the strings the engine's own reporting exports return.
        -sEXPORTED_RUNTIME_METHODS=FS,callMain,UTF8ToString

        -fwasm-exceptions
    )

    #
    # ---------------------------------------------------------
    # The yield scaffold
    # ---------------------------------------------------------
    #
    # A page owns the thread the engine borrows, and the engine has around sixty waits that
    # do not return. A wait therefore has to suspend the engine's own stack and resume it
    # later, which is what lets a browser build run before those waits are flattened.
    # docs/WASM-PORT.md section 1 records the scaffold as deliberately temporary: it keeps the
    # reentrancy the project means to remove, and Browser_Blocking_Wait_Count is the number
    # that has to reach zero before it can go.
    #
    #   JSPI      JavaScript Promise Integration. The virtual machine suspends a real
    #             WebAssembly stack, so nothing is instrumented and nothing is paid for at
    #             run time. It is not in a released Safari.
    #   ASYNCIFY  Binaryen rewrites the module so an instrumented function can unwind its
    #             locals to a side stack and rewind later. Plain WebAssembly, so it runs
    #             anywhere, and it is paid for in size and speed on every instrumented call.
    #   NONE      Nothing carries a wait. The engine keeps the thread, the page stops
    #             answering at the first one, and says so once. This is the destination:
    #             what a build configured here fails at is the work still to do.
    #
    set(OPENTS_WASM_SUSPEND "JSPI" CACHE STRING
        "How the engine's not-yet-flattened waits are carried: JSPI, ASYNCIFY, or NONE")
    set_property(CACHE OPENTS_WASM_SUSPEND PROPERTY STRINGS JSPI ASYNCIFY NONE)

    if(NOT OPENTS_WASM_SUSPEND MATCHES "^(JSPI|ASYNCIFY|NONE)$")
        message(FATAL_ERROR "OPENTS_WASM_SUSPEND must be JSPI, ASYNCIFY, or NONE")
    endif()

    if(OPENTS_MOVIE_FORMAT STREQUAL "MP4" AND OPENTS_WASM_SUSPEND STREQUAL "NONE")
        message(FATAL_ERROR "OPENTS_MOVIE_FORMAT=MP4 requires JSPI or ASYNCIFY")
    endif()

    #
    # Both mechanisms give the engine the same thing -- a wait that hands the thread back and
    # comes back to where it left -- and the source asks for no more than that, so one
    # definition marks either of them.
    #
    if(NOT OPENTS_WASM_SUSPEND STREQUAL "NONE")
        target_compile_definitions(OpenTS PRIVATE OPENTS_WASM_JSPI)
    endif()

    if(OPENTS_WASM_SUSPEND STREQUAL "JSPI")
        target_link_options(OpenTS PRIVATE -sJSPI)
    endif()

    if(OPENTS_WASM_SUSPEND STREQUAL "ASYNCIFY")
        target_link_options(OpenTS PRIVATE
            -sASYNCIFY

            # The rewrite reaches every function that can be on the stack at a wait, and in
            # this engine a wait is reached from render, logic, queue and dialog code alike,
            # so the transitive closure is effectively the whole engine. Binaryen's default
            # side stack is 4KB and the engine's stack at a wait is far deeper than that;
            # overflowing it aborts rather than degrading.
            -sASYNCIFY_STACK_SIZE=65536
        )
    endif()

    #
    # The two suspending artifacts differ only in how a wait is carried, so that is what they
    # are named for, and a page can then serve both out of one directory and load whichever
    # the browser can run. The plain name belongs to the mechanism a browser needs nothing
    # extra to run -- JSPI today, and NONE once the waits are gone.
    #
    # An Emscripten build is single configuration, so the name is settled here rather than
    # deferred, which is what lets the page name the artifact this build did not produce.
    #
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(OPENTS_WASM_MODULE_BASE "GameD")
    else()
        set(OPENTS_WASM_MODULE_BASE "Game")
    endif()

    set(OPENTS_WASM_MODULE_SUFFIX "")

    if(OPENTS_WASM_SUSPEND STREQUAL "ASYNCIFY")
        set(OPENTS_WASM_MODULE_SUFFIX "-asyncify")
    endif()

    set_target_properties(OpenTS PROPERTIES
        OUTPUT_NAME "${OPENTS_WASM_MODULE_BASE}${OPENTS_WASM_MODULE_SUFFIX}"
        OUTPUT_NAME_DEBUG "${OPENTS_WASM_MODULE_BASE}${OPENTS_WASM_MODULE_SUFFIX}"
        OUTPUT_NAME_RELEASE "${OPENTS_WASM_MODULE_BASE}${OPENTS_WASM_MODULE_SUFFIX}"

        # What the page looks for, which is both artifacts rather than the one this
        # configuration builds: either configuration generates the same page, so the two can
        # be built separately and served together.
        OPENTS_MODULE_BASE "${OPENTS_WASM_MODULE_BASE}"
    )

    # The engine opens its archives out of the directory it runs in, through the Win32 file
    # API that win32compat.cpp puts on POSIX. Under node the directory that reaches is the
    # host's own, which this hands the module directly, and that is the whole of the data
    # mounting story for the node target. The option exists because the flag is node-only --
    # a page built with it throws before main runs -- so a browser build turns it off and
    # brings its data in another way.
    option(OPENTS_WASM_NODERAWFS
        "Give the WebAssembly build the host filesystem, for running under node" ON)

    if(OPENTS_WASM_NODERAWFS)
        target_link_options(OpenTS PRIVATE -sNODERAWFS=1)
    endif()
endif()

if (MSVC)
    target_link_options(OpenTS PRIVATE
        /SUBSYSTEM:WINDOWS
        /DEBUG
        /MAP
        # Raises the user address space from 2GB to 4GB under 64-bit Windows. Pointers may
        # then have the high bit set, so anything holding one in a signed integer must only
        # round-trip the bits and never compare or scale them.
        /LARGEADDRESSAWARE
        # Declares the process per-monitor DPI aware, so a scaled desktop does not blur
        # the frame by stretching the whole window.
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/sun.manifest"
        /MANIFEST:EMBED
    )
endif()

#
# ---------------------------------------------------------
# Visual Studio source groups
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Setting up Visual Studio filters...")

# Init Source Files (_*.cpp)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "[/\\\\]_.*\\.cpp$")
        source_group("Source Files\\Init Source Files" FILES ${f})
    endif()
endforeach()

# Init Header Files (_*.h)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "[/\\\\]_.*\\.h$")
        source_group("Header Files\\Init Header Files" FILES ${f})
    endif()
endforeach()

# Define Header Files (*.hh)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.hh$")
        source_group("Header Files\\Define Header Files" FILES ${f})
    endif()
endforeach()

# Resource Files
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.(ico|cur|bmp|gif|jpe?g|rc)$")
        source_group("Resource Files" FILES ${f})
    endif()
endforeach()

# The interface headers the locomotors are written against.
source_group("Interface Files" FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/iflyctrl.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/iloco.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipiggy.h"
)

# General source files
source_group("Source Files" REGULAR_EXPRESSION ".*\\.(c|cpp)$")
source_group("Header Files" REGULAR_EXPRESSION ".*\\.(h|hpp)$")

#
# ---------------------------------------------------------
# Populate the local run directory
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Creating post-build commands...")
add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${TS_RUN_DIR}"
)

add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:OpenTS>"
        "${TS_RUN_DIR}"
)

# Emscripten splits the executable in two: a loader, which the copy above brings across, and
# the module it looks for beside itself. Without the module the run directory holds half a
# program.
if(EMSCRIPTEN)
    add_custom_command(TARGET OpenTS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:OpenTS>/$<TARGET_FILE_BASE_NAME:OpenTS>.wasm"
            "${TS_RUN_DIR}"
    )
endif()

# The symbol file and the map come from the MSVC linker.
if(MSVC)
    # Copy the linker-generated .pdb alongside the exe
    add_custom_command(TARGET OpenTS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PDB_FILE:OpenTS>"
            "${TS_RUN_DIR}"
    )

    # Copy the linker-generated .map (sits next to the exe, no GenEx for it)
    add_custom_command(TARGET OpenTS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:OpenTS>/$<TARGET_FILE_BASE_NAME:OpenTS>.map"
            "${TS_RUN_DIR}"
    )
endif()

#
# ---------------------------------------------------------
# Visual Studio startup project
# ---------------------------------------------------------
#
set_property(GLOBAL PROPERTY VS_STARTUP_PROJECT OpenTS)

# ---------------------------------------------------------
# Add the .natvis file for Visual Studio debugging
# ---------------------------------------------------------
if(MSVC)
    set(NATVIS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/sun.natvis")
    source_group("Natvis Files" FILES ${NATVIS_FILE})
    target_sources(OpenTS PRIVATE ${NATVIS_FILE})
    target_link_options(OpenTS PRIVATE "/NATVIS:${NATVIS_FILE}")
endif()
