#include "framework/golden.h"
#include "framework/sh_test.h"

#include "engine/audio/file_player.h"
#include "engine/audio/sound_registry.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/footsteps.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "framework/mod_fixture.h"

namespace fs = std::filesystem;
using sh::chara::CharaLibrary;
using sh::chara::Footsteps;
using sh::chara::StepPlace;
using sh::chara::SurfaceTable;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

std::int32_t q12(float x) { return static_cast<std::int32_t>(x * 4096.0f); }

constexpr const char* kTable = "chara/footsteps.txt";

bool narrow_pitch(const sh::chara::Surface& s) {
    return s.walk == sh::chara::StepPitch{-4, 8} && s.run == sh::chara::StepPitch{56, 16};
}

bool wide_pitch(const sh::chara::Surface& s) {
    return s.walk == sh::chara::StepPitch{-16, 32} && s.run == sh::chara::StepPitch{32, 64};
}

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "footsteps_test" / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() { std::error_code ec; fs::remove_all(root, ec); }
    void write(const fs::path& relative, std::string_view text) const {
        fs::create_directories(root / relative.parent_path());
        std::ofstream out(root / relative, std::ios::binary);
        out << text;
        out.close();
        sh::test::mod_file_written(root / relative);
    }
};

}  // namespace

// The shipped data against the recorded table: for every map and material the step sound and pitch away from the
// special places, and a digest over every room the rules name and every probed place (on, just inside and just
// outside every edge), the same for every character that walks on Harry's rules.
SH_TEST(footsteps_shipped_as_recorded) {
    std::vector<std::string> warnings;
    const auto library = CharaLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(library);
    auto surfaces = SurfaceTable::load(repo_gamedata(), {}, warnings);
    SH_CHECK(surfaces && !surfaces.value().empty());
    SH_CHECK(warnings.empty());
    Footsteps steps(library.value(), std::move(surfaces.value()), repo_gamedata(), {});

    std::vector<std::int32_t> xs, zs;
    const auto edges_x = sh::test::golden_rows(kTable, "edges_x");
    const auto edges_z = sh::test::golden_rows(kTable, "edges_z");
    SH_CHECK(edges_x.size() == 1 && edges_z.size() == 1);
    for (const auto* row : {&edges_x, &edges_z}) {
        std::vector<std::int32_t>& out = row == &edges_x ? xs : zs;
        for (const std::string& cell : row->empty() ? std::vector<std::string>{} : row->front().cells) {
            for (int d : {-1, 0, 1}) {
                out.push_back(static_cast<std::int32_t>(std::strtol(cell.c_str(), nullptr, 10)) + d);
            }
        }
    }
    SH_CHECK(xs.size() == 39 && zs.size() == 51);

    const auto rows = sh::test::golden_rows(kTable, "surface");
    SH_CHECK(rows.size() == 43 * 16);
    long checked = 0;
    int mismatches = 0;
    for (int chara : {1, 38, 32, 33, 34, 24, 28}) {  // Harry, Kaufmann, Lisa, Bloody Lisa, Alessa, Monster Cybil, Cheryl
        for (const auto& r : rows) {
            const std::string& map = r.str(0);
            const int material = static_cast<int>(r.num(1));
            const auto base = steps.choose(static_cast<std::uint8_t>(chara), material, map, StepPlace{0, 0, 0, -1}, 1311);
            bool same = base.sound == r.num(2) &&
                        (r.str(3) == "narrow" ? narrow_pitch(*base.surface) : wide_pitch(*base.surface));
            const bool grid = r.str(4) == "grid";
            sh::test::Digest d;
            static const std::vector<std::int32_t> origin{0};
            {
                for (int room : {-1, 18, 20, 21}) {
                    for (std::int32_t y : {0, 1}) {
                        for (std::int32_t x : grid ? xs : origin) {
                            for (std::int32_t z : grid ? zs : origin) {
                                const auto got = steps.choose(static_cast<std::uint8_t>(chara), material, map,
                                                              StepPlace{x, y, z, room}, 1311);
                                d.add(got.sound);
                                d.add(narrow_pitch(*got.surface) ? 1 : 0);
                                same = same && (narrow_pitch(*got.surface) || wide_pitch(*got.surface));
                                ++checked;
                            }
                        }
                    }
                }
                same = same && d.value() == r.hex(5);
            }
            if (!same && ++mismatches <= 3) {
                sh::test::golden_mismatch(kTable, r,
                                          "for character " + std::to_string(chara) + " the sound " +
                                              std::to_string(base.sound) + " and the digest " + d.hex());
            }
        }
    }
    SH_CHECK(checked > 100000);
    SH_CHECK(mismatches == 0);
    SH_CHECK(steps.take_warnings().empty());

    // The Hanged Scratcher: metal grating (11) -> its metal step, anything else -> its step.
    for (int material = -1; material <= 14; ++material) {
        SH_CHECK(steps.table(10, material, -1).sound == (material == 11 ? 1582 : 1581));
    }
}

