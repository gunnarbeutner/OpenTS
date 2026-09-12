# The logger is compiled straight into the harness. It lives outside code/ so that the
# recursive glob building OpenTS cannot pick this target's entry point up.

opents_add_test(LogStress
    NAME logstress
    SOURCES logstress.cpp
    ENGINE
        ${OPENTS_LOGGER_SOURCES}
    INCLUDES ${OPENTS_GENERATED_DIR}
    DEFINITIONS WIN32 _WINDOWS _MBCS NOMINMAX
    STAMP
)
