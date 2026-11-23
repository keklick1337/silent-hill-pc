#include "engine/audio/effects.h"
#include "engine/audio/engine.h"
#include "engine/audio/music_converter.h"
#include "engine/audio/music_layers.h"
#include "engine/audio/music_render.h"
#include "engine/audio/sequencer.h"
#include "engine/audio/song.h"
#include "engine/audio/sound_bank.h"
#include "engine/audio/synth.h"
#include "engine/audio/voice_stream.h"
#include "formats/convert/audio/audio_converter.h"
#include "formats/convert/audio/wav.h"
#include "framework/sh_disc.h"
#include "framework/sh_test.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using sh::audio::KeyStatus;
using sh::audio::Synth;

// Two ADPCM blocks of a constant 0x7000: the first starts the loop, the second ends it and repeats.
Bytes looping_sample() {
    Bytes s(32, 0x77);
    s[0] = 0x00;
    s[1] = 0x04;
    s[16] = 0x00;
    s[17] = 0x03;
    return s;
}

// A one-program bank: tone 0 plays sample 1 over the whole keyboard, root key 60, the given envelope.
Bytes synthetic_vab(std::uint16_t adsr1, std::uint16_t adsr2, std::uint8_t mode = 0) {
    const Bytes sample = looping_sample();
    Bytes b(0x820 + 512 + 512, 0);
    b[0] = 'p';
    b[1] = 'B';
    b[2] = 'A';
    b[3] = 'V';
    b[0x12] = 1;  // one tone table
    b[0x14] = 1;
    b[0x16] = 1;  // one sample
    b[0x18] = 127;
    b[0x19] = 64;
    b[32 + 0] = 1;    // program 0: one tone
    b[32 + 1] = 127;  // volume
    b[32 + 4] = 64;   // pan
    std::uint8_t* t = &b[0x820];
    t[1] = mode;
    t[2] = 127;
    t[3] = 64;
    t[4] = 60;
    t[6] = 0;
    t[7] = 127;
    t[12] = 2;  // bend range 2 semitones
    t[13] = 2;
    t[16] = static_cast<std::uint8_t>(adsr1);
    t[17] = static_cast<std::uint8_t>(adsr1 >> 8);
    t[18] = static_cast<std::uint8_t>(adsr2);
    t[19] = static_cast<std::uint8_t>(adsr2 >> 8);
    t[22] = 1;  // sample 1
    b[0x820 + 512 + 2] = static_cast<std::uint8_t>(sample.size() / 8);  // size entry of sample 1
    b.insert(b.end(), sample.begin(), sample.end());
    const auto size = static_cast<std::uint32_t>(b.size());
    b[12] = static_cast<std::uint8_t>(size);
    b[13] = static_cast<std::uint8_t>(size >> 8);
    b[14] = static_cast<std::uint8_t>(size >> 16);
    return b;
}

// KDT1 with one track. `body` is the track after its first delta time.
Bytes synthetic_kdt(const Bytes& body) {
    Bytes track = {0x00};
    track.insert(track.end(), body.begin(), body.end());
    Bytes b = {'K', 'D', 'T', '1', 0, 0, 0, 0, 0xE0, 0x01, 0, 0, 1, 0, 0, 0,
               static_cast<std::uint8_t>(track.size()), static_cast<std::uint8_t>(track.size() >> 8)};
    b.insert(b.end(), track.begin(), track.end());
    b[4] = static_cast<std::uint8_t>(b.size());
    b[5] = static_cast<std::uint8_t>(b.size() >> 8);
    return b;
}

// The looping sample decoded: 56 frames of 0x7000, looping from the start.
std::shared_ptr<const sh::audio::Sample> looping_pcm() {
    auto s = std::make_shared<sh::audio::Sample>();
    s->pcm.assign(56, 0x7000);
    s->loop_start = 0;
    return s;
}

// Renders frames one at a time and returns the envelope after each.
std::vector<int> envelope_run(Synth& spu, int voice, int frames) {
    std::vector<int> out;
    std::int16_t pcm[2];
    for (int i = 0; i < frames; ++i) {
        spu.render(pcm, 1);
        out.push_back(spu.envelope(voice));
    }
    return out;
}

