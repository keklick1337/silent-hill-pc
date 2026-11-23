// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the glow fields (sh::effects::GlowField) with a scripted host.

#include "engine/effects/glow_field.h"

#include <algorithm>
#include <string>
#include <vector>

#include "framework/sh_test.h"

using namespace sh::effects;

namespace {

class Host final : public GlowFieldHost {
public:
    std::int32_t roll = 0x30;
    int rolls = 0;
    std::vector<GlowQuad> quads;
    std::vector<std::int32_t> slots;
    std::vector<std::int32_t> page_slots;
    int pages = 0;
    int meshes = 0;
    std::int32_t page_slot = -1;
    std::int16_t last_corner[3] = {};

    std::int32_t rand16() override {
        rolls++;
        return roll;
    }
    void emit_quad(const GlowQuad& q, std::int32_t slot) override {
        quads.push_back(q);
        slots.push_back(slot);
    }
    void emit_page(std::int32_t slot, std::int32_t, std::int32_t) override {
        pages++;
        page_slot = slot;
        page_slots.push_back(slot);
    }
    void begin_mesh(const GlowMesh&) override { meshes++; }
    void project4(const std::int16_t (&v)[4][3], std::int16_t (&x)[4], std::int16_t (&y)[4]) override {
        for (int k = 0; k < 4; k++) {
            x[k] = v[k][0];
            y[k] = v[k][2];
        }
        std::copy(v[3], v[3] + 3, last_corner);
    }
};

GlowFieldParams small() {
    GlowFieldParams p;
    p.cols = 28;
    p.rows = 19;
    p.cell = 12;
    p.origin_x = -166;
    p.origin_y = -112;
    p.extent_x = 332;
    p.extent_y = 224;
    for (int i = 0; i < 256; i++) {
        p.shade[i] = static_cast<std::uint32_t>(i);
    }
    return p;
}

}  // namespace

SH_TEST(glow_field_kernel_peaks_in_the_middle) {
    GlowField f(small());
    f.reset();
    SH_CHECK(f.kernel().size() == 129u * 129u);
    SH_CHECK(f.kernel()[64 * 129 + 64] == 255);  // (64 << 8) / 64 clamped
    SH_CHECK(f.kernel()[0] == 0);                // a corner is past the radius
    SH_CHECK(f.kernel()[64 * 129 + 32] == 128);  // half way out
}

SH_TEST(glow_field_splat_lights_cells_around_the_point) {
    GlowField f(small());
    f.reset();
    f.splat(0, 0, 40, 4096);
    // Screen (0, 0) is grid pixel (166, 112): cell (9, 13).
    const auto& c = f.cells();
    SH_CHECK(c[9 * 28 + 13] > 150);
    SH_CHECK(c[0] == 0);
    SH_CHECK(c[18 * 28 + 27] == 0);
    // Saturates at 255.
    for (int i = 0; i < 4; i++) {
        f.splat(0, 0, 40, 4096);
    }
    SH_CHECK(f.cells()[9 * 28 + 13] == 255);
    // A spot that shrinks to nothing does nothing.
    const std::vector<std::uint8_t> before = f.cells();
    f.splat(0, 0, 1, 1);
    SH_CHECK(f.cells() == before);
}

SH_TEST(glow_field_decay_rolls_once_per_lit_cell) {
    GlowField f(small());
    f.reset();
    f.splat(0, 0, 20, 4096);
    int lit = 0;
    for (std::uint8_t v : f.cells()) {
        lit += v != 0 ? 1 : 0;
    }
    Host h;
    h.roll = 0;  // -2 a frame
    const std::uint8_t peak = f.cells()[9 * 28 + 13];
    f.decay(h);
    SH_CHECK(h.rolls == lit);
    SH_CHECK(f.cells()[9 * 28 + 13] == peak - 2);
    h.roll = 0x30;  // -2 + 3
    f.decay(h);
    SH_CHECK(f.cells()[9 * 28 + 13] == peak - 1);
}

SH_TEST(glow_field_draws_every_quad_and_links_lit_ones) {
    GlowField f(small());
    f.reset();
    Host h;
    f.draw(h);
    SH_CHECK(h.quads.size() == 18u * 27u);
    SH_CHECK(h.pages == 1);
    SH_CHECK(h.page_slot == 0);
    bool any = false;
    for (const GlowQuad& q : h.quads) {
        any = any || q.lit;
    }
    SH_CHECK(!any);
    SH_CHECK(h.quads[0].x[0] == -166 && h.quads[0].x[1] == -154 && h.quads[0].y[2] == -100);
    f.splat(0, 0, 30, 4096);
    h.quads.clear();
    f.draw(h);
    SH_CHECK(h.quads[8 * 27 + 12].lit);
    SH_CHECK((h.quads[8 * 27 + 12].color[3] >> 24) == 0x3A);
}

