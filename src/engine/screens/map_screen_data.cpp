// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/map_screen_data.h"

#include <stdexcept>

#include "engine/screens/screen_data.h"
#include "formats/common/json.h"

namespace sh::screens {

namespace {

using json::Value;

struct Bad : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const Value& at, const std::string& what) { throw Bad(at_line(at) + what); }

// "_" keys and "description" are notes for people.
bool is_comment(const std::string& key) { return key.rfind("_", 0) == 0 || key == "description"; }

int read_int(const Value& v, const std::string& key, int lo, int hi) {
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(v, "\"" + key + "\" must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
    }
    return static_cast<int>(v.integer());
}

template <std::size_t N>
std::array<int, N> read_ints(const Value& v, const std::string& key, int lo, int hi) {
    if (!v.is_array() || v.array().size() != N) {
        fail(v, "\"" + key + "\" must be an array of " + std::to_string(N) + " integers");
    }
    std::array<int, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = read_int(v.array()[i], key, lo, hi);
    }
    return out;
}

MapRgb read_rgb(const Value& v, const std::string& key) {
    const std::array<int, 3> c = read_ints<3>(v, key, 0, 255);
    return {c[0], c[1], c[2]};
}

const std::string& read_name(const Value& v, const std::string& key) {
    if (!v.is_string() || v.string().empty()) {
        fail(v, "\"" + key + "\" must be a non-empty name");
    }
    return v.string();
}

const std::vector<Value::Member>& members_of(const Value& v, const char* key) {
    if (!v.is_object()) {
        fail(v, std::string("\"") + key + "\" must be an object");
    }
    return v.members();
}

void parse_zoom(const Value& obj, MapScreenData& d) {
    for (const auto& [key, v] : members_of(obj, "zoom")) {
        if (is_comment(key)) {
            continue;
        }
        if (key == "step") {
            d.zoom_step = read_int(v, key, 1, 4096);
        } else if (key == "focus_offset") {
            d.focus_offset = read_ints<2>(v, key, -1024, 1024);
        } else if (key == "pan_max") {
            d.pan_max = read_ints<2>(v, key, 0, 1024);
        } else if (key == "cursor_divisor") {
            d.cursor_divisor = read_int(v, key, 1, 100000);
        } else {
            fail(v, "unknown zoom key \"" + key + "\"");
        }
    }
}

void parse_player_arrow(const Value& obj, MapPlayerArrow& a) {
    for (const auto& [key, v] : members_of(obj, "player_arrow")) {
        if (is_comment(key)) {
            continue;
        }
        if (key == "tip") {
            a.tip = read_int(v, key, -256, 256);
        } else if (key == "back") {
            a.back = read_int(v, key, -256, 256);
        } else if (key == "half_width") {
            a.half_width = read_int(v, key, -256, 256);
        } else if (key == "outline") {
            a.outline = read_rgb(v, key);
        } else if (key == "tip_color") {
            a.tip_color = read_rgb(v, key);
        } else if (key == "back_color") {
            a.back_color = read_rgb(v, key);
        } else {
            fail(v, "unknown player_arrow key \"" + key + "\"");
        }
    }
}

void parse_page_arrows(const Value& obj, MapPageArrows& a) {
    for (const auto& [key, v] : members_of(obj, "page_arrows")) {
        if (is_comment(key)) {
            continue;
        }
        if (key == "tip_y") {
            a.tip_y = read_int(v, key, -1024, 1024);
        } else if (key == "base_y") {
            a.base_y = read_int(v, key, -1024, 1024);
        } else if (key == "half_width") {
            a.half_width = read_int(v, key, -1024, 1024);
        } else if (key == "tip_color") {
            a.tip_color = read_rgb(v, key);
        } else if (key == "base_color") {
            a.base_color = read_rgb(v, key);
        } else {
            fail(v, "unknown page_arrows key \"" + key + "\"");
        }
    }
}

void parse_links(const Value& obj, MapScreenData& d) {
    for (const auto& [page, v] : members_of(obj, "links")) {
        if (is_comment(page)) {
            continue;
        }
        MapPageLink link;
        link.page = page;
        for (const auto& [key, target] : members_of(v, page.c_str())) {
            if (is_comment(key)) {
                continue;
            }
            if (key == "up") {
                link.up = read_name(target, key);
            } else if (key == "down") {
                link.down = read_name(target, key);
            } else {
                fail(target, "unknown link key \"" + key + "\"");
            }
        }
        d.links.push_back(std::move(link));
    }
}

