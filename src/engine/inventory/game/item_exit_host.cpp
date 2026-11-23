// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The item screens' input, carousel scroll and exit over the session, the powered weapons' motor (src/engine/items/powered_weapon.h) and the held item's bone. The
// decisions are sh_inventory's (screen_input.h, item_carousel.h, item_actions.h, exit_anim.h) and the weapons'
// "held_bone" (gamedata/weapons/); the entry points keep the names the menu, the status screen, combat, boot and
// the quick actions call them by.

#include <array>
#include <cstdint>

#include "engine/app/loader/game_files.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state_inventory.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/inventory/exit_anim.h"
#include "engine/inventory/game/inventory_host.h"
#include "engine/inventory/game/item_stage.h"
#include "engine/inventory/item_actions.h"
#include "engine/inventory/item_carousel.h"
#include "engine/inventory/item_catalog.h"
#include "engine/inventory/item_view.h"
#include "engine/inventory/screen_input.h"
#include "engine/items/powered_weapon.h"
#include "engine/log/sh_log.h"
#include "engine/player/harry_states.h"
#include "engine/player/player.h"
#include "engine/player/player_records.h"
#include "engine/scripting/script_buffers.h"
#include "engine/loop/frame_view.h"
#include "engine/loader/queue/disc_files.h"


extern "C" {
// Other modules: the item screens' outline (inventory draw), the held item, the gas weapons' sounds, the
// bone pose of a clip header, a file by its disc path.
void Gfx_Inventory_OutlineTrailReset(void);
void WorldGfx_HeldItemAttach(int chara_id, std::int32_t model_bone);
void PcCombat_GasSoundStopAll(void);
void PcCombat_GasSoundContact(int volume);
void PcCombat_GasSoundMotor(int chainsaw, int volume, int pitch);
void PcCombat_GasSoundDrillRun(int volume, int pitch);
void Anim_BoneUpdate(void* anm_header, sh::entities::BoneCoord* bones, std::int32_t keyframe0, std::int32_t keyframe1,
                     std::int32_t alpha);
}

namespace {

namespace inv = sh::inventory;
using sh::game::save_record;
using sh::game::session;

constexpr int kHarry = 1;  // characters.json id
constexpr std::uint8_t kKitchenKnifeItem = 128;  // the weapon numbering: item number - 128 in a byte
constexpr std::int32_t kFullAlpha = 4096;

// Pad bits of the analog stick past its threshold and of the direction pressed (the game's pad word).
constexpr std::uint32_t kAnalogUp = 1u << 16, kAnalogRight = 1u << 17, kAnalogDown = 1u << 18, kAnalogLeft = 1u << 19;
constexpr std::uint32_t kDirUp = 1u << 24, kDirRight = 1u << 25, kDirDown = 1u << 26, kDirLeft = 1u << 27;

// Where the health portrait's picture goes in texture memory (pages, texel origin, palette).
const sh::loader::ImagePlace kHealthPortrait{{1, 31}, 12, 103, 224, 13};

sh::items::PoweredWeaponMotor s_motor;

class MotorHost final : public sh::items::MotorHost {
public:
    void stop_all_sounds() override { PcCombat_GasSoundStopAll(); }
    void base_sound(std::int32_t volume) override { PcCombat_GasSoundContact(volume); }
    void motor_sound(bool chainsaw, std::int32_t volume, std::int32_t pitch) override
    {
        PcCombat_GasSoundMotor(chainsaw ? 1 : 0, volume, pitch);
    }
    void drill_bit_sound(std::int32_t volume, std::int32_t pitch) override { PcCombat_GasSoundDrillRun(volume, pitch); }
    void clear_power_timer() override { sh::game::harry_vars(session().system.characters.player).gas_weapon_timer = 0; }
    void clear_power_effect() override { session().system.characters.player.attack.phase = 0; }
};

void load_model(inv::ExitAnimModel& out, const sh::entities::ModelRecord& model)
{
    out.anim_status = model.anim.status;
    out.keyframe = model.anim.keyframe;
    out.control_state = model.control_state;
    out.state_step = model.control_step;
    out.anim_time = model.anim.time;
    out.anim_alpha = model.anim.alpha;
}

void store_model(sh::entities::ModelRecord& model, const inv::ExitAnimModel& in)
{
    model.anim.status = in.anim_status;
    model.anim.keyframe = in.keyframe;
    model.control_state = in.control_state;
    model.control_step = in.state_step;
    model.anim.time = in.anim_time;
    model.anim.alpha = in.anim_alpha;
}

// The weapon number of the item in hand: what the data holds it as (ItemCatalog::held_number); without item data the
// original's numbering, item - 128 in a byte.
int equipped_weapon_number(int item)
{
    if (inv::item_catalog().loaded())
        return inv::item_catalog().held_number(item);
    return static_cast<std::uint8_t>(item + kKitchenKnifeItem);
}

// Links the model of `slot`'s item to carousel display `display`.
void link_model(std::int32_t display, std::int32_t slot)
{
    const int item = save_record().items[slot].item;
    if (inv::item_stage().link_from_pack(display, item))
        return;
    // Cheat-granted items may be absent from this map's pack list: the slot stays blank.
    inv::item_stage().unlink(display);
    SH_DBG("[UNIFIEDITEM] carousel fallback source=none slot=%d item=%u", static_cast<int>(display),
           static_cast<unsigned>(item));
}

}  // namespace

