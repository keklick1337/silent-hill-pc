// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/flags/flag_registry.h"

#include "engine/ids/ids.h"
#include "engine/mods/mods.h"
#include "formats/common/json.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>

namespace sh::flags {

namespace fs = std::filesystem;

namespace {

Result<std::string> read_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return Result<std::string>::fail("cannot read " + file.string());
    }
    return Result<std::string>::ok(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

// The flag declaration files of a directory: flags/*.json by name, and flags.json unless `only_dir`.
std::vector<fs::path> declaration_files(const fs::path& dir, bool only_dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!only_dir && fs::is_regular_file(dir / "flags.json", ec)) {
        out.push_back(dir / "flags.json");
    }
    std::vector<fs::path> listed;
    if (fs::is_directory(dir / "flags", ec)) {
        for (const fs::directory_entry& e : fs::directory_iterator(dir / "flags", ec)) {
            if (e.path().extension() == ".json" && e.is_regular_file(ec)) {
                listed.push_back(e.path());
            }
        }
    }
    std::sort(listed.begin(), listed.end());
    out.insert(out.end(), listed.begin(), listed.end());
    return out;
}

}  // namespace

const FlagInfo* FlagRegistry::find(std::string_view id) const {
    const auto it = index_.find(id);
    return it != index_.end() ? &flags_[it->second] : nullptr;
}

Result<int> FlagRegistry::apply(std::string_view text, std::string_view ns, std::string_view origin,
                                std::vector<std::string>& warnings) {
    auto doc = json::parse(text);
    if (!doc) {
        return Result<int>::fail(std::string(origin) + ": " + doc.error());
    }
    const json::Value* list = doc.value().find("flags");
    if (list == nullptr || !list->is_object()) {
        return Result<int>::fail(std::string(origin) + ": no \"flags\" object");
    }
    const bool base = ns == ids::kBaseNamespace;
    const json::Value* kind = doc.value().find("kind");
    const bool marks = kind != nullptr && kind->is_string() && kind->string() == "paper_map_mark";
    // Former names in "aliases" / "old_save_names": short ones are of this file's namespace.
    const auto former = [&](const json::Value& entry, const char* key, const std::string& id,
                            std::vector<std::string>& out) {
        const json::Value* list = entry.find(key);
        if (list == nullptr || !list->is_array()) {
            return;
        }
        for (const json::Value& a : list->array()) {
            const bool ok = a.is_string() && (a.string().find(':') != std::string::npos ? ids::valid_id(a.string())
                                                                                          : ids::valid_local_name(a.string()));
            if (!ok) {
                warnings.push_back(std::string(origin) + ": " + id + ": a " + key + " entry is not a flag name");
                continue;
            }
            out.push_back(a.string().find(':') != std::string::npos ? a.string() : ids::qualify(a.string(), ns));
        }
    };
    int count = 0;
    for (const auto& [name, entry] : list->members()) {
        std::string id;
        if (name.find(':') != std::string::npos) {
            if (!ids::valid_id(name)) {
                warnings.push_back(std::string(origin) + ": '" + name + "' is not a flag name");
                continue;
            }
            id = name;
            if (ids::namespace_of(id) != ns && find(id) == nullptr) {
                warnings.push_back(std::string(origin) + ": " + id + " is another namespace's flag that nothing declares");
                continue;
            }
        } else {
            if (!ids::valid_local_name(name)) {
                warnings.push_back(std::string(origin) + ": '" + name + "' is not a flag name");
                continue;
            }
            // A mod's short name of a base flag is that flag (a patch of its description).
            const std::string base_id = ids::qualify(name);
            id = (!base && find(base_id) != nullptr) ? base_id : ids::qualify(name, ns);
        }
        std::string description;
        if (const json::Value* d = entry.find("description"); d != nullptr && d->is_string()) {
            description = d->string();
        }
        FlagInfo* info = nullptr;
        if (const auto it = index_.find(id); it != index_.end()) {
            info = &flags_[it->second];
            if (!description.empty()) {
                info->description = description;
            }
        } else {
            index_.emplace(id, flags_.size());
            flags_.push_back({id, description, std::string(origin), {}, {}, false});
            info = &flags_.back();
        }
        former(entry, "aliases", id, info->aliases);
        former(entry, "old_save_names", id, info->old_save_names);
        info->paper_map_mark = info->paper_map_mark || marks;
        ++count;
    }
    return Result<int>::ok(count);
}

