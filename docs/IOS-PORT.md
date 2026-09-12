# Native iOS port

The iOS app is the native macOS host under UIKit. `code/sdlhost.cpp` answers
`code/hostwindow.h` on both Apple targets, and what differs on iOS is behind
`OPENTS_IOS_HOST`: the window covers the screen, bgfx draws into a Metal layer
SDL owns rather than into a window, the software keyboard is never raised, and
a finger stands in for the mouse. `ios/` holds the app's entry point, its
bundle, and the tool that prepares the game data it ships with. Visual Studio
2022 Win32 remains the supported target; nothing here is a support claim.

## Build for the simulator

Use Xcode with an installed iOS simulator runtime, CMake, Ninja, and Python
3.11 or newer. Initialize the repository's submodules before configuring.
CMake downloads SDL2 2.32.10 from its release server and verifies its SHA-256;
there is no Homebrew SDL2 for iOS to find instead. An existing source directory
can be supplied with `-DFETCHCONTENT_SOURCE_DIR_SDL2=/path/to/SDL2-2.32.10`.

An app reads only what it ships with, so the game data is settled at configure
time. Prepare it from a local web asset tree, then build an Apple Silicon
simulator app:

```sh
python3 ios/prepare_assets.py ../OpenTS-Assets/build/web build/ios-assets
cmake -S . -B build/ios-sim -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DOPENTS_IOS_ASSET_DIR="$PWD/build/ios-assets" \
  -DOPENTS_IOS_AUDIO=OFF
cmake --build build/ios-sim --target OpenTS -j 8
```

`OPENTS_IOS_AUDIO` defaults to `ON`. The command disables CoreAudio because the
iOS 26.5 simulator aborted in `AURemoteIO::Initialize` during audio service RPC
calls when the port was first written. With it off, the engine reports no audio
device and continues silently; the audio decoders are still built.

`ios/prepare_assets.py` verifies the pointer file, the manifest and every
archive it copies, restores the MIX filenames the engine asks for, and writes
them under the ignored `build/` directory. MP4 movies and M4A music from the web
tree are left out, so the iOS build accepts a missing `SCORES.MIX` and a missing
`MOVIES.MIX` where every other target requires them. `ios/tests/` covers the
preparer and reads no game data:

```sh
python3 ios/tests/test_prepare_assets.py
```

CTest is not registered for an iOS build: the harnesses are console programs
with no bundle, and nothing runs them on a device or a simulator. Build them on
macOS instead ([Building OpenTS](BUILDING.md#macos-in-progress-and-unsupported)).

## Install and launch

Choose a device from `xcrun simctl list devices available`. Boot it if needed,
then use its UUID in place of `DEVICE`:

```sh
xcrun simctl boot DEVICE
xcrun simctl bootstatus DEVICE -b
xcrun simctl install DEVICE build/ios-sim/bin/GameD.app
xcrun simctl launch DEVICE org.opents.ios
```

`ios/main.cpp` is the app's `main`. It points the current directory at the
app's writable directory, which is where everything the game writes goes, and
then calls the engine's own `main` with the arguments it would have had from a
command line: the bundled data directory, the writable user directory,
`-NOINTRO` and `-NOBRIEFING`. Launching with no arguments of its own adds
`-SCENARIO=GDI1A.MAP` and `-CAMPAIGN=GDI1`, because the menus are not reachable
by touch yet. Arguments passed to `simctl launch` are appended instead of that
campaign default, so `-NOINTRO` alone opens the menus.

SDL turns a touch into a left mouse button, which reaches the game through the
same `Game_Window_Mouse_Button` path a mouse does, and `code/ui/uisdl.cpp` gives
the UI shell first refusal on it either way. A finger is not a pointer left
resting somewhere, so `Host_Pointer_Is_Hovering` turns false when it lifts and
the map stops scrolling at the edges. There are no gestures beyond that, and no
hardware keyboard is assumed.

Settings, saves, and logs live under `Library/Application Support/OpenTS/OpenTS`
in the app's data container, which survives a reinstall and goes with an
uninstall. Find it with:

```sh
xcrun simctl get_app_container DEVICE org.opents.ios data
```

The engine log is under `Debug/` there rather than beside the executable, whose
bundle is read only; `Debug_Init` takes the directory for it. `ios-runtime.log`
and `ios-errors.log` beside it hold what SDL, bgfx and the standard streams
wrote.

## Port boundaries

This is a platform addition. Game rules, archive layouts, saves and network
formats are the base branch's, and no save or network interoperability with
another platform is established by it. Signing for a physical device, App Store
packaging, multiplayer, campaign completion, and background and resume behavior
all need separate work.

The simulator converts the frame from RGB565 to BGRA8 through the presenter's
existing path, because its Metal validation rejects packed RGB565 textures
whatever bgfx's capability report says. A device does not.

## What has been run

On macOS 26.5 with Apple clang on arm64, Xcode's iOS 26.5 simulator SDK, and
the iPad Pro 13-inch (M5) simulator, on September 12, 2026:

- The macOS build configured, built and linked `OpenTS`, and all fifteen
  portable harnesses passed under `ctest`.
- The simulator app configured and built with the command above, and
  `bin/GameD.app` carried the twelve prepared archives under `GameData/`.
- The app installed into a freshly emptied container and launched. It opened
  the window, brought up Metal, loaded GDI's first mission with no arguments
  given, deployed the construction yard, and drew the map, the sidebar and the
  radar. The simulation went on running: the log recorded the radar activating
  and the mission's teams being created. Settings and the log landed under
  `Library/Application Support/OpenTS/OpenTS` in the writable directory, the
  log under `Debug/` there. That is a runtime observation, not a test result.
- `python3 ios/tests/test_prepare_assets.py` passed its five checks.

Not run here: a Release configuration, a physical device, any build with
`OPENTS_IOS_AUDIO=ON`, and any touch. Nothing has been played through the app,
and nothing but the opening of the first mission has been seen. The picture is
letterboxed, and the landscape orientation the app asks for has not been
checked against a rotated device.
