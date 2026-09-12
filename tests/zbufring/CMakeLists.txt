# The two buffer classes are compiled straight into the harness, along with the surface and
# blit sources their fills reach through. It lives outside code/ so that the recursive glob
# building OpenTS cannot pick this target's entry point up.

opents_add_test(ZBufRing
    NAME zbufring
    SOURCES zbufringtest.cpp
    ENGINE
        zbuffer.cpp
        abuffer.cpp
        xsurface.cpp
        blit.cpp
        buff.cpp
        _zbuffer.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
    DEFINITIONS WIN32 _WINDOWS _MBCS NOMINMAX
)

# The buffers carry the engine's MSVC declarations, so a POSIX build needs the same
# narrow wchar_t and MSVC keyword handling the engine is built with.
if(OPENTS_POSIX)
    target_compile_options(ZBufRing PRIVATE
        -fshort-wchar
        -fno-builtin-wcslen
        -fno-builtin-wcsnlen
        -fexceptions
        -fdeclspec
    )
endif()
