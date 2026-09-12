#
# ---------------------------------------------------------
# WebAssembly renderer demo
# ---------------------------------------------------------
#
# A self-contained Emscripten target that links the renderer seam and nothing else, so
# that the two structural unknowns of the port -- presenting onto a canvas and running a
# loop the browser drives -- can be answered before the engine itself links. It is not
# part of the game and nothing in the engine depends on it. docs/BUILDING.md records the
# WebAssembly target as in progress and unsupported; this target is inside that.
#
if(NOT EMSCRIPTEN)
    return()
endif()

# The repository root is derived rather than taken from CMAKE_SOURCE_DIR, so this
# directory can be added from any enclosing project.
get_filename_component(OPENTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(BGFX_ROOT "${OPENTS_ROOT}/thirdparty/bgfx.cmake/bgfx")

add_executable(OpenTSWasmDemo
    "${CMAKE_CURRENT_SOURCE_DIR}/demo.cpp"
    "${OPENTS_ROOT}/code/bgfxbackend.cpp"
)

target_compile_features(OpenTSWasmDemo PRIVATE cxx_std_20)

target_include_directories(OpenTSWasmDemo PRIVATE "${OPENTS_ROOT}/code")

# The same discipline the engine's own target keeps: bgfx's headers and the shaders it
# embeds go on the renderer's translation unit rather than on the target, so nothing else
# can reach a bgfx type. Source file properties are scoped to the directory that sets
# them, so this does not disturb the engine target's copy of the same file.
set_source_files_properties("${OPENTS_ROOT}/code/bgfxbackend.cpp"
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    PROPERTIES INCLUDE_DIRECTORIES
        "${BGFX_ROOT}/include;${OPENTS_ROOT}/thirdparty/bgfx.cmake/bx/include;${BGFX_ROOT}/examples/common/imgui"
    COMPILE_DEFINITIONS
        "BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,$<BOOL:${BX_CONFIG_DEBUG}>>"
)

add_dependencies(OpenTSWasmDemo bgfx bimg bx)
target_link_libraries(OpenTSWasmDemo PRIVATE
    $<TARGET_FILE:bgfx>
    $<TARGET_FILE:bimg>
    $<TARGET_FILE:bx>
)

set(OPENTS_WASM_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")

target_link_options(OpenTSWasmDemo PRIVATE
    # bgfx's OpenGL ES 3 renderer reaches the page through WebGL 2 alone.
    -sMIN_WEBGL_VERSION=2
    -sMAX_WEBGL_VERSION=2

    # bgfx resolves its GL entry points itself rather than through the linked stubs.
    -sGL_ENABLE_GET_PROC_ADDRESS=1

    # The frame buffer and bgfx's own allocations are sized at startup rather than
    # against a fixed heap.
    -sALLOW_MEMORY_GROWTH=1

    # main returns to the browser and the loop callback keeps running afterwards.
    -sEXIT_RUNTIME=0

    "--shell-file" "${OPENTS_WASM_SHELL}"
)

set_target_properties(OpenTSWasmDemo PROPERTIES
    OUTPUT_NAME "opents-wasm-demo"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/wasm"
    LINK_DEPENDS "${OPENTS_WASM_SHELL}"
)

#
# ---------------------------------------------------------
# The page the engine runs inside
# ---------------------------------------------------------
#
# The engine's own target is built as a plain module rather than through Emscripten's shell
# templating, so that the same GameD.js runs under node. The page that hosts it is written
# by hand and lands beside the module; it names the canvas that video.cpp and browser.cpp
# both look for.
#
# The page is three files: the markup names the style and the script, and each is served
# on its own so the two that carry the weight can be named for their content and cached
# forever, leaving only the small document that names them to be revalidated.
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/bin/index.html"
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/game.html"
)

file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/bin/page.css"
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/page.css"
)

# INPUT rather than COPYONLY because the script names the engine module through a
# generator expression, which only file(GENERATE) evaluates.
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/bin/page.js"
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/page.js"
)

# Lands beside the page because it belongs to whatever serves the page, and a deployment
# that copies the one without the other would be answering crawlers by accident.
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/bin/robots.txt"
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/robots.txt"
)

# The two icons a browser asks for by a fixed name at the root whatever the page declares.
# wasm/build-icons.sh rebuilds them from the project mark; they are copied rather than
# generated because they are not text.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/favicon.ico"
    "${CMAKE_BINARY_DIR}/bin/favicon.ico" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/apple-touch-icon.png"
    "${CMAKE_BINARY_DIR}/bin/apple-touch-icon.png" COPYONLY)

# What an installed copy draws its tile from, and the worker that lets it start with no
# network. The worker is served from the root because that is the scope it claims.
foreach(shell_file manifest.webmanifest sw.js icon-192.png icon-512.png)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${shell_file}"
        "${CMAKE_BINARY_DIR}/bin/${shell_file}" COPYONLY)
endforeach()
