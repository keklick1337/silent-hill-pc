// SPDX-License-Identifier: GPL-3.0-or-later
// Glow fields, written from the original's behaviour. Products are 32-bit as the original's
// (wrapped, then shifted); the spot's kernel walk is Q12 with truncating divisions, so its first
// sample row and column can start before the kernel (read as its edge) and step by
// cell * radius / size kernel texels per cell.

#include "engine/effects/glow_field.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

#include "engine/effects/effects_file.h"
#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::effects {
namespace {

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error(what);
}

std::int32_t mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(a) * b)) >> 12;
}

std::int32_t isqrt(std::int64_t v) {
    if (v <= 0) {
        return 0;
    }
    auto r = static_cast<std::int64_t>(std::sqrt(static_cast<double>(v)));
    while (r * r > v) {
        r--;
    }
    while ((r + 1) * (r + 1) <= v) {
        r++;
    }
    return static_cast<std::int32_t>(r);
}

// The square root the kernel was built with (the console's fast one): the value is cut to its
// top 8 bits at an even shift, that mantissa's root is taken in Q12, and shifted back. It reads
// low for some inputs (sqrt(289) gives 16).
std::int32_t coarse_sqrt(std::int32_t a) {
    if (a <= 0) {
        return 0;
    }
    int zeros = 0;
    while (zeros < 32 && (static_cast<std::uint32_t>(a) & (0x80000000u >> zeros)) == 0) {
        zeros++;
    }
    zeros &= ~1;
    const std::int32_t mantissa = zeros < 24 ? a >> (24 - zeros) : a << (zeros - 24);
    const std::int32_t root = isqrt(static_cast<std::int64_t>(mantissa) << 18);
    return (root << ((31 - zeros) >> 1)) >> 12;
}

}  // namespace

GlowField::GlowField(GlowFieldParams params) : params_(std::move(params)) {
    cells_.assign(static_cast<std::size_t>(params_.cols) * params_.rows, 0);
    emitters_.resize(static_cast<std::size_t>(params_.emitters.capacity));
}

// ---------------------------------------------------------------------------------------------
// Emitters

