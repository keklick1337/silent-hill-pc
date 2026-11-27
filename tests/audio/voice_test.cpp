// The voice side of the game's sound: a mod's voice recording made ready for the mixer (voice_clip.h), the
// model of a line's start delay (line_start_delay.h) and the voices of text boxes (message_voice.h). Expected
// values are worked out by hand in the comments.

#include "engine/audio/disc_drive.h"
#include "engine/audio/line_start_delay.h"
#include "engine/audio/message_voice.h"
#include "engine/audio/voice_clip.h"

#include "framework/sh_test.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

namespace au = sh::audio;

void put16(std::vector<std::uint8_t>& out, unsigned value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put32(std::vector<std::uint8_t>& out, unsigned value) {
    put16(out, value & 0xFFFF);
    put16(out, value >> 16);
}

void put_tag(std::vector<std::uint8_t>& out, const char* tag) { out.insert(out.end(), tag, tag + 4); }

// A WAV file with one format chunk and one data chunk.
std::vector<std::uint8_t> wav(unsigned tag, unsigned channels, unsigned rate, unsigned bits,
                              const std::vector<std::uint8_t>& data, bool extensible = false) {
    std::vector<std::uint8_t> out;
    put_tag(out, "RIFF");
    put32(out, 0);
    put_tag(out, "WAVE");
    put_tag(out, "fmt ");
    put32(out, extensible ? 40 : 16);
    put16(out, extensible ? 0xFFFE : tag);
    put16(out, channels);
    put32(out, rate);
    put32(out, rate * channels * bits / 8);
    put16(out, channels * bits / 8);
    put16(out, bits);
    if (extensible) {
        put16(out, 22);
        put16(out, bits);
        put32(out, 3);
        put16(out, tag);  // the sub-format begins with the real tag
        out.insert(out.end(), 14, 0);
    }
    put_tag(out, "data");
    put32(out, static_cast<unsigned>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    if (data.size() & 1) out.push_back(0);
    return out;
}

std::vector<std::uint8_t> samples16(const std::vector<int>& values) {
    std::vector<std::uint8_t> out;
    for (const int value : values) put16(out, static_cast<unsigned>(value) & 0xFFFF);
    return out;
}

// A host of MessageVoice that records what it is asked.
struct VoiceHost {
    std::vector<std::string> played;
    std::string only_file;  // the one file the "mod" has
    int stops = 0;
    bool audible = false;
    std::uint32_t now = 1000;
    std::uint32_t tail = 500;
    bool paused = false;
    std::vector<bool> holds;

    au::MessageVoice::Host host() {
        au::MessageVoice::Host h;
        h.message_key = [](int index) { return index < 0 ? std::string() : "MAP1_S00." + std::to_string(index); };
        h.play = [this](const std::string& path) {
            played.push_back(path);
            return path == only_file;
        };
        h.stop = [this] { ++stops; };
        h.audible = [this] { return audible; };
        h.now_ms = [this] { return now; };
        h.tail_ms = [this] { return tail; };
        h.paused = [this] { return paused; };
        h.hold = [this](bool held) { holds.push_back(held); };
        return h;
    }
};

}  // namespace

SH_TEST(voice_clip_reads_pcm_wav_of_the_voice_rates_unchanged) {
    // 16-bit stereo at 37.8 kHz: the frames as they are.
    const auto stereo = au::parse_voice_wav(wav(1, 2, 37800, 16, samples16({100, -100, 32767, -32768, 7, 8})));
    SH_CHECK(stereo && stereo.value().channels == 2 && stereo.value().rate == 37800 && stereo.value().frames() == 3);
    SH_CHECK(stereo.value().pcm == (std::vector<std::int16_t>{100, -100, 32767, -32768, 7, 8}));

    // 8-bit mono at 18.9 kHz: unsigned around 128, widened by 256 — 0 is -32768, 128 is 0, 255 is 32512.
    const auto low = au::parse_voice_wav(wav(1, 1, 18900, 8, {0, 128, 255, 129, 127}));
    SH_CHECK(low && low.value().channels == 1 && low.value().rate == 18900);
    SH_CHECK(low.value().pcm == (std::vector<std::int16_t>{-32768, 0, 32512, 256, -256}));

    // The extensible header names PCM in its sub-format. A frame cut short at the end of the data is dropped.
    std::vector<std::uint8_t> data = samples16({1, 2, 3, 4});
    data.push_back(9);
    const auto ext = au::parse_voice_wav(wav(1, 2, 37800, 16, data, true));
    SH_CHECK(ext && ext.value().pcm == (std::vector<std::int16_t>{1, 2, 3, 4}));

    // A data chunk that claims more than the file holds is cut at the end of the file.
    std::vector<std::uint8_t> lying = wav(1, 1, 37800, 16, samples16({5, 6, 7}));
    lying[40] = 0xFF;
    lying[41] = 0xFF;
    const auto cut = au::parse_voice_wav(lying);
    SH_CHECK(cut && cut.value().pcm == (std::vector<std::int16_t>{5, 6, 7}));
}

SH_TEST(voice_clip_refuses_what_the_mixer_cannot_play) {
    const std::vector<std::uint8_t> ok = wav(1, 1, 37800, 16, samples16({1, 2, 3, 4}));
    SH_CHECK(au::parse_voice_wav(ok));
    std::vector<std::uint8_t> not_riff = ok;
    not_riff[0] = 'X';
    SH_CHECK(!au::parse_voice_wav(not_riff) && !au::parse_voice_wav(not_riff).error().empty());
    SH_CHECK(!au::parse_voice_wav(std::vector<std::uint8_t>(ok.begin(), ok.begin() + 43)));  // shorter than a header
    SH_CHECK(!au::parse_voice_wav(wav(3, 2, 37800, 32, std::vector<std::uint8_t>(16, 0))));  // floating point
    SH_CHECK(!au::parse_voice_wav(wav(1, 2, 37800, 24, std::vector<std::uint8_t>(12, 0))));  // 24 bits
    SH_CHECK(!au::parse_voice_wav(wav(1, 6, 37800, 16, std::vector<std::uint8_t>(24, 0))));  // surround
    SH_CHECK(!au::parse_voice_wav(wav(1, 1, 3999, 16, samples16({1, 2}))));
    SH_CHECK(!au::parse_voice_wav(wav(1, 1, 96001, 16, samples16({1, 2}))));
    SH_CHECK(au::parse_voice_wav(wav(1, 1, 96000, 16, samples16({1, 2}))));
    std::vector<std::uint8_t> no_data = ok;
    std::memcpy(&no_data[36], "junk", 4);
    SH_CHECK(!au::parse_voice_wav(no_data));
    const auto missing = au::load_voice_wav("/nonexistent/sh_tests_voice.wav");
    SH_CHECK(!missing && missing.error().empty());
}

SH_TEST(voice_clip_brings_other_rates_to_the_voice_rate) {
    // 75.6 kHz is twice the voice rate: every second frame, nothing blended.
    const std::vector<std::int16_t> ramp{0, 10, 20, 30, 40, 50, 60};
    SH_CHECK(au::resample_voice(ramp, 1, 75600) == (std::vector<std::int16_t>{0, 20, 40}));

    // 25.2 kHz is two thirds of it: 4 frames become 4 * 37800 / 25200 = 6, output frame n lying at input
    // position n * 2 / 3. Between 0 and 300: 0, 200, (300 + 1/3 of the way to -300 =) 100, -300, then between
    // -300 and 30: -300 + 2/3 * 330 = -80, 30 + 1/3 * 0 = 30 (past the last frame the last one holds).
    const std::vector<std::int16_t> four{0, 300, -300, 30};
    SH_CHECK(au::resample_voice(four, 1, 25200) == (std::vector<std::int16_t>{0, 200, 100, -300, -80, 30}));

    // The blend is cut towards zero, not rounded: 2/3 of 100 is 66, 2/3 of -100 is -66, 1/3 of -100 is -33.
    SH_CHECK(au::resample_voice(std::vector<std::int16_t>{0, 100, 0}, 1, 25200) ==
             (std::vector<std::int16_t>{0, 66, 67, 0}));
    SH_CHECK(au::resample_voice(std::vector<std::int16_t>{0, -100, 0}, 1, 25200) ==
             (std::vector<std::int16_t>{0, -66, -67, 0}));

    // Stereo: each channel by itself.
    const std::vector<std::int16_t> stereo{0, 1000, 300, 700, -300, 400, 30, 100};
    SH_CHECK(au::resample_voice(stereo, 2, 25200) ==
             (std::vector<std::int16_t>{0, 1000, 200, 800, 100, 600, -300, 400, -80, 200, 30, 100}));

    // Through the reader: 44.1 kHz comes out at 37.8 kHz with 7 * 37800 / 44100 = 6 frames, the others unchanged.
    const auto clip = au::parse_voice_wav(wav(1, 1, 44100, 16, samples16({0, 600, 1200, 1800, 2400, 3000, 3600})));
    SH_CHECK(clip && clip.value().rate == 37800);
    // Output n lies at n * 7 / 6: 0, 700, 1400, 2100, 2800, 3500.
    SH_CHECK(clip.value().pcm == (std::vector<std::int16_t>{0, 700, 1400, 2100, 2800, 3500}));
}

SH_TEST(line_start_delay_counts_the_consoles_frames) {
    using Delay = au::LineStartDelay;
    Delay delay;
    const int seek_here = au::DiscDrive::seek_vblanks(1000, 1000, 60);
    SH_CHECK(seek_here == 3);  // the shortest seek of the drive model at 60 blanks a second

    // A first line, the head already there, frames of 2 blanks: the 7 fixed frames and (3 - 1) / 2 = 1 frame of
    // waiting for the seek — 8 frames, 16 blanks.
    Delay::Estimate e = delay.line_requested({100, 2, 60}, {1000, 5}, 1000, 84, 300);
    SH_CHECK(e.head_lba == 1000 && e.seek_vblanks == 3 && e.stop_vblanks_left == 0 && e.seek_wait_frames == 1 &&
             e.delay_vblanks == 16);
    SH_CHECK(delay.take_delay() == 16 && delay.take_delay() == 0);

    // The stop task at blank 200: volume 84 fades by 24 a frame — 60, 36, 12, below 2 after the fourth frame.
    // The disc is paused one frame later (blank 200 + 5 * 2 = 210) and the task ends after 3 more frames in all
    // (blank 200 + 7 * 2 = 214).
    delay.line_stopped({200, 2, 60}, 5);
    // The line began to read at blank 100 + 16 = 116 from sector 1000 at 150 sectors a second: by the pause it
    // read 94 blanks * 150 / 60 = 235 sectors, so the head is at 1235 whatever the drive says.
    e = delay.line_requested({202, 2, 60}, {9999, 5}, 1235, 84, 300);
    SH_CHECK(e.head_lba == 1235 && e.seek_vblanks == 3 && e.stop_vblanks_left == 12 && e.delay_vblanks == 12 + 16);
    SH_CHECK(delay.take_delay() == 28);

    // Something else moved the head since (another epoch): the drive's own position counts.
    e = delay.line_requested({1000, 2, 60}, {9999, 6}, 9999, 84, 300);
    SH_CHECK(e.head_lba == 9999 && e.stop_vblanks_left == 0 && e.delay_vblanks == 16);

    // The setting scales the delay: twice the console's gap doubles it, none removes it.
    SH_CHECK(delay.line_requested({2000, 2, 60}, {5000, 7}, 5000, 84, 600).delay_vblanks == 32);
    SH_CHECK(delay.line_requested({3000, 2, 60}, {5000, 8}, 5000, 84, 0).delay_vblanks == 0);
    // Never more than three seconds: 180 blanks at 60 a second, 150 at 50.
    SH_CHECK(delay.line_requested({4000, 2, 60}, {5000, 9}, 5000, 84, 30000).delay_vblanks == 180);
    SH_CHECK(delay.line_requested({5000, 2, 50}, {5000, 10}, 5000, 84, 30000).delay_vblanks == 150);

    // A far seek waits (seek - 1) / frame frames; a frame as long as the seek waits none.
    const int far = au::DiscDrive::seek_vblanks(5000, 150000, 60);
    SH_CHECK(far > 3);
    e = delay.line_requested({6000, 3, 60}, {5000, 11}, 150000, 84, 300);
    SH_CHECK(e.seek_wait_frames == (far - 1) / 3 && e.delay_vblanks == (7 + (far - 1) / 3) * 3);
    e = delay.line_requested({7000, 3, 60}, {5000, 12}, 5000, 84, 300);
    SH_CHECK(e.seek_wait_frames == 0 && e.delay_vblanks == 21);
    // Frame lengths outside 1..4 blanks: none counts as 2, more as 4.
    SH_CHECK(delay.line_requested({8000, 0, 60}, {5000, 13}, 5000, 84, 300).delay_vblanks == 16);
    SH_CHECK(delay.line_requested({9000, 9, 60}, {5000, 14}, 5000, 84, 300).delay_vblanks == 28);

    // A stop with nothing to fade (volume 0) still takes its frames: 1 + 3 frames of 2 blanks.
    Delay quiet;
    quiet.line_stopped({50, 2, 60}, 0);
    SH_CHECK(quiet.line_requested({50, 2, 60}, {0, 0}, 0, 84, 300).stop_vblanks_left == 8);
    quiet.forget_delay();
    SH_CHECK(quiet.take_delay() == 0);
}

SH_TEST(message_voice_holds_the_page_that_started_a_file) {
    VoiceHost host;
    host.only_file = "XA/msg_MAP1_S00_23.wav";
    au::MessageVoice voice(host.host());
    SH_CHECK(au::MessageVoice::mod_path("COMMON.5") == "XA/msg_COMMON_5.wav");
    // A mod's message goes by its key, the namespace too: a file name has no ':' on every system.
    SH_CHECK(au::MessageVoice::mod_path("mymod:hello.2") == "XA/msg_mymod_hello_2.wav");

    // A page with a file: it plays, and the page is held while it is heard and for the tail after.
    voice.page_started(23);
    SH_CHECK(host.played == (std::vector<std::string>{"XA/msg_MAP1_S00_23.wav"}));
    host.audible = true;
    SH_CHECK(voice.page_held());
    host.now = 3000;
    SH_CHECK(voice.page_held());  // the tail now ends at 3500
    host.audible = false;
    host.now = 3499;
    SH_CHECK(voice.page_held());
    host.now = 3500;
    SH_CHECK(!voice.page_held());
    host.now = 3400;  // the tail does not come back
    SH_CHECK(!voice.page_held());

    // The next page has no file: it rides the recording and is not held, even while it is heard.
    voice.page_started(24);
    host.audible = true;
    SH_CHECK(host.played.size() == 2 && !voice.page_held() && host.stops == 0);
    voice.box_closed();
    SH_CHECK(host.stops == 1);
    voice.box_closed();
    SH_CHECK(host.stops == 1);  // nothing plays any more

    // A message without a key, and a message the game voices itself, ask for nothing.
    voice.page_started(-1);
    voice.set_game_voiced(true);
    voice.page_started(23);
    SH_CHECK(host.played.size() == 2);
    voice.set_game_voiced(false);

    // The clock wrapping round during the tail releases the page instead of holding it for ever.
    host.now = 0xFFFFFF00u;
    host.audible = true;
    voice.page_started(23);
    SH_CHECK(voice.page_held());  // the tail ends at 0xFFFFFF00 + 500, past the wrap
    host.audible = false;
    host.now = 100;
    SH_CHECK(voice.page_held());
    host.now = 300;
    SH_CHECK(!voice.page_held());
    voice.box_closed();
}

SH_TEST(message_voice_stops_a_file_whose_box_is_gone) {
    VoiceHost host;
    host.only_file = "XA/msg_MAP1_S00_7.wav";
    au::MessageVoice voice(host.host());
    voice.page_started(7);
    // Drawn every frame: it goes on.
    for (int frame = 0; frame < 10; ++frame) {
        voice.box_drawn();
        voice.frame_ended();
    }
    SH_CHECK(host.stops == 0);
    // Two frames without the box are tolerated, the third stops the file.
    voice.frame_ended();
    voice.frame_ended();
    SH_CHECK(host.stops == 0);
    voice.box_drawn();
    voice.frame_ended();
    voice.frame_ended();
    voice.frame_ended();
    SH_CHECK(host.stops == 0);
    voice.frame_ended();
    SH_CHECK(host.stops == 1);
    // Nothing plays: frames without a box stop nothing.
    for (int frame = 0; frame < 10; ++frame) voice.frame_ended();
    SH_CHECK(host.stops == 1);
}

SH_TEST(message_voice_holds_its_file_while_the_world_is_held) {
    VoiceHost host;
    host.only_file = "XA/msg_MAP1_S00_7.wav";
    au::MessageVoice voice(host.host());
    voice.page_started(7);
    voice.box_drawn();
    voice.frame_ended();
    SH_CHECK(host.holds.back() == false);
    // The pause hides the box for many frames: the file is held, not stopped.
    host.paused = true;
    for (int frame = 0; frame < 30; ++frame) voice.frame_ended();
    SH_CHECK(host.stops == 0 && host.holds.back() == true);
    // Back in play the box is drawn again and the file goes on.
    host.paused = false;
    voice.box_drawn();
    voice.frame_ended();
    SH_CHECK(host.stops == 0 && host.holds.back() == false);
    // With nothing playing a pause holds nothing.
    voice.box_closed();
    host.paused = true;
    voice.frame_ended();
    SH_CHECK(host.holds.back() == false);
}
