/* The generic spawn selector over map.json's "spawn_overrides" — condition
 * evaluation (difficulty bound, flag sets/clears, nested all/any), selection order (later
 * entries win), and the mapdata reader's parse of
 * the section (absent = empty; malformed = a failed read). */
#include "engine/maps/spawn_overrides.h"
#include "formats/convert/mapdata/mapdata.h"
#include "framework/sh_test.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

namespace md = sh::convert::mapdata;

sh::maps::SpawnFlagGet flags_of(std::map<std::string, bool> state) {
    return [state = std::move(state)](const std::string& name) {
        const auto it = state.find(name);
        return it != state.end() && it->second;
    };
}

md::SpawnFlagSet make_set(const char* chara, int slot, std::vector<std::string> flags) {
    md::SpawnFlagSet s;
    s.chara = chara;
    s.slot = slot;
    s.flags = std::move(flags);
    return s;
}

// A minimal map.json body the unified reader accepts; kind arrays stay empty, so only the
// spawn_overrides section varies between the cases.
std::string scene(const std::string& section) {
    std::string body = R"({"format":"sh-map-scene","version":1,"map":"map1_s00",)"
                       R"("info":{"bgm":0,"ambient_audio":0,"ambient_tint":0,"weather":0,"unused_8":0,"pad_9":[0,0,0]},)"
                       R"("points":[],"entities":[],)";
    if (!section.empty()) {
        body += section;
    } else {
        body.pop_back();  // drop the trailing comma of the empty-section case
    }
    return body + "}";
}

const md::MapLayout& layout() {
    static const md::MapLayout l = {"map1_s00", 0, 0, 0, 0};
    return l;
}

}  // namespace

SH_TEST(spawn_overrides_conditions) {
    // e_GameDifficulty: easy = -1, normal = 0, hard = 1.
    constexpr int kEasy = -1, kNormal = 0, kHard = 1;

    md::SpawnCondition easy;
    easy.difficulty = kEasy;  // exact, like the C++ blocks ("difficulty == GameDifficulty_Easy")
    SH_CHECK(sh::maps::spawn_condition_matches(easy, kEasy, flags_of({})) == true);
    SH_CHECK(sh::maps::spawn_condition_matches(easy, kNormal, flags_of({})) == false);
    SH_CHECK(sh::maps::spawn_condition_matches(easy, kHard, flags_of({})) == false);

    md::SpawnCondition hard;
    hard.difficulty = kHard;
    SH_CHECK(sh::maps::spawn_condition_matches(hard, kHard, flags_of({})) == true);
    SH_CHECK(sh::maps::spawn_condition_matches(hard, kEasy, flags_of({})) == false);

    md::SpawnCondition normal;
    normal.difficulty = kNormal;
    SH_CHECK(sh::maps::spawn_condition_matches(normal, kNormal, flags_of({})) == true);
    SH_CHECK(sh::maps::spawn_condition_matches(normal, kEasy, flags_of({})) == false);

    // Inclusive bounds hit their edge values; unset fields never constrain.
    md::SpawnCondition not_easy;
    not_easy.difficulty_min = kNormal;
    SH_CHECK(sh::maps::spawn_condition_matches(not_easy, kEasy, flags_of({})) == false);
    SH_CHECK(sh::maps::spawn_condition_matches(not_easy, kNormal, flags_of({})) == true);
    SH_CHECK(sh::maps::spawn_condition_matches(not_easy, kHard, flags_of({})) == true);

    md::SpawnCondition at_most_normal;
    at_most_normal.difficulty_max = kNormal;
    SH_CHECK(sh::maps::spawn_condition_matches(at_most_normal, kEasy, flags_of({})) == true);
    SH_CHECK(sh::maps::spawn_condition_matches(at_most_normal, kNormal, flags_of({})) == true);
    SH_CHECK(sh::maps::spawn_condition_matches(at_most_normal, kHard, flags_of({})) == false);

    md::SpawnCondition unset;
    SH_CHECK(sh::maps::spawn_condition_matches(unset, kEasy, flags_of({})) == true);
    SH_CHECK(sh::maps::spawn_condition_matches(unset, kHard, flags_of({})) == true);

    md::SpawnCondition story;
    story.flags_set = {"M4S05_CybilStairsCutsceneDone"};
    story.flags_clear = {"M4S05_FloatstingerKilled"};  // like map3_s04's Floatstinger gate
    SH_CHECK(sh::maps::spawn_condition_matches(story, 2, flags_of({{"M4S05_CybilStairsCutsceneDone", true}})) == true);
    SH_CHECK(sh::maps::spawn_condition_matches(story, 2,
                                               flags_of({{"M4S05_CybilStairsCutsceneDone", true}, {"M4S05_FloatstingerKilled", true}})) == false);
    SH_CHECK(sh::maps::spawn_condition_matches(story, 2, flags_of({})) == false);

    // Nested groups: "all" must hold entirely, "any" needs one; empty groups match.
    md::SpawnCondition nested;
    md::SpawnCondition a;
    a.flags_set = {"A"};
    md::SpawnCondition b;
    b.flags_set = {"B"};
    nested.all = {a, b};
    md::SpawnCondition c;
    c.flags_set = {"C"};
    md::SpawnCondition d;
    d.flags_set = {"D"};
    nested.any = {c, d};
    SH_CHECK(sh::maps::spawn_condition_matches(
                 nested, 2, flags_of({{"A", true}, {"B", true}, {"C", false}, {"D", false}})) == false);
    SH_CHECK(sh::maps::spawn_condition_matches(
                 nested, 2, flags_of({{"A", true}, {"B", true}, {"C", false}, {"D", true}})) == true);
    SH_CHECK(sh::maps::spawn_condition_matches(nested, 2, flags_of({{"A", true}, {"B", false}, {"D", true}})) == false);

    md::SpawnCondition empty_any;
    SH_CHECK(sh::maps::spawn_condition_matches(empty_any, 2, flags_of({})) == true);
}

