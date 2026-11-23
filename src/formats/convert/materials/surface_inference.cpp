// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "formats/convert/materials/surface_inference.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_map>

#include "formats/png/sh_png.h"

namespace sh::convert::materials {

namespace fs = std::filesystem;

namespace {

float lum(const unsigned char* p) { return (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f; }

// Normalised autocorrelation of a profile at `lag`: 1 for a profile that repeats exactly with that period.
float repeat_at(const std::vector<float>& v, std::size_t lag) {
    if (lag * 2 > v.size()) return 0.0f;
    float mean = 0.0f;
    for (float x : v) mean += x;
    mean /= static_cast<float>(v.size());
    float num = 0.0f, den = 0.0f;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const float a = v[i] - mean;
        den += a * a;
        if (i + lag < v.size()) num += a * (v[i + lag] - mean);
    }
    if (den < 1e-6f) return 0.0f;
    return num / den * static_cast<float>(v.size()) / static_cast<float>(v.size() - lag);
}

float contrast(const std::vector<float>& v) {
    float lo = 1.0f, hi = 0.0f;
    for (float x : v) {
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    return hi - lo;
}

}  // namespace

PictureStats picture_stats(const unsigned char* rgba, unsigned width, unsigned height) {
    PictureStats s;
    if (width == 0 || height == 0) return s;
    std::vector<float> cols(width, 0.0f), rows(height, 0.0f);
    std::vector<unsigned> col_n(width, 0), row_n(height, 0);
    double l_sum = 0, s_sum = 0, d_sum = 0;
    unsigned d_n = 0;
    std::unordered_map<std::uint32_t, unsigned> colours;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const unsigned char* p = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
            if (p[3] == 0) continue;
            const float l = lum(p);
            const int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
            ++s.opaque;
            ++colours[static_cast<std::uint32_t>(p[0]) << 16 | static_cast<std::uint32_t>(p[1]) << 8 | p[2]];
            l_sum += l;
            s_sum += mx > 0 ? static_cast<double>(mx - mn) / mx : 0.0;
            cols[x] += l;
            rows[y] += l;
            ++col_n[x];
            ++row_n[y];
            if (x + 1 < width && y + 1 < height) {
                const unsigned char* r = p + 4;
                const unsigned char* b = p + static_cast<std::size_t>(width) * 4;
                if (r[3] != 0 && b[3] != 0) {
                    d_sum += std::abs(lum(r) - l) + std::abs(lum(b) - l);
                    d_n += 2;
                }
            }
        }
    }
    if (s.opaque == 0) return s;
    unsigned most = 0;
    for (const auto& [c, n] : colours) most = std::max(most, n);
    s.filler = static_cast<float>(most) / static_cast<float>(s.opaque);
    s.luminance = static_cast<float>(l_sum / s.opaque);
    s.saturation = static_cast<float>(s_sum / s.opaque);
    s.detail = d_n ? static_cast<float>(d_sum / d_n) : 0.0f;
    for (unsigned x = 0; x < width; ++x) cols[x] = col_n[x] ? cols[x] / static_cast<float>(col_n[x]) : s.luminance;
    for (unsigned y = 0; y < height; ++y) rows[y] = row_n[y] ? rows[y] / static_cast<float>(row_n[y]) : s.luminance;
    // Grout lines run the whole page: the mean of each column and row shows them, and they repeat with the tile.
    if (contrast(cols) > 0.04f && contrast(rows) > 0.04f) {
        // Pages are atlases laid out in 32 and 64 texel blocks: only the smaller periods of floor tiles count.
        for (std::size_t period : {8u, 16u}) {
            const float g = std::min(repeat_at(cols, period), repeat_at(rows, period));
            if (g > s.grid) {
                s.grid = g;
                s.grid_period = static_cast<int>(period);
            }
        }
    }
    s.grid = std::clamp(s.grid, 0.0f, 1.0f);
    return s;
}

SurfaceKind classify(const PictureStats& s) {
    // A page that is largely one flat colour is mostly empty space: its statistics say nothing of the surfaces.
    if (s.opaque < 256 || s.filler > 0.25f) return SurfaceKind::None;
    if (s.grid >= 0.7f && s.saturation < 0.3f) return SurfaceKind::Tiles;
    // Bare metal: grey, mid bright, smooth but not flat; a flat grey area this large is filler, not metal.
    if (s.filler < 0.1f && s.saturation < 0.08f && s.luminance > 0.25f && s.luminance < 0.7f && s.detail > 0.01f &&
        s.detail < 0.035f)
        return SurfaceKind::Metal;
    return SurfaceKind::None;
}

Result<InferenceReport> write_surface_inference(const fs::path& gamedata_dir) {
    InferenceReport report;
    const fs::path dir = gamedata_dir / "convert" / "textures" / "BG";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return Result<InferenceReport>::fail("no " + dir.string());
    std::vector<fs::path> pages;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        const fs::path& p = e.path();
        // The page itself: NAME.png; NAME.<variant>.png and the sprite sheets are not pages of their own.
        if (p.extension() != ".png" || p.stem().has_extension()) continue;
        pages.push_back(p);
    }
    std::sort(pages.begin(), pages.end());
    std::string body;
    char buf[512];
    for (const fs::path& p : pages) {
        std::ifstream in(p, std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto img = png::decode_rgba8(bytes);
        if (!img) {
            report.warnings.push_back(p.filename().string() + ": " + img.error());
            continue;
        }
        ++report.pages;
        const PictureStats s = picture_stats(img.value().pixels.data(), img.value().width, img.value().height);
        const SurfaceKind kind = classify(s);
        if (kind == SurfaceKind::None) continue;
        const std::string page = p.stem().string();
        const bool tiles = kind == SurfaceKind::Tiles;
        (tiles ? report.tiles : report.metal)++;
        std::snprintf(buf, sizeof buf,
                      "%s    \"%s\": {\"kind\": \"%s\", \"match\": [\"%s\"], %s, "
                      "\"stats\": {\"luminance\": %.3f, \"saturation\": %.3f, \"detail\": %.3f, \"grid\": %.3f, "
                      "\"grid_period\": %d, \"filler\": %.3f}}",
                      body.empty() ? "" : ",\n", page.c_str(), tiles ? "tiles" : "metal", page.c_str(),
                      tiles ? "\"roughness\": 0.45, \"wet\": 0.12"
                            : "\"roughness\": 0.5, \"metalness\": 0.6, \"reflective\": 0.12",
                      s.luminance, s.saturation, s.detail, s.grid, s.grid_period, s.filler);
        body += buf;
    }
    const std::string text =
        "{\n  \"format\": \"sh-surface-materials\",\n  \"version\": 1,\n"
        "  \"description\": \"Surface materials the import proposes from the map's texture pages "
        "(docs/RENDER.md section 10): tiles (a regular grid of grout lines) get a faint wet sheen, bare grey "
        "metal a metallic gloss; other pages are neutral. gamedata/materials/<page>.json overrides an entry key by key; any "
        "hand-written material that names the page wins. Generated on every import, never committed.\",\n"
        "  \"materials\": {\n" +
        body + "\n  }\n}\n";
    const fs::path out = gamedata_dir / "convert" / "surface_materials.json";
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) return Result<InferenceReport>::fail("cannot write " + out.string());
    f << text;
    return Result<InferenceReport>::ok(std::move(report));
}

std::string format_inference_line(const InferenceReport& report) {
    return "surface materials: " + std::to_string(report.pages) + " pages, " + std::to_string(report.tiles) +
           " tiles, " + std::to_string(report.metal) + " metal";
}

}  // namespace sh::convert::materials
