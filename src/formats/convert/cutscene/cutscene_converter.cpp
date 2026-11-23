#include "formats/convert/cutscene/cutscene_converter.h"

#include "formats/common/crc32.h"
#include "formats/common/json.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace sh::convert::cutscene {
namespace {

constexpr std::size_t kHeaderBytes = 0x2C;
constexpr std::size_t kEntryBytes = 16;
constexpr std::size_t kHoldBytes = 6;
constexpr std::size_t kCharacterKeyBytes = 12;
constexpr std::size_t kCameraKeyBytes = 16;

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v);
    b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

bool in_range(std::size_t offset, std::size_t bytes, std::size_t size) {
    return offset <= size && bytes <= size - offset;
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (const std::uint8_t b : bytes) {
        s.push_back(digits[b >> 4]);
        s.push_back(digits[b & 15]);
    }
    return s;
}

bool from_hex(std::string_view s, std::vector<std::uint8_t>& out) {
    if (s.size() % 2 != 0) {
        return false;
    }
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < s.size(); i += 2) {
        const int hi = nibble(s[i]), lo = nibble(s[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
    }
    return true;
}

Result<DmsEntry> read_entry(std::span<const std::uint8_t> raw, std::size_t at, bool camera, std::vector<bool>& covered) {
    using Out = Result<DmsEntry>;
    if (!in_range(at, kEntryBytes, raw.size())) {
        return Out::fail("DMS: entry outside the file");
    }
    const std::uint8_t* p = raw.data() + at;
    DmsEntry e;
    e.offset = static_cast<std::uint32_t>(at);
    e.keyframe_count = static_cast<std::int16_t>(le16(p));
    e.hold_count = p[2];
    e.pad3 = p[3];
    std::copy(p + 4, p + 8, e.name.begin());
    e.holds_offset = le32(p + 8);
    e.keyframes_offset = le32(p + 12);
    std::fill(covered.begin() + static_cast<std::ptrdiff_t>(at), covered.begin() + static_cast<std::ptrdiff_t>(at + kEntryBytes), true);
    if (e.keyframe_count < 0) {
        return Out::fail("DMS: negative keyframe count");
    }
    const std::size_t stride = camera ? kCameraKeyBytes : kCharacterKeyBytes;
    if (!in_range(e.holds_offset, e.hold_count * kHoldBytes, raw.size()) ||
        !in_range(e.keyframes_offset, static_cast<std::size_t>(e.keyframe_count) * stride, raw.size())) {
        return Out::fail("DMS: entry data outside the file");
    }
    for (std::size_t i = 0; i < e.hold_count; ++i) {
        const std::uint8_t* h = raw.data() + e.holds_offset + i * kHoldBytes;
        e.holds.push_back({static_cast<std::int16_t>(le16(h)), static_cast<std::int16_t>(le16(h + 2)),
                           static_cast<std::int16_t>(le16(h + 4))});
    }
    for (std::size_t k = 0; k < static_cast<std::size_t>(e.keyframe_count); ++k) {
        const std::uint8_t* h = raw.data() + e.keyframes_offset + k * stride;
        std::array<std::int16_t, 8> key{};
        for (std::size_t i = 0; i < stride / 2; ++i) {
            key[i] = static_cast<std::int16_t>(le16(h + i * 2));
        }
        e.keyframes.push_back(key);
    }
    const auto mark = [&](std::size_t from, std::size_t bytes) {
        std::fill(covered.begin() + static_cast<std::ptrdiff_t>(from),
                  covered.begin() + static_cast<std::ptrdiff_t>(from + bytes), true);
    };
    mark(e.holds_offset, e.hold_count * kHoldBytes);
    mark(e.keyframes_offset, static_cast<std::size_t>(e.keyframe_count) * stride);
    return Out::ok(std::move(e));
}

void write_entry(std::vector<std::uint8_t>& b, const DmsEntry& e, bool camera) {
    put16(b, e.offset, static_cast<std::uint16_t>(e.keyframe_count));
    b[e.offset + 2] = e.hold_count;
    b[e.offset + 3] = e.pad3;
    std::copy(e.name.begin(), e.name.end(), b.begin() + e.offset + 4);
    put32(b, e.offset + 8, e.holds_offset);
    put32(b, e.offset + 12, e.keyframes_offset);
    for (std::size_t i = 0; i < e.holds.size(); ++i) {
        const std::size_t at = e.holds_offset + i * kHoldBytes;
        put16(b, at, static_cast<std::uint16_t>(e.holds[i].start));
        put16(b, at + 2, static_cast<std::uint16_t>(e.holds[i].end));
        put16(b, at + 4, static_cast<std::uint16_t>(e.holds[i].keyframe));
    }
    const std::size_t stride = camera ? kCameraKeyBytes : kCharacterKeyBytes;
    for (std::size_t k = 0; k < e.keyframes.size(); ++k) {
        for (std::size_t i = 0; i < stride / 2; ++i) {
            put16(b, e.keyframes_offset + k * stride + i * 2, static_cast<std::uint16_t>(e.keyframes[k][i]));
        }
    }
}

Result<void> check_entry(const DmsEntry& e, bool camera, std::size_t size) {
    const std::size_t stride = camera ? kCameraKeyBytes : kCharacterKeyBytes;
    if (e.holds.size() != e.hold_count || e.keyframes.size() != static_cast<std::size_t>(std::max<int>(e.keyframe_count, 0)) ||
        !in_range(e.offset, kEntryBytes, size) || !in_range(e.holds_offset, e.holds.size() * kHoldBytes, size) ||
        !in_range(e.keyframes_offset, e.keyframes.size() * stride, size)) {
        return Result<void>::fail("DMS: entry counts or offsets do not fit the file");
    }
    return Result<void>::ok();
}

std::string printable_name(const std::array<std::uint8_t, 4>& name) {
    std::string s;
    for (const std::uint8_t c : name) {
        if (c == 0) {
            break;
        }
        s.push_back(std::isprint(c) ? static_cast<char>(c) : '?');
    }
    return s;
}

void write_entry_json(json::Writer& w, const DmsEntry& e, bool camera) {
    w.begin_object();
    w.member("name", printable_name(e.name));
    w.member("name_hex", to_hex(e.name));
    w.member("offset", e.offset);
    w.member("pad3", e.pad3);
    w.member("holds_offset", e.holds_offset);
    w.member("keyframes_offset", e.keyframes_offset);
    w.key("holds").begin_array();
    for (const DmsHoldRange& h : e.holds) {
        w.inline_next().begin_array().value(h.start).value(h.end).value(h.keyframe).end_array();
    }
    w.end_array();
    w.key("keyframes").begin_array();
    const std::size_t n = camera ? 8 : 6;
    for (const auto& k : e.keyframes) {
        w.inline_next().begin_array();
        for (std::size_t i = 0; i < n; ++i) {
            w.value(k[i]);
        }
        w.end_array();
    }
    w.end_array();
    w.end_object();
}

bool get_int(const json::Value* obj, const char* key, std::int64_t lo, std::int64_t hi, std::int64_t& out) {
    const json::Value* v = obj ? obj->find(key) : nullptr;
    if (!v || !v->is_integer() || v->integer() < lo || v->integer() > hi) {
        return false;
    }
    out = v->integer();
    return true;
}

Result<DmsEntry> read_entry_json(const json::Value* v, bool camera) {
    using Out = Result<DmsEntry>;
    if (!v || !v->is_object()) {
        return Out::fail("sh-cutscene: entry is not an object");
    }
    DmsEntry e;
    std::int64_t x = 0;
    if (!get_int(v, "offset", 0, 0xFFFFFFFF, x)) return Out::fail("sh-cutscene: entry offset");
    e.offset = static_cast<std::uint32_t>(x);
    if (!get_int(v, "pad3", 0, 255, x)) return Out::fail("sh-cutscene: entry pad3");
    e.pad3 = static_cast<std::uint8_t>(x);
    if (!get_int(v, "holds_offset", 0, 0xFFFFFFFF, x)) return Out::fail("sh-cutscene: holds_offset");
    e.holds_offset = static_cast<std::uint32_t>(x);
    if (!get_int(v, "keyframes_offset", 0, 0xFFFFFFFF, x)) return Out::fail("sh-cutscene: keyframes_offset");
    e.keyframes_offset = static_cast<std::uint32_t>(x);
    const json::Value* name = v->find("name_hex");
    std::vector<std::uint8_t> nb;
    if (!name || !name->is_string() || !from_hex(name->string(), nb) || nb.size() != 4) {
        return Out::fail("sh-cutscene: entry name_hex must be 4 bytes of hex");
    }
    std::copy(nb.begin(), nb.end(), e.name.begin());
    const json::Value* holds = v->find("holds");
    const json::Value* keys = v->find("keyframes");
    if (!holds || !holds->is_array() || !keys || !keys->is_array()) {
        return Out::fail("sh-cutscene: entry needs holds and keyframes arrays");
    }
    const auto s16 = [](const json::Value& x2, std::int16_t& out) {
        if (!x2.is_integer() || x2.integer() < -32768 || x2.integer() > 32767) {
            return false;
        }
        out = static_cast<std::int16_t>(x2.integer());
        return true;
    };
    for (const json::Value& h : holds->array()) {
        DmsHoldRange r;
        if (!h.is_array() || h.array().size() != 3 || !s16(h.array()[0], r.start) || !s16(h.array()[1], r.end) ||
            !s16(h.array()[2], r.keyframe)) {
            return Out::fail("sh-cutscene: a hold range is [start, end, keyframe]");
        }
        e.holds.push_back(r);
    }
    const std::size_t n = camera ? 8 : 6;
    for (const json::Value& k : keys->array()) {
        std::array<std::int16_t, 8> key{};
        if (!k.is_array() || k.array().size() != n) {
            return Out::fail("sh-cutscene: a keyframe has the wrong number of values");
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (!s16(k.array()[i], key[i])) {
                return Out::fail("sh-cutscene: keyframe value outside s16");
            }
        }
        e.keyframes.push_back(key);
    }
    if (e.holds.size() > 255 || e.keyframes.size() > 32767) {
        return Out::fail("sh-cutscene: too many holds or keyframes");
    }
    e.hold_count = static_cast<std::uint8_t>(e.holds.size());
    e.keyframe_count = static_cast<std::int16_t>(e.keyframes.size());
    return Out::ok(std::move(e));
}

bool is_dms_path(std::string_view path) {
    std::string u(path.size() >= 4 ? path.substr(path.size() - 4) : path);
    std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return u == ".DMS";
}

class CutsceneConverter final : public sh::convert::Converter {
public:
    std::string_view name() const override { return "cutscene"; }
    bool lossless() const override { return true; }
    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        return is_dms_path(game_path) && looks_like_dms(raw);
    }
    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        using Out = Result<std::vector<Output>>;
        auto dms = parse_dms(raw);
        if (!dms) {
            return Out::fail(std::string(game_path) + ": " + dms.error());
        }
        const std::string json = dms_to_json(dms.value(), game_path, raw);
        std::string rel(game_path);
        rel = "cutscenes/" + rel.substr(0, rel.rfind('.')) + ".dms.json";
        std::vector<Output> out;
        out.push_back({rel, std::vector<std::uint8_t>(json.begin(), json.end())});
        return Out::ok(std::move(out));
    }
    Result<std::vector<std::uint8_t>> encode(std::span<const Output> outputs) const override {
        using Out = Result<std::vector<std::uint8_t>>;
        for (const Output& o : outputs) {
            if (o.relative_path.ends_with(".dms.json")) {
                auto dms = dms_from_json(std::string_view(reinterpret_cast<const char*>(o.bytes.data()), o.bytes.size()));
                if (!dms) {
                    return Out::fail(o.relative_path + ": " + dms.error());
                }
                return serialize_dms(dms.value());
            }
        }
        return Out::fail("cutscene: no .dms.json among the outputs");
    }
};

}  // namespace

