// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/chara_vram.h"

#include "formats/common/json.h"

namespace sh::world {

namespace {

using json::Value;

bool number(const Value& parent, const char* key, std::int64_t lo, std::int64_t hi, std::int64_t& out,
            std::string& error) {
    const Value* v = parent.find(key);
    if (v == nullptr || !v->is_integer() || v->integer() < lo || v->integer() > hi) {
        error = "\"" + std::string(key) + "\" is an integer " + std::to_string(lo) + ".." + std::to_string(hi);
        return false;
    }
    out = v->integer();
    return true;
}

bool read_image(const Value* v, bool with_clut, VramImage& out, std::string& error) {
    if (v == nullptr || !v->is_object()) {
        error = "an image is an object {tpage, u, v" + std::string(with_clut ? ", clut_x, clut_y}" : "}");
        return false;
    }
    const Value* tp = v->find("tpage");
    if (tp == nullptr || !tp->is_array() || tp->array().size() != 2) {
        error = "\"tpage\" holds the two bytes of the page";
        return false;
    }
    for (std::size_t i = 0; i < 2; ++i) {
        const Value& b = tp->array()[i];
        if (!b.is_integer() || b.integer() < 0 || b.integer() > 255) {
            error = "a tpage byte is 0..255";
            return false;
        }
        out.tpage[i] = static_cast<std::uint8_t>(b.integer());
    }
    std::int64_t u = 0, vv = 0, cx = 0, cy = 0;
    if (!number(*v, "u", 0, 255, u, error) || !number(*v, "v", 0, 255, vv, error)) {
        return false;
    }
    if (with_clut && (!number(*v, "clut_x", 0, 1023, cx, error) || !number(*v, "clut_y", 0, 511, cy, error))) {
        return false;
    }
    out.u = static_cast<std::uint8_t>(u);
    out.v = static_cast<std::uint8_t>(vv);
    out.clut_x = static_cast<std::int16_t>(cx);
    out.clut_y = static_cast<std::int16_t>(cy);
    return true;
}

}  // namespace

Result<CharaVramLayout> CharaVramLayout::parse(std::string_view text) {
    Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<CharaVramLayout>::fail(doc.error());
    }
    const Value& root = doc.value();
    const Value* fmt = root.find("format");
    if (!root.is_object() || fmt == nullptr || !fmt->is_string() || fmt->string() != "sh-chara-vram") {
        return Result<CharaVramLayout>::fail("not a sh-chara-vram file");
    }
    CharaVramLayout out;
    std::string error;
    const Value* names = root.find("player_characters");
    if (names == nullptr || !names->is_array()) {
        return Result<CharaVramLayout>::fail("\"player_characters\" lists character names");
    }
    for (const Value& n : names->array()) {
        if (!n.is_string()) {
            return Result<CharaVramLayout>::fail("\"player_characters\" lists character names");
        }
        out.player_characters.push_back(n.string());
    }
    if (!read_image(root.find("player"), true, out.player, error)) {
        return Result<CharaVramLayout>::fail("player: " + error);
    }
    const Value* slots = root.find("slots");
    if (slots == nullptr || !slots->is_array() || slots->array().empty()) {
        return Result<CharaVramLayout>::fail("\"slots\" lists at least one image");
    }
    for (const Value& s : slots->array()) {
        VramImage img;
        if (!read_image(&s, false, img, error)) {
            return Result<CharaVramLayout>::fail("slots: " + error);
        }
        out.slots.push_back(img);
    }
    const Value* clut = root.find("slot_clut");
    std::int64_t x = 0, step = 0, y = 0;
    if (clut == nullptr || !clut->is_object() || !number(*clut, "x", 0, 1023, x, error) ||
        !number(*clut, "step", 0, 1023, step, error) || !number(*clut, "y", 0, 511, y, error)) {
        return Result<CharaVramLayout>::fail("slot_clut: " + (error.empty() ? std::string("an object {x, step, y}") : error));
    }
    out.clut_x = static_cast<std::int32_t>(x);
    out.clut_step = static_cast<std::int32_t>(step);
    out.clut_y = static_cast<std::int32_t>(y);
    return Result<CharaVramLayout>::ok(std::move(out));
}

VramImage CharaVramLayout::image(bool player_character, std::int32_t model_idx) const {
    if (player_character) {
        return player;
    }
    const bool listed = model_idx >= 0 && static_cast<std::size_t>(model_idx) < slots.size();
    VramImage out = slots[listed ? static_cast<std::size_t>(model_idx) : 0];
    // The palette x is a 16-bit coordinate; a far index wraps like the original's s16 store.
    const std::uint32_t cx = static_cast<std::uint32_t>(model_idx) * static_cast<std::uint32_t>(clut_step) +
                             static_cast<std::uint32_t>(clut_x);
    out.clut_x = static_cast<std::int16_t>(static_cast<std::uint16_t>(cx & 0xFFFFu));
    out.clut_y = static_cast<std::int16_t>(clut_y);
    return out;
}

}  // namespace sh::world