extern "C" {

void Gfx_PlayerHeldItemAttach(std::int32_t weapon_attack);

void Inventory_DirectionalInputSet(void)
{
    const sh::game::PadState& pad0 = session().game.pads[0];
    inv::PadFrame pad;
    pad.clicked = pad0.clicked;
    pad.held = pad0.held;
    pad.pulsed = pad0.pulsed;
    pad.stick_x = pad0.sticks.left_x;
    pad.stick_y = pad0.sticks.left_y;

    static const inv::DirectionBits kAnalog{kAnalogUp, kAnalogRight, kAnalogDown, kAnalogLeft};
    static const inv::DirectionBits kDirection{kDirUp, kDirRight, kDirDown, kDirLeft};
    inv::item_screen().directions = inv::read_directions(pad, kAnalog, kDirection);
}

void Inventory_ScrollUpdate(std::int32_t direction, std::int32_t slot)
{
    inv::ItemView& view = inv::item_view();
    std::array<std::int32_t, inv::kCarouselModels> displays;
    for (int i = 0; i < inv::kCarouselModels; i++)
        displays[static_cast<std::size_t>(i)] = view.shown[static_cast<std::size_t>(i)];

    const int display = inv::scroll_in_slot(displays, direction != 0, slot, save_record().slot_count);

    if (save_record().items[slot].item != 0xFF)
        link_model(display, slot);

    for (int i = 0; i < inv::kCarouselModels; i++)
        view.shown[static_cast<std::size_t>(i)] = displays[static_cast<std::size_t>(i)];
}

// The inventory opens: the ammo in hand goes back to its slots, the outline restarts and the portrait is read.
void Inventory_UnkS8Set(std::int32_t /*unused*/)
{
    sh::game::PlayerCombat& combat = session().system.combat;
    inv::Slots slots = sh::game_state::inventory_slots();
    inv::HandAmmo hand;
    hand.clip = combat.ammo_loaded;
    hand.reserve = combat.ammo_total;
    // The original scans items[weapon slot] even with nothing in hand (index -1: the four bytes before the save);
    // store_hand_ammo writes nothing then, the engine's guard.
    const bool tidy = inv::store_hand_ammo(slots, save_record().slot_count, combat.weapon_slot, hand,
                                           inv::game::item_rules());
    sh::game_state::set_inventory_slots(slots);
    if (tidy)
        inv::game::tidy_live();

    Gfx_Inventory_OutlineTrailReset();

    sh::loader::queue_read_image(sh::loader::disc_files().find("ITEM/HEROPIC2.TIM"), sh::scripting::file_buffer(1), &kHealthPortrait);
}

// Harry's first aiming leg state, and how long after the inventory the aim comes straight back up (Q12 seconds).
constexpr std::int32_t kLowerBodyAim = 20;
constexpr std::int32_t kAimResumeWindow = 4096;

void Inventory_ExitAnimEquippedItemUpdate(std::uint8_t* /*unused: the original overwrites it before reading it*/)
{
    sh::entities::CharacterSlots& characters = session().system.characters;
    sh::entities::CharacterRecord& chara = characters.player;
    sh::entities::UpperBodyRecord& upper = characters.player_upper;
    sh::game::HarryVars& vars = sh::game::harry_vars(chara);
    sh::game::PlayerCombat& combat = session().system.combat;

    sh::game::player().clear_controls();

    inv::ExitAnimPlayer p;
    p.state = upper.state;
    p.upper_body_state = upper.upper_state;
    p.lower_body_state = upper.lower_state;
    p.flags = vars.flags;
    p.move_speed = vars.move_speed;
    p.afk_timer = vars.afk_timer;
    p.gas_power_timer = vars.gas_weapon_timer;
    p.power_effect = chara.attack.phase;
    p.weapon_attack = combat.weapon_attack;
    p.previous_attack = inv::item_stage().weapon_on_open;
    p.is_aiming = combat.aiming;
    load_model(p.body, chara.model);
    load_model(p.upper, upper.model);

    inv::exit_anim_apply(p);

    // GAMEPLAY CHANGE (aim_resume_after_inventory): the stance survived the inventory (the same weapon, still
    // aimed), so Harry's script may bring the weapon straight back up for a while.
    const bool stance_kept = p.is_aiming != 0 && p.lower_body_state >= kLowerBodyAim && p.weapon_attack != -1 &&
                             p.previous_attack == p.weapon_attack;
    sh::game::player().set(sh::game::PlayerVar::AimResume,
                           g_PcConfig.aimResumeAfterInventory != 0 && stance_kept ? kAimResumeWindow : 0);

    upper.state = p.state;
    upper.upper_state = p.upper_body_state;
    upper.lower_state = p.lower_body_state;
    vars.flags = p.flags;
    vars.move_speed = p.move_speed;
    vars.afk_timer = p.afk_timer;
    vars.gas_weapon_timer = p.gas_power_timer;
    chara.attack.phase = p.power_effect;
    combat.aiming = p.is_aiming;
    store_model(chara.model, p.body);
    store_model(upper.model, p.upper);

    // Pose the lower half with the upper-body bones off, then the upper half with the lower off.
    void* clips = sh::scripting::file_buffer(0);
    upper.disabled_bones = static_cast<std::int32_t>(sh::game::harry::kUpperBodyBones);
    Anim_BoneUpdate(clips, characters.player_bones, chara.model.anim.keyframe, chara.model.anim.keyframe, kFullAlpha);
    upper.disabled_bones = static_cast<std::int32_t>(sh::game::harry::kLowerBodyBones);
    Anim_BoneUpdate(clips, characters.player_bones, upper.model.anim.keyframe, upper.model.anim.keyframe, kFullAlpha);
    inv::item_stage().close_projection();
}

void Player_WeaponModelStateSet(std::int32_t held_attack, std::int8_t event)
{
    MotorHost host;
    s_motor.handle(held_attack, event, save_record().map, sh::loop::frame_delta(), host);
}

void Inventory_ExitAnimFixes(void)
{
    sh::game::PlayerCombat& combat = session().system.combat;
    std::uint8_t attack = static_cast<std::uint8_t>(combat.weapon_attack);

    save_record().equipped_weapon = inv::inventory_menu().equipped_item;
    const int equipped = save_record().equipped_weapon;
    const inv::ExitWeapon exit = inv::weapon_on_exit(equipped, equipped != 0 ? equipped_weapon_number(equipped) : 0);
    combat.weapon_attack = exit.attack;
    if (exit.stop_aiming)
        combat.aiming = 0;

    Gfx_PlayerHeldItemAttach(combat.weapon_attack);
    Inventory_ExitAnimEquippedItemUpdate(&attack);
}

void Gfx_PlayerHeldItemAttach(std::int32_t weapon_attack)
{
    const sh::items::ItemLibrary* library = inv::item_catalog().library();
    const std::array<std::uint8_t, 2> bone =
        library != nullptr ? library->held_bone(weapon_attack) : sh::items::kDefaultHeldBone;
    WorldGfx_HeldItemAttach(kHarry, static_cast<std::int16_t>(bone[0] | (bone[1] << 4)));
}

}  // extern "C"
