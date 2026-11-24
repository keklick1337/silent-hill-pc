// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/items/item_library.h"

#include "engine/ids/ids.h"
#include "engine/mods/mods.h"
#include "formats/common/json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>

namespace sh::items {

namespace {

namespace fs = std::filesystem;
using json::Value;

[[noreturn]] void fail(std::string message) { throw std::runtime_error(std::move(message)); }

std::string where(const Value& value, std::string_view what) {
    return std::to_string(value.line()) + ", column " + std::to_string(value.column()) + ": " +
           std::string(what);
}

std::int64_t require_int(const Value& parent, std::string_view key, const Value& entry) {
    const Value* field = parent.find(key);
    if (!field) {
        fail(where(entry, "missing \"" + std::string(key) + "\""));
    }
    if (!field->is_number()) {
        fail(where(*field, "\"" + std::string(key) + "\" must be a number"));
    }
    return field->integer();
}

std::string opt_string(const Value& parent, std::string_view key) {
    const Value* field = parent.find(key);
    return field && field->is_string() ? field->string() : std::string();
}

std::int64_t opt_int(const Value& parent, std::string_view key, std::int64_t fallback) {
    const Value* field = parent.find(key);
    if (!field || field->is_null()) {
        return fallback;
    }
    if (!field->is_number()) {
        fail(where(*field, "\"" + std::string(key) + "\" must be a number or null"));
    }
    return field->integer();
}

double opt_double(const Value& parent, std::string_view key, double fallback) {
    const Value* v = parent.find(key);
    return v != nullptr && v->is_number() ? v->number() : fallback;
}

bool opt_bool(const Value& parent, std::string_view key, bool fallback) {
    const Value* field = parent.find(key);
    if (!field || field->is_null()) {
        return fallback;
    }
    if (!field->is_bool()) {
        fail(where(*field, "\"" + std::string(key) + "\" must be true, false or null"));
    }
    return field->boolean();
}

template <typename T>
T clamp_to(std::int64_t value) {
    return static_cast<T>(std::clamp<std::int64_t>(value, std::numeric_limits<T>::min(),
                                                   std::numeric_limits<T>::max()));
}

Numbering& default_numbering_storage() {
    static Numbering numbering;
    return numbering;
}

// The groups of the old layout ("healthitems" ...) -> the readable ones.
std::string group_name(std::string_view group) {
    if (group == "healthitems") return "health";
    if (group == "puzzleitems") return "puzzle";
    if (group == "meleeweapons") return "melee";
    if (group == "gunweapons") return "guns";
    if (group == "gunammo") return "ammo";
    if (group == "none" || group == "portableitems") return "portable";
    return std::string(group);
}

bool known_group(std::string_view group) {
    for (std::string_view g : {"health", "keys", "puzzle", "melee", "guns", "ammo", "portable"}) {
        if (group == g) {
            return true;
        }
    }
    return false;
}

// "InvItemId_Handgun" / "EquippedWeaponId_Handgun" -> "silenthill:Handgun".
std::string id_of_enum(std::string_view enum_name) {
    for (std::string_view prefix : {"InvItemId_", "EquippedWeaponId_"}) {
        if (enum_name.starts_with(prefix)) {
            enum_name.remove_prefix(prefix.size());
        }
    }
    return ids::qualify(enum_name);
}

// A file of the new layout names nothing by number: say so once per file.
void note_numeric_keys(const Value& entry, std::string_view origin, std::vector<std::string>& warnings) {
    for (std::string_view key : {"id", "enum", "item_id", "item_enum", "ammo_item_enum"}) {
        if (entry.find(key) != nullptr) {
            warnings.push_back(std::string(origin) + ": \"" + std::string(key) +
                               "\" is ignored: the file name is the identifier, other things are named by "
                               "id (docs/modding/data.md)");
            return;
        }
    }
}

// The "inventory" object of an item file over `inventory` (a patch changes only what it names).
void merge_inventory(const Value& entry, InventoryDef& inventory) {
    if (!entry.is_object()) {
        fail(where(entry, "\"inventory\" must be an object"));
    }
    if (entry.find("order") != nullptr) {
        inventory.order = clamp_to<std::uint8_t>(opt_int(entry, "order", 0));
    }
    if (entry.find("stack_limit") != nullptr) {
        inventory.stack_limit = clamp_to<std::uint16_t>(opt_int(entry, "stack_limit", 0));
    }
    inventory.keep_when_empty = opt_bool(entry, "keep_when_empty", inventory.keep_when_empty);
    inventory.counts_ammo_in_hand = opt_bool(entry, "counts_ammo_in_hand", inventory.counts_ammo_in_hand);
    if (const Value* command = entry.find("command"); command && command->is_string()) {
        inventory.command = command->string();
    }
    if (const Value* command = entry.find("command_in_hand"); command && command->is_string()) {
        inventory.command_in_hand = command->string();
    }
    if (const Value* maps = entry.find("command_on_maps"); maps && maps->is_object()) {
        for (const auto& [map, command] : maps->members()) {
            if (!command.is_string()) {
                fail(where(command, "\"command_on_maps\" values must be command names"));
            }
            inventory.command_on_maps[map] = command.string();
        }
    }
    if (const Value* pickup = entry.find("pickup"); pickup && pickup->is_string()) {
        if (pickup->string() != "item" && pickup->string() != "special") {
            fail(where(*pickup, "\"pickup\" must be \"item\" or \"special\""));
        }
        inventory.pickup = pickup->string();
    }
    inventory.pickup_once = opt_bool(entry, "pickup_once", inventory.pickup_once);
    if (const Value* tag = entry.find("name_tag"); tag && tag->is_string()) {
        if (tag->string() != "stock" && tag->string() != "switch" && tag->string() != "hyper_blaster" &&
            !tag->string().empty()) {
            fail(where(*tag, "\"name_tag\" must be \"stock\", \"switch\", \"hyper_blaster\" or \"\""));
        }
        inventory.name_tag = tag->string();
    }
    inventory.stock_in_hand = opt_bool(entry, "stock_in_hand", inventory.stock_in_hand);
    inventory.used_up_on_event = opt_bool(entry, "used_up_on_event", inventory.used_up_on_event);
    if (const Value* usable = entry.find("usable_ammo"); usable && usable->is_string()) {
        if (usable->string() != "loaded" && usable->string() != "always" && usable->string() != "with_gun" &&
            !usable->string().empty()) {
            fail(where(*usable, "\"usable_ammo\" must be \"loaded\", \"always\", \"with_gun\" or \"\""));
        }
        inventory.usable_ammo = usable->string();
    }
    if (const Value* unlock = entry.find("unlock"); unlock != nullptr) {
        if (!unlock->is_object()) {
            fail(where(*unlock, "\"unlock\" must be an object"));
        }
        inventory.unlock.enabled = true;
        if (const Value* endings = unlock->find("endings"); endings && endings->is_array()) {
            inventory.unlock.endings.clear();
            for (const Value& ending : endings->array()) {
                if (!ending.is_string()) {
                    fail(where(ending, "\"endings\" must list ending names"));
                }
                inventory.unlock.endings.push_back(ending.string());
            }
        }
        inventory.unlock.gun_controller = opt_bool(*unlock, "gun_controller", inventory.unlock.gun_controller);
    }
    inventory.selectable = opt_bool(entry, "selectable", inventory.selectable);
    if (const Value* sw = entry.find("switches"); sw && sw->is_string()) {
        if (sw->string() != "flashlight" && sw->string() != "radio" && !sw->string().empty()) {
            fail(where(*sw, "\"switches\" must be \"flashlight\", \"radio\" or \"\""));
        }
        inventory.switches = sw->string();
    }
    if (const Value* picture = entry.find("examine_picture"); picture && picture->is_string()) {
        inventory.examine_picture = picture->string();
    }
    if (const Value* mark = entry.find("examine_flag"); mark && mark->is_object()) {
        if (const Value* when = mark->find("when"); when && when->is_string()) {
            inventory.examine_flag.when = when->string();
        }
        if (const Value* flag = mark->find("flag"); flag && flag->is_string()) {
            inventory.examine_flag.flag = flag->string();
        }
    }
    inventory.fills_saturation = opt_bool(entry, "fills_saturation", inventory.fills_saturation);
    if (const Value* heal = entry.find("heal"); heal && heal->is_string()) {
        inventory.heal = heal->string();
    }
    if (const Value* use = entry.find("use_on_npc"); use && use->is_object()) {
        if (const Value* map = use->find("map"); map && map->is_string()) {
            inventory.use_on_npc.map = map->string();
        }
        if (const Value* flag = use->find("flag"); flag && flag->is_string()) {
            inventory.use_on_npc.flag = flag->string();
        }
        if (const Value* v = use->find("distance"); v && v->is_number()) {
            inventory.use_on_npc.distance = v->number();
        }
        if (const Value* v = use->find("height"); v && v->is_number()) {
            inventory.use_on_npc.height = v->number();
        }
    }
}

void merge_item(const Value& entry, ItemDef& item) {
    if (!entry.is_object()) {
        fail(where(entry, "an item must be an object"));
    }
    if (const Value* group = entry.find("group"); group && group->is_string()) {
        item.group = group_name(group->string());
    }
    if (const Value* tilt = entry.find("display_tilt"); tilt && tilt->is_object()) {
        item.display_tilt_x = opt_double(*tilt, "x", item.display_tilt_x);
        item.display_tilt_z = opt_double(*tilt, "z", item.display_tilt_z);
    }
    if (const Value* model = entry.find("model"); model && model->is_string()) {
        item.model_file = model->string();
    }
    if (const Value* lit = entry.find("display_lit"); lit && lit->is_bool()) {
        item.display_lit = lit->boolean();
    }
    if (const Value* model = entry.find("close_up_model"); model && model->is_string()) {
        item.close_up_model = model->string();
    }
    if (const Value* fix = entry.find("close_up_fix"); fix && fix->is_string()) {
        item.close_up_fix = fix->string();
    }
    if (const Value* model = entry.find("world_model"); model && model->is_string()) {
        item.world_model = model->string();
    }
    if (entry.find("pickup_count") != nullptr) {
        item.pickup_count = clamp_to<int>(opt_int(entry, "pickup_count", 1));
    }
    if (const Value* inventory = entry.find("inventory"); inventory != nullptr) {
        merge_inventory(*inventory, item.inventory);
    }
}

void merge_attack(const Value& entry, WeaponAttack& attack, bool& legacy) {
    // The unnamed keys of the earliest weapon files are not read; a row that still has them is
    // reported once per file so the data can be renamed.
    for (const Value::Member& member : entry.members()) {
        if (member.first.starts_with("field_") || member.first == "chara") {
            legacy = true;
        }
    }
    const auto set_int = [&](std::string_view key, auto& field) {
        if (entry.find(key) != nullptr) {
            field = clamp_to<std::remove_reference_t<decltype(field)>>(opt_int(entry, key, 0));
        }
    };
    set_int("reach_q12", attack.reach_q12);
    set_int("reach_start_q12", attack.reach_start_q12);
    set_int("power", attack.power);
    set_int("knockback_q12", attack.knockback_q12);
    set_int("keyframe_start", attack.keyframe_start);
    set_int("keyframe_length", attack.keyframe_length);
    set_int("hit_type", attack.hit_type);
    set_int("hurt_sound", attack.hurt_sound);
    set_int("blood_splatter", attack.blood_splatter);
    if (const Value* v = entry.find("impact_sound"); v != nullptr) {
        attack.impact_sound = v->is_string() ? v->string() : std::string();
    }
    attack.motor_contact = opt_bool(entry, "motor_contact", attack.motor_contact);
    set_int("motor_hit_keyframes", attack.motor_hit_keyframes);
    attack.finisher = opt_bool(entry, "finisher", attack.finisher);
    attack.colour_modes = opt_bool(entry, "colour_modes", attack.colour_modes);
    if (const Value* v = entry.find("ranking"); v != nullptr) {
        attack.ranking = v->is_string() ? v->string() : std::string();
        if (!attack.ranking.empty() && attack.ranking != "melee" && attack.ranking != "heavy_melee" &&
            attack.ranking != "distance_shot" && attack.ranking != "other_shot" &&
            attack.ranking != "hyper_blaster" && attack.ranking != "none") {
            fail(where(*v, "\"ranking\" is melee, heavy_melee, distance_shot, other_shot, hyper_blaster or none"));
        }
    }

    // The six shape bytes: the box view, then the shot view.
    const auto set_u16 = [&](std::size_t at, std::int64_t value) {
        const auto v = clamp_to<std::uint16_t>(value);
        attack.shape[at] = static_cast<std::uint8_t>(v & 0xFF);
        attack.shape[at + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    if (const Value* box = entry.find("min_hit_box_q12"); box != nullptr && !box->is_null()) {
        if (!box->is_array() || box->array().size() != 3) {
            fail(where(*box, "\"min_hit_box_q12\" must be an array of 3 numbers"));
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const Value& v = box->array()[axis];
            if (!v.is_number()) {
                fail(where(v, "\"min_hit_box_q12\" must be an array of 3 numbers"));
            }
            set_u16(axis * 2, v.integer());
        }
    }
    if (entry.find("shot_interval_q12") != nullptr) {
        set_u16(0, opt_int(entry, "shot_interval_q12", 0));
    }
    if (entry.find("max_shots") != nullptr) {
        attack.shape[2] = clamp_to<std::uint8_t>(opt_int(entry, "max_shots", 0));
    }
    if (entry.find("pellets") != nullptr) {
        attack.shape[3] = clamp_to<std::uint8_t>(opt_int(entry, "pellets", 0));
    }
    if (entry.find("spread_q12") != nullptr) {
        attack.shape[4] = clamp_to<std::uint8_t>(opt_int(entry, "spread_q12", 0));
    }
    if (entry.find("spread_sectors") != nullptr || entry.find("spread_stride") != nullptr) {
        const auto sectors = std::clamp<std::int64_t>(opt_int(entry, "spread_sectors", attack.spread_sectors()), 0, 15);
        const auto stride = std::clamp<std::int64_t>(opt_int(entry, "spread_stride", attack.spread_stride()), 0, 15);
        attack.shape[5] = static_cast<std::uint8_t>(stride << 4 | sectors);
    }
}

WeaponAttack::Input parse_input(const Value& entry) {
    const std::string input = opt_string(entry, "input");
    if (input == "tap") {
        return WeaponAttack::Input::Tap;
    }
    if (input == "hold") {
        return WeaponAttack::Input::Hold;
    }
    if (input == "multitap") {
        return WeaponAttack::Input::Multitap;
    }
    fail(where(entry, "unknown attack input \"" + input + "\" (tap, hold, multitap)"));
}

// One packed anim status value: {"anim": name, "active": bool}.
WeaponRigStatus rig_status_value(const Value& value, std::string_view what) {
    if (!value.is_object()) {
        fail(where(value, std::string(what) + " must be an object with \"anim\" and \"active\""));
    }
    WeaponRigStatus status;
    status.anim = opt_string(value, "anim");
    status.active = opt_bool(value, "active", false);
    if (status.anim.empty()) {
        fail(where(value, std::string(what) + " has no \"anim\""));
    }
    return status;
}

// One packed anim status: {"anim": name, "active": bool}, looked up by key on the parent.
WeaponRigStatus parse_rig_status(const Value& parent, std::string_view key) {
    const Value* field = parent.find(key);
    if (field == nullptr || !field->is_object()) {
        fail(where(parent, "\"" + std::string(key) + "\" must be an object with \"anim\" and \"active\""));
    }
    return rig_status_value(*field, key);
}

// The equip block (tools/scripts/gen_weapon_rig.py writes it; unknown keys are ignored like the
// rest of the file). A rig with a wrong shape fails the file, like the other sections.
void parse_rig(const Value& rig, WeaponRig& out) {
    if (!rig.is_object()) {
        fail(where(rig, "\"rig\" must be an object"));
    }
    out.present = true;
    out.anim_file = opt_string(rig, "anim_file");
    out.hand_collision_q12 = clamp_to<std::int32_t>(opt_int(rig, "hand_collision_q12", 0));
    if (const Value* clips = rig.find("clips"); clips && !clips->is_null()) {
        if (!clips->is_array()) {
            fail(where(*clips, "\"clips\" must be an array"));
        }
        for (const Value& row : clips->array()) {
            if (!row.is_object()) {
                fail(where(row, "a rig clip must be an object"));
            }
            WeaponRigClip clip;
            clip.kind = opt_string(row, "kind");
            // The clip's own status sits flat on the row ("anim"/"active"), not in a sub-object.
            clip.status = WeaponRigStatus{opt_string(row, "anim"), opt_bool(row, "active", false)};
            if (clip.status.anim.empty()) {
                fail(where(row, "a rig clip has no \"anim\""));
            }
            clip.variable = opt_bool(row, "variable", false);
            if (const Value* next = row.find("next")) {
                if (!next->is_null()) {
                    clip.next = WeaponRigStatus{opt_string(*next, "anim"), opt_bool(*next, "active", false)};
                }
            }
            clip.speed_q12 = clamp_to<std::int32_t>(opt_int(row, "speed_q12", 0));
            clip.start_kf = clamp_to<std::int32_t>(opt_int(row, "start_kf", -1));
            clip.end_kf = clamp_to<std::int32_t>(opt_int(row, "end_kf", 0));
            out.clips.push_back(std::move(clip));
        }
    }
    if (const Value* rows = rig.find("keyframes"); rows && !rows->is_null()) {
        if (!rows->is_array()) {
            fail(where(*rows, "\"keyframes\" must be an array"));
        }
        for (const Value& row : rows->array()) {
            if (!row.is_object()) {
                fail(where(row, "a rig keyframe must be an object"));
            }
            WeaponRigKeyframe keyframe;
            keyframe.status_a = parse_rig_status(row, "status_a");
            keyframe.status_b = parse_rig_status(row, "status_b");
            keyframe.value = clamp_to<std::uint8_t>(opt_int(row, "value", 0));
            keyframe.start_kf = clamp_to<std::int16_t>(opt_int(row, "start_kf", 0));
            keyframe.end_kf = clamp_to<std::int16_t>(opt_int(row, "end_kf", 0));
            out.keyframes.push_back(std::move(keyframe));
        }
    }
    if (const Value* sounds = rig.find("sounds"); sounds && sounds->is_object()) {
        out.attack_sound = opt_string(*sounds, "attack");
        out.reload_sound = opt_string(*sounds, "reload");
        out.out_of_ammo_sound = opt_string(*sounds, "out_of_ammo");
    }
    if (const Value* anims = rig.find("anims"); anims && anims->is_object()) {
        out.stop_aiming = parse_rig_status(*anims, "stop_aiming");
        out.attack = parse_rig_status(*anims, "attack");
        out.attack_hold = parse_rig_status(*anims, "attack_hold");
        out.reload_sound_offset = clamp_to<std::int32_t>(opt_int(*anims, "reload_sound_offset", 0));
        out.attack_keyframe_slot = clamp_to<std::uint8_t>(opt_int(*anims, "attack_keyframe_slot", 0));
        out.aim_stop_keyframe_slot = clamp_to<std::uint8_t>(opt_int(*anims, "aim_stop_keyframe_slot", 0));
        const auto pair = [&](const char* key, std::array<std::int32_t, 2>& to) {
            const Value* v = anims->find(key);
            if (v == nullptr || v->is_null()) {
                return;
            }
            if (!v->is_array() || v->array().size() != 2 || !v->array()[0].is_integer() ||
                !v->array()[1].is_integer()) {
                fail(where(*v, "\"" + std::string(key) + "\" must be two integers (keyframes)"));
            }
            to = {clamp_to<std::int32_t>(v->array()[0].integer()), clamp_to<std::int32_t>(v->array()[1].integer())};
        };
        pair("follow_up_keyframes", out.follow_up_keyframes);
        pair("aim_hold_keyframes", out.aim_hold_keyframes);
    }
}

// "muzzle_light" over `light`, key by key (a mod's patch may change only the colour).
void merge_muzzle_light(const Value& v, WeaponMuzzleLight& light) {
    if (!v.is_object()) {
        fail(where(v, "\"muzzle_light\" must be an object or null"));
    }
    const auto number = [&](const char* key, float& to, double lo, double hi) {
        const Value* f = v.find(key);
        if (f == nullptr) {
            return;
        }
        if (!f->is_number() || !std::isfinite(f->number()) || f->number() < lo || f->number() > hi) {
            fail(where(*f, "\"" + std::string(key) + "\" must be a number in " + std::to_string(lo) + " .. " +
                               std::to_string(hi)));
        }
        to = static_cast<float>(f->number());
    };
    if (const Value* c = v.find("color")) {
        if (!c->is_array() || c->array().size() != 3) {
            fail(where(*c, "\"color\" must be [r, g, b] in 0 .. 1"));
        }
        for (std::size_t i = 0; i < 3; ++i) {
            const Value& e = c->array()[i];
            if (!e.is_number() || e.number() < 0.0 || e.number() > 1.0) {
                fail(where(e, "\"color\" must be [r, g, b] in 0 .. 1"));
            }
            light.color[i] = static_cast<float>(e.number());
        }
    }
    number("intensity", light.intensity, 0.0, 64.0);
    number("range", light.range, 0.1, 64.0);
    number("radius", light.radius, 0.0, 2.0);
    number("fade", light.fade, 0.0, 1.0);
    light.casts_shadows = opt_bool(v, "casts_shadows", light.casts_shadows);
    light.gi = opt_bool(v, "gi", light.gi);
}

// A weapon file's fields over `weapon` (a patch changes only what it names). Item and ammo
// names are full ids or short names resolved later in the file's namespace.
void merge_weapon(const Value& entry, WeaponDef& weapon, bool& legacy) {
    if (!entry.is_object()) {
        fail(where(entry, "a weapon must be an object"));
    }
    if (const Value* cls = entry.find("class"); cls && cls->is_string()) {
        weapon.cls = cls->string();
    }
    if (const Value* item = entry.find("item")) {
        weapon.item = item->is_string() ? item->string() : std::string();
    }
    if (const Value* ammo = entry.find("ammo")) {
        weapon.ammo = ammo->is_string() ? ammo->string() : std::string();
    }
    if (const Value* clip = entry.find("clip")) {
        if (clip->is_null()) {
            weapon.clip.reset();
        } else if (!clip->is_number()) {
            fail(where(*clip, "\"clip\" must be a number or null"));
        } else {
            weapon.clip = clamp_to<int>(clip->integer());
        }
    }
    if (const Value* bone = entry.find("held_bone")) {
        if (bone->is_null()) {
            weapon.held_bone.reset();
        } else {
            if (!bone->is_array() || bone->array().size() != 2 || !bone->array()[0].is_number() ||
                !bone->array()[1].is_number()) {
                fail(where(*bone, "\"held_bone\" must be [mesh variant, joint] or null"));
            }
            // MODEL_BONE packs each part in four bits.
            weapon.held_bone = std::array<std::uint8_t, 2>{
                clamp_to<std::uint8_t>(std::clamp<std::int64_t>(bone->array()[0].integer(), 0, 15)),
                clamp_to<std::uint8_t>(std::clamp<std::int64_t>(bone->array()[1].integer(), 0, 15))};
        }
    }
    if (const Value* rig = entry.find("rig")) {
        if (!rig->is_null()) {
            parse_rig(*rig, weapon.rig);
        }
    }
    weapon.gas = opt_bool(entry, "gas", weapon.gas);
    if (const Value* light = entry.find("muzzle_light")) {
        if (light->is_null()) {
            weapon.muzzle_light.reset();
        } else {
            if (!weapon.muzzle_light) {
                weapon.muzzle_light.emplace();
            }
            merge_muzzle_light(*light, *weapon.muzzle_light);
        }
    }
    if (const Value* like = entry.find("like")) {
        weapon.like = like->is_string() ? like->string() : std::string();
    }
    if (const Value* icon = entry.find("touch_icon")) {
        weapon.touch_icon = icon->is_string() ? icon->string() : std::string();
    }
    const Value* attacks = entry.find("attacks");
    if (attacks && !attacks->is_null()) {
        if (!attacks->is_array()) {
            fail(where(*attacks, "\"attacks\" must be an array"));
        }
        for (const Value& row : attacks->array()) {
            if (!row.is_object()) {
                fail(where(row, "an attack must be an object"));
            }
            const WeaponAttack::Input input = parse_input(row);
            auto it = std::find_if(weapon.attacks.begin(), weapon.attacks.end(),
                                   [&](const WeaponAttack& a) { return a.input == input; });
            if (it == weapon.attacks.end()) {
                weapon.attacks.push_back(WeaponAttack{});
                it = std::prev(weapon.attacks.end());
                it->input = input;
            }
            merge_attack(row, *it, legacy);
        }
        std::sort(weapon.attacks.begin(), weapon.attacks.end(),
                  [](const WeaponAttack& a, const WeaponAttack& b) { return a.input < b.input; });
    }
}

Result<std::string> read_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return Result<std::string>::fail("cannot read " + file.string());
    }
    return Result<std::string>::ok(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

Value read_json(const fs::path& file) {
    const Result<std::string> text = read_file(file);
    if (!text) {
        fail(text.error());
    }
    Result<Value> doc = json::parse(text.value());
    if (!doc) {
        fail(file.string() + ": " + doc.error());
    }
    return std::move(doc.value());
}

bool is_legacy_file(const fs::path& file) {
    const std::string name = file.filename().string();
    return name == "items.json" || name == "weapons.json";
}

// The <Name>.json files of a directory (not the old single files), sorted.
std::vector<fs::path> thing_files(const fs::path& dir) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return files;
    }
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const fs::path& p = e.path();
        if (e.is_regular_file(ec) && p.extension() == ".json" && !p.filename().string().starts_with(".") &&
            !is_legacy_file(p)) {
            files.push_back(p);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<fs::path> namespace_dirs(const fs::path& dir) {
    std::vector<fs::path> dirs;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return dirs;
    }
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_directory(ec) && ids::valid_namespace(e.path().filename().string())) {
            dirs.push_back(e.path());
        }
    }
    std::sort(dirs.begin(), dirs.end());
    return dirs;
}

template <typename Def>
Def* find_by_id(std::vector<Def>& list, std::string_view id) {
    for (Def& d : list) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

// The short-name rule (docs/modding/data.md) over a list of things with an `id`.
template <typename Def>
const Def* resolve(const std::vector<Def>& list, std::string_view name, std::string_view context) {
    if (name.empty()) {
        return nullptr;
    }
    auto exact = [&](std::string_view id) -> const Def* {
        for (const Def& d : list) {
            if (d.id == id) {
                return &d;
            }
        }
        return nullptr;
    };
    if (name.find(':') != std::string_view::npos) {
        return exact(name);
    }
    if (!context.empty()) {
        if (const Def* d = exact(ids::qualify(name, context))) {
            return d;
        }
    }
    if (const Def* d = exact(ids::qualify(name))) {
        return d;
    }
    const Def* found = nullptr;
    for (const Def& d : list) {
        if (ids::local_name(d.id) == name) {
            if (found != nullptr) {
                return nullptr;  // two mods have it: the caller has to say which
            }
            found = &d;
        }
    }
    return found;
}

// The old single files: items.json {"items": [...]} and weapons.json {"weapons": [...]}.
void parse_legacy_items(const Value& doc, std::vector<ItemDef>& items, std::string_view origin) {
    const Value* list = doc.find("items");
    if (!list || !list->is_array()) {
        fail(std::string(origin) + ": no \"items\" array");
    }
    items.clear();
    for (const Value& entry : list->array()) {
        if (!entry.is_object()) {
            fail(where(entry, "an item must be an object"));
        }
        ItemDef item;
        item.number = clamp_to<int>(require_int(entry, "id", entry));
        const std::string enum_name = opt_string(entry, "enum");
        item.id = enum_name.empty() ? ids::qualify("Item" + std::to_string(item.number)) : id_of_enum(enum_name);
        merge_item(entry, item);
        items.push_back(std::move(item));
    }
}

void parse_legacy_weapons(const Value& doc, std::vector<WeaponDef>& weapons, std::vector<ItemDef>& items,
                          bool& legacy_keys, std::string_view origin) {
    const Value* list = doc.find("weapons");
    if (!list || !list->is_array()) {
        fail(std::string(origin) + ": no \"weapons\" array");
    }
    // Pickup counts lived here: a default and one per ammo item enum.
    const std::int64_t fallback = opt_int(doc, "default_pickup_count", 1);
    for (ItemDef& item : items) {
        item.pickup_count = clamp_to<int>(fallback);
    }
    if (const Value* ammo = doc.find("ammo_pickup_counts"); ammo && ammo->is_object()) {
        for (const auto& [name, count] : ammo->members()) {
            if (ItemDef* item = find_by_id(items, id_of_enum(name)); item && count.is_number()) {
                item->pickup_count = clamp_to<int>(count.integer());
            }
        }
    }
    weapons.clear();
    for (const Value& entry : list->array()) {
        if (!entry.is_object()) {
            fail(where(entry, "a weapon must be an object"));
        }
        WeaponDef weapon;
        weapon.number = clamp_to<int>(require_int(entry, "id", entry));
        const std::string enum_name = opt_string(entry, "enum");
        weapon.id = enum_name.empty() ? ids::qualify("Weapon" + std::to_string(weapon.number)) : id_of_enum(enum_name);
        merge_weapon(entry, weapon, legacy_keys);
        // The item it is carried as, by enum, else by number.
        if (const std::string e = opt_string(entry, "item_enum"); !e.empty()) {
            weapon.item = id_of_enum(e);
        } else if (const std::int64_t n = opt_int(entry, "item_id", 0); n > 0) {
            for (const ItemDef& item : items) {
                if (item.number == n) {
                    weapon.item = item.id;
                }
            }
        }
        if (const std::string e = opt_string(entry, "ammo_item_enum"); !e.empty()) {
            weapon.ammo = id_of_enum(e);
        }
        // The old rows carried their packed index; a hand edit that broke it stays visible.
        if (const Value* rows = entry.find("attacks"); rows && rows->is_array()) {
            for (const Value& row : rows->array()) {
                const WeaponAttack::Input input = parse_input(row);
                for (WeaponAttack& a : weapon.attacks) {
                    if (a.input == input) {
                        a.index = clamp_to<std::int32_t>(
                            opt_int(row, "index", weapon.number + static_cast<int>(input) * 10));
                    }
                }
            }
        }
        weapons.push_back(std::move(weapon));
    }
}

}  // namespace

const WeaponAttack* WeaponDef::attack(WeaponAttack::Input input) const {
    for (const WeaponAttack& a : attacks) {
        if (a.input == input) {
            return &a;
        }
    }
    return nullptr;
}

void ItemLibrary::set_default_numbering(Numbering numbering) { default_numbering_storage() = std::move(numbering); }

const Numbering& ItemLibrary::default_numbering() { return default_numbering_storage(); }

void ItemLibrary::number_all(const Numbering& numbering, std::vector<std::string>& warnings) {
    // Base things first, then the mods' in id order: the numbers a mod's things get only depend
    // on which mods are installed, never on load order.
    const auto order = [](const auto& a, const auto& b) {
        const bool ab = ids::is_base(a.id);
        const bool bb = ids::is_base(b.id);
        return ab != bb ? ab : a.id < b.id;
    };
    std::sort(items_.begin(), items_.end(), order);
    std::sort(weapons_.begin(), weapons_.end(), order);
    for (ItemDef& item : items_) {
        if (item.number != 0 || !numbering.item) {
            continue;
        }
        const int n = numbering.item(item.id, item.group);
        if (n > 0 && n != 0xFF) {
            item.number = n;
        } else {
            warnings.push_back(item.id + ": the inventory has no free item number left (docs/modding/data.md); "
                                         "the item is not usable in game");
        }
    }
    for (WeaponDef& weapon : weapons_) {
        // The old single files carry their numbers and packed indexes; everything else is numbered here.
        if (weapon.number >= 0) {
            continue;
        }
        if (numbering.weapon) {
            weapon.number = numbering.weapon(weapon.id, weapon.cls);
        }
        if (weapon.number < 0) {
            if (numbering.weapon) {
                warnings.push_back(weapon.id + ": every attack slot for a mod's weapon is taken ("
                                       "Identifiers); its attacks are not used");
            }
            continue;
        }
        for (WeaponAttack& attack : weapon.attacks) {
            attack.index = weapon.number + static_cast<int>(attack.input) * 10;
        }
    }
    // The rows carry their weapon's rules for the combat code, which sees rows only.
    for (WeaponDef& weapon : weapons_) {
        for (WeaponAttack& attack : weapon.attacks) {
            attack.gas = weapon.gas;
            attack.gun_shot = weapon.is_gun() && attack.input == WeaponAttack::Input::Tap;
        }
    }
    std::sort(items_.begin(), items_.end(), [](const ItemDef& a, const ItemDef& b) { return a.id < b.id; });
    std::sort(weapons_.begin(), weapons_.end(), [](const WeaponDef& a, const WeaponDef& b) { return a.id < b.id; });
}

Result<ItemLibrary> ItemLibrary::parse(std::string_view items_json, std::string_view weapons_json,
                                       std::vector<std::string>& warnings, const Numbering* numbering) try {
    ItemLibrary library;
    const Result<Value> items_doc = json::parse(items_json);
    if (!items_doc) {
        return Result<ItemLibrary>::fail("items.json: " + items_doc.error());
    }
    parse_legacy_items(items_doc.value(), library.items_, "items.json");
    const Result<Value> weapons_doc = json::parse(weapons_json);
    if (!weapons_doc) {
        return Result<ItemLibrary>::fail("weapons.json: " + weapons_doc.error());
    }
    bool legacy_keys = false;
    parse_legacy_weapons(weapons_doc.value(), library.weapons_, library.items_, legacy_keys, "weapons.json");
    if (legacy_keys) {
        warnings.push_back("weapons.json: attack rows use the old unnamed keys, which are ignored; "
                           "the current names are in docs/modding/data.md");
    }
    library.number_all(numbering ? *numbering : default_numbering(), warnings);
    return Result<ItemLibrary>::ok(std::move(library));
} catch (const std::exception& e) {
    return Result<ItemLibrary>::fail(std::string(e.what()));
}

Result<ItemLibrary> ItemLibrary::load(const std::filesystem::path& gamedata_dir,
                                      std::vector<std::string>& warnings) {
    return load(gamedata_dir, std::filesystem::path(), warnings);
}

Result<ItemLibrary> ItemLibrary::load(const std::filesystem::path& gamedata_dir,
                                      const std::filesystem::path& mods_dir, std::vector<std::string>& warnings,
                                      LoadSource* source, const Numbering* numbering) try {
    ItemLibrary library;
    std::error_code ec;
    auto note_legacy = [&](const fs::path& file) {
        warnings.push_back(file.string() + " uses the old single-file layout; it still loads, the current one is "
                                           "items/<Name>.json and weapons/<Name>.json");
        if (source) {
            source->legacy = true;
        }
    };
    bool legacy_keys = false;
    // Where each weapon's short item/ammo names resolve: the namespace of the file that set them.
    std::map<std::string, std::string> weapon_context;

    // The base: the folders, else the old single files.
    const fs::path base_items = gamedata_dir / "items";
    const fs::path base_weapons = gamedata_dir / "weapons";
    const std::vector<fs::path> item_files = thing_files(base_items);
    const std::vector<fs::path> weapon_files = thing_files(base_weapons);
    if (!item_files.empty() || !weapon_files.empty()) {
        for (const fs::path& file : item_files) {
            ItemDef item;
            item.id = ids::qualify(file.stem().string());
            const Value doc = read_json(file);
            note_numeric_keys(doc, file.string(), warnings);
            merge_item(doc, item);
            library.items_.push_back(std::move(item));
        }
        for (const fs::path& file : weapon_files) {
            WeaponDef weapon;
            weapon.id = ids::qualify(file.stem().string());
            const Value doc = read_json(file);
            note_numeric_keys(doc, file.string(), warnings);
            merge_weapon(doc, weapon, legacy_keys);
            weapon_context[weapon.id] = std::string(ids::kBaseNamespace);
            library.weapons_.push_back(std::move(weapon));
        }
        for (const char* old : {"items.json", "weapons.json"}) {
            if (fs::is_regular_file(base_items / old, ec)) {
                warnings.push_back((base_items / old).string() + " is the old layout next to the new one and is "
                                                                 "ignored; delete it");
            }
        }
    } else if (fs::is_regular_file(base_items / "items.json", ec) && fs::is_regular_file(base_items / "weapons.json", ec)) {
        note_legacy(base_items / "items.json");
        parse_legacy_items(read_json(base_items / "items.json"), library.items_, (base_items / "items.json").string());
        parse_legacy_weapons(read_json(base_items / "weapons.json"), library.weapons_, library.items_, legacy_keys,
                             (base_items / "weapons.json").string());
    } else {
        return Result<ItemLibrary>::fail("no item data in " + base_items.string());
    }

    // The mods, ascending priority: the last one has the last word.
    if (!mods_dir.empty() && fs::is_directory(mods_dir, ec)) {
        std::vector<std::string> mod_warnings;
        const std::vector<mods::ModInfo> found = mods::discover(mods_dir, mod_warnings);
        for (const std::string& w : mod_warnings) {
            warnings.push_back("mods: " + w);
        }
        for (const mods::ModInfo& mod : found) {
            const std::string ns = mod.id;
            const fs::path mod_items = mod.root / "items";
            const fs::path mod_weapons = mod.root / "weapons";
            bool changed = false;
            if (fs::is_regular_file(mod_items / "items.json", ec)) {
                note_legacy(mod_items / "items.json");
                parse_legacy_items(read_json(mod_items / "items.json"), library.items_,
                                   (mod_items / "items.json").string());
                changed = true;
            }
            if (fs::is_regular_file(mod_items / "weapons.json", ec)) {
                note_legacy(mod_items / "weapons.json");
                parse_legacy_weapons(read_json(mod_items / "weapons.json"), library.weapons_, library.items_,
                                     legacy_keys, (mod_items / "weapons.json").string());
                weapon_context.clear();
                changed = true;
            }
            // items/<Name>.json: the base item of that name, else a new <ns>:<Name>.
            // items/<namespace>/<Name>.json: that namespace's item, which must exist.
            auto apply = [&](auto& list, const fs::path& dir, auto&& merge, bool weapons) {
                auto one = [&](const fs::path& file, const std::string& target, bool may_add) {
                    const Value doc = read_json(file);
                    note_numeric_keys(doc, file.string(), warnings);
                    auto* def = find_by_id(list, target);
                    if (def == nullptr) {
                        if (!may_add) {
                            warnings.push_back(file.string() + ": no " + target + " to change (file ignored)");
                            return;
                        }
                        list.emplace_back();
                        def = &list.back();
                        def->id = target;
                    }
                    merge(doc, *def);
                    if (weapons) {
                        weapon_context[target] = ns;
                    }
                    changed = true;
                };
                for (const fs::path& file : thing_files(dir)) {
                    const std::string name = file.stem().string();
                    if (!ids::valid_local_name(name)) {
                        warnings.push_back(file.string() + ": \"" + name + "\" is not a valid name (file ignored)");
                        continue;
                    }
                    const std::string base = ids::qualify(name);
                    one(file, find_by_id(list, base) != nullptr ? base : ids::qualify(name, ns), true);
                }
                for (const fs::path& sub : namespace_dirs(dir)) {
                    for (const fs::path& file : thing_files(sub)) {
                        one(file, ids::qualify(file.stem().string(), sub.filename().string()), false);
                    }
                }
            };
            apply(library.items_, mod_items, [](const Value& doc, ItemDef& item) { merge_item(doc, item); }, false);
            apply(library.weapons_, mod_weapons,
                  [&](const Value& doc, WeaponDef& weapon) { merge_weapon(doc, weapon, legacy_keys); }, true);
            if (changed) {
                if (source) {
                    source->mod_id = mod.id;
                    source->mod_gameplay = mod.gameplay;
                }
                if (!mod.gameplay) {
                    warnings.push_back("mod " + mod.id + " changes item data (game data) but does not declare "
                                                         "\"gameplay\": true in mod.json");
                }
            }
        }
    }
    if (legacy_keys) {
        warnings.push_back("weapons: attack rows use the old unnamed keys, which are ignored; the current "
                           "names are in docs/modding/data.md");
    }

    for (ItemDef& item : library.items_) {
        if (item.group.empty()) {
            warnings.push_back(item.id + ": no \"group\"; puzzle assumed");
            item.group = "puzzle";
        } else if (!known_group(item.group)) {
            warnings.push_back(item.id + ": unknown group \"" + item.group + "\"");
        }
    }
    // Item and ammo names of the weapons become full ids, in the namespace of the file that named them.
    for (WeaponDef& weapon : library.weapons_) {
        const auto ctx = weapon_context.find(weapon.id);
        const std::string_view context = ctx != weapon_context.end() ? std::string_view(ctx->second) : std::string_view();
        for (std::string* ref : {&weapon.item, &weapon.ammo}) {
            if (ref->empty()) {
                continue;
            }
            if (const ItemDef* item = resolve(library.items_, *ref, context)) {
                *ref = item->id;
            } else {
                *ref = ids::qualify(*ref, context.empty() ? ids::kBaseNamespace : context);
            }
        }
    }
    library.number_all(numbering ? *numbering : default_numbering(), warnings);
    return Result<ItemLibrary>::ok(std::move(library));
} catch (const std::exception& e) {
    return Result<ItemLibrary>::fail(std::string(e.what()));
}

const ItemDef* ItemLibrary::find_item(int number) const {
    if (number == 0) {
        return nullptr;
    }
    for (const ItemDef& item : items_) {
        if (item.number == number) {
            return &item;
        }
    }
    return nullptr;
}

const ItemDef* ItemLibrary::find_item(std::string_view name, std::string_view context) const {
    return resolve(items_, name, context);
}

const WeaponDef* ItemLibrary::find_weapon(int number) const {
    if (number < 0) {
        return nullptr;
    }
    for (const WeaponDef& weapon : weapons_) {
        if (weapon.number == number) {
            return &weapon;
        }
    }
    return nullptr;
}

const WeaponDef* ItemLibrary::find_weapon(std::string_view name, std::string_view context) const {
    return resolve(weapons_, name, context);
}

const WeaponDef* ItemLibrary::weapon_of_item(int item_number) const {
    // Number 0 is every item the numbering left out.
    const ItemDef* item = item_number != 0 ? find_item(item_number) : nullptr;
    if (item == nullptr) {
        return nullptr;
    }
    for (const WeaponDef& weapon : weapons_) {
        if (!weapon.item.empty() && find_item(weapon.item) == item) {
            return &weapon;
        }
    }
    return nullptr;
}

int ItemLibrary::held_number(int item_number) const {
    const ItemDef* item = item_number != 0 ? find_item(item_number) : nullptr;
    if (item == nullptr) {
        return -1;
    }
    if (const WeaponDef* weapon = weapon_of_item(item_number); weapon != nullptr) {
        return weapon->number;
    }
    if (!ids::is_base(item->id) || (item->group != "melee" && item->group != "guns")) {
        return -1;
    }
    return static_cast<std::int8_t>(static_cast<std::uint8_t>(item_number + kBaseHeldOffset));
}

int ItemLibrary::held_item(int number) const {
    if (const WeaponDef* weapon = find_weapon(number); weapon != nullptr) {
        const ItemDef* item = weapon->item.empty() ? nullptr : find_item(weapon->item);
        return item != nullptr && item->number != 0 ? item->number : -1;
    }
    const int item = static_cast<std::uint8_t>(number + kBaseHeldOffset);
    return held_number(item) == number ? item : -1;
}

bool ItemLibrary::held_as_gun(int item_number) const {
    if (const WeaponDef* weapon = weapon_of_item(item_number); weapon != nullptr) {
        return weapon->is_gun();
    }
    const ItemDef* item = item_number != 0 ? find_item(item_number) : nullptr;
    return item != nullptr && ids::is_base(item->id) && item->group == "guns";
}

int ItemLibrary::ammo_item_number(int item_number) const {
    const WeaponDef* weapon = weapon_of_item(item_number);
    const ItemDef* ammo = weapon != nullptr ? ammo_item_of(*weapon) : nullptr;
    return ammo != nullptr && ammo->number != 0 ? ammo->number : -1;
}

std::array<std::uint8_t, 2> ItemLibrary::held_bone(int weapon_number) const {
    const WeaponDef* weapon = find_weapon(weapon_number);
    return weapon != nullptr && weapon->held_bone ? *weapon->held_bone : kDefaultHeldBone;
}

int ItemLibrary::clip_capacity(int weapon_number) const {
    const WeaponDef* weapon = find_weapon(weapon_number);
    if (weapon == nullptr || !weapon->is_gun()) {
        return 0;
    }
    return weapon->clip ? *weapon->clip : kUnlimitedClip;
}

int ItemLibrary::ammo_pickup_count(int item_number) const {
    const ItemDef* item = find_item(item_number);
    return item != nullptr ? item->pickup_count : 1;
}

const ItemDef* ItemLibrary::ammo_item_of(const WeaponDef& weapon) const {
    return weapon.ammo.empty() ? nullptr : find_item(weapon.ammo);
}

std::vector<std::string> ItemLibrary::check_consistency() const {
    std::vector<std::string> problems;
    std::set<std::string> ids;
    std::set<int> numbers;
    for (const ItemDef& item : items_) {
        if (!ids.insert(item.id).second) {
            problems.push_back("duplicate item " + item.id);
        }
        if (!ids::valid_id(item.id)) {
            problems.push_back("item " + item.id + ": not a valid identifier");
        }
        if (item.number != 0 && !numbers.insert(item.number).second) {
            problems.push_back("item " + item.id + ": number " + std::to_string(item.number) + " given twice");
        }
    }
    ids.clear();
    numbers.clear();
    for (const WeaponDef& weapon : weapons_) {
        if (!ids.insert(weapon.id).second) {
            problems.push_back("duplicate weapon " + weapon.id);
        }
        if (weapon.number >= 0 && !numbers.insert(weapon.number).second) {
            problems.push_back("weapon " + weapon.id + ": number " + std::to_string(weapon.number) + " given twice");
        }
        if (!weapon.item.empty() && !find_item(weapon.item)) {
            problems.push_back(weapon.id + " carries unknown item " + weapon.item);
        }
        if (!weapon.ammo.empty() && !find_item(weapon.ammo)) {
            problems.push_back(weapon.id + " uses unknown ammo " + weapon.ammo);
        }
        if (const WeaponDef* like = weapon.like.empty() ? nullptr : find_weapon(weapon.like, weapon.id);
            !weapon.like.empty() && (like == nullptr || !ids::is_base(like->id))) {
            problems.push_back(weapon.id + " is \"like\" " + weapon.like + ", which is no weapon of the base game");
        }
        for (const WeaponAttack& attack : weapon.attacks) {
            const int packed = weapon.number + static_cast<int>(attack.input) * 10;
            if (weapon.number >= 0 && attack.index != packed) {
                problems.push_back(weapon.id + " attack index " + std::to_string(attack.index) +
                                   " does not match input " + std::to_string(packed));
            }
        }
    }
    return problems;
}

std::int16_t angle_q12(double degrees) {
    const float turns = static_cast<float>(degrees) / 360.0f;
    return static_cast<std::int16_t>(static_cast<std::int32_t>(turns * 4096.0f));
}

}  // namespace sh::items
