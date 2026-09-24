# The harness drives the file a saved game is kept in: savefile.cpp's writer and reader over
# the field table, the compressed content, and every refusal the reader makes. It links the
# same vendored LZO library the engine does, and compiles crc.cpp for the checksum the
# header and the content carry.

# The platform layer resolves a name the host has no file for through the manifest, so the
# reader it is handed comes with it.
set(SAVE_TEST_ENGINE crc.cpp savefile.cpp)
if(EMSCRIPTEN)
    list(APPEND SAVE_TEST_ENGINE httpsource.cpp manifest.cpp)
endif()

opents_add_test(SaveTest
    NAME save
    SOURCES savetest.cpp
    ENGINE ${SAVE_TEST_ENGINE}
    DEFINITIONS WIN32 _WINDOWS _MBCS NOMINMAX
    LIBRARIES lzo OpenTSPlatform
)

if(OPENTS_POSIX)
    target_compile_options(SaveTest PRIVATE -fexceptions)

    # The harness writes the files it reads back, so it needs a real filesystem rather than
    # the in-memory one Emscripten supplies by default.
    target_link_options(SaveTest PRIVATE -fexceptions $<$<BOOL:${EMSCRIPTEN}>:-sNODERAWFS=1;-sALLOW_MEMORY_GROWTH=1>)
endif()
