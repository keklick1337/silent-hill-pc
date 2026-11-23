// SPDX-License-Identifier: GPL-3.0-or-later
// The drain splash, written from the original's behaviour. Widths follow the original: the
// projected centre is kept as the raw words, the stamp's column and row are 16-bit, products are
// 32-bit (the throw uses 64-bit ones), positions are narrowed to 16 bits for the projection.

#include "engine/effects/drain_splash.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::effects {
namespace {

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error(what);
}

std::int32_t mul(std::int32_t a, std::int32_t b) {
    return (a * b) >> 12;
}
std::int32_t mul_wide(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

// Rounds away from zero before halving, as the original did for the stamp's cell.
std::int16_t halve_out(std::int16_t v) {
    v = static_cast<std::int16_t>(v + (v > 0 ? 1 : -1));
    return static_cast<std::int16_t>(v / 2);
}

}  // namespace

void DrainSplash::start(DrainSplashHost& host, const Vec3& position) {
    state_.yaw = 0;
    state_.delay = params_.start_delay;
    state_.origin_q8 = Vec3{position.x >> 4, position.y >> 4, position.z >> 4};
    state_.field.fill(0);

    for (SplashParticle& p : state_.particles) {
        p.delay = host.rand16() % (params_.spawn_delay_max + 1);
        p.pos = Vec3{};
        p.brightness = params_.start_brightness;

        const std::int32_t angle = host.rand16();
        const std::int32_t s = host.sin(angle);
        const std::int32_t up = s < 0 ? s : -s;
        const std::int32_t c = host.cos(angle);
        const std::int32_t yaw = host.rand16();

        const std::int32_t side_x = mul(host.sin(yaw), c);
        const std::int32_t side_z = mul(host.cos(yaw), c);
        p.vel[0] = static_cast<std::int16_t>(mul_wide(side_x, params_.speed_side));
        p.vel[1] = static_cast<std::int16_t>(mul_wide(up, params_.speed_up));
        p.vel[2] = static_cast<std::int16_t>(mul_wide(side_z, params_.speed_side));
    }
}

void DrainSplash::stamp(int x, int y, std::int32_t brightness) {
    const std::int32_t off_x = state_.centre_x & 0xFFFF;
    const std::int32_t off_y = state_.centre_y;
    const std::int16_t col = halve_out(static_cast<std::int16_t>((x + kSplashCols) - off_x));
    const std::int16_t row = halve_out(static_cast<std::int16_t>((y + kSplashRows) - off_y));
    if (col > 0 && col < kSplashCols && row > 0 && row < kSplashRows) {
        state_.row(0)[col + kSplashCols * row] = static_cast<std::uint8_t>(brightness);
    }
}

void DrainSplash::fly(DrainSplashHost& host) {
    for (SplashParticle& p : state_.particles) {
        if (p.delay < 0) {
            const Vec3s at{static_cast<std::int16_t>(p.pos.x >> 8), static_cast<std::int16_t>(p.pos.y >> 8),
                           static_cast<std::int16_t>(p.pos.z >> 8)};
            const ScreenPoint s = host.project(at);
            stamp(s.x, s.y, p.brightness);

            p.pos.x += p.vel[0];
            p.pos.y = (p.pos.y + p.vel[1]) + params_.gravity;
            p.pos.z += p.vel[2];
            p.brightness = std::max(p.brightness - params_.brightness_decay, 0);
        } else {
            p.delay -= host.delta_time();
        }
    }
}

void DrainSplash::smooth(DrainSplashHost& host) {
    for (int i = 0; i < kSplashRows; i++) {
        std::uint8_t* tab = state_.row(i);
        for (int j = 0; j < kSplashCols; j++) {
            std::int32_t val = tab[j - kSplashCols];
            if (j == 0) {
                val = 0;
            } else {
                val += tab[j - 1];
            }
            if (j == kSplashCols - 1) {
                val = 0;
            } else {
                val += tab[j + 1];
            }
            val += tab[j + kSplashCols];
            val >>= 2;
            val -= params_.smooth_loss;
            tab[j] = val <= 0 ? 0 : static_cast<std::uint8_t>(val);

            if (static_cast<std::uint32_t>(host.rand16()) % static_cast<std::uint32_t>(params_.clear_range + 1) == 0) {
                tab[j] = 0;
            }
        }
    }
}

std::uint32_t DrainSplash::shade_at(int col, int row) const {
    return params_.shade[state_.row(0)[row * kSplashCols + col]];
}