Synth& voice_with(Synth& spu, std::uint16_t adsr1, std::uint16_t adsr2) {
    spu.set_sample(0, looping_pcm());
    spu.set_pitch(0, 0x1000);
    spu.set_volume(0, 0x3FFF, 0x3FFF);
    spu.set_adsr(0, adsr1, adsr2);
    spu.set_main_volume(0x3FFF, 0x3FFF);
    return spu;
}

}  // namespace

SH_TEST(AudioNoteToPitchFollowsLibsdTable) {
    SH_CHECK(sh::audio::note_to_pitch(60, 0, 60, 0) == 0x1000);
    SH_CHECK(sh::audio::note_to_pitch(72, 0, 60, 0) == 0x2000);
    SH_CHECK(sh::audio::note_to_pitch(48, 0, 60, 0) == 0x0800);
    SH_CHECK(sh::audio::note_to_pitch(61, 0, 60, 0) == 0x10F3);  // PitchTbl[1][0]
    SH_CHECK(sh::audio::note_to_pitch(60, 1, 60, 0) == 0x1001);  // PitchTbl[0][1]
    SH_CHECK(sh::audio::note_to_pitch(59, 0, 60, 0) == 0x0F1A);  // PitchTbl[11][0] >> 1
    // The fine tune adds to the cents; 128 cents carry into the next semitone.
    SH_CHECK(sh::audio::note_to_pitch(60, 64, 60, 64) == 0x10F3);
}

SH_TEST(AudioAdsrLinearAttackAndFastRelease) {
    Synth spu;
    // Attack rate 0 (shift 0, step +7 << 11), decay shift 15, sustain level 15, sustain frozen (rate 0x7F),
    // release shift 0 linear.
    voice_with(spu, 0x00FF, 0x1FC0);
    spu.key_on(1);
    SH_CHECK(spu.key_status(0) == KeyStatus::OnEnvOff);  // nothing rendered yet: the level is still 0
    const auto env = envelope_run(spu, 0, 4);
    SH_CHECK(env[0] == 14336 && env[1] == 28672 && env[2] == 0x7FFF);
    SH_CHECK(env[3] == 0x7FFF);  // decay to sustain level 0x7FFF, then frozen
    SH_CHECK(spu.key_status(0) == KeyStatus::On);
    spu.key_off(1);
    SH_CHECK(spu.key_status(0) == KeyStatus::OffEnvOn);
    const auto rel = envelope_run(spu, 0, 3);
    SH_CHECK(rel[0] == 0x7FFF - 16384 && rel[1] == 0);
    SH_CHECK(spu.key_status(0) == KeyStatus::Off);
    SH_CHECK(!spu.voice_active(0));
}

SH_TEST(AudioAdsrSlowRatesAndFrozenRate) {
    Synth spu;
    // Attack shift 12 step 0 (rate 48): +7 every 2 samples.
    voice_with(spu, static_cast<std::uint16_t>(48 << 8 | 0xF), 0x1FC0);
    spu.key_on(1);
    const auto env = envelope_run(spu, 0, 6);
    SH_CHECK(env[0] == 0 && env[1] == 7 && env[2] == 7 && env[3] == 14 && env[5] == 21);

    Synth frozen;
    voice_with(frozen, 0x7F0F, 0x1FC0);  // attack rate 0x7F never steps
    frozen.key_on(1);
    const auto none = envelope_run(frozen, 0, 100);
    SH_CHECK(none.back() == 0);
    SH_CHECK(frozen.key_status(0) == KeyStatus::OnEnvOff);
}

SH_TEST(AudioAdsrExponentialDecayAndSustainLevel) {
    Synth spu;
    // Instant attack, decay shift 0 (exponential -8 << 11 scaled by the level), sustain level 7 = 0x4000.
    voice_with(spu, 0x0007, 0x1FC0);
    spu.key_on(1);
    const auto env = envelope_run(spu, 0, 12);
    SH_CHECK(env[2] == 0x7FFF);
    SH_CHECK(env[3] == 0x7FFF + ((-16384 * 0x7FFF) >> 15));
    SH_CHECK(env.back() <= 0x4000 && env.back() > 0x3000);
}

