#include "engine/audio/sequencer.h"

#include <algorithm>
#include <cmath>

namespace sh::audio {
namespace {

// The driver's pitch table: 0x1000 * 2^(k / 1536), rounded down, for the 128 steps of each of the 12 semitones.
struct PitchTable {
    std::array<std::uint16_t, 12 * 128> v{};
    PitchTable() {
        for (int k = 0; k < 12 * 128; ++k) {
            v[static_cast<std::size_t>(k)] = static_cast<std::uint16_t>(std::floor(4096.0 * std::exp2(k / 1536.0)));
        }
    }
};

const PitchTable& pitch_table() {
    static const PitchTable table;
    return table;
}

constexpr std::uint32_t voice_bit(int voice) { return 1u << voice; }

}  // namespace

std::uint16_t note_to_pitch(int note, int cent, int sample_note, int sample_cent) {
    const auto total = static_cast<std::int16_t>(cent + sample_cent);
    const int steps = (total < 0 ? total + 127 : total) >> 7;
    const auto final_note = static_cast<std::int16_t>(note + steps);
    const int offset = std::clamp(total - (steps << 7), 0, 127);
    const auto diff = static_cast<std::int16_t>(final_note - sample_note);
    const auto& t = pitch_table().v;
    if (diff >= 0) {
        return static_cast<std::uint16_t>(t[static_cast<std::size_t>((diff % 12) * 128 + offset)] << (diff / 12));
    }
    const int down = -diff;
    return static_cast<std::uint16_t>(t[static_cast<std::size_t>(((12 - down % 12) % 12) * 128 + offset)] >>
                                      ((down + 11) / 12));
}

Sequencer::Sequencer(Synth& synth) : spu_(synth) {
    for (int i = 0; i < kVoices; ++i) {
        ports_[static_cast<std::size_t>(i)].voice = static_cast<std::uint8_t>(i);
    }
    sound_off();
}

void Sequencer::open_bank(const SoundBank& bank) { bank_ = bank; }

Result<void> Sequencer::play(const Song& song) {
    if (song.tracks.empty()) {
        return Result<void>::fail("sequencer: the song has no tracks");
    }
    song_ = song;
    division_ = static_cast<std::uint16_t>(song_.ticks_per_quarter);
    const std::size_t count = std::min<std::size_t>(song_.tracks.size(), 32);
    tracks_.assign(count, Track{});
    loop_flag_ = 0;
    beat_ = beat_loop_ = 0;
    beat_stat_ = 16;
    control_stat_ = 0;
    for (std::size_t i = 0; i < count; ++i) {
        Track& t = tracks_[i];
        t.steps = i;
        t.pos = 0;
        t.tempo = t.tempo_acc = 104;
        t.loop_pos = t.pos;
        t.eof = false;
        t.status = 0;
        t.rest = 0;
        t.channel = 0;
        t.delta = static_cast<std::uint16_t>(read_delta(t));
        convert_delta(t);
    }
    playing_ = true;
    return Result<void>::ok();
}

void Sequencer::stop() {
    for (Track& t : tracks_) {
        t.eof = true;
        t.pos = 0;
    }
    sound_off();
    volume_left_ = volume_right_ = 127;
    playing_ = false;
}

void Sequencer::set_volume(int left, int right) {
    volume_left_ = left & 0x7F;
    volume_right_ = right & 0x7F;
    if (playing_) {
        volume_dirty_ = true;
    }
}

void Sequencer::set_channel_volume(int channel, int volume) { control_change(channel & 15, 7, volume & 0x7F); }

int Sequencer::channel_volume(int channel) const { return channels_[static_cast<std::size_t>(channel & 15)].volume; }

bool Sequencer::playing() const {
    if (!playing_) {
        return false;
    }
    return std::any_of(tracks_.begin(), tracks_.end(), [](const Track& t) { return !t.eof; });
}

std::uint32_t Sequencer::beat() const { return beat_; }

// Past the last step (the song's size field cut it there) the track reads as ended.
const SongStep* Sequencer::next_step(Track& t) {
    const std::vector<SongStep>& steps = song_.tracks[t.steps];
    if (t.pos >= steps.size()) {
        t.eof = true;
        return nullptr;
    }
    return &steps[t.pos++];
}

std::uint32_t Sequencer::read_delta(Track& t) {
    const SongStep* s = next_step(t);
    if (!s || s->kind != SongStep::Kind::Wait) {
        return 0;
    }
    return s->ticks;
}

// The song's ticks scaled to the sequencer's 1/96 or 1/120 quarter, the remainder carried.
void Sequencer::convert_delta(Track& t) {
    std::uint32_t d = t.delta;
    switch (division_) {
        case 48: d = d * 10 + t.rest; t.rest = static_cast<std::uint16_t>(d & 3); d /= 4; break;
        case 96: d = d * 5 + t.rest; t.rest = static_cast<std::uint16_t>(d & 3); d /= 4; break;
        case 192:
        case 240: d += t.rest; t.rest = static_cast<std::uint16_t>(d & 1); d /= 2; break;
        case 288:
        case 360: d /= 3; break;
        case 384:
        case 480: d += t.rest; t.rest = static_cast<std::uint16_t>(d & 3); d /= 4; break;
        case 768:
        case 960: d += t.rest; t.rest = static_cast<std::uint16_t>(d & 7); d /= 8; break;
        default: break;
    }
    t.delta = static_cast<std::uint16_t>(d);
}

bool Sequencer::read_event(Track& t) {
    const SongStep* s = next_step(t);
    if (!s) {
        return true;
    }
    using K = SongStep::Kind;
    switch (s->kind) {
        case K::Note:
            if (s->b & 0x7F) {
                key_on(t.channel, s->a & 0x7F, s->b & 0x7F);
            } else {
                key_off(t.channel, s->a & 0x7F);
            }
            t.status = s->a;
            return (s->b & 0x80) != 0;
        case K::ReleaseLast: key_off(t.channel, t.status); return false;
        case K::ReleaseLastNoDelta: key_off(t.channel, t.status); return true;
        case K::Tempo:
            for (Track& other : tracks_) {
                other.tempo = static_cast<std::uint16_t>((s->a & 0x7F) * 2 + 2);
            }
            return (s->a & 0x80) != 0;
        case K::Bend: channels_[t.channel & 15].bend = s->a & 0x7F; return (s->a & 0x80) != 0;
        case K::Program: channels_[t.channel & 15].program = s->a & 0x7F; return (s->a & 0x80) != 0;
        case K::Channel: t.channel = s->a & 0xF; return (s->a & 0x80) != 0;
        case K::End: t.eof = true; return (s->a & 0x80) != 0;
        case K::Control:
            channel_message(t, static_cast<std::uint8_t>(s->a & 0x7F), static_cast<std::uint8_t>(s->b & 0x7F));
            return (s->b & 0x80) != 0;
        case K::Wait: return false;  // a delta time where an event belongs: not in any song of the game
    }
    return false;
}

// Control change: the loop markers and the loop count, then the controller itself.
void Sequencer::channel_message(Track& t, std::uint8_t controller, std::uint8_t value) {
    const int ch = t.channel & 15;
    if (controller == 0x63) {
        if (value == 0x14) {
            loop_flag_ = 2;
        } else if (value == 0x1E) {
            loop_flag_ = 1;
        }
    }
    if (controller == 6 && channels_[static_cast<std::size_t>(ch)].nrpn_msb == 0x14) {
        for (Track& other : tracks_) {
            other.loop_count = value;
        }
    }
    control_change(ch, controller, value);
}

void Sequencer::song_tick() {
    if (!playing_) {
        return;
    }
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        Track& t = tracks_[i];
        if (t.eof) {
            continue;
        }
        t.tempo_acc = static_cast<std::uint16_t>(t.tempo_acc + t.tempo);
        if (t.tempo_acc <= 0xFF) {
            continue;
        }
        t.tempo_acc &= 0xFF;
        if (i == 1) {
            ++beat_;
        }
        if (t.delta != 0) {
            --t.delta;
            continue;
        }
        while (true) {
            const bool no_delta = read_event(t);
            if (t.eof) {
                if (t.delta != 0) {
                    --t.delta;
                }
                break;
            }
            if (!no_delta) {
                t.delta = static_cast<std::uint16_t>(read_delta(t));
            }
            if (t.delta != 0) {
                convert_delta(t);
            }
            if (t.delta == 0) {
                continue;
            }
            --t.delta;
            break;
        }
    }
    if (loop_flag_ == 2) {
        for (Track& t : tracks_) {
            t.loop_count = 127;
            t.loop_pos = t.pos;
            t.delta_loop = t.delta;
            t.rest_loop = t.rest;
            t.eof_loop = t.eof;
            t.status_loop = t.status;
        }
        beat_loop_ = beat_;
        ++loop_starts_;
    } else if (loop_flag_ == 1) {
        for (Track& t : tracks_) {
            if (t.loop_count == 0) {
                continue;
            }
            if (t.loop_count < 127) {
                --t.loop_count;
            }
            t.pos = t.loop_pos;
            t.delta = t.delta_loop;
            t.rest = t.rest_loop;
            t.eof = t.eof_loop;
            t.status = t.status_loop;
        }
        beat_ = beat_loop_;
        ++loop_ends_;
    }
    loop_flag_ = 0;
}

