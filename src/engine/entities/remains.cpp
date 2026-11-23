// SPDX-License-Identifier: GPL-3.0-or-later
// The store of persistent remains -- see remains.h.

#include "engine/entities/remains.h"

#include <cstdlib>
#include <limits>

#include "formats/common/json.h"

namespace sh::entities {

namespace {

bool same_spot(const Vec3& a, const Vec3& b) {
    return std::abs(a.x - b.x) < RemainsStore::kSameSpot && std::abs(a.y - b.y) < RemainsStore::kSameSpot &&
           std::abs(a.z - b.z) < RemainsStore::kSameSpot;
}

template <typename T>
void trim(std::vector<T>& list, std::size_t limit) {
    if (list.size() > limit) {
        list.erase(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(list.size() - limit));
    }
}

void write_triple(json::Writer& w, std::string_view key, std::int64_t a, std::int64_t b, std::int64_t c) {
    w.key(key).inline_next().begin_array().value(a).value(b).value(c).end_array();
}

// Reads members with range checks; the first problem is kept and fails the whole read.
class Reader {
public:
    std::string error;

    template <typename T>
    void integer(const json::Value& obj, const char* key, T& out, bool required = true) {
        const json::Value* v = obj.find(key);
        if (v == nullptr) {
            if (required) {
                fail(std::string("missing \"") + key + "\"");
            }
            return;
        }
        if (!v->is_integer() || !fits<T>(v->integer())) {
            fail(std::string("\"") + key + "\" is not a valid number");
            return;
        }
        out = static_cast<T>(v->integer());
    }

    template <typename A, typename B, typename C>
    void triple(const json::Value& obj, const char* key, A& a, B& b, C& c) {
        const json::Value* v = obj.find(key);
        if (v == nullptr || !v->is_array() || v->array().size() != 3 || !v->array()[0].is_integer() ||
            !v->array()[1].is_integer() || !v->array()[2].is_integer() || !fits<A>(v->array()[0].integer()) ||
            !fits<B>(v->array()[1].integer()) || !fits<C>(v->array()[2].integer())) {
            fail(std::string("\"") + key + "\" is not three numbers");
            return;
        }
        a = static_cast<A>(v->array()[0].integer());
        b = static_cast<B>(v->array()[1].integer());
        c = static_cast<C>(v->array()[2].integer());
    }

