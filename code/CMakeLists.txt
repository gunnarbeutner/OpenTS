# Limit the generated solution to supported configurations.
set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "" FORCE)

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
    if(OPENTS_POSIX AND f MATCHES "\\.(rc|asm)$")
        set(EXCLUDED TRUE)
    endif()

    # The Windows UI input adapter is fed by the window procedure; another target's host
    # feeds the shell itself.
    # buffer rather than dispatching a window message, so the hook it offers is what feeds
    # the shell there, and the window procedure's hook is what feeds it on Windows.
    if(OPENTS_POSIX AND f MATCHES "/ui/uiwin32\\.cpp$")
        set(EXCLUDED TRUE)
    endif()

    if(NOT EMSCRIPTEN AND f MATCHES "/ui/uibrowser\\.cpp$")
        set(EXCLUDED TRUE)
    endif()

    # The platform layer is built once, as a library the engine and the harnesses link.
    if(f MATCHES "/platform/[^/]*\\.cpp$")
        set(EXCLUDED TRUE)
    endif()

    # The POSIX file layer reads mounted images through the block source, so on those
    # targets it goes into the platform library; its headers stay beside the engine's
    # because cdfile.h includes them on every toolchain.
    if(OPENTS_POSIX AND f MATCHES "/blocksource\\.cpp$")
        set(EXCLUDED TRUE)
    endif()

    if(NOT EXCLUDED)
        list(APPEND OPENTS_SRC "${f}")
    endif()
endforeach()

#
# ---------------------------------------------------------
# Create actual OpenTS executable (Win32)
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Creating '${PROJECT_NAME}' executable...")

# The WIN32 marker selects the Windows GUI entry point and means nothing elsewhere. A POSIX
# target links only with a host that answers code/hostwindow.h, which a host branch
# supplies and names by setting OPENTS_HOST; without one the executable is left out of the
# default build and the platform library and the harnesses still build.
if(OPENTS_POSIX AND NOT OPENTS_HOST)
    add_executable(OpenTS EXCLUDE_FROM_ALL ${OPENTS_SRC})
elseif(OPENTS_POSIX)
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
    VS_DEBUGGER_COMMAND "$<TARGET_FILE:OpenTS>"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:OpenTS>"
    VS_DEBUGGER_COMMAND_ARGUMENTS "-DATADIR=${OPENTS_GAME_DIR}"
)

#
# ---------------------------------------------------------
# Compile flags
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Applying compiler flags...")

if(OPENTS_POSIX)

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
# The VQA player carries its own project, and hands OpenTS its include path along with the
# library, so the movie code is reached the same way from anywhere in the solution.
add_subdirectory(vqalib)

message(STATUS "${PROJECT_NAME}: Adding include paths...")
target_include_directories(OpenTS PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OPENTS_GENERATED_DIR}"
)

# The generated build stamp has to exist before anything compiles.
add_dependencies(OpenTS OpenTSBuildStamp)