void Sequencer::timer_tick() {
    song_tick();
    if (timer_sync_ >= 11) {
        slow_tick();
        auto_key_off();
        timer_sync_ = 0;
    }
    ++timer_sync_;
}

// --- Voices ------------------------------------------------------------------------------------------------------

void Sequencer::release_voice(int voice) {
    spu_.set_adsr(voice, 0, 0);
    spu_.key_off(voice_bit(voice));
}

void Sequencer::sound_off() {
    master_volume_ = 127;
    volume_dirty_ = false;
    for (Track& t : tracks_) {
        t = Track{};
    }
    for (int vo = 0; vo < kVoices; ++vo) {
        Port& p = ports_[static_cast<std::size_t>(vo)];
        set_voice_volume(vo, 0, 0, 0);
        p.voice = static_cast<std::uint8_t>(vo);
        p.stat = 0;
        p.pan = 64;
        p.pedal = 0;
        p.vib_phase = 0;
        p.vib_wait_count = 0;
        p.vib_steps = 0;
        p.vib_depth = 0;
        p.vib_depth_max = 0;
        p.vib_data = 0;
        p.tre_phase = 0;
        p.tre_wait_count = 0;
        p.tre_steps = 0;
        p.tre_depth = 0;
        p.tre_depth_max = 0;
        p.tre_data = 0;
        p.bend_seen = 64;
        release_voice(vo);
    }
    for (Channel& m : channels_) {
        const std::uint8_t program = m.program;
        m = Channel{};
        m.program = program;  // the program survives
    }
}

