#include "engine/app/texture_upscale_run.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/app/game_data_dir.h"
#include "engine/app/loader/upscaled_pictures.h"
#include "engine/app/pc_paths.h"
#include "engine/assets/texture_upscale.h"
#include "engine/assets/upscale_cache.h"
#include "engine/log/sh_log.h"
#include "engine/mods/registry.h"
#include "engine/vfs/vfs_data_json.h"
#include "render/scene/scene.h"
#include "render/skinning/gltf_chara.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace sh::app {

namespace {

namespace fs = std::filesystem;

int g_running_scale = 0;  // the scale of the pass this game started, while it runs
int g_drawn = 0;          // the scale the readers were last told to draw (0: the originals)
bool g_available = true;  // the tool is beside the game
std::string g_status;
bool g_start_pending = false;
std::chrono::steady_clock::time_point g_start_at{};

#ifdef _WIN32
HANDLE g_process = nullptr;

bool still_running() {
    if (g_process == nullptr) return false;
    if (WaitForSingleObject(g_process, 0) == WAIT_TIMEOUT) return true;
    CloseHandle(g_process);
    g_process = nullptr;
    return false;
}

void stop() {
    if (g_process == nullptr) return;
    TerminateProcess(g_process, 1);
    WaitForSingleObject(g_process, 2000);
    CloseHandle(g_process);
    g_process = nullptr;
    g_running_scale = 0;
}

bool spawn(const fs::path& exe, const std::vector<std::wstring>& args) {
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    for (const std::wstring& a : args) cmd += L" \"" + a + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.wstring().c_str(), cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);
    g_process = pi.hProcess;
    return true;
}
#else
pid_t g_process = 0;

bool still_running() {
    if (g_process <= 0) return false;
    if (waitpid(g_process, nullptr, WNOHANG) == 0) return true;
    g_process = 0;
    return false;
}

void stop() {
    if (g_process <= 0) return;
    kill(g_process, SIGTERM);
    waitpid(g_process, nullptr, 0);
    g_process = 0;
    g_running_scale = 0;
}

bool spawn(const fs::path& exe, const std::vector<std::string>& args) {
    std::vector<std::string> all{exe.string()};
    all.insert(all.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& a : all) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), environ) != 0) return false;
    g_process = pid;
    return true;
}
#endif

fs::path tool_path() {
#ifdef _WIN32
    return fs::path(PcPaths_ExeDir()) / "sh-upscale.exe";
#else
    return fs::path(PcPaths_ExeDir()) / "sh-upscale";
#endif
}

// The cache key of a file an active mod has ("convert/textures/BG/A.png", a .dds of it, or the disc's
// "BG/A.TIM"), or nothing for any other file.
std::optional<std::string> replaced_key(std::string path) {
    for (char& c : path)
        if (c == '\\') c = '/';
    const std::size_t dot = path.rfind('.');
    if (dot == std::string::npos) return std::nullopt;
    std::string ext = path.substr(dot);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".dds") return sh::upscale::texture_key(path.substr(0, dot) + ".png");
    if (ext == ".tim" && path.find('/') != std::string::npos && path.find('/') == path.rfind('/'))
        return sh::upscale::texture_key(std::string(sh::upscale::kTexturesDir) + path.substr(0, dot) + ".png");
    return sh::upscale::texture_key(path);
}

// Writes `text` to `file` unless it holds it already; true when it changed.
bool write_if_changed(const fs::path& file, const std::string& text) {
    {
        std::ifstream f(file, std::ios::binary);
        if (f && std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()) == text)
            return false;
    }
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f << text;
    return true;
}

// The tool's inputs that depend on the mods: the lettering list with their patches and the pictures they replace
// (those are drawn as the mod made them, so the tool spends no time on them). True when either changed.
bool write_tool_inputs(const fs::path& gamedata) {
    const fs::path root = sh::upscale::cache_root(gamedata);
    const std::optional<std::string> lettering =
        ShVfs_ReadDataJsonText("config/texture_upscale.json", gamedata.string().c_str());
    std::set<std::string> replaced;
    sh::mods::Registry& reg = sh::mods::registry();
    for (const sh::mods::ResolvedMod* mod : reg.active())
        for (const sh::mods::ModFile& f : reg.files(mod->candidate.manifest.id))
            if (std::optional<std::string> key = replaced_key(f.path)) replaced.insert(std::move(*key));
    std::string skip;
    for (const std::string& k : replaced) skip += k + "\n";
    const bool a = write_if_changed(root / sh::upscale::kLetteringInput, lettering.value_or("{}"));
    const bool b = write_if_changed(root / sh::upscale::kSkipInput, skip);
    return a || b;
}