# The platform layer: files, directory searches, free space and file times, with a Windows
# and a POSIX implementation that each guard themselves. It is compiled with the engine's
# own options.
file(GLOB OPENTS_PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp")
add_library(OpenTSPlatform STATIC ${OPENTS_PLATFORM_SOURCES})
if(OPENTS_POSIX)
    target_sources(OpenTSPlatform PRIVATE blocksource.cpp)
endif()
target_compile_features(OpenTSPlatform PUBLIC cxx_std_20)
target_include_directories(OpenTSPlatform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(OpenTSPlatform PRIVATE ${OPENTS_COMPILE_OPTIONS})
target_compile_definitions(OpenTSPlatform PRIVATE NOMINMAX)
target_link_libraries(OpenTS PRIVATE OpenTSPlatform)

# The tactical caption draws with FreeType directly, as well as through RmlUi.
target_link_libraries(OpenTS PRIVATE freetype)

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

# The UI shell is the only part of the engine that sees RmlUi, so the library's headers go
# on those files rather than on the whole target.
file(GLOB OPENTS_UI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp")
set_source_files_properties(${OPENTS_UI_SOURCES} PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/thirdparty/RmlUi/Include;${OPENTS_GENERATED_DIR}"
)

# The shell reads the generated string-name table, so the collector runs before it compiles.
add_dependencies(OpenTS OpenTSStringNames)

# The shell's renderer is the second translation unit that sees bgfx, so it takes the same
# headers, shaders, and build settings that bgfxbackend.cpp does.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/ui/uirender.cpp" PROPERTIES
    INCLUDE_DIRECTORIES
        "${CMAKE_SOURCE_DIR}/thirdparty/RmlUi/Include;${BGFX_ROOT}/include;${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bx/include;${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bimg/include;${BGFX_ROOT}/examples/common/imgui"
    COMPILE_DEFINITIONS
        "BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,$<BOOL:${BX_CONFIG_DEBUG}>>"
)

if(MSVC)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ui/uirender.cpp" APPEND PROPERTY
        COMPILE_OPTIONS "/Zc:preprocessor"
    )
endif()

if(OPENTS_EXPERIMENTAL_CLANG_CL AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ui/uirender.cpp" APPEND PROPERTY
        COMPILE_OPTIONS "/FI${CMAKE_SOURCE_DIR}/thirdparty/bx-clang-compat.h"
    )
endif()

#
# ---------------------------------------------------------
# Shipped UI files
# ---------------------------------------------------------
#
# The documents, styles, and fonts the UI shell reads. They are looked up by basename
# through CCFileClass, so the directory is a packaging convenience: startup registers it as
# a search folder, and a mod overrides one of these by placing a file earlier in that order
# or by shipping it in a mix.
set(OPENTS_UI_DATA_DIR "${CMAKE_SOURCE_DIR}/ui")

if(EMSCRIPTEN)
    # A page has no directory beside the executable to copy into, and these files are not in
    # the asset manifest, which carries the archives and films of a data release. Embedding
    # puts them in the module's own file system, where the working directory is the root.
    target_link_options(OpenTS PRIVATE "SHELL:--embed-file ${OPENTS_UI_DATA_DIR}@/ui")

    # The embedded files are link inputs that the link line does not name one at a time, so
    # editing a document would otherwise leave the module holding the old copy.
    file(GLOB OPENTS_UI_DATA_FILES CONFIGURE_DEPENDS "${OPENTS_UI_DATA_DIR}/*")
    set_property(TARGET OpenTS APPEND PROPERTY LINK_DEPENDS "${OPENTS_UI_DATA_FILES}")
else()
    add_custom_command(TARGET OpenTS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENTS_UI_DATA_DIR}" "$<TARGET_FILE_DIR:OpenTS>/ui"
        COMMENT "Copying the UI files beside the executable"
    )
endif()

message(STATUS "${PROJECT_NAME}: Adding compilier definitions...")
target_compile_definitions(OpenTS PRIVATE
    $<$<BOOL:${WIN32}>:WIN32>
    $<$<BOOL:${WIN32}>:_WINDOWS>
    NOMINMAX
)

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
    VQALib

    $<TARGET_FILE:bgfx>
    $<TARGET_FILE:bx>
    $<TARGET_FILE:bimg_decode>
    $<TARGET_FILE:bimg>
    miniaudio
    lzo
    rmlui
)

# bimg's decoder is built for the texture tools the engine does not ship, so it is excluded
# from the default build. The UI shell decodes document images with it, which brings it back.
set_target_properties(bimg_decode PROPERTIES EXCLUDE_FROM_ALL FALSE)
add_dependencies(OpenTS bimg_decode)


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
        winmm
        wsock32
        kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32
        odbc32 odbccp32
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

        # FreeType's error path longjmps, and it is compiled for the wasm form of it, so
        # the link has to provide that form rather than the JavaScript one.
        -sSUPPORT_LONGJMP=wasm
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

    # The engine opens its archives out of the directory it runs in, through the POSIX half
    # of the file layer in code/platform. Under node the directory that reaches is the
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
        # Raises the user address space of a 32-bit process from 2GB to 4GB under 64-bit
        # Windows. Pointers may then have the high bit set, so anything holding one in a
        # signed integer must only round-trip the bits and never compare or scale them. A
        # 64-bit image carries the flag already.
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