SH_TEST(AudioEndWithoutRepeatSilencesTheVoice) {
    Synth spu;
    auto one = std::make_shared<sh::audio::Sample>();
    one->pcm.assign(28, 0x7000);  // no loop
    spu.set_sample(0, one);
    spu.set_pitch(0, 0x1000);
    spu.set_volume(0, 0x3FFF, 0x3FFF);
    spu.set_adsr(0, 0x000F, 0x1FC0);
    spu.set_main_volume(0x3FFF, 0x3FFF);
    spu.key_on(1);
    std::vector<std::int16_t> pcm(2 * 40);
    spu.render(pcm.data(), 40);
    SH_CHECK(!spu.voice_active(0));
    // The library still counts it as keyed on: the automatic key off has to release it.
    SH_CHECK(spu.key_status(0) == KeyStatus::OnEnvOff);
    SH_CHECK(pcm[2 * 10] != 0 && pcm[2 * 39] == 0);
}

SH_TEST(AudioSequencerTempoNotesAndLoop) {
    // channel 0, program 0, tempo c = 63 (128 per tick), loop start, note 60 for 480 ticks, loop end, end.
    const Bytes kdt = synthetic_kdt({0xC6, 0x00, 0x00, 0xC9, 0x00, 0x00, 0xC7, 0x3F, 0x00, 0xE3, 0x14, 0x00,
                                     60, 100, 0x83, 0x60, 0xCA, 0x00, 0xE3, 0x9E, 0xFF, 0x00});
    auto bank = sh::audio::parse_vab(synthetic_vab(0x000F, 0x1FC0));
    SH_CHECK(static_cast<bool>(bank));
    if (!bank) {
        return;
    }
    Synth spu;
    spu.set_main_volume(0x3FFF, 0x3FFF);
    sh::audio::Sequencer seq(spu);
    seq.open_bank(bank.value());
    auto song = sh::audio::parse_kdt(kdt);
    SH_CHECK(static_cast<bool>(song));
    SH_CHECK(seq.play(song.value()));
    int keyed_at = -1, released_at = -1;
    for (int tick = 0; tick < 2000 && seq.loop_ends() == 0; ++tick) {
        seq.timer_tick();
        std::int16_t pcm[2];
        spu.render(pcm, 1);
        if (keyed_at < 0 && spu.voice_active(0)) {
            keyed_at = tick;
        }
        if (keyed_at >= 0 && released_at < 0 && spu.key_status(0) != KeyStatus::On) {
            released_at = tick;
        }
    }
    SH_CHECK(seq.loop_starts() == 1 && seq.loop_ends() == 1);
    // 480 song ticks are 120 sequencer ticks; at tempo 128 one comes every second interrupt.
    std::printf("  keyed at %d, released at %d\n", keyed_at, released_at);
    SH_CHECK(keyed_at >= 0 && released_at - keyed_at >= 238 && released_at - keyed_at <= 242);
    SH_CHECK(spu.pitch(0) == 0x1000);
}

SH_TEST(AudioSequencerMutedChannelIsNotKeyed) {
    const Bytes kdt = synthetic_kdt({0xC6, 0x00, 0x00, 0x87, 0x00, 0x00, 60, 100, 0x10, 0xCA, 0x00, 0xFF, 0x00});
    auto bank = sh::audio::parse_vab(synthetic_vab(0x000F, 0x1FC0));
    Synth spu;
    sh::audio::Sequencer seq(spu);
    seq.open_bank(bank.value());
    SH_CHECK(seq.play(sh::audio::parse_kdt(kdt).value()));
    for (int i = 0; i < 100; ++i) {
        seq.timer_tick();
    }
    for (int v = 0; v < sh::audio::kVoices; ++v) {
        SH_CHECK(!spu.voice_active(v));
    }
}

