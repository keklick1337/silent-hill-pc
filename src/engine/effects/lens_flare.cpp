// SPDX-License-Identifier: GPL-3.0-or-later
// Lens flares: fans of rays and a corona of rings around a projected point (lens_flare.h).

#include "engine/effects/lens_flare.h"

#include <stdexcept>
#include <utility>

#include "engine/effects/effects_file.h"
#include "formats/common/json.h"

namespace sh::effects {

namespace {

std::int32_t mul_q12(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

}  // namespace

LensFlare::LensFlare(LensFlareParams params) : params_(std::move(params)), phases_(params_.fans.size(), 0) {}

bool LensFlare::draw(LensFlareHost& host, int origin) {
    if (origin < 0 || origin >= static_cast<int>(params_.origins_q8.size())) {
        return false;
    }
    host.begin_view(params_.origins_q8[static_cast<std::size_t>(origin)], params_.yaw);
    const ScreenPoint p = host.project_origin();
    // A depth of -1 would divide by zero; it is behind the camera, nothing is drawn.
    if (p.depth + 1 == 0) {
        return false;
    }
    const std::int32_t size = (host.geom_screen() * params_.size_numerator) / (p.depth + 1);
    if (size > params_.max_size) {
        return false;
    }
    for (std::size_t i = 0; i < params_.fans.size(); i++) {
        const FlareFan& fan = params_.fans[i];
        draw_fan(host, fan, fan.angle + phases_[i], size, p.x, p.y);
        phases_[i] += fan.spin;
    }
    if (params_.corona.present) {
        draw_corona(host, size, p.x, p.y);
    }
    if (params_.mode) {
        host.emit_mode(params_.slot, params_.mode_tpage, params_.mode_dfe, params_.mode_dtd);
    }
    return true;
}

void LensFlare::draw_fan(LensFlareHost& host, const FlareFan& fan, std::int32_t angle, std::int32_t size, int cx,
                         int cy) {
    const std::int32_t half = (4096 / fan.rays) / 2;
    const std::int32_t tip = mul_q12(size, fan.outer);
    const std::int32_t base = mul_q12(size, fan.inner);
    std::int32_t a = angle;
    for (int i = 0; i < fan.rays; i++) {
        SplashQuad q;
        q.x[0] = mul_q12(host.sin(a), base) + cx;
        q.y[0] = mul_q12(host.cos(a), base) + cy;
        a += half;
        q.x[2] = mul_q12(host.sin(a), tip) + cx;
        q.y[2] = mul_q12(host.cos(a), tip) + cy;
        a += half;
        // The last ray closes on the first one's edge exactly.
        if (i == fan.rays - 1) {
            a = angle;
        }
        q.x[3] = mul_q12(host.sin(a), base) + cx;
        q.y[3] = mul_q12(host.cos(a), base) + cy;
        q.x[1] = cx;
        q.y[1] = cy;
        q.color[0] = 0;
        q.color[1] = fan.color;
        q.color[2] = 0;
        q.color[3] = 0;
        host.emit_quad(q, params_.slot);
    }
}

void LensFlare::draw_corona(LensFlareHost& host, std::int32_t size, int cx, int cy) {
    const FlareCorona& c = params_.corona;
    const std::int32_t radius = mul_q12(size, c.scale);
    const std::int32_t turn = 4096 / c.spokes;
    const auto shade = [&](std::int32_t b) -> std::uint32_t {
        return b >= 0 && b < static_cast<std::int32_t>(c.shade.size()) ? c.shade[static_cast<std::size_t>(b)] : 0u;
    };
    // Positions are Q12 pixels; each ring is radius / rings wide.
    const std::int32_t x = cx * 4096;
    const std::int32_t y = cy * 4096;
    std::int32_t angle = 0;
    for (int i = 0; i < c.spokes; i++) {
        std::int32_t w0 = host.sin(angle);
        std::int32_t h0 = host.cos(angle);
        angle += turn;
        std::int32_t w1 = host.sin(angle);
        std::int32_t h1 = host.cos(angle);
        w0 = (w0 * radius) / c.rings;
        h0 = (h0 * radius) / c.rings;
        w1 = (w1 * radius) / c.rings;
        h1 = (h1 * radius) / c.rings;

        std::int32_t x0 = x, y0 = y, x1 = x, y1 = y;
        std::int32_t b = c.brightness_start;
        for (int j = 0; j < c.rings; j++) {
            SplashQuad q;
            q.x[0] = x0 >> 12;
            q.x[1] = (x0 + w0) >> 12;
            q.x[2] = x1 >> 12;
            q.x[3] = (x1 + w1) >> 12;
            q.y[0] = y0 >> 12;
            q.y[1] = (y0 + h0) >> 12;
            q.y[2] = y1 >> 12;
            q.y[3] = (y1 + h1) >> 12;
            const std::uint32_t inner = shade(b);
            const std::uint32_t outer = shade(b - c.brightness_step);
            q.color[0] = inner;
            q.color[1] = outer;
            q.color[2] = inner;
            q.color[3] = outer;
            host.emit_quad(q, params_.slot);
            x0 += w0;
            x1 += w1;
            y0 += h0;
            y1 += h1;
            b -= c.brightness_step;
        }
    }
}

namespace {

namespace p = parse;

std::uint32_t word(const json::Value& v) {
    if (!v.is_integer() || v.integer() < 0 || v.integer() > 0xFFFFFFFFll) {
        p::fail(v, "an unsigned 32-bit colour word is expected");
    }
    return static_cast<std::uint32_t>(v.integer());
}

constexpr std::int64_t kBig = 1 << 24;

LensFlareParams parse_one(const json::Value& v) {
    p::reject_unknown(v, {"origins", "yaw", "slot", "size_numerator", "max_size", "fans", "corona", "prim_code",
                          "mode_tpage", "mode_dfe", "mode_dtd"});
    LensFlareParams out;
    const json::Value& origins = p::member(v, "origins");
    if (!origins.is_array() || origins.array().empty()) {
        p::fail(origins, "\"origins\" must be a non-empty array of [x, y, z] (Q8)");
    }
    for (const json::Value& o : origins.array()) {
        if (!o.is_array() || o.array().size() != 3) {
            p::fail(o, "an origin is [x, y, z] (Q8)");
        }
        out.origins_q8.push_back(Vec3{p::integer(o.array()[0], -kBig * 16, kBig * 16),
                                      p::integer(o.array()[1], -kBig * 16, kBig * 16),
                                      p::integer(o.array()[2], -kBig * 16, kBig * 16)});
    }
    out.yaw = v.find("yaw") != nullptr ? p::integer_of(v, "yaw", -32768, 32767) : 0;
    out.slot = p::integer_of(v, "slot", 0, 65535);
    out.size_numerator = p::integer_of(v, "size_numerator", 0, 65535);
    out.max_size = p::integer_of(v, "max_size", -kBig, kBig);
    out.prim_code = p::integer_of(v, "prim_code", 0, 255);

    const json::Value& fans = p::member(v, "fans");
    if (!fans.is_array()) {
        p::fail(fans, "\"fans\" must be an array");
    }
    for (const json::Value& f : fans.array()) {
        p::reject_unknown(f, {"rays", "angle", "spin", "outer", "inner", "color"});
        FlareFan fan;
        fan.rays = p::integer_of(f, "rays", 1, 2048);
        fan.angle = p::integer_of(f, "angle", -kBig, kBig);
        fan.spin = f.find("spin") != nullptr ? p::integer_of(f, "spin", -4096, 4096) : 0;
        fan.outer = p::integer_of(f, "outer", -kBig, kBig);
        fan.inner = p::integer_of(f, "inner", -kBig, kBig);
        fan.color = word(p::member(f, "color"));
        out.fans.push_back(fan);
    }

    if (const json::Value* c = v.find("corona"); c != nullptr) {
        p::reject_unknown(*c, {"spokes", "rings", "scale", "brightness_start", "brightness_step", "shade"});
        out.corona.present = true;
        out.corona.spokes = p::integer_of(*c, "spokes", 1, 4096);
        out.corona.rings = p::integer_of(*c, "rings", 1, 256);
        out.corona.scale = p::integer_of(*c, "scale", -kBig, kBig);
        out.corona.brightness_start = p::integer_of(*c, "brightness_start", 0, 255);
        out.corona.brightness_step = p::integer_of(*c, "brightness_step", 0, 255);
        const json::Value& shade = p::member(*c, "shade");
        if (!shade.is_array() || shade.array().empty() || shade.array().size() > 256) {
            p::fail(shade, "\"shade\" must be an array of 1..256 colour words (missing entries are 0)");
        }
        for (std::size_t i = 0; i < shade.array().size(); i++) {
            out.corona.shade[i] = word(shade.array()[i]);
        }
    }

    if (v.find("mode_tpage") != nullptr) {
        out.mode = true;
        out.mode_tpage = p::integer_of(v, "mode_tpage", 0, 65535);
        out.mode_dfe = p::integer_of(v, "mode_dfe", 0, 1);
        out.mode_dtd = p::integer_of(v, "mode_dtd", 0, 1);
    }
    return out;
}

}  // namespace

Result<std::map<std::string, LensFlareParams>> parse_lens_flares(std::string_view json_text) try {
    using R = Result<std::map<std::string, LensFlareParams>>;
    Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return R::fail(doc.error());
    }
    const json::Value& root = doc.value();
    p::reject_unknown(root, {"flares"});
    const json::Value& flares = p::member(root, "flares");
    if (!flares.is_object()) {
        p::fail(flares, "\"flares\" must be an object of named flares");
    }
    std::map<std::string, LensFlareParams> out;
    for (const auto& [name, value] : flares.members()) {
        if (name.rfind("_", 0) == 0) {
            continue;
        }
        try {
            out.emplace(name, parse_one(value));
        } catch (const std::exception& e) {
            throw std::runtime_error("flare \"" + name + "\": " + e.what());
        }
    }
    return R::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<std::map<std::string, LensFlareParams>>::fail(e.what());
}

Result<std::map<std::string, LensFlareParams>> load_lens_flares(const std::filesystem::path& gamedata_dir,
                                                               const std::filesystem::path& mods_dir,
                                                               std::string* source) {
    using R = Result<std::map<std::string, LensFlareParams>>;
    std::string where;
    Result<std::string> text =
        read_effect_file(gamedata_dir, mods_dir, std::filesystem::path("effects") / "lens_flares.json", &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!text) {
        return R::fail(text.error());
    }
    R parsed = parse_lens_flares(text.value());
    if (!parsed) {
        return R::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
