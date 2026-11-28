#include "engine/config/retired_keys.h"

#include <algorithm>
#include <array>

namespace sh::config {

namespace {

constexpr std::array<std::string_view, 67> kRetired = {
    // Render paths and switches that were removed; old configs may still name them.
    "aniso_level", "rhi_prims", "model_source", "scene_renderer", "per_pixel_lighting", "chara_per_pixel_light",
    // The old collision code and its comparison mode.
    "world_source", "world_check",
    // Shadow runs of old game-flow and loop code next to the engine's.
    "gameflow_shadow", "loop_shadow",
    // Switches between the engine and a path that does not exist.
    "cutscene_source", "entity_anim",
    // Replacements and texture packs come from mods/.
    "allow_loose_files", "texture_packs",
    // Texture packs matched by the hash of an upload, and their dump: a mod replaces a picture by its name.
    "texpack_cache_mb", "texpack_budget_mb", "texpack_lazy_ms", "texpack_worker", "dump_textures",
    // The quick options overlay.
    "key_quick_options", "pad_quick_options", "action_quick_options",
    // Hotkeys that tuned graphics effects; the Options pages set them.
    "key_gfx_cycle", "key_gfx_prev", "key_gfx_next",
    // Keys nothing read any more: diagnostics of the removed draw path, sound-chip switches, the ragdoll.
    "item_depth_probe", "chara_prim_probe", "gl_verbose", "allow_mouse_secondary", "adsr", "reverb_scale", "ragdoll",
    // Extras that were not part of the original game: online services, alternative game modes, cosmetics.
    "retroachievements", "ra_username", "ra_token", "ra_sfx", "ra_hash_override", "ra_spectator",
    "discord_rich_presence", "discord_app_id", "randomizer", "player_character", "female_voice_pitch",
    "dark_harry", "dark_harry_afk_sec", "dark_harry_distance", "dark_harry_x", "dark_harry_z", "pc_port_credits",
    "unlimited_enemies", "global_chara_pool", "uncensored",
    // Languages are the text directories with a lang.json; `language` names one on any disc.
    "jp_language", "cross_region_languages",
    // The choice among disc images of several regions: the game data is one import of the NTSC USA disc.
    "region", "disc_image",
    // Native code libraries are not loaded; mods are data and Lua.
    "enable_plugins", "allow_unrecognized_dlls",
    // The engine's own sound system is the only one.
    "audio_engine",
    // The second newspaper always sets its flag, as in the PAL release.
    "fix_second_newspaper",
    // The over-the-shoulder camera: third person rests over the shoulder (tps_rest_x, tps_aim_x).
    "ots_fov", "ots_aim_zoom_amount", "ots_rest_zoom_amount", "ots_rest_x", "ots_rest_y", "ots_aim_x", "ots_aim_y",
};

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

}  // namespace

std::span<const std::string_view> retired_keys() { return kRetired; }

bool is_retired_key(std::string_view key) {
    return std::find(kRetired.begin(), kRetired.end(), key) != kRetired.end();
}

bool is_retired_line(std::string_view line) {
    line = trim(line);
    if (line.empty() || line.front() == '#' || line.front() == ';') return false;
    const auto eq = line.find('=');
    if (eq == std::string_view::npos) return false;
    return is_retired_key(trim(line.substr(0, eq)));
}

}  // namespace sh::config
