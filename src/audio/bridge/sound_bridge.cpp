#include "audio/bridge/sound_bridge.h"

#include "engine/audio/sound_api.h"
#include "engine/log/sh_log.h"
#include "engine/vfs/vfs_c.h"

#include "engine/audio/output/device/platform_sinks.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

unsigned char* read_converted(const char* relative, size_t* size) {
    char path[1024];
    if (!ShVfs_FindConverted(relative, path, sizeof path)) {
        return nullptr;
    }
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        return nullptr;
    }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    auto* data = static_cast<unsigned char*>(std::malloc(n > 0 ? static_cast<size_t>(n) : 1));
    const bool ok = data && (n <= 0 || std::fread(data, 1, static_cast<size_t>(n), f) == static_cast<size_t>(n));
    std::fclose(f);
    if (!ok) {
        std::free(data);
        return nullptr;
    }
    *size = n > 0 ? static_cast<size_t>(n) : 0;
    return data;
}

// "1ST/BASE.VAB" -> "BASE", "SND/B.KDT" -> "SND/B".
std::string path_at(int lba) {
    char path[256];
    if (!ShVfs_PathAtLba(lba, path, sizeof path)) {
        return {};
    }
    return path;
}

std::string without_extension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of('/');
    return dot != std::string::npos && (slash == std::string::npos || dot > slash) ? path.substr(0, dot) : path;
}

bool g_started = false;

// An import older than the engine's sound system lacks the songs or the full tone tables; it plays with gaps.
void check_import() {
    char path[1024];
    if (!ShVfs_FindConverted("audio/seq/SND/B.kdt.json", path, sizeof path)) {
        SH_LOG_WARN(AUDIO, "gamedata has no converted songs (convert/audio/seq): music is silent; re-import with --reimport");
    }
    size_t size = 0;
    unsigned char* bank = read_converted("audio/vab/BASE.vab.json", &size);
    if (!bank) {
        SH_LOG_WARN(AUDIO, "gamedata has no converted sound banks (convert/audio/vab): effects are silent; re-import");
        return;
    }
    const std::string text(reinterpret_cast<const char*>(bank), size);
    std::free(bank);
    if (text.find("\"version\": 2") == std::string::npos) {
        SH_LOG_WARN(AUDIO, "convert/audio/vab is from an older import (tone tables cut at the tone count); re-import");
    }
}

}  // namespace

namespace sh::audio {

output::OutputSettings load_output_settings(const char* config_path) {
    std::string text;
    if (std::FILE* file = std::fopen(config_path, "rb")) {
        char block[4096];
        for (std::size_t got; (got = std::fread(block, 1, sizeof block, file)) > 0;) {
            text.append(block, got);
        }
        std::fclose(file);
    }
    return output::parse_output_settings(text);
}

bool start_sound_output(const output::OutputSettings& settings) {
    ShSound_SetReader(read_converted);
    check_import();
    if (g_started) {
        return true;
    }
    output::OutputDevice& device = output::game_output();
    const output::OutputResult result = device.open(settings, [](std::int16_t* stereo, std::uint32_t frames) {
        ShSound_Render(stereo, frames);
        return frames;
    });
    g_started = output::playing(result);
    const output::OutputStatus status = device.status();
    if (!g_started) {
        SH_LOG_ERROR(AUDIO, "sound output failed to start: %s (%s)", output::name_of(result), status.note.c_str());
        return false;
    }
    if (status.fallbacks != 0) {
        SH_LOG_INFO(AUDIO, "sound output: %s (%s)", status.note.c_str(), output::name_of(status.fallback_reason));
    }
    SH_LOG_INFO(AUDIO, "sound output: %s, %u Hz, %s", output::name_of(status.sink), status.device_rate,
                status.device_name.c_str());
    return true;
}

}  // namespace sh::audio

extern "C" int ShSoundBridge_LoadBankAt(int slot, int lba) {
    std::string path = path_at(lba);
    if (path.empty()) {
        SH_LOG_WARN(AUDIO, "no file at sector %d for bank slot %d", lba, slot);
        return 0;
    }
    std::string name = without_extension(path);
    if (const std::size_t slash = name.find_last_of('/'); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    if (!ShSound_LoadBank(slot, name.c_str())) {
        SH_LOG_WARN(AUDIO, "bank %s (%s) did not load into slot %d", name.c_str(), path.c_str(), slot);
        return 0;
    }
    SH_LOG_DEBUG(AUDIO, "bank %s in slot %d", name.c_str(), slot);
    return 1;
}

extern "C" int ShSoundBridge_OpenSongAt(int lba) {
    const std::string path = path_at(lba);
    if (path.empty() || !ShSound_OpenSong(without_extension(path).c_str())) {
        SH_LOG_WARN(AUDIO, "song at sector %d (%s) did not load", lba, path.c_str());
        return 0;
    }
    SH_LOG_DEBUG(AUDIO, "song %s", path.c_str());
    return 1;
}

extern "C" int ShSoundBridge_TrackAt(int lba, char* out, unsigned out_size) {
    return ShVfs_PathAtLba(lba, out, out_size);
}
