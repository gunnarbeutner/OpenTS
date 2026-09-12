# The harness drives the engine's file layer in code/platform, whichever implementation the
# target compiles: the Win32 one under MSVC and the POSIX one elsewhere. It lives outside
# code/ so that the recursive glob building OpenTS cannot pick its entry point up.

opents_add_test(PlatformFileTest
    NAME platformfile
    SOURCES platformfiletest.cpp
    LIBRARIES OpenTSPlatform
)

if(OPENTS_POSIX)
    target_compile_options(PlatformFileTest PRIVATE -fexceptions)
    target_link_options(PlatformFileTest PRIVATE -fexceptions)
endif()

# The harness writes the files it reads back, so it is given a directory of its own.
set_tests_properties(platformfile PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
