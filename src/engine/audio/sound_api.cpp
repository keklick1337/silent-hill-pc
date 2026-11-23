#include "engine/audio/sound_api.h"

#include "engine/audio/engine.h"

#include <cstdlib>
#include <cstring>
#include <string>

using sh::audio::audio_engine;

namespace {

ShSoundReadFn g_read = nullptr;

sh::Result<std::vector<std::uint8_t>> read_converted(std::string_view relative) {
    using R = sh::Result<std::vector<std::uint8_t>>;
    if (!g_read) {
        return R::fail("no reader");
    }
    std::size_t size = 0;
    const std::string path(relative);
    unsigned char* data = g_read(path.c_str(), &size);
    if (!data) {
        return R::fail(path + ": not found");
    }
    std::vector<std::uint8_t> bytes(data, data + size);
    std::free(data);
    return R::ok(std::move(bytes));
}

}  // namespace

extern "C" {

void ShSound_SetReader(ShSoundReadFn read) {
    g_read = read;
    audio_engine().set_reader(read ? sh::audio::ConvertedFileReader(read_converted) : sh::audio::ConvertedFileReader());
}

void ShSound_Render(int16_t* stereo, unsigned frames) { audio_engine().render(stereo, frames); }

int ShSound_LoadBank(int slot, const char* name) { return audio_engine().load_bank(slot, name ? name : "") ? 1 : 0; }
void ShSound_CloseBank(int slot) { audio_engine().close_bank(slot); }

int ShSound_KeyOnProgram(int slot, int program, int note, int fine, int left, int right) {
    return audio_engine().key_on_program(slot, program, note, fine, left, right);
}
int ShSound_KeyOnTone(int slot, int program, int tone, int note, int fine, int left, int right) {
    return audio_engine().key_on_tone(slot, program, tone, note, fine, left, right);
}
int ShSound_KeyOnVoice(int voice, int slot, int program, int tone, int note, int fine, int left, int right) {
    return audio_engine().key_on_voice(voice, slot, program, tone, note, fine, left, right);
}
void ShSound_KeyOffProgram(int slot, int program, int note, int cut) {
    audio_engine().key_off_program(slot, program, note, cut != 0);
}
int ShSound_KeyOffVoice(int voice, int cut) { return audio_engine().key_off_voice(voice, cut != 0); }
int ShSound_KeyStatus(int voice) { return static_cast<int>(audio_engine().key_status(voice)); }
int ShSound_VoicePitch(int voice) { return audio_engine().voice_pitch(voice); }
void ShSound_SetVoicePitch(int voice, int pitch) {
    audio_engine().set_voice_pitch(voice, static_cast<std::uint16_t>(pitch));
}
void ShSound_SetVoiceVolume(int voice, int left, int right) { audio_engine().set_voice_volume(voice, left, right); }
unsigned ShSound_TakeReleasedVoices(void) { return audio_engine().take_released_voices(); }
void ShSound_SetMono(int mono) { audio_engine().set_mono(mono != 0); }
void ShSound_SetMainExempt(int on) { audio_engine().set_main_exempt(on != 0); }

int ShSound_PlayFile(const char* path, int tag, int left, int right, float semitones, int loop) {
    return path != nullptr && audio_engine().play_file(path, tag, left, right, semitones, loop != 0) ? 1 : 0;
}
void ShSound_StopFile(int tag) { audio_engine().stop_file(tag); }
void ShSound_SetFileVolume(int tag, int left, int right) { audio_engine().set_file_volume(tag, left, right); }

void ShSound_SetMusicSlot(int slot) { audio_engine().set_music_slot(slot); }
int ShSound_OpenSong(const char* name) { return audio_engine().open_song(name ? name : "") ? 1 : 0; }
void ShSound_SongPlay(void) { audio_engine().song_play(); }
void ShSound_SongStop(void) { audio_engine().song_stop(); }
void ShSound_SongClose(void) { audio_engine().song_close(); }
void ShSound_SetSongVolume(int left, int right) { audio_engine().set_song_volume(left, right); }
void ShSound_SetChannelVolume(int channel, int volume) { audio_engine().set_channel_volume(channel, volume); }
int ShSound_ChannelVolume(int channel) { return audio_engine().channel_volume(channel); }

void ShSound_SetMainVolume(int left, int right) { audio_engine().set_main_volume(left, right); }
void ShSound_SetReverbDepth(int left, int right) { audio_engine().set_reverb_depth(left, right); }
void ShSound_SetLineGain(float gain) { audio_engine().set_voice_gain(gain); }

int ShSound_LineStart(const char* track, unsigned sector, unsigned length_vblanks) {
    return audio_engine().voice_start(track ? track : "", sector, length_vblanks) ? 1 : 0;
}
void ShSound_LineStartPcm(const int16_t* pcm, unsigned frames, int channels, unsigned rate, int side) {
    const std::size_t n = static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels == 1 ? 1 : 2);
    auto copy = std::make_shared<const std::vector<std::int16_t>>(pcm, pcm + n);
    audio_engine().voice_start_pcm(std::move(copy), channels, rate, side != 0);
}
void ShSound_LineStop(int side) { audio_engine().voice_stop(side != 0); }
int ShSound_LineActive(int side) { return audio_engine().voice_active(side != 0) ? 1 : 0; }
int ShSound_LineDrained(int side) { return audio_engine().voice_drained(side != 0) ? 1 : 0; }
void ShSound_LinePause(int paused) { audio_engine().voice_set_paused(paused != 0); }
void ShSound_SetLineVolume(int left, int right) { audio_engine().set_voice_volume(left, right); }
void ShSound_SetLineMix(int on) { audio_engine().set_voice_mix(on != 0); }

}  // extern "C"
