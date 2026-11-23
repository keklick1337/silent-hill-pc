// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gconfig/sound_tables.h"

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace sh::gconfig {

namespace {

using json::Value;

[[noreturn]] void fail(const Value& at, const std::string& what) {
    throw std::runtime_error("sound_tables.json line " + std::to_string(at.line()) + ": " + what);
}

const Value& member(const Value& obj, const char* key) {
    const Value* v = obj.is_object() ? obj.find(key) : nullptr;
    if (!v) {
        fail(obj, std::string("missing \"") + key + "\"");
    }
    return *v;
}

std::int64_t ranged(const Value& obj, const char* key, std::int64_t lo, std::int64_t hi) {
    const Value& v = member(obj, key);
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(v, std::string("\"") + key + "\" must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
    }
    return v.integer();
}

const std::vector<Value>& rows(const Value& doc, const char* key, std::size_t count) {
    const Value& v = member(doc, key);
    if (!v.is_array() || v.array().size() != count) {
        fail(v, std::string("\"") + key + "\" needs exactly " + std::to_string(count) + " rows");
    }
    return v.array();
}

std::uint8_t bank_number(const Value& obj) {
    const Value& v = member(obj, "bank");
    if (v.is_string()) {
        const std::string& s = v.string();
        if (s == "BaseAudio" || s == "MusicKey") return 0;
        if (s == "Weapon") return 1;
        if (s == "Ambient" || s == "SpecialScreen") return 2;
        if (s == "MusicBank") return 3;
    }
    fail(v, "unknown bank");
}

}  // namespace

Result<SoundTables> SoundTables::parse(std::string_view text) try {
    const Result<Value> parsed = json::parse(text);
    if (!parsed) {
        return Result<SoundTables>::fail("sound_tables.json: " + parsed.error());
    }
    const Value& doc = parsed.value();
    SoundTables t;
    const auto& depths = rows(doc, "reverb_depths", kReverbDepthCount);
    for (std::size_t i = 0; i < kReverbDepthCount; i++) {
        if (!depths[i].is_integer() || depths[i].integer() < 0 || depths[i].integer() > 255) fail(depths[i], "bad reverb depth");
        t.reverb_depths[i] = static_cast<std::uint8_t>(depths[i].integer());
    }
    const auto& layers = rows(doc, "layer_channels", kLayerChannelRows);
    for (std::size_t r = 0; r < kLayerChannelRows; r++) {
        if (!layers[r].is_array() || layers[r].array().size() != kLayerChannelCols) fail(layers[r], "a layer row needs 16 values");
        for (std::size_t c = 0; c < kLayerChannelCols; c++) {
            const Value& v = layers[r].array()[c];
            if (!v.is_integer() || v.integer() < 0 || v.integer() > 255) fail(v, "bad channel value");
            t.layer_channels[r][c] = static_cast<std::uint8_t>(v.integer());
        }
    }
    for (const Value& f : rows(doc, "files", kSoundFileCount)) {
        t.files.push_back({bank_number(f), static_cast<std::uint16_t>(ranged(f, "vag_data_offset", 0, 65535)),
                           static_cast<std::uint32_t>(ranged(f, "size", 0, 0xFFFFFFFFll)),
                           static_cast<std::int32_t>(ranged(f, "sector", -0x80000000ll, 0x7FFFFFFFll))});
    }
    for (const Value& e : rows(doc, "effects", kSoundEffectCount)) {
        t.effects.push_back({static_cast<std::uint8_t>(ranged(e, "audio_index", 0, 255)), bank_number(e),
                             static_cast<std::uint8_t>(ranged(e, "program", 0, 127)),
                             static_cast<std::uint8_t>(ranged(e, "note", 0, 255)),
                             static_cast<std::int8_t>(ranged(e, "volume", -128, 127))});
    }
    for (const Value& x : rows(doc, "xa_lines", kXaLineCount)) {
        t.xa_lines.push_back({static_cast<std::uint8_t>(ranged(x, "xa_file", 0, 255)),
                              static_cast<std::uint32_t>(ranged(x, "sector", 0, 0xFFFFFF)),
                              static_cast<std::uint8_t>(ranged(x, "group_index", 0, 255)),
                              static_cast<std::uint32_t>(ranged(x, "length", 0, 0xFFFFFF)),
                              static_cast<std::uint8_t>(ranged(x, "group", 0, 255))});
    }
    return Result<SoundTables>::ok(std::move(t));
} catch (const std::exception& e) {
    return Result<SoundTables>::fail(e.what());
}

Result<SoundTables> SoundTables::load(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                      std::vector<std::string>& warnings, std::string* mod_id) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("config") / "sound_tables.json";
    // Base + every active mod's patch, in load order.
    Result<mods::MergedFile> read = mods::read_data_file(gamedata_dir, mods_dir, relative, warnings);
    if (!read) {
        return Result<SoundTables>::fail(read.error());
    }
    if (mod_id) *mod_id = read.value().last_mod();
    return parse(read.value().text);
}

SoundTables SoundTables::empty() {
    SoundTables t;
    t.files.resize(kSoundFileCount);
    t.effects.resize(kSoundEffectCount);
    t.xa_lines.resize(kXaLineCount);
    return t;
}

ActiveSoundTables& active_sound_tables() {
    static ActiveSoundTables active;
    return active;
}

bool load_active_sound_tables(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                              std::vector<std::string>& messages, std::string* mod_id) {
    Result<SoundTables> loaded = SoundTables::load(gamedata_dir, mods_dir, messages, mod_id);
    if (!loaded) {
        messages.push_back(loaded.error());
        return false;
    }
    ActiveSoundTables& active = active_sound_tables();
    active.tables = std::move(loaded.value());
    return true;
}

}  // namespace sh::gconfig