void parse_anchors(const Value& obj, MapScreenData& d) {
    for (const auto& [name, v] : members_of(obj, "player_anchors")) {
        if (is_comment(name)) {
            continue;
        }
        MapAnchorGrid g;
        g.name = name;
        const Value* first = nullptr;
        const Value* cells = nullptr;
        for (const auto& [key, field] : members_of(v, name.c_str())) {
            if (is_comment(key)) {
                continue;
            }
            if (key == "first_cell") {
                first = &field;
            } else if (key == "cells") {
                cells = &field;
            } else {
                fail(field, "unknown player_anchors key \"" + key + "\"");
            }
        }
        if (first == nullptr || cells == nullptr) {
            fail(v, "\"" + name + "\" needs first_cell and cells");
        }
        const std::array<int, 2> f = read_ints<2>(*first, "first_cell", -128, 127);
        g.first_x = f[0];
        g.first_z = f[1];
        if (!cells->is_array() || cells->array().empty() || cells->array().size() > 64) {
            fail(*cells, "\"cells\" must be an array of 1..64 rows");
        }
        for (const Value& row : cells->array()) {
            if (!row.is_array() || row.array().empty() || row.array().size() > 64) {
                fail(row, "a cells row must be an array of 1..64 [x, y] points");
            }
            if (g.size_x == 0) {
                g.size_z = static_cast<int>(row.array().size());
            } else if (static_cast<int>(row.array().size()) != g.size_z) {
                fail(row, "all cells rows must have the same length");
            }
            for (const Value& p : row.array()) {
                g.cells.push_back(read_ints<2>(p, "cells", -128, 127));
            }
            ++g.size_x;
        }
        d.anchors.push_back(std::move(g));
    }
}

void parse_atlases(const Value& obj, MapScreenData& d) {
    for (const auto& [name, v] : members_of(obj, "marking_atlases")) {
        if (is_comment(name)) {
            continue;
        }
        if (!v.is_array() || v.array().empty() || v.array().size() > 255) {
            fail(v, "atlas \"" + name + "\" must be an array of 1..255 sprites");
        }
        MapMarkingAtlas a;
        a.name = name;
        for (const Value& s : v.array()) {
            const std::array<int, 4> r = read_ints<4>(s, "sprite", 0, 255);
            a.sprites.push_back({r[0], r[1], r[2], r[3]});
        }
        d.atlases.push_back(std::move(a));
    }
}

void parse_markings(const Value& obj, MapScreenData& d) {
    for (const auto& [page, v] : members_of(obj, "markings")) {
        if (is_comment(page)) {
            continue;
        }
        MapPageMarks pm;
        pm.page = page;
        const Value* marks = nullptr;
        for (const auto& [key, field] : members_of(v, page.c_str())) {
            if (is_comment(key)) {
                continue;
            }
            if (key == "atlas") {
                pm.atlas = read_name(field, key);
            } else if (key == "marks") {
                marks = &field;
            } else {
                fail(field, "unknown markings key \"" + key + "\"");
            }
        }
        if (pm.atlas.empty() || marks == nullptr) {
            fail(v, "page \"" + page + "\" needs atlas and marks");
        }
        const MapMarkingAtlas* atlas = d.atlas(pm.atlas);
        if (atlas == nullptr) {
            fail(v, "page \"" + page + "\" names unknown atlas \"" + pm.atlas + "\"");
        }
        if (!marks->is_array() || marks->array().empty() || marks->array().size() > 1024) {
            fail(*marks, "\"marks\" must be an array of 1..1024 marks");
        }
        const int count = static_cast<int>(atlas->sprites.size());
        for (const Value& m : marks->array()) {
            MapMark mark;
            bool at = false, sprites = false, flags = false;
            for (const auto& [key, field] : members_of(m, "mark")) {
                if (is_comment(key)) {
                    continue;
                }
                if (key == "at") {
                    const std::array<int, 2> p = read_ints<2>(field, key, -128, 127);
                    mark.x = p[0];
                    mark.y = p[1];
                    at = true;
                } else if (key == "sprites") {
                    mark.sprites = read_ints<2>(field, key, 0, count);
                    sprites = true;
                } else if (key == "flags") {
                    if (!field.is_array() || field.array().size() != 2) {
                        fail(field, "\"flags\" must be the two flag names of the mark's halves");
                    }
                    mark.flags = {read_name(field.array()[0], key), read_name(field.array()[1], key)};
                    flags = true;
                } else {
                    fail(field, "unknown mark key \"" + key + "\"");
                }
            }
            if (!at || !sprites || !flags) {
                fail(m, "a mark needs at, sprites and flags");
            }
            pm.marks.push_back(mark);
        }
        d.markings.push_back(std::move(pm));
    }
}