Result<FlagRegistry> FlagRegistry::load(const fs::path& gamedata_dir, const fs::path& mods_dir,
                                        std::vector<std::string>& warnings) {
    FlagRegistry registry;
    std::error_code ec;
    // A base without flags/ is an older gamedata layout: one flags.json. A stale flags.json next to
    // flags/ (an older copy in a build directory) is ignored.
    const std::vector<fs::path> base_files = declaration_files(gamedata_dir, fs::is_directory(gamedata_dir / "flags", ec));
    for (const fs::path& base : base_files) {
        auto text = read_file(base);
        if (!text) {
            return Result<FlagRegistry>::fail(text.error());
        }
        auto applied = registry.apply(text.value(), ids::kBaseNamespace, base.string(), warnings);
        if (!applied) {
            return Result<FlagRegistry>::fail(applied.error());
        }
    }
    if (mods_dir.empty() || !fs::is_directory(mods_dir, ec)) {
        return Result<FlagRegistry>::ok(std::move(registry));
    }
    std::vector<std::string> mod_warnings;
    for (const mods::ModInfo& mod : mods::discover(mods_dir, mod_warnings)) {
        for (const fs::path& file : declaration_files(mod.root, false)) {
            auto text = read_file(file);
            // A broken mod file costs that mod its flags, not the game its start.
            auto applied = text ? registry.apply(text.value(), mod.id, file.string(), warnings)
                                : Result<int>::fail(text.error());
            if (!applied) {
                warnings.push_back(applied.error());
            }
        }
    }
    for (const std::string& w : mod_warnings) {
        warnings.push_back("mods: " + w);
    }
    return Result<FlagRegistry>::ok(std::move(registry));
}

std::map<std::string, int, std::less<>> FlagRegistry::assign_bits(const std::map<std::string, int, std::less<>>& base_bits,
                                                                  int bit_count, std::vector<std::string>& warnings) const {
    std::map<std::string, int, std::less<>> out(base_bits.begin(), base_bits.end());
    std::set<int> taken;
    for (const auto& [id, bit] : base_bits) {
        taken.insert(bit);
    }
    // New flags in a fixed order (namespace, then name): the same mods give the same bits,
    // whatever order their files were read in.
    std::vector<const FlagInfo*> fresh;
    for (const FlagInfo& f : flags_) {
        if (out.find(f.id) == out.end()) {
            fresh.push_back(&f);  // a mod's flag, or a base flag the original layout does not have
        }
    }
    std::sort(fresh.begin(), fresh.end(), [](const FlagInfo* a, const FlagInfo* b) { return a->id < b->id; });
    int next = bit_count - 1;
    for (const FlagInfo* f : fresh) {
        while (next > 0 && taken.count(next) != 0) {
            --next;
        }
        if (next <= 0) {
            warnings.push_back(f->origin + ": no free save bit left for " + f->id);
            continue;
        }
        out.emplace(f->id, next);
        taken.insert(next);
    }
    return out;
}

std::vector<std::string> set_flag_names(std::span<const std::uint32_t> words, FlagNameOf name_of) {
    std::vector<std::string> out;
    const int bits = static_cast<int>(words.size() * 32);
    for (int bit = 1; bit < bits && name_of != nullptr; ++bit) {
        if (((words[static_cast<std::size_t>(bit >> 5)] >> (bit & 31)) & 1u) == 0) {
            continue;
        }
        if (const char* name = name_of(bit)) {
            out.emplace_back(name);
        }
    }
    return out;
}

bool apply_flag_names(std::span<std::uint32_t> words, const std::vector<std::string>& names, FlagNumberOf number_of,
                      FlagNameOf name_of, std::vector<std::string>& unknown) {
    if (number_of == nullptr || name_of == nullptr) {
        return false;
    }
    const int bits = static_cast<int>(words.size() * 32);
    std::vector<std::uint32_t> listed(words.size(), 0);
    for (const std::string& name : names) {
        const int bit = number_of(name.c_str());
        if (bit <= 0 || bit >= bits) {
            unknown.push_back(name);
            continue;
        }
        listed[static_cast<std::size_t>(bit >> 5)] |= 1u << (bit & 31);
    }
    bool changed = false;
    for (int bit = 1; bit < bits; ++bit) {
        if (name_of(bit) == nullptr) {
            continue;
        }
        const std::size_t w = static_cast<std::size_t>(bit >> 5);
        const std::uint32_t mask = 1u << (bit & 31);
        if ((words[w] & mask) != (listed[w] & mask)) {
            words[w] = (words[w] & ~mask) | (listed[w] & mask);
            changed = true;
        }
    }
    return changed;
}

}  // namespace sh::flags
