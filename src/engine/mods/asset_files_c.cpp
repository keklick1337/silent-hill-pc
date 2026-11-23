#include "engine/mods/asset_files_c.h"

#include "engine/mods/asset_names.h"
#include "engine/mods/registry.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace {

bool no_mods() { return sh::mods::registry().active().empty(); }

std::optional<std::filesystem::path> find(std::string_view relative) { return sh::mods::registry().find(relative); }

int copy_out(const std::filesystem::path& file, char* out, size_t out_size) {
    const std::string host = file.string();
    if (out == nullptr || host.size() + 1 > out_size) {
        return 0;
    }
    std::memcpy(out, host.c_str(), host.size() + 1);
    return 1;
}

int image_out(const std::optional<sh::mods::ImageFile>& image, char* out, size_t out_size, int* is_dds) {
    if (!image || !copy_out(image->path, out, out_size)) {
        return 0;
    }
    if (is_dds != nullptr) {
        *is_dds = image->dds ? 1 : 0;
    }
    return 1;
}

}  // namespace

extern "C" FILE* ShModAsset_Open(const char* relative, const char* mode) {
    if (relative == nullptr || mode == nullptr || no_mods()) {
        return nullptr;
    }
    const std::optional<std::filesystem::path> file = find(relative);
    return file ? std::fopen(file->string().c_str(), mode) : nullptr;
}

extern "C" int ShModAsset_Find(const char* relative, char* out, size_t out_size) {
    if (relative == nullptr || no_mods()) {
        return 0;
    }
    const std::optional<std::filesystem::path> file = find(relative);
    return file ? copy_out(*file, out, out_size) : 0;
}

extern "C" int ShModAsset_TimHasImage(const char* disc_path) {
    if (disc_path == nullptr || no_mods()) {
        return 0;
    }
    return sh::mods::find_image(sh::mods::tim_whole_names(disc_path), find) ||
           sh::mods::tim_is_per_row(disc_path, find);
}

extern "C" int ShModAsset_TimPerRow(const char* disc_path) {
    if (disc_path == nullptr || no_mods()) {
        return 0;
    }
    return sh::mods::tim_is_per_row(disc_path, find) ? 1 : 0;
}

extern "C" int ShModAsset_TimRow(const char* disc_path, int row, char* out, size_t out_size, int* is_dds) {
    if (disc_path == nullptr || no_mods()) {
        return 0;
    }
    return image_out(sh::mods::find_image(sh::mods::tim_row_names(disc_path, row), find), out, out_size, is_dds);
}

extern "C" int ShModAsset_ListDir(const char* relative_dir, void (*visit)(const char* name, void* ctx), void* ctx) {
    if (relative_dir == nullptr || visit == nullptr || no_mods()) {
        return 0;
    }
    std::set<std::string> lowered;
    std::vector<std::string> names;
    for (const sh::mods::ResolvedMod* mod : sh::mods::registry().active()) {
        const std::optional<std::filesystem::path> dir = sh::mods::find_dir_nocase(mod->candidate.root, relative_dir);
        if (!dir) {
            continue;
        }
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(*dir, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            std::string name = entry.path().filename().string();
            if (lowered.insert(sh::mods::lower_ascii(name)).second) {
                names.push_back(std::move(name));
            }
        }
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return sh::mods::lower_ascii(a) < sh::mods::lower_ascii(b);
    });
    for (const std::string& name : names) {
        visit(name.c_str(), ctx);
    }
    return static_cast<int>(names.size());
}

extern "C" int ShModAsset_FindEach(const char* relative, void (*visit)(const char* host, void* ctx), void* ctx) {
    if (relative == nullptr || visit == nullptr || no_mods()) {
        return 0;
    }
    const std::vector<std::filesystem::path> files = sh::mods::registry().find_all(relative);
    for (auto it = files.rbegin(); it != files.rend(); ++it) {
        visit(it->string().c_str(), ctx);
    }
    return static_cast<int>(files.size());
}

extern "C" int ShModAsset_DirEach(const char* relative_dir, void (*visit)(const char* host_dir, void* ctx), void* ctx) {
    if (relative_dir == nullptr || no_mods()) {
        return 0;
    }
    const std::vector<const sh::mods::ResolvedMod*> active = sh::mods::registry().active();
    int count = 0;
    for (auto it = active.rbegin(); it != active.rend(); ++it) {
        if (const std::optional<std::filesystem::path> dir = sh::mods::find_dir_nocase((*it)->candidate.root, relative_dir)) {
            if (visit != nullptr) {
                visit(dir->string().c_str(), ctx);
            }
            ++count;
        }
    }
    return count;
}

extern "C" int ShModAsset_TimWhole(const char* disc_path, char* out, size_t out_size, int* is_dds) {
    if (disc_path == nullptr || no_mods() || sh::mods::tim_is_per_row(disc_path, find)) {
        return 0;
    }
    return image_out(sh::mods::find_image(sh::mods::tim_whole_names(disc_path), find), out, out_size, is_dds);
}
