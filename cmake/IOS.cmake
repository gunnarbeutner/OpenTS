include(FetchContent)
enable_language(OBJC)
option(OPENTS_IOS_AUDIO "Enable the iOS CoreAudio output device" ON)

# There is no Homebrew SDL2 for iOS, so the host's SDL is built from the release tarball.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2
    URL https://www.libsdl.org/release/SDL2-2.32.10.tar.gz
    URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
)
FetchContent_MakeAvailable(SDL2)

# An iOS app reads only what it ships with, so the game data is settled at configure time.
set(OPENTS_IOS_ASSET_DIR "" CACHE PATH "Prepared local iOS game data")
if(NOT EXISTS "${OPENTS_IOS_ASSET_DIR}/TIBSUN.MIX")
    message(FATAL_ERROR "Prepare local assets with ios/prepare_assets.py, then set OPENTS_IOS_ASSET_DIR")
endif()
