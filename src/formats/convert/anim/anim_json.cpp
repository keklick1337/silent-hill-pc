#include "formats/convert/anim/anim_json.h"

#include "formats/common/crc32.h"
#include "formats/common/json.h"

#include <cstdio>

namespace sh::convert::anim {
namespace {

std::string to_hex(std::span<const std::uint8_t> bytes) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        s.push_back(digits[b >> 4]);
        s.push_back(digits[b & 15]);
    }
    return s;
}

Result<std::vector<std::uint8_t>> from_hex(const json::Value* v, const char* what) {
    using Out = Result<std::vector<std::uint8_t>>;
    if (!v) {
        return Out::ok({});
    }
    if (!v->is_string() || v->string().size() % 2 != 0) {
        return Out::fail(std::string("sh-anim: \"") + what + "\" must be a hex string");
    }
    std::vector<std::uint8_t> out;
    const std::string& s = v->string();
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < s.size(); i += 2) {
        const int hi = nibble(s[i]), lo = nibble(s[i + 1]);
        if (hi < 0 || lo < 0) {
            return Out::fail(std::string("sh-anim: \"") + what + "\" must be a hex string");
        }
        out.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
    }
    return Out::ok(std::move(out));
}

bool get_int(const json::Value& obj, const char* key, std::int64_t lo, std::int64_t hi, std::int64_t& out) {
    const json::Value* v = obj.find(key);
    if (!v || !v->is_integer() || v->integer() < lo || v->integer() > hi) {
        return false;
    }
    out = v->integer();
    return true;
}

}  // namespace

