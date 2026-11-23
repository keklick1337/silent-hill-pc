// Retired config.cfg keys (src/engine/config/retired_keys.h): an old file that still carries them is read without a
// warning, and the shipped config.cfg sets none of them.

#include "engine/config/retired_keys.h"
#include "framework/sh_test.h"

#include <fstream>
#include <sstream>
#include <string>

namespace cfg = sh::config;

SH_TEST(retired_keys_old_config_lines_are_skipped) {
    // An old config.cfg as a user may still have it: every retired key, with old values and spacing.
    std::string text =
        "# old file\n"
        "world_source = legacy\n"
        "world_check=1\n"
        "\tscene_renderer = psx\n"
        "model_source = disc\n"
        "rhi_prims = 1\n"
        "aniso_level = 16\n"
        "per_pixel_lighting = 1\n"
        "chara_per_pixel_light = 0\n"
        "gameflow_shadow = 1\n"
        "loop_shadow = 1\n"
        "cutscene_source = legacy\n"
        "entity_anim = legacy\n"
        "allow_loose_files = 1\n"
        "texture_packs = 1\n"
        "texpack_cache_mb = 2048\n"
        "texpack_budget_mb = 6144\n"
        "texpack_lazy_ms = 4\n"
        "texpack_worker = 1\n"
        "dump_textures = 1\n"
        "key_quick_options = F10\n"
        "pad_quick_options = BACK\n"
        "action_quick_options = 1\n"
        "key_gfx_cycle = \\\n"
        "key_gfx_prev = [\n"
        "key_gfx_next = ]\n"
        "item_depth_probe = 1\n"
        "chara_prim_probe = 16\n"
        "gl_verbose = 1\n"
        "allow_mouse_secondary = 1\n"
        "adsr = 0\n"
        "reverb_scale = 2.0\n"
        "ragdoll = 1\n"
        "retroachievements = 1\n"
        "ra_username = someone\n"
        "ra_token = 0123456789abcdef\n"
        "ra_sfx = steam\n"
        "ra_hash_override = usa\n"
        "ra_spectator = 1\n"
        "discord_rich_presence = 1\n"
        "discord_app_id = 123\n"
        "randomizer = 1\n"
        "player_character = lisa\n"
        "female_voice_pitch = 140\n"
        "dark_harry = 1\n"
        "dark_harry_afk_sec = 300\n"
        "dark_harry_distance = 14\n"
        "dark_harry_x = 1250\n"
        "dark_harry_z = -300\n"
        "pc_port_credits = 0\n"
        "unlimited_enemies = 1\n"
        "global_chara_pool = 1\n"
        "uncensored = 1\n"
        "jp_language = zh\n"
        "cross_region_languages = 0\n"
        "region = pal\n"
        "disc_image = Silent Hill (USA)-patched.bin\n"
        "enable_plugins = 1\n"
        "allow_unrecognized_dlls = 1\n"
        "audio_engine = native\n";
    int retired = 0, other = 0;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        (cfg::is_retired_line(line) ? retired : other)++;
    }
    SH_CHECK(other == 0);
    SH_CHECK(retired == static_cast<int>(cfg::retired_keys().size()));

    // Live keys, comments and binds are not retired.
    SH_CHECK(!cfg::is_retired_line("use_pgxp = 1"));
    SH_CHECK(!cfg::is_retired_line("# world_source = native"));
    SH_CHECK(!cfg::is_retired_line("bind F5 noclip"));
    SH_CHECK(!cfg::is_retired_key("foliage_edge"));
    SH_CHECK(!cfg::is_retired_key("whole_map_exteriors"));
}

SH_TEST(retired_keys_not_in_shipped_config) {
    std::ifstream f(SH_PROJECT_ROOT "/assets/config.cfg");
    SH_CHECK(f.good());
    int lines = 0, retired = 0;
    for (std::string line; std::getline(f, line);) {
        ++lines;
        if (cfg::is_retired_line(line)) ++retired;
    }
    SH_CHECK(lines > 100);
    SH_CHECK(retired == 0);
}
