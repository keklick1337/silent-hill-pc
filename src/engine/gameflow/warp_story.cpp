// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gameflow/warp_story.h"

#include <algorithm>
#include <utility>

#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

using R = Result<WarpStory>;

Result<std::vector<std::string>> names(const json::Value* v, const std::string& what, bool required) {
    using N = Result<std::vector<std::string>>;
    std::vector<std::string> out;
    if (v == nullptr || v->is_null()) {
        if (required) return N::fail("warp_story.json: " + what + " is missing");
        return N::ok(std::move(out));
    }
    if (!v->is_array()) return N::fail("warp_story.json: " + what + " must be an array of names");
    for (const json::Value& e : v->array()) {
        if (!e.is_string() || e.string().empty()) {
            return N::fail("warp_story.json: " + what + " must hold non-empty names");
        }
        out.push_back(e.string());
    }
    return N::ok(std::move(out));
}

}  // namespace

Result<WarpStory> WarpStory::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) return R::fail("warp_story.json: " + doc.error());
    const json::Value* list = doc.value().find("chapters");
    if (list == nullptr || !list->is_array()) return R::fail("warp_story.json: \"chapters\" must be an array");

    WarpStory story;
    for (const json::Value& item : list->array()) {
        if (!item.is_object()) return R::fail("warp_story.json: chapters must be objects");
        WarpChapter c;
        const json::Value* name = item.find("name");
        if (name == nullptr || !name->is_string() || name->string().empty()) {
            return R::fail("warp_story.json: a chapter needs a non-empty \"name\"");
        }
        c.name = name->string();
        auto maps = names(item.find("maps"), "\"maps\" of chapter " + c.name, true);
        if (!maps) return R::fail(maps.error());
        auto flags = names(item.find("done_flags"), "\"done_flags\" of chapter " + c.name, false);
        if (!flags) return R::fail(flags.error());
        c.maps = std::move(maps.value());
        c.done_flags = std::move(flags.value());
        for (const std::string& m : c.maps) {
            for (const WarpChapter& earlier : story.chapters_) {
                if (std::find(earlier.maps.begin(), earlier.maps.end(), m) != earlier.maps.end()) {
                    return R::fail("warp_story.json: map " + m + " is in two chapters");
                }
            }
        }
        story.chapters_.push_back(std::move(c));
    }
    return R::ok(std::move(story));
}

std::vector<std::string> WarpStory::flags_for(std::string_view map) const {
    std::vector<std::string> out;
    for (const WarpChapter& c : chapters_) {
        if (std::find(c.maps.begin(), c.maps.end(), map) != c.maps.end()) {
            break;
        }
        for (const std::string& f : c.done_flags) {
            if (std::find(out.begin(), out.end(), f) == out.end()) {
                out.push_back(f);
            }
        }
    }
    return out;
}

}  // namespace sh::gameflow
