// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory items and weapons of the game as data: one file per thing,
// gamedata/items/<Name>.json and gamedata/weapons/<Name>.json, edited without touching the engine. The file name is the
// identifier: "HealthDrink" is silenthill:HealthDrink, a mod's
// items/Crowbar.json is <mod folder>:Crowbar. The data carries no numbers; the engine numbers
// things at run time through a Numbering the game installs (the game code addresses items by
// the numbers of the original enums). Names and descriptions are game text,
// the import of the disc (English) and gamedata/text/<lang>/items.json (keys item.<Name>.name / .desc).
//
// This module is the data model only — combat, pickup and UI logic read it through ItemLibrary.
// The old single files items/items.json and items/weapons.json still load, with a warning.

#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::items {

// How the inventory treats an item ("inventory" of items/<Name>.json). Names stay
// names here; the inventory module (src/engine/inventory) turns them into its rules.
struct InventoryDef {
    std::uint8_t order = 0;          // position in the sorted inventory, lower first
    std::uint16_t stack_limit = 0;   // duplicates merge up to this; 0: a duplicate is dropped
    bool keep_when_empty = false;    // the slot stays at count 0 (a gun keeps its rounds inside)
    bool counts_ammo_in_hand = false;  // in hand, the ammo counter shows its weapon's ammo stock
    std::string command = "Use";     // command of the slot (Use, UseHealth, Equip, ...)
    std::string command_in_hand;     // command while the item is in hand; empty: cannot be in hand
    std::map<std::string, std::string> command_on_maps;  // map name -> command there
    std::string pickup = "item";     // pickup counter of the ending ranking: "item" or "special"
    bool pickup_once = false;        // a pickup counts only the first time
    std::string name_tag;            // after the name on the screen: "stock", "switch", "hyper_blaster"
    bool stock_in_hand = false;      // in hand, the screen's top counter shows this item's count
    bool used_up_on_event = false;   // using it on an event (a door, a puzzle) uses it up
    std::string usable_ammo;         // counts as usable gun ammo: "loaded", "always", "with_gun"
    bool selectable = true;          // false: the inventory refuses to select it
    std::string switches;            // the On/Off command switches "flashlight" or "radio"
    std::string examine_picture;     // game file shown by Look ("TIM/LION.TIM")
    // Looking at it sets `flag` once `when` is set (empty `when`: always).
    struct ExamineFlag {
        std::string when, flag;
        bool operator==(const ExamineFlag&) const = default;
    } examine_flag;
    bool fills_saturation = false;   // a health item that also fills the health saturation
    // The health it restores: the row of gamedata/config/gameplay.json "health_items" ("health_drink"); empty:
    // none.
    std::string heal;
    // Used near the map's first NPC on `map`: sets `flag` (the Aglaophotis on Cybil).
    struct UseOnNpc {
        std::string map;
        double distance = 0.0, height = 0.0;  // metres: horizontal radius, vertical difference
        std::string flag;
        bool operator==(const UseOnNpc&) const = default;
    } use_on_npc;
    // Unlocked from outside the story (handed out when the inventory opens).
    struct Unlock {
        bool enabled = false;
        std::vector<std::string> endings;  // cleared endings that unlock it ("Ufo", ...)
        bool gun_controller = false;       // a light gun on the second port unlocks it
        bool operator==(const Unlock&) const = default;
    } unlock;

    bool operator==(const InventoryDef&) const = default;
};

// Tilt in 4096 = full turn units, the float truncation the original's angle constants use.
std::int16_t angle_q12(double degrees);

