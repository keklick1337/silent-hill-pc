// The weapon equip rig (gamedata/weapons/<Name>.json "rig", engine's ItemLibrary) against the
// numbers of the tables it was generated from: the packed status bytes are written out by hand
// as (anim index << 1) | active with the character's anim numbers noted in the comments. The
// data files are the repository's own (SH_PROJECT_ROOT/gamedata).

#include "framework/sh_test.h"

#include "engine/items/item_library.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sh::items::ItemLibrary;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

}  // namespace

SH_TEST(weapon_rig_handgun) {
    std::vector<std::string> warnings;
    const auto library = ItemLibrary::load(repo_gamedata(), warnings);
    SH_CHECK(library);
    const sh::items::WeaponDef* handgun = library ? library.value().find_weapon("Handgun") : nullptr;
    SH_CHECK(handgun != nullptr);
    if (handgun == nullptr) {
        return;
    }
    SH_CHECK(handgun->rig.present);
    SH_CHECK(handgun->rig.anim_file == "ANIM/HB_WEP4.ANM");
    SH_CHECK(handgun->rig.hand_collision_q12 == -5632);  // -0x1600 of the equip case
    SH_CHECK(handgun->rig.clips.size() == 20);
    SH_CHECK(handgun->rig.keyframes.size() == 10);

    // First clip: the blend into the aim pose. handgun_aim is anim 28: (28 << 1) = 56, active 57.
    const sh::items::WeaponRigClip& blend = handgun->rig.clips[0];
    SH_CHECK(blend.kind == "blend_linear");
    SH_CHECK(blend.status.anim == "handgun_aim");
    SH_CHECK(!blend.status.active);
    SH_CHECK(blend.next.has_value() && blend.next->anim == "handgun_aim" && blend.next->active);
    SH_CHECK(blend.speed_q12 == 409600);  // Q12(100.0): the blend runs at 100 keyframes a second
    SH_CHECK(blend.start_kf == -1);       // no start: NO_VALUE in the table
    SH_CHECK(blend.end_kf == 570);
    const sh::items::WeaponRigClip& aim = handgun->rig.clips[1];
    SH_CHECK(aim.kind == "playback_once");
    SH_CHECK(aim.status.active);
    SH_CHECK(aim.speed_q12 == 143360);  // Q12(35.0)
    SH_CHECK(aim.start_kf == 570 && aim.end_kf == 579);

    // First keyframe record: statuses 56/57, the unread byte 130, keyframes 570..579.
    const sh::items::WeaponRigKeyframe& kf = handgun->rig.keyframes[0];
    SH_CHECK(kf.status_a.anim == "handgun_aim" && !kf.status_a.active);  // packs to 56
    SH_CHECK(kf.status_b.anim == "handgun_aim" && kf.status_b.active);   // packs to 57
    SH_CHECK(kf.value == 130);
    SH_CHECK(kf.start_kf == 570 && kf.end_kf == 579);
    SH_CHECK(handgun->rig.keyframes[9].value == 140);
    SH_CHECK(handgun->rig.keyframes[9].start_kf == 568 && handgun->rig.keyframes[9].end_kf == 587);

    // The info row: sound registry names, the three statuses of the rig
    // (66 = powered_weapon_idle inactive, 72 = gun_attack inactive, 61 = weapon_attack_2
    // active), the reload sound offset 37 and the packed slots byte 0x25 (attack 2, aim stop 5).
    SH_CHECK(handgun->rig.attack_sound == "handgun_shot");
    SH_CHECK(handgun->rig.reload_sound == "handgun_reload");
    SH_CHECK(handgun->rig.out_of_ammo_sound == "handgun_empty");
    SH_CHECK(handgun->rig.stop_aiming.anim == "powered_weapon_idle" && !handgun->rig.stop_aiming.active);
    SH_CHECK(handgun->rig.attack.anim == "gun_attack" && !handgun->rig.attack.active);
    SH_CHECK(handgun->rig.attack_hold.anim == "weapon_attack_2" && handgun->rig.attack_hold.active);
    SH_CHECK(handgun->rig.reload_sound_offset == 37);
    SH_CHECK(handgun->rig.attack_keyframe_slot == 2);
    SH_CHECK(handgun->rig.aim_stop_keyframe_slot == 5);
}

SH_TEST(weapon_rig_hyperblaster) {
    std::vector<std::string> warnings;
    const auto library = ItemLibrary::load(repo_gamedata(), warnings);
    SH_CHECK(library);
    const sh::items::WeaponDef* blaster = library ? library.value().find_weapon("HyperBlaster") : nullptr;
    SH_CHECK(blaster != nullptr);
    if (blaster == nullptr) {
        return;
    }
    SH_CHECK(blaster->rig.present);
    SH_CHECK(blaster->rig.anim_file == "ANIM/HB_WEP53.ANM");
    SH_CHECK(blaster->rig.hand_collision_q12 == -5648);  // -0x1610
    // The clip block is 18 of 20 entries long: the equip pass leaves the last two slots alone.
    SH_CHECK(blaster->rig.clips.size() == 18);
    SH_CHECK(blaster->rig.keyframes.size() == 10);

    // The tenth keyframe record is the one the table read past its end on the hardware: still
    // (anim 0, both statuses pack to 0), the bytes 1896 and 0x8007 landed as they were.
    const sh::items::WeaponRigKeyframe& tail = blaster->rig.keyframes[9];
    SH_CHECK(tail.status_a.anim == "still" && !tail.status_a.active);
    SH_CHECK(tail.status_b.anim == "still" && !tail.status_b.active);
    SH_CHECK(tail.value == 0);
    SH_CHECK(tail.start_kf == 1896);
    SH_CHECK(tail.end_kf == -32761);  // 0x8007 read as signed

    // No reload and no dry-fire sound: null in the data, empty here.
    SH_CHECK(blaster->rig.attack_sound == "hyper_blaster_shot");
    SH_CHECK(blaster->rig.reload_sound.empty());
    SH_CHECK(blaster->rig.out_of_ammo_sound.empty());
    SH_CHECK(blaster->rig.reload_sound_offset == 0);
    SH_CHECK(blaster->rig.attack_keyframe_slot == 2);
    SH_CHECK(blaster->rig.aim_stop_keyframe_slot == 5);

    // A hold-packed attack is not the weapon's number: no rig answers it (the tap arm of the
    // equip switch).
    SH_CHECK(library.value().find_weapon(42) == nullptr);
}
