// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's developer keys on the engine key table. Active only with
// allow_debug_controls and in game (the main loop's debug camera chain decides when to call):
//   Numpad *      free camera on/off
//   , .           keyframe inspector: step the keyframe down/up (held: auto-repeat)
//   /             keyframe inspector: jump to the next weapon (or base) animation start
//   N             keyframe inspector: next viewed character (Harry, then the loaded NPCs)
//   P             keyframe inspector: loop the animation around the keyframe / stop
//   L             log the first-person eye offset (or the camera's offset in Harry's frame)
//   Numpad 3      fall rescue: back to the height Harry had on entering the room, pushed back
// The inspector itself is a quick options row; the pose override lives in the player and NPC updates.

#include <cstdint>

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include "engine/devtools/dev_keys.h"
#include "engine/entities/character_record.h"
#include "engine/entities/clip_record.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "input/camera_style.h"

namespace {

using sh::entities::ClipRecord;
using sh::entities::Vec3;

}  // namespace

extern "C" {
extern const unsigned char* g_sdlKeyboardState;
extern int g_DebugCamEnabled;
extern int g_DebugAnimKfView;
extern int g_DebugAnimKf;
extern int g_DebugAnimPlaying;
extern int g_DebugAnimKfStart;
extern int g_DebugAnimKfEnd;
extern std::int32_t g_DebugAnimRate;
extern int g_DebugViewNpcSlot;
extern int g_DebugAnimPlayGen;
extern Vec3 g_PcFpsOffset;
extern Vec3 g_PcCamAppliedPos;
// Harry's base clips (the equipped weapon's clips overwrite entries 56..75).
extern ClipRecord HARRY_BASE_ANIM_INFOS[];
void Pc_FreeCam_Set(int on);
void Pc_FreeCam_Position(std::int32_t* out);
}

