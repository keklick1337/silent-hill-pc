// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/math/precise_capture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "engine/math/geometry_state.h"
#include "render/gpu/render_state.h"

namespace sh::math::capture {

namespace {

using RotationKey = std::array<std::int16_t, 9>;
using TranslationKey = std::array<std::int32_t, 3>;
using Rotation = std::array<double, 9>;
using Translation = std::array<double, 3>;

constexpr RotationKey kUnitKey = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
constexpr Rotation kUnit = {1, 0, 0, 0, 1, 0, 0, 0, 1};
// A unit matrix squeezed to three quarters in y: every element is exact in Q12, so its twin is known without
// anyone noting it.
constexpr RotationKey kSquatKey = {4096, 0, 0, 0, 3072, 0, 0, 0, 4096};
constexpr Rotation kSquat = {1, 0, 0, 0, 0.75, 0, 0, 0, 1};

bool enabled() { return sh::render::precise().enabled != 0; }

RotationKey key_of(const Matrix3& m) {
    RotationKey k;
    for (std::size_t i = 0; i < 9; ++i) k[i] = m.m[i / 3][i % 3];
    return k;
}
TranslationKey key_of(const Vec3i& t) { return {t.x, t.y, t.z}; }

// The integers of a record in memory. A record is read through its address alone, as bytes: callers hand in
// matrices of several record types that share this layout (nine Q12 elements, then three 32-bit words at offset
// 20).
RotationKey rotation_at(const void* record) {
    RotationKey k;
    std::memcpy(k.data(), record, sizeof(k));
    return k;
}
TranslationKey translation_at(const void* record) {
    TranslationKey k;
    std::memcpy(k.data(), static_cast<const std::uint8_t*>(record) + offsetof(Transform, trans), sizeof(k));
    return k;
}

Rotation as_exact(const RotationKey& k) {
    Rotation e;
    for (std::size_t i = 0; i < 9; ++i) e[i] = static_cast<double>(k[i]) / 4096.0;
    return e;
}
Translation as_exact(const TranslationKey& k) {
    return {static_cast<double>(k[0]), static_cast<double>(k[1]), static_cast<double>(k[2])};
}

template <typename T, std::size_t N>
void store(const std::array<T, N>& from, T* to) {
    std::memcpy(to, from.data(), sizeof(from));
}
template <typename T, std::size_t N>
std::array<T, N> load(const T* from) {
    std::array<T, N> a;
    std::memcpy(a.data(), from, sizeof(a));
    return a;
}

// The frame counter stamps every entry: an entry of an earlier frame counts as free, so ending the frame clears all
// tables at once. It starts at 1 and the tables start zeroed, so nothing matches before the first note.
unsigned g_frame = 1;

constexpr unsigned kProbes = 16;

std::uint32_t mix(std::uint32_t h, std::uint32_t v) { return (h ^ v) * 16777619u; }
template <typename T, std::size_t N>
std::uint32_t hash_of(const std::array<T, N>& key) {
    std::uint32_t h = 2166136261u;
    for (const T v : key) h = mix(h, static_cast<std::uint32_t>(v));
    return h;
}
std::uint32_t hash_of(std::uintptr_t address) { return static_cast<std::uint32_t>(address >> 2) * 2654435761u; }

// A fixed table of this frame's entries under a key, searched over a short run of neighbours. When a run is full
// of live entries the newest note replaces the one at the run's start: a lost twin only costs precision.
template <typename Key, typename Value, unsigned Bits>
class FrameTable {
public:
    struct Entry {
        Key key{};
        unsigned frame = 0;
        Value value{};
    };

    // The entry to write `key` into.
    Entry& claim(const Key& key) {
        const std::uint32_t home = hash_of(key);
        for (unsigned i = 0; i < kProbes; ++i) {
            Entry& e = at(home + i);
            if (e.frame != g_frame || e.key == key) return fresh(e, key);
        }
        return fresh(at(home), key);
    }
    // This frame's entry of `key`, or null. Entries of one run are written front to back, so the first free one
    // ends the search.
    Entry* find(const Key& key) {
        const std::uint32_t home = hash_of(key);
        for (unsigned i = 0; i < kProbes; ++i) {
            Entry& e = at(home + i);
            if (e.frame != g_frame) return nullptr;
            if (e.key == key) return &e;
        }
        return nullptr;
    }

private:
    Entry& at(std::uint32_t index) { return entries_[index & ((1u << Bits) - 1)]; }
    static Entry& fresh(Entry& e, const Key& key) {
        e.key = key;
        e.frame = g_frame;
        return e;
    }