// One inventory item, one gamedata/items/<Name>.json.
struct ItemDef {
    std::string id;            // "silenthill:Handgun", "mymod:Crowbar"
    std::string group;         // "health", "keys", "puzzle", "melee", "guns", "ammo", "portable"
    std::string model_file;    // under convert/, empty: no 3d representation yet
    // Game file of the item's own model (picked up, examined); empty: the shared fallback
    // (gamedata/menu/item_models.json "close_up_fallback").
    std::string close_up_model;
    // A correction the close-up model needs ("liquid_uvs": the stock Unknown Liquid bottle's stray
    // liquid faces, sh::inventory::fix_liquid_uvs); empty: none.
    std::string close_up_fix;
    // Model of the item lying in the world (an LM model name, up to 8 characters: "AIDKIT_N"); what a runtime
    // pickup draws when its script names no model. Empty: none known.
    std::string world_model;
    int pickup_count = 1;  // units one pickup gives (ammo: 15 bullets a box)
    InventoryDef inventory;
    // Tilt of the 3d model on the item screen and in the pickup animation, degrees about the
    // X and Z axes.
    double display_tilt_x = 0.0, display_tilt_z = 0.0;
    // A mod's own close-up model (a glb): the point of the model, in its metres, put at the middle of the item's
    // place ("display_center"); unset: the middle of its bounds.
    std::optional<std::array<double, 3>> display_center;
    // A mod's own close-up model: its size on the item screens times this ("display_scale").
    double display_scale = 1.0;
    // The 3d model is shaded by the item screen's lights; false: drawn with its own colours.
    bool display_lit = false;
    // Run-time number (the original's item id for base items); 0: none could be given.
    int number = 0;

    bool operator==(const ItemDef&) const = default;
};

// One attack of a weapon, one row of the original weapon attack table (the member names follow
// what the combat code does with them). The packed index is the
// weapon number + input*10
// (WEAPON_ATTACK of the original). Q12 values are stored as the raw integer, exactly as the game
// uses them.
struct WeaponAttack {
    enum class Input : std::uint8_t { Tap = 0, Hold = 1, Multitap = 2 };

    // hit_type values the combat code tells apart (the per-character attack update and the
    // attacker-against-target hit check).
    static constexpr std::uint8_t kHitLightSweep = 1;  // the swing is swept in steps over the window
    static constexpr std::uint8_t kHitHeavySweep = 2;  // the whole swing at once, stronger pad rumble
    static constexpr std::uint8_t kHitRay = 3;         // shots along rays (guns): `shape` is shot data

    std::int32_t index = 0;            // the packed weaponAttack the tables are indexed by
    Input input = Input::Tap;
    std::uint8_t hit_type = 0;         // +0x10
    std::int32_t reach_q12 = 0;        // +0x0: far end of the hit segment / ray length
    std::int32_t reach_start_q12 = 0;  // +0x2: near end of the hit segment, from the attacker
    std::uint16_t power = 0;           // +0x4: base damage (times 0.875..1.125 at random)
    std::int32_t knockback_q12 = 0;    // +0xC: push applied to the target along the hit direction
    std::uint8_t keyframe_start = 0;   // +0xE: hit window start, keyframes into the attack animation
    std::uint8_t keyframe_length = 0;  // +0xF: hit window length in keyframes
    std::int8_t hurt_sound = 0;        // +0x11: which hurt sound Harry plays when hit by it; -1: none
    std::uint8_t blood_splatter = 0;   // +0x12: blood effect strength on a hit (0: none)
    // +0x6..+0xB, read two ways by hit_type; the accessors below name both views.
    std::array<std::uint8_t, 6> shape{};

    // Combat rules of the row (weapons/<Name>.json); the defaults are an ordinary melee
    // row. The combat code reads these instead of the packed index.
    std::string impact_sound;            // "impact_sound": the sound of its first hit on a target; empty: none
    bool motor_contact = false;          // "motor_contact": with the motor running a hit grinds (the contact sound
                                         // instead of impact_sound, again on every later hit of the swing)
    std::uint8_t motor_hit_keyframes = 0;  // "motor_hit_keyframes": the hit window's length while the motor runs
    bool finisher = false;               // "finisher": a downed target dies (1000 damage), the push is level, and
                                         // the ranking does not count it (the kick and the stomp)
    bool colour_modes = false;           // "colour_modes": the Hyper Blaster's colour setting scales the damage
    std::string ranking;                 // "ranking": the finished attack's statistic: "melee" (empty), "heavy_melee",
                                         // "distance_shot", "other_shot", "hyper_blaster" or "none"
    bool gas = false;                    // the weapon's "gas" (a motor weapon: the chainsaw, the rock drill)
    bool gun_shot = false;               // the tap row of a "gun" class weapon: it shoots