// The special places in words: a spot, a room or a whole map changes the step sound of the ground under it.
SH_TEST(footsteps_special_places) {
    std::vector<std::string> warnings;
    const auto library = CharaLibrary::load(repo_gamedata(), {}, warnings);
    auto surfaces = SurfaceTable::load(repo_gamedata(), {}, warnings);
    SH_CHECK(library && surfaces);
    Footsteps steps(library.value(), std::move(surfaces.value()), repo_gamedata(), {});
    const auto sound = [&](const char* map, int material, int room, float x, float y, float z) {
        return steps.choose(1, material, map, StepPlace{q12(x), q12(y), q12(z), room}, 1311).sound;
    };
    // Plain ground steps with the default sound, and two materials sound different on one map each.
    SH_CHECK(sound("map0_s00", 0, -1, 0, 0, 0) == 1311);
    SH_CHECK(sound("map0_s00", 9, -1, 0, 0, 0) == 1331 && sound("map0_s02", 9, -1, 0, 0, 0) == 1388);
    SH_CHECK(sound("map0_s00", 5, -1, 0, 0, 0) == 1557 && sound("map4_s02", 5, -1, 0, 0, 0) == 1543);
    // A strip of material 8 on map2_s00 sounds like material 2; beside it, and on other ground there, it does not.
    SH_CHECK(sound("map2_s00", 8, -1, 100.0f, 0, -30.0f) == 1389);
    SH_CHECK(sound("map2_s00", 8, -1, 94.0f, 0, -30.0f) == 1330);
    SH_CHECK(sound("map2_s00", 0, -1, 100.0f, 0, -30.0f) == 1311);
    // map6_s02: room 20 below the floor level, and room 21 on everything but material 1.
    SH_CHECK(sound("map6_s02", 0, 20, 0, 0.5f, 0) == 1346 && sound("map6_s02", 0, 20, 0, 0, 0) == 1311);
    SH_CHECK(sound("map6_s02", 0, 21, 0, 0, 0) == 1346 && sound("map6_s02", 1, 21, 0, 0, 0) == 1311);
    // map4_s03 has two spots of its own and shares the five spots of map6_s00.
    SH_CHECK(sound("map4_s03", 0, -1, 166.0f, 0, 60.0f) == 1565 && sound("map4_s03", 0, -1, 112.0f, 0, -100.0f) == 1565);
    SH_CHECK(sound("map6_s00", 0, -1, 166.0f, 0, 60.0f) == 1311);
    for (const char* map : {"map4_s03", "map6_s00"}) SH_CHECK(sound(map, 0, -1, -159.0f, 0, 27.0f) == 1600);
    SH_CHECK(sound("map6_s00", 0, -1, -159.0f, 0, 17.0f) == 1600 && sound("map6_s00", 0, -1, -168.0f, 0, -15.0f) == 1600);
    SH_CHECK(sound("map6_s00", 0, -1, -171.0f, 0, -23.0f) == 1600 && sound("map6_s00", 0, -1, -168.0f, 0, -35.0f) == 1600);
    // One room of map6_s01, and the whole of map6_s04 whatever the material.
    SH_CHECK(sound("map6_s01", 0, 18, 0, 0, 0) == 1608 && sound("map6_s01", 0, 20, 0, 0, 0) == 1311);
    SH_CHECK(sound("map6_s04", 0, -1, 0, 0, 0) == 1332 && sound("map6_s04", 3, -1, 0, 0, 0) == 1332);
}

SH_TEST(footsteps_names_and_maps) {
    SH_CHECK(sh::chara::map_name(0) == "map0_s00");
    SH_CHECK(sh::chara::map_name(10) == "map2_s00");
    SH_CHECK(sh::chara::map_name(42) == "map7_s03");
    SH_CHECK(sh::chara::map_name(43).empty());
    std::vector<std::string> warnings;
    auto surfaces = SurfaceTable::load(repo_gamedata(), {}, warnings);
    SH_CHECK(surfaces);
    SH_CHECK(surfaces.value().of_material(3).name == "grass");
    SH_CHECK(surfaces.value().of_material(11).name == "metal_grating");
    SH_CHECK(surfaces.value().of_material(25).name == "default");  // not listed -> the fallback
}