void Sequencer::set_voice_volume(int voice, int channel, int left, int right) {
    int l = 0, r = 0;
    if (left | right) {
        l = (left * volume_left_) >> 7;
        r = (right * volume_right_) >> 7;
        if (channels_[static_cast<std::size_t>(channel & 15)].wide) {
            r = -r;
        }
    }
    spu_.set_volume(voice, l, r);
}

void Sequencer::volume_calc(Port& p, const Channel& m) {
    int pan = bank_.master_pan + p.prog_pan + p.tone_pan + m.pan - 0xC0;
    if (m.key_pan != 0) {
        const int shifted = ((p.note * m.key_pan) >> 6) + 0x40;
        pan = pan + (shifted - m.key_pan) - 0x40;
    }
    pan = std::clamp(pan, 0, 0x7F);
    p.pan = static_cast<std::uint16_t>(pan);

    int left = (bank_.master_volume * static_cast<int>(master_volume_) * m.expression * m.volume) >> 14;
    left = left * p.prog_volume;
    left = (left * p.tone_volume) >> 14;
    int right = left;
    if (pan > 0x40) {
        left = ((0x40 - (pan & 0x3F)) * (left * 2)) >> 7;
    } else if (pan < 0x40) {
        right = (pan * (left * 2)) >> 7;
    }
    p.left = static_cast<std::uint16_t>((left * (p.velocity & 0x7F)) >> 7);
    p.right = static_cast<std::uint16_t>((right * (p.velocity & 0x7F)) >> 7);
    if (m.vol_mode >= 0x40) {
        p.left = static_cast<std::uint16_t>((p.left * p.left) >> 14);
        p.right = static_cast<std::uint16_t>((p.right * p.right) >> 14);
    }
}

int Sequencer::pitch_bend(const Port& p, int bend) const {
    if (bend < 0x40) {
        return p.bend_min ? -(p.bend_min * 2 * (0x40 - bend)) : 0;
    }
    if (bend == 0x40 || !p.bend_max) {
        return 0;
    }
    return static_cast<std::int16_t>(p.bend_max * 2) * (bend - 0x3F);
}

