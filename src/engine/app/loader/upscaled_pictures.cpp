#include "engine/app/loader/upscaled_pictures.h"

#include <cstdio>
#include <map>
#include <optional>
#include <vector>

#include "engine/assets/texture_upscale.h"
#include "engine/assets/upscale_cache.h"
#include "engine/vfs/vfs_c.h"
#include "render/textures/replacements.h"

namespace sh::app {

namespace {

// The pictures put, by cache key: where they lie in video memory and the picture file under gamedata.
struct Placed {
    sh::textures::PictureKey key;
    std::string relative;
};
std::multimap<std::string, Placed>& placed() {
    static std::multimap<std::string, Placed> map;
    return map;
}

std::optional<std::vector<std::uint8_t>> read_data_file(const std::string& relative) {
    char path[1024];
    if (!ShVfs_FindDataFile(relative.c_str(), path, sizeof(path))) return std::nullopt;
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return std::nullopt;
    std::vector<std::uint8_t> bytes;
    std::uint8_t chunk[65536];
    std::size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) bytes.insert(bytes.end(), chunk, chunk + n);
    std::fclose(f);
    return bytes;
}

// The upscaled picture when the cache has one, else the imported one: a picture put now shows its upscaled copy
// the moment it arrives, and whether it is still in place tells whether video memory still holds the original.
bool put(const Placed& p) {
    std::optional<std::vector<std::uint8_t>> bytes = read_data_file(p.relative);
    if (!bytes) return false;
    if (auto upscaled = sh::upscale::find(p.relative, *bytes)) bytes = std::move(upscaled);
    const auto picture = sh::textures::read_picture(p.relative, *bytes);
    return picture && sh::textures::replacements().put(p.key, picture->pixels, picture->width, picture->height, false);
}

}  // namespace

bool put_upscaled_picture(const std::string& disc_path, const sh::textures::PictureKey& key, bool stp_uniform) {
    if (sh::upscale::scale() == 0 || !stp_uniform) return false;
    // "TIM/MP_0TOWN.TIM" -> convert/textures/TIM/MP_0TOWN.png, where the import wrote it.
    const std::size_t dot = disc_path.rfind('.');
    const std::string relative = "convert/textures/" + disc_path.substr(0, dot) + ".png";
    const std::optional<std::string> name = sh::upscale::texture_key(relative);
    if (!name) return false;
    Placed p{key, relative};
    if (!put(p)) return false;
    auto& map = placed();
    for (auto it = map.begin(); it != map.end();) {
        // One picture per place: an earlier one there is gone from video memory.
        it = it->second.key == key ? map.erase(it) : std::next(it);
    }
    map.emplace(*name, std::move(p));
    return true;
}

void refresh_upscaled_pictures(const std::set<std::string>& keys) {
    auto& map = placed();
    for (auto it = map.begin(); it != map.end();) {
        if (!keys.count(it->first)) {
            ++it;
            continue;
        }
        // Overwritten video memory took the replacement away: the picture is not shown any more.
        if (!sh::textures::replacements().has(it->second.key) || !put(it->second)) {
            it = map.erase(it);
            continue;
        }
        ++it;
    }
}

}  // namespace sh::app
