# Building

How to build Silent Hill PC from source on Linux and macOS, and for Windows. Nothing is installed into the system by the build or by any script of the project: sources of dependencies that are not vendored in `third_party/` are fetched at pinned tags and commits into `third_party/deps/` (ignored by git), and built there.

The commands below are the ones the CI runs ([.github/workflows/](../.github/workflows/)). How to run the checks after a build: [DEVELOPMENT.md](DEVELOPMENT.md), section 4.

## 1. Linux

### 1.1 Requirements

- GCC 14 or newer (or a recent Clang), CMake 3.16+, Ninja, `pkg-config`, Python 3 (for the checks).
- Development files of OpenGL, OpenAL and libjpeg, and of SDL3 3.2 or newer if your system has it.
- `glslc` or `glslangValidator` for the SPIR-V shaders of the Vulkan renderers; without one, `renderer = vulkan` falls back to OpenGL at run time.

Debian / Ubuntu:

```sh
sudo apt-get install g++-14 cmake ninja-build pkg-config python3 glslang-tools \
    libgl-dev libegl-dev libopenal-dev libjpeg-dev
```

### 1.2 Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ./SilentHillPC --image "/path/to/Silent Hill (v1.1).cue"
```

The build copies the repository's `gamedata/` beside the executable; the first start imports the disc image into it.

### 1.3 SDL3 from source

Without a system SDL3 (Ubuntu 24.04 has none), fetch the pinned source and let the build compile it statically. SDL then needs the X11, Wayland and audio development packages (the list is in [build-linux.yml](../.github/workflows/build-linux.yml): `libx11-dev`, `libxkbcommon-dev`, `libwayland-dev`, `wayland-protocols`, `libpulse-dev`, `libasound2-dev`, `libpipewire-0.3-dev` and others):

```sh
tools/win-deps/fetch.sh SDL3
ln -sfn deps/src/SDL3 third_party/SDL3
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON
```

### 1.4 Optional dependencies

| Script | What it builds | What you get |
| --- | --- | --- |
| `tools/ffmpeg/build.sh` | the project's minimal LGPL FFmpeg (mp4/mov, H.264, HEVC, AAC) | mp4 movies of mods; the game loads it at run time (a system FFmpeg 6 to 8 works too). Disc movies and mod AVI always work. |
| `tools/ncnn/build.sh` | ncnn with Vulkan compute, plus the Real-ESRGAN model | the AI texture upscaler `sh-upscale` (`texture_upscale`, [design/RENDER.md](RENDER.md) section 12) |
| `tools/linux-deps/build.sh` | OpenAL Soft (shared), libjpeg-turbo (static), FFmpeg and ncnn | everything a release package carries, as the release build does |

All of them install into `third_party/deps/linux-x86_64/`, which CMake picks up by itself at the next configure. Without ncnn only `sh-upscale` is missing (CMake prints a warning); another prefix can be given with `-DSH_UPSCALE_NCNN_PREFIX=<dir>`.

### 1.5 CMake options

| Option | Default | Meaning |
| --- | --- | --- |
| `CMAKE_BUILD_TYPE` | — | `Release` for playing, `Debug` / `RelWithDebInfo` for development |
| `SH_DEBUG` | `OFF` | extra debug output |
| `SH_ASAN` | `OFF` | AddressSanitizer |
| `SH_FMV_FFMPEG` | `ON` | the run-time FFmpeg decoder for mod movies |
| `SH_RHI_SDL3` | `OFF` | make the SPIR-V shader compiler mandatory |
| `SH_UPSCALE_NCNN_PREFIX` | `third_party/deps/<os>-<cpu>` | where `tools/ncnn/build.sh` installed ncnn |
| `SH_WIN_DEPS_PREFIX` | `third_party/deps/x86_64-w64-mingw32` | the Windows dependency prefix (section 2) |

### 1.6 Release build as in CI

```sh
tools/win-deps/fetch.sh && ln -sfn deps/src/SDL3 third_party/SDL3
tools/linux-deps/build.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON \
    "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc"
cmake --build build
tools/ci/package.sh build linux dev dist
python3 -I tools/ci/check_package.py check dist/SilentHillPC-dev-linux-x86_64.tar.gz linux --run
```

The release binaries are built on Ubuntu 24.04 and need glibc 2.39 or newer.

## 2. Windows

The Windows build is **cross-compiled on Linux** with the mingw-w64 GCC toolchain (posix thread model); this is how the CI and the releases build it. A native build on Windows (MSVC, or MSYS2 with MinGW) is not supported: the dependency scripts are bash scripts that expect a Linux host, and the project has not been built that way.

### 2.1 Requirements

Debian trixie (its mingw-w64 is GCC 14) or another distribution with mingw-w64 GCC 14+, plus `cmake`, `ninja-build`, `pkg-config`, `nasm`, `curl`, `7z`, `python3` and `glslang-tools`:

```sh
sudo apt-get install cmake ninja-build pkg-config nasm zip unzip python3 curl 7zip glslang-tools \
    gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64