void Sequencer::pitch_calc(Port& p, bool force) {
    const Channel& m = channels_[p.channel & 15];
    if (p.vib_depth_max != 0 || m.mod != 0 || m.porta != 0 || m.bend != p.bend_seen || force) {
        p.bend_seen = m.bend;
        auto pitch = static_cast<std::int16_t>(p.vib_data + static_cast<std::uint16_t>(m.mod_depth) +
                                               static_cast<std::uint16_t>(m.porta_depth));
        pitch = static_cast<std::int16_t>(pitch + (p.note_start << 7) + pitch_bend(p, m.bend));
        spu_.set_pitch(p.voice, note_to_pitch(pitch >> 7, pitch & 0x7F, p.center, p.shift));
    }
    if (p.vib_depth_max != 0 && spu_.key_status(p.voice) == KeyStatus::Off) {
        p.vib_depth_max = 0;
        p.vib_data = 0;
    }
}

void Sequencer::vibrato(Port& p) {
    if (p.vib_depth_max == 0) {
        return;
    }
    if (p.vib_wait_count != p.vib_wait) {
        ++p.vib_wait_count;
        return;
    }
    if (p.vib_steps >= p.vib_count) {
        p.vib_depth = p.vib_depth_max;
    } else {
        p.vib_depth = p.vib_steps ? static_cast<std::uint16_t>(p.vib_depth + p.vib_add) : p.vib_add;
        ++p.vib_steps;
    }
    p.vib_phase = static_cast<std::int8_t>(p.vib_phase + p.vib_speed);
    int data;
    if (p.vib_phase < 0) {
        auto c = static_cast<std::int8_t>(-p.vib_phase * 2);
        data = p.vib_depth * std::abs(static_cast<int>(c));
        data = data > 0 ? -data : data;
    } else {
        auto c = static_cast<std::int8_t>(p.vib_phase * 2);
        data = p.vib_depth * std::abs(static_cast<int>(c));
        data = data < 0 ? -data : data;
    }
    if (data < 0) {
        data += 0x3FF;
    }
    p.vib_data = static_cast<std::uint16_t>(data ? data >> 10 : 0);
}

void Sequencer::tremolo(Port& p) {
    if (p.tre_depth_max == 0) {
        return;
    }
    if (p.tre_wait_count != p.tre_wait) {
        ++p.tre_wait_count;
    } else {
        if (p.tre_steps >= p.tre_count) {
            p.tre_depth = p.tre_depth_max;
        } else {
            p.tre_depth = p.tre_steps ? static_cast<std::uint16_t>(p.tre_depth + p.tre_add) : p.tre_add;
            ++p.tre_steps;
        }
        p.tre_phase = static_cast<std::int8_t>(p.tre_phase + p.tre_speed);
        int data;
        if (p.tre_phase < 0) {
            auto c = static_cast<std::int8_t>(-p.tre_phase * 2);
            data = p.tre_depth * std::abs(static_cast<int>(c));
            data = data > 0 ? -data : data;
        } else {
            auto c = static_cast<std::int8_t>(p.tre_phase * 2);
            data = p.tre_depth * std::abs(static_cast<int>(c));
            data = data < 0 ? -data : data;
        }
        if (data < 0) {
            data += 0xFF;
        }
        p.tre_data = static_cast<std::int16_t>(data ? data >> 8 : 0);
    }
    if (p.tre_written == p.tre_data) {
        return;
    }
    p.tre_written = p.tre_data;
    // The driver writes the tremolo volume without the song volume.
    spu_.set_volume(p.voice, std::clamp(p.left + p.tre_data, 0, 0x3FFF), std::clamp(p.right + p.tre_data, 0, 0x3FFF));
}