void DrainSplash::draw(DrainSplashHost& host) {
    const std::int32_t x = (state_.centre_x & 0xFFFF) - kSplashCols;
    const std::int32_t y = static_cast<std::int32_t>(static_cast<std::uint32_t>(state_.centre_y) & 0xFFFF) - kSplashRows;
    const std::int32_t slot = state_.slot >> 1;

    for (int i = 1; i < kSplashRows; i++) {
        std::uint32_t c1 = shade_at(0, i - 1);
        std::uint32_t c3 = shade_at(0, i);
        for (int j = 1; j < kSplashCols; j++) {
            const std::uint32_t c0 = c1;
            const std::uint32_t c2 = c3;
            c1 = shade_at(j, i - 1);
            c3 = shade_at(j, i);
            if (params_.quiet_color == c0 && c0 == c1 && c0 == c2 && c2 == c3) {
                continue;
            }
            const std::int32_t px = x + j * 2;
            const std::int32_t py = y + i * 2;
            SplashQuad q;
            if ((j + i) & 1) {
                const std::int32_t xs[4] = {px, px + 2, px, px + 2};
                const std::int32_t ys[4] = {py, py, py + 2, py + 2};
                const std::uint32_t cs[4] = {c0, c1, c2, c3};
                std::copy(xs, xs + 4, q.x);
                std::copy(ys, ys + 4, q.y);
                std::copy(cs, cs + 4, q.color);
            } else {
                const std::int32_t xs[4] = {px + 2, px, px + 2, px};
                const std::int32_t ys[4] = {py, py, py + 2, py + 2};
                const std::uint32_t cs[4] = {c1, c0, c3, c2};
                std::copy(xs, xs + 4, q.x);
                std::copy(ys, ys + 4, q.y);
                std::copy(cs, cs + 4, q.color);
            }
            host.emit_quad(q, slot);
        }
    }
    host.emit_mode(slot, params_.mode_tpage, params_.mode_dfe, params_.mode_dtd);
}

void DrainSplash::update(DrainSplashHost& host) {
    if (state_.delay > 0) {
        state_.delay -= host.delta_time();
        return;
    }
    host.begin_view(state_.origin_q8, state_.yaw);
    const ScreenPoint c = host.project_origin();
    state_.centre_x = static_cast<std::uint16_t>(c.x);
    state_.centre_y = c.y;
    state_.slot = c.depth;

    fly(host);
    smooth(host);
    draw(host);
}

Result<DrainSplashParams> parse_drain_splash_params(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<DrainSplashParams>::fail(doc.error());
    }
    const json::Value& root = doc.value();
    if (!root.is_object()) {
        fail("the file must be an object");
    }
    static const char* const keys[] = {"start_delay", "spawn_delay_max", "start_brightness", "brightness_decay",
                                       "gravity", "speed_side", "speed_up", "smooth_loss", "clear_range",
                                       "quiet_color", "prim_code", "mode_tpage", "mode_dfe", "mode_dtd", "shade"};
    for (const auto& [key, value] : root.members()) {
        (void)value;
        if (key.rfind("_", 0) == 0 || key == "description") {
            continue;
        }
        if (std::find_if(std::begin(keys), std::end(keys), [&](const char* k) { return key == k; }) == std::end(keys)) {
            fail("unknown key \"" + key + "\"");
        }
    }
    const auto int_of = [&](const char* key, std::int64_t lo, std::int64_t hi) -> std::int64_t {
        const json::Value* v = root.find(key);
        if (v == nullptr) {
            fail(std::string("missing key \"") + key + "\"");
        }
        if (!v->is_integer() || v->integer() < lo || v->integer() > hi) {
            fail(std::string("\"") + key + "\" must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
        }
        return v->integer();
    };
    DrainSplashParams p;
    p.start_delay = static_cast<std::int32_t>(int_of("start_delay", 0, 1 << 24));
    p.spawn_delay_max = static_cast<std::int32_t>(int_of("spawn_delay_max", 0, 1 << 24));
    p.start_brightness = static_cast<std::int32_t>(int_of("start_brightness", 0, 255));
    p.brightness_decay = static_cast<std::int32_t>(int_of("brightness_decay", 0, 255));
    p.gravity = static_cast<std::int32_t>(int_of("gravity", -(1 << 20), 1 << 20));
    p.speed_side = static_cast<std::int32_t>(int_of("speed_side", -(1 << 20), 1 << 20));
    p.speed_up = static_cast<std::int32_t>(int_of("speed_up", -(1 << 20), 1 << 20));
    p.smooth_loss = static_cast<std::int32_t>(int_of("smooth_loss", 0, 255));
    p.clear_range = static_cast<std::int32_t>(int_of("clear_range", 0, 1 << 20));
    p.quiet_color = static_cast<std::uint32_t>(int_of("quiet_color", 0, 0xFFFFFFFFll));
    p.prim_code = static_cast<std::int32_t>(int_of("prim_code", 0, 255));
    p.mode_tpage = static_cast<std::int32_t>(int_of("mode_tpage", 0, 65535));
    p.mode_dfe = static_cast<std::int32_t>(int_of("mode_dfe", 0, 1));
    p.mode_dtd = static_cast<std::int32_t>(int_of("mode_dtd", 0, 1));
    const json::Value* shade = root.find("shade");
    if (shade == nullptr || !shade->is_array() || shade->array().size() != p.shade.size()) {
        fail("\"shade\" must be an array of 256 colour words");
    }
    for (std::size_t i = 0; i < p.shade.size(); i++) {
        const json::Value& e = shade->array()[i];
        if (!e.is_integer() || e.integer() < 0 || e.integer() > 0xFFFFFFFFll) {
            fail("\"shade\" entries must be unsigned 32-bit integers");
        }
        p.shade[i] = static_cast<std::uint32_t>(e.integer());
    }
    return Result<DrainSplashParams>::ok(p);
} catch (const std::exception& e) {
    return Result<DrainSplashParams>::fail(e.what());
}

Result<DrainSplashParams> load_drain_splash_params(const std::filesystem::path& gamedata_dir,
                                                   const std::filesystem::path& mods_dir, std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("effects") / "drain_splash.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<DrainSplashParams>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<DrainSplashParams> parsed = parse_drain_splash_params(text);
    if (!parsed) {
        return Result<DrainSplashParams>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
