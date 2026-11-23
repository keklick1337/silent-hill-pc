// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/held_items.h"

#include "formats/common/json.h"

namespace sh::world {

namespace {

using json::Value;

std::string at(const Value& v) {
    return "line " + std::to_string(v.line()) + ": ";
}

bool integer(const Value& parent, const char* key, std::int64_t lo, std::int64_t hi, std::int64_t& out,
             std::string& error) {
    const Value* v = parent.find(key);
    if (v == nullptr || !v->is_integer() || v->integer() < lo || v->integer() > hi) {
        error = at(v != nullptr ? *v : parent) + "\"" + key + "\" is an integer " + std::to_string(lo) + ".." +
                std::to_string(hi);
        return false;
    }
    out = v->integer();
    return true;
}

bool image(const Value* v, const char* name, HeldItemImage& out, std::string& error) {
    if (v == nullptr || !v->is_object()) {
        error = std::string("\"") + name + "\" is an object {tpage, u, v, clut_x, clut_y}";
        return false;
    }
    const Value* tp = v->find("tpage");
    if (tp == nullptr || !tp->is_array() || tp->array().size() != 2) {
        error = at(*v) + name + ": \"tpage\" holds the two bytes of the page";
        return false;
    }
    for (std::size_t i = 0; i < 2; ++i) {
        const Value& b = tp->array()[i];
        if (!b.is_integer() || b.integer() < 0 || b.integer() > 255) {
            error = at(b) + name + ": a tpage byte is 0..255";
            return false;
        }
        out.tpage[i] = static_cast<std::uint8_t>(b.integer());
    }
    std::int64_t u = 0, vv = 0, cx = 0, cy = 0;
    if (!integer(*v, "u", 0, 255, u, error) || !integer(*v, "v", 0, 255, vv, error) ||
        !integer(*v, "clut_x", 0, 1023, cx, error) || !integer(*v, "clut_y", 0, 511, cy, error)) {
        error = std::string(name) + ": " + error;
        return false;
    }
    out.u = static_cast<std::uint8_t>(u);
    out.v = static_cast<std::uint8_t>(vv);
    out.clut_x = static_cast<std::int16_t>(cx);
    out.clut_y = static_cast<std::int16_t>(cy);
    return true;
}

bool text(const Value& parent, const char* key, std::string& out, std::string& error) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        return true;
    }
    if (!v->is_string()) {
        error = at(*v) + "\"" + key + "\" is a string";
        return false;
    }
    out = v->string();
    return true;
}

}  // namespace

Result<HeldItems> HeldItems::parse(std::string_view json_text) {
    Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<HeldItems>::fail(doc.error());
    }
    const Value& root = doc.value();
    const Value* fmt = root.find("format");
    if (!root.is_object() || fmt == nullptr || !fmt->is_string() || fmt->string() != "sh-held-items") {
        return Result<HeldItems>::fail("not a sh-held-items file");
    }
    HeldItems out;
    std::string error;
    if (!image(root.find("image_shared"), "image_shared", out.image_shared, error) ||
        !image(root.find("image_own"), "image_own", out.image_own, error)) {
        return Result<HeldItems>::fail(error);
    }
    const Value* items = root.find("items");
    if (items == nullptr || !items->is_object()) {
        return Result<HeldItems>::fail("\"items\" maps an item name to what the hand holds");
    }
    for (const auto& [name, v] : items->members()) {
        if (!v.is_object()) {
            return Result<HeldItems>::fail(at(v) + name + ": an entry is an object");
        }
        HeldItemDef def;
        def.item = name;
        std::string material;
        const bool has_material = v.find("texture_name") != nullptr;
        if (!text(v, "texture_name", material, error) || !text(v, "texture", def.texture, error) ||
            !text(v, "model", def.model, error)) {
            return Result<HeldItems>::fail(name + ": " + error);
        }
        if (has_material) {
            def.texture_name = material;
        }
        std::string hand = "right";
        if (!text(v, "hand", hand, error)) {
            return Result<HeldItems>::fail(name + ": " + error);
        }
        if (hand != "right" && hand != "left") {
            return Result<HeldItems>::fail(at(v) + name + ": \"hand\" is \"right\" or \"left\"");
        }
        def.left_hand = hand == "left";
        if (out.find(name) != nullptr) {
            return Result<HeldItems>::fail(at(v) + name + ": listed twice");
        }
        out.items.push_back(std::move(def));
    }
    return Result<HeldItems>::ok(std::move(out));
}

const HeldItemDef* HeldItems::find(std::string_view item) const {
    for (const HeldItemDef& d : items) {
        if (d.item == item) {
            return &d;
        }
    }
    return nullptr;
}

}  // namespace sh::world
