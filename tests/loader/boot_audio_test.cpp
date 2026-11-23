// The map-start audio tables and step machines (engine/loader/queue/boot_audio.h).

#include "engine/loader/queue/boot_audio.h"

#include "framework/sh_test.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>


namespace {

namespace ld = sh::loader;

struct Music : ld::MusicInitHost {
    bool busy = false, differs = true, idle = true;
    std::vector<std::string> log;
    bool audio_or_queue_busy() override { return busy; }
    void restore_kept_track() override { log.push_back("restore"); }
    bool track_differs() override { return differs; }
    void fade_out_and_mute() override { log.push_back("fade"); }
    bool sound_idle() override { return idle; }
    void load_map_track() override { log.push_back("load"); }
};

struct Ambient : ld::AmbientInitHost {
    bool busy = false, differs = true;
    std::vector<std::string> log;
    bool audio_or_queue_busy() override { return busy; }
    bool target_differs() override { return differs; }
    void fade_out_ambient() override { log.push_back("fade"); }
    void load_target_ambient() override { log.push_back("load"); }
};

}  // namespace

SH_TEST(boot_audio_shipped_tables) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/config/boot_audio.json", std::ios::binary);
    SH_CHECK(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto a = ld::BootAudio::parse(text);
    SH_CHECK(static_cast<bool>(a));
    SH_CHECK(*a.value().bgm_load(2) == 32);
    SH_CHECK(*a.value().bgm_load(41) == 63);
    SH_CHECK(!a.value().bgm_load(42));
    SH_CHECK(*a.value().bgm_channels(2) == 769);
    SH_CHECK(*a.value().ambient_load(39) == 203);
    SH_CHECK(!a.value().ambient_load(-1));
}

SH_TEST(boot_audio_rejects_bad_tables) {
    SH_CHECK(!ld::BootAudio::parse("{}"));
    SH_CHECK(!ld::BootAudio::parse(
        R"({"bgm_load_commands":[1],"bgm_channel_commands":[70000],"ambient_load_commands":[1]})"));
}

SH_TEST(boot_audio_music_steps) {
    Music m;
    std::int32_t step = 0;
    m.busy = true;
    SH_CHECK(ld::music_init_step(step, m) == ld::BootStep::Wait && step == 0);
    m.busy = false;
    SH_CHECK(ld::music_init_step(step, m) == ld::BootStep::Run && step == 2);  // restore, fade
    m.idle = false;
    SH_CHECK(ld::music_init_step(step, m) == ld::BootStep::Run && step == 2);
    m.idle = true;
    SH_CHECK(ld::music_init_step(step, m) == ld::BootStep::Run && step == 3);
    SH_CHECK(ld::music_init_step(step, m) == ld::BootStep::Done);
    SH_CHECK((m.log == std::vector<std::string>{"restore", "fade", "load"}));

    Music same;
    same.differs = false;
    step = 0;
    SH_CHECK(ld::music_init_step(step, same) == ld::BootStep::Run && step == 3);
    SH_CHECK(ld::music_init_step(step, same) == ld::BootStep::Done);
    SH_CHECK((same.log == std::vector<std::string>{"restore"}));
}

SH_TEST(boot_audio_ambient_steps) {
    Ambient a;
    std::int32_t step = 0;
    SH_CHECK(ld::ambient_init_step(step, a) == ld::BootStep::Run && step == 1);
    SH_CHECK(ld::ambient_init_step(step, a) == ld::BootStep::Run && step == 2);
    SH_CHECK(ld::ambient_init_step(step, a) == ld::BootStep::Done);
    SH_CHECK((a.log == std::vector<std::string>{"fade", "load"}));

    Ambient same;
    same.differs = false;
    step = 0;
    SH_CHECK(ld::ambient_init_step(step, same) == ld::BootStep::Done && step == 0);
}