sudo update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
```

### 2.2 Build

```sh
tools/win-deps/build.sh          # fetch and build the dependencies into third_party/deps/x86_64-w64-mingw32/
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake
cmake --build build-win
tools/ci/package.sh build-win windows dev dist
```

`tools/win-deps/build.sh --clean` rebuilds the dependencies from scratch; `tools/win-deps/fetch.sh [--force]` only downloads the sources. `SH_WIN_DEPS_PREFIX` (environment variable or `-D`) sets another dependency prefix, for example when building from a git worktree: `-DSH_WIN_DEPS_PREFIX=<main tree>/third_party/deps/x86_64-w64-mingw32`.

The GCC runtime is linked statically into every exe (the toolchain file passes `-static -static-libgcc -static-libstdc++`); SDL3, OpenAL Soft and FFmpeg stay DLLs beside the exe (OpenAL Soft and FFmpeg are LGPL, so the user can replace them). The exe does not import the FFmpeg DLLs: it loads them at a mod's first mp4 movie.

Result in `build-win/`: `SilentHillPC.exe`, `SDL3.dll`, `OpenAL32.dll`, the FFmpeg DLLs, `software_gl/` (Mesa llvmpipe, used only when the system has no OpenGL 3.3 driver, [design/RENDER.md](RENDER.md) section 2.2c), `sh-upscale.exe` with `upscale-models/`, `assets/`, `licenses/`, `gamedata/`, `tests/sh_tests.exe`, `tools/sh-import/sh-import.exe`.

### 2.3 Dependencies

| What | Version (tag) | How it is built | License |
| --- | --- | --- | --- |
| SDL3 | `release-3.4.18` | `SDL3.dll` (window, input, audio, GPU) | Zlib |
| OpenAL Soft | `1.24.3` | `OpenAL32.dll` | LGPL-2.1-or-later |
| libjpeg-turbo | `3.1.4.1` | static, SIMD via nasm | IJG, BSD-3-Clause, Zlib |
| FFmpeg | `n8.1.3` | `avformat`, `avcodec`, `avutil`, `swscale`, `swresample` DLLs (`tools/ffmpeg/build.sh --mingw`) | LGPL-2.1-or-later |
| ncnn | `20250916` | static, for `sh-upscale` (`tools/ncnn/build.sh --mingw`) | BSD-3-Clause |
| Mesa llvmpipe | `26.2.6` (mesa-dist-win) | not built: the prebuilt `opengl32.dll` and `libgallium_wgl.dll`, pinned by SHA-256 (`tools/win-deps/software_gl.sh`) | MIT and others |

The full commit hashes are in `tools/win-deps/fetch.sh`; each dependency is pinned by tag and commit, and the script stops if a tag moved. Linux uses the same versions. `build.sh` writes the license texts into `<prefix>/share/sh-licenses/`, and CMake copies them beside the exe.

### 2.4 Testing under Wine

Use a prefix of your own. Some Wine builds do not pass the `SDL_*` and `ALSOFT_*` variables of the Unix environment to the process, so set them in the prefix's registry once (SDL3 reads `SDL_AUDIO_DRIVER`):

```sh
export WINEPREFIX=$PWD/.tmp/wineprefix WINEDEBUG=-all
wineboot -i
wine reg add 'HKCU\Environment' /v SDL_AUDIO_DRIVER /d dummy /f
wine reg add 'HKCU\Environment' /v ALSOFT_DRIVERS /d null /f
wineserver -w
(cd build-win/tests && wine sh_tests.exe)
(cd build-win && wine SilentHillPC.exe --dialogs-to-console --image "Z:/path/to/Silent Hill (v1.1).cue")
```

`build-win/tools/sh-audio-check/sh-audio-check.exe` opens the sound output without the game. For the bundled llvmpipe under Wine set `software_gl = force` and `WINEDLLOVERRIDES=opengl32=n,b`.

### 2.5 Notes for code that must build on both

- `SH_LOG_PRINTF_ATTR` (`src/engine/log/sh_log.h`) is `format(gnu_printf)` under mingw: plain `printf` checks formats against msvcrt and rejects `%zu`.
- `tests/CMakeLists.txt` copies `SDL3.dll` beside `sh_tests.exe`.
- Run-time loading of FFmpeg: `src/engine/loader/shared_library.{h,cpp}` (`LoadLibrary` / `dlopen`).
- The exe icon is `assets/icon/app.ico` (resource `assets/res/app.rc`), made by `assets/icon/generate.sh` from `master.png`.

## 3. macOS

macOS 13.3 or newer, on Apple silicon (arm64) or Intel. `renderer = gl` is Apple's OpenGL 4.1 on Metal; `renderer = vulkan` runs the native Vulkan backend on MoltenVK (Vulkan 1.3 over Metal): HDR, TAA, volumetric fog and the tessellation of model upscaling work, ray tracing and mesh shaders do not (MoltenVK has neither), so the RTX presets run without their ray-traced effects. `renderer = sdlgpu` falls back to GL (its SPIR-V would need translating to Metal). Sound goes through SDL3.

### 3.1 Requirements

- The Xcode Command Line Tools (`xcode-select --install`): Apple Clang, `make`, `git`.
- CMake 3.16+, Ninja, `pkg-config` and glslang (the SPIR-V of the Vulkan renderer; without it `renderer = vulkan` falls back to GL), for example from Homebrew: `brew install cmake ninja pkg-config glslang`.
- For `renderer = vulkan` in a development build, MoltenVK: `brew install molten-vk` (or the LunarG Vulkan SDK). The game looks for it beside the exe, in `/usr/local/lib` and in `/opt/homebrew/lib` (`rhi::loadVulkanLibrary`).
- For a development build, SDL3 3.2+ and libjpeg: `brew install sdl3 jpeg-turbo` (FFmpeg for mod mp4 movies is optional: `brew install ffmpeg`). OpenAL and OpenGL are the system's frameworks.
- For the test scripts of [DEVELOPMENT.md](DEVELOPMENT.md), section 4 (not for building or playing): GNU coreutils and a current bash, `brew install coreutils bash`, with `$(brew --prefix coreutils)/libexec/gnubin` first in `PATH`.

### 3.2 Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ./SilentHillPC --image "/path/to/Silent Hill (v1.1).cue"
```