    std::unique_ptr<Entry[]> entries_ = std::make_unique<Entry[]>(std::size_t{1} << Bits);
};

// What is known of a record at an address: the integers the twin was noted for, and the twin, or the mark that
// the exact values of those integers are unknown.
template <typename Key, typename Exact>
struct Twin {
    Key integers{};
    Exact exact{};
    bool known = false;
};

struct Tables {
    FrameTable<std::uintptr_t, Twin<RotationKey, Rotation>, 14> rotations;
    FrameTable<std::uintptr_t, Twin<TranslationKey, Translation>, 14> translations;
    FrameTable<RotationKey, Rotation, 12> derived_rotations;
    FrameTable<TranslationKey, Translation, 12> derived_translations;
};
Tables& tables() {
    static Tables t;
    return t;
}

CameraTwin g_camera;
bool g_camera_set = false;

// The twin of the loaded view, with the integers it stands for.
struct Loaded {
    Twin<RotationKey, Rotation> rotation;
    Twin<TranslationKey, Translation> translation;
};
Loaded g_loaded;

bool derived(const RotationKey& key, Rotation& out) {
    if (!g_camera_set) return false;
    if (key == key_of(g_camera.key.rot)) {
        out = load<double, 9>(g_camera.rot);
        return true;
    }
    if (const auto* e = tables().derived_rotations.find(key)) {
        out = e->value;
        return true;
    }
    return false;
}
bool derived(const TranslationKey& key, Translation& out) {
    if (!g_camera_set) return false;
    if (key == key_of(g_camera.key.trans)) {
        out = load<double, 3>(g_camera.trans);
        return true;
    }
    if (const auto* e = tables().derived_translations.find(key)) {
        out = e->value;
        return true;
    }
    return false;
}

std::uintptr_t address(const void* record) { return reinterpret_cast<std::uintptr_t>(record); }

void note(const void* record, const RotationKey& integers, const Rotation* exact) {
    auto& twin = tables().rotations.claim(address(record)).value;
    twin.integers = integers;
    twin.known = exact != nullptr;
    if (exact != nullptr) twin.exact = *exact;
}
void note(const void* record, const TranslationKey& integers, const Translation* exact) {
    auto& twin = tables().translations.claim(address(record)).value;
    twin.integers = integers;
    twin.known = exact != nullptr;
    if (exact != nullptr) twin.exact = *exact;
}

// The twin of the rotation at `record`, noting what was worked out so the next lookup finds it at the address. A
// record marked as unknown for its present integers gets those integers and nothing better: it was built from
// values nobody kept.
Rotation resolve_rotation(const void* record) {
    const RotationKey integers = rotation_at(record);
    const auto* e = tables().rotations.find(address(record));
    const bool same = e != nullptr && e->value.integers == integers;
    if (same && e->value.known) return e->value.exact;
    Rotation exact;
    if (same || !derived(integers, exact)) {
        exact = integers == kUnitKey ? kUnit : integers == kSquatKey ? kSquat : as_exact(integers);
    }
    note(record, integers, &exact);
    return exact;
}
Translation resolve_translation(const void* record) {
    const TranslationKey integers = translation_at(record);
    const auto* e = tables().translations.find(address(record));
    const bool same = e != nullptr && e->value.integers == integers;
    if (same && e->value.known) return e->value.exact;
    Translation exact;
    // A whole-unit translation of unknown origin is exact as it stands.
    if (same || !derived(integers, exact)) exact = as_exact(integers);
    note(record, integers, &exact);
    return exact;
}

}  // namespace

void note_rotation(const void* record, const double exact[9]) {
    if (!enabled() || record == nullptr || exact == nullptr) return;
    const Rotation e = load<double, 9>(exact);
    note(record, rotation_at(record), &e);
}

void note_translation(const void* record, const double exact[3]) {
    if (!enabled() || record == nullptr || exact == nullptr) return;
    const Translation e = load<double, 3>(exact);
    note(record, translation_at(record), &e);
}

void forget_rotation(const void* record) {
    if (!enabled() || record == nullptr) return;
    note(record, rotation_at(record), static_cast<const Rotation*>(nullptr));
}

void forget_translation(const void* record) {
    if (!enabled() || record == nullptr) return;
    note(record, translation_at(record), static_cast<const Translation*>(nullptr));
}

void copy_rotation(const void* to, const void* from) {
    if (!enabled() || to == nullptr || from == nullptr) return;
    const Rotation exact = resolve_rotation(from);
    note(to, rotation_at(to), &exact);
}

bool rotation_of(const void* record, double out[9]) {
    if (!enabled() || record == nullptr || out == nullptr) return false;
    store(resolve_rotation(record), out);
    return true;
}

bool translation_of(const void* record, double out[3]) {
    if (!enabled() || record == nullptr || out == nullptr) return false;
    store(resolve_translation(record), out);
    return true;
}

void rotation_loaded(const void* record) {
    if (!enabled()) return;
    g_loaded.rotation.known = false;
    if (record == nullptr) return;
    const RotationKey loaded = key_of(geometry().rotation());
    if (rotation_at(record) != loaded) return;
    g_loaded.rotation = {loaded, resolve_rotation(record), true};
}

void translation_loaded(const void* record) {
    if (!enabled()) return;
    g_loaded.translation.known = false;
    if (record == nullptr) return;
    const TranslationKey loaded = key_of(geometry().translation());
    if (translation_at(record) != loaded) return;
    g_loaded.translation = {loaded, resolve_translation(record), true};
}

bool current_rotation(double out[9]) {
    if (!enabled() || out == nullptr) return false;
    const RotationKey loaded = key_of(geometry().rotation());
    if (g_loaded.rotation.known && g_loaded.rotation.integers == loaded) {
        store(g_loaded.rotation.exact, out);
    } else if (loaded == kUnitKey) {
        store(kUnit, out);
    } else if (loaded == kSquatKey) {
        store(kSquat, out);
    } else {
        return false;
    }
    return true;
}

bool current_translation(double out[3]) {
    if (!enabled() || out == nullptr) return false;
    if (!g_loaded.translation.known || g_loaded.translation.integers != key_of(geometry().translation())) return false;
    store(g_loaded.translation.exact, out);
    return true;
}

void set_camera(const CameraTwin& twin) {
    g_camera = twin;
    g_camera_set = true;
}

void clear_camera() { g_camera_set = false; }

const CameraTwin* camera() { return g_camera_set ? &g_camera : nullptr; }

const CameraTwin* camera_of(const Matrix3& rot, const Vec3i& trans) {
    if (!g_camera_set) return nullptr;
    const bool same = key_of(rot) == key_of(g_camera.key.rot) && key_of(trans) == key_of(g_camera.key.trans);
    return same ? &g_camera : nullptr;
}

void register_derived(const Transform& integer, const double rot[9], const double trans[3]) {
    if (!g_camera_set) return;
    if (rot != nullptr) tables().derived_rotations.claim(key_of(integer.rot)).value = load<double, 9>(rot);
    if (trans != nullptr) tables().derived_translations.claim(key_of(integer.trans)).value = load<double, 3>(trans);
}

void register_camera_point(const Transform& ws, const double point[3], const Transform& integer) {
    const CameraTwin* c = camera_of(ws.rot, ws.trans);
    if (c == nullptr) return;
    double moved[3];
    for (int row = 0; row < 3; ++row) {
        const double* r = &c->rot[row * 3];
        moved[row] = r[0] * point[0] + r[1] * point[1] + r[2] * point[2] + c->trans[row];
    }
    register_derived(integer, nullptr, moved);
}

void register_camera_turn(const Transform& ws, const Matrix3& turn, const Transform& integer) {
    const CameraTwin* c = camera_of(ws.rot, ws.trans);
    if (c == nullptr) return;
    double turned[9];
    for (int row = 0; row < 3; ++row) {
        const double* r = &c->rot[row * 3];
        for (int col = 0; col < 3; ++col) {
            turned[row * 3 + col] = (r[0] * turn.m[0][col] + r[1] * turn.m[1][col] + r[2] * turn.m[2][col]) / 4096.0;
        }
    }
    register_derived(integer, turned, nullptr);
}

bool derived_rotation(const Matrix3& rot, double out[9]) {
    Rotation exact;
    if (!derived(key_of(rot), exact)) return false;
    store(exact, out);
    return true;
}

bool derived_translation(const Vec3i& trans, double out[3]) {
    Translation exact;
    if (!derived(key_of(trans), exact)) return false;
    store(exact, out);
    return true;
}

void end_frame() { ++g_frame; }

}  // namespace sh::math::capture
