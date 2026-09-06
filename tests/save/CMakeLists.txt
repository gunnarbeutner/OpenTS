# The harness drives the file a saved game is kept in: savefile.cpp's writer and reader over
# the field table, the compressed content, and every refusal the reader makes. The LZO codec
# it compresses with is the engine's own.
add_executable(SaveTest
    "${CMAKE_CURRENT_SOURCE_DIR}/savetest.cpp"
    "${CMAKE_SOURCE_DIR}/code/savefile.cpp"
    "${CMAKE_SOURCE_DIR}/code/lzo1x_c.cpp"
    "${CMAKE_SOURCE_DIR}/code/lzo1x_d.cpp"
)

target_compile_features(SaveTest PRIVATE cxx_std_20)

target_include_directories(SaveTest PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
)

if(MSVC)
    target_compile_definitions(SaveTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(SaveTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )

    target_link_libraries(SaveTest PRIVATE kernel32)
endif()

if(OPENTS_WIN32_SUBSTITUTE)
    target_link_libraries(SaveTest PRIVATE Win32Substitute)
endif()

set_target_properties(SaveTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(OPENTS_WIN32_SUBSTITUTE)
    # The substitute's structures are pinned to their Win32 x86 widths, so the harness needs
    # the same narrow wchar_t and MSVC keyword handling the engine is built with.
    target_compile_options(SaveTest PRIVATE
        -fshort-wchar
        -fno-builtin-wcslen
        -fno-builtin-wcsnlen
        -fexceptions
        -fdeclspec
    )

    # The harness writes the files it reads back, so it needs a real filesystem rather than
    # the in-memory one Emscripten supplies by default.
    target_link_options(SaveTest PRIVATE -fexceptions $<$<BOOL:${EMSCRIPTEN}>:-sNODERAWFS=1;-sALLOW_MEMORY_GROWTH=1>)
endif()

# The harness writes the files it reads back, so it is given a directory of its own.
add_test(NAME save COMMAND SaveTest "${CMAKE_CURRENT_BINARY_DIR}")
