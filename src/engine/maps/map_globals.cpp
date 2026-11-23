// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/maps/map_globals.h"

#include <cstring>

#include "formats/common/json.h"

namespace sh::maps {

namespace {

// Element width of a declared first value; 0 for an unknown type.
std::size_t type_width(std::string_view type) {
    if (type == "u8" || type == "s8") return 1;
    if (type == "u16" || type == "s16") return 2;
    if (type == "u32" || type == "s32") return 4;
    return 0;
}

}  // namespace

void* MapGlobals::find(std::string_view name) {
    for (const Bound& b : bound_) {
        if (b.name == name) {
            return b.address;
        }
    }
    for (const std::unique_ptr<Block>& b : blocks_) {
        if (b->name == name) {
            return b->bytes.data();
        }
    }
    return nullptr;
}

void MapGlobals::bind(std::string_view name, void* address) {
    for (Bound& b : bound_) {
        if (b.name == name) {
            b.address = address;
            return;
        }
    }
    bound_.push_back(Bound{std::string(name), address});
}

bool MapGlobals::declare_from_json(std::string_view text, std::string* error) {
    auto doc = sh::json::parse(text);
    if (!doc) {
        *error = doc.error();
        return false;
    }
    const sh::json::Value* globals = doc.value().find("globals");
    if (globals == nullptr || !globals->is_object()) {
        *error = "no \"globals\" object";
        return false;
    }
    for (const auto& [name, v] : globals->members()) {
        const sh::json::Value* bytes = v.find("bytes");
        const sh::json::Value* type = v.find("type");
        const sh::json::Value* init = v.find("init");
        const std::size_t width = type != nullptr && type->is_string() ? type_width(type->string()) : 1;
        if (bytes == nullptr || !bytes->is_integer() || bytes->integer() <= 0 || width == 0 ||
            (init != nullptr && !init->is_array())) {
            *error = "global '" + name + "' needs a positive \"bytes\", a known \"type\" and an \"init\" array";
            return false;
        }
        std::vector<std::uint8_t> storage(static_cast<std::size_t>(bytes->integer()), 0);
        if (init != nullptr) {
            if (init->array().size() * width > storage.size()) {
                *error = "global '" + name + "': \"init\" is longer than \"bytes\"";
                return false;
            }
            std::size_t at = 0;
            for (const sh::json::Value& e : init->array()) {
                if (!e.is_integer()) {
                    *error = "global '" + name + "': \"init\" holds integers";
                    return false;
                }
                // Two's complement little-endian, as the PS1 stored the word.
                const auto word = static_cast<std::uint32_t>(static_cast<std::int64_t>(e.integer()));
                for (std::size_t k = 0; k < width; ++k) {
                    storage[at++] = static_cast<std::uint8_t>(word >> (8 * k));
                }
            }
        }
        // A later declaration (a mod's patch) of the same name replaces an unused one; a block in use keeps its
        // address.
        bool replaced = false;
        for (std::unique_ptr<Block>& b : blocks_) {
            if (b->name == name) {
                if (storage.size() <= b->bytes.size()) {
                    std::memcpy(b->bytes.data(), storage.data(), storage.size());
                }
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            auto b = std::make_unique<Block>();
            b->name = name;
            b->bytes = std::move(storage);
            blocks_.push_back(std::move(b));
        }
    }
    return true;
}

MapGlobals& map_globals() {
    static MapGlobals instance;
    return instance;
}

}  // namespace sh::maps