void Sequencer::slow_tick() {
    if (playing_) {
        for (Channel& m : channels_) {
            // midi_mod
            if (m.mod != 0) {
                if (m.mod_up) {
                    if (m.mod_depth + m.mod_add < m.mod_limit) {
                        m.mod_depth = static_cast<std::int16_t>(m.mod_depth + m.mod_add);
                    } else {
                        m.mod_up = 0;
                        m.mod_depth = m.mod_limit;
                    }
                } else if (m.mod_depth - m.mod_add > -m.mod_limit) {
                    m.mod_depth = static_cast<std::int16_t>(m.mod_depth - m.mod_add);
                } else {
                    m.mod_up = 1;
                    m.mod_depth = static_cast<std::int16_t>(-m.mod_limit);
                }
            } else {
                m.mod_depth = 0;
            }
            // midi_porta
            if (m.porta != 0) {
                if (m.porta_up) {
                    m.porta_depth = m.porta_depth + m.porta_add < m.porta_limit
                                        ? static_cast<std::int16_t>(m.porta_depth + m.porta_add)
                                        : static_cast<std::int16_t>(m.porta_limit);
                } else {
                    m.porta_depth = m.porta_add - m.porta_depth < m.porta_limit
                                        ? static_cast<std::int16_t>(m.porta_depth - m.porta_add)
                                        : static_cast<std::int16_t>(-static_cast<int>(m.porta_limit));
                }
            }
        }
        for (Port& p : ports_) {
            vibrato(p);
            tremolo(p);
            pitch_calc(p, false);
            if (volume_dirty_) {
                set_voice_volume(p.voice, p.channel, p.left, p.right);
            }
        }
        volume_dirty_ = false;
    }
}

void Sequencer::auto_key_off() {
    for (Port& p : ports_) {
        if (p.stat == 0 || spu_.key_status(p.voice) != KeyStatus::OnEnvOff) {
            continue;
        }
        if (p.stat >= 2) {
            spu_.key_off(voice_bit(p.voice));
            p.stat = 0;
        } else {
            ++p.stat;
        }
    }
}

int Sequencer::voice_check(int channel, int note, bool only_free) const {
    for (int vo = 0; vo < kVoices; ++vo) {
        if (spu_.key_status(vo) == KeyStatus::Off) {
            return vo;
        }
    }
    for (int vo = 0; vo < kVoices; ++vo) {
        const KeyStatus s = spu_.key_status(vo);
        if (s == KeyStatus::OffEnvOn || s == KeyStatus::Off) {
            return vo;
        }
    }
    if (only_free) {
        return -1;
    }
    for (int vo = 0; vo < kVoices; ++vo) {
        const Port& p = ports_[static_cast<std::size_t>(vo)];
        if (p.channel == channel && p.note == note) {
            return vo;
        }
    }
    for (int vo = 0; vo < kVoices; ++vo) {
        if (ports_[static_cast<std::size_t>(vo)].stat == 0) {
            return vo;
        }
    }
    return -1;
}