namespace {

namespace dt = ::sh::devtools;
namespace gs = ::sh::game_state;
using sh::game::save_record;
using sh::game::session;

constexpr int kWeaponAnimFirst = 56;
constexpr int kWeaponAnimEnd = 76;
constexpr int kHarryAnimCount = 76;
constexpr int kNpcAnimCountMax = 64;  // NPC tables carry no length: capped and validated
constexpr int kHarryViewKeyframe = 588;
constexpr int kLoopKeyframeMax = 2048;
constexpr std::int32_t kDefaultLoopRate = 15 * 4096;
constexpr int kCharaNone = 0;
constexpr int kCharaPadlock = 44;
constexpr std::int32_t kRescuePush = static_cast<std::int32_t>(2.5f * 4096.0f);
constexpr std::int32_t kVoidCellHeight = 8 * 4096;  // the ground height of a cell with no floor

sh::entities::CharacterSlots& characters() { return session().system.characters; }

bool inspector() { return g_DebugAnimKfView != 0; }

bool echo_keyframe = false;

void scrub(const dt::KeyEvent& ev, int step) {
    if (ev.fire) {
        if (step < 0) {
            if (g_DebugAnimKf > 0) g_DebugAnimKf--;
        } else {
            g_DebugAnimKf++;
        }
        g_DebugAnimPlaying = 0;
    }
    // The amber panel is the live readout; the journal gets the tap and the landed frame only.
    echo_keyframe = echo_keyframe || ev.pressed() || ev.released();
}

// The next animation start above the current keyframe: among the weapon animations when a weapon is equipped (they
// replace entries 56..75), else among all of Harry's; wraps to the lowest.
void next_anim_start() {
    bool weapon = false;
    for (int i = kWeaponAnimFirst; i < kWeaponAnimEnd; i++) {
        if (HARRY_BASE_ANIM_INFOS[i].start_keyframe >= 0) {
            weapon = true;
            break;
        }
    }
    const int lo = weapon ? kWeaponAnimFirst : 0;
    const int hi = weapon ? kWeaponAnimEnd : 256;
    int best = -1;
    int lowest = -1;
    for (int i = lo; i < hi; i++) {
        const int start = HARRY_BASE_ANIM_INFOS[i].start_keyframe;
        if (start < 0) {
            continue;
        }
        if (lowest < 0 || start < lowest) lowest = start;
        if (start > g_DebugAnimKf && (best < 0 || start < best)) best = start;
    }
    if (best < 0) best = lowest;
    if (best >= 0) {
        g_DebugAnimKf = best;
        g_DebugAnimPlaying = 0;
        SH_DBG_ECHO("[DEBUG] / %s anim start: KF %d", weapon ? "weapon" : "base", g_DebugAnimKf);
    }
}

void next_viewed_character() {
    int next = -1;
    for (int i = g_DebugViewNpcSlot + 1; i < sh::entities::kNpcSlotCount; i++) {
        const int id = characters().npcs[i].model.chara_id;
        if (id != kCharaNone && id != kCharaPadlock) {
            next = i;
            break;
        }
    }
    g_DebugViewNpcSlot = next;
    g_DebugAnimPlaying = 0;
    if (next < 0) {
        g_DebugAnimKf = kHarryViewKeyframe;
        SH_DBG_ECHO("[DEBUG] N: view Harry (KF %d)", g_DebugAnimKf);
    } else {
        g_DebugAnimKf = 0;
        SH_DBG_ECHO("[DEBUG] N: view NPC slot %d (chara %d)", next, (int)characters().npcs[next].model.chara_id);
    }
}

void toggle_loop() {
    if (g_DebugAnimPlaying) {
        g_DebugAnimPlaying = 0;
        SH_DBG_ECHO("[DEBUG] P: play stopped (KF %d)", g_DebugAnimKf);
        return;
    }
    const ClipRecord* table = HARRY_BASE_ANIM_INFOS;
    int count = kHarryAnimCount;
    if (g_DebugViewNpcSlot >= 0 && g_DebugViewNpcSlot < sh::entities::kNpcSlotCount) {
        const sh::entities::CharacterRecord& npc = characters().npcs[g_DebugViewNpcSlot];
        table = npc.model.chara_id != kCharaNone ? static_cast<const ClipRecord*>(npc.model.anim.clip_table) : nullptr;
        count = kNpcAnimCountMax;
    }
    int lo = -1, hi = -1;
    std::int32_t rate = kDefaultLoopRate;
    for (int i = 0; table != nullptr && i < count; i++) {
        const std::int32_t start = table[i].start_keyframe;
        const std::int32_t end = table[i].end_keyframe;
        if (table[i].playback == nullptr || start < 0 || end <= start || end >= kLoopKeyframeMax) {
            continue;
        }
        if (g_DebugAnimKf >= start && g_DebugAnimKf <= end) {
            lo = start;
            hi = end;
            rate = table[i].variable_duration ? kDefaultLoopRate : table[i].duration.constant;
            break;
        }
    }
    if (lo < 0) {
        SH_DBG_ECHO("[DEBUG] P: no loopable anim contains KF %d", g_DebugAnimKf);
        return;
    }
    g_DebugAnimKfStart = lo;
    g_DebugAnimKfEnd = hi;
    g_DebugAnimRate = rate;
    g_DebugAnimPlayGen++;
    g_DebugAnimPlaying = 1;
    SH_DBG_ECHO("[DEBUG] P: play loop [%d..%d] rate=%d", lo, hi, (int)rate);
}

// In first person the eye is the baseline plus live head sway; logging the baseline (which the numpad edits) is
// what converges when pasted. Otherwise the camera's offset in Harry's frame.
void log_eye_offset() {
    if (sh::input::camera_styles().first_person() && !g_DebugCamEnabled) {
        SH_DBG_ECHO("[FPSCAM] g_PcFpsOffset = { %d, %d, %d }  (baseline; paste to bake)", (int)g_PcFpsOffset.x,
                    (int)g_PcFpsOffset.y, (int)g_PcFpsOffset.z);
        return;
    }
    const sh::entities::CharacterRecord& harry = characters().player;
    Vec3 cam = g_PcCamAppliedPos;
    if (g_DebugCamEnabled) {
        std::int32_t free_cam[3];
        Pc_FreeCam_Position(free_cam);
        cam = {free_cam[0], free_cam[1], free_cam[2]};
    }
    const std::int32_t dx = cam.x - harry.position.x;
    const std::int32_t dy = cam.y - harry.position.y;
    const std::int32_t dz = cam.z - harry.position.z;
    const std::int32_t s = gs::sin_q12(harry.rotation.y);
    const std::int32_t c = gs::cos_q12(harry.rotation.y);
    const auto local_x = static_cast<std::int32_t>(((std::int64_t)dx * c - (std::int64_t)dz * s) >> 12);
    const auto local_z = static_cast<std::int32_t>(((std::int64_t)dx * s + (std::int64_t)dz * c) >> 12);
    SH_DBG_ECHO("[FPSCAM] cam=(%d,%d,%d) harry=(%d,%d,%d) bodyYaw=%d  -> LOCAL OFFSET { %d, %d, %d }", cam.x, cam.y,
                cam.z, harry.position.x, harry.position.y, harry.position.z, (int)harry.rotation.y, local_x, dy,
                local_z);
}

// The height Harry had when he entered the room is the last known good floor: a mid-fall frame can also show a zero
// fall speed, so the snapshot is taken on room and map changes only.
struct FallRescue {
    std::int32_t safe_y = 0;
    bool have_safe_y = false;
    std::int32_t room = -1;
    std::int32_t map = -1;
} s_rescue;

void track_room() {
    const std::int32_t room = save_record().room;
    const std::int32_t map = save_record().map;
    if (map != s_rescue.map || room != s_rescue.room) {
        s_rescue.safe_y = characters().player.position.y;
        s_rescue.have_safe_y = true;
        s_rescue.map = map;
        s_rescue.room = room;
    }
}

// Back to the safe height and 2.5 units against the facing, or he lands on the same broken spot.
void rescue_fall() {
    sh::entities::CharacterRecord& harry = characters().player;
    sh::entities::Vec3& p = harry.position;
    const std::int32_t facing = harry.rotation.y;
    const std::int32_t s = gs::sin_q12(facing);
    const std::int32_t c = gs::cos_q12(facing);
    // Where he landed, with the ground under it.
    const gs::GroundSample ground = gs::ground_sample(p.x, p.z);
    SH_DBG("HARRY FALL POSITION mapId=%d roomIdx=%d pos=(%ld,%ld,%ld) yaw=%d groundH=%ld slopeX=%d slopeZ=%d "
           "validPts=%d voidCell=%d",
           (int)save_record().map, (int)save_record().room, (long)p.x, (long)p.y, (long)p.z, (int)facing,
           (long)ground.height, ground.tilt_x, ground.tilt_z, ground.type, (int)(ground.height == kVoidCellHeight));
    p.y = s_rescue.safe_y;
    harry.fall_speed = 0;
    p.x -= static_cast<std::int32_t>((std::int64_t)s * kRescuePush >> 12);
    p.z -= static_cast<std::int32_t>((std::int64_t)c * kRescuePush >> 12);
}

dt::DevKeys& keys() {
    static dt::DevKeys k = [] {
        dt::DevKeys d;
        d.bind({SDL_SCANCODE_KP_MULTIPLY}, false, {}, [](const dt::KeyEvent& ev) {
            if (ev.fire) Pc_FreeCam_Set(!g_DebugCamEnabled);
        });
        d.bind({SDL_SCANCODE_COMMA}, true, inspector, [](const dt::KeyEvent& ev) { scrub(ev, -1); });
        d.bind({SDL_SCANCODE_PERIOD}, true, inspector, [](const dt::KeyEvent& ev) {
            scrub(ev, 1);
            if (echo_keyframe) {
                SH_DBG_ECHO("[DEBUG] KF %d", g_DebugAnimKf);
                echo_keyframe = false;
            }
        });
        d.bind({SDL_SCANCODE_SLASH}, false, inspector, [](const dt::KeyEvent& ev) {
            if (ev.fire) next_anim_start();
        });
        d.bind({SDL_SCANCODE_N}, false, inspector, [](const dt::KeyEvent& ev) {
            if (ev.fire) next_viewed_character();
        });
        d.bind({SDL_SCANCODE_P}, false, inspector, [](const dt::KeyEvent& ev) {
            if (ev.fire) toggle_loop();
        });
        d.bind({SDL_SCANCODE_L}, false, {}, [](const dt::KeyEvent& ev) {
            if (ev.fire) log_eye_offset();
        });
        // The first-person eye tuner owns Numpad 3 while that camera is on.
        d.bind({SDL_SCANCODE_KP_3}, false,
               [] { return session().game.game_state == static_cast<std::int32_t>(gs::GameState::InGame); },
               [](const dt::KeyEvent& ev) {
                   track_room();
                   if (ev.fire && s_rescue.have_safe_y && !sh::input::camera_styles().first_person()) rescue_fall();
               });
        return d;
    }();
    return k;
}

}  // namespace

extern "C" void Pc_DevKeys_Update(void) {
    const unsigned char* ks = g_sdlKeyboardState;
    keys().update([ks](int key) { return ks[key] != 0; }, SDL_GetTicks());
}
