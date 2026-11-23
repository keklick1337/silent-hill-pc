#include "engine/audio/synth.h"

#include <algorithm>

namespace sh::audio {
namespace {

constexpr std::int32_t clamp16(std::int32_t v) {
    return v < -0x8000 ? -0x8000 : (v > 0x7FFF ? 0x7FFF : v);
}

constexpr std::int32_t apply_volume(std::int32_t sample, std::int32_t volume) {
    return (sample * volume) >> 15;
}

// Reverb register indices, port order.
enum Rev : std::uint32_t {
    dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2,
    mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF,
    mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN,
};

// Taps of the 39-tap half-band filter that resamples the reverb input and output (psx-spx "Reverb Buffer
// Resampling"): the 20 non-zero even taps; the centre tap is 0x4000.
constexpr std::int32_t kFir[20] = {
    -0x0001, 0x0002, -0x000A, 0x0023, -0x0067, 0x010A, -0x0268, 0x0534, -0x0B90, 0x2806,
    0x2806, -0x0B90, 0x0534, -0x0268, 0x010A, -0x0067, 0x0023, -0x000A, 0x0002, -0x0001,
};

constexpr std::uint32_t kReverbHalfwords = 256 * 1024;
constexpr std::uint32_t kHalfwordMask = kReverbHalfwords - 1;

std::int32_t negate_volume(std::int16_t v) {
    return v == -0x8000 ? 0x7FFF : -static_cast<std::int32_t>(v);
}

std::int16_t level_of(int libspu_volume) {
    const int v = std::clamp(libspu_volume, -0x4000, 0x3FFF);
    return static_cast<std::int16_t>(v * 2);
}

}  // namespace

// Rates as the ADSR registers give them: (shift << 2) | step. A rate with every bit of its field set never steps.
void Synth::Envelope::reset(std::uint8_t r, std::uint8_t rate_mask, bool dec, bool exp) {
    rate = r;
    decreasing = dec;
    exponential = exp;
    counter = 0;
    increment = 0x8000;
    const std::int32_t base = 7 - (r & 3);
    step = dec ? ~base : base;
    if (r < 44) {
        step <<= 11 - (r >> 2);
    } else if (r >= 48) {
        increment >>= (r >> 2) - 11;
        if ((r & rate_mask) != rate_mask) {
            increment = std::max<std::uint32_t>(increment, 1);
        }
    }
}

void Synth::Envelope::tick(std::int16_t& level) {
    std::uint32_t inc = increment;
    std::int32_t s = step;
    if (exponential) {
        if (decreasing) {
            s = (s * level) >> 15;
        } else if (level >= 0x6000) {
            if (rate < 40) {
                s >>= 2;
            } else if (rate >= 44) {
                inc >>= 2;
            } else {
                s >>= 1;
                inc >>= 1;
            }
        }
    }
    counter += inc;
    if (!(counter & 0x8000)) {
        return;
    }
    counter = 0;
    level = static_cast<std::int16_t>(std::clamp<std::int32_t>(level + s, 0, 0x7FFF));
}

Synth::Synth() : rev_mem_(kReverbHalfwords, 0) { reset(); }

void Synth::reset() {
    std::fill(rev_mem_.begin(), rev_mem_.end(), std::int16_t{0});
    voices_ = {};
    main_left_ = main_right_ = 0;
    key_on_tag_ = 0;
    rev_ = {};
    rev_out_left_ = rev_out_right_ = 0;
    rev_enabled_ = false;
    rev_base_ = rev_current_ = 0;
    rev_down_ = {};
    rev_up_ = {};
    rev_phase_ = 0;
}

void Synth::set_sample(int voice, std::shared_ptr<const Sample> sample) { voices_[voice].next = std::move(sample); }

void Synth::set_pitch(int voice, std::uint16_t pitch) { voices_[voice].pitch = pitch; }

void Synth::set_volume(int voice, int left, int right) {
    voices_[voice].volume_left = level_of(left);
    voices_[voice].volume_right = level_of(right);
}

void Synth::set_adsr(int voice, std::uint16_t adsr1, std::uint16_t adsr2) {
    Voice& v = voices_[voice];
    v.adsr1 = adsr1;
    v.adsr2 = adsr2;
    // The hardware reads the registers on every step, so a new value takes effect in the current phase.
    if (v.phase != Phase::Off) {
        update_envelope(v);
    }
}

void Synth::update_envelope(Voice& v) {
    switch (v.phase) {
        case Phase::Off:
            v.target = 0;
            v.envelope.reset(0, 0, false, false);
            break;
        case Phase::Attack:
            v.target = 0x7FFF;
            v.envelope.reset(static_cast<std::uint8_t>((v.adsr1 >> 8) & 0x7F), 0x7F, false, (v.adsr1 & 0x8000) != 0);
            break;
        case Phase::Decay:
            v.target = static_cast<std::int16_t>(std::min<std::int32_t>(((v.adsr1 & 0xF) + 1) * 0x800, 0x7FFF));
            v.envelope.reset(static_cast<std::uint8_t>(((v.adsr1 >> 4) & 0xF) << 2), 0x1F << 2, true, true);
            break;
        case Phase::Sustain:
            v.target = 0;
            v.envelope.reset(static_cast<std::uint8_t>((v.adsr2 >> 6) & 0x7F), 0x7F, (v.adsr2 & 0x4000) != 0,
                             (v.adsr2 & 0x8000) != 0);
            break;
        case Phase::Release:
            v.target = 0;
            v.envelope.reset(static_cast<std::uint8_t>((v.adsr2 & 0x1F) << 2), 0x1F << 2, true, (v.adsr2 & 0x20) != 0);
            break;
    }
}

void Synth::tick_envelope(Voice& v) {
    v.envelope.tick(v.level);
    if (v.phase == Phase::Sustain) {
        return;
    }
    const bool reached = v.envelope.decreasing ? v.level <= v.target : v.level >= v.target;
    if (!reached) {
        return;
    }
    switch (v.phase) {
        case Phase::Attack: v.phase = Phase::Decay; break;
        case Phase::Decay: v.phase = Phase::Sustain; break;
        case Phase::Release: v.phase = Phase::Off; break;
        default: break;
    }
    update_envelope(v);
}

void Synth::key_on(std::uint32_t voice_mask) {
    for (int i = 0; i < kVoices; ++i) {
        if (!(voice_mask & (1u << i))) {
            continue;
        }
        Voice& v = voices_[i];
        v.playing = v.next;
        v.step_scale = v.playing && v.playing->rate != kOutputRate
                           ? static_cast<std::uint32_t>((static_cast<std::uint64_t>(v.playing->rate) << 16) / kOutputRate)
                           : 0x10000u;
        v.counter = 0;
        v.level = 0;
        // The first frame is current and three silent ones precede it, as after the hardware's first block decode.
        v.history = {0, 0, 0, 0};
        v.position = 0;
        v.ended = false;
        if (v.playing && !v.playing->pcm.empty()) {
            v.history[3] = v.playing->pcm[0];
            v.position = 1;
        } else {
            v.ended = true;
        }
        v.phase = v.ended ? Phase::Off : Phase::Attack;
        v.keyed = true;
        v.tag = key_on_tag_;
        update_envelope(v);
    }
}

void Synth::key_off(std::uint32_t voice_mask) {
    for (int i = 0; i < kVoices; ++i) {
        if (!(voice_mask & (1u << i))) {
            continue;
        }
        Voice& v = voices_[i];
        v.keyed = false;
        if (v.phase != Phase::Off && v.phase != Phase::Release) {
            v.phase = Phase::Release;
            update_envelope(v);
        }
    }
}

void Synth::set_reverb_voices(std::uint32_t voice_mask, bool on) {
    for (int i = 0; i < kVoices; ++i) {
        if (voice_mask & (1u << i)) {
            voices_[i].reverb = on;
        }
    }
}

KeyStatus Synth::key_status(int voice) const {
    const Voice& v = voices_[voice];
    if (v.keyed) {
        return v.level != 0 ? KeyStatus::On : KeyStatus::OnEnvOff;
    }
    return v.level != 0 ? KeyStatus::OffEnvOn : KeyStatus::Off;
}

std::int16_t Synth::envelope(int voice) const { return voices_[voice].level; }
std::uint16_t Synth::pitch(int voice) const { return voices_[voice].pitch; }
int Synth::volume_left(int voice) const { return voices_[voice].volume_left / 2; }
int Synth::volume_right(int voice) const { return voices_[voice].volume_right / 2; }
std::uint8_t Synth::voice_tag(int voice) const { return voices_[voice].tag; }
bool Synth::voice_active(int voice) const { return voices_[voice].phase != Phase::Off; }

void Synth::set_main_volume(int left, int right) {
    main_left_ = level_of(left);
    main_right_ = level_of(right);
}

void Synth::set_reverb_mode(int mode) {
    const ReverbPreset& p = reverb_preset(mode);
    rev_ = p.regs;
    rev_base_ = (((kReverbHalfwords * 2 - p.work_bytes) >> 3) << 2) & kHalfwordMask;
    rev_current_ = rev_base_;
    std::fill(rev_mem_.begin() + static_cast<std::ptrdiff_t>(rev_base_), rev_mem_.end(), std::int16_t{0});
    rev_down_ = {};
    rev_up_ = {};
}

void Synth::set_reverb_depth(std::int16_t left, std::int16_t right) {
    rev_out_left_ = left;
    rev_out_right_ = right;
}

void Synth::set_reverb_enabled(bool on) { rev_enabled_ = on; }

// The next frame of the sample enters the history. Past the last frame the voice continues at the loop start, or,
// with no loop, its envelope is cut off the way the hardware does when it leaves an end block without repeat.
void Synth::advance(Voice& v) {
    std::int16_t next = 0;
    if (!v.ended) {
        const Sample& s = *v.playing;
        if (v.position >= s.pcm.size()) {
            if (s.loop_start && *s.loop_start < s.pcm.size()) {
                v.position = *s.loop_start;
            } else {
                v.ended = true;
                v.phase = Phase::Off;
                v.level = 0;
                update_envelope(v);
            }
        }
        if (!v.ended) {
            next = s.pcm[v.position++];
        }
    }
    v.history = {v.history[1], v.history[2], v.history[3], next};
}

std::int32_t Synth::sample_voice(Voice& v) {
    const auto gauss = gauss_table();
    const std::uint32_t i = (v.counter >> 4) & 0xFF;
    std::int32_t out = gauss[0x0FF - i] * v.history[0];
    out += gauss[0x1FF - i] * v.history[1];
    out += gauss[0x100 + i] * v.history[2];
    out += gauss[0x000 + i] * v.history[3];
    const std::int32_t sample = apply_volume(out >> 15, v.level);

    tick_envelope(v);

    std::uint32_t step = v.pitch > 0x3FFF ? 0x4000u : v.pitch;
    if (v.step_scale != 0x10000u) {
        step = static_cast<std::uint32_t>((static_cast<std::uint64_t>(step) * v.step_scale) >> 16);
    }
    v.counter += step;
    while (v.counter >= 0x1000 && v.phase != Phase::Off) {
        v.counter -= 0x1000;
        advance(v);
    }
    return sample;
}

std::int16_t Synth::reverb_read(std::uint32_t reg, std::int32_t extra) const {
    std::uint32_t offset = rev_current_ + (((reg << 2) + static_cast<std::uint32_t>(extra)) & kHalfwordMask);
    if (offset & (kHalfwordMask + 1)) {
        offset += rev_base_;
    }
    offset &= kHalfwordMask;
    return rev_mem_[offset];
}

void Synth::reverb_write(std::uint32_t reg, std::int16_t value) {
    std::uint32_t offset = rev_current_ + ((reg << 2) & kHalfwordMask);
    if (offset & (kHalfwordMask + 1)) {
        offset += rev_base_;
    }
    offset &= kHalfwordMask;
    rev_mem_[offset] = value;
}

// One 44.1 kHz step of the reverb unit: the input is filtered down to 22.05 kHz, the delay network runs on every
// second step, and its output is filtered back up (DuckStation's measured model of the hardware).
void Synth::reverb_tick(std::int32_t in_left, std::int32_t in_right, std::int32_t& out_left, std::int32_t& out_right) {
    const std::int16_t in[2] = {static_cast<std::int16_t>(clamp16(in_left)), static_cast<std::int16_t>(clamp16(in_right))};
    for (int ch = 0; ch < 2; ++ch) {
        rev_down_[ch][rev_phase_ | 0x00] = in[ch];
        rev_down_[ch][rev_phase_ | 0x40] = in[ch];
    }
    std::int32_t out[2];
    const auto r = [&](Rev k) { return static_cast<std::int16_t>(rev_[k]); };
    if (rev_phase_ & 1) {
        std::int32_t down[2];
        for (int ch = 0; ch < 2; ++ch) {
            const std::int16_t* src = &rev_down_[ch][(rev_phase_ - 38) & 0x3F];
            std::int32_t acc = 0x4000 * src[19];
            for (int i = 0; i < 20; ++i) {
                acc += kFir[i] * src[2 * i];
            }
            down[ch] = clamp16(acc >> 15);
        }
        const Rev same[2] = {mLSAME, mRSAME}, dsame[2] = {dLSAME, dRSAME};
        const Rev diff[2] = {mLDIFF, mRDIFF}, ddiff[2] = {dLDIFF, dRDIFF};
        const Rev comb1[2] = {mLCOMB1, mRCOMB1}, comb2[2] = {mLCOMB2, mRCOMB2};
        const Rev comb3[2] = {mLCOMB3, mRCOMB3}, comb4[2] = {mLCOMB4, mRCOMB4};
        const Rev apf1[2] = {mLAPF1, mRAPF1}, apf2[2] = {mLAPF2, mRAPF2}, vin[2] = {vLIN, vRIN};
        const std::int16_t iir = r(vIIR);
        // vIIR = -0x8000 multiplies correctly but negates what is written (psx-spx "Reverb Bugs").
        const auto feedback = [iir](std::int16_t old) -> std::int64_t {
            if (iir == -0x8000) {
                return old == -0x8000 ? 0 : static_cast<std::int64_t>(old) * -65536;
            }
            return static_cast<std::int64_t>(old) * (0x8000 - iir);
        };
        for (int ch = 0; ch < 2; ++ch) {
            const int other = ch ^ 1;
            const std::int32_t input = (down[ch] * r(vin[ch])) >> 14;
            if (rev_enabled_) {
                const std::int32_t same_in = clamp16((((reverb_read(rev_[dsame[ch]], 0) * r(vWALL)) >> 14) + input) >> 1);
                const std::int32_t diff_in =
                    clamp16((((reverb_read(rev_[ddiff[other]], 0) * r(vWALL)) >> 14) + input) >> 1);
                const std::int32_t same_out = clamp16(
                    (((same_in * iir) >> 14) + static_cast<std::int32_t>(feedback(reverb_read(rev_[same[ch]], -1)) >> 14)) >> 1);
                const std::int32_t diff_out = clamp16(
                    (((diff_in * iir) >> 14) + static_cast<std::int32_t>(feedback(reverb_read(rev_[diff[ch]], -1)) >> 14)) >> 1);
                reverb_write(rev_[same[ch]], static_cast<std::int16_t>(same_out));
                reverb_write(rev_[diff[ch]], static_cast<std::int16_t>(diff_out));
            }
            const std::int32_t acc = ((reverb_read(rev_[comb1[ch]], 0) * r(vCOMB1)) >> 14) +
                                     ((reverb_read(rev_[comb2[ch]], 0) * r(vCOMB2)) >> 14) +
                                     ((reverb_read(rev_[comb3[ch]], 0) * r(vCOMB3)) >> 14) +
                                     ((reverb_read(rev_[comb4[ch]], 0) * r(vCOMB4)) >> 14);
            const std::int32_t fb_a = reverb_read(static_cast<std::uint32_t>(rev_[apf1[ch]] - rev_[dAPF1]), 0);
            const std::int32_t fb_b = reverb_read(static_cast<std::uint32_t>(rev_[apf2[ch]] - rev_[dAPF2]), 0);
            const std::int32_t mda = clamp16((acc + ((fb_a * negate_volume(r(vAPF1))) >> 14)) >> 1);
            const std::int32_t mdb =
                clamp16(fb_a + ((((mda * r(vAPF1)) >> 14) + ((fb_b * negate_volume(r(vAPF2))) >> 14)) >> 1));
            const auto up = static_cast<std::int16_t>(clamp16(fb_b + ((mdb * r(vAPF2)) >> 15)));
            rev_up_[ch][(rev_phase_ >> 1) | 0x20] = up;
            rev_up_[ch][rev_phase_ >> 1] = up;
            if (rev_enabled_) {
                reverb_write(rev_[apf1[ch]], static_cast<std::int16_t>(mda));
                reverb_write(rev_[apf2[ch]], static_cast<std::int16_t>(mdb));
            }
        }
        rev_current_ = (rev_current_ + 1) & kHalfwordMask;
        if (rev_current_ == 0) {
            rev_current_ = rev_base_;
        }
        for (int ch = 0; ch < 2; ++ch) {
            const std::int16_t* src = &rev_up_[ch][((rev_phase_ >> 1) - 19) & 0x1F];
            std::int32_t acc = 0;
            for (int i = 0; i < 20; ++i) {
                acc += kFir[i] * src[i];
            }
            out[ch] = clamp16(acc >> 14);
        }
    } else {
        const std::uint32_t idx = (((rev_phase_ >> 1) - 19) & 0x1F) + 9;
        out[0] = rev_up_[0][idx];
        out[1] = rev_up_[1][idx];
    }
    rev_phase_ = (rev_phase_ + 1) & 0x3F;
    out_left = apply_volume(out[0], rev_out_left_);
    out_right = apply_volume(out[1], rev_out_right_);
}

void Synth::render(std::int16_t* out, std::size_t frames, std::uint32_t mix_tags) {
    for (std::size_t f = 0; f < frames; ++f) {
        std::int32_t left = 0, right = 0, rev_left = 0, rev_right = 0;
        for (Voice& v : voices_) {
            if (v.phase == Phase::Off) {
                continue;
            }
            const std::int32_t s = sample_voice(v);
            if (!(mix_tags & (1u << v.tag))) {
                continue;
            }
            const std::int32_t l = apply_volume(s, v.volume_left);
            const std::int32_t r = apply_volume(s, v.volume_right);
            left += l;
            right += r;
            if (v.reverb) {
                rev_left += l;
                rev_right += r;
            }
        }
        std::int32_t wet_left, wet_right;
        reverb_tick(rev_left, rev_right, wet_left, wet_right);
        left += wet_left;
        right += wet_right;
        out[2 * f] = static_cast<std::int16_t>(clamp16(apply_volume(clamp16(left), main_left_)));
        out[2 * f + 1] = static_cast<std::int16_t>(clamp16(apply_volume(clamp16(right), main_right_)));
    }
}

void Synth::render_buses(std::int16_t* main, std::int16_t* side, std::size_t frames) {
    for (std::size_t f = 0; f < frames; ++f) {
        std::int32_t left = 0, right = 0, rev_left = 0, rev_right = 0, side_left = 0, side_right = 0;
        for (Voice& v : voices_) {
            if (v.phase == Phase::Off) {
                continue;
            }
            const std::int32_t s = sample_voice(v);
            const std::int32_t l = apply_volume(s, v.volume_left);
            const std::int32_t r = apply_volume(s, v.volume_right);
            if (v.tag != 0) {
                side_left += l;
                side_right += r;
                continue;
            }
            left += l;
            right += r;
            if (v.reverb) {
                rev_left += l;
                rev_right += r;
            }
        }
        std::int32_t wet_left, wet_right;
        reverb_tick(rev_left, rev_right, wet_left, wet_right);
        left += wet_left;
        right += wet_right;
        main[2 * f] = static_cast<std::int16_t>(clamp16(apply_volume(clamp16(left), main_left_)));
        main[2 * f + 1] = static_cast<std::int16_t>(clamp16(apply_volume(clamp16(right), main_right_)));
        side[2 * f] = static_cast<std::int16_t>(clamp16(apply_volume(clamp16(side_left), main_left_)));
        side[2 * f + 1] = static_cast<std::int16_t>(clamp16(apply_volume(clamp16(side_right), main_right_)));
    }
}

}  // namespace sh::audio