Such a build links Homebrew's libraries and runs on the machine that built it.

### 3.3 Release build as in CI

A package for other Macs carries nothing of Homebrew: SDL3 is compiled statically from the pinned source, `tools/macos-deps/build.sh` builds libjpeg-turbo (static), the project's LGPL FFmpeg (dylibs named by `@loader_path`, beside the exe) and ncnn for `sh-upscale` into `third_party/deps/darwin-<cpu>/`, which CMake picks up by itself, and fetches MoltenVK (the Khronos release binary, pinned by SHA-256), which goes beside the exe. `MACOSX_DEPLOYMENT_TARGET` is the oldest macOS the binaries run on; the dependencies and the game must be built with the same one.

```sh
export MACOSX_DEPLOYMENT_TARGET=13.3
tools/win-deps/fetch.sh SDL3 && ln -sfn deps/src/SDL3 third_party/SDL3
tools/macos-deps/build.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET
cmake --build build
tools/ci/package.sh build macos dev dist
python3 -I tools/ci/check_package.py check dist/SilentHillPC-dev-macos-arm64.tar.gz macos --run
```

The package is ad-hoc signed, not notarized: macOS keeps a downloaded copy in quarantine. After unpacking, `xattr -dr com.apple.quarantine <folder>` lets it start (or *Open* from the Finder's context menu, then *Open Anyway* in *System Settings > Privacy & Security*).

### 3.4 Notes

- The quick setup recommends Vulkan on macOS when MoltenVK gives a device (Apple's OpenGL is frozen at 4.1 over Metal, `Hardware::gl_legacy`); OpenGL stays selectable.
- The profiler overlay's figures on macOS (`src/platform/process_stats.cpp`): RAM is the process's memory footprint (as in Activity Monitor), GPU load and the memory in use come from the IOAccelerator statistics, and the total is the unified memory.
- Floating-point contraction is off for the whole build (`-ffp-contract=off`): on arm64 Clang would otherwise fuse `a*b+c` into one rounding where x86-64 rounds twice, and the replays would differ between the platforms.
- `SDL_VIDEODRIVER=offscreen` (the tests, the replay harness, the map sweep): SDL's offscreen driver has OpenGL only through EGL, which macOS lacks, so on macOS the game answers it with Cocoa and hidden windows (`src/engine/desktop/video_driver.h`). It needs a logged-in session with a window server, which a GitHub macOS runner has.

## 4. Packages

`tools/ci/package.sh <build> <linux|windows|macos> <version> [<out dir>]` writes `SilentHillPC-<version>-<platform>-<cpu>.tar.gz` (Linux, macOS) or `.zip` (Windows); `<cpu>` is `x86_64`, on macOS the build's (`arm64`): the executable, `sh-import`, `sh-upscale` with its model, the libraries, `assets/`, `config.cfg`, the licenses and `gamedata/` strictly from the files tracked by git. It follows the import tables of every binary to decide which libraries go in, and refuses to write a package in which an imported library is missing or anything disc-derived is present. `python3 -I tools/ci/check_package.py check <archive> <linux|windows|macos> --run` checks a package unpacked into an empty directory. Releases: [DEVELOPMENT.md](DEVELOPMENT.md), section 5.
