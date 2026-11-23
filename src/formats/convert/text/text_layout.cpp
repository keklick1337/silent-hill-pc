#include "formats/convert/text/text_layout.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <map>

namespace sh::convert::text {

namespace {

// NTSC USA 1.1 (SLUS-00707). Addresses and sizes are checked against the disc: every pointer lands on a
// NUL-terminated string.
constexpr std::uint32_t kBodyprogBase = 0x80024B60;
constexpr std::uint32_t kScreenBase = 0x801E2600;
constexpr std::uint32_t kMapBase = 0x800C9578;

// Size of the common block that every map overlay starts MAP_MESSAGES with (the same 15 lines in every map).
constexpr std::uint32_t kCommonMessages = 15;

constexpr std::uint32_t kMaxStringLength = 2048;

struct TableSpec {
    const char* name;
    std::uint32_t vram;
    std::uint32_t count;
    Dialect dialect;
};

struct FileSpec {
    const char* path;
    std::uint32_t base;
    bool encrypted;
    std::span<const TableSpec> tables;
};

constexpr std::array kBodyprogTables = {
    TableSpec{"item_commands", 0x80026214, 8, Dialect::Game},       // Use, Equip, Unequip, Reload, Detail, On, Off, Look
    TableSpec{"inventory_menu", 0x80027E34, 8, Dialect::Game},      // Equipment, Exit, Option, Map, Command, Status, ...
    TableSpec{"item_labels", 0x80027F94, 7, Dialect::Game},         // Stock:, ==On==, ==Off==, ==Use_OK==, ..., Fuel:
    TableSpec{"results_screen", 0x8002B4C0, 34, Dialect::Game},     // GAME_RESULT screen after the ending
    TableSpec{"item_near_names", 0x800A99E4, 8, Dialect::Game},     // PAD_NEAR, AIDKIT_N, ... (8-char resource names)
    TableSpec{"main_menu", 0x800A9A8C, 5, Dialect::Game},           // LOAD, CONTINUE, START, OPTION, EXTRA
    TableSpec{"difficulty_menu", 0x800A9AA0, 3, Dialect::Game},     // EASY, NORMAL, HARD
    TableSpec{"item_names", 0x800ADB60, 195, Dialect::Game},        // INVENTORY_ITEM_NAMES, indexed by item id
    TableSpec{"item_descriptions", 0x800ADE6C, 195, Dialect::Game}, // g_ItemDescriptions, indexed by item id
};

constexpr std::array kOptionTables = {
    TableSpec{"option_rows_1", 0x801E2764, 8, Dialect::Game},       // Weapon_Control ... Bullet_Adjust
    TableSpec{"option_rows_2", 0x801E280C, 9, Dialect::Game},       // Exit, Brightness_Level ... SE_Volume
    TableSpec{"option_values", 0x801E2A6C, 19, Dialect::Game},      // Press, Switch, Normal, ... x6
    TableSpec{"option_sound_values", 0x801E2BDC, 4, Dialect::Game}, // On, Off, Stereo, Monaural
    TableSpec{"controller_config", 0x801E736C, 18, Dialect::Game},  // EXIT, TYPE_1 ... OPTION
};

constexpr std::array kSaveloadTables = {
    TableSpec{"slot_messages", 0x801E2830, 11, Dialect::Game},
    TableSpec{"progress_messages", 0x801E297C, 12, Dialect::Game},
    TableSpec{"confirm_messages", 0x801E2A3C, 3, Dialect::Game},
    TableSpec{"file_labels", 0x801E2D70, 6, Dialect::Game},
    TableSpec{"save_locations", 0x801E74A8, 25, Dialect::Game},
};

constexpr std::array kCreditTables = {
    TableSpec{"credit_lines", 0x801E5590, 399, Dialect::Ascii},
};

constexpr std::array<FileSpec, 4> kFixedFiles = {{
    {"1ST/BODYPROG.BIN", kBodyprogBase, true, kBodyprogTables},
    {"VIN/OPTION.BIN", kScreenBase, false, kOptionTables},
    {"VIN/SAVELOAD.BIN", kScreenBase, false, kSaveloadTables},
    {"VIN/STF_ROLL.BIN", kScreenBase, false, kCreditTables},
}};

std::string normalise(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (const char c : path) {
        if (c == '\\') {
            out += '/';
        } else if (c >= 'a' && c <= 'z') {
            out += static_cast<char>(c - 'a' + 'A');
        } else {
            out += c;
        }
    }
    while (out.starts_with("./")) {
        out.erase(0, 2);
    }
    return out;
}

// VIN/MAPn_Snn.BIN
bool is_map_overlay_path(const std::string& path) {
    if (path.size() != 16 || !path.starts_with("VIN/MAP") || !path.ends_with(".BIN")) {
        return false;
    }
    const auto digit = [&](std::size_t i) { return path[i] >= '0' && path[i] <= '9'; };
    return digit(7) && path[8] == '_' && path[9] == 'S' && digit(10) && digit(11);
}

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) | (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

// File offset of a string the word points to, if it points into the file at a NUL-terminated string.
bool resolve_pointer(std::span<const std::uint8_t> image, std::uint32_t base, std::uint32_t value, std::uint32_t limit,
                     std::uint32_t& offset) {
    if (value < base || value - base >= limit) {
        return false;
    }
    offset = value - base;
    const auto begin = image.begin() + offset;
    const auto end = image.begin() + std::min<std::size_t>(image.size(), offset + kMaxStringLength + 1);
    return std::find(begin, end, std::uint8_t{0}) != end;
}

// MAP_MESSAGES of a map overlay. The overlay header starts at file offset 4; its word at +0x30 points to the array,
// which is not terminated. The array is followed by other data, so its end is where a word stops being either 0
// or the address of a 4-aligned string located before the array (the strings are laid out ahead of the data).
Result<Table> find_map_messages(std::span<const std::uint8_t> image, std::uint32_t base) {
    using R = Result<Table>;
    constexpr std::size_t kPointerAt = 4 + 0x30;
    if (image.size() < kPointerAt + 4) {
        return R::fail("overlay is too small for a map header");
    }
    const std::uint32_t vram = read_u32(image, kPointerAt);
    if (vram < base || (vram - base) % 4 != 0 || vram - base >= image.size()) {
        return R::fail("mapMessages does not point into the overlay");
    }
    const std::uint32_t array_offset = vram - base;

    std::uint32_t count = 0;
    std::uint32_t last_pointer = 0;
    for (std::size_t at = array_offset; at + 4 <= image.size(); at += 4) {
        const std::uint32_t word = read_u32(image, at);
        if (word != 0) {
            std::uint32_t target = 0;
            if (word % 4 != 0 || !resolve_pointer(image, base, word, array_offset, target)) {
                break;
            }
            const auto nul = std::find(image.begin() + target, image.end(), std::uint8_t{0});
            if (static_cast<std::size_t>(nul - image.begin()) >= array_offset) {
                break;
            }
            last_pointer = count + 1;
        }
        ++count;
    }
    count = last_pointer;  // zero words after the last string are padding, not entries
    if (count < kCommonMessages) {
        return R::fail("MAP_MESSAGES is shorter than the common block");
    }

    Table table;
    table.name = "map_messages";
    table.vram = vram;
    table.file_offset = array_offset;
    table.dialect = Dialect::Markup;
    table.entries.resize(count);
    return R::ok(std::move(table));
}

}  // namespace

std::vector<std::uint8_t> xor_gamma(std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> out(data.begin(), data.end());
    std::uint32_t seed = 0;
    for (std::size_t i = 0; i + 4 <= out.size(); i += 4) {
        seed = (seed + 0x01309125u) * 0x03A452F7u;
        const std::uint32_t word = read_u32(out, i) ^ seed;
        out[i] = static_cast<std::uint8_t>(word);
        out[i + 1] = static_cast<std::uint8_t>(word >> 8);
        out[i + 2] = static_cast<std::uint8_t>(word >> 16);
        out[i + 3] = static_cast<std::uint8_t>(word >> 24);
    }
    return out;
}

Result<Layout> analyze(std::span<const std::uint8_t> raw, std::string_view game_path) {
    using R = Result<Layout>;
    Layout layout;
    layout.game_path = normalise(game_path);

    const FileSpec* spec = nullptr;
    for (const FileSpec& candidate : kFixedFiles) {
        if (layout.game_path == candidate.path) {
            spec = &candidate;
        }
    }
    const bool is_map = is_map_overlay_path(layout.game_path);
    if (spec == nullptr && !is_map) {
        return R::fail(layout.game_path + " is not a file with known text tables");
    }
    if (raw.empty()) {
        return R::fail("file is empty");
    }

    layout.base = is_map ? kMapBase : spec->base;
    layout.encrypted = !is_map && spec->encrypted;
    if (layout.encrypted) {
        if (raw.size() % 4 != 0) {
            return R::fail("encrypted file size is not a multiple of 4");
        }
        layout.image = xor_gamma(raw);
    } else {
        layout.image.assign(raw.begin(), raw.end());
    }
    const std::span<const std::uint8_t> image(layout.image);

    if (is_map) {
        auto table = find_map_messages(image, layout.base);
        if (!table) {
            return R::fail(table.error());
        }
        layout.tables.push_back(std::move(table.value()));
    } else {
        for (const TableSpec& t : spec->tables) {
            Table table;
            table.name = t.name;
            table.vram = t.vram;
            table.dialect = t.dialect;
            if (t.vram < layout.base || (t.vram - layout.base) % 4 != 0) {
                return R::fail("table " + table.name + " is not inside the file");
            }
            table.file_offset = t.vram - layout.base;
            if (static_cast<std::uint64_t>(table.file_offset) + 4ull * t.count > image.size()) {
                return R::fail("table " + table.name + " runs past the end of the file");
            }
            table.entries.resize(t.count);
            layout.tables.push_back(std::move(table));
        }
    }

    // Resolve every pointer. A slot belongs to the first entry that points at it.
    std::map<std::uint32_t, std::uint32_t> slot_by_offset;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> owners;  // per slot: table, id
    for (std::uint32_t t = 0; t < layout.tables.size(); ++t) {
        Table& table = layout.tables[t];
        for (std::uint32_t id = 0; id < table.entries.size(); ++id) {
            Entry& entry = table.entries[id];
            const std::uint32_t word = read_u32(image, table.file_offset + 4ull * id);
            if (word == 0) {
                entry.null = true;
                continue;
            }
            std::uint32_t offset = 0;
            if (!resolve_pointer(image, layout.base, word, static_cast<std::uint32_t>(image.size()), offset)) {
                return R::fail("table " + table.name + " entry " + std::to_string(id) + " does not point at a string");
            }
            const auto found = slot_by_offset.find(offset);
            if (found != slot_by_offset.end()) {
                entry.slot = found->second;
                entry.alias = true;
                entry.owner_table = owners[found->second].first;
                entry.owner_id = owners[found->second].second;
                continue;
            }
            const auto nul = std::find(image.begin() + offset, image.end(), std::uint8_t{0});
            Slot slot{};
            slot.offset = offset;
            slot.length = static_cast<std::uint32_t>(nul - (image.begin() + offset));
            entry.slot = static_cast<std::uint32_t>(layout.slots.size());
            slot_by_offset.emplace(offset, entry.slot);
            owners.emplace_back(t, id);
            layout.slots.push_back(slot);
        }
    }

    // The room of each slot: up to the next slot, the next 4-byte boundary and the first non-zero byte after the
    // NUL, so that a longer text never overwrites something else.
    for (auto it = slot_by_offset.begin(); it != slot_by_offset.end(); ++it) {
        Slot& slot = layout.slots[it->second];
        const std::uint64_t nul_at = static_cast<std::uint64_t>(slot.offset) + slot.length;
        const auto next = std::next(it);
        const std::uint64_t next_slot = next == slot_by_offset.end() ? image.size() : next->first;
        if (nul_at >= next_slot) {
            return R::fail("strings overlap at file offset " + std::to_string(slot.offset));
        }
        std::uint64_t end = std::min<std::uint64_t>(next_slot, (nul_at + 1 + 3) & ~3ull);
        std::uint64_t zeros = nul_at + 1;
        while (zeros < end && image[zeros] == 0) {
            ++zeros;
        }
        end = zeros;
        slot.capacity = static_cast<std::uint32_t>(end - slot.offset);
    }

    // A slot must not reach into a pointer table: the frame zeroes the slots.
    for (const Slot& slot : layout.slots) {
        for (const Table& table : layout.tables) {
            const std::uint64_t table_end = static_cast<std::uint64_t>(table.file_offset) + 4ull * table.entries.size();
            if (slot.offset < table_end && slot.offset + slot.capacity > table.file_offset) {
                return R::fail("a string overlaps table " + table.name);
            }
        }
    }
    return R::ok(std::move(layout));
}

}  // namespace sh::convert::text
