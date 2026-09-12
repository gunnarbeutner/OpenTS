# The key-name helper is compiled straight into the harness. It lives outside code/ so that the
# recursive glob building OpenTS cannot pick this target's entry point up.

opents_add_test(KeyName
    NAME keyname
    SOURCES keynametest.cpp
    ENGINE keyname.cpp
    DEFINITIONS WIN32 _WINDOWS _MBCS NOMINMAX
)

# The keyboard header carries the engine's MSVC declarations, so a POSIX build needs the
# same narrow wchar_t and MSVC keyword handling the engine is built with.
if(OPENTS_POSIX)
    target_compile_options(KeyName PRIVATE
        -fshort-wchar
        -fno-builtin-wcslen
        -fno-builtin-wcsnlen
        -fexceptions
        -fdeclspec
    )
endif()
