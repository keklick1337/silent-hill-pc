// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/audio/sound_registry.h"

#include "engine/mods/data_reload.h"
#include "engine/mods/mods.h"
#include "formats/common/json.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <utility>

namespace sh::audio {

namespace {

bool valid_name(std::string_view name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::vector<std::string> strings(const json::Value* list) {
    std::vector<std::string> out;
    if (list != nullptr && list->is_array()) {
        for (const json::Value& v : list->array()) {
            if (v.is_string()) {
                out.push_back(v.string());
            }
        }
    }
    return out;
}

Result<std::string> read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return Result<std::string>::fail("cannot read " + file.string());
    }
    return Result<std::string>::ok(
        std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

// sounds.json first (the base table), then the other files by name.
std::vector<std::filesystem::path> sound_files(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return files;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        const bool a_base = a.filename() == "sounds.json";
        const bool b_base = b.filename() == "sounds.json";
        return a_base != b_base ? a_base : a < b;
    });
    return files;
}

SoundRegistry::Numbering& default_numbering_slot() {
    static SoundRegistry::Numbering numbering = nullptr;
    return numbering;
}

}  // namespace

void SoundRegistry::set_default_numbering(Numbering numbering) { default_numbering_slot() = numbering; }

int SoundRegistry::default_number(std::string_view name) {
    const Numbering numbering = default_numbering_slot();
    return numbering != nullptr ? numbering(name) : -1;
}

