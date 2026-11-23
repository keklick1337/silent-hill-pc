// The sound driver rules (engine/audio/sound_rules.h, gamedata/config/sound_driver.json) and the sound distance
// measures of engine/audio/positional_sound.h.

#include "engine/audio/positional_sound.h"
#include "engine/audio/sound_rules.h"

#include "framework/sh_test.h"

#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace {

namespace au = sh::audio;

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

au::SoundRules base_rules() {
    auto parsed = au::SoundRules::parse(read_text(std::string(SH_PROJECT_ROOT) + "/gamedata/config/sound_driver.json"));
    SH_CHECK(static_cast<bool>(parsed));
    return parsed.value();
}

// The base game's numbers of the sounds the rules name.
int base_number(std::string_view name) {
    static const std::map<std::string, int, std::less<>> numbers = {
        {"harry_body_fall", 1317},     {"harry_death_cry", 1318},     {"harry_death_fall", 1319},
        {"harry_heavy_breath", 1320},  {"radio_interference_loop", 1321}, {"radio_static_loop", 1322},
        {"harry_damage", 1326},        {"harry_damage_type_2", 1327}, {"harry_damage_type_4", 1328},
        {"harry_damage_type_5", 1329}};
    const auto it = numbers.find(name);
    return it == numbers.end() ? -1 : it->second;
}

}  // namespace

// The base data gives the driver the values the original driver had built in.
SH_TEST(sound_rules_base_tables) {
    const au::SoundRules rules = base_rules();
    const au::ResolvedSoundRules resolved = au::resolve(rules, base_number);
    au::DriverTables t;
    au::apply_rules(rules, resolved, t);

    SH_CHECK(t.first_bank_command == 160);
    SH_CHECK(t.first_ambient_command == 170);
    SH_CHECK(t.last_ambient_command == 204);
    SH_CHECK(t.song_file_offset == 54);
    SH_CHECK(t.song_bank_offset == 173);
    SH_CHECK(t.music_slot == 3);
    SH_CHECK(t.music_volume == 40);
    SH_CHECK(t.music_ducked_volume == 24);
    SH_CHECK(t.default_reverb == 20);
    SH_CHECK(t.default_line_volume == 84);
    SH_CHECK(rules.commands.fade_out_ambient == 17);
    SH_CHECK(rules.commands.fade_out_music == 18);
    SH_CHECK(rules.commands.fade_out_music_fast == 23);

    const std::uint16_t scaled[] = {53, 56, 596, 597, 598, 600, 602, 612, 614, 620, 657, 606};
    SH_CHECK(t.line_volumes.size() == 15);
    for (std::size_t i = 0; i < 12; ++i) {
        SH_CHECK(t.line_volumes[i].line == scaled[i]);
        SH_CHECK(t.line_volumes[i].volume == 84);
        SH_CHECK(t.line_volumes[i].scaled);
    }
    SH_CHECK(t.line_volumes[12].line == 723 && t.line_volumes[12].volume == 50 && !t.line_volumes[12].scaled);
    SH_CHECK(t.line_volumes[13].line == 725 && t.line_volumes[13].volume == 50 && !t.line_volumes[13].scaled);
    SH_CHECK(t.line_volumes[14].line == 724 && t.line_volumes[14].volume == 40 && !t.line_volumes[14].scaled);

    SH_CHECK(t.fixed_voices.size() == 2);
    SH_CHECK(t.fixed_voices[0].effect == 1321 && t.fixed_voices[0].voice == 22 && t.fixed_voices[0].fine == 0);
    SH_CHECK(t.fixed_voices[1].effect == 1322 && t.fixed_voices[1].voice == 23 && t.fixed_voices[1].fine == 120);
    SH_CHECK(resolved.fixed_voice(1321) && resolved.fixed_voice(1322) && !resolved.fixed_voice(1320));
}

// Old Silent Hill's streets: ambient 4 until either flag is set, then 11; other maps keep theirs.
SH_TEST(sound_rules_ambient_override) {
    const au::SoundRules rules = base_rules();
    auto none = [](std::string_view) { return false; };
    auto first = [](std::string_view f) { return f == "M1S06_AfterBossFmvSeen"; };
    auto second = [](std::string_view f) { return f == "M2S01_ChurchFmvSeen"; };
    SH_CHECK(au::ambient_override(rules, "map2_s00", none) == 4);
    SH_CHECK(au::ambient_override(rules, "map2_s00", first) == 11);
    SH_CHECK(au::ambient_override(rules, "map2_s00", second) == 11);
    SH_CHECK(au::ambient_override(rules, "map2_s01", first) == -1);
}

SH_TEST(sound_rules_rejects_bad_data) {
    SH_CHECK(!au::SoundRules::parse("{}"));
    SH_CHECK(!au::SoundRules::parse("not json"));
    std::string text = read_text(std::string(SH_PROJECT_ROOT) + "/gamedata/config/sound_driver.json");
    const std::size_t at = text.find("\"voice\": 22");
    SH_CHECK(at != std::string::npos);
    text.replace(at, 11, "\"voice\": 99");
    SH_CHECK(!au::SoundRules::parse(text));
}

SH_TEST(sound_distances) {
    // 3-4-5 metres in Q12.
    SH_CHECK(au::precise_length(3 * 4096, 4 * 4096, 0) == 5 * 4096);
    SH_CHECK(au::wrapped_length(3, 4, 0) == 5);
    SH_CHECK(au::coarse_length(3 * 4096, 0, 4 * 4096) == 5 * 4096);
    SH_CHECK(au::precise_length(0, 0, 0) == 0);
    // A wrapped (negative) sum reads as no distance.
    SH_CHECK(au::wrapped_length(50000, 0, 0) == 0);
}
