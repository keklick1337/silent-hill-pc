#include "engine/maps/map_table_data.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <stdexcept>

#include "engine/maps/map_tables.h"
#include "formats/common/json.h"

namespace sh::maps {

namespace {

using json::Value;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string at(const Value& v, const std::string& what) {
    return "line " + std::to_string(v.line()) + ": " + what;
}

bool hex_of(const Value* v, std::uint32_t* out) {
    if (v == nullptr || !v->is_string()) {
        return false;
    }
    char* end = nullptr;
    *out = static_cast<std::uint32_t>(std::strtoul(v->string().c_str(), &end, 16));
    return end != nullptr && *end == '\0' && !v->string().empty();
}

// One scalar field of a disc record.
struct Field {
    std::string_view name;
    std::uint16_t offset;
    std::uint8_t width;  // bytes: 1, 2 or 4
    bool is_signed;
};

struct Layout {
    std::uint16_t size;
    std::span<const Field> fields;
};

// The effect parameter blocks (engine/effects/map_effect_view.h, effect_link.h), in the disc layout.
constexpr Field kEmitterFields[] = {
    {"flags", 0, 1, false},        {"mode", 1, 1, false},        {"level_speed", 2, 1, false},
    {"spark_spread", 3, 1, false}, {"fade_phase", 4, 1, false},  {"fade_amp", 5, 1, false},
    {"spark_size", 6, 1, false},   {"wobble", 8, 2, false},      {"swirl", 10, 2, true},
    {"count", 12, 2, false},       {"speed", 14, 2, true},       {"level", 16, 2, true},
    {"top", 18, 2, true},          {"bottom", 20, 2, true},      {"height", 22, 2, true},
    {"half_x", 24, 2, true},       {"half_z", 26, 2, true},      {"radius", 28, 2, false},
    {"ember_x", 32, 4, true},      {"ember_z", 36, 4, true},
};
constexpr Field kSmokeFields[] = {
    {"shape", 0, 1, false},     {"size", 1, 1, false},       {"keep", 2, 1, false},     {"puffs", 4, 2, true},
    {"top", 6, 2, true},        {"base", 8, 2, true},        {"radius", 10, 2, true},   {"rise", 12, 2, true},
    {"fade_rate", 14, 2, true}, {"fade", 16, 2, true},       {"depth_bias", 18, 2, true},
    {"offset_x", 20, 4, true},  {"offset_z", 24, 4, true},   {"fade_delay", 28, 4, true},
};
constexpr Field kGlassFields[] = {{"x", 0, 4, true}, {"z", 4, 4, true}, {"size", 8, 2, true}};
constexpr Field kDripFields[] = {
    {"x", 0, 4, true},       {"z", 4, 4, true},       {"y", 8, 2, true},      {"angle", 10, 2, true},
    {"spread", 12, 2, true}, {"size", 14, 2, true},   {"period", 16, 2, true}, {"count", 18, 2, true},
    {"pattern", 20, 1, false},
};
// One of the water stream's four segments: offset of its first element, the element width.
constexpr Field kWaterSegmentFields[] = {
    {"x", 0, 4, true},       {"y", 16, 2, true},      {"z", 24, 4, true},      {"length", 40, 2, true},
    {"mode", 48, 1, false},  {"angle", 52, 2, true},  {"width", 60, 2, true},  {"step", 68, 2, true},
    {"flow", 76, 2, true},   {"phase", 84, 2, false}, {"spread", 92, 2, true}, {"ramp", 100, 2, true},
    {"jitter", 108, 2, true},
};
constexpr Field kWaterFields[] = {{"drops", 116, 2, true}, {"count", 120, 1, false}};
constexpr std::size_t kWaterSegments = 4;
constexpr std::uint16_t kWaterSize = 124;

constexpr Layout kEmitter = {40, kEmitterFields};
constexpr Layout kSmoke = {32, kSmokeFields};
constexpr Layout kGlass = {12, kGlassFields};
constexpr Layout kDrip = {24, kDripFields};

constexpr std::uint16_t kClipSize = 16;
constexpr std::uint16_t kRowSize = 8;

void check_keys(const Value& object, const std::string& where, std::initializer_list<std::string_view> known,
                std::span<const Field> fields = {}) {
    if (!object.is_object()) {
        fail(at(object, where + " must be an object"));
    }
    for (const auto& [key, value] : object.members()) {
        bool found = key == "description";
        for (std::string_view k : known) {
            found = found || key == k;
        }
        for (const Field& f : fields) {
            found = found || key == f.name;
        }
        if (!found) {
            fail(at(value, where + ": unknown key \"" + key + "\""));
        }
    }
}

const Value& member(const Value& object, const std::string& where, std::string_view key) {
    const Value* v = object.find(key);
    if (v == nullptr) {
        fail(at(object, where + ": missing \"" + std::string(key) + "\""));
    }
    return *v;
}

std::int64_t integer_in(const Value& v, const std::string& where, std::int64_t lo, std::int64_t hi) {
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(at(v, where + " must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi)));
    }
    return v.integer();
}

void put(std::vector<std::uint8_t>& out, std::size_t offset, std::int64_t value, unsigned width) {
    const auto u = static_cast<std::uint32_t>(value);
    for (unsigned i = 0; i < width; ++i) {
        out[offset + i] = static_cast<std::uint8_t>(u >> (8 * i));  // the disc is little-endian
    }
}

std::int64_t field_value(const Value& record, const std::string& where, const Field& f) {
    const std::int64_t lo = f.is_signed ? -(std::int64_t{1} << (8 * f.width - 1)) : 0;
    const std::int64_t hi = f.is_signed ? (std::int64_t{1} << (8 * f.width - 1)) - 1 : (std::int64_t{1} << (8 * f.width)) - 1;
    return integer_in(member(record, where, f.name), where + "." + std::string(f.name), lo, hi);
}

void put_record(std::vector<std::uint8_t>& out, std::size_t base, const Value& record, const std::string& where,
                std::span<const Field> fields, std::size_t stride_index = 0) {
    for (const Field& f : fields) {
        put(out, base + f.offset + stride_index * f.width, field_value(record, where, f), f.width);
    }
}

const std::vector<Value>& list_of(const Value& v, const std::string& where) {
    if (!v.is_array()) {
        fail(at(v, where + " must be a list"));
    }
    return v.array();
}

// The address of a game function the table's "functions" list names.
std::uint32_t function_va(const Value& root, std::string_view symbol, const std::string& where) {
    if (const Value* list = root.find("functions"); list != nullptr && list->is_array()) {
        for (const Value& f : list->array()) {
            const Value* name = f.find("symbol");
            std::uint32_t va = 0;
            if (name != nullptr && name->is_string() && name->string() == symbol && hex_of(f.find("va"), &va)) {
                return va;
            }
        }
    }
    fail(where + ": the table's \"functions\" do not list " + std::string(symbol));
}

std::string_view playback_function(const Value& v, const std::string& where) {
    if (v.is_string()) {
        if (v.string() == "blend") return "Anim_BlendLinear";
        if (v.string() == "once") return "Anim_PlaybackOnce";
        if (v.string() == "loop") return "Anim_PlaybackLoop";
    }
    fail(at(v, where + ".playback must be \"blend\", \"once\" or \"loop\""));
}

void encode_clips(const Value& root, const Value& clips, const std::string& where, std::vector<std::uint8_t>& out) {
    const std::vector<Value>& list = list_of(clips, where);
    out.assign(list.size() * kClipSize, 0);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Value& c = list[i];
        const std::string w = where + "[" + std::to_string(i) + "]";
        check_keys(c, w, {"playback", "status", "link", "speed", "speed_q12", "speed_function", "start_keyframe",
                          "end_keyframe"});
        const std::size_t e = i * kClipSize;
        put(out, e, function_va(root, playback_function(member(c, w, "playback"), w), w), 4);
        put(out, e + 4, integer_in(member(c, w, "status"), w + ".status", 0, 255), 1);
        put(out, e + 6, integer_in(member(c, w, "link"), w + ".link", 0, 255), 1);
        const Value* speed = c.find("speed");
        const Value* speed_q12 = c.find("speed_q12");
        const Value* speed_function = c.find("speed_function");
        if ((speed != nullptr) + (speed_q12 != nullptr) + (speed_function != nullptr) != 1) {
            fail(at(c, w + ": needs exactly one of \"speed\", \"speed_q12\" and \"speed_function\""));
        }
        if (speed_function != nullptr) {
            if (!speed_function->is_string() || speed_function->string() != "variable_duration") {
                fail(at(*speed_function, w + ".speed_function must be \"variable_duration\""));
            }
            put(out, e + 5, 1, 1);
            put(out, e + 8, function_va(root, "Player_VariableAnimDurationGet", w), 4);
        } else if (speed_q12 != nullptr) {
            put(out, e + 8, integer_in(*speed_q12, w + ".speed_q12", -0x7FFFFFFF - 1, 0x7FFFFFFF), 4);
        } else {
            if (!speed->is_number() || speed->number() < -65536.0 || speed->number() > 65536.0) {
                fail(at(*speed, w + ".speed must be a number -65536..65536"));
            }
            // Keyframes per second in Q12, truncated in single precision like the game's constants.
            volatile float q = static_cast<float>(speed->number()) * 4096.0f;
            put(out, e + 8, static_cast<std::int32_t>(q), 4);
        }
        put(out, e + 12, integer_in(member(c, w, "start_keyframe"), w + ".start_keyframe", -32768, 32767), 2);
        put(out, e + 14, integer_in(member(c, w, "end_keyframe"), w + ".end_keyframe", -32768, 32767), 2);
    }
}

void encode_rows(const Value& rows, const std::string& where, const TableNames& names,
                 std::vector<std::uint8_t>& out) {
    const std::vector<Value>& list = list_of(rows, where);
    out.assign(list.size() * kRowSize, 0);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Value& r = list[i];
        const std::string w = where + "[" + std::to_string(i) + "]";
        check_keys(r, w, {"reaction", "status", "next_status", "start_keyframe", "last_keyframe"});
        const Value& reaction = member(r, w, "reaction");
        std::int64_t id = -1;
        if (reaction.is_string()) {
            id = names.reaction(reaction.string());
            if (id < 0) {
                fail(at(reaction, w + ".reaction \"" + reaction.string() + "\" is no reaction of chara/Harry/reactions.json"));
            }
        } else {
            id = integer_in(reaction, w + ".reaction", 0, 32767);
        }
        const std::size_t e = i * kRowSize;
        put(out, e, integer_in(member(r, w, "status"), w + ".status", 0, 255), 1);
        put(out, e + 1, integer_in(member(r, w, "next_status"), w + ".next_status", 0, 255), 1);
        put(out, e + 2, id, 2);
        put(out, e + 4, integer_in(member(r, w, "start_keyframe"), w + ".start_keyframe", -32768, 32767), 2);
        put(out, e + 6, integer_in(member(r, w, "last_keyframe"), w + ".last_keyframe", -32768, 32767), 2);
    }
}

void encode_items(const Value& items, const std::string& where, const TableNames& names,
                  std::vector<std::uint8_t>& out) {
    const std::vector<Value>& list = list_of(items, where);
    out.assign(list.size(), 0);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Value& v = list[i];
        const int number = v.is_string() ? names.item(v.string()) : -1;
        if (number <= 0 || number > 255) {
            fail(at(v, where + "[" + std::to_string(i) + "] must name an item"));
        }
        out[i] = static_cast<std::uint8_t>(number);
    }
}

