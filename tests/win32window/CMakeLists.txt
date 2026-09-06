# The harness calls the Win32 code page, window geometry, and pointer API and is checked
# against whichever implementation the target provides: the operating system's under MSVC,
# and the WebAssembly target's substitute for it under Emscripten. The substitute is
# compiled straight into the harness; its entry point lives outside code/ so that the
# recursive glob building OpenTS cannot pick it up.
set(WIN32WINDOW_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/win32windowtest.cpp"
)

# The canvas is checked on its own, against stubs of the rest of the substitute, so it is
# compiled here rather than taken from the library.
if(OPENTS_WIN32_SUBSTITUTE)
    list(APPEND WIN32WINDOW_TEST_SOURCES
        "${CMAKE_SOURCE_DIR}/code/win32compat/win32window.cpp"
    )
endif()

add_executable(Win32WindowTest ${WIN32WINDOW_TEST_SOURCES})

target_compile_features(Win32WindowTest PRIVATE cxx_std_20)

target_include_directories(Win32WindowTest PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
    "${CMAKE_SOURCE_DIR}/code/win32compat"
)

if(MSVC)
    target_compile_definitions(Win32WindowTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(Win32WindowTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )
endif()

set_target_properties(Win32WindowTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(OPENTS_WIN32_SUBSTITUTE)
    # The substitute's structures are pinned to their Win32 x86 widths, so the harness needs
    # the same narrow wchar_t and MSVC keyword handling the engine is built with. A wide
    # character that is not sixteen bits is a different test.
    target_compile_options(Win32WindowTest PRIVATE
        -fshort-wchar
        -fno-builtin-wcslen
        -fno-builtin-wcsnlen
        -fexceptions
        -fdeclspec
    )

    target_link_options(Win32WindowTest PRIVATE -fexceptions)
endif()

add_test(NAME win32window COMMAND Win32WindowTest)
