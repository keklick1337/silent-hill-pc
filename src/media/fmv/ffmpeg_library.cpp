// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/fmv/ffmpeg_library.h"

#if defined(SH_FMV_FFMPEG)

#include "engine/loader/shared_library.h"
#include "engine/log/sh_log.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace sh::fmv {

namespace {

enum Library { Format, Codec, Util, Scale, Resample, kLibraryCount };

constexpr std::array<const char*, kLibraryCount> kLibraryNames = {"avformat", "avcodec", "avutil", "swscale", "swresample"};

// The FFmpeg releases the player accepts, newest first, by the major version of each library.
//
// It is a list and not "this version or newer" because the player reads fields of FFmpeg's structures itself, and
// a structure's layout holds only within one major version. For these three releases the fields it reads were
// measured with offsetof against the real headers and sit at the same place in all of them: AVFrame's data,
// linesize, extended_data, width, height, nb_samples, format and pts; AVFormatContext's streams; AVStream's
// codecpar, time_base and avg_frame_rate; AVCodecParameters' codec_id; AVPacket's stream_index; all of
// AVChannelLayout and its size. What does move between them (the sample rate, channel layout, thread settings and
// size of AVCodecContext, the colour tags of AVFrame) is never read as a field: it goes through the libraries' own
// options by name. A release that is not listed has not been measured and is refused; measure it, then add its row.
//
// The five libraries of a release only work together, so a row is taken whole or not at all. Three rows, not one,
// because a Linux distribution ships exactly one FFmpeg release: the span is what lets the system's own package
// serve when the project's build is absent.
struct Release {
    const char* name;
    std::array<int, kLibraryCount> major;
};
constexpr Release kReleases[] = {
    {"8", {62, 62, 60, 9, 6}},
    {"7", {61, 61, 59, 8, 5}},
    {"6", {60, 60, 58, 7, 4}},
};

// A library opened by its path finds the libraries it needs by name, and takes one already loaded: with avutil
// first, avcodec and avformat bind the avutil beside the exe and not a system one of the same major.
constexpr std::array<Library, kLibraryCount> kLoadOrder = {Util, Resample, Scale, Codec, Format};

std::vector<std::string> file_names(Library library, int major) {
    const std::string base = kLibraryNames[library];
    const std::string version = std::to_string(major);
#if defined(_WIN32)
    return {base + "-" + version + ".dll", "lib" + base + "-" + version + ".dll"};
#elif defined(__APPLE__)
    // A bare name does not reach Homebrew's folders.
    const std::string name = "lib" + base + "." + version + ".dylib";
    return {name, "/opt/homebrew/lib/" + name, "/usr/local/lib/" + name};
#else
    return {"lib" + base + ".so." + version};
#endif
}

sh::loader::SharedLibrary open_library(Library library, int major) {
    const std::vector<std::string> names = file_names(library, major);
    // The project's own build lies beside the exe and wins over a system FFmpeg of the same major. On Linux a
    // bare name never looks there, so that file is opened by its full path.
    if (const char* exe_folder = SDL_GetBasePath(); exe_folder != nullptr && exe_folder[0] != '\0') {
        const std::string beside = std::string(exe_folder) + names.front();
        if (std::FILE* file = std::fopen(beside.c_str(), "rb")) {
            std::fclose(file);
            if (sh::loader::SharedLibrary handle = sh::loader::SharedLibrary::open(beside.c_str())) return handle;
        }
    }
    for (const std::string& name : names) {
        if (sh::loader::SharedLibrary handle = sh::loader::SharedLibrary::open(name.c_str())) return handle;
    }
    return {};
}

using Handles = std::array<sh::loader::SharedLibrary, kLibraryCount>;

void close_all(Handles& handles) {
    for (sh::loader::SharedLibrary& handle : handles) handle.close();
}

// The journal is the only place the player learns why an .mp4 did not play, so it names what is there, what
// would do, and where to get it.
void report_nothing_usable() {
    SH_LOG_DEBUG(FMV, "[FMV] FFmpeg: no usable set (releases 6, 7 and 8 are accepted); .mp4/.mkv/.webm/.mov "
                      "replacements are not played");
    for (int library = 0; library < kLibraryCount; ++library) {
        std::string found, accepted;
        for (const Release& release : kReleases) {
            const std::string major = std::to_string(release.major[static_cast<std::size_t>(library)]);
            accepted += (accepted.empty() ? "" : ", ") + major;
            if (sh::loader::SharedLibrary handle = open_library(static_cast<Library>(library), release.major[static_cast<std::size_t>(library)])) {
                found += (found.empty() ? "" : ", ") + major;
                handle.close();
            }
        }
        SH_LOG_DEBUG(FMV, "[FMV] FFmpeg:   %-10s found: %-10s accepted: %s", kLibraryNames[static_cast<std::size_t>(library)],
                     found.empty() ? "none" : found.c_str(), accepted.c_str());
    }
    SH_LOG_DEBUG(FMV, "[FMV] FFmpeg: all five libraries must be of one release. tools/ffmpeg/build.sh (Windows: "
                      "tools/win-deps/build.sh) builds the project's set and the game's build copies it beside the exe; "
                      "the system's FFmpeg libraries of an accepted release also do");
}

bool load_into(FfmpegLibrary& library) {
    Handles handles{};
    const Release* release = nullptr;
    for (const Release& candidate : kReleases) {
        bool whole = true;
        for (Library which : kLoadOrder) {
            handles[which] = open_library(which, candidate.major[which]);
            if (!handles[which]) {
                whole = false;
                break;
            }
        }
        if (whole) {
            release = &candidate;
            break;
        }
        close_all(handles);
    }
    if (release == nullptr) {
        report_nothing_usable();
        return false;
    }

    const char* missing = nullptr;
#define SH_FFMPEG_RESOLVE(which, name)                                                                   \
    library.name = reinterpret_cast<decltype(library.name)>(handles[which].symbol(#name)); \
    if (library.name == nullptr && missing == nullptr) missing = #name;
    SH_FFMPEG_FUNCTIONS(SH_FFMPEG_RESOLVE)
#undef SH_FFMPEG_RESOLVE
    if (missing != nullptr) {
        SH_LOG_DEBUG(FMV, "[FMV] FFmpeg %s: %s is missing, so this is not a whole FFmpeg build; not used", release->name,
                     missing);
        close_all(handles);
        return false;
    }

    // A file named for one major that holds another would undo the table above.
    const std::array<unsigned, kLibraryCount> reported = {library.avformat_version(), library.avcodec_version(),
                                                          library.avutil_version(), library.swscale_version(),
                                                          library.swresample_version()};
    for (std::size_t i = 0; i < kLibraryCount; ++i) {
        if (static_cast<int>(reported[i] >> 16) != release->major[i]) {
            SH_LOG_DEBUG(FMV, "[FMV] FFmpeg %s: %s is named for major %d and reports %u; not used", release->name,
                         kLibraryNames[i], release->major[i], reported[i] >> 16);
            close_all(handles);
            return false;
        }
    }
    SH_LOG_DEBUG(FMV, "[FMV] FFmpeg %s loaded: avformat %u.%u, avcodec %u.%u, avutil %u.%u, swscale %u.%u, swresample %u.%u",
                 release->name, reported[Format] >> 16, (reported[Format] >> 8) & 0xFF, reported[Codec] >> 16,
                 (reported[Codec] >> 8) & 0xFF, reported[Util] >> 16, (reported[Util] >> 8) & 0xFF, reported[Scale] >> 16,
                 (reported[Scale] >> 8) & 0xFF, reported[Resample] >> 16, (reported[Resample] >> 8) & 0xFF);
    return true;
}

}  // namespace

const FfmpegLibrary* FfmpegLibrary::get() {
    static FfmpegLibrary library;
    static const bool loaded = load_into(library);
    return loaded ? &library : nullptr;
}

}  // namespace sh::fmv

#endif  // SH_FMV_FFMPEG
