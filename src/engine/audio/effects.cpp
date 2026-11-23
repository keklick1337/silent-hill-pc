#include "engine/audio/effects.h"

#include "engine/audio/sequencer.h"

#include <algorithm>

namespace sh::audio {
namespace {

constexpr std::uint32_t voice_bit(int voice) { return 1u << voice; }

}  // namespace

Effects::Effects(Synth& synth) : synth_(synth) {}

void Effects::set_bank(int slot, std::shared_ptr<const SoundBank> bank) {
    if (slot >= 0 && slot < kBankSlots) {
        banks_[static_cast<std::size_t>(slot)] = std::move(bank);
    }
}

std::shared_ptr<const SoundBank> Effects::bank(int slot) const {
    return slot >= 0 && slot < kBankSlots ? banks_[static_cast<std::size_t>(slot)] : nullptr;
}

int Effects::free_voice() const {
    for (int vo = 0; vo < kVoices; ++vo) {
        if (synth_.key_status(vo) == KeyStatus::Off) {
            return vo;
        }
    }
    for (int vo = 0; vo < kVoices; ++vo) {
        if (ports_[static_cast<std::size_t>(vo)].busy == 0) {
            return vo;
        }
    }
    return -1;
}

int Effects::key_on_voice(int voice, int slot, int program, int tone, int note, int fine, int left, int right) {
    if (tone < 0 || tone >= 16 || program < 0 || program >= 128 || note < 0 || note >= 128) {
        return -1;
    }
    if (voice < 0 || voice >= kVoices) {
        return -1;
    }
    const std::shared_ptr<const SoundBank> b = bank(slot);
    if (!b) {
        return -1;
    }
    const BankTone* atr = b->tone(static_cast<std::size_t>(program) * 16 + static_cast<std::size_t>(tone));
    if (!atr || atr->sample == 0) {
        return -1;
    }
    if (note < atr->key_min || note > atr->key_max) {
        return -1;
    }
    const BankProgram& prog = b->programs[static_cast<std::size_t>(program)];

    // Fastest release, then the key off: the voice is silent before it is keyed again.
    synth_.set_adsr(voice, 0, 0);
    synth_.key_off(voice_bit(voice));

    Port& p = ports_[static_cast<std::size_t>(voice)];
    p.adsr1 = atr->adsr1;
    p.adsr2 = atr->adsr2;
    p.slot = static_cast<std::uint8_t>(slot);
    p.program = static_cast<std::uint8_t>(program);
    p.tone = static_cast<std::uint8_t>(tone);
    p.note = static_cast<std::uint8_t>(note);
    p.effect = true;
    p.busy = 1;

    const int pan = std::clamp(b->master_pan + prog.pan + atr->pan - 0x80, 0, 0x7F);
    const int master = ((b->master_volume * prog.volume) * atr->volume) >> 7;
    int l, r;
    if (pan >= 0x40) {
        r = master;
        l = ((0x40 - (pan & 0x3F)) * (r * 2)) >> 7;
    } else {
        l = master;
        r = (pan * (l * 2)) >> 7;
    }
    l = (l * b->master_volume) >> 7;
    r = (r * b->master_volume) >> 7;
    int out_left = (l * left) >> 7;
    int out_right = (r * right) >> 7;
    if (mono_) {
        out_left = out_right = (out_left + out_right) >> 1;
    }

    synth_.set_volume(voice, out_left, out_right);
    synth_.set_pitch(voice, note_to_pitch(note, fine, atr->root_key, atr->fine_tune));
    synth_.set_sample(voice, b->sample(atr->sample));
    synth_.set_adsr(voice, atr->adsr1, atr->adsr2);
    synth_.set_key_on_tag(exempt_ ? 1 : 0);
    synth_.key_on(voice_bit(voice));
    synth_.set_reverb_voices(voice_bit(voice), (atr->mode & 4) != 0);
    return voice;
}

int Effects::key_on_tone(int slot, int program, int tone, int note, int fine, int left, int right) {
    const int vo = free_voice();
    return vo < 0 ? -1 : key_on_voice(vo, slot, program, tone, note, fine, left, right);
}

int Effects::key_on_program(int slot, int program, int note, int fine, int left, int right) {
    const std::shared_ptr<const SoundBank> b = bank(slot);
    if (!b || program < 0 || program >= 128) {
        return -1;
    }
    const std::size_t table = b->table_of(program);
    const BankProgram& prog = b->programs[static_cast<std::size_t>(program)];
    int voice = -1;
    for (int tone = 0; tone < prog.tones; ++tone) {
        const BankTone* atr = b->tone(table * 16 + static_cast<std::size_t>(tone));
        if (!atr || atr->sample == 0 || atr->key_min > note || atr->key_max < note) {
            continue;
        }
        voice = free_voice();
        if (voice >= 0) {
            voice = key_on_voice(voice, slot, program, tone, note, fine, left, right);
        }
    }
    return voice;
}

void Effects::key_off_program(int slot, int program, int note, bool cut) {
    std::uint32_t voices = 0;
    for (int vo = 0; vo < kVoices; ++vo) {
        Port& p = ports_[static_cast<std::size_t>(vo)];
        if (p.busy == 0 || !p.effect || p.note != note || p.slot != slot || p.program != program) {
            continue;
        }
        p.busy = 0;
        p.note = 0;
        synth_.set_adsr(vo, cut ? 0 : p.adsr1, cut ? 0 : p.adsr2);
        voices |= voice_bit(vo);
    }
    synth_.key_off(voices);
}

int Effects::key_off_voice(int voice, bool cut) {
    if (voice < 0 || voice >= kVoices) {
        return -1;
    }
    Port& p = ports_[static_cast<std::size_t>(voice)];
    if (p.busy == 0 || !p.effect) {
        return -1;
    }
    p.busy = 0;
    p.note = 0;
    synth_.set_adsr(voice, cut ? 0 : p.adsr1, cut ? 0 : p.adsr2);
    synth_.key_off(voice_bit(voice));
    return 0;
}

void Effects::auto_release() {
    for (int vo = 0; vo < kVoices; ++vo) {
        Port& p = ports_[static_cast<std::size_t>(vo)];
        if (p.busy == 0 || synth_.key_status(vo) != KeyStatus::OnEnvOff) {
            continue;
        }
        if (p.busy >= 2) {
            synth_.key_off(voice_bit(vo));
            p.busy = 0;
            released_ |= voice_bit(vo);
        } else {
            ++p.busy;
        }
    }
}

std::uint32_t Effects::take_released_voices() {
    const std::uint32_t r = released_;
    released_ = 0;
    return r;
}

}  // namespace sh::audio
