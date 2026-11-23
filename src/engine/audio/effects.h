#pragma once

// Sound effects: the 24 voices the game keys its effects on, with the voice allocation, release and volume rules of
// the console sound driver it shipped with.
//
// A voice is keyed from a bank slot (0..15: base sounds, weapon, ambience, music instruments, ...), a program, a
// tone and a key. What the game reads back depends on the rules, so they are kept exactly:
//  - a free voice is the first whose key status is Off (released and silent), else the first whose port is idle;
//  - looking a key up through a program finds its tones through the program's table, but keying a known tone reads
//    the tone at program * 16 + slot;
//  - the volume is master x program x tone volume with the bank, program and tone pans, then the caller's volume;
//  - a voice whose sample ended without a loop stays keyed until the automatic release frees it two driver frames
//    later (released_voices() reports it, so the game can forget which sound it held).

#include "engine/audio/sound_bank.h"
#include "engine/audio/synth.h"

#include <array>
#include <cstdint>
#include <memory>

namespace sh::audio {

inline constexpr int kBankSlots = 16;

class Effects {
public:
    explicit Effects(Synth& synth);

    void set_bank(int slot, std::shared_ptr<const SoundBank> bank);
    std::shared_ptr<const SoundBank> bank(int slot) const;

    // Keys every tone of `program` whose key range holds `note`, each on a free voice; the voice of the last one,
    // or -1. `fine` is 1/128 semitone.
    int key_on_program(int slot, int program, int note, int fine, int left, int right);
    // A known tone on a free voice; the voice or -1.
    int key_on_tone(int slot, int program, int tone, int note, int fine, int left, int right);
    // A known tone on `voice`; the voice or -1.
    int key_on_voice(int voice, int slot, int program, int tone, int note, int fine, int left, int right);

    // Releases every effect voice playing `note` of `program`. `cut` silences it at once instead of letting the
    // tone's release run.
    void key_off_program(int slot, int program, int note, bool cut);
    // 0, or -1 when the voice holds no effect.
    int key_off_voice(int voice, bool cut);

    // Two driver frames after a voice fell silent with its key still on, it is released (call at the driver's
    // slow rate, every 11th timer interrupt).
    void auto_release();
    // Voices auto_release() freed since the last call, as a bit mask.
    std::uint32_t take_released_voices();

    void set_mono(bool mono) { mono_ = mono; }
    // Voices keyed while set are left out of the game's main volume (the pause menu's cues sound under its mute).
    void set_main_exempt(bool on) { exempt_ = on; }

    KeyStatus key_status(int voice) const { return synth_.key_status(voice); }
    bool port_busy(int voice) const { return ports_[static_cast<std::size_t>(voice)].busy != 0; }
    std::uint16_t pitch(int voice) const { return synth_.pitch(voice); }
    void set_pitch(int voice, std::uint16_t pitch) { synth_.set_pitch(voice, pitch); }
    // Library units (0..0x3FFF).
    void set_volume(int voice, int left, int right) { synth_.set_volume(voice, left, right); }
    int volume_left(int voice) const { return synth_.volume_left(voice); }
    int volume_right(int voice) const { return synth_.volume_right(voice); }

private:
    struct Port {
        std::uint8_t busy = 0;  // 0 idle, 1 keyed, 2.. counting towards the automatic release
        bool effect = false;    // keyed by this class (the driver's effect channel)
        std::uint8_t slot = 0, program = 0, tone = 0, note = 0;
        std::uint16_t adsr1 = 0, adsr2 = 0;
    };

    int free_voice() const;

    Synth& synth_;
    std::array<std::shared_ptr<const SoundBank>, kBankSlots> banks_{};
    std::array<Port, kVoices> ports_{};
    std::uint32_t released_ = 0;
    bool mono_ = false;
    bool exempt_ = false;
};

}  // namespace sh::audio
