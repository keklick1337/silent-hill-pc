// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game manifest reader (gamedata/game.json). Plain C++20 with no engine
// dependencies beyond sh_common's json: this TU is
// linked into the game, the import tools and the tests alike, so it must not
// pull in anything. A missing or broken manifest is not an error — callers
// keep their built-in defaults (that is the fresh-install case).

#include "engine/manifest/map_manifest.h"

#include "formats/common/json.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

using namespace sh;      // Result
using sh::json::Value;

namespace {

// More maps than the engine has overlay slots: a manifest's extras are
// ignored.
constexpr int kMaxMaps = 128;
constexpr int kMaxContainers = 8;

struct Manifest {
    std::vector<std::string> names;   // by id; empty entry = fallback
    std::vector<std::string> descs;
    std::vector<std::string> containers;
    std::string title;
    std::string exe_name;
    std::string log_prefix;
    std::string start_map;
    std::string new_game_map;
    std::string intro_file;         // XA path/file of the intro movie ("XA/C1_20670")
    std::string intro_file_extra;   // extra-options intro ("XA/C2_20670"; empty = same as intro)
    int intro_frame_offset = 0;     // alt-intro start frame (MovieIntroAlternate)
    std::string opening_file;       // the New Game opening movie ("XA/M1_03500")
    std::string disc_exe_file;
    std::string disc_game_id;
    std::string disc_region;
};

Manifest& manifest() {
    static Manifest m;
    return m;
}

bool& loaded() {
    static bool v = false;
    return v;
}

std::string& root() {
    static std::string v;
    return v;
}

std::filesystem::path find_manifest() {
    if (root().empty()) {
        return {};
    }
    std::error_code ec;
    const std::filesystem::path candidate =
        std::filesystem::path(root()) / "gamedata" / "game.json";
    if (std::filesystem::is_regular_file(candidate, ec)) {
        return candidate;
    }
    return {};
}

bool key_is(const char* key, const char* literal) {
    return std::strcmp(key, literal) == 0;
}

}  // namespace

extern "C" void MapManifest_SetRoot(const char* exe_dir) {
    root() = exe_dir != nullptr ? exe_dir : "";
}

namespace {

int parse_manifest(const std::string& text);

}  // namespace

extern "C" int MapManifest_Load(void) {
    if (loaded()) {
        return (int)manifest().names.size();
    }
    loaded() = true;

    const std::filesystem::path path = find_manifest();
    if (path.empty()) {
        return 0;  // the normal case on a fresh install: built-in defaults
    }
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse_manifest(text);
}

extern "C" int MapManifest_LoadText(const char* text) {
    manifest() = Manifest{};
    loaded() = text != nullptr;
    return text != nullptr ? parse_manifest(text) : 0;
}

namespace {

int parse_manifest(const std::string& text) {
    const Result<Value> parsed = json::parse(text);
    if (!parsed) {
        return 0;
    }
    const Value& root_value = parsed.value();
    if (!root_value.is_object()) {
        return 0;
    }

    Manifest& m = manifest();
    const Value* str = nullptr;
    if ((str = root_value.find("title")) != nullptr && str->is_string()) m.title = str->string();
    if ((str = root_value.find("exe_name")) != nullptr && str->is_string()) m.exe_name = str->string();
    if ((str = root_value.find("log_prefix")) != nullptr && str->is_string()) m.log_prefix = str->string();
    if ((str = root_value.find("start_map")) != nullptr && str->is_string()) m.start_map = str->string();
    if ((str = root_value.find("new_game_map")) != nullptr && str->is_string()) m.new_game_map = str->string();

    /* Intro/opening movies: XA file names as "XA/C1_20670", which the game looks up in the
     * file table; absent fields keep the original's. */
    if ((str = root_value.find("intro_file")) != nullptr && str->is_string()) m.intro_file = str->string();
    if ((str = root_value.find("intro_file_extra")) != nullptr && str->is_string()) m.intro_file_extra = str->string();
    if (const Value* off = root_value.find("intro_frame_offset"); off != nullptr && off->is_integer()) {
        m.intro_frame_offset = (int)off->integer();
    }
    if ((str = root_value.find("opening_file")) != nullptr && str->is_string()) m.opening_file = str->string();

    const Value* disc = root_value.find("disc");
    if (disc != nullptr && disc->is_object()) {
        if ((str = disc->find("exe_file")) != nullptr && str->is_string()) m.disc_exe_file = str->string();
        if ((str = disc->find("game_id")) != nullptr && str->is_string()) m.disc_game_id = str->string();
        if ((str = disc->find("region")) != nullptr && str->is_string()) m.disc_region = str->string();
        const Value* containers = disc->find("containers");
        if (containers != nullptr && containers->is_array()) {
            for (const Value& c : containers->array()) {
                if (c.is_string() && (int)m.containers.size() < kMaxContainers) {
                    m.containers.push_back(c.string());
                }
            }
        }
    }

    const Value* maps = root_value.find("maps");
    if (maps == nullptr || !maps->is_array()) {
        return 0;
    }
    /* The array position is the map's number: gamedata carries no numeric ids,
     * the number is derived from order.
     * A legacy "id" that disagrees with the position gets one warning, then
     * the position wins; an entry without a usable name keeps its slot empty
     * so callers fall back to their compiled-in default. */
    std::size_t position = 0;
    bool id_warned = false;
    for (const Value& entry : maps->array()) {
        const std::size_t index = position++;
        if (index >= (std::size_t)kMaxMaps) {
            break;
        }
        if (!entry.is_object()) {
            continue;
        }
        const Value* name = entry.find("name");
        const Value* desc = entry.find("description");
        if (name == nullptr || !name->is_string()) {
            continue;
        }
        const Value* id = entry.find("id");
        if (id != nullptr && id->is_integer() && id->integer() != (std::int64_t)index && !id_warned) {
            std::fprintf(stderr,
                         "game.json: map '%s' keeps legacy id %lld but sits at position %zu; using the position\n",
                         name->string().c_str(), (long long)id->integer(), index);
            id_warned = true;
        }
        if (index >= m.names.size()) {
            m.names.resize(index + 1);
            m.descs.resize(index + 1);
        }
        m.names[index] = name->string();
        if (desc != nullptr && desc->is_string()) {
            m.descs[index] = desc->string();
        }
    }
    return (int)m.names.size();
}

}  // namespace