    void fail(std::string what) {
        if (error.empty()) {
            error = std::move(what);
        }
    }

private:
    template <typename T>
    static bool fits(std::int64_t n) {
        return n >= static_cast<std::int64_t>(std::numeric_limits<T>::min()) &&
               n <= static_cast<std::int64_t>(std::numeric_limits<T>::max());
    }
};

}  // namespace

bool RemainsStore::add_corpse(std::string_view map, const CorpseRemains& corpse) {
    MapRemains& m = maps_[std::string(map)];
    ++revision_;
    for (CorpseRemains& c : m.corpses) {
        if (c.chara == corpse.chara && same_spot(c.position, corpse.position)) {
            c = corpse;
            return false;
        }
    }
    m.corpses.push_back(corpse);
    trim(m.corpses, kMaxCorpses);
    return true;
}

bool RemainsStore::add_stain(std::string_view map, const BloodStain& stain) {
    MapRemains& m = maps_[std::string(map)];
    ++revision_;
    for (BloodStain& s : m.stains) {
        // A body's pool is several patches a hand apart: only the very same patch is a repeat.
        if (s.position == stain.position) {
            s = stain;
            return false;
        }
    }
    m.stains.push_back(stain);
    trim(m.stains, kMaxStains);
    return true;
}

const MapRemains* RemainsStore::find(std::string_view map) const {
    const auto it = maps_.find(map);
    return it != maps_.end() ? &it->second : nullptr;
}

void RemainsStore::clear() {
    maps_.clear();
    ++revision_;
}

std::string RemainsStore::to_json() const {
    json::Writer w(true);
    w.begin_object();
    w.member("format", kRemainsFormat);
    w.key("maps").begin_array();
    for (const auto& [name, m] : maps_) {
        if (m.corpses.empty() && m.stains.empty()) {
            continue;
        }
        w.begin_object();
        w.member("map", name);
        w.key("corpses").begin_array();
        for (const CorpseRemains& c : m.corpses) {
            w.begin_object();
            w.member("chara", c.chara);
            w.member("room", c.room);
            write_triple(w, "position", c.position.x, c.position.y, c.position.z);
            write_triple(w, "rotation", c.rotation.x, c.rotation.y, c.rotation.z);
            w.key("pose").inline_next().begin_object();
            w.member("clip", static_cast<int>(c.clip_status));
            w.member("time", c.clip_time);
            w.member("keyframe", c.keyframe);
            w.end_object();
            w.member("palette", static_cast<int>(c.palette));
            w.member("timer", c.timer);
            w.member("variant", static_cast<int>(c.variant));
            w.end_object();
        }
        w.end_array();
        w.key("stains").begin_array();
        for (const BloodStain& s : m.stains) {
            w.inline_next().begin_object();
            w.member("room", s.room);
            w.key("position").begin_array().value(s.position.x).value(s.position.y).value(s.position.z).end_array();
            w.member("size", s.size);
            w.member("color", static_cast<int>(s.color));
            w.member("shape", static_cast<int>(s.shape));
            w.end_object();
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.take();
}

Result<RemainsStore> RemainsStore::from_json(std::string_view text) {
    Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<RemainsStore>::fail(doc.error());
    }
    const json::Value& v = doc.value();
    const json::Value* format = v.is_object() ? v.find("format") : nullptr;
    if (format == nullptr || !format->is_string() || format->string() != kRemainsFormat) {
        return Result<RemainsStore>::fail("not an sh-remains/1 document");
    }
    RemainsStore store;
    Reader r;
    const json::Value* maps = v.find("maps");
    if (maps == nullptr || !maps->is_array()) {
        return Result<RemainsStore>::fail("missing \"maps\"");
    }
    for (const json::Value& entry : maps->array()) {
        const json::Value* name = entry.is_object() ? entry.find("map") : nullptr;
        if (name == nullptr || !name->is_string() || name->string().empty()) {
            return Result<RemainsStore>::fail("a map entry has no name");
        }
        MapRemains& m = store.maps_[name->string()];
        if (const json::Value* corpses = entry.find("corpses"); corpses != nullptr && corpses->is_array()) {
            for (const json::Value& e : corpses->array()) {
                const json::Value* chara = e.is_object() ? e.find("chara") : nullptr;
                if (chara == nullptr || !chara->is_string()) {
                    return Result<RemainsStore>::fail("a corpse has no character");
                }
                CorpseRemains c;
                c.chara = chara->string();
                r.integer(e, "room", c.room, false);
                r.triple(e, "position", c.position.x, c.position.y, c.position.z);
                r.triple(e, "rotation", c.rotation.x, c.rotation.y, c.rotation.z);
                if (const json::Value* pose = e.find("pose"); pose != nullptr && pose->is_object()) {
                    r.integer(*pose, "clip", c.clip_status);
                    r.integer(*pose, "time", c.clip_time);
                    r.integer(*pose, "keyframe", c.keyframe);
                } else {
                    r.fail("a corpse has no pose");
                }
                r.integer(e, "palette", c.palette, false);
                r.integer(e, "timer", c.timer, false);
                r.integer(e, "variant", c.variant, false);
                m.corpses.push_back(std::move(c));
            }
        }
        if (const json::Value* stains = entry.find("stains"); stains != nullptr && stains->is_array()) {
            for (const json::Value& e : stains->array()) {
                if (!e.is_object()) {
                    return Result<RemainsStore>::fail("a stain is not an object");
                }
                BloodStain s;
                r.integer(e, "room", s.room, false);
                r.triple(e, "position", s.position.x, s.position.y, s.position.z);
                r.integer(e, "size", s.size);
                r.integer(e, "color", s.color, false);
                r.integer(e, "shape", s.shape, false);
                m.stains.push_back(s);
            }
        }
        trim(m.corpses, kMaxCorpses);
        trim(m.stains, kMaxStains);
    }
    if (!r.error.empty()) {
        return Result<RemainsStore>::fail(r.error);
    }
    store.revision_ = 1;
    return Result<RemainsStore>::ok(std::move(store));
}

bool remains_within(std::int32_t x, std::int32_t z, std::int32_t from_x, std::int32_t from_z, std::int32_t reach) {
    const std::int64_t dx = static_cast<std::int64_t>(x) - from_x;
    const std::int64_t dz = static_cast<std::int64_t>(z) - from_z;
    return dx * dx + dz * dz <= static_cast<std::int64_t>(reach) * reach;
}

}  // namespace sh::entities