bool looks_like_dms(std::span<const std::uint8_t> raw) {
    if (raw.size() < kHeaderBytes) {
        return false;
    }
    const std::uint8_t* d = raw.data();
    const std::size_t chars = d[1], segs = d[2];
    return in_range(le32(d + 8), segs * 4, raw.size()) && in_range(le32(d + 0x18), chars * kEntryBytes, raw.size()) &&
           static_cast<std::int16_t>(le16(d + 0x1C)) >= 0 &&
           in_range(le32(d + 0x24), d[0x1E] * kHoldBytes, raw.size()) &&
           in_range(le32(d + 0x28), static_cast<std::size_t>(le16(d + 0x1C)) * kCameraKeyBytes, raw.size());
}

Result<Dms> parse_dms(std::span<const std::uint8_t> raw) {
    using Out = Result<Dms>;
    if (!looks_like_dms(raw)) {
        return Out::fail("DMS: header offsets outside the file");
    }
    std::vector<bool> covered(raw.size(), false);
    const std::uint8_t* d = raw.data();
    Dms dms;
    dms.size = static_cast<std::uint32_t>(raw.size());
    dms.is_loaded = d[0];
    dms.unused_3 = d[3];
    dms.unused_4 = le32(d + 4);
    dms.segments_offset = le32(d + 8);
    for (std::size_t i = 0; i < 3; ++i) {
        dms.origin[i] = static_cast<std::int32_t>(le32(d + 12 + i * 4));
    }
    dms.characters_offset = le32(d + 0x18);
    std::fill(covered.begin(), covered.begin() + 0x1C, true);
    auto camera = read_entry(raw, 0x1C, true, covered);
    if (!camera) {
        return Out::fail(camera.error());
    }
    dms.camera = std::move(camera.value());
    for (std::size_t i = 0; i < d[2]; ++i) {
        const std::uint8_t* s = d + dms.segments_offset + i * 4;
        dms.segments.push_back({static_cast<std::int16_t>(le16(s)), static_cast<std::int16_t>(le16(s + 2))});
    }
    std::fill(covered.begin() + dms.segments_offset, covered.begin() + dms.segments_offset + d[2] * 4, true);
    for (std::size_t i = 0; i < d[1]; ++i) {
        auto e = read_entry(raw, dms.characters_offset + i * kEntryBytes, false, covered);
        if (!e) {
            return Out::fail(e.error());
        }
        dms.characters.push_back(std::move(e.value()));
    }
    for (std::size_t i = 0; i < raw.size();) {
        if (covered[i]) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < raw.size() && !covered[j]) {
            ++j;
        }
        dms.fill.push_back({static_cast<std::uint32_t>(i), std::vector<std::uint8_t>(raw.begin() + static_cast<std::ptrdiff_t>(i), raw.begin() + static_cast<std::ptrdiff_t>(j))});
        i = j;
    }
    auto back = serialize_dms(dms);
    if (!back || back.value() != std::vector<std::uint8_t>(raw.begin(), raw.end())) {
        return Out::fail("DMS: blocks overlap inconsistently; the file does not rebuild");
    }
    return Out::ok(std::move(dms));
}