extern "C" const char* MapManifest_String(const char* key) {
    if (key == nullptr) {
        return nullptr;
    }
    MapManifest_Load();
    Manifest& m = manifest();
    if (key_is(key, "title")) return m.title.empty() ? nullptr : m.title.c_str();
    if (key_is(key, "exe_name")) return m.exe_name.empty() ? nullptr : m.exe_name.c_str();
    if (key_is(key, "log_prefix")) return m.log_prefix.empty() ? nullptr : m.log_prefix.c_str();
    if (key_is(key, "start_map")) return m.start_map.empty() ? nullptr : m.start_map.c_str();
    if (key_is(key, "new_game_map")) return m.new_game_map.empty() ? nullptr : m.new_game_map.c_str();
    if (key_is(key, "intro_file")) return m.intro_file.empty() ? nullptr : m.intro_file.c_str();
    if (key_is(key, "intro_file_extra")) return m.intro_file_extra.empty() ? nullptr : m.intro_file_extra.c_str();
    if (key_is(key, "opening_file")) return m.opening_file.empty() ? nullptr : m.opening_file.c_str();
    return nullptr;
}

extern "C" int MapManifest_Int(const char* key) {
    if (key == nullptr) {
        return 0;
    }
    MapManifest_Load();
    Manifest& m = manifest();
    if (key_is(key, "intro_frame_offset")) return m.intro_frame_offset;
    return 0;
}

extern "C" const char* MapManifest_DiscString(const char* key) {
    if (key == nullptr) {
        return nullptr;
    }
    MapManifest_Load();
    Manifest& m = manifest();
    if (key_is(key, "exe_file")) return m.disc_exe_file.empty() ? nullptr : m.disc_exe_file.c_str();
    if (key_is(key, "game_id")) return m.disc_game_id.empty() ? nullptr : m.disc_game_id.c_str();
    if (key_is(key, "region")) return m.disc_region.empty() ? nullptr : m.disc_region.c_str();
    return nullptr;
}

extern "C" const char* MapManifest_DiscContainer(int index) {
    MapManifest_Load();
    Manifest& m = manifest();
    if (index < 0 || index >= (int)m.containers.size() || m.containers[index].empty()) {
        return nullptr;
    }
    return m.containers[index].c_str();
}

extern "C" const char* MapManifest_Name(int id) {
    MapManifest_Load();
    Manifest& m = manifest();
    const std::size_t index = static_cast<std::size_t>(id);
    if (index < m.names.size() && !m.names[index].empty()) {
        return m.names[index].c_str();
    }
    return nullptr;
}

extern "C" const char* MapManifest_Description(int id) {
    MapManifest_Load();
    Manifest& m = manifest();
    const std::size_t index = static_cast<std::size_t>(id);
    if (index < m.descs.size() && !m.descs[index].empty()) {
        return m.descs[index].c_str();
    }
    return nullptr;
}

extern "C" int MapManifest_Count(void) {
    MapManifest_Load();
    return (int)manifest().names.size();
}