    // Sweeps (hit_type != kHitRay): the swept box is grown to at least this size per axis.
    std::uint16_t min_hit_box_q12(int axis) const {
        return static_cast<std::uint16_t>(shape[axis * 2] | shape[axis * 2 + 1] << 8);
    }
    // Rays (hit_type == kHitRay): time between shots in animation keyframes (Q12), 0: one burst.
    std::uint16_t shot_interval_q12() const { return min_hit_box_q12(0); }
    std::uint8_t max_shots() const { return shape[2]; }       // shots per attack; 0 counts as 1
    std::uint8_t pellets() const { return shape[3]; }         // rays per shot (the shotgun fires 12)
    std::uint8_t spread_q12() const { return shape[4]; }      // largest off-axis deviation of a ray
    std::uint8_t spread_sectors() const { return shape[5] & 0xF; }  // circle split in N sectors
    std::uint8_t spread_stride() const { return shape[5] >> 4; }    // sectors per pellet; 0: 16

    Input input_type() const { return input; }
};

// The equip rig of a weapon: the animation block the equip pass installs for Harry (clips,
// keyframes, sounds, statuses). Names stay names here — sound names resolve through the sound
// registry, anim names through the character's "anims" — and the caller packs them with
// (index << 1) | active, the layout of the game's status bytes. Written by
// tools/scripts/gen_weapon_rig.py, whose --roundtrip pins the packing.
struct WeaponRigStatus {
    std::string anim;    // name in the character's "anims"
    bool active = false; // false: the blend phase, true: the playback phase
};

struct WeaponRigClip {
    std::string kind;  // "blend_linear" | "playback_once" | "playback_loop"
    WeaponRigStatus status;
    bool variable = false;           // the duration comes from a function, not the constant
    std::optional<WeaponRigStatus> next;  // the clip this one hands over to; nullopt: none
    std::int32_t speed_q12 = 0;      // duration, keyframes per second in Q12 (negative: reverse)
    std::int32_t start_kf = -1;      // -1: none (NO_VALUE)
    std::int32_t end_kf = 0;
};

struct WeaponRigKeyframe {
    WeaponRigStatus status_a;
    WeaponRigStatus status_b;
    std::uint8_t value = 0;  // nothing reads this byte; carried for the exact table image
    std::int16_t start_kf = 0;
    std::int16_t end_kf = 0;
};

struct WeaponRig {
    bool present = false;         // the weapon file has a rig
    std::string anim_file;        // disc path of the weapon's animation file
    std::int32_t hand_collision_q12 = 0;  // the collision box extent the weapon forces on Harry
    std::vector<WeaponRigClip> clips;     // 18..20; a shorter block leaves the last slots alone
    std::vector<WeaponRigKeyframe> keyframes;  // 10
    std::string attack_sound;     // empty: none
    std::string reload_sound;
    std::string out_of_ammo_sound;
    WeaponRigStatus stop_aiming;
    WeaponRigStatus attack;
    WeaponRigStatus attack_hold;
    std::int32_t reload_sound_offset = 0;    // keyframes after the reload clip starts
    std::uint8_t attack_keyframe_slot = 0;   // slot of keyframes[] the attack windows use
    std::uint8_t aim_stop_keyframe_slot = 0;  // slot the aim-stop state uses
    // A mod's weapon's own numbers that Harry's script keeps per base weapon (-1: none, the script's default):
    // the keyframes between which a press queues the melee follow-up swing; the keyframe the aim pose holds on
    // the alternate cameras (first person, other).
    std::array<std::int32_t, 2> follow_up_keyframes{-1, -1};
    std::array<std::int32_t, 2> aim_hold_keyframes{-1, -1};
};