void parse_page_files(const Value& obj, MapScreenData& d) {
    for (const auto& [page, v] : members_of(obj, "page_files")) {
        if (is_comment(page)) {
            continue;
        }
        MapPageFiles f;
        f.page = page;
        for (const auto& [key, field] : members_of(v, page.c_str())) {
            if (is_comment(key)) {
                continue;
            }
            if (key == "image") {
                f.image = read_name(field, key);
            } else if (key == "markings") {
                f.markings = read_name(field, key);
            } else {
                fail(field, "unknown page_files key \"" + key + "\"");
            }
        }
        if (f.image.empty()) {
            fail(v, "page \"" + page + "\" needs an image");
        }
        d.page_files.push_back(std::move(f));
    }
}

std::vector<std::string> read_names(const Value& v, const std::string& key) {
    if (!v.is_array() || v.array().empty()) {
        fail(v, "\"" + key + "\" must be a non-empty array of names");
    }
    std::vector<std::string> out;
    for (const Value& n : v.array()) {
        out.push_back(read_name(n, key));
    }
    return out;
}

std::vector<std::array<int, 2>> read_pairs(const Value& v, const std::string& key) {
    if (!v.is_array() || v.array().empty()) {
        fail(v, "\"" + key + "\" must be a non-empty array of [x, z]");
    }
    std::vector<std::array<int, 2>> out;
    for (const Value& p : v.array()) {
        out.push_back(read_ints<2>(p, key, -1000, 1000));
    }
    return out;
}

MapPlacementCoord read_coord(const Value& v, const std::string& key) {
    MapPlacementCoord c;
    if (v.is_integer()) {
        c.fixed = read_int(v, key, -32768, 32767);
        return c;
    }
    c.from_position = true;
    bool from = false;
    for (const auto& [k, field] : members_of(v, key.c_str())) {
        if (is_comment(k)) {
            continue;
        }
        if (k == "from") {
            const std::string& s = read_name(field, k);
            if (s != "x" && s != "-x" && s != "z" && s != "-z") {
                fail(field, "\"from\" must be x, -x, z or -z");
            }
            c.negate = s[0] == '-';
            c.axis = s.back();
            from = true;
        } else if (k == "add") {
            c.add_metres = read_int(field, k, -100000, 100000);
        } else if (k == "shift") {
            c.shift = read_int(field, k, 0, 30);
        } else if (k == "offset") {
            c.offset = read_int(field, k, -32768, 32767);
        } else {
            fail(field, "unknown " + key + " key \"" + k + "\"");
        }
    }
    if (!from) {
        fail(v, "\"" + key + "\" needs from");
    }
    return c;
}

MapPlacementAnchor read_anchor(const Value& v) {
    MapPlacementAnchor a;
    for (const auto& [k, field] : members_of(v, "anchor")) {
        if (is_comment(k)) {
            continue;
        }
        if (k == "grid") {
            a.grid = read_name(field, k);
        } else if (k == "x" || k == "z") {
            const std::array<int, 2> r = read_ints<2>(field, k, -1000000, 1000000);
            if (r[1] == 0) {
                fail(field, "\"" + k + "\" divides by 0");
            }
            (k == "x" ? a.x : a.z) = r;
        } else {
            fail(field, "unknown anchor key \"" + k + "\"");
        }
    }
    if (a.grid.empty()) {
        fail(v, "\"anchor\" needs grid");
    }
    return a;
}

MapPlacementRule read_rule(const Value& v, const MapScreenData& d) {
    MapPlacementRule r;
    bool x = false, z = false;
    for (const auto& [key, field] : members_of(v, "rule")) {
        if (is_comment(key)) {
            continue;
        }
        if (key == "maps") {
            r.maps = read_names(field, key);
        } else if (key == "cells") {
            r.cells = read_pairs(field, key);
        } else if (key == "chunks") {
            r.chunks = read_pairs(field, key);
        } else if (key == "cell_z_below") {
            r.cell_z_below = read_int(field, key, -1000, 1000);
        } else if (key == "cell_x_in") {
            r.cell_x_in = read_ints<2>(field, key, -1000, 1000);
        } else if (key == "anchor") {
            r.anchor = read_anchor(field);
            if (d.anchor_grid(r.anchor->grid) == nullptr) {
                fail(field, "unknown anchor grid \"" + r.anchor->grid + "\"");
            }
        } else if (key == "x") {
            r.x = read_coord(field, key);
            x = true;
        } else if (key == "z") {
            r.z = read_coord(field, key);
            z = true;
        } else if (key == "turn") {
            r.turn_degrees = read_int(field, key, -360, 360);
        } else {
            fail(field, "unknown rule key \"" + key + "\"");
        }
    }
    if (r.anchor ? (x || z) : !(x && z)) {
        fail(v, "a rule needs either x and z or anchor");
    }
    return r;
}

