#include "formats/convert/level/level_textures.h"

#include <array>
#include <utility>

namespace sh::convert::level {
namespace {

std::uint32_t u32_at(std::span<const std::uint8_t> b, std::size_t o) {
    return std::uint32_t{b[o]} | (std::uint32_t{b[o + 1]} << 8) | (std::uint32_t{b[o + 2]} << 16) |
           (std::uint32_t{b[o + 3]} << 24);
}

int u16_at(std::span<const std::uint8_t> b, std::size_t o) { return b[o] | (b[o + 1] << 8); }

// Directories of the game's file table in table order (SILENT. ids 0-13 1ST, 14-170 ANIM, 171-1370 BG, ...). Every
// directory is one contiguous run of ids, so trying them in this order finds the first file of a name the way
// Fs_FindNextFile does.
constexpr std::array<std::string_view, 10> kTableDirectories = {"1ST", "ANIM", "BG",  "CHARA", "ITEM",
                                                                 "MISC", "SND",  "TEST", "TIM",  "VIN"};

}  // namespace

Result<TimInfo> read_tim_info(std::span<const std::uint8_t> tim, std::string_view game_path) {
    using Out = Result<TimInfo>;
    if (tim.size() < 8 || u32_at(tim, 0) != 0x10) {
        return Out::fail("not a TIM");
    }
    TimInfo info;
    info.game_path = std::string(game_path);
    const std::uint32_t flag = u32_at(tim, 4);
    static constexpr int kBpp[4] = {4, 8, 16, 24};
    info.bpp = kBpp[flag & 3];
    std::size_t pos = 8;
    if (flag & 8) {
        if (pos + 12 > tim.size()) {
            return Out::fail("truncated CLUT block");
        }
        const std::uint32_t length = u32_at(tim, pos);
        info.has_clut = true;
        info.clut_words = u16_at(tim, pos + 8);
        info.clut_rows = u16_at(tim, pos + 10);
        if (length != 12u + 2u * static_cast<std::uint32_t>(info.clut_words * info.clut_rows) ||
            pos + length > tim.size()) {
            return Out::fail("CLUT block length does not match its size");
        }
        pos += length;
    }
    if (pos + 12 > tim.size()) {
        return Out::fail("truncated image block");
    }
    const int words = u16_at(tim, pos + 8);
    info.height = u16_at(tim, pos + 10);
    switch (info.bpp) {
        case 4: info.width = words * 4; break;
        case 8: info.width = words * 2; break;
        case 16: info.width = words; break;
        default: info.width = words * 2 / 3; break;
    }
    if (info.width <= 0 || info.height <= 0) {
        return Out::fail("empty picture");
    }
    if (info.has_clut) {
        info.palette_size = info.bpp == 4 ? 16 : info.bpp == 8 ? 256 : info.clut_words;
        const int total = info.clut_words * info.clut_rows;
        if (info.palette_size <= 0 || total == 0 || total % info.palette_size != 0) {
            return Out::fail("CLUT is not a whole number of palettes");
        }
        info.palette_count = total / info.palette_size;
    }
    return Out::ok(std::move(info));
}

int palette_index(const TimInfo& tim, std::uint16_t primitive_clut, std::uint16_t material_clut_base) {
    if (!tim.has_clut || tim.palette_size <= 0) {
        return -1;
    }
    const int row = (primitive_clut >> 6) - (material_clut_base >> 6);
    const int word = ((primitive_clut & 63) - (material_clut_base & 63)) * 16;
    if (row < 0 || row >= tim.clut_rows || word < 0 || word >= tim.clut_words || word % tim.palette_size != 0) {
        return -1;
    }
    return row * (tim.clut_words / tim.palette_size) + word / tim.palette_size;
}

std::string palette_png(const TimInfo& tim, int palette) {
    std::string base = "textures/";
    base.append(tim.game_path.substr(0, tim.game_path.rfind('.')));
    if (palette <= 0) {
        return base + ".png";
    }
    std::size_t digits = 2;
    for (int n = tim.palette_count - 1; n >= 100; n /= 10) {
        ++digits;
    }
    std::string number = std::to_string(palette);
    if (number.size() < digits) {
        number.insert(0, digits - number.size(), '0');
    }
    return base + ".p" + number + ".png";
}

std::string relative_uri(std::string_view from, std::string_view target) {
    std::string uri;
    for (const char c : from) {
        if (c == '/') {
            uri += "../";
        }
    }
    uri.append(target);
    return uri;
}

TextureCatalog::TextureCatalog(SiblingReader sibling) : sibling_(std::move(sibling)) {}

const TimInfo* TextureCatalog::by_path(std::string_view game_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = by_path_.find(game_path);
    if (it == by_path_.end()) {
        std::optional<TimInfo> info;
        if (sibling_) {
            if (const auto bytes = sibling_(game_path)) {
                if (auto parsed = read_tim_info(*bytes, game_path)) {
                    info = std::move(parsed.value());
                }
            }
        }
        it = by_path_.emplace(std::string(game_path), std::move(info)).first;
    }
    return it->second ? &*it->second : nullptr;
}

const TimInfo* TextureCatalog::by_name(std::string_view name) const {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = by_name_.find(name);
        if (it != by_name_.end()) {
            return it->second ? &*it->second : nullptr;
        }
    }
    const TimInfo* found = nullptr;
    if (!name.empty()) {
        for (const std::string_view dir : kTableDirectories) {
            std::string path(dir);
            path += '/';
            path.append(name);
            path += ".TIM";
            found = by_path(path);
            if (found != nullptr) {
                break;
            }
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = by_name_.emplace(std::string(name), found ? std::optional<TimInfo>(*found) : std::nullopt).first;
    return it->second ? &*it->second : nullptr;
}

}  // namespace sh::convert::level