// The bone a weapon without "held_bone" (and any attack that is no weapon's) is held on.
inline constexpr std::array<std::uint8_t, 2> kDefaultHeldBone{1, 0};

// "muzzle_light": the light of the gun's muzzle flash for the modern lighting (map_lights / rt_lights), shown while
// the original draws the flash sprite. Rendering only: no rule reads it.
struct WeaponMuzzleLight {
    std::array<float, 3> color{1.0f, 0.75f, 0.45f};  // linear
    float intensity = 4.0f;
    float range = 6.0f;    // metres
    float radius = 0.03f;  // metres: the emitter's size (shadow softness)
    float fade = 0.5f;     // 0..1: the share of the intensity that is gone at the end of the flash
    bool casts_shadows = true;
    bool gi = true;
};

// One player weapon, one gamedata/weapons/<Name>.json.
struct WeaponDef {
    std::string id;                        // "silenthill:Handgun"
    std::string cls;                       // "melee" or "gun"
    std::string item;                      // id of the item it is carried as; empty: kick, stomp
    std::optional<int> clip;               // rounds the gun holds (255 reads as unlimited); nullopt: melee or unlimited
    std::string ammo;                      // id of the ammo item; empty: melee or unlimited
    std::vector<WeaponAttack> attacks;     // tap/hold/multitap rows that exist
    WeaponRig rig;                         // the equip block; rig.present says whether the file has one
    // Bone Harry holds it on (mesh variant, joint, as the game's MODEL_BONE packs them); nullopt:
    // kDefaultHeldBone.
    std::optional<std::array<std::uint8_t, 2>> held_bone;
    // "gas": a motor weapon that burns gasoline (the chainsaw, the rock drill): the running motor deals the damage,
    // keeps the hit window open from its first keyframe, and stops when the weapon is put away.
    bool gas = false;
    std::optional<WeaponMuzzleLight> muzzle_light;  // nullopt: its flash lights nothing
    // "like": the base weapon whose details Harry's script keeps by weapon (the tip of the blade, the shuffle
    // speed of an aimed turn, the default follow-up and aim hold keyframes) a mod's weapon borrows; empty: the
    // steel pipe for melee, the handgun for a gun.
    std::string like;
    // "touch_icon": the picture of the touch screen's attack button, a file of gamedata/ui/touch/ without ".svg"
    // ("attack_pistol", "attack_blade", ...); empty: the plain attack picture of its class.
    std::string touch_icon;
    // Run-time number (the original's equipped weapon id); -1: the combat code has no slot.
    int number = -1;

    const WeaponAttack* attack(WeaponAttack::Input input) const;
    bool is_gun() const { return cls == "gun"; }
};

// How the engine numbers things at run time. `item` gets the full id and the item's group and
// returns a number (positive, never 0xFF: the empty slot) or -1 when none can be given; `weapon` the
// same for weapons, with the weapon's class. The engine installs one (src/engine/ids/game_numbers.cpp:
// the original's numbers for the base game, free numbers for mods); without one, numbers are 0 / -1.
struct Numbering {
    std::function<int(std::string_view id, std::string_view group)> item;
    std::function<int(std::string_view id, std::string_view cls)> weapon;
};

// Where the loaded data came from.
struct LoadSource {
    std::string mod_id;           // the last mod whose item or weapon files were used; empty: base only
    bool mod_gameplay = false;    // that mod's gameplay flag, for the caller's warning
    bool legacy = false;          // some data came in the old single-file layout
};

class ItemLibrary {
public:
    // The numbering every load() uses unless one is given.
    static void set_default_numbering(Numbering numbering);
    static const Numbering& default_numbering();

    // Reads <gamedata_dir>/items/*.json and <gamedata_dir>/weapons/*.json (the old
    // items/items.json + items/weapons.json when the folders hold none). Unknown fields and
    // non-fatal oddities go to `warnings` (a mod may add fields the engine does not know yet).
    static Result<ItemLibrary> load(const std::filesystem::path& gamedata_dir,
                                    std::vector<std::string>& warnings);

