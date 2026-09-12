# The harness drives the engine's file layer in code/platform, whichever implementation the
# target compiles: the Win32 one under MSVC and the POSIX one elsewhere. It lives outside
# code/ so that the recursive glob building OpenTS cannot pick its entry point up.

# The layer resolves a name the host has no file for through the manifest, so the reader it
# is handed comes with it.
set(PLATFORMFILE_TEST_ENGINE "")
if(EMSCRIPTEN)
    list(APPEND PLATFORMFILE_TEST_ENGINE httpsource.cpp manifest.cpp)
endif()

opents_add_test(PlatformFileTest
    NAME platformfile
    SOURCES platformfiletest.cpp
    ENGINE ${PLATFORMFILE_TEST_ENGINE}
    LIBRARIES OpenTSPlatform
)

if(OPENTS_POSIX)
    target_compile_options(PlatformFileTest PRIVATE -fexceptions)

    # The harness creates and reads back real files, so it needs a real filesystem rather
    # than the in-memory one Emscripten supplies by default.
    target_link_options(PlatformFileTest PRIVATE -fexceptions $<$<BOOL:${EMSCRIPTEN}>:-sNODERAWFS=1>)
endif()

# The harness writes the files it reads back, so it is given a directory of its own.
set_tests_properties(platformfile PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