SH_TEST(AudioRenderMusicLoopsAndSpills) {
    // Reverb on the tone (mode bit 2) so the spill carries the reverb tail too.
    const Bytes kdt = synthetic_kdt({0xC6, 0x00, 0x00, 0xC9, 0x00, 0x00, 0xC7, 0x3F, 0x00, 0xE3, 0x14, 0x00,
                                     60, 100, 0x83, 0x60, 0xCA, 0x00, 0xE3, 0x9E, 0xFF, 0x00});
    auto bank = sh::audio::parse_vab(synthetic_vab(0x000F, 0x1FCA, 4));
    const auto song = sh::audio::parse_kdt(kdt);
    const auto a = sh::audio::render_music(song.value(), bank.value());
    const auto b = sh::audio::render_music(song.value(), bank.value());
    SH_CHECK(a && b);
    if (!a || !b) {
        return;
    }
    SH_CHECK(a.value().pcm == b.value().pcm);
    SH_CHECK(a.value().loop_start && a.value().loop_end);
    const std::uint32_t length = *a.value().loop_end - *a.value().loop_start;
    std::printf("  loop %u..%u, spill %u\n", *a.value().loop_start, *a.value().loop_end, a.value().spill_frames);
    // 120 sequencer ticks at 2 interrupts each, 76.3 frames per interrupt.
    SH_CHECK(length > 18000 && length < 18700);
    SH_CHECK(a.value().spill_frames > 0 && a.value().spill_frames < 3 * 44100);
}

// The driver's voice allocation: free (Off) voices first, then idle ports; a sample that ends unlooped keeps its
// voice keyed until the automatic release frees it on the second slow tick.
SH_TEST(AudioEffectsAllocationAndAutoRelease) {
    auto bank = sh::audio::parse_vab(synthetic_vab(0x000F, 0x1FC0));
    SH_CHECK(static_cast<bool>(bank));
    if (!bank) {
        return;
    }
    auto shot = std::make_shared<sh::audio::SoundBank>(bank.value());
    auto once = std::make_shared<sh::audio::Sample>();
    once->pcm.assign(28, 0x7000);
    shot->samples[0] = once;
    Synth synth;
    sh::audio::Effects fx(synth);
    fx.set_bank(0, shot);
    SH_CHECK(fx.key_on_program(0, 0, 60, 0, 0x3FFF, 0x3FFF) == 0);
    SH_CHECK(fx.key_on_program(0, 0, 60, 0, 0x3FFF, 0x3FFF) == 1);
    SH_CHECK(fx.key_on_program(0, 1, 60, 0, 0x3FFF, 0x3FFF) == -1);  // a program without tones
    SH_CHECK(fx.key_on_voice(22, 0, 0, 0, 60, 0, 0x3FFF, 0x3FFF) == 22);
    SH_CHECK(fx.key_on_voice(22, 0, 0, 1, 60, 0, 0x3FFF, 0x3FFF) == -1);  // slot 1 has no sample
    std::vector<std::int16_t> pcm(2 * 64);
    synth.render(pcm.data(), 64);
    SH_CHECK(fx.key_status(0) == KeyStatus::OnEnvOff);
    fx.auto_release();
    SH_CHECK(fx.take_released_voices() == 0);
    fx.auto_release();
    SH_CHECK(fx.take_released_voices() == ((1u << 0) | (1u << 1) | (1u << 22)));
    SH_CHECK(fx.key_status(0) == KeyStatus::Off);
    // A released, silent voice is free again and taken first.
    SH_CHECK(fx.key_on_tone(0, 0, 0, 60, 0, 0x3FFF, 0x3FFF) == 0);
    SH_CHECK(fx.key_off_voice(0, true) == 0);
    SH_CHECK(fx.key_off_voice(0, true) == -1);
}

// A line of N source frames at 37.8 kHz lasts ceil(N * 44100 / 37800) output frames.
SH_TEST(AudioVoiceStreamLength) {
    auto pcm = std::make_shared<const std::vector<std::int16_t>>(2 * 4032, std::int16_t{1000});
    sh::audio::VoiceStream v;
    v.start(pcm, 2, 37800, 2016, 2016);
    SH_CHECK(v.output_frames_left() == 2352);
    std::vector<std::int32_t> l(2000, 0), r(2000, 0);
    v.mix(l.data(), r.data(), 2000, 0x8000, 0x8000);
    SH_CHECK(!v.drained() && v.output_frames_left() == 352);
    // The zigzag interpolator's gain is about 0.9 at low frequencies.
    SH_CHECK(l[100] > 850 && l[100] <= 1000 && r[100] == l[100]);
    v.mix(l.data(), r.data(), 2000, 0x8000, 0x8000);
    SH_CHECK(v.drained());
}