namespace {

std::int32_t mul64(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

}  // namespace

GlowEmitter* GlowField::acquire() {
    for (GlowEmitter& e : emitters_) {
        if (!e.active) {
            // Only these are set: an add sets what its kind reads, the rest is the slot's leftover.
            e.active = true;
            e.scale = 4096;
            return &e;
        }
    }
    return nullptr;
}

void GlowField::add_point(GlowAnchor at, GlowAnchor turn) {
    GlowEmitter* e = acquire();
    if (e == nullptr) {
        return;
    }
    const GlowEmitterParams::Point& pt = params_.emitters.point;
    e->at = at;
    e->turn = turn;
    std::copy(std::begin(pt.offset), std::end(pt.offset), e->offset);
    e->size = pt.size;
    e->size_limit = pt.size_max;
    e->strength = pt.strength;
    e->kind = GlowKind::Point;
}

void GlowField::add_bones(GlowFieldHost& host, GlowAnchor at, GlowAnchor skeleton, GlowAnchor timer) {
    const GlowEmitterParams::Bones& b = params_.emitters.bones;
    for (const GlowEmitterParams::BoneSpot& spot : b.spots) {
        GlowEmitter* e = acquire();
        if (e == nullptr) {
            continue;
        }
        e->at = at;
        e->skeleton = skeleton;
        if (spot.bone != 0) {
            std::int32_t t[3];
            host.bone_translation(skeleton, spot.bone, t);
            for (int k = 0; k < 3; k++) {
                e->offset[k] = static_cast<std::int16_t>(mul64(t[k], b.offset_scale));
            }
            e->base = b.base_bone;
        } else {
            e->offset[0] = e->offset[1] = e->offset[2] = 0;
            e->base = b.base_root;
        }
        e->size_limit = spot.factor;
        e->size = spot.factor;
        e->strength = spot.strength;
        e->strength_max = spot.strength_max;
        e->kind = GlowKind::Bones;
            reset();  // the grid's slot back to its own
        e->scale = 0;
        e->timer = timer;
        e->phase = 0;
    }
}

void GlowField::add_ray(GlowFieldHost& host, GlowAnchor at, GlowAnchor target) {
    GlowEmitter* e = acquire();
    if (e == nullptr) {
        return;
    }
    e->kind = GlowKind::Ray;
    e->at = at;
    e->target = target;
    e->size = 0;
    host.view_ray(at, target);
}

void GlowField::add_flash(GlowAnchor timer) {
    GlowEmitter* e = acquire();
    reset();
    if (e == nullptr) {
        return;
    }
    e->size = params_.emitters.flash.size;
    e->strength = params_.emitters.flash.strength;
    e->kind = GlowKind::Flash;
    e->phase = 0;
    e->timer = timer;
}

void GlowField::remove(GlowKind kind) {
    for (GlowEmitter& e : emitters_) {
        if (e.active && e.kind == kind) {
            e.active = false;
        }
    }
}

void GlowField::clear_emitters() {
    for (GlowEmitter& e : emitters_) {
        e.active = false;
    }
}

bool GlowField::update(GlowFieldHost& host, GlowEmitter& e) {
    const GlowEmitterParams& p = params_.emitters;
    switch (e.kind) {
        case GlowKind::Point:
            host.view_point(e.at, e.turn);
            e.size = std::min(e.size_limit, e.size + p.point.grow);
            return true;
        case GlowKind::Bones:
            // The view takes the swell before this frame's change.
            host.view_skeleton(e.skeleton, e.scale);
            if (e.phase == 0) {
                e.scale += p.bones.grow;
                e.size = e.base + mul64(e.scale, e.size_limit);
                e.strength = std::min(e.strength_max, e.strength + p.bones.strength_step);
                if (host.timer(e.timer) > p.bones.fade_after) {
                    e.phase++;
                }
            } else if (e.phase == 1) {
                e.scale = std::max(e.scale - p.bones.shrink, 0);
                e.size = e.base + mul64(e.scale, e.size_limit);
                e.strength = std::max(e.strength - p.bones.strength_step, 0);
            }
            return true;
        case GlowKind::Ray: {
            host.view_ray(e.at, e.target);
            e.size += p.ray.sweep;
            const GlowEmitterParams::Ray& r = p.ray;
            for (int i = 0; i < r.spots; i++) {
                // The original's arguments are evaluated right to left: strength, size, y, x.
                const std::int32_t strength = host.rand16() % r.strength_range;
                const std::int32_t size = host.rand16() % r.size_range + r.size_min;
                const std::int32_t y = (host.rand16() % r.height) / r.y_divisor + r.y_offset;
                const std::int32_t x = (host.rand16() % r.width) / r.x_divisor + (e.size - r.width);
                splat(x, y, size, strength);
            }
            return false;
        }
        case GlowKind::Flash: {
            const GlowEmitterParams::Flash& f = p.flash;
            if (e.phase == 0) {
                if (host.timer(e.timer) > f.start_after) {
                    host.flash_cue(f.sound, f.rumble);
                    flash_size_ = f.spot_size;
                    flash_speed_ = f.spot_speed;
                    e.phase++;
                }
            } else if (e.phase == 1) {
                splat(f.x, f.y, flash_size_, f.spot_strength);
                flash_size_ += flash_speed_ >> 12;
                flash_speed_ += f.spot_speed_add;
            }
            return false;
        }
    }
    return false;
}

void GlowField::emit(GlowFieldHost& host) {
    clear();
    for (GlowEmitter& e : emitters_) {
        if (!e.active || !update(host, e)) {
            continue;
        }
        const GlowFieldHost::Projected at = host.project_view(e.offset);
        const std::int32_t near = 4096 - at.depth * params_.emitters.depth_scale;
        splat(at.x, at.y, mul64(e.size >> 8, near) >> 4, e.strength);
    }
}

void GlowField::reset() {
    const int r = params_.kernel_radius;
    if (kernel_.empty()) {
        const int side = 2 * r + 1;
        kernel_.resize(static_cast<std::size_t>(side) * side);
        std::size_t k = 0;
        for (int i = -r; i <= r; i++) {
            for (int j = -r; j <= r; j++, k++) {
                const std::int32_t falloff = r - coarse_sqrt(i * i + j * j);
                kernel_[k] = falloff > 0 ? static_cast<std::uint8_t>(std::min(255, (falloff << 8) / r)) : 0;
            }
        }
    }
    slot_ = params_.slot;
}

void GlowField::clear() {
    std::fill(cells_.begin(), cells_.end(), 0);
}

void GlowField::splat(std::int32_t x, std::int32_t y, std::int32_t size, std::int32_t strength) {
    if (kernel_.empty()) {
        reset();
    }
    const std::int32_t r = params_.kernel_radius;
    const std::int32_t cell = params_.cell;
    x -= params_.origin_x;
    y -= params_.origin_y;
    if (size == 0) {
        return;
    }
    size = mul(size, strength);
    // The spot shrank to nothing: no divisions by zero (the engine's guard; the original divided).
    if (size == 0) {
        return;
    }

    std::int32_t top = y - size;
    if (top > params_.extent_y) {
        return;
    }
    std::int32_t v0;
    if (top < 0) {
        v0 = ((-top) * r / size) * 4096;
        top = 0;
    } else {
        v0 = (((-top) % cell) * r / size) * 4096;
    }
    const std::int32_t row_first = top / cell;
    const std::int32_t bottom = y + size;
    if (bottom < 0) {
        return;
    }
    const std::int32_t row_last = std::min(bottom, params_.extent_y) / cell;

    std::int32_t left = x - size;
    if (left > params_.extent_x) {
        return;
    }
    std::int32_t u0;
    if (left < 0) {
        u0 = ((-left) * r / size) * 4096;
        left = 0;
    } else {
        u0 = ((left / cell * cell - left) * r / size) * 4096;
    }
    const std::int32_t col_first = left / cell;
    const std::int32_t right = x + size;
    if (right < 0) {
        return;
    }
    const std::int32_t col_last = std::min(right, params_.extent_x) / cell;
    const std::int32_t step = (cell * r * 4096) / size;
    const std::int32_t side = 2 * r + 1;
    const auto kernel_at = [&](std::int32_t ky, std::int32_t kx) -> std::int32_t {
        const std::int64_t at = static_cast<std::int64_t>(ky) * side + kx;
        return at < static_cast<std::int64_t>(kernel_.size()) ? kernel_[static_cast<std::size_t>(at)] : 0;
    };

    std::int32_t v = v0;
    for (std::int32_t i = row_first; i <= row_last; i++) {
        const std::int32_t ky = std::max(v >> 12, 0);
        v += step;
        std::int32_t u = u0;
        for (std::int32_t j = col_first; j <= col_last; j++) {
            const std::int64_t idx = static_cast<std::int64_t>(i) * params_.cols + j;
            const std::int32_t kx = std::max(u >> 12, 0);
            const std::int32_t add = mul(kernel_at(ky, kx), strength);
            u += step;
            if (idx >= 0 && idx < static_cast<std::int64_t>(cells_.size())) {
                std::uint8_t& c = cells_[static_cast<std::size_t>(idx)];
                c = static_cast<std::uint8_t>(std::min<std::int32_t>(c + add, 255));
            }
        }
    }
}

void GlowField::decay(GlowFieldHost& host) {
    for (std::uint8_t& c : cells_) {
        if (c == 0) {
            continue;
        }
        const std::int32_t roll = host.rand16();
        std::int32_t v = c - params_.decay_step + ((roll & params_.decay_jitter_mask) >> params_.decay_jitter_shift);
        c = static_cast<std::uint8_t>(std::clamp(v, 0, 255));
    }
}

std::uint32_t GlowField::colour_at(int index, bool fold) const {
    if (index < 0 || index >= static_cast<int>(cells_.size())) {
        return static_cast<std::uint32_t>(params_.prim_code) << 24;
    }
    std::int32_t v = cells_[static_cast<std::size_t>(index)];
    if (fold) {
        v = std::max(v > params_.fold_peak ? 2 * params_.fold_peak - v : v, 0);
    }
    return params_.shade[static_cast<std::size_t>(v)] | (static_cast<std::uint32_t>(params_.prim_code) << 24);
}

void GlowField::draw(GlowFieldHost& host) const {
    const int cols = params_.cols;
    const std::int32_t cell = params_.cell;
    for (int i = 1; i < params_.rows; i++) {
        std::uint32_t top = colour_at(cols * (i - 1), false);
        std::uint32_t bottom = colour_at(cols * i, false);
        for (int j = 1; j < cols; j++) {
            GlowQuad q;
            q.color[0] = top;
            q.color[2] = bottom;
            top = colour_at(j + cols * (i - 1), false);
            bottom = colour_at(j + cols * i, false);
            q.color[1] = top;
            q.color[3] = bottom;
            const auto x0 = static_cast<std::int16_t>(params_.origin_x + cell * (j - 1));
            const auto y0 = static_cast<std::int16_t>(params_.origin_y + cell * (i - 1));
            const auto x1 = static_cast<std::int16_t>(x0 + cell);
            const auto y1 = static_cast<std::int16_t>(y0 + cell);
            const std::int16_t xs[4] = {x0, x1, x0, x1};
            const std::int16_t ys[4] = {y0, y0, y1, y1};
            std::copy(xs, xs + 4, q.x);
            std::copy(ys, ys + 4, q.y);
            q.lit = false;
            for (std::uint32_t c : q.color) {
                q.lit = q.lit || (c & 0xFFFFFFu) != 0;
            }
            host.emit_quad(q, slot_);
        }
    }
    host.emit_page(slot_, params_.mode_tpage, params_.mode_dtd);
}

bool GlowField::draw_mesh(GlowFieldHost& host, const std::string& name) const {
    const auto found = params_.meshes.find(name);
    if (found == params_.meshes.end()) {
        return false;
    }
    const GlowMesh& m = found->second;
    const int cols = params_.cols;
    const auto vertex = [&](int i, int j, std::int16_t (&v)[3]) {
        v[0] = v[1] = v[2] = 0;
        v[m.col_axis] = static_cast<std::int16_t>(j * m.col_step - m.col_offset);
        v[m.row_axis] = static_cast<std::int16_t>(i * m.row_step - m.row_offset);
    };
    host.begin_mesh(m);
    std::int32_t slot = slot_;
    for (int i = 1; i < params_.rows; i++) {
        if (i >= m.far_from_row) {
            slot = slot_ + m.far_slot_add;
        }
        std::uint32_t top = colour_at(cols * (i - 1), m.fold);
        std::uint32_t bottom = colour_at(cols * i, m.fold);
        for (int j = 1; j < cols; j++) {
            std::int16_t v[4][3];
            vertex(i, j, v[0]);
            vertex(i, j + 1, v[1]);
            vertex(i + 1, j, v[2]);
            vertex(i + 1, j + 1, v[3]);
            GlowQuad q;
            q.projected = true;
            host.project4(v, q.x, q.y);
            q.color[0] = top;
            q.color[2] = bottom;
            top = colour_at(j + cols * (i - 1), m.fold);
            bottom = colour_at(j + cols * i, m.fold);
            q.color[1] = top;
            q.color[3] = bottom;
            for (std::uint32_t c : q.color) {
                q.lit = q.lit || (c & 0xFFFFFFu) != 0;
            }
            host.emit_quad(q, slot);
        }
    }
    host.emit_page(slot_, m.tpage, m.dtd);
    host.emit_page(slot, m.tpage, m.dtd);
    return true;
}

namespace {

GlowMesh parse_mesh(const std::string& where, const json::Value& v) {
    if (!v.is_object()) {
        fail(where + " must be an object");
    }
    static const char* const kKeys[] = {"col_axis", "row_axis", "col", "row", "fold", "far_from_row",
                                        "far_slot_add", "tpage", "dtd", "transform"};
    for (const auto& [key, value] : v.members()) {
        if (key.rfind("_", 0) == 0 || key == "description") {
            continue;
        }
        if (std::find(std::begin(kKeys), std::end(kKeys), key) == std::end(kKeys)) {
            fail("unknown key \"" + key + "\" in " + where);
        }
    }
    const auto axis_of = [&](const char* key) -> int {
        const json::Value* e = v.find(key);
        if (e == nullptr || !e->is_string() || e->string().size() != 1 || e->string()[0] < 'x' ||
            e->string()[0] > 'z') {
            fail(where + "." + key + " must be \"x\", \"y\" or \"z\"");
        }
        return e->string()[0] - 'x';
    };
    const auto int_of = [&](const char* key, std::int64_t lo, std::int64_t hi) -> std::int32_t {
        const json::Value* e = v.find(key);
        if (e == nullptr || !e->is_integer() || e->integer() < lo || e->integer() > hi) {
            fail(where + "." + key + " must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
        }
        return static_cast<std::int32_t>(e->integer());
    };
    const auto pair_of = [&](const char* key, std::int32_t* step, std::int32_t* offset) {
        const json::Value* e = v.find(key);
        if (e == nullptr || !e->is_object() || e->find("step") == nullptr || e->find("offset") == nullptr ||
            e->members().size() != 2 || !e->find("step")->is_integer() || !e->find("offset")->is_integer() ||
            std::abs(e->find("step")->integer()) > 32767 || std::abs(e->find("offset")->integer()) > 32767) {
            fail(where + "." + key + " must be {\"step\": n, \"offset\": n} (-32767..32767)");
        }
        *step = static_cast<std::int32_t>(e->find("step")->integer());
        *offset = static_cast<std::int32_t>(e->find("offset")->integer());
    };
    const auto triple_of = [&](const json::Value& e, const char* key, std::int16_t (&out)[3]) {
        const json::Value* t = e.find(key);
        if (t == nullptr || !t->is_array() || t->array().size() != 3) {
            fail(where + ".transform." + key + " must be [x, y, z] (-32768..32767)");
        }
        for (int k = 0; k < 3; k++) {
            const json::Value& c = t->array()[static_cast<std::size_t>(k)];
            if (!c.is_integer() || c.integer() < -32768 || c.integer() > 32767) {
                fail(where + ".transform." + key + " must be [x, y, z] (-32768..32767)");
            }
            out[k] = static_cast<std::int16_t>(c.integer());
        }
    };

    GlowMesh m;
    m.col_axis = axis_of("col_axis");
    m.row_axis = axis_of("row_axis");
    if (m.col_axis == m.row_axis) {
        fail(where + ": col_axis and row_axis must differ");
    }
    pair_of("col", &m.col_step, &m.col_offset);
    pair_of("row", &m.row_step, &m.row_offset);
    const json::Value* fold = v.find("fold");
    if (fold == nullptr || !fold->is_bool()) {
        fail(where + ".fold must be true or false");
    }
    m.fold = fold->boolean();
    m.far_from_row = int_of("far_from_row", 0, 256);
    m.far_slot_add = int_of("far_slot_add", -4095, 4095);
    m.tpage = int_of("tpage", 0, 0xFFFF);
    m.dtd = int_of("dtd", 0, 1);
    if (const json::Value* t = v.find("transform"); t != nullptr && !t->is_null()) {
        if (!t->is_object() || t->members().size() != 2) {
            fail(where + ".transform must be null or {\"position\": [x, y, z], \"rotation\": [x, y, z]}");
        }
        triple_of(*t, "position", m.position);
        triple_of(*t, "rotation", m.rotation);
        m.has_transform = true;
    }
    return m;
}

// "emitters": every section and key is required (the numbers are the original's), unknown keys
// are refused.
GlowEmitterParams parse_emitters(const std::string& where, const json::Value& v) {
    const auto object = [&](const json::Value& o, const std::string& at,
                            std::initializer_list<const char*> keys) {
        if (!o.is_object()) {
            fail(at + " must be an object");
        }
        for (const auto& [key, value] : o.members()) {
            if (key.rfind("_", 0) == 0 || key == "description") {
                continue;
            }
            if (std::find_if(keys.begin(), keys.end(), [&](const char* k) { return key == k; }) == keys.end()) {
                fail("unknown key \"" + key + "\" in " + at);
            }
        }
    };
    const auto int_of = [&](const json::Value& o, const std::string& at, const char* key, std::int64_t lo,
                            std::int64_t hi) -> std::int32_t {
        const json::Value* e = o.find(key);
        if (e == nullptr || !e->is_integer() || e->integer() < lo || e->integer() > hi) {
            fail(at + "." + key + " must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
        }
        return static_cast<std::int32_t>(e->integer());
    };
    const auto section = [&](const char* key) -> const json::Value& {
        const json::Value* e = v.find(key);
        if (e == nullptr) {
            fail("missing key " + where + "." + key);
        }
        return *e;
    };
    constexpr std::int64_t kBig = std::int64_t{1} << 30;

    object(v, where, {"capacity", "depth_scale", "point", "bones", "ray", "flash"});
    GlowEmitterParams p;
    p.capacity = int_of(v, where, "capacity", 0, 4096);
    p.depth_scale = int_of(v, where, "depth_scale", -4096, 4096);

    const std::string pw = where + ".point";
    const json::Value& pt = section("point");
    object(pt, pw, {"offset", "size", "grow", "size_max", "strength"});
    const json::Value* off = pt.find("offset");
    if (off == nullptr || !off->is_array() || off->array().size() != 3) {
        fail(pw + ".offset must be [x, y, z] (-32768..32767)");
    }
    for (std::size_t k = 0; k < 3; k++) {
        const json::Value& c = off->array()[k];
        if (!c.is_integer() || c.integer() < -32768 || c.integer() > 32767) {
            fail(pw + ".offset must be [x, y, z] (-32768..32767)");
        }
        p.point.offset[k] = static_cast<std::int16_t>(c.integer());
    }
    p.point.size = int_of(pt, pw, "size", -kBig, kBig);
    p.point.grow = int_of(pt, pw, "grow", -kBig, kBig);
    p.point.size_max = int_of(pt, pw, "size_max", -kBig, kBig);
    p.point.strength = int_of(pt, pw, "strength", -kBig, kBig);

    const std::string bw = where + ".bones";
    const json::Value& b = section("bones");
    object(b, bw, {"spots", "offset_scale", "base_root", "base_bone", "grow", "shrink", "strength_step", "fade_after"});
    const json::Value* spots = b.find("spots");
    if (spots == nullptr || !spots->is_array()) {
        fail(bw + ".spots must be an array of [bone, factor, strength, strength_max]");
    }
    for (const json::Value& e : spots->array()) {
        if (!e.is_array() || e.array().size() != 4) {
            fail(bw + ".spots entries are [bone, factor, strength, strength_max]");
        }
        std::int64_t n[4];
        for (std::size_t k = 0; k < 4; k++) {
            if (!e.array()[k].is_integer() || std::abs(e.array()[k].integer()) > kBig) {
                fail(bw + ".spots entries are [bone, factor, strength, strength_max] integers");
            }
            n[k] = e.array()[k].integer();
        }
        if (n[0] < 0 || n[0] > 255) {
            fail(bw + ".spots: a bone is 0..255");
        }
        p.bones.spots.push_back(GlowEmitterParams::BoneSpot{static_cast<int>(n[0]), static_cast<std::int32_t>(n[1]),
                                                            static_cast<std::int32_t>(n[2]),
                                                            static_cast<std::int32_t>(n[3])});
    }
    p.bones.offset_scale = int_of(b, bw, "offset_scale", -kBig, kBig);
    p.bones.base_root = int_of(b, bw, "base_root", -kBig, kBig);
    p.bones.base_bone = int_of(b, bw, "base_bone", -kBig, kBig);
    p.bones.grow = int_of(b, bw, "grow", -kBig, kBig);
    p.bones.shrink = int_of(b, bw, "shrink", -kBig, kBig);
    p.bones.strength_step = int_of(b, bw, "strength_step", -kBig, kBig);
    p.bones.fade_after = int_of(b, bw, "fade_after", -kBig, kBig);

    const std::string rw = where + ".ray";
    const json::Value& r = section("ray");
    object(r, rw, {"sweep", "spots", "width", "x_divisor", "height", "y_divisor", "y_offset", "size_range",
                   "size_min", "strength_range"});
    p.ray.sweep = int_of(r, rw, "sweep", -kBig, kBig);
    p.ray.spots = int_of(r, rw, "spots", 0, 4096);
    p.ray.width = int_of(r, rw, "width", 1, 32767);
    p.ray.x_divisor = int_of(r, rw, "x_divisor", 1, 32767);
    p.ray.height = int_of(r, rw, "height", 1, 32767);
    p.ray.y_divisor = int_of(r, rw, "y_divisor", 1, 32767);
    p.ray.y_offset = int_of(r, rw, "y_offset", -32768, 32767);
    p.ray.size_range = int_of(r, rw, "size_range", 1, 32767);
    p.ray.size_min = int_of(r, rw, "size_min", -32768, 32767);
    p.ray.strength_range = int_of(r, rw, "strength_range", 1, 32767);

    const std::string fw = where + ".flash";
    const json::Value& f = section("flash");
    object(f, fw, {"size", "strength", "start_after", "sound", "rumble", "x", "y", "spot_size", "spot_speed",
                   "spot_speed_add", "spot_strength"});
    p.flash.size = int_of(f, fw, "size", -kBig, kBig);
    p.flash.strength = int_of(f, fw, "strength", -kBig, kBig);
    p.flash.start_after = int_of(f, fw, "start_after", -kBig, kBig);
    p.flash.sound = parse::sound_of(f, "sound");
    p.flash.rumble = int_of(f, fw, "rumble", -1, 255);
    p.flash.x = int_of(f, fw, "x", -1024, 1024);
    p.flash.y = int_of(f, fw, "y", -1024, 1024);
    p.flash.spot_size = int_of(f, fw, "spot_size", -kBig, kBig);
    p.flash.spot_speed = int_of(f, fw, "spot_speed", -kBig, kBig);
    p.flash.spot_speed_add = int_of(f, fw, "spot_speed_add", -kBig, kBig);
    p.flash.spot_strength = int_of(f, fw, "spot_strength", -kBig, kBig);
    return p;
}

GlowFieldParams parse_one(const std::string& name, const json::Value& v) {
    if (!v.is_object()) {
        fail("\"" + name + "\" must be an object");
    }
    const auto where = [&](const char* key) { return "\"" + name + "." + key + "\""; };
    static const char* const kKeys[] = {"cols", "rows", "cell", "origin", "extent", "kernel_radius", "decay_step",
                                        "decay_jitter_mask", "decay_jitter_shift", "fold_peak", "slot",
                                        "prim_code", "mode_tpage", "mode_dtd", "shade", "meshes", "emitters"};
    for (const auto& [key, value] : v.members()) {
        if (key.rfind("_", 0) == 0 || key == "description") {
            continue;
        }
        if (std::find(std::begin(kKeys), std::end(kKeys), key) == std::end(kKeys)) {
            fail("unknown key " + where(key.c_str()));
        }
    }
    const auto int_of = [&](const char* key, std::int64_t lo, std::int64_t hi) -> std::int32_t {
        const json::Value* e = v.find(key);
        if (e == nullptr) {
            fail("missing key " + where(key));
        }
        if (!e->is_integer() || e->integer() < lo || e->integer() > hi) {
            fail(where(key) + " must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
        }
        return static_cast<std::int32_t>(e->integer());
    };
    const auto pair_of = [&](const char* key, std::int32_t* a, std::int32_t* b) {
        const json::Value* e = v.find(key);
        if (e == nullptr || !e->is_array() || e->array().size() != 2 || !e->array()[0].is_integer() ||
            !e->array()[1].is_integer() || std::abs(e->array()[0].integer()) > 1024 ||
            std::abs(e->array()[1].integer()) > 1024) {
            fail(where(key) + " must be [x, y] pixels (-1024..1024)");
        }
        *a = static_cast<std::int32_t>(e->array()[0].integer());
        *b = static_cast<std::int32_t>(e->array()[1].integer());
    };

    GlowFieldParams p;
    p.cols = int_of("cols", 2, 256);
    p.rows = int_of("rows", 2, 256);
    p.cell = int_of("cell", 1, 256);
    pair_of("origin", &p.origin_x, &p.origin_y);
    pair_of("extent", &p.extent_x, &p.extent_y);
    if (p.extent_x < 0 || p.extent_y < 0) {
        fail(where("extent") + " must not be negative");
    }
    p.kernel_radius = int_of("kernel_radius", 1, 255);
    p.decay_step = int_of("decay_step", -255, 255);
    p.decay_jitter_mask = int_of("decay_jitter_mask", 0, 0x7FFF);
    p.decay_jitter_shift = int_of("decay_jitter_shift", 0, 15);
    p.fold_peak = int_of("fold_peak", 0, 255);
    p.slot = int_of("slot", 0, 4095);
    p.prim_code = int_of("prim_code", 0, 255);
    p.mode_tpage = int_of("mode_tpage", 0, 0xFFFF);
    p.mode_dtd = int_of("mode_dtd", 0, 1);

    const json::Value* shade = v.find("shade");
    if (shade == nullptr || !shade->is_array() || shade->array().size() != 256) {
        fail(where("shade") + " must be an array of 256 colours [r, g, b]");
    }
    for (std::size_t i = 0; i < 256; i++) {
        const json::Value& e = shade->array()[i];
        if (!e.is_array() || e.array().size() != 3) {
            fail(where("shade") + " entries must be [r, g, b]");
        }
        std::uint32_t word = 0;
        for (std::size_t c = 0; c < 3; c++) {
            const json::Value& ch = e.array()[c];
            if (!ch.is_integer() || ch.integer() < 0 || ch.integer() > 255) {
                fail(where("shade") + " channels must be 0..255");
            }
            word |= static_cast<std::uint32_t>(ch.integer()) << (8 * c);
        }
        p.shade[i] = word;
    }

    if (const json::Value* meshes = v.find("meshes"); meshes != nullptr) {
        if (!meshes->is_object()) {
            fail(where("meshes") + " must be an object of named meshes");
        }
        for (const auto& [mesh, value] : meshes->members()) {
            if (mesh.rfind("_", 0) == 0) {
                continue;
            }
            p.meshes.emplace(mesh, parse_mesh("\"" + name + ".meshes." + mesh + "\"", value));
        }
    }
    if (const json::Value* emitters = v.find("emitters"); emitters != nullptr) {
        p.emitters = parse_emitters("\"" + name + ".emitters\"", *emitters);
    }
    return p;
}

}  // namespace

Result<std::map<std::string, GlowFieldParams>> parse_glow_fields(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<std::map<std::string, GlowFieldParams>>::fail(doc.error());
    }
    const json::Value& root = doc.value();
    if (!root.is_object()) {
        fail("the file must be an object");
    }
    for (const auto& [key, value] : root.members()) {
        if (key.rfind("_", 0) != 0 && key != "description" && key != "fields") {
            fail("unknown key \"" + key + "\"");
        }
    }
    const json::Value* fields = root.find("fields");
    if (fields == nullptr || !fields->is_object()) {
        fail("\"fields\" must be an object of named fields");
    }
    std::map<std::string, GlowFieldParams> out;
    for (const auto& [name, value] : fields->members()) {
        if (name.rfind("_", 0) == 0) {
            continue;
        }
        out.emplace(name, parse_one(name, value));
    }
    return Result<std::map<std::string, GlowFieldParams>>::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<std::map<std::string, GlowFieldParams>>::fail(e.what());
}

Result<std::map<std::string, GlowFieldParams>> load_glow_fields(const std::filesystem::path& gamedata_dir,
                                                               const std::filesystem::path& mods_dir,
                                                               std::string* source) {
    namespace fs = std::filesystem;
    using R = Result<std::map<std::string, GlowFieldParams>>;
    const fs::path relative = fs::path("effects") / "glow_fields.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return R::fail(read.error());
    }
    const std::string& text = read.value();
    R parsed = parse_glow_fields(text);
    if (!parsed) {
        return R::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