void Sequencer::key_on(int channel, int key, int velocity) {
    Channel& m = channels_[static_cast<std::size_t>(channel & 15)];
    const int program = m.program;
    std::size_t table = 0;
    for (int i = 0; i < program; ++i) {
        table += bank_.programs[static_cast<std::size_t>(i)].tones != 0;
    }
    const BankProgram& prog = bank_.programs[static_cast<std::size_t>(program)];
    for (int tone = 0; tone < prog.tones; ++tone) {
        const std::size_t index = table * 16 + static_cast<std::size_t>(tone);
        if (index >= bank_.tones.size()) {
            break;
        }
        const BankTone& atr = bank_.tones[index];
        if (atr.sample == 0 || key < atr.key_min || key > atr.key_max) {
            continue;
        }
        int vo = -1;
        int note = key;
        bool held = false;  // mode 126: the channel's voice is still sounding, so it is retuned instead of keyed
        if (m.mode != 0) {
            for (int v = 0; v < kVoices; ++v) {
                if (ports_[static_cast<std::size_t>(v)].channel == channel) {
                    vo = v;
                    break;
                }
            }
            if (vo >= 0) {
                if (ports_[static_cast<std::size_t>(vo)].stat == 0) {
                    release_voice(vo);
                } else {
                    held = true;
                }
            } else {
                vo = voice_check(channel, key, true);
            }
        }
        if (m.porta != 0) {
            note = m.before_note & 0x7F;
            m.porta_depth = 0;
            if (m.before_note < key) {
                m.porta_up = 1;
                m.porta_limit = static_cast<std::uint16_t>((key - m.before_note) << 7);
                m.porta_add = static_cast<std::int16_t>((m.porta_limit * 4) / m.porta);
            } else if (m.before_note == key) {
                m.porta_add = 0;
                note = key;
            } else {
                m.porta_up = 0;
                m.porta_limit = static_cast<std::uint16_t>((m.before_note - key) << 7);
                m.porta_add = static_cast<std::int16_t>((m.porta_limit * 4) / m.porta);
            }
        } else {
            m.porta_depth = 0;
        }
        if (vo < 0) {
            vo = voice_check(channel, key, false);
            if (vo < 0) {
                continue;
            }
        }
        Port& p = ports_[static_cast<std::size_t>(vo)];
        if (m.mode == 0) {
            release_voice(vo);
        }

        p.adsr1 = atr.adsr1;
        p.adsr2 = atr.adsr2;
        m.before_note = static_cast<std::uint8_t>(key);
        m.velocity = static_cast<std::uint8_t>(velocity);
        p.center = atr.root_key;
        p.shift = atr.fine_tune;
        p.bend_min = atr.bend_min;
        p.bend_max = atr.bend_max;
        p.voice = static_cast<std::uint8_t>(vo);
        p.program = static_cast<std::uint8_t>(program);
        p.tone = static_cast<std::uint16_t>(tone);
        p.note = static_cast<std::uint16_t>(note & 0x7F);
        p.note_start = static_cast<std::uint16_t>(note & 0x7F);
        p.channel = static_cast<std::uint8_t>(channel);
        p.stat = 1;
        m.mod_depth = 0;
        p.prog_volume = prog.volume;
        p.prog_pan = prog.pan;
        p.tone_volume = atr.volume;
        p.tone_pan = atr.pan;
        p.velocity = static_cast<std::uint8_t>(velocity);
        p.vib_depth_max = 0;
        if (m.vib_depth_max != 0) {
            p.vib_phase = 0;
            p.vib_wait_count = 0;
            p.vib_steps = 0;
            p.vib_wait = m.vib_wait;
            p.vib_speed = m.vib_speed;
            p.vib_add = m.vib_add;
            p.vib_depth_max = m.vib_depth_max;
            p.vib_count = m.vib_count;
        }
        p.vib_depth = 0;
        p.vib_data = 0;
        p.tre_depth_max = 0;
        if (m.tre_depth_max != 0) {
            p.tre_phase = 0;
            p.tre_wait_count = 0;
            p.tre_steps = 0;
            p.tre_wait = m.tre_wait;
            p.tre_speed = m.tre_speed;
            p.tre_add = m.tre_add;
            p.tre_depth_max = m.tre_depth_max;
            p.tre_count = m.tre_count;
        }
        p.tre_depth = 0;
        p.tre_data = 0;

        volume_calc(p, m);
        int left = (p.left * volume_left_) >> 7;
        int right = (p.right * volume_right_) >> 7;
        if (m.wide) {
            right = -right;
        }
        p.pedal = m.pedal;
        p.note = static_cast<std::uint16_t>(key & 0x7F);
        p.bend_seen = 0xFFFF;

        std::uint16_t pitch;
        if (m.bend_mode < 0x40) {
            auto cents = static_cast<std::int16_t>(p.vib_data + (m.mod_depth + m.porta_depth));
            cents = static_cast<std::int16_t>(cents + (p.note_start << 7) + pitch_bend(p, m.bend));
            pitch = note_to_pitch(cents >> 7, cents & 0x7F, atr.root_key, atr.fine_tune);
        } else {
            m.bend = 0x40;
            pitch = note_to_pitch(note, 0, atr.root_key, atr.fine_tune);
        }

        // The driver writes the attributes and keys the voice in one call, which it skips for a voice
        // whose volume works out to zero (a muted music layer) and for a held mode-126 voice.
        if (!held && (left | right) != 0) {
            spu_.set_volume(vo, left, right);
            spu_.set_pitch(vo, pitch);
            spu_.set_sample(vo, bank_.sample(atr.sample));
            spu_.set_adsr(vo, atr.adsr1, atr.adsr2);
            spu_.key_on(voice_bit(vo));
        }

        if (m.rev_depth == 0) {
            spu_.set_reverb_voices(voice_bit(vo), (atr.mode & 4) != 0);
        } else {
            spu_.set_reverb_voices(voice_bit(vo), m.rev_depth != 1);
        }
    }
}

void Sequencer::key_off(int channel, int key) {
    for (Port& p : ports_) {
        if (p.channel != channel || p.note != key) {
            continue;
        }
        if (p.pedal == 0) {
            spu_.set_adsr(p.voice, p.adsr1, p.adsr2);
            spu_.key_off(voice_bit(p.voice));
            p.stat = 0;
        } else {
            p.velocity |= 0x80;
        }
    }
}

