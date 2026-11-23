// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gconfig/controller_layouts.h"

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace sh::gconfig {

namespace {

using json::Value;

[[noreturn]] void fail(const Value& at, const std::string& what) {
    throw std::runtime_error("controller_layouts.json line " + std::to_string(at.line()) + ": " + what);
}

constexpr const char* kActions[kLayoutActionCount] = {"enter", "cancel", "skip", "action", "aim", "light", "run",
                                                      "view", "step_left", "step_right", "pause", "item", "map", "option"};

std::uint16_t button_bit(const Value& v) {
    if (!v.is_string()) {
        fail(v, "a button must be a string");
    }
    static const struct { const char* name; std::uint16_t bit; } kButtons[] = {
        {"Select", 1u << 0}, {"Start", 1u << 3}, {"L2", 1u << 8}, {"R2", 1u << 9}, {"L1", 1u << 10},
        {"R1", 1u << 11}, {"Triangle", 1u << 12}, {"Circle", 1u << 13}, {"Cross", 1u << 14}, {"Square", 1u << 15}};
    for (const auto& b : kButtons) {
        if (v.string() == b.name) {
            return b.bit;
        }
    }
    fail(v, "unknown button \"" + v.string() + "\"");
}

}  // namespace

Result<ControllerLayoutLibrary> ControllerLayoutLibrary::parse(std::string_view text) try {
    const Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<ControllerLayoutLibrary>::fail("controller_layouts.json: " + doc.error());
    }
    const Value* layouts = doc.value().is_object() ? doc.value().find("layouts") : nullptr;
    if (!layouts || !layouts->is_object() || layouts->members().empty()) {
        fail(doc.value(), "\"layouts\" must be a non-empty object");
    }
    ControllerLayoutLibrary lib;
    for (const auto& member : layouts->members()) {
        const std::string& name = member.first;
        const Value& list = member.second;
        if (!list.is_array() || list.array().size() != kLayoutTypeCount) {
            fail(list, "region \"" + name + "\" needs exactly 3 layouts");
        }
        std::array<ControllerLayout, kLayoutTypeCount> types;
        for (std::size_t t = 0; t < kLayoutTypeCount; t++) {
            const Value& layout = list.array()[t];
            if (!layout.is_object()) {
                fail(layout, "a layout must be an object");
            }
            for (std::size_t a = 0; a < kLayoutActionCount; a++) {
                const Value* buttons = layout.find(kActions[a]);
                if (!buttons || !buttons->is_array()) {
                    fail(layout, std::string("missing button list \"") + kActions[a] + "\"");
                }
                std::uint16_t mask = 0;
                for (const Value& b : buttons->array()) {
                    mask = static_cast<std::uint16_t>(mask | button_bit(b));
                }
                types[t].masks[a] = mask;
            }
        }
        lib.regions_.emplace_back(name, types);
    }
    return Result<ControllerLayoutLibrary>::ok(std::move(lib));
} catch (const std::exception& e) {
    return Result<ControllerLayoutLibrary>::fail(e.what());
}

const std::array<ControllerLayout, kLayoutTypeCount>* ControllerLayoutLibrary::region(const std::string& name) const {
    for (const auto& r : regions_) {
        if (r.first == name) {
            return &r.second;
        }
    }
    return nullptr;
}

Result<ControllerLayoutLibrary> ControllerLayoutLibrary::load(const std::filesystem::path& gamedata_dir,
                                                              const std::filesystem::path& mods_dir,
                                                              std::vector<std::string>& warnings,
                                                              ControllerLayoutLoadSource* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("config") / "controller_layouts.json";
    // Base + every active mod's patch, in load order.
    Result<mods::MergedFile> read = mods::read_data_file(gamedata_dir, mods_dir, relative, warnings);
    if (!read) {
        return Result<ControllerLayoutLibrary>::fail(read.error());
    }
    if (source) source->mod_id = read.value().last_mod();
    return parse(read.value().text);
}

}  // namespace sh::gconfig
