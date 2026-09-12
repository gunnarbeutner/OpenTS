# The process and diagnostics platform files are compiled straight into the harness together
# with the debug logger that stands on them. It lives outside code/ so that the recursive glob
# building OpenTS cannot pick this target's entry point up.

opents_add_test(PlatformProcess
    NAME platformprocess
    SOURCES platformtest.cpp
    ENGINE ${OPENTS_LOGGER_SOURCES}
    INCLUDES ${OPENTS_GENERATED_DIR}
    DEFINITIONS WIN32 _WINDOWS _MBCS NOMINMAX
    STAMP
)
