#include "engine/audio/music_render.h"

#include "engine/audio/sequencer.h"
#include "engine/audio/synth.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace sh::audio {
namespace {

constexpr std::uint64_t kTickPeriod = static_cast<std::uint64_t>(kOutputRate) * kTimerDenominator;  // in 1/N frames
constexpr std::uint32_t kSilentRun = kOutputRate / 2;  // the end needs half a second of digital silence

}  // namespace

void open_layers(MusicOptions& options, const std::array<std::uint8_t, 16>& layers) {
    for (std::size_t ch = 0; ch < 16; ++ch) {
        if (layers[ch] != 0 && options.channel_volume[ch] < 0) {
            options.channel_volume[ch] = 127;
        }
    }
}

Result<RenderedMusic> render_music(const Song& song, const SoundBank& bank, const MusicOptions& options) {
    using R = Result<RenderedMusic>;
    auto spu = std::make_unique<Synth>();
    spu->set_main_volume(options.main_volume << 7, options.main_volume << 7);
    spu->set_reverb_mode(options.reverb_mode);
    const auto depth = static_cast<std::int16_t>((options.reverb_depth << 16) >> 8);
    spu->set_reverb_depth(depth, depth);
    spu->set_reverb_enabled(true);

    Sequencer seq(*spu);
    seq.open_bank(bank);
    if (auto played = seq.play(song); !played) {
        return R::fail(played.error());
    }
    seq.set_volume(options.song_volume, options.song_volume);

    RenderedMusic out;
    const auto max_frames = static_cast<std::uint64_t>(options.max_seconds * kOutputRate);
    const auto max_spill = static_cast<std::uint32_t>(options.max_spill_seconds * kOutputRate);
    std::uint64_t phase = 0;
    std::uint32_t silent_run = 0;
    bool spilling = false;
    std::vector<std::int16_t> chunk;

    const auto hold_channels = [&]() {
        for (int ch = 0; ch < 16; ++ch) {
            const int want = options.channel_volume[static_cast<std::size_t>(ch)];
            if (want >= 0 && seq.channel_volume(ch) != want) {
                seq.set_channel_volume(ch, want);
            }
        }
    };
    hold_channels();
    std::size_t next_event = 0;

    while (true) {
        // Frames up to the next timer interrupt.
        const std::uint64_t frames = (kTickPeriod - phase + kTimerNumerator - 1) / kTimerNumerator;
        chunk.resize(frames * 2);
        spu->render(chunk.data(), frames, spilling ? 1u : ~0u);
        out.pcm.insert(out.pcm.end(), chunk.begin(), chunk.end());
        const bool quiet = std::all_of(chunk.begin(), chunk.end(), [](std::int16_t s) { return std::abs(s) <= 1; });
        silent_run = quiet ? silent_run + static_cast<std::uint32_t>(frames) : 0;

        std::uint32_t sounding = 0, old_sounding = 0;
        for (int v = 0; v < kVoices; ++v) {
            if (spu->voice_active(v)) {
                ++sounding;
                old_sounding += spu->voice_tag(v) == 0;
            }
        }
        out.voices_peak = std::max(out.voices_peak, sounding);

        if (spilling) {
            out.spill_frames = out.frames() - *out.loop_end;
            // Notes the jump left hanging can sound for ever; the spill never runs longer than one pass.
            const std::uint32_t pass = *out.loop_end - *out.loop_start;
            if ((old_sounding == 0 && silent_run >= kSilentRun) || out.spill_frames >= std::min(max_spill, pass)) {
                break;
            }
        } else if (!seq.playing() && sounding == 0 && silent_run >= kSilentRun) {
            break;
        } else if (out.frames() >= max_frames) {
            break;
        }

        phase += frames * kTimerNumerator;
        while (phase >= kTickPeriod) {
            phase -= kTickPeriod;
            while (next_event < options.events.size() &&
                   options.events[next_event].seconds * kOutputRate <= static_cast<double>(out.frames())) {
                const VolumeEvent& e = options.events[next_event++];
                if (e.channel < 0) {
                    seq.set_volume(e.volume, e.volume);
                } else {
                    seq.set_channel_volume(e.channel, e.volume);
                }
            }
            const std::uint32_t starts = seq.loop_starts();
            const std::uint32_t ends = seq.loop_ends();
            seq.timer_tick();
            ++out.timer_ticks;
            hold_channels();
            if (!out.loop_start && seq.loop_starts() != starts) {
                out.loop_start = out.frames();
                for (int ch = 0; ch < 16; ++ch) {
                    out.channel_volume_at_loop[static_cast<std::size_t>(ch)] = seq.channel_volume(ch);
                }
            }
            if (!spilling && seq.loop_ends() != ends && out.loop_start) {
                // From here on, voices keyed by the second pass carry tag 1 and are left out of the mix.
                out.loop_end = out.frames();
                spu->set_key_on_tag(1);
                spilling = true;
            }
        }
    }

    // The silence the end detection waited for is not part of the music.
    std::uint32_t keep = out.frames();
    const std::uint32_t floor = out.loop_end ? *out.loop_end : 0;
    while (keep > floor && std::abs(out.pcm[2 * keep - 2]) <= 1 && std::abs(out.pcm[2 * keep - 1]) <= 1) {
        --keep;
    }
    out.pcm.resize(static_cast<std::size_t>(keep) * 2);
    out.spill_frames = out.loop_end ? keep - *out.loop_end : 0;
    return R::ok(std::move(out));
}

}  // namespace sh::audio