SH_TEST(glow_field_colour_folds_above_the_peak) {
    GlowField f(small());
    f.reset();
    for (int i = 0; i < 10; i++) {
        f.splat(0, 0, 40, 4096);
    }
    const int idx = 9 * 28 + 13;  // 255
    SH_CHECK((f.colour_at(idx, false) & 0xFF) == 255);
    SH_CHECK((f.colour_at(idx, true) & 0xFF) == 0);  // 200 - 255 < 0
    SH_CHECK((f.colour_at(0, true) & 0xFF) == 0);
}

SH_TEST(glow_field_mesh_lays_its_grid_and_splits_the_slots) {
    GlowFieldParams p = small();
    GlowMesh m;
    m.col_axis = 2;
    m.row_axis = 0;
    m.col_step = 256;
    m.col_offset = 1792;
    m.row_step = 7;
    m.row_offset = 66;
    m.far_from_row = 10;
    m.far_slot_add = 300;
    p.meshes.emplace("Ray", m);
    GlowField f(p);
    f.reset();
    Host h;
    SH_CHECK(!f.draw_mesh(h, "Missing"));
    SH_CHECK(f.draw_mesh(h, "Ray"));
    SH_CHECK(h.meshes == 1);
    SH_CHECK(h.quads.size() == 18u * 27u);
    // Row 1, column 1: x = 7 - 66, z = 256 - 1792; the last corner is row 19, column 28.
    SH_CHECK(h.quads[0].projected && h.quads[0].x[0] == -59 && h.quads[0].y[0] == -1536);
    SH_CHECK(h.last_corner[0] == 7 * 19 - 66 && h.last_corner[1] == 0 && h.last_corner[2] == 256 * 28 - 1792);
    SH_CHECK(h.slots[8 * 27] == 0 && h.slots[9 * 27] == 300);
    SH_CHECK(h.page_slots.size() == 2u && h.page_slots[0] == 0 && h.page_slots[1] == 300);
}

SH_TEST(glow_field_mesh_parses_strictly) {
    const std::string head = R"({"fields": {"F": {"cols": 28, "rows": 19, "cell": 12, "origin": [-166, -112],
        "extent": [332, 224], "kernel_radius": 64, "decay_step": 2, "decay_jitter_mask": 48, "decay_jitter_shift": 4,
        "fold_peak": 100, "slot": 0, "prim_code": 58, "mode_tpage": 37, "mode_dtd": 1, "shade": [)";
    std::string shade;
    for (int i = 0; i < 256; i++) {
        shade += i ? ", [0, 0, 0]" : "[0, 0, 0]";
    }
    const auto doc = [&](const std::string& mesh) { return head + shade + R"(], "meshes": {"M": )" + mesh + "}}}}"; };
    const std::string good = R"({"col_axis": "x", "row_axis": "y", "col": {"step": 256, "offset": 3584},
        "row": {"step": 256, "offset": 2432}, "fold": true, "far_from_row": 10, "far_slot_add": 300, "tpage": 69,
        "dtd": 1, "transform": {"position": [-2180, 0, -9312], "rotation": [0, 1206, 0]}})";
    auto parsed = parse_glow_fields(doc(good));
    SH_CHECK(static_cast<bool>(parsed));
    if (parsed) {
        const GlowMesh& m = parsed.value().at("F").meshes.at("M");
        SH_CHECK(m.has_transform && m.position[2] == -9312 && m.rotation[1] == 1206 && m.fold && m.tpage == 69);
    }
    std::string same_axes = good;
    same_axes.replace(same_axes.find("\"y\""), 3, "\"x\"");
    SH_CHECK(!parse_glow_fields(doc(same_axes)));
    SH_CHECK(!parse_glow_fields(doc(R"({"col_axis": "x"})")));
    std::string extra = good;
    extra.insert(1, R"("colour": 1, )");
    SH_CHECK(!parse_glow_fields(doc(extra)));
}

SH_TEST(glow_field_rejects_bad_files) {
    SH_CHECK(!parse_glow_fields(R"({"fields": {"F": {"cols": 2}}})"));
    SH_CHECK(!parse_glow_fields(R"({"other": 1, "fields": {}})"));
    SH_CHECK(static_cast<bool>(parse_glow_fields(R"({"fields": {}})")));
}

