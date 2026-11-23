// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engine's side of engine/app/game_view.h and engine/loop/frame_view.h: the boot, main loop, map load and map
// start hosts reach the systems of the running game here, over the session and the game's entry points by their
// readable names. The demo player, the warm reset and the settings go by the C names the map code calls. The parts
// that need the frame layers and the display are in src/engine/app/game_view_frame.cpp.

#include "engine/app/game_view.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "engine/app/gameflow/demo_host.h"
#include "engine/app/gameflow/game_flow.h"
#include "engine/app/loader/game_files.h"
#include "engine/chara/runtime/chara_models.h"
#include "engine/entities/anim_player.h"
#include "engine/entities/character_record.h"
#include "engine/entities/game/remains_host.h"
#include "engine/game_state/session.h"
#include "engine/loop/frame_view.h"
#include "engine/maps/map_runtime.h"
#include "engine/player/player.h"
#include "engine/runtime/cell_stream_runtime.h"
#include "engine/runtime/world_map.h"
#include "engine/app/loader/picture_places.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/gameflow/anim_slots.h"

namespace {

using Vec3 = sh::entities::Vec3;

}  // namespace

extern "C" {

// ---- The game's entry points the hosts call ----
void GameFs_BgItemLoad(void);
void GameFs_BgEtcGfxLoad(void);
void Map_EffectTexturesLoad(std::int32_t mapIdx);
void Screen_Init(std::int32_t screenWidth, bool isInterlaced);
void Screen_RectInterlacedClear(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h, std::uint8_t r,
                                std::uint8_t g, std::uint8_t b);
void Screen_XyPositionSet(std::int32_t x, std::int32_t y);
void SD_Init(void);
std::uint8_t Sd_AudioStreamingCheck(void);
void Sd_TaskPoolExecute(void);
void Sd_TaskPoolDrain(void);
void sd_work_init(void);
void Sd_AmbientSfxSet(std::int32_t idx);
void Vibration_Play(std::int32_t step);
std::int32_t Vibration_Update(void);
void Vibration_MainModeRequest(std::int32_t mode);
void Vw_SetLookAtMatrix(const Vec3* pos, const Vec3* lookAt);
void vwSetViewInfo(void);

void AreaLoad_UpdatePlayerPosition(void);
bool Screen_BackgroundMotionBlur(std::int32_t vBlanks);
void Map_WorldClear(void);
bool Ipd_ChunkInitCheck(void);
bool Bgm_Init(void);
bool Sd_AmbientSfxInit(void);
void AreaLoad_TransitionSound(void);

void vcInitCamera(void* mapHdr, const Vec3* chr_pos);
void vcSetCameraUseWarp(const Vec3* chr_pos, std::int16_t chr_ang_y);
void Gfx_MapEffectsSet(std::int32_t unused);
void Map_EffectStatesReset(void);
void Map_EffectsInit(std::int32_t mapId);
void Game_FlashlightAttributesFix(void);
void Map_WeatherEnvironmentSet(std::int8_t environment, std::uint32_t lightRequest);
void Map_WeatherUpdate(std::int32_t unused, std::int32_t mapIdx, std::int32_t phase);
void Game_NpcRoomInitSpawn(bool cond);
void Game_PlayerHeightUpdate(void);
void GameFs_Tim00TIMLoad(void);
void GameFs_MapItemsModelLoad(std::uint32_t mapId);

// ---- The game state updates (gamedata/config/boot.json names them) ----
void GameState_Boot_Update(void);
void GameState_KonamiLogo_Update(void);
void GameState_KcetLogo_Update(void);
void GameState_MovieIntroFadeIn_Update(void);
void GameState_AutoLoadSavegame_Update(void);
void GameState_MovieIntroAlternate_Update(void);
void GameState_MovieIntro_Update(void);
void GameState_MainMenu_Update(void);
void GameState_LoadSavegameScreen_Update(void);
void GameState_MovieOpening_Update(void);
void GameState_LoadMapScreen_Update(void);
void GameState_InGame_Update(void);
void GameState_MapEvent_Update(void);
void GameState_ExitMovie_Update(void);
void GameState_ItemScreens_Update(void);
void GameState_PaperMapScreen_Update(void);
void GameState_DebugMoviePlayer_Update(void);
void GameState_Options_Update(void);
void GameState_LoadStatusScreen_Update(void);
void GameState_StaffRoll_Update(void);

// The flashlight's world position before the first-person eye takes over: the shadow-map light uses it, so first
// person shadows match third person.
Vec3 g_PcFlashlightShadowWorld = {0, 0, 0};

void Demo_DemoRandSeedBackup(void) { sh::gameflow::demo_player().back_up_seed(sh::gameflow::demo_host()); }
void Demo_DemoRandSeedRestore(void) { sh::gameflow::demo_player().restore_seed(sh::gameflow::demo_host()); }
void Demo_DemoRandSeedAdvance(void) { sh::gameflow::demo_player().advance_seed(sh::gameflow::demo_host()); }
bool Demo_Update(void) { return sh::gameflow::demo_player().update(sh::gameflow::demo_host()); }
bool Demo_ControllerDataUpdate(void) { return sh::gameflow::demo_player().feed_pad(sh::gameflow::demo_host()); }
bool Demo_GameRandSeedSet(void) { return sh::gameflow::demo_player().apply_frame_seed(sh::gameflow::demo_host()); }
bool Demo_SpawnPositionCheck(std::int32_t posX, std::int32_t posZ) {
    return sh::gameflow::demo_player().spawn_allowed(sh::gameflow::demo_host(), posX, posZ);
}

// ---- The warm reset, the title files and the settings (src/engine/app/gameflow/game_flow.h) ----
void Game_WarmBoot(void) { sh::app::warm_boot(); }
void GameFs_StreamBinLoad(void) { sh::app::stream_overlay_load(); }
void GameFs_OptionBinLoad(void) { sh::app::option_overlay_load(); }
void Settings_RestoreControlDefaults(std::int32_t configIdx) { sh::app::settings_restore_control_defaults(configIdx); }

}  // extern "C"