void encode_records(const Value& records, const std::string& where, const Layout& layout,
                    std::vector<std::uint8_t>& out) {
    const std::vector<Value>& list = list_of(records, where);
    out.assign(list.size() * layout.size, 0);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string w = where + "[" + std::to_string(i) + "]";
        check_keys(list[i], w, {}, layout.fields);
        put_record(out, i * layout.size, list[i], w, layout.fields);
    }
}

void encode_block(const Value& block, const std::string& where, const Layout& layout, std::vector<std::uint8_t>& out) {
    check_keys(block, where, {}, layout.fields);
    out.assign(layout.size, 0);
    put_record(out, 0, block, where, layout.fields);
}

void encode_water(const Value& block, const std::string& where, std::vector<std::uint8_t>& out) {
    check_keys(block, where, {"segments"}, kWaterFields);
    out.assign(kWaterSize, 0);
    put_record(out, 0, block, where, kWaterFields);
    const std::string segments_where = where + ".segments";
    const Value& segments_value = member(block, where, "segments");
    const std::vector<Value>& segments = list_of(segments_value, segments_where);
    if (segments.size() != kWaterSegments) {
        fail(at(block, where + ".segments must list 4 segments"));
    }
    for (std::size_t i = 0; i < kWaterSegments; ++i) {
        const std::string w = where + ".segments[" + std::to_string(i) + "]";
        check_keys(segments[i], w, {}, kWaterSegmentFields);
        put_record(out, 0, segments[i], w, kWaterSegmentFields, i);
    }
}

}  // namespace

