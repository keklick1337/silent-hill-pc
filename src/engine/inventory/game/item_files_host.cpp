// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The item files of the running game: the catalog of gamedata/items/ and
// gamedata/weapons/ loaded at boot, the reads of the item model files and texture sheets the item screens and the
// pickup show, and Harry's weapon rig ("rig" of gamedata/weapons/<Name>.json) as the equip block, clips and keyframes
// the player's host installs. The models themselves are drawn from the import (the item stage); the model reads stay
// in the file queue because their place in it paces the loads that follow.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/app/loader/game_files.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/game_state/session.h"
#include "engine/inventory/game/attack_table.h"
#include "engine/inventory/game/harry_clip_table.h"
#include "engine/inventory/game/item_stage.h"
#include "engine/inventory/item_catalog.h"
#include "engine/inventory/item_models.h"
#include "engine/log/sh_log.h"
#include "engine/player/player_records.h"
#include "engine/player/player_state.h"
#include "engine/scripting/script_buffers.h"
#include "engine/app/loader/picture_places.h"
#include "engine/loader/queue/disc_files.h"

extern "C" {
// Where the item texture sheets go in texture memory (the screen data's image records).
// The clip playbacks of the model runtime.
void Anim_BlendLinear(void* model, void* anm_header, void* bones, void* clip);
void Anim_PlaybackOnce(void* model, void* anm_header, void* bones, void* clip);
void Anim_PlaybackLoop(void* model, void* anm_header, void* bones, void* clip);
}

namespace {

namespace inv = sh::inventory;

constexpr int kHarry = 1;  // characters.json id
constexpr int kTextureBuffer = 1;
constexpr int kCloseUpBuffer = 5;
constexpr int kPackBuffer = 8;
constexpr std::uint8_t kNoLink = 0xFF;

std::int32_t file_number(const std::string& name)
{
    const std::int32_t file = name.empty() ? -1 : sh::loader::disc_files().find(name.c_str());
    // A mod's own model (a glb below convert/models/) is no game file: the scene reads it itself.
    const bool own_model = name.size() > 4 && name.compare(name.size() - 4, 4, ".glb") == 0;
    if (file < 0 && !name.empty() && !own_model)
        SH_ERR("[ITEMS] no game file %s", name.c_str());
    return file;
}

void texture_reads(const std::vector<inv::TextureRead>& reads)
{
    for (const inv::TextureRead& read : reads)
    {
        const std::int32_t file = file_number(read.file);
        if (file < 0)
            continue;
        sh::loader::PicturePlaces& pics = sh::loader::picture_places();
        const sh::loader::ImagePlace* place = read.sheet == inv::TextureSheet::Common  ? &pics.first_aid_kit_textures
                                              : read.sheet == inv::TextureSheet::Model ? &pics.item_model_textures
                                                                                       : &pics.key_item_textures;
        sh::loader::queue_read_image(file, sh::scripting::file_buffer(kTextureBuffer), place);
    }
}

std::uint8_t& resident_sheets() { return sh::game::session().system.inventory_load_flags; }

// A weapon rig's clip status by Harry's animation name, packed (index << 1) | active as the game's status bytes.
std::uint8_t rig_status(const sh::items::WeaponRigStatus& status)
{
    const int anim = Pc_CharaAnim(kHarry, status.anim.c_str(), -1);
    if (anim < 0)
    {
        SH_WARN("[ITEMS] weapon rig: Harry anim \"%s\" not found", status.anim.c_str());
        return 0;
    }
    return static_cast<std::uint8_t>((anim << 1) | (status.active ? 1 : 0));
}

std::int16_t rig_sound(const std::string& name)
{
    if (name.empty())
        return 0;  // no sound
    const int id = Pc_SoundIdFind(name.c_str());
    if (id < 0)
    {
        SH_WARN("[ITEMS] weapon rig: sound \"%s\" not found", name.c_str());
        return 0;
    }
    return static_cast<std::int16_t>(id);
}

using Playback = void (*)(void*, void*, void*, sh::game::ClipTableEntry*);

Playback playback_of(const std::string& kind)
{
    if (kind == "blend_linear")
        return reinterpret_cast<Playback>(&Anim_BlendLinear);
    if (kind == "playback_loop")
        return reinterpret_cast<Playback>(&Anim_PlaybackLoop);
    if (kind != "playback_once")
        SH_WARN("[ITEMS] weapon rig: unknown clip kind \"%s\"", kind.c_str());
    return reinterpret_cast<Playback>(&Anim_PlaybackOnce);
}

}  // namespace