SH_TEST(spawn_overrides_selection_order) {
    std::vector<md::SpawnOverride> overrides(3);
    overrides[0].when.difficulty = -1;  // easy only, exact
    overrides[0].set = {make_set("GreyChild", 10, {"none"})};
    overrides[1].when.flags_set = {"M3S04_FirstScareSoundPlayed"};  // matches with the flag
    overrides[1].set = {make_set("PuppetDoctor", 2, {"spawn_0"}), make_set("GreyChild", 1, {})};
    overrides[2].when.flags_set = {"Never"};  // never matches here
    overrides[2].set = {make_set("Stalker", 5, {})};

    const auto easy = sh::maps::select_spawn_overrides(overrides, -1, flags_of({{"M3S04_FirstScareSoundPlayed", false}}));
    SH_CHECK(easy.size() == 1);
    SH_CHECK(easy[0]->chara == "GreyChild" && easy[0]->slot == 10 && easy[0]->flags.size() == 1);

    // Both matching overrides contribute, in file order: the doctor first, GreyChild after —
    // a later write to the same slot overwrites an earlier one at the apply site.
    const auto hard = sh::maps::select_spawn_overrides(overrides, 1, flags_of({{"M3S04_FirstScareSoundPlayed", true}}));
    SH_CHECK(hard.size() == 2);
    SH_CHECK(hard[0]->chara == "PuppetDoctor" && hard[1]->chara == "GreyChild");

    SH_CHECK(sh::maps::select_spawn_overrides(overrides, 1, flags_of({})).empty());
}