    // The same, then the mods of <mods_dir> by ascending priority: a mod's items/<Name>.json
    // patches silenthill:<Name> when the base game has it, else adds <mod folder>:<Name>;
    // items/<namespace>/<Name>.json patches <namespace>:<Name> (another mod's item). The same
    // for weapons/. The old items/items.json / weapons.json of a mod replace the set, with a
    // warning. A mod changing item data without gameplay = 1 is named in `warnings`.
    static Result<ItemLibrary> load(const std::filesystem::path& gamedata_dir,
                                    const std::filesystem::path& mods_dir, std::vector<std::string>& warnings,
                                    LoadSource* source = nullptr, const Numbering* numbering = nullptr);

    // The old single files from JSON text (tests, tools); see load().
    static Result<ItemLibrary> parse(std::string_view items_json, std::string_view weapons_json,
                                     std::vector<std::string>& warnings, const Numbering* numbering = nullptr);

    std::size_t item_count() const { return items_.size(); }
    const std::vector<ItemDef>& items() const { return items_; }
    const ItemDef* find_item(int number) const;
    // By id: full ("mymod:Crowbar") or short. A short name means <context>:<name> when
    // `context` (a namespace) has it, else silenthill:<name>, else the one namespace that has
    // it; nullptr when unknown or ambiguous.
    const ItemDef* find_item(std::string_view name, std::string_view context = {}) const;

    std::size_t weapon_count() const { return weapons_.size(); }
    const std::vector<WeaponDef>& weapons() const { return weapons_; }
    const WeaponDef* find_weapon(int number) const;
    const WeaponDef* find_weapon(std::string_view name, std::string_view context = {}) const;

    // The weapon carried as the item numbered `item_number`; nullptr when none is.
    const WeaponDef* weapon_of_item(int item_number) const;

    // The weapon number (the packed tap attack) the item numbered `item_number` is held as: its weapon's number. An
    // item of the base game's "melee" or "guns" group no weapon file names (the cutscene props) keeps the original's
    // in-hand number, kBaseHeldOffset past its item number in a signed byte; -1 for anything else (a mod's item that
    // is no weapon, a weapon without a number, an unknown number).
    static constexpr int kBaseHeldOffset = 0x80;
    int held_number(int item_number) const;
    // The item held as weapon number `number` (the inverse of held_number); -1 when no item is.
    int held_item(int number) const;
    // Whether the item is held as a gun: its weapon's class, else (a base prop) its group "guns".
    bool held_as_gun(int item_number) const;
    // The number of the gun item's ammo item (weapons/<Name>.json "ammo"); -1 when it has none.
    int ammo_item_number(int item_number) const;

    // The bone the weapon numbered `weapon_number` is held on; kDefaultHeldBone when no weapon has
    // that number or it names no bone.
    std::array<std::uint8_t, 2> held_bone(int weapon_number) const;

    // Rounds one magazine of the gun numbered `weapon_number` holds: its clip, kUnlimitedClip for a
    // gun without one (Hyper Blaster), 0 for melee weapons and numbers no weapon has (the reload
    // and the manual-reload gate read 0 as "not a gun").
    static constexpr int kUnlimitedClip = 0xFF;
    int clip_capacity(int weapon_number) const;

    // How many units of an item (by number) one pickup gives; 1 when unknown.
    int ammo_pickup_count(int item_number) const;

    // The item the given weapon's ammo is carried as, or nullptr.
    const ItemDef* ammo_item_of(const WeaponDef& weapon) const;

    // Ids and numbers unique, weapons' items and ammo exist, attack indexes match; problems listed.
    std::vector<std::string> check_consistency() const;

private:
    void number_all(const Numbering& numbering, std::vector<std::string>& warnings);

    std::vector<ItemDef> items_;      // sorted by id
    std::vector<WeaponDef> weapons_;  // sorted by id
};

}  // namespace sh::items