extern "C" {

void GameFs_UniqueItemModelLoad(int item)
{
    // The stage draws the model from the import; the read keeps the queue's timing.
    inv::item_stage().close_up_model = inv::item_catalog().close_up_model(item);
    const std::int32_t file = file_number(inv::item_stage().close_up_model);
    sh::loader::queue_read(file, sh::scripting::file_buffer(kCloseUpBuffer));
}

void GameFs_Tim00TIMLoad(void)
{
    std::uint8_t resident = resident_sheets();
    const std::optional<inv::TextureRead> read = inv::common_texture_read(inv::item_catalog().model_files(), resident);
    if (!read)
        return;
    texture_reads({*read});
    resident_sheets() = resident;
}

void GameFs_MapItemsModelLoad(std::uint32_t map)
{
    std::uint8_t resident = resident_sheets();
    texture_reads(inv::map_texture_reads(inv::item_catalog().model_files(),
                                         inv::map_name_of(map > 0x7FFFFFFF ? -1 : static_cast<std::int32_t>(map)),
                                         resident));
    resident_sheets() = resident;
}

// The map's item pack goes to its file buffer.
void GameFs_MapItemsTextureLoad(std::int32_t map)
{
    const std::string& models = inv::item_catalog().map_pack_models(inv::map_name_of(map));
    // The stage draws the pack from the import; the read keeps the queue's timing.
    inv::item_stage().pack_model = models;
    const std::int32_t file = models.empty() ? -1 : file_number(models);
    if (file >= 0)
        sh::loader::queue_read(file, sh::scripting::file_buffer(kPackBuffer));
}

// gamedata/items/ and gamedata/weapons/, the attack table (gamedata/config/attacks.json) and Harry's base clips
// (gamedata/chara/Harry/base_clips.json), once at boot. The
// weapons' attack rows go over the table's rows of the same index.
static void inventory_data_load(const char* gamedata_dir, const char* mods_dir, bool again)
{
    inv::ItemCatalog& catalog = inv::item_catalog();
    if ((catalog.loaded() && !again) || gamedata_dir == nullptr)
        return;
    std::vector<inv::CatalogMessage> log;
    if (again)
        catalog.reload(gamedata_dir, mods_dir ? mods_dir : "", log);
    else
        catalog.load(gamedata_dir, mods_dir ? mods_dir : "", log);
    for (const inv::CatalogMessage& message : log)
    {
        if (message.warning)
            SH_WARN("[ITEMS] %s", message.text.c_str());
        else
            SH_LOG("[ITEMS] %s", message.text.c_str());
    }
    inv::game::load_attack_table(gamedata_dir, mods_dir ? mods_dir : "");
    inv::game::load_harry_clip_table(gamedata_dir, mods_dir ? mods_dir : "");
    if (!catalog.loaded())
        return;

    int patched = 0;
    for (const sh::items::WeaponDef& weapon : catalog.library()->weapons())
    {
        if (weapon.number < 0)
            continue;
        for (const sh::items::WeaponAttack& attack : weapon.attacks)
        {
            if (inv::game::set_attack_row(attack))
                patched++;
        }
    }
    SH_LOG("[ITEMS] %d attack rows", patched);
}

void Inventory_GamedataInit(const char* gamedata_dir, const char* mods_dir)
{
    inventory_data_load(gamedata_dir, mods_dir, false);
}

// `mods reload` (between frames, no play state alive): the library, the model files, the attack table and Harry's
// base clips read again.
void Inventory_GamedataReload(const char* gamedata_dir, const char* mods_dir)
{
    inventory_data_load(gamedata_dir, mods_dir, true);
}

std::int32_t Inventory_WeaponRigInfo(std::int32_t attack, sh::game::EquippedWeapon* info, std::int32_t* hand_reach)
{
    const sh::items::WeaponRig* rig = inv::item_catalog().weapon_rig(attack);
    if (rig == nullptr)
        return false;
    info->attack_sound = rig_sound(rig->attack_sound);
    info->reload_sound = rig_sound(rig->reload_sound);
    info->empty_sound = rig_sound(rig->out_of_ammo_sound);
    info->clip_stop_aiming = rig_status(rig->stop_aiming);
    info->clip_attack = rig_status(rig->attack);
    info->clip_attack_hold = rig_status(rig->attack_hold);
    info->reload_sound_keyframe = static_cast<std::uint8_t>(rig->reload_sound_offset);
    info->clip_index = static_cast<std::uint8_t>((rig->attack_keyframe_slot << 4) | rig->aim_stop_keyframe_slot);
    *hand_reach = rig->hand_collision_q12;
    return true;
}

std::int32_t Inventory_WeaponRigClips(std::int32_t attack, sh::game::ClipTableEntry* clips, std::int32_t slots)
{
    const sh::items::WeaponRig* rig = inv::item_catalog().weapon_rig(attack);
    if (rig == nullptr)
        return 0;
    std::int32_t count = 0;
    for (const sh::items::WeaponRigClip& clip : rig->clips)
    {
        if (count >= slots)
            break;
        sh::game::ClipTableEntry& out = clips[count++];
        out.playback = playback_of(clip.kind);
        out.status = rig_status(clip.status);
        out.has_variable_duration = clip.variable;
        out.link_status = clip.next.has_value() ? rig_status(*clip.next) : kNoLink;
        out.duration.constant = clip.speed_q12;
        out.start_keyframe = static_cast<std::int16_t>(clip.start_kf);
        out.end_keyframe = static_cast<std::int16_t>(clip.end_kf);
    }
    return count;
}

std::int32_t Inventory_WeaponRigKeyframes(std::int32_t attack, sh::game::WeaponKeyframes* keyframes, std::int32_t slots)
{
    const sh::items::WeaponRig* rig = inv::item_catalog().weapon_rig(attack);
    if (rig == nullptr)
        return 0;
    std::int32_t count = 0;
    for (const sh::items::WeaponRigKeyframe& keyframe : rig->keyframes)
    {
        if (count >= slots)
            break;
        sh::game::WeaponKeyframes& out = keyframes[count++];
        out.status_a = rig_status(keyframe.status_a);
        out.status_b = rig_status(keyframe.status_b);
        out.value = static_cast<std::uint8_t>(keyframe.value);
        out.start = static_cast<std::int16_t>(keyframe.start_kf);
        out.ready = static_cast<std::int16_t>(keyframe.end_kf);
    }
    return count;
}

const char* Inventory_WeaponRigAnimFile(std::int32_t attack)
{
    const sh::items::WeaponRig* rig = inv::item_catalog().weapon_rig(attack);
    return rig != nullptr ? rig->anim_file.c_str() : nullptr;
}

}  // extern "C"