Result<int> SoundRegistry::apply(std::string_view text, std::string_view origin, const std::filesystem::path& root) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<int>::fail(std::string(origin) + ": " + doc.error());
    }
    const json::Value* sounds = doc.value().find("sounds");
    if (sounds == nullptr || !sounds->is_object()) {
        return Result<int>::fail(std::string(origin) + ": no \"sounds\" object");
    }
    const auto bad = [&](const std::string& name, const std::string& what) {
        return Result<int>::fail(std::string(origin) + ": sound \"" + name + "\" " + what);
    };
    std::vector<SoundInfo> parsed;
    std::vector<std::pair<std::size_t, std::string>> aliases;  // (index in parsed, former name)
    for (const auto& [name, value] : sounds->members()) {
        if (!name.empty() && name.front() == '_') {
            continue;  // "_comment"
        }
        if (!valid_name(name)) {
            return Result<int>::fail(std::string(origin) + ": sound name \"" + name + "\" is not snake_case");
        }
        SoundInfo info;
        info.name = name;
        if (value.is_string()) {  // "my_step": "footstep_concrete" - another name of that sound
            const SoundInfo* target = find(value.string());
            info.id = target != nullptr ? target->id : default_number(value.string());
            if (info.id < 0) {
                return bad(name, "names \"" + value.string() + "\", which is not a sound");
            }
        } else if (value.is_integer()) {  // "my_step": 1311 - a numeric alias
            info.id = static_cast<int>(value.integer());
        } else if (value.is_object()) {
            const json::Value* id = value.find("id");
            const json::Value* file = value.find("file");
            if (file != nullptr) {
                if (!file->is_string() || file->string().empty()) {
                    return bad(name, "has a \"file\" that is not a path");
                }
                info.file = file->string();
                info.file_path = root.empty() ? std::filesystem::path(info.file) : root / info.file;
                const auto number = [&](const char* key, float& out, float lo, float hi) {
                    const json::Value* v = value.find(key);
                    if (v == nullptr) {
                        return true;
                    }
                    if (!v->is_number() || v->number() < lo || v->number() > hi) {
                        return false;
                    }
                    out = static_cast<float>(v->number());
                    return true;
                };
                if (!number("volume", info.volume, 0.0f, 4.0f)) {
                    return bad(name, "\"volume\" must be a number 0..4");
                }
                if (!number("pitch", info.pitch, -48.0f, 48.0f)) {
                    return bad(name, "\"pitch\" must be semitones -48..48");
                }
                if (!number("pan", info.pan, -1.0f, 1.0f)) {
                    return bad(name, "\"pan\" must be a number -1..1");
                }
                if (const json::Value* loop = value.find("loop"); loop != nullptr) {
                    if (!loop->is_bool()) {
                        return bad(name, "\"loop\" must be true or false");
                    }
                    info.loop = loop->boolean();
                }
            }
            if (id != nullptr && !id->is_integer()) {
                return bad(name, "has an \"id\" that is not an integer");
            }
            // No "id" (the normal case): a base game's sound has the number of the default numbering
            // for its name, an existing name is a patch, a new file gets a free id below (-1 until
            // then).
            if (id != nullptr) {
                info.id = id->is_integer() ? static_cast<int>(id->integer()) : -1;
            } else if (const auto it = sounds_.find(name); it != sounds_.end()) {
                info.id = it->second.id;
            } else {
                info.id = default_number(name);
            }
            if (info.id < 0 && id == nullptr && file == nullptr) {
                unresolved_.push_back(std::string(origin) + ": sound \"" + name +
                                      "\" is no sound of the game and has no \"file\" (skipped)");
                continue;
            }
            if (const json::Value* e = value.find("enum"); e != nullptr && e->is_string()) {
                info.enum_name = e->string();
            }
            info.banks = strings(value.find("banks"));
            info.files = strings(value.find("files"));
            for (std::string& alias : strings(value.find("aliases"))) {
                if (!valid_name(alias)) {
                    return bad(name, "has an alias \"" + alias + "\" that is not snake_case");
                }
                aliases.emplace_back(parsed.size(), std::move(alias));
            }
        } else {
            return Result<int>::fail(std::string(origin) + ": sound \"" + name + "\" must be an object or an id");
        }
        if (info.id >= 0 || !info.has_file()) {
            if (info.id < 0 || info.id > 0xFFFF) {
                return bad(name, "id " + std::to_string(info.id) + " is not a sound id");
            }
        }
        parsed.push_back(std::move(info));
    }
    // Ids for new file-only names, allocated after the whole file parsed (all-or-nothing).
    std::vector<int> taken_ids;
    for (SoundInfo& info : parsed) {
        if (info.id >= 0) {
            continue;
        }
        if (const auto it = sounds_.find(info.name); it != sounds_.end()) {
            info.id = it->second.id;  // an existing name turns into its own file, same id
            continue;
        }
        int next = kFileIdFirst;
        while (next <= kFileIdLast &&
               (names_.count(next) != 0 || std::find(taken_ids.begin(), taken_ids.end(), next) != taken_ids.end())) {
            ++next;
        }
        if (next > kFileIdLast) {
            return bad(info.name, "needs a new id but all of " + std::to_string(kFileIdFirst) + ".." +
                                      std::to_string(kFileIdLast) + " are taken");
        }
        info.id = next;
        taken_ids.push_back(next);
    }
    // A former name ("aliases") is another name of the same sound, after it so the entry keeps
    // naming its id; it carries the bank documentation but no own file.
    for (auto& [index, alias] : aliases) {
        SoundInfo info;
        info.name = std::move(alias);
        info.id = parsed[index].id;
        info.banks = parsed[index].banks;
        info.files = parsed[index].files;
        parsed.push_back(std::move(info));
    }
    for (SoundInfo& info : parsed) {
        auto it = sounds_.find(info.name);
        if (it == sounds_.end()) {
            names_.emplace(info.id, info.name);  // keeps the first name of an id
            if (!info.enum_name.empty()) {
                enums_.emplace(info.enum_name, info.id);
            }
            if (info.has_file()) {
                file_ids_[info.id] = info.name;
            }
            sounds_.emplace(info.name, std::move(info));
            continue;
        }
        // A rebind keeps what the patch does not say (bank files stay documented).
        SoundInfo& current = it->second;
        if (current.id != info.id) {
            if (auto n = names_.find(current.id); n != names_.end() && n->second == current.name) {
                names_.erase(n);
            }
            if (auto f = file_ids_.find(current.id); f != file_ids_.end() && f->second == current.name) {
                file_ids_.erase(f);
            }
            names_.emplace(info.id, current.name);
            current.id = info.id;
        }
        if (!info.banks.empty()) {
            current.banks = std::move(info.banks);
        }
        if (!info.files.empty()) {
            current.files = std::move(info.files);
        }
        if (info.has_file()) {
            current.file = std::move(info.file);
            current.file_path = std::move(info.file_path);
            current.volume = info.volume;
            current.pitch = info.pitch;
            current.pan = info.pan;
            current.loop = info.loop;
        }
        if (current.has_file()) {
            file_ids_[current.id] = current.name;
        }
    }
    return Result<int>::ok(static_cast<int>(parsed.size()));
}

