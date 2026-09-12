# The decoders are compiled straight into the harness. It lives outside code/ so that the
# recursive glob building OpenTS cannot pick this target's entry point up.

opents_add_test(UnvqDelta
    NAME unvqdelta
    SOURCES unvqdelta.cpp
    ENGINE
        vqalib/unvq.cpp
        unvqtblc.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/code/vqalib
    DEFINITIONS WIN32 _WINDOWS _MBCS
    FLOAT
)

# The decoders carry the engine's MSVC declarations, so a POSIX build needs the same
# narrow wchar_t and MSVC keyword handling the engine is built with.
if(OPENTS_POSIX)
    target_compile_options(UnvqDelta PRIVATE
        -fshort-wchar
        -fno-builtin-wcslen
        -fno-builtin-wcsnlen
        -fexceptions
        -fdeclspec
    )
endif()
