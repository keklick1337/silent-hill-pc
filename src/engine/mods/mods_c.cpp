#include "engine/mods/mods_c.h"

#include "engine/mods/registry.h"

#include <cstring>
#include <filesystem>
#include <optional>
#include <string>

extern "C" int ShMods_FindFile(const char* relative, char* out, size_t out_size) {
    if (relative == nullptr || out == nullptr || out_size == 0) {
        return 0;
    }
    const std::optional<std::filesystem::path> file = sh::mods::registry().find(relative);
    if (!file) {
        return 0;
    }
    const std::string host = file->string();
    if (host.size() + 1 > out_size) {
        return 0;
    }
    std::memcpy(out, host.c_str(), host.size() + 1);
    return 1;
}

extern "C" int ShMods_ActiveCount(void) {
    return static_cast<int>(sh::mods::registry().active().size());
}