SH_TEST(spawn_overrides_mapdata_read) {
    std::vector<std::string> warnings;

    // A map.json without the section reads to an empty list (every shipped map today).
    md::MapData data;
    SH_CHECK(md::read_unified_json(scene(""), layout(), data, warnings));
    SH_CHECK(data.spawn_overrides.empty());

    // With the section: parsed names land verbatim; nested conditions survive the read into
    // the selector.
    const std::string section =
        R"("spawn_overrides":[{"when":{"difficulty":"easy"},"set":[{"chara":"GreyChild","slot":10,"flags":["none"]},)"
        R"({"chara":"PuppetDoctor","slot":2,"flags":["spawn_0","spawn_3"]}]},)"
        R"({"when":{"all":[{"flags_set":["M3S04_FirstScareSoundPlayed"]},{"any":[{"flags_clear":["M3S04_SecondScareSoundArmed"]},{"flags_set":["M4S01_CabinetPushed"]}]}]},)"
        R"("set":[{"chara":"GreyChild","slot":1,"flags":[]}]}])";
    md::MapData with_data;
    SH_CHECK(md::read_unified_json(scene(section), layout(), with_data, warnings));
    if (with_data.spawn_overrides.size() != 2) {
        SH_CHECK(!"expected 2 spawn overrides");
    } else {
    SH_CHECK(with_data.spawn_overrides[0].set.size() == 2);
    SH_CHECK(with_data.spawn_overrides[0].set[1].flags == std::vector<std::string>({"spawn_0", "spawn_3"}));
    SH_CHECK(with_data.spawn_overrides[1].when.all.size() == 2 &&
             with_data.spawn_overrides[1].when.all[1].any.size() == 2);
    }

    // The parsed section drives the selector: easy picks the first override's writes; the
    // nested condition picks the second only through its any-branch.
    const auto easy = sh::maps::select_spawn_overrides(with_data.spawn_overrides, -1, flags_of({}));
    SH_CHECK(easy.size() == 2);
    const auto story = sh::maps::select_spawn_overrides(
        with_data.spawn_overrides, 1, flags_of({{"M3S04_FirstScareSoundPlayed", true}, {"M3S04_SecondScareSoundArmed", false}, {"M4S01_CabinetPushed", false}}));
    SH_CHECK(story.size() == 1 && story[0]->chara == "GreyChild" && story[0]->slot == 1);

    // Difficulty NAMES read into the exact enum values (easy=-1, normal=0, hard=1).
    const std::string names =
        R"("spawn_overrides":[{"when":{"difficulty":"easy"},"set":[{"chara":"A","slot":0,"flags":[]}]},)"
        R"({"when":{"difficulty":"hard"},"set":[{"chara":"B","slot":0,"flags":[]}]},)"
        R"({"when":{"difficulty_min":"normal"},"set":[{"chara":"C","slot":0,"flags":[]}]}])";
    md::MapData named;
    SH_CHECK(md::read_unified_json(scene(names), layout(), named, warnings));
    if (named.spawn_overrides.size() != 3) {
        SH_CHECK(!"expected 3 named overrides");
    } else {
        SH_CHECK(named.spawn_overrides[0].when.difficulty == -1);
        SH_CHECK(named.spawn_overrides[1].when.difficulty == 1);
        SH_CHECK(named.spawn_overrides[2].when.difficulty_min == 0);
        SH_CHECK(sh::maps::select_spawn_overrides(named.spawn_overrides, -1, flags_of({})).size() == 1);
        SH_CHECK(sh::maps::select_spawn_overrides(named.spawn_overrides, 0, flags_of({})).size() == 1);
        SH_CHECK(sh::maps::select_spawn_overrides(named.spawn_overrides, 1, flags_of({})).size() == 2);
    }

    // The hand file maps/<map>/spawn_overrides.json: the same entries in their own wrapper,
    // appended to whatever a map.json section already put in (hand entries win).
    const std::string hand =
        R"({"format":"sh-map-spawn-overrides","version":1,"spawn_overrides":[)"
        R"({"when":{"difficulty":"hard"},"set":[{"chara":"Stalker","slot":5,"flags":["none"]}]}]})";
    md::MapData hand_data = with_data;  // carries the map.json section from above
    SH_CHECK(md::read_spawn_overrides_file(hand, hand_data, warnings));
    if (hand_data.spawn_overrides.size() != with_data.spawn_overrides.size() + 1) {
        SH_CHECK(!"the hand file must append its entries");
    } else {
        SH_CHECK(hand_data.spawn_overrides.back().set[0].chara == "Stalker");
        const auto both = sh::maps::select_spawn_overrides(hand_data.spawn_overrides, 1, flags_of({}));
        SH_CHECK(both.size() == 1 && both[0]->chara == "Stalker");  // only the hand entry (its "hard" matches)
        const auto easy_all = sh::maps::select_spawn_overrides(hand_data.spawn_overrides, -1, flags_of({}));
        SH_CHECK(easy_all.size() == 2);  // the map.json "easy" override's two writes; the hand "hard" entry does not
    }
    // A wrong format string or version is a failed read, not a silent skip.
    md::MapData bad_hand;
    SH_CHECK(!md::read_spawn_overrides_file(
        R"({"format":"sh-map-spawn-overrides","version":2,"spawn_overrides":[]})", bad_hand, warnings));
    SH_CHECK(!md::read_spawn_overrides_file(
        R"({"format":"other","version":1,"spawn_overrides":[]})", bad_hand, warnings));

    // A malformed entry fails the whole read (the store falls back to the disc); it never
    // silently applies half a section. Raw numbers are not difficulties either.
    for (const std::string broken :
         {std::string(R"("spawn_overrides":[{"when":{"difficulty":"nightmare"},"set":[]}]}])"),
          std::string(R"("spawn_overrides":[{"when":{"difficulty":0},"set":[]}]}])"),
          std::string(R"("spawn_overrides":[{"when":{},"set":[]}]})")}) {
        md::MapData bad;
        SH_CHECK(!md::read_unified_json(scene(broken), layout(), bad, warnings));
    }
}