std::string anm_to_json(const AnmFile& file, std::string_view game_path, std::span<const std::uint8_t> raw,
                        std::string_view base_path) {
    const AnmHeader& h = file.header();
    json::Writer w(true);
    w.begin_object();
    w.member("format", "sh-anim/1");
    w.key("source").inline_next().begin_object();
    w.member("path", std::string(game_path));
    w.member("size", raw.size());
    char crc[16];
    std::snprintf(crc, sizeof crc, "%08X", crc32(raw.data(), raw.size()));
    w.member("crc32", crc);
    w.end_object();
    w.member("kind", file.extension() ? "keyframes" : "anm");
    if (file.extension()) {
        w.member("base", std::string(base_path));
        w.member("first_keyframe_index", std::uint64_t{h.keyframe_count});
    }
    w.key("units").inline_next().begin_object();
    w.member("translation", "value << scale_log2 = PS1 units (256 = 1 m, +Y down); slot 0 also minus root_y_offset");
    w.member("rotation", "value << 5 = Q12 (4096 = 1.0), row-major 3x3");
    w.member("keyframe_rate", "set by the game's animation tables (anim_clips.json); 30 per second by default");
    w.end_object();
    w.key("header").inline_next().begin_object();
    w.member("data_offset", h.data_offset);
    w.member("rotation_slots", h.rotation_slots);
    w.member("translation_slots", h.translation_slots);
    w.member("keyframe_size", h.keyframe_size);
    w.member("bone_count", h.bone_count);
    w.member("pad7", h.pad7);
    w.member("active_bones", h.active_bones);
    w.member("file_size", h.file_size);
    w.member("keyframe_count", h.keyframe_count);
    w.member("scale_log2", h.scale_log2);
    w.member("root_y_offset", h.root_y_offset);
    w.end_object();
    w.key("bones").begin_array();
    for (const AnmBone& b : file.bones()) {
        w.inline_next().begin_object();
        w.member("parent", b.parent);
        w.member("rotation", b.rotation);
        w.member("translation", b.translation);
        w.key("bind").begin_array().value(b.bind[0]).value(b.bind[1]).value(b.bind[2]).end_array();
        w.end_object();
    }
    w.end_array();
    w.member("gap", to_hex(file.gap()));
    w.member("tail", to_hex(file.tail()));
    w.key("keyframes").begin_array();
    for (const AnmKeyframe& kf : file.keyframes()) {
        w.inline_next().begin_object();
        w.key("t").begin_array();
        for (const auto& t : kf.translations) {
            w.begin_array();
            for (const std::int8_t v : t) {
                w.value(static_cast<int>(v));
            }
            w.end_array();
        }
        w.end_array();
        w.key("r").begin_array();
        for (const auto& r : kf.rotations) {
            w.begin_array();
            for (const std::int8_t v : r) {
                w.value(static_cast<int>(v));
            }
            w.end_array();
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.take() + "\n";
}

Result<AnmFile> anm_from_json(std::string_view json_text) {
    using Out = Result<AnmFile>;
    auto parsed = json::parse(json_text);
    if (!parsed) {
        return Out::fail("sh-anim: " + parsed.error());
    }
    const json::Value& doc = parsed.value();
    const json::Value* format = doc.find("format");
    if (!format || !format->is_string() || format->string() != "sh-anim/1") {
        return Out::fail("sh-anim: not an sh-anim/1 document");
    }
    const json::Value* kind = doc.find("kind");
    if (!kind || !kind->is_string() || (kind->string() != "anm" && kind->string() != "keyframes")) {
        return Out::fail("sh-anim: \"kind\" must be \"anm\" or \"keyframes\"");
    }
    const bool extension = kind->string() == "keyframes";

    const json::Value* hv = doc.find("header");
    if (!hv || !hv->is_object()) {
        return Out::fail("sh-anim: no header");
    }
    AnmHeader h;
    std::int64_t v = 0;
    const auto field = [&](const char* key, std::int64_t hi) {
        return get_int(*hv, key, 0, hi, v);
    };
    if (!field("data_offset", 0xFFFF)) return Out::fail("sh-anim: header.data_offset");
    h.data_offset = static_cast<std::uint16_t>(v);
    if (!field("rotation_slots", 0xFF)) return Out::fail("sh-anim: header.rotation_slots");
    h.rotation_slots = static_cast<std::uint8_t>(v);
    if (!field("translation_slots", 0xFF)) return Out::fail("sh-anim: header.translation_slots");
    h.translation_slots = static_cast<std::uint8_t>(v);
    if (!field("keyframe_size", 0xFFFF)) return Out::fail("sh-anim: header.keyframe_size");
    h.keyframe_size = static_cast<std::uint16_t>(v);
    if (!field("bone_count", 0xFF)) return Out::fail("sh-anim: header.bone_count");
    h.bone_count = static_cast<std::uint8_t>(v);
    if (!field("pad7", 0xFF)) return Out::fail("sh-anim: header.pad7");
    h.pad7 = static_cast<std::uint8_t>(v);
    if (!field("active_bones", 0xFFFFFFFF)) return Out::fail("sh-anim: header.active_bones");
    h.active_bones = static_cast<std::uint32_t>(v);
    if (!field("file_size", 0xFFFFFFFF)) return Out::fail("sh-anim: header.file_size");
    h.file_size = static_cast<std::uint32_t>(v);
    if (!field("keyframe_count", 0xFFFF)) return Out::fail("sh-anim: header.keyframe_count");
    h.keyframe_count = static_cast<std::uint16_t>(v);
    if (!field("scale_log2", 0xFF)) return Out::fail("sh-anim: header.scale_log2");
    h.scale_log2 = static_cast<std::uint8_t>(v);
    if (!field("root_y_offset", 0xFF)) return Out::fail("sh-anim: header.root_y_offset");
    h.root_y_offset = static_cast<std::uint8_t>(v);

    std::vector<AnmBone> bones;
    const json::Value* bv = doc.find("bones");
    if (!bv || !bv->is_array()) {
        return Out::fail("sh-anim: no bones array");
    }
    for (const json::Value& b : bv->array()) {
        AnmBone bone;
        std::int64_t p = 0, r = 0, t = 0;
        const json::Value* bind = b.find("bind");
        if (!get_int(b, "parent", -128, 127, p) || !get_int(b, "rotation", -128, 127, r) ||
            !get_int(b, "translation", -128, 127, t) || !bind || !bind->is_array() || bind->array().size() != 3) {
            return Out::fail("sh-anim: bone " + std::to_string(bones.size()) + " is incomplete");
        }
        bone.parent = static_cast<int>(p);
        bone.rotation = static_cast<int>(r);
        bone.translation = static_cast<int>(t);
        for (std::size_t c = 0; c < 3; ++c) {
            const json::Value& x = bind->array()[c];
            if (!x.is_integer() || x.integer() < -128 || x.integer() > 127) {
                return Out::fail("sh-anim: bone " + std::to_string(bones.size()) + " bind is not s8");
            }
            bone.bind[c] = static_cast<int>(x.integer());
        }
        bones.push_back(bone);
    }

    auto gap = from_hex(doc.find("gap"), "gap");
    if (!gap) {
        return Out::fail(gap.error());
    }
    auto tail = from_hex(doc.find("tail"), "tail");
    if (!tail) {
        return Out::fail(tail.error());
    }

    std::vector<AnmKeyframe> keyframes;
    const json::Value* kv = doc.find("keyframes");
    if (!kv || !kv->is_array()) {
        return Out::fail("sh-anim: no keyframes array");
    }
    const auto read_s8_rows = [](const json::Value* rows, std::size_t width, auto& out) -> bool {
        if (!rows || !rows->is_array()) {
            return false;
        }
        for (const json::Value& row : rows->array()) {
            if (!row.is_array() || row.array().size() != width) {
                return false;
            }
            auto& dst = out.emplace_back();
            for (std::size_t i = 0; i < width; ++i) {
                const json::Value& x = row.array()[i];
                if (!x.is_integer() || x.integer() < -128 || x.integer() > 127) {
                    return false;
                }
                dst[i] = static_cast<std::int8_t>(x.integer());
            }
        }
        return true;
    };
    for (const json::Value& k : kv->array()) {
        AnmKeyframe kf;
        if (!read_s8_rows(k.find("t"), 3, kf.translations) || !read_s8_rows(k.find("r"), 9, kf.rotations)) {
            return Out::fail("sh-anim: keyframe " + std::to_string(keyframes.size()) + " is malformed");
        }
        keyframes.push_back(std::move(kf));
    }
    return AnmFile::from_parts(extension, h, std::move(bones), std::move(gap.value()), std::move(keyframes),
                               std::move(tail.value()));
}

}  // namespace sh::convert::anim