Result<std::vector<std::uint8_t>> serialize_dms(const Dms& dms) {
    using Out = Result<std::vector<std::uint8_t>>;
    const std::size_t size = dms.size;
    if (size < kHeaderBytes || dms.segments.size() > 255 || dms.characters.size() > 255 ||
        !in_range(dms.segments_offset, dms.segments.size() * 4, size) ||
        !in_range(dms.characters_offset, dms.characters.size() * kEntryBytes, size)) {
        return Out::fail("DMS: header counts or offsets do not fit the file");
    }
    if (auto ok = check_entry(dms.camera, true, size); !ok) {
        return Out::fail(ok.error());
    }
    for (std::size_t i = 0; i < dms.characters.size(); ++i) {
        if (auto ok = check_entry(dms.characters[i], false, size); !ok) {
            return Out::fail(ok.error());
        }
        if (dms.characters[i].offset != dms.characters_offset + i * kEntryBytes) {
            return Out::fail("DMS: character entries must follow each other from characters_offset");
        }
    }
    if (dms.camera.offset != 0x1C) {
        return Out::fail("DMS: the camera entry lies at 0x1C");
    }
    std::vector<std::uint8_t> b(size, 0);
    for (const Dms::Fill& f : dms.fill) {
        if (!in_range(f.offset, f.bytes.size(), size)) {
            return Out::fail("DMS: fill outside the file");
        }
        std::copy(f.bytes.begin(), f.bytes.end(), b.begin() + f.offset);
    }
    b[0] = dms.is_loaded;
    b[1] = static_cast<std::uint8_t>(dms.characters.size());
    b[2] = static_cast<std::uint8_t>(dms.segments.size());
    b[3] = dms.unused_3;
    put32(b, 4, dms.unused_4);
    put32(b, 8, dms.segments_offset);
    for (std::size_t i = 0; i < 3; ++i) {
        put32(b, 12 + i * 4, static_cast<std::uint32_t>(dms.origin[i]));
    }
    put32(b, 0x18, dms.characters_offset);
    for (std::size_t i = 0; i < dms.segments.size(); ++i) {
        put16(b, dms.segments_offset + i * 4, static_cast<std::uint16_t>(dms.segments[i][0]));
        put16(b, dms.segments_offset + i * 4 + 2, static_cast<std::uint16_t>(dms.segments[i][1]));
    }
    write_entry(b, dms.camera, true);
    for (const DmsEntry& e : dms.characters) {
        write_entry(b, e, false);
    }
    return Out::ok(std::move(b));
}