// A mod: a new surface name for a material, Harry's step on it, and its own WAV for the sound.
SH_TEST(footsteps_mod_own_wav) {
    Scratch dir("mod");
    dir.write("mods/snow/mod.json", R"({"id": "snow", "version": "1.0.0", "priority": 10})");
    dir.write("mods/snow/world/surfaces.json", R"json({"surfaces": {"snow": {"material": 3}}})json");
    dir.write("mods/snow/sounds/snow.json",
              R"json({"sounds": {"harry_snow_step": {"file": "sounds/snow_step.wav", "volume": 0.8, "pitch": -2}}})json");
    dir.write("mods/snow/chara/Harry/character.json", R"json({"footsteps": {"snow": "harry_snow_step"}})json");
    dir.write("mods/snow/sounds/snow_step.wav", "RIFF");  // existence is enough here

    std::vector<std::string> warnings;
    const auto library = CharaLibrary::load(repo_gamedata(), dir.root / "mods", warnings);
    SH_CHECK(library);
    const int id = library.value().sound_registry().id("harry_snow_step");
    SH_CHECK(id >= sh::audio::SoundRegistry::kFileIdFirst && id <= sh::audio::SoundRegistry::kFileIdLast);
    const sh::audio::SoundInfo* info = library.value().sound_registry().file_sound(id);
    SH_CHECK(info != nullptr && info->name == "harry_snow_step");
    SH_CHECK(info->file_path == dir.root / "mods/snow/sounds/snow_step.wav");
    SH_CHECK(info->volume == 0.8f && info->pitch == -2.0f && !info->loop);
    SH_CHECK(library.value().sound_registry().file_sound(1311) == nullptr);  // bank tones stay tones

    auto surfaces = SurfaceTable::load(repo_gamedata(), dir.root / "mods", warnings);
    SH_CHECK(surfaces);
    Footsteps steps(library.value(), std::move(surfaces.value()), repo_gamedata(), dir.root / "mods");
    const auto step = steps.choose(1, 3, "map2_s00", StepPlace{}, 1311);
    SH_CHECK(step.surface->name == "snow" && step.sound == id);
    SH_CHECK(steps.choose(1, 1, "map2_s00", StepPlace{}, -1).sound == 1311);  // other surfaces unchanged
}

SH_TEST(sound_registry_file_entries) {
    sh::audio::SoundRegistry r;
    SH_CHECK(r.apply(R"json({"sounds": {"footstep_concrete": {"id": 1311}}})json", "base"));
    // An existing name turned into a file keeps its id; a new file-only name gets the first free one.
    SH_CHECK(r.apply(R"json({"sounds": {"footstep_concrete": {"file": "a.wav", "loop": true, "pan": -0.5},
                                        "my_new": {"file": "b.wav"}, "my_next": {"file": "c.wav"}}})json",
                     "mod", "/m"));
    SH_CHECK(r.id("footstep_concrete") == 1311);
    SH_CHECK(r.file_sound(1311) != nullptr && r.file_sound(1311)->loop && r.file_sound(1311)->pan == -0.5f);
    SH_CHECK(r.file_sound(1311)->file_path == fs::path("/m/a.wav"));
    SH_CHECK(r.id("my_new") == 1700 && r.id("my_next") == 1701);
    SH_CHECK(r.has_file_sounds());
    // Bad values reject the whole file.
    SH_CHECK(!r.apply(R"json({"sounds": {"x_ok": {"file": "d.wav"}, "x_bad": {"file": "e.wav", "volume": 9}}})json", "bad"));
    SH_CHECK(r.id("x_ok") == -1);
    // A name that is no sound of the game and has no file is skipped, the rest of the file applies.
    SH_CHECK(r.apply(R"json({"sounds": {"nothing": {"banks": ["BASE"]}, "my_alias": "footstep_concrete"}})json", "mod"));
    SH_CHECK(r.id("nothing") == -1);
    SH_CHECK(r.id("my_alias") == 1311);
    SH_CHECK(!r.apply(R"json({"sounds": {"bad_alias": "no_such_sound"}})json", "bad"));
}

SH_TEST(file_player_mixes_and_stops) {
    sh::audio::FilePlayer player;
    auto pcm = std::make_shared<const std::vector<std::int16_t>>(std::vector<std::int16_t>(441, 10000));
    player.play(pcm, 1, 44100, 1.0, sh::audio::FilePlayer::kUnity, sh::audio::FilePlayer::kUnity / 2, false, 7);
    SH_CHECK(player.active(7) == 1);
    std::vector<std::int32_t> l(1000, 0), r(1000, 0);
    player.mix(l.data(), r.data(), 1000);
    SH_CHECK(l[0] == 10000 && r[0] == 5000);
    SH_CHECK(l[999] == 0);          // the file ended after 441 frames
    SH_CHECK(player.active(7) == 0);
    player.play(pcm, 1, 22050, 1.0, sh::audio::FilePlayer::kUnity, sh::audio::FilePlayer::kUnity, true, 8);
    std::fill(l.begin(), l.end(), 0);
    player.mix(l.data(), r.data(), 1000);
    SH_CHECK(l[999] == 10000);      // a loop keeps going
    player.stop(8);
    SH_CHECK(player.active_count() == 0);
}
