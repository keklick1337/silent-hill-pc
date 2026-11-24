// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/app_bundle.h"

#include "engine/app/pc_paths.h"
#include "engine/version/engine_version.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#ifdef __ANDROID__
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_system.h>
#include <android/log.h>
#include <unistd.h>

#include <thread>
#endif

namespace sh::app {

namespace {

namespace fs = std::filesystem;

// Only a macOS bundle has a Resources folder to copy from.
#ifdef __APPLE__
constexpr std::string_view kMacOsDir = ".app/Contents/MacOS/";
constexpr const char* kStampFile = ".bundle-files";

bool same_bytes(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    return fa && fb &&
           std::equal(std::istreambuf_iterator<char>(fa), std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(fb), std::istreambuf_iterator<char>());
}

// What the root already has and what the user changed stays: config.cfg only when there is none, and nothing is
// deleted. A file of the bundle replaces an older one of the same name, as the build's copy of gamedata/ does.
// The model of sh-upscale is read in place (texture_upscale_run.cpp).
bool copy_resources(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(from, ec), end; it != end && !ec; it.increment(ec)) {
        const fs::path rel = it->path().lexically_relative(from);
        if (*rel.begin() == "upscale-models") {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        const fs::path target = to / rel;
        if (it->is_directory(ec)) {
            fs::create_directories(target, ec);
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        if (rel == "config.cfg" && fs::exists(target, ec)) continue;
        if (fs::exists(target, ec) && same_bytes(it->path(), target)) continue;
        if (!fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec)) {
            std::fprintf(stderr, "app bundle: cannot copy %s: %s\n", target.c_str(), ec.message().c_str());
            return false;
        }
    }
    if (ec) std::fprintf(stderr, "app bundle: cannot read %s: %s\n", from.c_str(), ec.message().c_str());
    return !ec;
}
#endif

#ifdef __ANDROID__
// An app's stdout and stderr go nowhere: a thread passes them on to the system log (adb logcat), line by line.
void route_output_to_system_log() {
    static int pipe_fds[2] = {-1, -1};
    if (pipe_fds[0] >= 0 || pipe(pipe_fds) != 0) return;
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(pipe_fds[1], STDOUT_FILENO);
    dup2(pipe_fds[1], STDERR_FILENO);
    std::thread([] {
        std::string line;
        char chunk[512];
        for (;;) {
            const ssize_t n = read(pipe_fds[0], chunk, sizeof(chunk));
            if (n <= 0) return;
            for (ssize_t i = 0; i < n; ++i) {
                if (chunk[i] == '\n' || line.size() >= 1000) {
                    __android_log_write(ANDROID_LOG_INFO, "SilentHillPC", line.c_str());
                    line.clear();
                    if (chunk[i] == '\n') continue;
                }
                line += chunk[i];
            }
        }
    }).detach();
}
#endif

}  // namespace

bool use_bundle_root() {
#ifdef __ANDROID__
    // The activity (android/, SilentHillActivity.java) unpacks the base data of the APK into the app's own storage
    // once per build and names that folder in SH_ANDROID_ROOT; config.cfg, the import, saves and mods live there too.
    route_output_to_system_log();
    // Every window of the game is landscape, the picker's and the progress window's too.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    const char* named = std::getenv("SH_ANDROID_ROOT");
    const char* root = named != nullptr && named[0] != '\0' ? named : SDL_GetAndroidInternalStoragePath();
    if (root == nullptr || root[0] == '\0') {
        std::fprintf(stderr, "android: no storage for the game's files\n");
        return false;
    }
    std::string dir = root;
    if (dir.back() != '/') dir += '/';
    PcPaths_SetRoot(dir.c_str(), dir.c_str());
    if (chdir(dir.c_str()) != 0) std::fprintf(stderr, "android: cannot enter %s\n", dir.c_str());
    return true;
#endif
#ifdef __APPLE__
    const std::string bin = PcPaths_BinDir();
    if (bin.size() < kMacOsDir.size() || bin.compare(bin.size() - kMacOsDir.size(), kMacOsDir.size(), kMacOsDir) != 0)
        return true;
    const fs::path resources = fs::path(bin).parent_path().parent_path() / "Resources";
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        std::fprintf(stderr, "app bundle: no HOME; the game stays inside the bundle\n");
        return false;
    }
    const fs::path root = fs::path(home) / "Library" / "Application Support" / "Silent Hill PC";
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        std::fprintf(stderr, "app bundle: cannot make %s: %s\n", root.c_str(), ec.message().c_str());
        return false;
    }
    // Copied once per build of the bundle, not at every start.
    const std::string build = sh::version::full();
    std::string stamped;
    if (std::ifstream in(root / kStampFile); in) std::getline(in, stamped);
    if (stamped != build) {
        if (!copy_resources(resources, root)) return false;
        std::ofstream(root / kStampFile) << build << '\n';
    }
    PcPaths_SetRoot((root.string() + "/").c_str(), (resources.string() + "/").c_str());
#endif
    return true;
}

}  // namespace sh::app