std::string dms_to_json(const Dms& dms, std::string_view game_path, std::span<const std::uint8_t> raw) {
    json::Writer w(true);
    w.begin_object();
    w.member("format", "sh-cutscene/1");
    w.key("source").inline_next().begin_object();
    w.member("path", std::string(game_path));
    w.member("size", raw.size());
    char crc[16];
    std::snprintf(crc, sizeof crc, "%08X", crc32(raw.data(), raw.size()));
    w.member("crc32", crc);
    w.end_object();
    int frames = 0;
    for (const auto& s : dms.segments) {
        frames = std::max(frames, s[0] + s[1]);
    }
    w.member("frames", frames);
    w.member("frames_per_second", 30);
    w.key("units").begin_object();
    w.member("origin", "Q8 world position (256 = 1 m, PS1 axes: +X right, +Y down, +Z forward)");
    w.member("character_keyframe", "[x, y, z, rx, ry, rz]: position Q8 relative to origin, angles Q12 (4096 = 360 deg)");
    w.member("camera_keyframe",
             "[px, py, pz, lx, ly, lz, unused_angle, projection]: position and look-at targets Q8 relative to origin, "
             "an unused Q12 angle, the projection distance (vcChangeProjectionValue)");
    w.member("holds", "[start frame, end frame, keyframe]: frames start..end (inclusive) show that keyframe");
    w.member("segments", "[start frame, frame count]: playback segments (Dms_SegmentStateGet)");
    w.end_object();
    w.key("header").inline_next().begin_object();
    w.member("is_loaded", dms.is_loaded);
    w.member("unused_3", dms.unused_3);
    w.member("unused_4", dms.unused_4);
    w.member("segments_offset", dms.segments_offset);
    w.member("characters_offset", dms.characters_offset);
    w.key("origin").begin_array().value(dms.origin[0]).value(dms.origin[1]).value(dms.origin[2]).end_array();
    w.end_object();
    w.key("segments").begin_array();
    for (const auto& s : dms.segments) {
        w.inline_next().begin_array().value(s[0]).value(s[1]).end_array();
    }
    w.end_array();
    w.key("camera");
    write_entry_json(w, dms.camera, true);
    w.key("characters").begin_array();
    for (const DmsEntry& e : dms.characters) {
        write_entry_json(w, e, false);
    }
    w.end_array();
    w.member("size", dms.size);
    w.key("fill").begin_array();
    for (const Dms::Fill& f : dms.fill) {
        w.inline_next().begin_object();
        w.member("offset", f.offset);
        if (std::all_of(f.bytes.begin(), f.bytes.end(), [](std::uint8_t b) { return b == 0; })) {
            w.member("zeros", f.bytes.size());
        } else {
            w.member("hex", to_hex(f.bytes));
        }
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.take() + "\n";
}

Result<Dms> dms_from_json(std::string_view json_text) {
    using Out = Result<Dms>;
    auto parsed = json::parse(json_text);
    if (!parsed) {
        return Out::fail("sh-cutscene: " + parsed.error());
    }
    const json::Value& doc = parsed.value();
    const json::Value* format = doc.find("format");
    if (!format || !format->is_string() || format->string() != "sh-cutscene/1") {
        return Out::fail("sh-cutscene: not an sh-cutscene/1 document");
    }
    Dms dms;
    std::int64_t x = 0;
    if (!get_int(&doc, "size", kHeaderBytes, 0x7FFFFFFF, x)) return Out::fail("sh-cutscene: size");
    dms.size = static_cast<std::uint32_t>(x);
    const json::Value* h = doc.find("header");
    if (!get_int(h, "is_loaded", 0, 255, x)) return Out::fail("sh-cutscene: header.is_loaded");
    dms.is_loaded = static_cast<std::uint8_t>(x);
    if (!get_int(h, "unused_3", 0, 255, x)) return Out::fail("sh-cutscene: header.unused_3");
    dms.unused_3 = static_cast<std::uint8_t>(x);
    if (!get_int(h, "unused_4", 0, 0xFFFFFFFF, x)) return Out::fail("sh-cutscene: header.unused_4");
    dms.unused_4 = static_cast<std::uint32_t>(x);
    if (!get_int(h, "segments_offset", 0, 0xFFFFFFFF, x)) return Out::fail("sh-cutscene: header.segments_offset");
    dms.segments_offset = static_cast<std::uint32_t>(x);
    if (!get_int(h, "characters_offset", 0, 0xFFFFFFFF, x)) return Out::fail("sh-cutscene: header.characters_offset");
    dms.characters_offset = static_cast<std::uint32_t>(x);
    const json::Value* origin = h ? h->find("origin") : nullptr;
    if (!origin || !origin->is_array() || origin->array().size() != 3) {
        return Out::fail("sh-cutscene: header.origin");
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const json::Value& o = origin->array()[i];
        if (!o.is_integer() || o.integer() < INT32_MIN || o.integer() > INT32_MAX) {
            return Out::fail("sh-cutscene: header.origin");
        }
        dms.origin[i] = static_cast<std::int32_t>(o.integer());
    }
    const json::Value* segs = doc.find("segments");
    if (!segs || !segs->is_array()) {
        return Out::fail("sh-cutscene: segments");
    }
    for (const json::Value& s : segs->array()) {
        if (!s.is_array() || s.array().size() != 2 || !s.array()[0].is_integer() || !s.array()[1].is_integer()) {
            return Out::fail("sh-cutscene: a segment is [start, frames]");
        }
        dms.segments.push_back({static_cast<std::int16_t>(s.array()[0].integer()), static_cast<std::int16_t>(s.array()[1].integer())});
    }
    auto cam = read_entry_json(doc.find("camera"), true);
    if (!cam) {
        return Out::fail(cam.error());
    }
    dms.camera = std::move(cam.value());
    const json::Value* chars = doc.find("characters");
    if (!chars || !chars->is_array()) {
        return Out::fail("sh-cutscene: characters");
    }
    for (const json::Value& c : chars->array()) {
        auto e = read_entry_json(&c, false);
        if (!e) {
            return Out::fail(e.error());
        }
        dms.characters.push_back(std::move(e.value()));
    }
    const json::Value* fill = doc.find("fill");
    if (fill && fill->is_array()) {
        for (const json::Value& f : fill->array()) {
            Dms::Fill run;
            if (!get_int(&f, "offset", 0, 0x7FFFFFFF, x)) return Out::fail("sh-cutscene: fill offset");
            run.offset = static_cast<std::uint32_t>(x);
            if (get_int(&f, "zeros", 0, 0x7FFFFFFF, x)) {
                run.bytes.assign(static_cast<std::size_t>(x), 0);
            } else {
                const json::Value* hex = f.find("hex");
                if (!hex || !hex->is_string() || !from_hex(hex->string(), run.bytes)) {
                    return Out::fail("sh-cutscene: fill needs zeros or hex");
                }
            }
            dms.fill.push_back(std::move(run));
        }
    }
    return Out::ok(std::move(dms));
}

std::unique_ptr<sh::convert::Converter> make_converter() { return std::make_unique<CutsceneConverter>(); }

}  // namespace sh::convert::cutscene
