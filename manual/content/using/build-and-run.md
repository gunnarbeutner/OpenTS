---
title: Build and run
summary: Builds the Debug or Release executable for either platform and runs it against a directory of game data.
category: getting-started
source_files:
  - docs/BUILDING.md
  - CMakeLists.txt
  - code/CMakeLists.txt
  - code/languagestrings.cpp
related:
  - type: using
    id: game-data
  - type: using
    id: developer-build-troubleshooting
---

Install Visual Studio 2022 with the **Desktop development with C++** workload, a Windows SDK, CMake 3.23 or newer, and Git for Windows. The repository's `docs/BUILDING.md` covers toolchain details and options.

The renderer and the audio layer are vendored dependencies, so a clone that did not fetch submodules has to fetch them before configuring. Configuration stops with instructions if a submodule is missing.

```powershell title="PowerShell"
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Debug
```

The Debug build writes `GameD.exe`, its symbols, map file, to `build/bin/Debug/`, and places the repository's `ui/` directory of UI documents, styles, and font beside them. Use `--config Release` to write `Game.exe` to `build/bin/Release/` instead. Nothing is copied out of the build directory, so the two configurations never overwrite each other.

`-A x64` builds the 64-bit executable. A build directory holds one platform, so give the 64-bit build its own, such as `-B build/x64`. A saved game belongs to the platform that wrote it, and a network game needs every player on the same platform.

Supply the required game data in `Run/`, then launch the built executable and name that data directory:

```powershell title="PowerShell"
.\build\bin\Debug\GameD.exe -DATADIR="$PWD\Run"
```

The game changes to the directory holding the executable before it reads the command line. A relative `-DATADIR` path is resolved from that directory, not from the one the command runs in.

The game's text is compiled into the executable. OpenTS neither builds nor reads `Language.dll`, so a localized or edited copy beside the executable or in the game data directory has no effect.

The experimental native iOS app bundles local game data and starts the first GDI campaign mission by default. The repository's `docs/IOS-PORT.md` owns simulator build and installation instructions, storage locations, and port limitations.
