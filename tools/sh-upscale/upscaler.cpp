#include "sh-upscale/upscaler.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <utility>

#include "formats/common/json.h"

namespace sh::upscale {

void bleed_colour(Picture& p) {
    const int w = p.width, h = p.height;
    std::vector<std::uint8_t> known(static_cast<std::size_t>(w) * h);
    std::size_t unknown = 0;
    for (std::size_t i = 0; i < known.size(); ++i) {
        known[i] = p.rgba[i * 4 + 3] != 0;
        if (!known[i]) ++unknown;
    }
    if (unknown == known.size()) return;
    // A ring per pass; the network's context is small, so a few rings cover what it reads.
    for (int pass = 0; pass < 16 && unknown > 0; ++pass) {
        std::vector<std::uint8_t> next = known;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * w + x;
                if (known[i]) continue;
                int sum[3] = {0, 0, 0}, n = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = x + dx, sy = y + dy;
                        if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
                        const std::size_t j = static_cast<std::size_t>(sy) * w + sx;
                        if (!known[j]) continue;
                        for (int c = 0; c < 3; ++c) sum[c] += p.rgba[j * 4 + c];
                        ++n;
                    }
                }
                if (n == 0) continue;
                for (int c = 0; c < 3; ++c) p.rgba[i * 4 + c] = static_cast<std::uint8_t>(sum[c] / n);
                next[i] = 1;
                --unknown;
            }
        }
        known.swap(next);
    }
}

void scale_alpha_nearest(const Picture& src, int scale, Picture& dst) {
    for (int y = 0; y < dst.height; ++y) {
        const int sy = std::min(y / scale, src.height - 1);
        for (int x = 0; x < dst.width; ++x) {
            const int sx = std::min(x / scale, src.width - 1);
            dst.rgba[(static_cast<std::size_t>(y) * dst.width + x) * 4 + 3] =
                src.rgba[(static_cast<std::size_t>(sy) * src.width + sx) * 4 + 3];
        }
    }
}

bool upscale_tiled(const Picture& src, int scale, int tile, int pad, const TileInfer& infer, Picture& dst) {
    dst.width = src.width * scale;
    dst.height = src.height * scale;
    dst.rgba.assign(static_cast<std::size_t>(dst.width) * dst.height * 4, 0);
    std::vector<std::uint8_t> in, out;
    for (int ty = 0; ty < src.height; ty += tile) {
        for (int tx = 0; tx < src.width; tx += tile) {
            const int cw = std::min(tile, src.width - tx), ch = std::min(tile, src.height - ty);
            const int iw = cw + 2 * pad, ih = ch + 2 * pad;
            in.resize(static_cast<std::size_t>(iw) * ih * 3);
            for (int y = 0; y < ih; ++y) {
                const int sy = std::clamp(ty - pad + y, 0, src.height - 1);
                for (int x = 0; x < iw; ++x) {
                    const int sx = std::clamp(tx - pad + x, 0, src.width - 1);
                    const std::uint8_t* s = &src.rgba[(static_cast<std::size_t>(sy) * src.width + sx) * 4];
                    std::uint8_t* d = &in[(static_cast<std::size_t>(y) * iw + x) * 3];
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                }
            }
            out.clear();
            if (!infer(in, iw, ih, out) || out.size() != static_cast<std::size_t>(iw) * scale * ih * scale * 3)
                return false;
            const int ow = iw * scale;
            for (int y = 0; y < ch * scale; ++y) {
                for (int x = 0; x < cw * scale; ++x) {
                    const std::uint8_t* s =
                        &out[(static_cast<std::size_t>(y + pad * scale) * ow + x + pad * scale) * 3];
                    std::uint8_t* d =
                        &dst.rgba[(static_cast<std::size_t>(ty * scale + y) * dst.width + tx * scale + x) * 4];
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                }
            }
        }
    }
    scale_alpha_nearest(src, scale, dst);
    return true;
}