// Data entry: NRPN 16/lsb edits the tones of the channel's program in the bank. The driver finds the tones at
// program * 16 here, not at the program's table as key_on does; kept.
void Sequencer::data_entry(int channel) {
    Channel& m = channels_[static_cast<std::size_t>(channel)];
    const auto edit = [&](BankTone& t, bool all) {
        const std::uint8_t d = m.data_entry;
        switch (m.nrpn_lsb) {
            case 1: t.mode = d; break;
            case 2: t.key_min = d; break;
            case 3: t.key_max = d; break;
            case 4: t.adsr1 = static_cast<std::uint16_t>((t.adsr1 & 0xFF) | ((0x7F - d) << 8)); break;
            case 5: t.adsr1 = static_cast<std::uint16_t>((t.adsr1 & 0xFF) | ((0x7F - d) << 8) | 0x8000); break;
            case 6: t.adsr1 = static_cast<std::uint16_t>((t.adsr1 & 0xFF0F) | ((d * 2) & 0xF0)); break;
            case 7: t.adsr1 = static_cast<std::uint16_t>((t.adsr1 & 0xFFF0) | (d >> 3)); break;
            case 8: t.adsr2 = static_cast<std::uint16_t>((t.adsr2 & 0x603F) | ((0x7F - d) << 6)); break;
            case 9: t.adsr2 = static_cast<std::uint16_t>((t.adsr2 & 0x603F) | ((0x7F - d) << 6) | 0x8000); break;
            case 10: t.adsr2 = static_cast<std::uint16_t>((t.adsr2 & 0xFFC0) | ((0x7F - d) >> 3)); break;
            case 11: t.adsr2 = static_cast<std::uint16_t>((t.adsr2 & 0xFFC0) | ((0x7F - d) >> 3) | 0x20); break;
            case 12: t.adsr2 = static_cast<std::uint16_t>(d <= 0x40 ? (t.adsr2 | 0x4000) : (t.adsr2 & 0xBFFF)); break;
            case 13: t.adsr1 = t.saved_adsr1; t.adsr2 = t.saved_adsr2; break;
            case 23: if (!all) m.before_note = d; break;
            default: break;
        }
    };
    const auto tone_at = [&](int tone) -> BankTone* {
        const std::size_t i = static_cast<std::size_t>(m.program) * 16 + static_cast<std::size_t>(tone);
        return i < bank_.tones.size() ? &bank_.tones[i] : nullptr;
    };
    if (m.nrpn_msb == 0x10 && m.nrpn_lsb < 0xE) {
        for (int i = 0; i < 16; ++i) {
            if (BankTone* t = tone_at(i)) {
                edit(*t, true);
            }
        }
    } else if (BankTone* t = tone_at(m.nrpn_msb & 0xF)) {
        edit(*t, false);
    }
}