Result<TableDataResult> apply_table_data(std::string_view tables_json, std::span<const std::uint8_t> overlay,
                                         const TableNames& names) try {
    const Result<Value> doc = json::parse(tables_json);
    if (!doc) {
        return Result<TableDataResult>::fail(doc.error());
    }
    const Value& root = doc.value();
    std::uint32_t base = kOverlayBase;
    if (const Value* b = root.find("base"); b != nullptr && !hex_of(b, &base)) {
        fail(at(*b, "\"base\" must be a hex address"));
    }
    TableDataResult result;
    result.overlay.assign(overlay.begin(), overlay.end());
    const Value* symbols = root.find("symbols");
    if (symbols == nullptr || !symbols->is_array()) {
        fail("needs \"symbols\"");
    }
    struct Write {
        std::size_t offset;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Write> writes;
    for (const Value& s : symbols->array()) {
        const Value* data = nullptr;
        std::string_view key;
        for (std::string_view k : kTableDataKeys) {
            if (const Value* v = s.find(k)) {
                if (data != nullptr) {
                    fail(at(*v, "a symbol carries one data key, not \"" + std::string(key) + "\" and \"" +
                                    std::string(k) + "\""));
                }
                data = v;
                key = k;
            }
        }
        if (data == nullptr) {
            continue;
        }
        const Value* name = s.find("symbol");
        const std::string where = (name != nullptr && name->is_string() ? name->string() : std::string("?")) + "." +
                                  std::string(key);
        std::uint32_t va = 0;
        if (!hex_of(s.find("va"), &va) || va < base) {
            fail(at(s, where + ": the symbol needs its \"va\" inside the overlay"));
        }
        std::vector<std::uint8_t> bytes;
        if (key == "items") {
            encode_items(*data, where, names, bytes);
        } else if (key == "clips") {
            encode_clips(root, *data, where, bytes);
        } else if (key == "reaction_rows") {
            encode_rows(*data, where, names, bytes);
        } else if (key == "particle_emitter") {
            encode_block(*data, where, kEmitter, bytes);
        } else if (key == "smoke_column") {
            encode_block(*data, where, kSmoke, bytes);
        } else if (key == "water_stream") {
            encode_water(*data, where, bytes);
        } else if (key == "glass_panes") {
            encode_records(*data, where, kGlass, bytes);
        } else {
            encode_records(*data, where, kDrip, bytes);
        }
        // The array's room: what the disc gives it (bytes, else count elements); the data must fit and the rest
        // of the room is zero.
        const Value* count = s.find("count");
        const Value* room_bytes = s.find("bytes");
        std::size_t room = 0;
        if (room_bytes != nullptr && room_bytes->is_integer() && room_bytes->integer() > 0) {
            room = static_cast<std::size_t>(room_bytes->integer());
        } else if (count != nullptr && count->is_integer() && count->integer() > 0) {
            const std::size_t element = key == "clips" ? kClipSize : key == "reaction_rows" ? kRowSize : 1;
            room = static_cast<std::size_t>(count->integer()) * element;
        }
        if (bytes.size() > room) {
            fail(at(*data, where + ": " + std::to_string(bytes.size()) + " bytes of data do not fit the array's " +
                               std::to_string(room)));
        }
        bytes.resize(room, 0);
        writes.push_back({va - base, std::move(bytes)});
    }
    // Larger arrays first: an array that lies inside another one's room (map1_s03's drips in the water stream's)
    // keeps its own data over the other's zero fill.
    std::stable_sort(writes.begin(), writes.end(),
                     [](const Write& a, const Write& b) { return a.bytes.size() > b.bytes.size(); });
    for (const Write& w : writes) {
        if (result.overlay.size() < w.offset + w.bytes.size()) {
            result.overlay.resize(w.offset + w.bytes.size(), 0);
        }
        std::memcpy(result.overlay.data() + w.offset, w.bytes.data(), w.bytes.size());
        ++result.arrays;
    }
    return Result<TableDataResult>::ok(std::move(result));
} catch (const std::exception& e) {
    return Result<TableDataResult>::fail(e.what());
}

}  // namespace sh::maps