// A bank from its JSON and WAVs, as the importer writes it.
SH_TEST(AudioBankFromJson) {
    sh::convert::audio::Wav wav;
    wav.samples.assign(56, 0x7000);
    wav.sampler = sh::convert::audio::WavSampler{};
    wav.sampler->loop = sh::convert::audio::WavLoop{0, 55};
    const auto wav_bytes = sh::convert::audio::write_wav(wav).value();
    const std::string json = R"({"format": "silent-hill-vab", "bank": "T",
        "header": {"tone_tables": 1, "master_volume": 100, "master_pan": 64},
        "programs": [{"index": 0, "volume": 90, "pan": 64, "tone_count": 1, "tone_table": 0,
            "tones": [{"slot": 0, "sample": 1, "volume": 127, "pan": 64, "root_key": 60, "fine_tune": 0,
                       "key_min": 0, "key_max": 127, "mode": 4, "adsr1": "0x80FF", "adsr2": "0xDFF5"}]}],
        "samples": [{"number": 1, "file": "T/001.wav"}]})";
    std::string asked;
    auto bank = sh::audio::load_sound_bank(json, [&](std::string_view rel) {
        asked = std::string(rel);
        return sh::Result<std::vector<std::uint8_t>>::ok(wav_bytes);
    });
    SH_CHECK(static_cast<bool>(bank));
    if (!bank) {
        std::printf("  %s\n", bank.error().c_str());
        return;
    }
    SH_CHECK(asked == "T/001.wav");
    SH_CHECK(bank.value().master_volume == 100 && bank.value().programs[0].tones == 1);
    SH_CHECK(bank.value().tones.size() == 16 && bank.value().tones[0].adsr1 == 0x80FF);
    SH_CHECK(bank.value().sample(1) && bank.value().sample(1)->loop_start == 0u);
    SH_CHECK(bank.value().sample(1)->pcm.size() == 56);
}

SH_TEST(AudioSongLayersTable) {
    const auto b = sh::audio::song_layers("B");
    SH_CHECK(b && (*b)[1] == 2 && (*b)[5] == 4 && (*b)[11] == 1 && (*b)[0] == 0);
    SH_CHECK(!sh::audio::song_layers("NOT_A_SONG"));
    SH_CHECK(sh::audio::song_layers("Z4") && (*sh::audio::song_layers("Z4"))[10] == 7);
}