namespace sh::loop {
FrameTiming& frame_timing() {
    static FrameTiming timing;
    return timing;
}
}  // namespace sh::loop

extern "C" {
// The fixed pictures (src/engine/app/loader/picture_places.cpp).
}

namespace sh::app::view {

const sh::loader::ImagePlace* picture(Picture which) {
    switch (which) {
        case Picture::Font16Atlas: return &sh::loader::picture_places().font16_atlas;
        case Picture::KonamiLogo: return &sh::loader::picture_places().konami_logo;
        case Picture::KcetLogo: return &sh::loader::picture_places().kcet_logo;
        case Picture::ItemInspection: break;
    }
    return &sh::loader::picture_places().item_inspection;
}

namespace {

sh::entities::CharacterSlots& characters() { return sh::game::session().system.characters; }
const Vec3* player_position() { return &characters().player.position; }

struct StateHandler {
    const char* name;
    void (*fn)(void);
};

const StateHandler kStateHandlers[] = {
    {"Boot", GameState_Boot_Update},
    {"KonamiLogo", GameState_KonamiLogo_Update},
    {"KcetLogo", GameState_KcetLogo_Update},
    {"MovieIntroFadeIn", GameState_MovieIntroFadeIn_Update},
    {"AutoLoadSavegame", GameState_AutoLoadSavegame_Update},
    {"MovieIntroAlternate", GameState_MovieIntroAlternate_Update},
    {"MovieIntro", GameState_MovieIntro_Update},
    {"MainMenu", GameState_MainMenu_Update},
    {"LoadSavegameScreen", GameState_LoadSavegameScreen_Update},
    {"MovieOpening", GameState_MovieOpening_Update},
    {"LoadMapScreen", GameState_LoadMapScreen_Update},
    {"InGame", GameState_InGame_Update},
    {"MapEvent", GameState_MapEvent_Update},
    {"ExitMovie", GameState_ExitMovie_Update},
    {"ItemScreens", GameState_ItemScreens_Update},
    {"PaperMapScreen", GameState_PaperMapScreen_Update},
    {"DebugMoviePlayer", GameState_DebugMoviePlayer_Update},
    {"Options", GameState_Options_Update},
    {"LoadStatusScreen", GameState_LoadStatusScreen_Update},
    {"Unk15", GameState_StaffRoll_Update},
};

}  // namespace

std::uintptr_t map_overlay_base() { return reinterpret_cast<std::uintptr_t>(sh::game_state::map_overlay_buffer()); }
bool near_active_cell(std::int32_t x, std::int32_t z) { return sh::world::stream::near_window(x, z); }

void intro_world_loads() {
    sh::chara::chara_models().load_player();
    GameFs_BgItemLoad();
    GameFs_BgEtcGfxLoad();  // snow, rain and particle textures
    Map_EffectTexturesLoad(-1);
}

void harry_model_load() { sh::chara::chara_models().load_player(); }
void item_textures_load() { GameFs_BgItemLoad(); }
void world_effect_textures_load() { GameFs_BgEtcGfxLoad(); }
void map_effect_textures_load() { Map_EffectTexturesLoad(-1); }

void screen_init() { Screen_Init(320, false); }
void screen_init_mode(int width, bool interlaced) { Screen_Init(width, interlaced); }
void clear_screen_rect(int x, int y, int w, int h) {
    Screen_RectInterlacedClear(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y),
                               static_cast<std::int16_t>(w), static_cast<std::int16_t>(h), 0, 0, 0);
}
void screen_position_set(int x, int y) { Screen_XyPositionSet(x, y); }
void audio_init() { SD_Init(); }
bool audio_streaming() { return Sd_AudioStreamingCheck() != 0; }
void audio_tasks_run() { Sd_TaskPoolExecute(); }
void audio_tasks_drain() { Sd_TaskPoolDrain(); }
void audio_work_init() { sd_work_init(); }
void ambient_set(int mode) { Sd_AmbientSfxSet(mode); }
void vibration_play(int effect) { Vibration_Play(effect); }
void vibration_update() { Vibration_Update(); }
void vibration_mode_request(int mode) { Vibration_MainModeRequest(mode); }

