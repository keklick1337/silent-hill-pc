// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Named binds of the map effects, the effect slots, the world objects and the glow fields:
//   world.effects_track(cell, preset0, preset1, min, max): the map effects blend from preset0 to preset1 as the
//       value of tracked cell `cell` (0..3) goes from min to max; world.effects_track_set(cell, value) sets the value.
//       The effects keep the cell's address, so the cells live here, outside any script state.
//   world.effect_slot_alloc(type) -> slot or -1; world.effect_slot_get(slot, field) / effect_slot_set(slot, field,
//       value): a field of a map effect slot by name: x, z (32-bit), y (16-bit), type, variant (bytes), and the
//       8-byte payload as s32_0..1, s16_0..3, u16_0..3, u8_0..7. A write keeps the field's width.
//   world.darkness_at(x, y, z) -> 0..255. world.light_flags() / world.effects_flags(): the flag byte of the current
//       environment preset (bit 0 the dark set-up, bit 1 an area where the flashlight matters).
//   world.object_draw(name, x, y, z, rx, ry, rz): one more draw this tick of a "script" world object at that pose.
//   world.script_call(name[, a, b, c]) / world_objects.call(...): the global `name` of the loaded map's
//       world_objects.lua with integer arguments; 1 when it ran.
//   effects.slot_y_add(type, dy, count) / effects.slot_free(type, count): over the first `count` slots, the slots
//       of that type move up by dy / are freed. effects.smoke_column_start(): the map's smoke column starts.
//   effects.glow_field_<op>(name, ...): the glow field `name` (gamedata/effects/glow_fields.json): reset, decay,
//       draw, draw_mesh(name, mesh), emit, clear_emitters, remove(name, kind), add_point(name, posRef, rotRef),
//       add_bones(name, posRef, bonesRef, timerRef), add_ray(name, fromRef, toRef), add_flash(name, timerRef). A
//       "ref" is a reference of the named API (a map variable name, "pos:2", "npc_bone:30", ...).
//   combat.table_point_set(x, y, z, kind, value): a point of the combat table the map's effects read.
//   map6_s02.lake_effect_init(rebuild): the lake surface textures set up (0) or rebuilt (1), on a map whose
//       overlay.json declares the "lake_surface" effect.

#include "engine/runtime/world_object_runtime.h"
#include "engine/scripting/binds/binds.h"

#include <cstring>
#include <string_view>

#include "engine/game_state/session.h"
#include "engine/scripting/binds/game_links.h"
#include "engine/effects/named_effects.h"

namespace {

// The named glow fields of the engine (engine/effects/named_effects.h) under the names the bind table uses.
void GlowField_Reset(const char* name) { sh::effects::glow_fields().reset(name); }
void GlowField_Decay(const char* name) { sh::effects::glow_fields().decay(name); }
void GlowField_Draw(const char* name) { sh::effects::glow_fields().draw(name); }
void GlowField_DrawMesh(const char* name, const char* mesh) { sh::effects::glow_fields().draw_mesh(name, mesh); }
void GlowField_AddPoint(const char* name, const void* position, const void* rotation) {
    sh::effects::glow_fields().add_point(name, position, rotation);
}
void GlowField_AddBones(const char* name, const void* position, void* bones, const std::int32_t* timer) {
    sh::effects::glow_fields().add_bones(name, position, bones, timer);
}
void GlowField_AddRay(const char* name, const void* from, const void* to) {
    sh::effects::glow_fields().add_ray(name, from, to);
}
void GlowField_AddFlash(const char* name, const std::int32_t* timer) { sh::effects::glow_fields().add_flash(name, timer); }
void GlowField_Remove(const char* name, const char* kind) { sh::effects::glow_fields().remove(name, kind); }
void GlowField_ClearEmitters(const char* name) { sh::effects::glow_fields().clear_emitters(name); }
void GlowField_Emit(const char* name) { sh::effects::glow_fields().emit(name); }

}  // namespace

