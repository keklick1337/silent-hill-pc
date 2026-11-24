# Building

How to build Silent Hill PC from source on Linux and macOS, and for Windows and Android. Nothing is installed into the system by the build or by any script of the project: sources of dependencies that are not vendored in `third_party/` are fetched at pinned tags and commits into `third_party/deps/` (ignored by git), and built there.

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
tools/ci/package_dmg.sh dist/SilentHillPC-dev-macos-arm64.tar.gz dist
```

`package_dmg.sh` turns the checked package into what Mac users install: `Silent Hill PC.app` (the binaries and dylibs in `Contents/MacOS`, everything else in `Contents/Resources`, the icon made from `assets/icon/master.png`) in `SilentHillPC-<version>-macos-<cpu>.dmg` beside an *Applications* link. A bundle cannot be written to, so the game started from it keeps its root (`config.cfg`, `gamedata/` with the import, `.savegames/`, `logs/`, `mods/`) in `~/Library/Application Support/Silent Hill PC/` and copies the bundle's resources there once per build (`src/engine/app/app_bundle.h`; `PcPaths_BinDir` is where the binaries are, `PcPaths_ResourceDir` the read-only files).

Without a certificate the app is signed ad hoc and not notarized: macOS refuses a downloaded copy once, until *Open Anyway* in *System Settings > Privacy & Security* (or `xattr -dr com.apple.quarantine` on it). With an Apple Developer ID the workflow signs with the hardened runtime and notarizes: set the repository secrets `MACOS_CERTIFICATE_P12` (the base64 of the exported "Developer ID Application" certificate), `MACOS_CERTIFICATE_PASSWORD`, `MACOS_SIGN_IDENTITY` (its name, `Developer ID Application: <name> (<team>)`), and for notarization `MACOS_NOTARY_APPLE_ID`, `MACOS_NOTARY_TEAM_ID` and `MACOS_NOTARY_PASSWORD` (an app-specific password). Locally the same comes from `SH_MACOS_SIGN_IDENTITY` and `SH_NOTARY_*`.

### 3.4 Notes

- The quick setup recommends Vulkan on macOS when MoltenVK gives a device (Apple's OpenGL is frozen at 4.1 over Metal, `Hardware::gl_legacy`); OpenGL stays selectable.
- The profiler overlay's figures on macOS (`src/platform/process_stats.cpp`): RAM is the process's memory footprint (as in Activity Monitor), GPU load and the memory in use come from the IOAccelerator statistics, and the total is the unified memory.
- Floating-point contraction is off for the whole build (`-ffp-contract=off`): on arm64 Clang would otherwise fuse `a*b+c` into one rounding where x86-64 rounds twice, and the replays would differ between the platforms.
- `SDL_VIDEODRIVER=offscreen` (the tests, the replay harness, the map sweep): SDL's offscreen driver has OpenGL only through EGL, which macOS lacks, so on macOS the game answers it with Cocoa and hidden windows (`src/engine/desktop/video_driver.h`). It needs a logged-in session with a window server, which a GitHub macOS runner has.

## 4. Packages

`tools/ci/package.sh <build> <linux|windows|macos> <version> [<out dir>]` writes `SilentHillPC-<version>-<platform>-<cpu>.tar.gz` (Linux, macOS) or `.zip` (Windows); `<cpu>` is `x86_64`, on macOS the build's (`arm64` or `x86_64`; CI builds both, the Intel one natively on an Intel runner): the executable, `sh-import`, `sh-upscale` with its model, the libraries, `assets/`, `config.cfg`, the licenses and `gamedata/` strictly from the files tracked by git. It follows the import tables of every binary to decide which libraries go in, and refuses to write a package in which an imported library is missing or anything disc-derived is present. `python3 -I tools/ci/check_package.py check <archive> <linux|windows|macos> --run` checks a package unpacked into an empty directory. Releases: [DEVELOPMENT.md](DEVELOPMENT.md), section 5.

## 5. Android

The APK is built on Linux with the Android NDK; one APK holds all four ABIs (`arm64-v8a`, `armeabi-v7a`, `x86_64`, `x86`), Android 10 (API 29) or newer, the first release that requires Vulkan 1.1 on every new 64-bit device. The game is `libmain.so`, started by SDL's Java activity (`android/`, `SilentHillActivity` on SDL's `SDLActivity`, whose Java sources come from the pinned SDL3 source tree, not copied into the repository).

### 5.1 Requirements

- The Android SDK (`ANDROID_HOME`) with the platform `android-36` and build tools, and the NDK `28.2.13676358` (`ANDROID_NDK_HOME`, default `$ANDROID_HOME/ndk/28.2.13676358`).
- JDK 17 or newer, Gradle 8.14 (the Android Gradle Plugin `8.13.1` is pinned in `android/settings.gradle`; Gradle fetches it into `~/.gradle`), CMake 3.16+, Ninja, `zip`, Python 3, `git`.
- The SPIR-V compiler: the NDK's `shader-tools/*/glslc` is used.

### 5.2 Build

```sh
tools/android/build.sh                 # all four ABIs; or name some: tools/android/build.sh x86_64
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

The script builds the dependencies of an ABI the first time (`tools/android-deps/build.sh`: SDL3 shared, libjpeg-turbo static, from the pinned sources of `tools/win-deps/fetch.sh`, into `third_party/deps/android-<abi>/`), the game for each ABI in `build-android/<abi>/`, stages the native libraries (`libmain.so`, `libSDL3.so`, the NDK's `libc++_shared.so`), the base data and the launcher icon in `android/app/build/sh/`, and runs `gradle assembleDebug` in `android/` (`--release` runs `assembleRelease`, unsigned). The debug APK is signed with the debug key of the machine.

What differs from the desktop build:

- Renderer: Vulkan only. The native Vulkan backend runs on Vulkan 1.1: with dynamic rendering and synchronization2 where the device has them, else with render pass objects and Vulkan 1.0 barriers ([RENDER.md](RENDER.md) 2.2a); when it cannot start, the SDL3 GPU backend takes over (asked without the clip distance, depth clamp and indirect first instance features it would otherwise require, which Mali GPUs lack). The OpenGL backend is desktop GL 3.3 core, which Android lacks (it has OpenGL ES), so `renderer = gl` runs Vulkan, and without Vulkan the game shows a message box (`ui.gpu.no_vulkan`, in every language) and ends.
- Sound goes through SDL3 (AAudio or OpenSL ES); OpenAL Soft is not linked. FFmpeg (mods' mp4 movies) and the texture upscaler `sh-upscale` are not part of the Android build; the disc's movies have their own decoder.
- The quick setup is skipped (its questions are a desktop's); a restart that a setting asks for ends the game, which the player starts again.
- The game's root is the app's folder on the shared storage, `Android/data/<app id>/files/` (`getExternalFilesDir`, else the internal files folder): the activity unpacks the APK's `base.zip` there once per build (tracked `gamedata/`, the runtime assets, `config.cfg` only when there is none, the console font, the licenses), and names it in `SH_ANDROID_ROOT` (`src/engine/app/app_bundle.cpp`). The import, `config.cfg`, saves, logs and mods live there. `stdout` and `stderr` go to the system log (`adb logcat -s SilentHillPC`).
- The disc image is chosen with the system file picker (Storage Access Framework) from the import screen's Browse. A `.zip` of the `.cue` and `.bin` is the way: a picked `.cue` cannot reach its `.bin`. The picker gives a `content://` URI; the activity opens it, and since the system lets no file path reach the open document, copies it into the app's folder for the import and deletes the copy afterwards (`SilentHillActivity.openDocument`, `src/engine/app/first_run/picked_document.cpp`).
- Landscape, fullscreen without the system bars (into a notch's corners: the controls keep to the safe area), the screen kept on; the back key is Esc. The activity is resizeable and keeps running through size and density changes (tablets, foldables). The picture is always the screen's own size (`width`, `height`, `fullscreen` and `vsync` are not read there; the Display page hides their rows): the graphics preset `mobile` (the default there, [RENDER.md](RENDER.md) 7.2) sets `render_scale` from the screen's height, and frames are shown at the display's rhythm (FIFO), the frame clock sleeping instead of spinning. The Renderer row offers Vulkan and SDL GPU.
- Touch: `src/input/touch/` (README, "Touch controls"). Once the game window is open the finger events reach it through `sh::platform::set_touch_hook` (`src/engine/platform/window.h`) and SDL's mouse made of the first finger is turned off (`SDL_HINT_TOUCH_MOUSE_EVENTS`): in gameplay the on-screen controls hold input actions (`PcActions_SetTouchActions`) and the stick drives the pad's analog bytes; elsewhere a finger moves a pointer the menus follow (`Sh_SetTouchPointer`), a drag turns the wheel, a tap (a finger that did not move, however long it rested) clicks (and is Cross on screens that do not follow the pointer). The lists drawn by the menu view (`src/ui/menu/menu_view.cpp`: Options, the pause menu, the debug menu and the list of a row's values) claim the drag (`claim_list_drag`) and follow the finger themselves (`src/engine/menu/touch_scroll.h`: 1:1 while it drags, a glide after a flick, a rubber band at the ends, at rest on a whole row); a tap on a row of values (On / Off included) opens the list of them, a bar is set where it is tapped or dragged sideways. The phone's Back key is counted by an event watch, so a press shorter than a frame still reaches the game as Back. The import screen (`src/engine/app/first_run/progress_window.cpp`, before the game window) reads the finger events itself: a tap presses a button, a drag scrolls, and it keeps out of the display cutout (`SDL_GetWindowSafeArea`). The controls are drawn as anti-aliased coverage masks made at their on-screen size (`touch_sprites.h`: discs, rings, soft shadows, the SVG icons sampled 4x4 per pixel) into the window overlay, on the window after the present at the display's resolution, whatever the render scale. `Sh_IsMobile()` (`src/platform/sh_platform.h`) tells code that a phone or tablet runs it. On a desktop `touch_controls = on` turns the mouse into a finger to try the controls.

### 5.3 Testing on the emulator

A debuggable build takes the command line from the launching intent, so the import runs without the picker. The app may read its own folder on the shared storage:

```sh
adb -s emulator-5554 push "Silent Hill (v1.1).zip" /sdcard/Android/data/io.github.keklick1337.silenthillpc/files/sh.zip
adb -s emulator-5554 shell am start -n io.github.keklick1337.silenthillpc/.SilentHillActivity \
    --es image /sdcard/Android/data/io.github.keklick1337.silenthillpc/files/sh.zip
adb -s emulator-5554 logcat -s SilentHillPC
adb -s emulator-5554 exec-out screencap -p > shot.png
```

`--es args "<words>"` passes any other options; through `adb shell` the words are quoted once more so the device's shell keeps them together, e.g. `--es args "'-skiptogame'"` (straight into play, to reach the pause menu) or `--es args "'-consolecmd /sdcard/Android/data/io.github.keklick1337.silenthillpc/files/t.cmd'"` with a line `@4 options interface` in that file (an Options page from the title). The emulator needs a host GPU with Vulkan (`emulator -avd <name> -gpu host -feature Vulkan`).

### 5.4 Profiling on a phone

The game writes `[PERF]` lines to the system log every 5 s on a phone or tablet: frames per second, the frame time and its worst, the GPU time (one timestamp pair per frame) with the GPU clock where the system shows it (`/sys/kernel/gpu/gpu_clock` on Exynos, `/sys/class/kgsl/kgsl-3d0/gpuclk` on Snapdragon) and the work in millions of GPU cycles per frame (ms × MHz: the governor lowers the clock until the GPU is nearly busy at 60 fps, so a light frame shows almost the GPU time of a heavy one; compare the cycles), and per frame what the commands did: GPU render passes and the passes merged into them, attachment loads and stores with their megabytes (on a phone's tile-based GPU each one moves the whole target through memory), transfers, copies, blits, uploads (and how many went ahead of the frame), draws, pipeline binds, descriptor sets, barriers; then the CPU load, the preset and the scene's and the window's size. The swapchain is logged when it is made (`[PERF] swapchain`: size, images, present mode, transforms, frames in flight). A file named `perf_capture` in the game's folder starts a 10 s capture: the timestamps between passes go on, the scene passes time the split kinds inside them (`~prims`, `~scene world`, `~scene item`), two frames' commands are traced (`[PERF] trace`), and at the end one line gives the GPU time per pass and section. Timestamps inside a pass can change how a tile-based GPU runs it: compare the capture's total with the 5 s lines around it.

```sh
adb logcat -c
adb logcat -s SilentHillPC | grep --line-buffered PERF          # the 5 s lines
adb shell touch /sdcard/Android/data/io.github.keklick1337.silenthillpc/files/perf_capture   # a capture
```

With the profiler overlay on (Options > Interface > Performance Overlay > Profiler, or `fps_profiler = 1`) the overlay shows the same counts (`Passes`, `Binds`) and the capture's countdown. `SH_VK_NO_MERGE=1` in the environment of a desktop run turns off the backend's pass merging and hoisted uploads ([RENDER.md](RENDER.md) 2.2a), to compare the counts and the picture.