SH_TEST(glow_field_shipped_file_loads) {
    std::string source;
    auto loaded = load_glow_fields(std::string(SH_PROJECT_ROOT) + "/gamedata", "", &source);
    SH_CHECK(static_cast<bool>(loaded));
    if (loaded) {
        SH_CHECK(loaded.value().count("FlaurosGlow") == 1);
        const GlowFieldParams& p = loaded.value().at("FlaurosGlow");
        SH_CHECK(p.cols * p.rows == 0x214);
        SH_CHECK(p.mode_tpage == 0x25);
        SH_CHECK(p.meshes.count("FlashPlane") == 1 && p.meshes.count("RayPlane") == 1);
        SH_CHECK(p.meshes.at("FlashPlane").has_transform && !p.meshes.at("RayPlane").has_transform);
        SH_CHECK(p.emitters.capacity == 16 && p.emitters.bones.spots.size() == 6);
        SH_CHECK(p.emitters.point.size_max == 450560 && p.emitters.flash.sound == 1637);
    }
}

namespace {

class EmitterHost final : public GlowFieldHost {
public:
    std::vector<std::int32_t> rolls;
    std::size_t next = 0;
    std::int32_t now = 0;
    std::int32_t depth = 0;
    std::vector<std::string> calls;

    std::int32_t rand16() override { return next < rolls.size() ? rolls[next++] : 0; }
    void emit_quad(const GlowQuad&, std::int32_t) override {}
    void emit_page(std::int32_t, std::int32_t, std::int32_t) override {}
    void view_point(GlowAnchor, GlowAnchor) override { calls.push_back("point"); }
    void view_skeleton(GlowAnchor, std::int32_t scale) override { calls.push_back("skeleton " + std::to_string(scale)); }
    void view_ray(GlowAnchor, GlowAnchor) override { calls.push_back("ray"); }
    Projected project_view(const std::int16_t (&v)[3]) override { return Projected{v[0], v[1], depth}; }
    void bone_translation(GlowAnchor, int bone, std::int32_t (&t)[3]) override {
        t[0] = bone * 4096;
        t[1] = 0;
        t[2] = 0;
    }
    std::int32_t timer(GlowAnchor) override { return now; }
    void flash_cue(std::int32_t sound, std::int32_t rumble) override {
        calls.push_back("cue " + std::to_string(sound) + " " + std::to_string(rumble));
    }
};

GlowFieldParams with_emitters() {
    GlowFieldParams p = small();
    GlowEmitterParams& e = p.emitters;
    e.capacity = 4;
    e.point = {{0, 0, 0}, 16384, 4096, 24576, 2048};
    e.bones.spots = {{0, 4096, 100, 130}, {2, 8192, 10, 20}};
    e.bones.offset_scale = 12288;
    e.bones.base_root = 409600;
    e.bones.base_bone = 163840;
    e.bones.grow = 80;
    e.bones.shrink = 200;
    e.bones.strength_step = 30;
    e.bones.fade_after = 4096;
    e.ray = {30, 2, 332, 4, 224, 2, -56, 64, 10, 2048};
    e.flash = {16384, 1638, 4096, 1637, 8, -108, -20, 10, 12288, 6144, 4096};
    return p;
}

}  // namespace