void parse_placement(const Value& obj, MapScreenData& d) {
    if (!obj.is_array()) {
        fail(obj, "\"player_placement\" must be an array of groups");
    }
    for (const Value& g : obj.array()) {
        MapPlacementGroup group;
        const Value* rules = nullptr;
        for (const auto& [key, field] : members_of(g, "group")) {
            if (is_comment(key)) {
                continue;
            }
            if (key == "pages") {
                group.pages = read_names(field, key);
            } else if (key == "rules") {
                rules = &field;
            } else {
                fail(field, "unknown placement group key \"" + key + "\"");
            }
        }
        if (group.pages.empty() || rules == nullptr || !rules->is_array()) {
            fail(g, "a placement group needs pages and an array of rules");
        }
        for (const Value& r : rules->array()) {
            group.rules.push_back(read_rule(r, d));
        }
        d.placement.push_back(std::move(group));
    }
}

template <class T>
void no_duplicates(const std::vector<T>& list, std::string T::*name, const char* what) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        for (std::size_t j = i + 1; j < list.size(); ++j) {
            if (list[i].*name == list[j].*name) {
                throw Bad(std::string(what) + " \"" + list[i].*name + "\" is listed twice");
            }
        }
    }
}

}  // namespace

std::optional<std::array<int, 2>> MapAnchorGrid::at(int x, int z) const {
    const int ix = x - first_x;
    const int iz = z - first_z;
    if (ix < 0 || iz < 0 || ix >= size_x || iz >= size_z) {
        return std::nullopt;
    }
    return cells[static_cast<std::size_t>(ix * size_z + iz)];
}

const MapAnchorGrid* MapScreenData::anchor_grid(std::string_view name) const {
    for (const MapAnchorGrid& g : anchors) {
        if (g.name == name) {
            return &g;
        }
    }
    return nullptr;
}

const MapMarkingAtlas* MapScreenData::atlas(std::string_view name) const {
    for (const MapMarkingAtlas& a : atlases) {
        if (a.name == name) {
            return &a;
        }
    }
    return nullptr;
}

Result<MapScreenData> parse_map_screen(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<MapScreenData>::fail(doc.error());
    }
    const Value& root = doc.value();
    if (!root.is_object()) {
        return Result<MapScreenData>::fail("map screen data must be an object");
    }
    MapScreenData d;
    // Atlases and anchor grids first: marks and placement rules are checked against the ones they name.
    if (const Value* atlases = root.find("marking_atlases")) {
        parse_atlases(*atlases, d);
    }
    if (const Value* anchors = root.find("player_anchors")) {
        parse_anchors(*anchors, d);
    }
    bool format = false;
    for (const auto& [key, v] : root.members()) {
        if (is_comment(key) || key == "marking_atlases" || key == "player_anchors") {
            continue;
        }
        if (key == "format") {
            if (!v.is_string() || v.string() != "sh-map-screen") {
                fail(v, "\"format\" must be \"sh-map-screen\"");
            }
            format = true;
        } else if (key == "version") {
            read_int(v, key, 1, 1);
        } else if (key == "zoom") {
            parse_zoom(v, d);
        } else if (key == "image_shade") {
            d.image_shade = read_int(v, key, 0, 255);
        } else if (key == "marking_shade") {
            d.marking_shade = read_int(v, key, 0, 255);
        } else if (key == "player_arrow") {
            parse_player_arrow(v, d.player_arrow);
        } else if (key == "page_arrows") {
            parse_page_arrows(v, d.page_arrows);
        } else if (key == "links") {
            parse_links(v, d);
        } else if (key == "markings") {
            parse_markings(v, d);
        } else if (key == "page_files") {
            parse_page_files(v, d);
        } else if (key == "player_placement") {
            parse_placement(v, d);
        } else {
            fail(v, "unknown key \"" + key + "\"");
        }
    }
    if (!format) {
        return Result<MapScreenData>::fail("missing \"format\": \"sh-map-screen\"");
    }
    no_duplicates(d.links, &MapPageLink::page, "link page");
    no_duplicates(d.anchors, &MapAnchorGrid::name, "anchor grid");
    no_duplicates(d.atlases, &MapMarkingAtlas::name, "atlas");
    no_duplicates(d.markings, &MapPageMarks::page, "markings page");
    no_duplicates(d.page_files, &MapPageFiles::page, "page_files page");
    return Result<MapScreenData>::ok(std::move(d));
} catch (const std::exception& e) {
    return Result<MapScreenData>::fail(e.what());
}

Result<MapScreenData> load_map_screen(const std::filesystem::path& gamedata_dir,
                                      const std::filesystem::path& mods_dir, std::string* source) {
    std::string where;
    const Result<std::string> text =
        read_screen_file(gamedata_dir, mods_dir, std::filesystem::path("menu") / "map_screen.json", &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!text) {
        return Result<MapScreenData>::fail(text.error());
    }
    Result<MapScreenData> parsed = parse_map_screen(text.value());
    if (!parsed) {
        return Result<MapScreenData>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::screens