bool start(int scale) {
    const fs::path gamedata = PcPort_GetGameDataPath();
    const fs::path exe = tool_path();
    std::error_code ec;
    if (!fs::exists(exe, ec)) {
        SH_LOG_WARN(GAME,
                    "texture upscaling: %s is not there (the build found no ncnn: tools/ncnn/build.sh); the original "
                    "textures stay",
                    exe.string().c_str());
        return false;
    }
    const fs::path root = sh::upscale::cache_root(gamedata);
    const fs::path log = root / "upscale.log";
    const fs::path lettering = root / sh::upscale::kLetteringInput;
    const fs::path skip = root / sh::upscale::kSkipInput;
    const std::string scale_text = std::to_string(scale);
#ifdef _WIN32
    const bool started =
        spawn(exe, {L"--gamedata", gamedata.wstring(), L"--scale", std::wstring(scale_text.begin(), scale_text.end()),
                    L"--log", log.wstring(), L"--lettering", lettering.wstring(), L"--skip", skip.wstring()});
#else
    const bool started = spawn(exe, {"--gamedata", gamedata.string(), "--scale", scale_text, "--log", log.string(),
                                     "--lettering", lettering.string(), "--skip", skip.string()});
#endif
    if (!started) {
        SH_LOG_WARN(GAME, "texture upscaling: %s did not start; the original textures stay", exe.string().c_str());
        return false;
    }
    g_running_scale = scale;
    SH_LOG_INFO(GAME, "texture upscaling x%d runs in the background (%s)", scale, log.string().c_str());
    return true;
}

// Pictures of textures in use that the cache now has better: each reader takes its own again, a few per look so
// the decoding and the upload never stall a frame for long.
unsigned long long g_swaps = 0;

void swap_improved() {
    constexpr std::size_t kPerLook = 4;
    const std::vector<std::string> keys = sh::upscale::improved(kPerLook);
    if (keys.empty()) return;
    g_swaps += keys.size();
    const std::set<std::string> wanted(keys.begin(), keys.end());
    const auto match = [&wanted](const std::string& path) {
        const std::optional<std::string> key = sh::upscale::texture_key(path);
        return key && wanted.count(*key) != 0;
    };
    sh::scene::refresh_textures(match);
    sh::gltf_chara::refresh_textures(match);
    refresh_upscaled_pictures(wanted);
}

}  // namespace

unsigned long long texture_upscale_swaps() { return g_swaps; }

bool apply_texture_upscale(int scale) {
    sh::upscale::set_gamedata(PcPort_GetGameDataPath());
    sh::upscale::set_scale(scale);
    scale = sh::upscale::scale();
    std::error_code ec;
    g_available = fs::exists(tool_path(), ec);
    if (still_running() && g_running_scale != scale) stop();
    // Another scale: everything loaded is read again (the path of a data reload); within one scale pictures change
    // one by one (swap_improved).
    if (scale != g_drawn) {
        g_drawn = scale;
        sh::scene::forget_assets();
    }
    if (scale == 0 || still_running()) return true;
    // A finished cache is checked again when the lettering list or the mods' pictures changed.
    const bool inputs_changed = write_tool_inputs(PcPort_GetGameDataPath());
    if (sh::upscale::progress(scale).complete && !inputs_changed) return true;
    // Started from the poll once the value has held for a moment: stepping through the Options values would
    // otherwise start and kill a pass per step.
    g_start_at = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    g_start_pending = true;
    return g_available;
}

void poll_texture_upscale() {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point next{}, next_wanted{};
    const Clock::time_point now = Clock::now();
    if (now < next) return;
    next = now + std::chrono::milliseconds(250);
    const int scale = sh::upscale::scale();
    if (g_running_scale != 0 && !still_running()) {
        if (!sh::upscale::progress(g_running_scale).complete) {
            SH_LOG_WARN(GAME, "texture upscaling x%d ended unfinished (upscale.log); the original textures stay",
                        g_running_scale);
        }
        g_running_scale = 0;
    }
    if (g_start_pending && now >= g_start_at) {
        g_start_pending = false;
        if (scale != 0 && !still_running()) start(scale);
    }
    g_status.clear();
    if (scale == 0) return;
    swap_improved();
    if (g_running_scale == scale && now >= next_wanted) {
        next_wanted = now + std::chrono::seconds(1);
        sh::upscale::publish_wanted();
    }
    const sh::upscale::Progress p = sh::upscale::progress(scale);
    if (p.complete) return;
    if (p.total > 0)
        g_status = std::to_string(p.done) + "/" + std::to_string(p.total);
    else if (g_running_scale == scale)
        g_status = "...";
}

const char* texture_upscale_status() { return g_status.c_str(); }

bool texture_upscale_available() { return g_available; }

}  // namespace sh::app