void shrink_colour(Picture& p, int factor) {
    Picture out{p.width / factor, p.height / factor, {}};
    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            int sum[3] = {0, 0, 0};
            for (int dy = 0; dy < factor; ++dy)
                for (int dx = 0; dx < factor; ++dx)
                    for (int c = 0; c < 3; ++c)
                        sum[c] +=
                            p.rgba[(static_cast<std::size_t>(y * factor + dy) * p.width + x * factor + dx) * 4 + c];
            for (int c = 0; c < 3; ++c)
                out.rgba[(static_cast<std::size_t>(y) * out.width + x) * 4 + c] =
                    static_cast<std::uint8_t>((sum[c] + factor * factor / 2) / (factor * factor));
        }
    }
    p = std::move(out);
}

namespace {

double lanczos3(double x) {
    x = std::fabs(x);
    if (x < 1e-9) return 1.0;
    if (x >= 3.0) return 0.0;
    constexpr double kPi = 3.14159265358979323846;
    const double a = kPi * x;
    return 3.0 * std::sin(a) * std::sin(a / 3.0) / (a * a);
}

// The six taps of each of the `scale` output phases: the first source texel and normalised weights.
struct Taps {
    int first = 0;
    double w[6] = {};
};
std::vector<Taps> lanczos_taps(int scale) {
    std::vector<Taps> out(static_cast<std::size_t>(scale));
    for (int phase = 0; phase < scale; ++phase) {
        const double centre = (phase + 0.5) / scale - 0.5;
        Taps& t = out[static_cast<std::size_t>(phase)];
        t.first = static_cast<int>(std::floor(centre)) - 2;
        double sum = 0;
        for (int k = 0; k < 6; ++k) sum += t.w[k] = lanczos3(centre - (t.first + k));
        for (double& v : t.w) v /= sum;
    }
    return out;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

void scale_lanczos(const Picture& src, int scale, Picture& dst) {
    const int w = src.width, h = src.height, W = w * scale, H = h * scale;
    const std::vector<Taps> taps = lanczos_taps(scale);
    // Rows first, into a float picture W x h, then columns; the edge texels repeat past the picture.
    std::vector<float> rows(static_cast<std::size_t>(W) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int X = 0; X < W; ++X) {
            const Taps& t = taps[static_cast<std::size_t>(X % scale)];
            const int base = X / scale + t.first;
            double acc[3] = {0, 0, 0};
            for (int k = 0; k < 6; ++k) {
                const int sx = std::clamp(base + k, 0, w - 1);
                const std::uint8_t* p = &src.rgba[(static_cast<std::size_t>(y) * w + sx) * 4];
                for (int c = 0; c < 3; ++c) acc[c] += t.w[k] * p[c];
            }
            for (int c = 0; c < 3; ++c)
                rows[(static_cast<std::size_t>(y) * W + X) * 3 + c] = static_cast<float>(acc[c]);
        }
    }
    dst.width = W;
    dst.height = H;
    dst.rgba.assign(static_cast<std::size_t>(W) * H * 4, 255);
    for (int Y = 0; Y < H; ++Y) {
        const Taps& t = taps[static_cast<std::size_t>(Y % scale)];
        const int base = Y / scale + t.first;
        for (int X = 0; X < W; ++X) {
            double acc[3] = {0, 0, 0};
            for (int k = 0; k < 6; ++k) {
                const int sy = std::clamp(base + k, 0, h - 1);
                const float* p = &rows[(static_cast<std::size_t>(sy) * W + X) * 3];
                for (int c = 0; c < 3; ++c) acc[c] += t.w[k] * p[c];
            }
            std::uint8_t* d = &dst.rgba[(static_cast<std::size_t>(Y) * W + X) * 4];
            for (int c = 0; c < 3; ++c) d[c] = static_cast<std::uint8_t>(std::clamp(std::lround(acc[c]), 0L, 255L));
        }
    }
}

void paste_rects(const Picture& sharp, const std::vector<Rect>& rects, int scale, Picture& dst) {
    if (sharp.width != dst.width || sharp.height != dst.height || scale < 1) return;
    for (const Rect& r : rects) {
        // The fade reaches one source texel past the rectangle.
        const int x0 = std::max(0, (r.x - 1) * scale), y0 = std::max(0, (r.y - 1) * scale);
        const int x1 = std::min(dst.width, (r.x + r.w + 1) * scale), y1 = std::min(dst.height, (r.y + r.h + 1) * scale);
        for (int Y = y0; Y < y1; ++Y) {
            for (int X = x0; X < x1; ++X) {
                // Distance of the texel centre from the rectangle, in source texels.
                const double sx = (X + 0.5) / scale, sy = (Y + 0.5) / scale;
                const double dx = std::max({r.x - sx, 0.0, sx - (r.x + r.w)});
                const double dy = std::max({r.y - sy, 0.0, sy - (r.y + r.h)});
                const double a = std::clamp(1.0 - std::max(dx, dy), 0.0, 1.0);
                if (a <= 0) continue;
                const std::size_t i = (static_cast<std::size_t>(Y) * dst.width + X) * 4;
                for (int c = 0; c < 3; ++c)
                    dst.rgba[i + c] =
                        static_cast<std::uint8_t>(std::lround(dst.rgba[i + c] * (1.0 - a) + sharp.rgba[i + c] * a));
            }
        }
    }
}

std::string Lettering::signature() const {
    if (rects.empty()) return "whole";
    std::string out;
    for (const Rect& r : rects) {
        if (!out.empty()) out += ';';
        out += std::to_string(r.x) + ',' + std::to_string(r.y) + ',' + std::to_string(r.w) + ',' + std::to_string(r.h);
    }
    return out;
}

std::optional<LetteringList> parse_lettering(std::string_view json, std::string& error) {
    const Result<json::Value> doc = json::parse(json);
    if (!doc) {
        error = doc.error();
        return std::nullopt;
    }
    LetteringList out;
    const json::Value* list = doc.value().find("lettering");
    if (list == nullptr) return out;
    if (!list->is_array()) {
        error = "\"lettering\" must be an array";
        return std::nullopt;
    }
    for (const json::Value& e : list->array()) {
        const json::Value* name = e.find("name");
        const std::string at = "line " + std::to_string(e.line()) + ": ";
        if (name == nullptr || !name->is_string() || name->string().empty()) {
            error = at + "an entry needs a \"name\", the texture as \"<FOLDER>/<NAME>\"";
            return std::nullopt;
        }
        Lettering l;
        if (const json::Value* rects = e.find("rects")) {
            if (!rects->is_array()) {
                error = at + "\"rects\" must be an array of [x, y, w, h]";
                return std::nullopt;
            }
            for (const json::Value& r : rects->array()) {
                const bool four = r.is_array() && r.array().size() == 4 &&
                                  std::all_of(r.array().begin(), r.array().end(),
                                              [](const json::Value& v) { return v.is_integer() && v.integer() >= 0; });
                if (!four || r.array()[2].integer() == 0 || r.array()[3].integer() == 0) {
                    error = "line " + std::to_string(r.line()) + ": a rectangle is [x, y, w, h] in texels, w and h > 0";
                    return std::nullopt;
                }
                l.rects.push_back(
                    Rect{static_cast<int>(r.array()[0].integer()), static_cast<int>(r.array()[1].integer()),
                         static_cast<int>(r.array()[2].integer()), static_cast<int>(r.array()[3].integer())});
            }
            if (l.rects.empty()) {
                error = at + "\"rects\" is empty: leave it out for the whole picture";
                return std::nullopt;
            }
        }
        out[lower(name->string())] = std::move(l);
    }
    return out;
}

const Lettering* lettering_for(const LetteringList& list, std::string_view key) {
    const std::string k = lower(key);
    if (const auto it = list.find(k); it != list.end()) return &it->second;
    const std::size_t slash = k.rfind('/');
    const std::size_t dot = k.find('.', slash == std::string::npos ? 0 : slash);
    if (dot == std::string::npos) return nullptr;
    const auto it = list.find(k.substr(0, dot));
    return it == list.end() ? nullptr : &it->second;
}

}  // namespace sh::upscale
