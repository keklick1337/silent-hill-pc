// sh-music: renders one KDT song with its VAB bank to a WAV, the way the importer's "music" converter does, with
// the render settings open for experiments. Every music layer of the song is open
// unless --base is given; --channel holds one channel at a volume.
//
//   sh-music SONG.KDT BANK.VAB OUT.wav [--base] [--song-volume N] [--reverb-depth N] [--reverb-mode N]
//            [--channel CH=VOL]... [--events FILE]
#include "engine/audio/music_layers.h"
#include "engine/audio/music_render.h"
#include "engine/audio/synth.h"
#include "engine/audio/song.h"
#include "engine/audio/sound_bank.h"
#include "formats/convert/audio/wav.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

int usage() {
    std::fprintf(stderr,
                 "usage: sh-music SONG.KDT BANK.VAB OUT.wav [--base] [--song-volume N] [--reverb-depth N] [--reverb-mode N]\n"
                 "                [--channel CH=VOL]...\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        return usage();
    }
    sh::audio::MusicOptions options;
    bool base = false;
    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--base") {
            base = true;
            continue;
        }
        if (i + 1 >= argc) {
            return usage();
        }
        const std::string value = argv[++i];
        if (arg == "--song-volume") {
            options.song_volume = std::atoi(value.c_str());
        } else if (arg == "--reverb-depth") {
            options.reverb_depth = std::atoi(value.c_str());
        } else if (arg == "--reverb-mode") {
            options.reverb_mode = std::atoi(value.c_str());
        } else if (arg == "--events") {
            // Lines "seconds channel volume", channel -1 for the song volume.
            std::ifstream f(value);
            sh::audio::VolumeEvent e;
            while (f >> e.seconds >> e.channel >> e.volume) {
                options.events.push_back(e);
            }
        } else if (arg == "--channel") {
            const auto eq = value.find('=');
            if (eq == std::string::npos) {
                return usage();
            }
            options.channel_volume[static_cast<std::size_t>(std::atoi(value.c_str()) & 15)] =
                std::atoi(value.c_str() + eq + 1);
        } else {
            return usage();
        }
    }
    if (!base) {
        std::string name = argv[1];
        name = name.substr(name.find_last_of("/\\") + 1);
        name = name.substr(0, name.find('.'));
        for (char& c : name) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (const auto layers = sh::audio::song_layers(name)) {
            sh::audio::open_layers(options, *layers);
        }
    }
    const auto kdt = read_file(argv[1]);
    const auto vab_raw = read_file(argv[2]);
    auto bank = sh::audio::parse_vab(vab_raw);
    if (!bank) {
        std::fprintf(stderr, "%s: %s\n", argv[2], bank.error().c_str());
        return 1;
    }
    auto song = sh::audio::parse_kdt(kdt);
    if (!song) {
        std::fprintf(stderr, "%s: %s\n", argv[1], song.error().c_str());
        return 1;
    }
    auto music = sh::audio::render_music(song.value(), bank.value(), options);
    if (!music) {
        std::fprintf(stderr, "%s: %s\n", argv[1], music.error().c_str());
        return 1;
    }
    const auto& m = music.value();
    sh::convert::audio::Wav wav;
    wav.channels = 2;
    wav.sample_rate = sh::audio::kOutputRate;
    wav.samples = m.pcm;
    if (m.loop_start && m.loop_end) {
        wav.sampler = sh::convert::audio::WavSampler{};
        wav.sampler->loop = sh::convert::audio::WavLoop{*m.loop_start, *m.loop_end - 1};
    }
    auto bytes = sh::convert::audio::write_wav(wav);
    if (!bytes) {
        std::fprintf(stderr, "%s\n", bytes.error().c_str());
        return 1;
    }
    std::ofstream(argv[3], std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.value().data()), static_cast<std::streamsize>(bytes.value().size()));
    std::printf("%s: %u frames (%.2f s), loop %d..%d, spill %u, voices peak %u, ticks %llu\n", argv[3], m.frames(),
                m.frames() / 44100.0, m.loop_start ? static_cast<int>(*m.loop_start) : -1,
                m.loop_end ? static_cast<int>(*m.loop_end) : -1, m.spill_frames, m.voices_peak,
                static_cast<unsigned long long>(m.timer_ticks));
    return 0;
}