void Sequencer::control_change(int channel, int controller, int value) {
    Channel& m = channels_[static_cast<std::size_t>(channel & 15)];
    bool volume = false;
    std::uint32_t pedal_release = 0;
    const auto retune = [&]() {
        m.mod_depth = 0;
        for (Port& p : ports_) {
            if (p.channel == channel) {
                pitch_calc(p, true);
            }
        }
    };
    switch (controller) {
        case 0x00: m.bank = static_cast<std::uint8_t>(value); break;
        case 0x01:
            m.mod = static_cast<std::uint8_t>(value);
            if (m.mod_mode < 0x40) {
                m.mod_limit = static_cast<std::int16_t>(value << 1);
                if (m.mod_speed != 0 && m.mod_limit != 0) {
                    m.mod_add = static_cast<std::int16_t>((m.mod_limit * 4) / m.mod_speed);
                } else {
                    retune();
                }
            } else {
                m.mod_speed = static_cast<std::uint16_t>(value);
                if (m.mod_speed != 0 && m.mod_limit != 0) {
                    m.mod_add = static_cast<std::int16_t>((m.mod_limit / m.mod_speed) * 2);
                } else {
                    retune();
                }
            }
            if (value == 0 || m.mod_add == 0) {
                retune();
            }
            break;
        case 0x02:
            if (m.mod_mode < 0x40) {
                m.mod_speed = static_cast<std::uint16_t>(value);
            } else {
                m.mod_limit = static_cast<std::int16_t>(value << 1);
            }
            break;
        case 0x03: m.mod_mode = static_cast<std::uint8_t>(value); break;
        case 0x04: m.bend_mode = static_cast<std::uint8_t>(value); break;
        case 0x05: m.porta = static_cast<std::int16_t>(value); break;
        case 0x07: m.volume = static_cast<std::uint8_t>(value); volume = true; break;
        case 0x0A: m.pan = static_cast<std::uint8_t>(value ? value : 1); volume = true; break;
        case 0x0B: m.expression = static_cast<std::uint8_t>(value); volume = true; break;
        case 0x0C: m.vol_mode = static_cast<std::uint8_t>(value); volume = true; break;
        case 0x0F: m.wide = static_cast<std::uint8_t>(value); break;
        case 0x10: m.key_pan = static_cast<std::uint16_t>(value); break;
        case 0x14: m.vib_wait = static_cast<std::uint8_t>(value << 1); break;
        case 0x15: m.vib_speed = static_cast<std::uint8_t>(value); break;
        case 0x16:
            m.vib_depth_max = static_cast<std::uint16_t>(value << 4);
            m.vib_count = 0;
            break;
        case 0x17:
            m.vib_count = static_cast<std::uint8_t>(value);
            if (value != 0 && m.vib_depth_max != 0) {
                m.vib_add = static_cast<std::uint16_t>(std::max(1, m.vib_depth_max / value));
            }
            break;
        case 0x19: m.tre_wait = static_cast<std::uint8_t>(value << 1); break;
        case 0x1A: m.tre_speed = static_cast<std::uint8_t>(value); break;
        case 0x1B:
            m.tre_depth_max = static_cast<std::uint16_t>(value << 7);
            m.tre_count = 0;
            break;
        case 0x1C:
            m.tre_count = static_cast<std::uint8_t>(value);
            if (value != 0 && m.tre_depth_max != 0) {
                m.tre_add = static_cast<std::uint16_t>(std::max(1, m.tre_depth_max / value));
            }
            break;
        case 0x1E:
            master_volume_ = static_cast<std::uint16_t>(value);
            for (Port& p : ports_) {
                set_voice_volume(p.voice, p.channel, p.left, p.right);
            }
            break;
        case 0x40:
            for (Port& p : ports_) {
                if (p.channel != channel) {
                    continue;
                }
                if (value < 0x40) {
                    for (Port& q : ports_) {
                        if (q.channel == channel && q.pedal != 0 && (q.velocity & 0x80)) {
                            pedal_release |= voice_bit(q.voice);
                            spu_.key_off(voice_bit(q.voice));
                            q.pedal = 0;
                            q.stat = 0;
                        }
                    }
                    p.pedal = 0;
                } else {
                    p.pedal = 1;
                }
            }
            m.pedal = value < 0x40 ? 0 : 1;
            break;
        case 0x5B: m.rev_depth = static_cast<std::uint8_t>(value & 0x7F); break;
        case 0x06:
            m.data_entry = static_cast<std::uint8_t>(value);
            if (m.nrpn_msb != 0x14 && m.nrpn_msb != 0x1E) {
                data_entry(channel & 15);
            }
            break;
        case 0x62: m.nrpn_lsb = static_cast<std::uint8_t>(value); break;
        case 0x63: m.nrpn_msb = static_cast<std::uint8_t>(value); break;
        case 0x76:
            if (value < 0x40) {
                if (value <= 0x20) {
                    beat_ = 0;
                    beat_stat_ = static_cast<std::uint8_t>(value);
                } else {
                    beat_stat_ = static_cast<std::uint8_t>(value - 0x21);
                }
            } else {
                control_stat_ = static_cast<std::uint8_t>(value);
            }
            break;
        case 0x77:
            for (BankTone& t : bank_.tones) {
                t.adsr1 = t.saved_adsr1;
                t.adsr2 = t.saved_adsr2;
            }
            break;
        case 0x78:
        case 0x79:
        case 0x7B: sound_off(); break;  // ends every track too
        case 0x7E: m.mode = static_cast<std::uint8_t>(value); break;
        default: break;  // 13/14 (random pitch) are not used by any song of the game and are not modelled
    }
    if (pedal_release) {
        spu_.key_off(pedal_release);
    }
    if (volume) {
        for (Port& p : ports_) {
            if (p.channel == channel && p.stat != 0) {
                volume_calc(p, m);
                set_voice_volume(p.voice, channel, p.left, p.right);
            }
        }
    }
}

}  // namespace sh::audio
