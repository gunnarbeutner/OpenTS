# The iOS half of the executable's build, included from code/CMakeLists.txt once the target
# exists. SDL2main owns the UIKit entry point, so the engine's own main is renamed and
# ios/main.cpp calls it.
target_compile_definitions(OpenTS PRIVATE OPENTS_SDL_HOST OPENTS_IOS_HOST)
set_property(SOURCE startup.cpp APPEND PROPERTY COMPILE_DEFINITIONS main=OpenTS_Main)
target_sources(OpenTS PRIVATE "${CMAKE_SOURCE_DIR}/ios/main.cpp")
target_link_libraries(OpenTS PRIVATE SDL2::SDL2main SDL2::SDL2-static
    "-framework UIKit" "-framework Metal" "-framework QuartzCore"
    "-framework CoreGraphics" "-framework CoreFoundation" "-framework Foundation"
    "-framework CoreVideo" "-framework CoreMedia" "-framework VideoToolbox"
    "-framework OpenGLES" "-framework AudioToolbox" "-framework AVFoundation"
)
set_target_properties(OpenTS PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist.in"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.opents.ios"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
)

# Each prepared archive keeps its place under GameData/ inside the bundle.
file(GLOB_RECURSE IOS_GAME_DATA CONFIGURE_DEPENDS "${OPENTS_IOS_ASSET_DIR}/*")
foreach(asset IN LISTS IOS_GAME_DATA)
    file(RELATIVE_PATH relative "${OPENTS_IOS_ASSET_DIR}" "${asset}")
    get_filename_component(directory "${relative}" DIRECTORY)
    set_source_files_properties("${asset}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/GameData/${directory}")
endforeach()
target_sources(OpenTS PRIVATE ${IOS_GAME_DATA})