SH_TEST(glow_emitters_fill_slots_and_update_by_kind) {
    GlowField f(with_emitters());
    EmitterHost h;
    f.reset();
    f.add_point(1, 2);
    f.add_bones(h, 1, 3, 4);  // two spots; the last one does not fit
    f.add_flash(4);
    SH_CHECK(f.emitters()[0].kind == GlowKind::Point && f.emitters()[1].kind == GlowKind::Bones &&
             f.emitters()[2].kind == GlowKind::Bones && f.emitters()[3].kind == GlowKind::Flash);
    SH_CHECK(f.emitters()[2].offset[0] == 2 * 3 * 4096 && f.emitters()[2].base == 163840);
    SH_CHECK(f.emitters()[1].scale == 0 && f.emitters()[1].size == 4096);
    f.emit(h);
    // The point grows by 1.0 up to its limit; the bones swell after their view took the old scale.
    SH_CHECK(f.emitters()[0].size == 20480);
    SH_CHECK(h.calls[0] == "point" && h.calls[1] == "skeleton 0" && h.calls[2] == "skeleton 0");
    SH_CHECK(f.emitters()[1].scale == 80 && f.emitters()[1].size == 409600 + 80 && f.emitters()[1].strength == 130);
    SH_CHECK(f.emitters()[2].strength == 20);
    SH_CHECK(h.calls.size() == 3);  // the flash waits for its time
    h.now = 8192;
    f.emit(h);
    SH_CHECK(f.emitters()[0].size == 24576);
    SH_CHECK(f.emitters()[1].phase == 1 && f.emitters()[3].phase == 1);
    SH_CHECK(h.calls.back() == "cue 1637 8" && f.flash_size() == 10 && f.flash_speed() == 12288);
    f.emit(h);  // the flash spot is drawn, then grows
    SH_CHECK(f.flash_size() == 13 && f.flash_speed() == 18432);
    SH_CHECK(f.emitters()[1].scale == 0 && f.emitters()[1].strength == 130 - 30);
    f.remove(GlowKind::Bones);
    SH_CHECK(f.emitters()[0].active && !f.emitters()[1].active && !f.emitters()[2].active && f.emitters()[3].active);
    f.clear_emitters();
    SH_CHECK(!f.emitters()[0].active && !f.emitters()[3].active);
}

SH_TEST(glow_emitters_ray_rolls_right_to_left) {
    GlowField f(with_emitters());
    EmitterHost h;
    f.reset();
    f.add_ray(h, 1, 2);
    SH_CHECK(h.calls.size() == 1 && h.calls[0] == "ray");
    // Each spot rolls strength, size, y, x (the original's arguments right to left).
    h.rolls = {2048 + 4095, 64 + 5, 0, 1208};  // then zeros: the second spot has no strength
    f.emit(h);
    SH_CHECK(h.next == 4 && f.emitters()[0].size == 30);
    // The sweep starts left of the screen (x = 53 - 302): nothing is lit yet.
    SH_CHECK(std::none_of(f.cells().begin(), f.cells().end(), [](std::uint8_t c) { return c != 0; }));
    for (int i = 0; i < 10; i++) {
        h.rolls = {2048 + 4095, 64 + 5, 0, 1208};
        h.next = 0;
        f.emit(h);
    }
    SH_CHECK(std::any_of(f.cells().begin(), f.cells().end(), [](std::uint8_t c) { return c != 0; }));
}

SH_TEST(glow_emitters_parse_strictly) {
    const char* base = R"({"fields": {"F": {"cols": 2, "rows": 2, "cell": 1, "origin": [0, 0], "extent": [2, 2],
        "kernel_radius": 1, "decay_step": 1, "decay_jitter_mask": 0, "decay_jitter_shift": 0, "fold_peak": 0,
        "slot": 0, "prim_code": 58, "mode_tpage": 0, "mode_dtd": 1, "shade": SHADE, "emitters": {
        "capacity": 2, "depth_scale": 4,
        "point": {"offset": [0, 0, 0], "size": 1, "grow": 1, "size_max": 1, "strength": 1},
        "bones": {"spots": [[0, 1, 1, 1]], "offset_scale": 1, "base_root": 1, "base_bone": 1, "grow": 1, "shrink": 1,
                  "strength_step": 1, "fade_after": 1},
        "ray": {"sweep": 1, "spots": 1, "width": 1, "x_divisor": 1, "height": 1, "y_divisor": 1, "y_offset": 0,
                "size_range": 1, "size_min": 0, "strength_range": 1},
        "flash": {"size": 1, "strength": 1, "start_after": 1, "sound": 1, "rumble": 8, "x": 0, "y": 0,
                  "spot_size": 1, "spot_speed": 1, "spot_speed_add": 1, "spot_strength": 1}}}}})";
    std::string shade = "[";
    for (int i = 0; i < 256; i++) {
        shade += i == 0 ? "[0, 0, 0]" : ", [0, 0, 0]";
    }
    shade += "]";
    std::string s = base;
    s.replace(s.find("SHADE"), 5, shade);
    auto ok = parse_glow_fields(s);
    SH_CHECK(static_cast<bool>(ok));
    if (ok) {
        SH_CHECK(ok.value().at("F").emitters.capacity == 2);
    }
    std::string bad = s;
    bad.replace(bad.find(R"("rumble": 8)"), 11, R"("rumbles": 8)");
    SH_CHECK(!parse_glow_fields(bad));
    bad = s;
    bad.replace(bad.find(R"("width": 1)"), 10, R"("width": 0)");
    SH_CHECK(!parse_glow_fields(bad));
}
