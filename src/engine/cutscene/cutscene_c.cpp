// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// cutscene_c.h over the VFS. Where a cutscene comes from, highest first:
//   1. a mod's convert/cutscenes/<dir>/<NAME>.dms.json;
//   2. a mod's <dir>/<NAME>.DMS (mods made for the disc layout), converted in memory;
//   3. gamedata/convert/cutscenes/<dir>/<NAME>.dms.json (the import);
//   4. gamedata/raw/<dir>/<NAME>.DMS of an import made before the cutscene converter: converted once and
//      written to (3), so later starts read the JSON and raw/ can go.
#include "engine/cutscene/cutscene_c.h"

#include "engine/cutscene/cutscene_director.h"
#include "engine/vfs/vfs_c.h"
#include "formats/convert/cutscene/cutscene_converter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <span>
#include <string>

namespace {

namespace fs = std::filesystem;
using sh::Result;
using sh::cutscene::Cutscene;

std::string json_path_for(const std::string& game_path) {
    std::string stem = game_path;
    for (char& c : stem) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (!stem.empty() && stem.front() == '/') {
        stem.erase(stem.begin());
    }
    if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos && stem.find('/', dot) == std::string::npos) {
        stem.resize(dot);
    }
    return "cutscenes/" + stem + ".dms.json";
}

bool read_text(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

Result<Cutscene> load_json(const std::string& host) {
    std::string text;
    if (!read_text(host, text)) {
        return Result<Cutscene>::fail(host + ": cannot read");
    }
    return sh::cutscene::parse_cutscene_json(text, host);
}

Result<std::string> disc_to_json(const std::string& game_path, const std::uint8_t* bytes, std::size_t size) {
    const std::span<const std::uint8_t> raw(bytes, size);
    auto dms = sh::convert::cutscene::parse_dms(raw);
    if (!dms) {
        return Result<std::string>::fail(game_path + ": " + dms.error());
    }
    return Result<std::string>::ok(sh::convert::cutscene::dms_to_json(dms.value(), game_path, raw));
}

Result<Cutscene> load(const std::string& game_path) {
    const std::string rel = json_path_for(game_path);
    char host[1024];
    if (ShVfs_FindModConverted(rel.c_str(), host, sizeof host)) {
        return load_json(host);
    }
    if (ShVfs_ModFilePath(game_path.c_str(), host, sizeof host)) {
        std::string bytes;
        if (!read_text(host, bytes)) {
            return Result<Cutscene>::fail(std::string(host) + ": cannot read");
        }
        auto json = disc_to_json(host, reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        if (!json) {
            return Result<Cutscene>::fail(json.error());
        }
        return sh::cutscene::parse_cutscene_json(json.value(), host);
    }
    if (ShVfs_FindConverted(rel.c_str(), host, sizeof host)) {
        return load_json(host);
    }
    size_t size = 0;
    unsigned char* bytes = ShVfs_ReadFile(game_path.c_str(), &size);
    if (!bytes) {
        return Result<Cutscene>::fail(game_path + ": no " + rel + " in gamedata/convert or a mod");
    }
    auto json = disc_to_json(game_path, bytes, size);
    std::free(bytes);
    if (!json) {
        return Result<Cutscene>::fail(json.error());
    }
    const char* gamedata = ShVfs_GameDataDir();
    if (gamedata && gamedata[0]) {
        const fs::path out = fs::path(gamedata) / "convert" / rel;
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary);
        if (f && f.write(json.value().data(), static_cast<std::streamsize>(json.value().size()))) {
            std::fprintf(stderr, "[CUTSCENE] wrote %s from raw/%s (an import older than the cutscene converter)\n",
                         out.string().c_str(), game_path.c_str());
        }
    }
    return sh::cutscene::parse_cutscene_json(json.value(), game_path);
}

sh::cutscene::Director& director() {
    static sh::cutscene::Director d(load);
    return d;
}

// The cutscene and shot the camera was last sampled in. Only the picture between updates reads this.
uintptr_t s_camera_slot = 0;
int s_camera_shot = -1;
std::uint32_t s_camera_cuts = 0;

}  // namespace

extern "C" void ShCutscene_Assign(uintptr_t slot, const char* game_path) {
    director().assign(slot, game_path ? game_path : "");
}

extern "C" int ShCutscene_Start(uintptr_t slot) {
    std::string error;
    const bool ok = director().start(slot, &error);
    if (!error.empty()) {
        std::fprintf(stderr, "warning: [CUTSCENE] %s\n", error.c_str());
    }
    return ok ? 1 : 0;
}

extern "C" int ShCutscene_ActorPose(uintptr_t slot, const char* name, int32_t time_q12, ShCutsceneActorPose* out) {
    std::memset(out, 0, sizeof *out);
    const Cutscene* cs = director().playing(slot);
    if (!cs) {
        return -1;
    }
    const auto idx = cs->find_actor(name ? name : "");
    if (!idx) {
        static std::set<std::string> reported;
        const std::string key = cs->source + "|" + (name ? name : "");
        if (reported.insert(key).second) {
            std::fprintf(stderr, "warning: [CUTSCENE] %s has no track %s\n", cs->source.c_str(), name ? name : "");
        }
        return 0;
    }
    const sh::cutscene::ActorPose pose = cs->actor_pose(*idx, time_q12);
    for (int i = 0; i < 3; ++i) {
        out->position[i] = pose.position[i];
        out->rotation[i] = pose.rotation[i];
    }
    return 1;
}

extern "C" int ShCutscene_CameraPose(uintptr_t slot, int32_t time_q12, ShCutsceneCameraPose* out) {
    std::memset(out, 0, sizeof *out);
    const Cutscene* cs = director().playing(slot);
    if (!cs) {
        return -1;
    }
    const int shot = cs->shot_at(time_q12);
    if (slot != s_camera_slot || shot != s_camera_shot) {
        s_camera_slot = slot;
        s_camera_shot = shot;
        ++s_camera_cuts;
    }
    const sh::cutscene::CameraPose pose = cs->camera_pose(time_q12);
    for (int i = 0; i < 3; ++i) {
        out->eye[i] = pose.eye[i];
        out->target[i] = pose.target[i];
    }
    out->roll = pose.roll;
    out->projection = pose.projection;
    return 1;
}

extern "C" uint32_t ShCutscene_CameraCuts(void) { return s_camera_cuts; }

extern "C" int ShCutscene_FrameCount(uintptr_t slot) {
    const Cutscene* cs = director().playing(slot);
    return cs ? cs->frame_count() : -1;
}

extern "C" const char* ShCutscene_Path(uintptr_t slot) {
    const std::string* p = director().assigned_path(slot);
    return p ? p->c_str() : "";
}