/* Spawn variants (map2_s00's three street tables): the hand file's "spawn_variants" section,
 * first-match choice with its flag side effect, the importer's named tables (decode_overlay ->
 * map.json "spawn_variant_rows" -> resolve by name), and inline rows for mods. The decision table is
 * the one the original's variant chooser encodes (M2S00_StreetSpawnCafe first; M2S00_StreetSpawnSchool turns into the third table
 * while M1S05_SplitHeadKilled is set and M2S01_ChurchFmvSeen clear; nothing set picks the first and sets M2S00_StreetSpawnCafe). */
SH_TEST(spawn_variants_read_and_choose) {
    std::vector<std::string> warnings;
    const char* const kNames[3] = {"CafeStreets", "SchoolStreets", "ChurchStreets"};
    const std::string hand =
        R"({"format":"sh-map-spawn-overrides","version":1,"spawn_overrides":[],"spawn_variants":{)"
        R"("enemy_state_active":10,"variants":[)"
        R"({"name":"CafeStreets","enemy_state_load":0,"enemy_state_save":0},)"
        R"({"name":"SchoolStreets","enemy_state_load":1,"enemy_state_save":1},)"
        R"({"name":"ChurchStreets","enemy_state_load":2,"enemy_state_save":2}],)"
        R"("choices":[{"when":{"flags_set":["M2S00_StreetSpawnCafe"]},"variant":"CafeStreets"},)"
        R"({"when":{"flags_set":["M2S00_StreetSpawnSchool","M1S05_SplitHeadKilled"],"flags_clear":["M2S01_ChurchFmvSeen"]},"variant":"ChurchStreets"},)"
        R"({"when":{"flags_set":["M2S00_StreetSpawnSchool"]},"variant":"SchoolStreets"},)"
        R"({"when":{"flags_set":["M2S00_StreetSpawnChurch"]},"variant":"ChurchStreets"},)"
        R"({"when":{},"variant":"CafeStreets","set_flags":["M2S00_StreetSpawnCafe"]}]}})";
    md::MapData data;
    SH_CHECK(md::read_spawn_overrides_file(hand, data, warnings));
    SH_CHECK(data.spawn_variants.present() && data.spawn_variants.variants.size() == 3);
    SH_CHECK(data.spawn_variants.enemy_state_active == 10);
    SH_CHECK(!data.spawn_variants.variants[1].rows_resolved && !data.spawn_variants.variants[1].rows_inline);

    const char* const kFlags[5] = {"M2S00_StreetSpawnCafe", "M2S00_StreetSpawnSchool", "M2S00_StreetSpawnChurch",
                                   "M1S05_SplitHeadKilled", "M2S01_ChurchFmvSeen"};
    for (int mask = 0; mask < 32; ++mask) {
        std::map<std::string, bool> state;
        for (int f = 0; f < 5; ++f) {
            state[kFlags[f]] = (mask >> f) & 1;
        }
        const bool f172 = mask & 1, f173 = mask & 2, f174 = mask & 4, f131 = mask & 8, f181 = mask & 16;
        int expect = !f172 ? (f173 ? 1 : f174 ? 2 : 0) : 0;
        if (f131 && !f181 && expect == 1) {
            expect = 2;
        }
        const bool sets_default = !f172 && !f173 && !f174;
        const md::SpawnVariantChoice* c = sh::maps::select_spawn_variant(data.spawn_variants, 0, flags_of(state));
        SH_CHECK(c != nullptr);
        if (c != nullptr) {
            SH_CHECK(c->variant == kNames[expect]);
            SH_CHECK(c->set_flags.empty() != sets_default);
            const md::SpawnVariant* v = sh::maps::find_spawn_variant(data.spawn_variants, c->variant);
            SH_CHECK(v != nullptr && v->enemy_state_load == expect && v->enemy_state_save == expect);
        }
    }

    // The importer's tables: a synthetic map2_s00 overlay (header pointers as the layout says, one
    // record in the second table), decoded, written into map.json and read back, resolved by name.
    const md::MapLayout* m2 = md::find_layout("map2_s00");
    SH_CHECK(m2 != nullptr);
    if (m2 != nullptr) {
        std::vector<std::uint8_t> overlay(0x800F1FAC - md::kOverlayBase + 32 * 12, 0);
        auto put32 = [&](std::size_t at, std::uint32_t v) {
            for (int b = 0; b < 4; ++b) overlay[at + b] = static_cast<std::uint8_t>(v >> (8 * b));
        };
        put32(md::kHeaderOffset + 0x1C, m2->points_va);
        put32(md::kHeaderOffset + 0x24, m2->events_va);
        const std::uint8_t rec[12] = {0x00, 0x60, 0xF4, 0xFF, 0, 160, 9, 0x0F, 0x00, 0x60, 0xF4, 0xFF};
        std::memcpy(overlay.data() + (0x800F1E2C - md::kOverlayBase) + 14 * 12, rec, 12);
        auto decoded = md::decode_overlay(overlay, *m2);
        SH_CHECK(decoded);
        if (decoded) {
            const std::vector<md::SpawnVariantRows>& tables = decoded.value().spawn_variant_rows;
            SH_CHECK(tables.size() == 3 && tables[0].name == "CafeStreets" && tables[1].name == "SchoolStreets" &&
                     tables[2].name == "ChurchStreets");
            const std::string text = md::write_unified_json(decoded.value(), {});
            SH_CHECK(text.find("\"spawn_variant_rows\"") != std::string::npos);
            md::MapData back;
            back.map = "map2_s00";
            SH_CHECK(md::read_unified_json(text, *m2, back, warnings));
            SH_CHECK(back.spawn_variant_rows.size() == 3 &&
                     std::memcmp(&back.spawn_variant_rows[1].rows, &tables[1].rows, sizeof tables[1].rows) == 0);
            SH_CHECK(md::resolve_spawn_variant_rows(back.spawn_variant_rows, data.spawn_variants));
            const ShMapSpawn& s = data.spawn_variants.variants[1].rows[14];
            SH_CHECK(s.positionX == -0xBA000 && s.rotationY == 160 && s.flags == 9 && s.gameDifficultyMin == -1 &&
                     s.pad7 == 0 && s.positionZ == -0xBA000 && data.spawn_variants.variants[1].rows_resolved);
        }
        std::vector<std::uint8_t> short_overlay(0x800F1E2C - md::kOverlayBase, 0);
        std::memcpy(short_overlay.data(), overlay.data(), 0x1000);
        SH_CHECK(!md::decode_overlay(short_overlay, *m2));
    }
    // A variant naming a table the map does not have: the resolve fails (the engine drops the variants).
    md::MapData again;
    SH_CHECK(md::read_spawn_overrides_file(hand, again, warnings));
    SH_CHECK(!md::resolve_spawn_variant_rows({}, again.spawn_variants));

    // Inline rows (a mod's own table): exactly 32 entries in the spawns-kind slot shape.
    std::string rows = "[{\"x\":-22,\"z\":117,\"rotation_deg\":180,\"chara\":\"None\",\"flags\":5,\"difficulty_min\":-1}";
    for (int i = 1; i < 32; ++i) {
        rows += ",null";
    }
    rows += "]";
    const std::string inline_hand =
        R"({"format":"sh-map-spawn-overrides","version":1,"spawn_overrides":[],"spawn_variants":{)"
        R"("enemy_state_active":10,"variants":[{"name":"Mine","rows":)" + rows +
        R"(,"enemy_state_load":3,"enemy_state_save":4}],"choices":[{"when":{},"variant":"Mine"}]}})";
    md::MapData mod;
    SH_CHECK(md::read_spawn_overrides_file(inline_hand, mod, warnings));
    SH_CHECK(mod.spawn_variants.variants.size() == 1 && mod.spawn_variants.variants[0].rows_resolved &&
             mod.spawn_variants.variants[0].rows[0].positionX == -22 * 4096 &&
             mod.spawn_variants.variants[0].rows[0].rotationY == 128 &&
             mod.spawn_variants.variants[0].rows[0].gameDifficultyMin == -1);
    SH_CHECK(md::resolve_spawn_variant_rows({}, mod.spawn_variants));

    // Malformed sections fail the read: an unknown variant, a short row list, a duplicate name.
    for (const std::string& broken : {
             std::string(R"({"enemy_state_active":10,"variants":[{"name":"A",)"
                         R"("enemy_state_load":0,"enemy_state_save":0}],"choices":[{"when":{},"variant":"B"}]})"),
             std::string(R"({"enemy_state_active":10,"variants":[{"name":"A","rows":[null],)"
                         R"("enemy_state_load":0,"enemy_state_save":0}],"choices":[]})"),
             std::string(R"({"enemy_state_active":10,"variants":[{"name":"A","enemy_state_load":0,"enemy_state_save":0},)"
                         R"({"name":"A","enemy_state_load":0,"enemy_state_save":0}],"choices":[]})")}) {
        md::MapData bad;
        SH_CHECK(!md::read_spawn_overrides_file(
            R"({"format":"sh-map-spawn-overrides","version":1,"spawn_overrides":[],"spawn_variants":)" + broken + "}",
            bad, warnings));
    }
}
