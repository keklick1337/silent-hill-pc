// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Harry's base clip table (gamedata/chara/Harry/base_clips.json): the clip of each status of HB_BASE.ANM, read at
// boot. Its readers index HARRY_BASE_ANIM_INFOS by clip status; the equipped weapon's rig fills the entries from 56
// and the map's clips the ones after them. Animation names are Harry's ("anims" of gamedata/chara/Harry/character.json).

#include "engine/inventory/game/harry_clip_table.h"

#include <cstdint>
#include <map>
#include <string>

#include "engine/log/sh_log.h"
#include "engine/mods/data_merge.h"
#include "engine/player/player_records.h"
#include "formats/common/json.h"

// Zero until the boot read: the entries past the base ones stay zero until the rig and the map's clips fill them.
sh::game::ClipTableEntry HARRY_BASE_ANIM_INFOS[sh::inventory::game::kHarryClipSlots];

extern "C" {
void Anim_BlendLinear(void* model, void* anm_header, void* bones, void* clip);
void Anim_PlaybackOnce(void* model, void* anm_header, void* bones, void* clip);
void Anim_PlaybackLoop(void* model, void* anm_header, void* bones, void* clip);
std::int32_t Player_VariableAnimDurationGet(const void* model);
}

namespace sh::inventory::game {

namespace {

using Playback = void (*)(void*, void*, void*, sh::game::ClipTableEntry*);
constexpr std::uint8_t kNoLink = 0xFF;

Result<json::Value> read_json(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                              const std::filesystem::path& relative, std::string& source)
{
    const Result<std::string> text = mods::read_data_text(gamedata_dir, mods_dir, relative, &source);
    if (!text)
        return Result<json::Value>::fail(text.error());
    return json::parse(text.value());
}

}  // namespace

void load_harry_clip_table(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir)
{
    std::string chara_source, clips_source;
    const Result<json::Value> chara = read_json(gamedata_dir, mods_dir, "chara/Harry/character.json", chara_source);
    const Result<json::Value> table = read_json(gamedata_dir, mods_dir, "chara/Harry/base_clips.json", clips_source);
    const json::Value* anims = chara ? chara.value().find("anims") : nullptr;
    const json::Value* clips = table ? table.value().find("clips") : nullptr;
    if (anims == nullptr || clips == nullptr || !clips->is_array())
    {
        SH_ERR("[ITEMS] Harry's base clips: %s - the table stays empty",
               !table ? table.error().c_str() : !chara ? chara.error().c_str() : "no \"anims\" or \"clips\"");
        return;
    }
    std::map<std::string, int, std::less<>> anim_index;
    for (const auto& [name, value] : anims->members())
    {
        if (value.is_integer())
            anim_index[name] = static_cast<int>(value.integer());
    }
    const auto status_of = [&](const json::Value& clip) -> std::uint8_t {
        const json::Value* anim = clip.find("anim");
        const json::Value* active = clip.find("active");
        const auto it = anim != nullptr && anim->is_string() ? anim_index.find(anim->string()) : anim_index.end();
        if (it == anim_index.end())
        {
            SH_WARN("[ITEMS] %s: unknown Harry anim \"%s\"", clips_source.c_str(),
                    anim != nullptr && anim->is_string() ? anim->string().c_str() : "?");
            return 0;
        }
        return static_cast<std::uint8_t>((it->second << 1) | (active != nullptr && active->boolean() ? 1 : 0));
    };
    const auto integer = [](const json::Value& clip, std::string_view key) -> std::int64_t {
        const json::Value* v = clip.find(key);
        return v != nullptr && v->is_integer() ? v->integer() : 0;
    };

    int count = 0;
    for (const json::Value& clip : clips->array())
    {
        if (count >= kHarryClipSlots)
            break;
        sh::game::ClipTableEntry& out = HARRY_BASE_ANIM_INFOS[count++];
        const json::Value* kind = clip.find("kind");
        const std::string k = kind != nullptr && kind->is_string() ? kind->string() : "";
        out.playback = k == "blend_linear"    ? reinterpret_cast<Playback>(&Anim_BlendLinear)
                       : k == "playback_loop" ? reinterpret_cast<Playback>(&Anim_PlaybackLoop)
                                              : reinterpret_cast<Playback>(&Anim_PlaybackOnce);
        out.status = status_of(clip);
        const json::Value* variable = clip.find("variable");
        out.has_variable_duration = variable != nullptr && variable->boolean() ? 1 : 0;
        const json::Value* next = clip.find("next");
        out.link_status = next != nullptr && next->is_object() ? status_of(*next) : kNoLink;
        if (out.has_variable_duration)
            out.duration.variable = reinterpret_cast<void*>(&Player_VariableAnimDurationGet);
        else
            out.duration.constant = static_cast<std::int32_t>(integer(clip, "speed_q12"));
        out.start_keyframe = static_cast<std::int16_t>(integer(clip, "start_kf"));
        out.end_keyframe = static_cast<std::int16_t>(integer(clip, "end_kf"));
    }
    SH_LOG("[ITEMS] %s: %d of Harry's base clips", clips_source.c_str(), count);
}

}  // namespace sh::inventory::game
