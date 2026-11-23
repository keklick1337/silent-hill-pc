// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/audio/sound_rules.h"

#include <algorithm>
#include <string>

#include "formats/common/json.h"

namespace sh::audio {
namespace {

using R = Result<SoundRules>;

struct Reader {
    std::string error;

    bool fail(std::string message) {
        if (error.empty()) {
            error = std::move(message);
        }
        return false;
    }

    const json::Value* object(const json::Value& parent, const char* key) {
        const json::Value* v = parent.find(key);
        if (v == nullptr || !v->is_object()) {
            fail(std::string("\"") + key + "\" must be an object");
            return nullptr;
        }
        return v;
    }

    const json::Value* array(const json::Value& parent, const char* key) {
        const json::Value* v = parent.find(key);
        if (v == nullptr || !v->is_array()) {
            fail(std::string("\"") + key + "\" must be an array");
            return nullptr;
        }
        return v;
    }

    template <typename T>
    T integer(const json::Value& parent, const char* key, std::int64_t lo, std::int64_t hi) {
        const json::Value* v = parent.find(key);
        if (v == nullptr || !v->is_integer() || v->integer() < lo || v->integer() > hi) {
            fail(std::string("\"") + key + "\" must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
            return T{};
        }
        return static_cast<T>(v->integer());
    }

    std::string string(const json::Value& parent, const char* key) {
        const json::Value* v = parent.find(key);
        if (v == nullptr || !v->is_string() || v->string().empty()) {
            fail(std::string("\"") + key + "\" must be a non-empty string");
            return {};
        }
        return v->string();
    }

    std::vector<std::string> names(const json::Value& parent, const char* key) {
        std::vector<std::string> out;
        if (const json::Value* a = array(parent, key)) {
            for (const json::Value& e : a->array()) {
                if (!e.is_string() || e.string().empty()) {
                    fail(std::string("\"") + key + "\" holds only names");
                    break;
                }
                out.push_back(e.string());
            }
        }
        return out;
    }
};

bool is_comment(const std::string& key) { return !key.empty() && key[0] == '_'; }

}  // namespace

Result<SoundRules> SoundRules::parse(std::string_view json_text) {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return R::fail(doc.error());
    }
    const json::Value& root = doc.value();
    Reader r;
    SoundRules s;

