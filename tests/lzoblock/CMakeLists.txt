# The LZO block classes are compiled straight into the harness against the same vendored
# library the engine links. It lives outside code/ so that the recursive glob building OpenTS
# cannot pick this target's entry point up.

opents_add_test(LzoBlock
    NAME lzoblock
    SOURCES lzoblock.cpp
    ENGINE
        buff.cpp
        lzopipe.cpp
        lzostraw.cpp
        pipe.cpp
        straw.cpp
        xpipe.cpp
        xstraw.cpp
    DEFINITIONS WIN32 _WINDOWS _MBCS NOMINMAX
    LIBRARIES lzo
    FLOAT
)

# The block classes carry the engine's MSVC declarations, so a POSIX build needs the
# same narrow wchar_t and MSVC keyword handling the engine is built with.
if(OPENTS_POSIX)
    target_compile_options(LzoBlock PRIVATE
        -fshort-wchar
        -fno-builtin-wcslen
        -fno-builtin-wcsnlen
        -fexceptions
        -fdeclspec
    )
endif()

if(EMSCRIPTEN)
    # The compressor keeps its 64 KiB work area on the stack, which is the whole of the
    # default wasm stack.
    target_link_options(LzoBlock PRIVATE -sSTACK_SIZE=1048576)
endif()