bool player_control_disabled() { return sh::game::session().player.control_disabled != 0; }
void hold_no_target() {
    characters().player.flags |= sh::entities::kFlagAttackVeto;
    sh::game::player().set(sh::game::PlayerVar::FlagTimer, 0);
}

void player_show() {
    characters().player.model.anim.flags |= sh::entities::kAnimPosed;
    characters().player_upper.model.anim.flags |= sh::entities::kAnimPosed;
}
void view_look_at(const std::int32_t eye[3], const std::int32_t at[3]) {
    const Vec3 pos = {eye[0], eye[1], eye[2]};
    const Vec3 target = {at[0], at[1], at[2]};
    Vw_SetLookAtMatrix(&pos, &target);
    vwSetViewInfo();
}

void area_player_position_update() { AreaLoad_UpdatePlayerPosition(); }
void background_motion_blur() { Screen_BackgroundMotionBlur(2); }
void chara_anim_alloc(int slot, int chara_id) { sh::gameflow::anim_slots().alloc(slot, chara_id, nullptr, nullptr); }
void map_chara_groups_load() { sh::chara::chara_models().load_map_groups(); }
void world_clear() { Map_WorldClear(); }
void player_chunk_init() {
    sh::world::world_map().enter(player_position()->x, player_position()->z);
}
bool chunks_ready() { return Ipd_ChunkInitCheck(); }
int music_init() { return Bgm_Init(); }
int ambient_init() { return Sd_AmbientSfxInit(); }
void transition_sound() { AreaLoad_TransitionSound(); }

void map_camera_init() { vcInitCamera(nullptr, player_position()); }
void map_camera_warp() { vcSetCameraUseWarp(player_position(), sh::game::session().system.camera_angle_y); }
void collision_triggers_set() {
    sh::world::world_map().set_collision_triggers(sh::maps::map_runtime().collision_triggers());
}
void map_effects_set() { Gfx_MapEffectsSet(0); }
void map_effects_reset() { Map_EffectStatesReset(); }
void map_effects_init(int map_index) { Map_EffectsInit(map_index); }
void chara_models_process_loads() { sh::chara::chara_models().process_loads(); }
void flashlight_fix() { Game_FlashlightAttributesFix(); }
void environment_apply(int environment, int level) {
    Map_WeatherEnvironmentSet(static_cast<std::int8_t>(environment), static_cast<std::uint32_t>(level));
}
void particles_update(int a, int map_index, int b) { Map_WeatherUpdate(a, map_index, b); }
void npcs_clear() {
    sh::entities::game::remains::room_left();
    sh::game::SessionScene& scene = sh::game::session().system.scene;
    scene.spawned_groups = 0;
    scene.npc_flags = 0;
    for (sh::entities::CharacterRecord& npc : characters().npcs) {
        npc = sh::entities::CharacterRecord{};
    }
    std::memset(scene.chara_group_flags, 0, sizeof(scene.chara_group_flags));
}
void npcs_spawn_room() { Game_NpcRoomInitSpawn(false); }
void player_height_update() { Game_PlayerHeightUpdate(); }
void anim_bones_update() { sh::gameflow::anim_slots().refresh_bones(); }
void common_texture_load() { GameFs_Tim00TIMLoad(); }
void map_items_models_load(int map_index) { GameFs_MapItemsModelLoad(static_cast<std::uint32_t>(map_index)); }

StateUpdate game_state_update(std::string_view name) {
    for (const StateHandler& h : kStateHandlers) {
        if (name == h.name) {
            return h.fn;
        }
    }
    return nullptr;
}

}  // namespace sh::app::view