    if (const json::Value* c = r.object(root, "commands")) {
        s.commands.first_bank = r.integer<std::uint8_t>(*c, "first_bank", 0, 255);
        s.commands.first_ambient = r.integer<std::uint8_t>(*c, "first_ambient", 0, 255);
        s.commands.last_ambient = r.integer<std::uint8_t>(*c, "last_ambient", 0, 255);
        s.commands.song_file_offset = r.integer<std::uint8_t>(*c, "song_file_offset", 0, 255);
        s.commands.song_bank_offset = r.integer<std::uint8_t>(*c, "song_bank_offset", 0, 255);
        s.commands.fade_out_ambient = r.integer<std::uint16_t>(*c, "fade_out_ambient", 0, 65535);
        s.commands.fade_out_music = r.integer<std::uint16_t>(*c, "fade_out_music", 0, 65535);
        s.commands.fade_out_music_fast = r.integer<std::uint16_t>(*c, "fade_out_music_fast", 0, 65535);
    }
    if (const json::Value* m = r.object(root, "music")) {
        s.music_slot = r.integer<std::uint8_t>(*m, "slot", 0, 3);
        s.music_volume = r.integer<std::uint8_t>(*m, "volume", 0, 127);
        s.music_ducked_volume = r.integer<std::uint8_t>(*m, "ducked_volume", 0, 127);
    }
    s.default_reverb = r.integer<std::uint8_t>(root, "default_reverb", 0, 127);
    if (const json::Value* v = r.object(root, "voice_lines")) {
        s.default_line_volume = r.integer<std::uint8_t>(*v, "default_volume", 0, 127);
        if (const json::Value* a = r.array(*v, "volumes")) {
            for (const json::Value& e : a->array()) {
                if (!e.is_object()) {
                    r.fail("\"volumes\" holds only objects");
                    break;
                }
                LineVolume l;
                l.line = r.integer<std::uint16_t>(e, "line", 0, 65535);
                l.volume = r.integer<std::uint8_t>(e, "volume", 0, 127);
                const json::Value* scaled = e.find("scaled");
                if (scaled == nullptr || !scaled->is_bool()) {
                    r.fail("\"scaled\" must be true or false");
                }
                l.scaled = scaled != nullptr && scaled->is_bool() && scaled->boolean();
                s.line_volumes.push_back(l);
            }
        }
    }
    if (const json::Value* f = r.object(root, "fixed_voices")) {
        for (const json::Value::Member& m : f->members()) {
            if (is_comment(m.first)) {
                continue;
            }
            if (!m.second.is_object()) {
                r.fail("\"fixed_voices\" maps sound names to objects");
                break;
            }
            FixedVoice fv;
            fv.sound = m.first;
            fv.voice = r.integer<std::uint8_t>(m.second, "voice", 0, 23);
            fv.fine = r.integer<std::uint8_t>(m.second, "fine", 0, 255);
            s.fixed_voices.push_back(std::move(fv));
        }
    }
    if (const json::Value* a = r.array(root, "ambient_overrides")) {
        for (const json::Value& e : a->array()) {
            if (!e.is_object()) {
                r.fail("\"ambient_overrides\" holds only objects");
                break;
            }
            AmbientOverride o;
            o.map = r.string(e, "map");
            o.ambient = r.integer<int>(e, "ambient", 0, 127);
            o.later_flags = r.names(e, "later_flags");
            o.later_ambient = r.integer<int>(e, "later_ambient", 0, 127);
            s.ambient_overrides.push_back(std::move(o));
        }
    }
    if (!r.error.empty()) {
        return R::fail(r.error);
    }
    return R::ok(std::move(s));
}

bool ResolvedSoundRules::fixed_voice(int sound) const {
    return std::any_of(fixed_voices.begin(), fixed_voices.end(),
                       [sound](const FixedVoiceEffect& f) { return f.effect == sound; });
}

ResolvedSoundRules resolve(const SoundRules& rules, const std::function<int(std::string_view)>& sound_number) {
    ResolvedSoundRules out;
    for (const SoundRules::FixedVoice& f : rules.fixed_voices) {
        const int id = sound_number(f.sound);
        if (id >= 0) {
            out.fixed_voices.push_back({static_cast<std::uint16_t>(id), f.voice, f.fine});
        }
    }
    return out;
}

void apply_rules(const SoundRules& rules, const ResolvedSoundRules& resolved, DriverTables& t) {
    t.line_volumes = rules.line_volumes;
    t.default_line_volume = rules.default_line_volume;
    t.fixed_voices = resolved.fixed_voices;
    t.first_bank_command = rules.commands.first_bank;
    t.first_ambient_command = rules.commands.first_ambient;
    t.last_ambient_command = rules.commands.last_ambient;
    t.song_file_offset = rules.commands.song_file_offset;
    t.song_bank_offset = rules.commands.song_bank_offset;
    t.music_slot = rules.music_slot;
    t.music_volume = rules.music_volume;
    t.music_ducked_volume = rules.music_ducked_volume;
    t.default_reverb = rules.default_reverb;
}

int ambient_override(const SoundRules& rules, std::string_view map, const std::function<bool(std::string_view)>& flag_set) {
    for (const SoundRules::AmbientOverride& o : rules.ambient_overrides) {
        if (o.map != map) {
            continue;
        }
        const bool later = std::any_of(o.later_flags.begin(), o.later_flags.end(),
                                       [&](const std::string& f) { return flag_set(f); });
        return later ? o.later_ambient : o.ambient;
    }
    return -1;
}

}  // namespace sh::audio
