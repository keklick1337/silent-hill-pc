// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/harry_reactions.h"

#include "formats/common/json.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace sh::chara {

namespace {

using json::Value;

bool parse_hex(const Value* v, std::uint32_t* out) {
    if (v == nullptr) {
        return false;
    }
    if (v->is_integer()) {
        *out = static_cast<std::uint32_t>(v->integer());
        return true;
    }
    if (!v->is_string()) {
        return false;
    }
    char* end = nullptr;
    const unsigned long n = std::strtoul(v->string().c_str(), &end, 0);
    if (end == v->string().c_str() || *end != '\0') {
        return false;
    }
    *out = static_cast<std::uint32_t>(n);
    return true;
}

std::int16_t s16_at(std::span<const std::uint8_t> b, std::size_t at) {
    return static_cast<std::int16_t>(b[at] | (b[at + 1] << 8));
}

std::uint32_t u32_at(std::span<const std::uint8_t> b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

}  // namespace

Result<HarryReactions> HarryReactions::parse(std::string_view json_text) {
    auto doc = json::parse(json_text);
    if (!doc) {
        return Result<HarryReactions>::fail(doc.error());
    }
    const Value& root = doc.value();
    const Value* clips = root.find("clips");
    if (clips == nullptr || !clips->is_object()) {
        return Result<HarryReactions>::fail("needs a \"clips\" object");
    }
    HarryReactions r;
    for (const auto& [name, v] : clips->members()) {
        const Value* anim = v.find("anim");
        const Value* active = v.find("active");
        if (anim == nullptr || !anim->is_integer() || anim->integer() < 0 || anim->integer() > 1023 ||
            (active != nullptr && !active->is_bool())) {
            return Result<HarryReactions>::fail("clip \"" + name + "\" needs \"anim\" (0..1023) and a bool \"active\"");
        }
        r.clips_[name] = static_cast<int>(anim->integer() << 1) | (active != nullptr && active->boolean() ? 1 : 0);
    }
    if (const Value* groups = root.find("groups"); groups != nullptr && groups->is_object()) {
        for (const auto& [name, v] : groups->members()) {
            std::vector<std::string> list;
            for (const Value& e : v.array()) {
                if (e.is_string()) {
                    list.push_back(e.string());
                }
            }
            r.groups_[name] = std::move(list);
        }
    }
    if (const Value* hostile = root.find("hostile"); hostile != nullptr && hostile->is_array()) {
        for (const Value& e : hostile->array()) {
            if (e.is_string()) {
                r.hostile_.push_back(e.string());
            }
        }
    }
    return Result<HarryReactions>::ok(std::move(r));
}

std::vector<int> HarryReactions::resolve(const std::vector<std::string>& names,
                                         std::vector<std::string>* unknown) const {
    std::set<int> ids;
    for (const std::string& name : names) {
        if (auto c = clips_.find(name); c != clips_.end()) {
            ids.insert(c->second);
            continue;
        }
        auto g = groups_.find(name);
        if (g == groups_.end()) {
            if (unknown != nullptr) {
                unknown->push_back(name);
            }
            continue;
        }
        for (const std::string& member : g->second) {
            if (auto c = clips_.find(member); c != clips_.end()) {
                ids.insert(c->second);
            } else if (unknown != nullptr) {
                unknown->push_back(name + "." + member);
            }
        }
    }
    return {ids.begin(), ids.end()};
}

int HarryReactions::id_of(std::string_view name) const {
    const auto it = clips_.find(name);
    return it != clips_.end() ? it->second : -1;
}

std::string HarryReactions::name_of(int id) const {
    for (const auto& [name, v] : clips_) {
        if (v == id) {
            return name;
        }
    }
    return {};
}

const ReactionRow* MapReactionTable::row(int id) const {
    for (const ReactionRow& r : rows) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

std::string tables_bin(std::string_view tables_json) {
    auto doc = json::parse(tables_json);
    if (!doc) {
        return {};
    }
    const Value* bin = doc.value().find("bin");
    return bin != nullptr && bin->is_string() ? bin->string() : std::string();
}

Result<MapReactionTable> read_map_reactions(std::string_view map, std::string_view tables_json,
                                            std::span<const std::uint8_t> bin) {
    using R = Result<MapReactionTable>;
    auto doc = json::parse(tables_json);
    if (!doc) {
        return R::fail(std::string(map) + "/tables.json: " + doc.error());
    }
    const Value& root = doc.value();
    std::uint32_t base = 0;
    const Value* symbols = root.find("symbols");
    if (!parse_hex(root.find("base"), &base) || symbols == nullptr || !symbols->is_array()) {
        return R::fail(std::string(map) + "/tables.json: needs \"base\" and \"symbols\"");
    }
    std::map<std::uint32_t, std::string> functions;
    if (const Value* f = root.find("functions"); f != nullptr && f->is_array()) {
        for (const Value& e : f->array()) {
            std::uint32_t va = 0;
            const Value* name = e.find("symbol");
            if (parse_hex(e.find("va"), &va) && name != nullptr && name->is_string()) {
                functions[va] = name->string();
            }
        }
    }
    auto function_name = [&](std::uint32_t va) -> std::string {
        if (va == 0) {
            return {};
        }
        auto it = functions.find(va);
        return it != functions.end() ? it->second : std::string("?");
    };

    MapReactionTable t;
    t.map = std::string(map);
    bool have_rows = false;
    bool have_clips = false;
    for (const Value& s : symbols->array()) {
        const Value* name = s.find("symbol");
        const Value* type = s.find("type");
        const Value* count = s.find("count");
        std::uint32_t va = 0;
        if (name == nullptr || !name->is_string() || type == nullptr || !type->is_string() || count == nullptr ||
            !count->is_integer() || !parse_hex(s.find("va"), &va) || va < base) {
            continue;
        }
        const std::size_t at = va - base;
        const std::size_t n = static_cast<std::size_t>(std::max<std::int64_t>(count->integer(), 0));
        if (name->string() == "g_MapHeaderTable_38") {
            if (at + n * 8 > bin.size()) {
                return R::fail(t.map + ": the reaction table lies outside the overlay");
            }
            for (std::size_t i = 0; i < n; ++i) {
                ReactionRow r;
                r.status = static_cast<std::uint16_t>(s16_at(bin, at + i * 8));
                r.id = s16_at(bin, at + i * 8 + 2);
                r.time = s16_at(bin, at + i * 8 + 4);
                r.last = s16_at(bin, at + i * 8 + 6);
                if (r.status == 0 && r.id == 0) {
                    break;
                }
                t.rows.push_back(r);
            }
            have_rows = true;
        } else if (type->string() == "s_AnimInfo" && name->string().starts_with("HARRY_")) {
            if (at + n * 16 > bin.size()) {
                return R::fail(t.map + ": Harry's map clips lie outside the overlay");
            }
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t e = at + i * 16;
                const std::uint32_t fn = u32_at(bin, e);
                if (fn == 0) {
                    break;
                }
                ReactionClip c;
                c.playback = function_name(fn);
                c.status = bin[e + 4];
                c.variable = bin[e + 5] != 0;
                c.link = bin[e + 6];
                const std::uint32_t d = u32_at(bin, e + 8);
                if (c.variable) {
                    c.duration_func = function_name(d);
                } else {
                    c.duration = static_cast<std::int32_t>(d);
                }
                c.start = s16_at(bin, e + 12);
                c.end = s16_at(bin, e + 14);
                t.clips.push_back(c);
            }
            have_clips = true;
        }
    }
    if (!have_rows || !have_clips) {
        return R::fail(t.map + ": tables.json names no reaction table or no Harry clips");
    }
    return R::ok(std::move(t));
}

Result<ReactionImport> plan_reaction_import(const MapReactionTable& source, const std::vector<int>& ids,
                                            int first_status, int first_keyframe, int map_keyframe_first) {
    using R = Result<ReactionImport>;
    if ((first_status & 1) != 0 || first_status < kHarryMapStatusFirst) {
        return R::fail("the first status of an import must be an even map status");
    }
    ReactionImport plan;
    std::map<int, int> pair_of;  // source pair (even status) -> target pair
    std::vector<int> pairs;      // source pairs in import order
    auto clip_at = [&](int status) -> const ReactionClip* {
        const int i = status - kHarryMapStatusFirst;
        return i >= 0 && i < static_cast<int>(source.clips.size()) ? &source.clips[static_cast<std::size_t>(i)] : nullptr;
    };
    // Takes the pair of `status` and every map clip it links to.
    auto take = [&](auto&& self, int status) -> void {
        if (status < kHarryMapStatusFirst || status >= kHarryStatusMax || clip_at(status) == nullptr) {
            return;
        }
        const int even = status & ~1;
        if (pair_of.contains(even)) {
            return;
        }
        pair_of[even] = first_status + static_cast<int>(pairs.size()) * 2;
        pairs.push_back(even);
        for (int s : {even, even + 1}) {
            if (const ReactionClip* c = clip_at(s)) {
                self(self, c->link);
            }
        }
    };
    std::vector<const ReactionRow*> rows;
    for (int id : ids) {
        const ReactionRow* r = source.row(id);
        if (r == nullptr || clip_at(r->status & 0xFF) == nullptr) {
            continue;
        }
        rows.push_back(r);
        take(take, r->status & 0xFF);
        take(take, (r->status >> 8) & 0xFF);
    }
    if (first_status + static_cast<int>(pairs.size()) * 2 - 1 > kHarryStatusMax) {
        return R::fail("Harry's clip statuses would pass 255");
    }

    // Keyframe ranges of the map file the clips and rows use.
    std::vector<std::pair<int, int>> ranges;
    auto use = [&](int lo, int hi) {
        lo = std::max(lo, map_keyframe_first);
        if (hi >= lo) {
            ranges.emplace_back(lo, hi + 1);
        }
    };
    for (int even : pairs) {
        for (int s : {even, even + 1}) {
            if (const ReactionClip* c = clip_at(s)) {
                use(c->start < 0 ? c->end : c->start, c->end);
            }
        }
    }
    for (const ReactionRow* r : rows) {
        use(r->time, r->last);
    }
    std::sort(ranges.begin(), ranges.end());
    int next = first_keyframe;
    for (const auto& [lo, hi] : ranges) {
        if (!plan.keyframes.empty()) {
            KeyframeCopy& prev = plan.keyframes.back();
            if (lo <= prev.source_first + prev.count) {
                const int grow = hi + 1 - (prev.source_first + prev.count);
                if (grow > 0) {
                    prev.count += grow;
                    next += grow;
                }
                continue;
            }
        }
        plan.keyframes.push_back(KeyframeCopy{lo, hi - lo + 1, next});
        next += hi - lo + 1;
    }
    auto keyframe = [&](int k) {
        if (k < map_keyframe_first) {
            return k;
        }
        for (const KeyframeCopy& c : plan.keyframes) {
            if (k >= c.source_first && k < c.source_first + c.count) {
                return c.target_first + (k - c.source_first);
            }
        }
        return k;
    };
    auto status = [&](int s) {
        if (s < kHarryMapStatusFirst || s >= kHarryStatusMax) {
            return s;
        }
        auto it = pair_of.find(s & ~1);
        return it != pair_of.end() ? it->second + (s & 1) : s;
    };

    for (int even : pairs) {
        for (int s : {even, even + 1}) {
            const ReactionClip* c = clip_at(s);
            ReactionClip n = c != nullptr ? *c : ReactionClip{};
            n.status = status(s);
            if (c != nullptr) {
                n.link = status(c->link);
                n.start = c->start < 0 ? c->start : keyframe(c->start);
                n.end = keyframe(c->end);
            }
            plan.clips.push_back(n);
        }
    }
    for (const ReactionRow* r : rows) {
        ReactionRow n = *r;
        const int hi = (r->status >> 8) & 0xFF;
        n.status = status(r->status & 0xFF) | ((hi != 0 ? status(hi) : 0) << 8);
        n.time = keyframe(r->time);
        n.last = keyframe(r->last);
        plan.rows.push_back(n);
        plan.ids.push_back(r->id);
    }
    return R::ok(std::move(plan));
}

}  // namespace sh::chara