// Every bank of the disc, written by the importer and read back from its JSON and WAVs, is the bank the decoder
// sees in the VAB: the engine plays exactly what the disc holds. The same for every song.
SH_TEST(AudioEveryConvertedBankAndSongLoads) {
    const auto disc = sh::test::open_game_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const auto vab = sh::convert::audio::make_vab_converter();
    const auto kdt = sh::convert::audio::make_kdt_converter();
    std::size_t banks = 0, songs = 0;
    for (const auto& e : disc->table) {
        const bool is_vab = e.name.ends_with(".VAB"), is_kdt = e.name.ends_with(".KDT");
        if (!is_vab && !is_kdt) {
            continue;
        }
        const auto raw = disc->read(e.name);
        SH_CHECK(raw.has_value());
        if (!raw) {
            continue;
        }
        const auto out = (is_vab ? vab : kdt)->decode(*raw, e.name);
        SH_CHECK(static_cast<bool>(out));
        if (!out) {
            continue;
        }
        const auto file = [&](std::string_view suffix) -> const sh::convert::Output* {
            for (const auto& o : out.value()) {
                if (o.relative_path.ends_with(suffix)) {
                    return &o;
                }
            }
            return nullptr;
        };
        if (is_kdt) {
            const auto* json = file(".kdt.json");
            const auto song = json ? sh::audio::load_song(std::string_view(reinterpret_cast<const char*>(json->bytes.data()), json->bytes.size()))
                                   : sh::Result<sh::audio::Song>::fail("no json");
            SH_CHECK(static_cast<bool>(song) && !song.value().tracks.empty());
            songs += song ? 1 : 0;
            continue;
        }
        const auto* json = file(".vab.json");
        SH_CHECK(json != nullptr);
        if (!json) {
            continue;
        }
        auto loaded = sh::audio::load_sound_bank(
            std::string_view(reinterpret_cast<const char*>(json->bytes.data()), json->bytes.size()),
            [&](std::string_view rel) {
                const auto* o = file("/" + std::string(rel));
                return o ? sh::Result<std::vector<std::uint8_t>>::ok(o->bytes)
                         : sh::Result<std::vector<std::uint8_t>>::fail("missing");
            });
        const auto direct = sh::audio::parse_vab(*raw);
        SH_CHECK(loaded && direct);
        if (!loaded || !direct) {
            std::printf("  %s: %s\n", e.name.c_str(), loaded ? direct.error().c_str() : loaded.error().c_str());
            continue;
        }
        const auto& a = loaded.value();
        const auto& b = direct.value();
        bool same = a.master_volume == b.master_volume && a.master_pan == b.master_pan && a.tones.size() == b.tones.size();
        for (std::size_t p = 0; same && p < 128; ++p) {
            same = a.programs[p].tones == b.programs[p].tones && (b.programs[p].tones == 0 ||
                   (a.programs[p].volume == b.programs[p].volume && a.programs[p].pan == b.programs[p].pan));
        }
        for (std::size_t t = 0; same && t < a.tones.size(); ++t) {
            const auto& x = a.tones[t];
            const auto& y = b.tones[t];
            same = x.sample == y.sample && x.adsr1 == y.adsr1 && x.adsr2 == y.adsr2 && x.root_key == y.root_key &&
                   x.fine_tune == y.fine_tune && x.key_min == y.key_min && x.key_max == y.key_max &&
                   x.volume == y.volume && x.pan == y.pan && x.mode == y.mode;
        }
        for (int n = 1; same && n <= static_cast<int>(b.samples.size()); ++n) {
            const auto sa = a.sample(n), sb = b.sample(n);
            same = (!sa && !sb) || (sa && sb && sa->pcm == sb->pcm && sa->loop_start == sb->loop_start);
        }
        if (!same) {
            std::printf("  %s: the converted bank differs\n", e.name.c_str());
        }
        SH_CHECK(same);
        ++banks;
    }
    std::printf("  %zu banks, %zu songs\n", banks, songs);
    SH_CHECK(banks >= 90 && songs == 40);
}

SH_TEST(AudioEverySongOfTheImageRenders) {
    const auto disc = sh::test::open_game_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const auto conv = sh::audio::make_music_converter(disc->reader());
    std::size_t songs = 0, looped = 0;
    double seconds = 0;
    for (const auto& e : disc->table) {
        if (!e.name.ends_with(".KDT")) {
            continue;
        }
        const auto raw = disc->read(e.name);
        SH_CHECK(raw.has_value());
        if (!raw || !conv->accepts(*raw, e.name)) {
            continue;
        }
        const auto out = conv->decode(*raw, e.name);
        SH_CHECK(out && out.value().size() == 2);
        if (!out) {
            std::printf("  %s: %s\n", e.name.c_str(), out.error().c_str());
            continue;
        }
        ++songs;
        const auto wav = sh::convert::audio::read_wav(out.value()[0].bytes);
        SH_CHECK(wav && wav.value().channels == 2 && wav.value().frames() > 44100);
        if (wav) {
            seconds += static_cast<double>(wav.value().frames()) / 44100.0;
            looped += wav.value().sampler && wav.value().sampler->loop;
            const auto peak = std::max_element(wav.value().samples.begin(), wav.value().samples.end(),
                                               [](std::int16_t x, std::int16_t y) { return std::abs(x) < std::abs(y); });
            SH_CHECK(std::abs(*peak) > 1000);  // every song is audible with its layers open
        }
    }
    std::printf("  music: %zu songs, %zu looped, %.0f s rendered\n", songs, looped, seconds);
    SH_CHECK(songs == 40 && looped == 40);
}