const SoundInfo* SoundRegistry::file_sound(int id) const {
    const auto it = file_ids_.find(id);
    return it != file_ids_.end() ? find(it->second) : nullptr;
}

Result<SoundRegistry> SoundRegistry::load(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir,
                                          std::vector<std::string>& warnings) {
    namespace fs = std::filesystem;
    SoundRegistry registry;
    auto apply_dir = [&](const fs::path& dir, const fs::path& root) {
        for (const fs::path& file : sound_files(dir)) {
            const Result<std::string> text = read_file(file);
            if (!text) {
                warnings.push_back(text.error());
                continue;
            }
            const Result<int> applied = registry.apply(text.value(), file.string(), root);
            if (!applied) {
                warnings.push_back(applied.error() + " (file skipped)");
            }
        }
    };
    apply_dir(gamedata_dir / "sounds", gamedata_dir);
    const auto take_unresolved = [&] {
        warnings.insert(warnings.end(), registry.unresolved_.begin(), registry.unresolved_.end());
        registry.unresolved_.clear();
    };
    take_unresolved();
    std::error_code ec;
    if (!mods_dir.empty() && fs::is_directory(mods_dir, ec)) {
        std::vector<std::string> mod_warnings;
        for (const mods::ModInfo& mod : mods::discover(mods_dir, mod_warnings)) {  // ascending priority
            apply_dir(mod.root / "sounds", mod.root);
        }
        take_unresolved();
        for (const std::string& w : mod_warnings) {
            warnings.push_back("mods: " + w);
        }
    }
    // A mod's file path may also name a file of gamedata (a converted WAV of another sound).
    for (auto& [name, info] : registry.sounds_) {
        if (!info.has_file() || fs::is_regular_file(info.file_path, ec)) {
            continue;
        }
        if (fs::is_regular_file(gamedata_dir / info.file, ec)) {
            info.file_path = gamedata_dir / info.file;
        } else {
            warnings.push_back("sound \"" + name + "\": file " + info.file_path.string() +
                               " not found (the sound stays silent)");
        }
    }
    return Result<SoundRegistry>::ok(std::move(registry));
}

int SoundRegistry::id(std::string_view name) const {
    if (name.empty()) {
        return -1;
    }
    int number = 0;
    const auto [end, ec] = std::from_chars(name.data(), name.data() + name.size(), number);
    if (ec == std::errc() && end == name.data() + name.size()) {
        return number >= 0 && number <= 0xFFFF ? number : -1;
    }
    if (const auto it = sounds_.find(name); it != sounds_.end()) {
        return it->second.id;
    }
    if (const auto it = enums_.find(name); it != enums_.end()) {
        return it->second;
    }
    return -1;
}

const std::string* SoundRegistry::name(int id) const {
    const auto it = names_.find(id);
    return it != names_.end() ? &it->second : nullptr;
}

const SoundInfo* SoundRegistry::find(std::string_view name) const {
    const auto it = sounds_.find(name);
    return it != sounds_.end() ? &it->second : nullptr;
}

const SoundRegistry& SoundRegistry::shared(const std::filesystem::path& gamedata_dir,
                                           const std::filesystem::path& mods_dir) {
    static std::mutex mutex;
    static std::map<std::pair<std::string, std::string>, std::unique_ptr<SoundRegistry>> cache;
    // A reference handed out before a `mods reload` may still be in use: the replaced registries stay alive.
    static std::vector<std::unique_ptr<SoundRegistry>> retired;
    static std::uint32_t epoch = mods::kNeverRead;
    const std::lock_guard<std::mutex> lock(mutex);
    if (mods::data_stale(epoch)) {
        for (auto& [key, registry] : cache) {
            retired.push_back(std::move(registry));
        }
        cache.clear();
    }
    auto& slot = cache[{gamedata_dir.string(), mods_dir.string()}];
    if (!slot) {
        std::vector<std::string> warnings;
        Result<SoundRegistry> loaded = load(gamedata_dir, mods_dir, warnings);
        slot = std::make_unique<SoundRegistry>(loaded ? std::move(loaded.value()) : SoundRegistry{});
    }
    return *slot;
}

}  // namespace sh::audio
