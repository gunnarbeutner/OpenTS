# The dialog font and its RmlUi engine are compiled straight into the harness, which stands in
# for the sheet reader, the code page, the log and the owner-draw hue factor with synthetic
# sheets, and drives the engine through RmlUi with a render interface that records what it is
# given. It lives outside code/ so that the recursive glob building OpenTS cannot pick this
# target's entry point up.

opents_add_test(UIFontDialog
    NAME uifontdialog
    SOURCES uifontdialog.cpp
    ENGINE
        hsv.cpp
        rgb.cpp
        ui/uifont.cpp
        ui/uifontengine.cpp
    DEFINITIONS WIN32 _WINDOWS _MBCS NOMINMAX
        "OPENTS_SHIPPED_FACE=\"${CMAKE_SOURCE_DIR}/ui/LiberationSans-Regular.ttf\""
    LIBRARIES rmlui
)

# FreeType is compiled for the wasm form of longjmp on that target, which the link has to
# provide, and that form needs the harness to unwind with wasm exceptions as the engine does.
if(EMSCRIPTEN)
    target_compile_options(UIFontDialog PRIVATE -fwasm-exceptions)
    target_link_options(UIFontDialog PRIVATE -fwasm-exceptions -sSUPPORT_LONGJMP=wasm -sNODERAWFS=1)
endif()