namespace sh::script::binds {

namespace {

using named::Call;
namespace links = sh::script::links;

constexpr int kTrackCells = 4;
std::int32_t g_track_cells[kTrackCells] = {};
// The value type the map effects read through the tracked cell's address: a 32-bit integer.
constexpr int kTrackValueS32 = 5;

bool cell_ok(Call& c, long long cell) {
    if (cell < 0 || cell >= kTrackCells) {
        c.fail("%s: cell must be 0..%d", c.name(), kTrackCells - 1);
        return false;
    }
    return true;
}

int effects_track(Call& c) {
    long long v[5];
    if (!c.ints(5, v) || !cell_ok(c, v[0])) {
        return -1;
    }
    Gfx_MapEffectsUpdate(static_cast<std::int32_t>(v[1]), static_cast<std::int32_t>(v[2]), kTrackValueS32,
                         &g_track_cells[v[0]], static_cast<std::int32_t>(v[3]), static_cast<std::int32_t>(v[4]));
    return 0;
}

int effects_track_set(Call& c) {
    long long v[2];
    if (!c.ints(2, v) || !cell_ok(c, v[0])) {
        return -1;
    }
    g_track_cells[v[0]] = static_cast<std::int32_t>(v[1]);
    return 0;
}

// ---- effect slots ----

enum class Width { S32, S16, U16, U8 };
struct SlotField {
    const char* name;
    int offset;  // byte offset in the 20-byte slot record
    Width width;
};
constexpr SlotField kSlotFields[] = {
    {"x", 0, Width::S32},       {"z", 4, Width::S32},       {"y", 8, Width::S16},       {"type", 10, Width::U8},
    {"variant", 11, Width::U8}, {"s32_0", 12, Width::S32}, {"s32_1", 16, Width::S32}, {"s16_0", 12, Width::S16},
    {"s16_1", 14, Width::S16},  {"s16_2", 16, Width::S16}, {"s16_3", 18, Width::S16}, {"u16_0", 12, Width::U16},
    {"u16_1", 14, Width::U16},  {"u16_2", 16, Width::U16}, {"u16_3", 18, Width::U16}, {"u8_0", 12, Width::U8},
    {"u8_1", 13, Width::U8},    {"u8_2", 14, Width::U8},   {"u8_3", 15, Width::U8},   {"u8_4", 16, Width::U8},
    {"u8_5", 17, Width::U8},    {"u8_6", 18, Width::U8},   {"u8_7", 19, Width::U8},
};

// The slot as its 20 record bytes (little-endian, as the table stores it).
struct SlotBytes {
    unsigned char b[20];
};

SlotBytes to_bytes(const sh::effects::EffectSlot& s) {
    SlotBytes r{};
    std::memcpy(r.b + 0, &s.x, 4);
    std::memcpy(r.b + 4, &s.z, 4);
    std::memcpy(r.b + 8, &s.y, 2);
    r.b[10] = s.kind;
    r.b[11] = s.flags;
    std::memcpy(r.b + 12, &s.a, 4);
    std::memcpy(r.b + 16, &s.b, 4);
    return r;
}

sh::effects::EffectSlot from_bytes(const SlotBytes& r) {
    sh::effects::EffectSlot s;
    std::memcpy(&s.x, r.b + 0, 4);
    std::memcpy(&s.z, r.b + 4, 4);
    std::memcpy(&s.y, r.b + 8, 2);
    s.kind = r.b[10];
    s.flags = r.b[11];
    std::memcpy(&s.a, r.b + 12, 4);
    std::memcpy(&s.b, r.b + 16, 4);
    return s;
}

long long field_read(const SlotBytes& r, const SlotField& f) {
    switch (f.width) {
        case Width::S32: { std::int32_t v; std::memcpy(&v, r.b + f.offset, 4); return v; }
        case Width::S16: { std::int16_t v; std::memcpy(&v, r.b + f.offset, 2); return v; }
        case Width::U16: { std::uint16_t v; std::memcpy(&v, r.b + f.offset, 2); return v; }
        case Width::U8: return r.b[f.offset];
    }
    return 0;
}

void field_write(SlotBytes& r, const SlotField& f, long long value) {
    switch (f.width) {
        case Width::S32: { const auto v = static_cast<std::uint32_t>(value); std::memcpy(r.b + f.offset, &v, 4); break; }
        case Width::S16:
        case Width::U16: { const auto v = static_cast<std::uint16_t>(value); std::memcpy(r.b + f.offset, &v, 2); break; }
        case Width::U8: r.b[f.offset] = static_cast<unsigned char>(value); break;
    }
}

const SlotField* slot_field(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (const SlotField& f : kSlotFields) {
        if (std::strcmp(f.name, name) == 0) {
            return &f;
        }
    }
    return nullptr;
}

int effect_slot_alloc(Call& c) {
    if (!c.is_int(0) || c.integer(0) < 1 || c.integer(0) > 255) {
        return c.fail("%s takes the slot type 1..255", c.name());
    }
    if (!links::effect_slots_present()) {
        return c.fail("%s: no map is loaded", c.name());
    }
    return c.reply({links::effect_slot_alloc(static_cast<int>(c.integer(0)))});
}

int effect_slot_access(Call& c, bool set) {
    if (c.argc() < (set ? 3 : 2) || !c.is_int(0) || (set && !c.is_int(2))) {
        return c.fail(set ? "%s takes (slot, field name, value)" : "%s takes (slot, field name)", c.name());
    }
    const long long slot = c.integer(0);
    if (!links::effect_slots_present() || slot < 0 || slot >= links::effect_slot_count()) {
        return c.fail("%s: no such effect slot", c.name());
    }
    const SlotField* f = slot_field(c.text(1));
    if (f == nullptr) {
        return c.fail("%s: unknown field (x, y, z, type, variant, s32_N, s16_N, u16_N, u8_N)", c.name());
    }
    SlotBytes bytes = to_bytes(links::effect_slot_load(static_cast<int>(slot)));
    if (set) {
        field_write(bytes, *f, c.integer(2));
        links::effect_slot_store(static_cast<int>(slot), from_bytes(bytes));
        return 0;
    }
    return c.reply({field_read(bytes, *f)});
}

int effect_slot_get(Call& c) { return effect_slot_access(c, false); }
int effect_slot_set(Call& c) { return effect_slot_access(c, true); }

int darkness_at(Call& c) {
    if (c.argc() < 3 || !c.ints_at(0, 3)) {
        return c.fail("%s takes a world position (x, y, z)", c.name());
    }
    return c.reply({WorldEnv_DarknessAtGet(static_cast<std::int32_t>(c.integer(0)),
                                           static_cast<std::int32_t>(c.integer(1)),
                                           static_cast<std::int32_t>(c.integer(2)))});
}

int light_flags(Call& c) { return c.reply({sh::game::session().system.environment.current.preset.flags}); }

int effects_flags(Call& c) {
    if (c.argc() != 0) {
        return c.fail("%s: no arguments expected", c.name());
    }
    return light_flags(c);
}

// ---- world objects and the map script ----

int object_draw(Call& c) {
    if (c.argc() != 7 || !c.is_text(0)) {
        return c.fail("%s: (name, x, y, z, rx, ry, rz) expected", c.name());
    }
    for (int i = 1; i < 7; ++i) {
        if (!c.is_int(i)) {
            return c.fail("%s: argument %d must be a number", c.name(), i);
        }
    }
    const std::int32_t pos[3] = {static_cast<std::int32_t>(c.integer(1)), static_cast<std::int32_t>(c.integer(2)),
                                 static_cast<std::int32_t>(c.integer(3))};
    const std::int16_t rot[3] = {static_cast<std::int16_t>(c.integer(4)), static_cast<std::int16_t>(c.integer(5)),
                                 static_cast<std::int16_t>(c.integer(6))};
    char err[160] = {};
    if (!links::world_object_draw(c.text(0), pos, rot, err, sizeof err)) {
        return c.fail("%s", err);
    }
    return 0;
}

int script_call(Call& c) {
    if (!c.is_text(0) || c.argc() > 4 || !c.ints_at(1, c.argc() - 1)) {
        return c.fail("%s takes a function name and up to three integers", c.name());
    }
    return c.reply({sh::world::world_object_runtime().call(c.text(0), c.integer(1), c.integer(2), c.integer(3)) ? 1 : 0});
}

// ---- effect slot passes of the map events ----

// The exe's effect slot table has room for this many.
constexpr long long kEffectSlotsMax = 500;

int slot_pass(Call& c, bool move) {
    const int n = move ? 3 : 2;
    if (c.argc() != n || !c.ints_at(0, n)) {
        return c.fail("%s: bad arguments", c.name());
    }
    const long long type = c.integer(0);
    const long long count = c.integer(n - 1);
    if (!links::effect_slots_present() || count < 0 || count > kEffectSlotsMax) {
        return c.fail("%s: bad slot count", c.name());
    }
    for (int i = 0; i < count; ++i) {
        sh::effects::EffectSlot s = links::effect_slot_load(i);
        if (s.kind != type) {
            continue;
        }
        if (move) {
            s.y = static_cast<std::int16_t>(s.y - c.integer(1));
        } else {
            s.kind = 0;
        }
        links::effect_slot_store(i, s);
    }
    return 0;
}

int slot_y_add(Call& c) { return slot_pass(c, true); }
int slot_free(Call& c) { return slot_pass(c, false); }

int smoke_column_start(Call& c) {
    if (!links::smoke_column_present()) {
        return c.fail("%s: the map has no smoke column", c.name());
    }
    SlotFx_SmokeStart();
    return 0;
}

// ---- glow fields ----

// A reference argument: nil (or 0) is no address, a string the named API's reference.
bool ref(Call& c, int i, void** out) {
    *out = nullptr;
    if (c.is_nil_or_zero(i)) {
        return true;
    }
    if (!c.is_text(i)) {
        c.fail("a reference argument takes a reference name or nil");
        return false;
    }
    char err[160] = {};
    *out = PcScriptNamed_RefAddr(c.text(i), err, sizeof err);
    if (*out == nullptr) {
        c.fail("%s", err);
        return false;
    }
    return true;
}

const char* glow_op(const Call& c) { return c.name() + std::strlen("effects.glow_field_"); }

bool glow_name(Call& c) {
    if (!c.is_text(0)) {
        c.fail("effects.glow_field_%s takes the field's name first", glow_op(c));
        return false;
    }
    return true;
}

template <void (*Fn)(const char*)>
int glow_simple(Call& c) {
    if (!glow_name(c)) {
        return -1;
    }
    Fn(c.text(0));
    return 0;
}

template <void (*Fn)(const char*, const char*)>
int glow_named(Call& c) {
    if (!glow_name(c)) {
        return -1;
    }
    if (!c.is_text(1)) {
        return c.fail("effects.glow_field_%s takes a name second", glow_op(c));
    }
    Fn(c.text(0), c.text(1));
    return 0;
}

// The reference arguments 1..3.
bool glow_refs(Call& c, void* r[3]) {
    return glow_name(c) && ref(c, 1, &r[0]) && ref(c, 2, &r[1]) && ref(c, 3, &r[2]);
}

int glow_add_point(Call& c) {
    void* r[3];
    if (!glow_refs(c, r)) {
        return -1;
    }
    GlowField_AddPoint(c.text(0), r[0], r[1]);
    return 0;
}

int glow_add_bones(Call& c) {
    void* r[3];
    if (!glow_refs(c, r)) {
        return -1;
    }
    GlowField_AddBones(c.text(0), r[0], r[1], static_cast<const std::int32_t*>(r[2]));
    return 0;
}

int glow_add_ray(Call& c) {
    void* r[3];
    if (!glow_refs(c, r)) {
        return -1;
    }
    GlowField_AddRay(c.text(0), r[0], r[1]);
    return 0;
}

int glow_add_flash(Call& c) {
    void* r[3];
    if (!glow_refs(c, r)) {
        return -1;
    }
    GlowField_AddFlash(c.text(0), static_cast<const std::int32_t*>(r[0]));
    return 0;
}

// ---- combat table and the lake ----

int table_point_set(Call& c) {
    std::int32_t pos[3] = {static_cast<std::int32_t>(c.integer(0)), static_cast<std::int32_t>(c.integer(1)),
                           static_cast<std::int32_t>(c.integer(2))};
    Combat_VectorUnkSet(pos, static_cast<std::int8_t>(c.integer(3)), static_cast<std::int16_t>(c.integer(4)));
    return 0;
}

int lake_effect_init(Call& c) {
    if (LakeSurface_TexturesInitIfDeclared(static_cast<int>(c.integer(0))) == 0) {
        return c.fail("the lake surface effect is not declared on this map");
    }
    return 0;
}

const named::Bind kBinds[] = {
    {"world.effects_track", effects_track},
    {"world.effects_track_set", effects_track_set},
    {"world.effect_slot_alloc", effect_slot_alloc},
    {"world.effect_slot_get", effect_slot_get},
    {"world.effect_slot_set", effect_slot_set},
    {"world.darkness_at", darkness_at},
    {"world.light_flags", light_flags},
    {"world.effects_flags", effects_flags},
    {"world.object_draw", object_draw},
    {"world.script_call", script_call},
    {"world_objects.call", script_call},
    {"effects.slot_y_add", slot_y_add},
    {"effects.slot_free", slot_free},
    {"effects.smoke_column_start", smoke_column_start},
    {"effects.glow_field_reset", glow_simple<GlowField_Reset>},
    {"effects.glow_field_decay", glow_simple<GlowField_Decay>},
    {"effects.glow_field_draw", glow_simple<GlowField_Draw>},
    {"effects.glow_field_emit", glow_simple<GlowField_Emit>},
    {"effects.glow_field_clear_emitters", glow_simple<GlowField_ClearEmitters>},
    {"effects.glow_field_draw_mesh", glow_named<GlowField_DrawMesh>},
    {"effects.glow_field_remove", glow_named<GlowField_Remove>},
    {"effects.glow_field_add_point", glow_add_point},
    {"effects.glow_field_add_bones", glow_add_bones},
    {"effects.glow_field_add_ray", glow_add_ray},
    {"effects.glow_field_add_flash", glow_add_flash},
    {"combat.table_point_set", table_point_set},
    {"map6_s02.lake_effect_init", lake_effect_init},
};

}  // namespace

std::span<const named::Bind> world_binds() { return kBinds; }

}  // namespace sh::script::binds
