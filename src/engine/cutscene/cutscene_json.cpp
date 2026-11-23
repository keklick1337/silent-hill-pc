// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh-cutscene/1 -> Cutscene (cutscene.h).
#include "engine/cutscene/cutscene.h"

#include "formats/common/json.h"

#include <cctype>

namespace sh::cutscene {
namespace {

using Out = Result<Cutscene>;

bool to_s16(const json::Value& v, std::int16_t& out) {
    if (!v.is_integer() || v.integer() < -32768 || v.integer() > 32767) {
        return false;
    }
    out = static_cast<std::int16_t>(v.integer());
    return true;
}

bool read_name(const json::Value& track, TrackName& out, std::string& error) {
    const json::Value* text = track.find("name");
    const json::Value* hex = track.find("name_hex");
    TrackName from_hex{};
    bool have_hex = false;
    if (hex && hex->is_string()) {
        const std::string& s = hex->string();
        if (s.size() != 8) {
            error = "name_hex must be 8 hex digits";
            return false;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            const auto digit = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = digit(s[2 * i]), lo = digit(s[2 * i + 1]);
            if (hi < 0 || lo < 0) {
                error = "name_hex must be 8 hex digits";
                return false;
            }
            from_hex[i] = static_cast<char>(hi * 16 + lo);
        }
        have_hex = true;
    }
    if (text && text->is_string()) {
        const std::string& s = text->string();
        if (s.empty() || s.size() > 4) {
            error = "name must have 1 to 4 characters (the game compares the first four)";
            return false;
        }
        // name_hex keeps bytes "name" cannot show (the converter writes '?' for them); it wins while "name"
        // still reads the same, so editing "name" alone renames the track.
        std::string shown;
        for (const char c : from_hex) {
            if (c == '\0') {
                break;
            }
            shown.push_back(std::isprint(static_cast<unsigned char>(c)) ? c : '?');
        }
        if (have_hex && shown == s) {
            out = from_hex;
            return true;
        }
        out = {};
        for (std::size_t i = 0; i < out.size() && i < s.size(); ++i) {
            out[i] = s[i];
        }
        return true;
    }
    if (have_hex) {
        out = from_hex;
        return true;
    }
    error = "a track needs a name";
    return false;
}

bool read_holds(const json::Value& track, std::vector<HoldRange>& out, std::string& error) {
    const json::Value* holds = track.find("holds");
    if (!holds) {
        return true;
    }
    if (!holds->is_array()) {
        error = "holds must be an array";
        return false;
    }
    for (const json::Value& h : holds->array()) {
        HoldRange r;
        if (!h.is_array() || h.array().size() != 3 || !to_s16(h.array()[0], r.first) || !to_s16(h.array()[1], r.last) ||
            !to_s16(h.array()[2], r.key)) {
            error = "a hold is [first frame, last frame, key]";
            return false;
        }
        out.push_back(r);
    }
    return true;
}

template <std::size_t N>
bool read_keys(const json::Value& track, std::vector<std::array<std::int16_t, N>>& out, std::string& error) {
    const json::Value* keys = track.find("keyframes");
    if (!keys || !keys->is_array()) {
        error = "keyframes must be an array";
        return false;
    }
    for (const json::Value& k : keys->array()) {
        std::array<std::int16_t, N> values{};
        if (!k.is_array() || k.array().size() != N) {
            error = "a keyframe has " + std::to_string(N) + " values";
            return false;
        }
        for (std::size_t i = 0; i < N; ++i) {
            if (!to_s16(k.array()[i], values[i])) {
                error = "keyframe values are 16-bit integers";
                return false;
            }
        }
        out.push_back(values);
    }
    return true;
}

bool read_camera(const json::Value* v, CameraTrack& out, std::string& error) {
    if (!v || !v->is_object()) {
        error = "camera must be an object";
        return false;
    }
    if ((v->find("name") || v->find("name_hex")) && !read_name(*v, out.name, error)) {
        return false;
    }
    std::vector<std::array<std::int16_t, 8>> raw;
    if (!read_holds(*v, out.holds, error) || !read_keys<8>(*v, raw, error)) {
        return false;
    }
    for (const auto& r : raw) {
        out.keys.push_back({{r[0], r[1], r[2]}, {r[3], r[4], r[5]}, r[6], r[7]});
    }
    return true;
}

bool read_actor(const json::Value& v, ActorTrack& out, std::string& error) {
    if (!v.is_object()) {
        error = "a character track must be an object";
        return false;
    }
    std::vector<std::array<std::int16_t, 6>> raw;
    if (!read_name(v, out.name, error) || !read_holds(v, out.holds, error) || !read_keys<6>(v, raw, error)) {
        return false;
    }
    for (const auto& r : raw) {
        out.keys.push_back({{r[0], r[1], r[2]}, {r[3], r[4], r[5]}});
    }
    return true;
}

bool read_origin(const json::Value& doc, Vec3i& out) {
    const json::Value* header = doc.find("header");
    const json::Value* origin = header ? header->find("origin") : nullptr;
    if (!origin) {
        origin = doc.find("origin");
    }
    if (!origin) {
        out = {};
        return true;
    }
    if (!origin->is_array() || origin->array().size() != 3) {
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const json::Value& o = origin->array()[i];
        if (!o.is_integer() || o.integer() < INT32_MIN || o.integer() > INT32_MAX) {
            return false;
        }
        out[i] = static_cast<std::int32_t>(o.integer());
    }
    return true;
}

}  // namespace

Result<Cutscene> parse_cutscene_json(std::string_view text, std::string source) {
    const std::string where = source.empty() ? std::string("sh-cutscene") : source;
    auto parsed = json::parse(text);
    if (!parsed) {
        return Out::fail(where + ": " + parsed.error());
    }
    const json::Value& doc = parsed.value();
    const json::Value* format = doc.find("format");
    if (!format || !format->is_string() || format->string() != "sh-cutscene/1") {
        return Out::fail(where + ": not an sh-cutscene/1 document");
    }
    Cutscene cs;
    cs.source = std::move(source);
    if (!read_origin(doc, cs.origin)) {
        return Out::fail(where + ": origin is [x, y, z] (Q8 integers)");
    }
    const json::Value* segments = doc.find("segments");
    if (!segments || !segments->is_array()) {
        return Out::fail(where + ": segments must be an array of [first frame, frame count]");
    }
    for (const json::Value& s : segments->array()) {
        Segment seg;
        if (!s.is_array() || s.array().size() != 2 || !to_s16(s.array()[0], seg.first) || !to_s16(s.array()[1], seg.count)) {
            return Out::fail(where + ": a segment is [first frame, frame count]");
        }
        cs.segments.push_back(seg);
    }
    std::string error;
    if (!read_camera(doc.find("camera"), cs.camera, error)) {
        return Out::fail(where + ": camera: " + error);
    }
    if (const json::Value* chars = doc.find("characters")) {
        if (!chars->is_array()) {
            return Out::fail(where + ": characters must be an array");
        }
        for (const json::Value& c : chars->array()) {
            ActorTrack track;
            if (!read_actor(c, track, error)) {
                return Out::fail(where + ": character " + std::to_string(cs.actors.size()) + ": " + error);
            }
            cs.actors.push_back(std::move(track));
        }
    }
    return Out::ok(std::move(cs));
}

}  // namespace sh::cutscene
