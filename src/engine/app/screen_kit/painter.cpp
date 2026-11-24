// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/screen_kit/painter.h"

#include "formats/common/json.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

namespace sh::screen_kit {

namespace {

namespace fs = std::filesystem;

// Rasterised lines kept at most; a text field being typed in makes a new one per key.
constexpr std::size_t kMaxTextures = 768;

bool read_rgb(const sh::json::Value* v, Rgb& out) {
    if (v == nullptr || !v->is_array() || v->array().size() != 3) return false;
    int c[3];
    for (int i = 0; i < 3; ++i) {
        const sh::json::Value& n = v->array()[static_cast<std::size_t>(i)];
        if (!n.is_number()) return false;
        c[i] = std::clamp(static_cast<int>(n.number()), 0, 255);
    }
    out = {static_cast<std::uint8_t>(c[0]), static_cast<std::uint8_t>(c[1]), static_cast<std::uint8_t>(c[2])};
    return true;
}

template <std::size_t N>
void read_rgbs(const sh::json::Value* v, std::array<Rgb, N>& out) {
    if (v == nullptr || !v->is_array() || v->array().size() != N) return;
    std::array<Rgb, N> got;
    for (std::size_t i = 0; i < N; ++i)
        if (!read_rgb(&v->array()[i], got[i])) return;
    out = got;
}

SDL_FColor fcolor(Rgb c, std::uint8_t alpha = 255) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha / 255.0f};
}

}  // namespace

Style load_style(const fs::path& file) {
    Style s;
    std::ifstream in(file, std::ios::binary);
    if (!in) return s;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto doc = sh::json::parse(text);
    if (!doc) return s;
    const sh::json::Value& root = doc.value();
    if (const auto* u = root.find("underline")) read_rgbs(u->find("colors"), s.underline);
    if (const auto* b = root.find("bullet")) {
        read_rgbs(b->find("front"), s.bullet_front);
        read_rgbs(b->find("front_inactive"), s.bullet_front_inactive);
        read_rgbs(b->find("border"), s.bullet_border);
        read_rgbs(b->find("border_inactive"), s.bullet_border_inactive);
    }
    if (const auto* a = root.find("arrow")) read_rgb(a->find("solid"), s.arrow);
    if (const auto* v = root.find("vignette")) read_rgb(v->find("edge_color"), s.vignette_edge);
    return s;
}

TextFace face_for(const std::vector<std::string>& language_fonts, bool rtl) {
    TextFace face;
    face.families = {"Barlow Semi Condensed"};
    face.families.insert(face.families.end(), language_fonts.begin(), language_fonts.end());
    face.rtl = rtl;
    return face;
}

Painter::Painter(const fs::path& base_data)
    : style_(load_style(base_data / "menu" / "menu_style.json")), raster_(base_data / "font") {}

Painter::~Painter() { clear_text(); }

void Painter::attach(SDL_Renderer* renderer) {
    clear_text();
    renderer_ = renderer;
}

void Painter::set_view(const View& view, bool mirrored) {
    if (view.scale != view_.scale) clear_text();
    if (view.width != view_.width) wrapped_.clear();
    view_ = view;
    mirrored_ = mirrored;
}

Rect Painter::box(float x0, float y0, float x1, float y1) const {
    const float a = sx(x0), b = sx(x1);
    return {std::min(a, b), sy(y0), std::max(a, b), sy(y1)};
}

Align Painter::flip(Align a) const {
    if (!mirrored_ || a == Align::Center) return a;
    return a == Align::Left ? Align::Right : Align::Left;
}

void Painter::set_face(TextFace face) {
    face_ = std::move(face);
    wrapped_.clear();
}

void Painter::clear_text() {
    for (auto& [k, t] : textures_)
        if (t.texture) SDL_DestroyTexture(t.texture);
    textures_.clear();
    widths_.clear();
    wrapped_.clear();
}

std::string Painter::key(const std::string& s, int px, const TextFace& face) const {
    std::string k = s;
    k += '\x1f';
    k += std::to_string(px);
    k += face.rtl ? 'r' : 'l';
    for (const auto& f : face.families) k += '\x1f' + f;
    return k;
}

const Texture& Painter::text(const std::string& s, float size, const TextFace& face) {
    const float px = std::max(6.0f, std::round(size * view_.scale));
    std::string k = key(s, static_cast<int>(px), face);
    auto it = textures_.find(k);
    if (it != textures_.end()) return it->second;
    if (textures_.size() >= kMaxTextures) {
        for (auto& [old, t] : textures_)
            if (t.texture) SDL_DestroyTexture(t.texture);
        textures_.clear();
    }
    Texture t;
    const TextBitmap bitmap = raster_.render(s, px, face.families, face.rtl);
    if (SDL_Surface* surface = SDL_CreateSurface(bitmap.width, bitmap.height, SDL_PIXELFORMAT_RGBA32)) {
        for (int y = 0; y < bitmap.height; ++y) {
            auto* row = static_cast<std::uint8_t*>(surface->pixels) + static_cast<std::size_t>(y) * surface->pitch;
            for (int x = 0; x < bitmap.width; ++x) {
                std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
                p[0] = p[1] = p[2] = 255;
                p[3] = bitmap.alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.width) +
                                    static_cast<std::size_t>(x)];
            }
        }
        t.texture = renderer_ ? SDL_CreateTextureFromSurface(renderer_, surface) : nullptr;
        SDL_DestroySurface(surface);
        if (t.texture) {
            SDL_SetTextureBlendMode(t.texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t.texture, SDL_SCALEMODE_LINEAR);
        }
        t.width = bitmap.width;
        t.height = bitmap.height;
    }
    widths_[k] = static_cast<float>(t.width);
    return textures_.emplace(std::move(k), t).first->second;
}

float Painter::width(const std::string& s, float size, const TextFace* face) {
    const TextFace& f = face ? *face : face_;
    const float px = std::max(6.0f, std::round(size * view_.scale));
    std::string k = key(s, static_cast<int>(px), f);
    auto it = widths_.find(k);
    if (it == widths_.end()) {
        // Measured without a texture: wrapping tries many lines that are never drawn.
        const TextBitmap bitmap = raster_.render(s, px, f.families, f.rtl);
        it = widths_.emplace(std::move(k), static_cast<float>(bitmap.width)).first;
    }
    return it->second / view_.scale;
}

void Painter::blit(const Texture& t, float left, float top, Rgb color, std::uint8_t alpha, bool shadow) {
    if (t.texture == nullptr) return;
    const float w = static_cast<float>(t.width), h = static_cast<float>(t.height);
    const SDL_FRect dst{std::round(left), std::round(top), w, h};
    if (shadow) {
        const float d = std::max(1.0f, std::round(view_.scale));
        const SDL_FRect sh_dst{dst.x + d, dst.y + d, w, h};
        SDL_SetTextureColorMod(t.texture, 0, 0, 0);
        SDL_SetTextureAlphaMod(t.texture, static_cast<std::uint8_t>(alpha * 3 / 4));
        SDL_RenderTexture(renderer_, t.texture, nullptr, &sh_dst);
    }
    SDL_SetTextureColorMod(t.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(t.texture, alpha);
    SDL_RenderTexture(renderer_, t.texture, nullptr, &dst);
}

float Painter::draw_text(const std::string& s, float x, float y, float size, Rgb color, Align align, float max_w,
                         const TextFace* face, bool shadow, std::uint8_t alpha) {
    if (s.empty()) return 0.0f;
    const TextFace& f = face ? *face : face_;
    const Texture* t = &text(s, size, f);
    float w = static_cast<float>(t->width);
    if (max_w > 0.0f && w > max_w * view_.scale) {
        // Too wide for its place: drawn smaller rather than cut.
        const float smaller = std::max(size * 0.6f, size * max_w * view_.scale / w);
        t = &text(s, smaller, f);
        w = static_cast<float>(t->width);
    }
    if (t->texture == nullptr) return 0.0f;
    float left = sx(x);
    switch (flip(align)) {
        case Align::Left:
            break;
        case Align::Center:
            left -= w * 0.5f;
            break;
        case Align::Right:
            left -= w;
            break;
    }
    blit(*t, left, sy(y) - static_cast<float>(t->height) * 0.5f, color, alpha, shadow);
    return w / view_.scale;
}

const std::vector<std::string>& Painter::wrap(const std::string& s, float size, float max_w, const TextFace* face) {
    const TextFace& f = face ? *face : face_;
    std::string k = key(s, static_cast<int>(size * 100.0f), f);
    k += '\x1f' + std::to_string(static_cast<int>(max_w * 100.0f));
    auto it = wrapped_.find(k);
    if (it != wrapped_.end()) return it->second;
    std::vector<std::string> lines =
        wrap_lines(s, max_w, [&](std::string_view line) { return width(std::string(line), size, &f); });
    return wrapped_.emplace(std::move(k), std::move(lines)).first->second;
}

void Painter::fill(Rect r, Rgb c, std::uint8_t alpha) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, alpha);
    const SDL_FRect rect{r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0};
    SDL_RenderFillRect(renderer_, &rect);
}

void Painter::outline(Rect r, float t, Rgb c, std::uint8_t alpha) {
    t = std::max(1.0f, std::round(t));
    fill({r.x0, r.y0, r.x1, r.y0 + t}, c, alpha);
    fill({r.x0, r.y1 - t, r.x1, r.y1}, c, alpha);
    fill({r.x0, r.y0 + t, r.x0 + t, r.y1 - t}, c, alpha);
    fill({r.x1 - t, r.y0 + t, r.x1, r.y1 - t}, c, alpha);
}

void Painter::fill_gradient(Rect r, Rgb a, std::uint8_t a_alpha, Rgb b, std::uint8_t b_alpha, bool horizontal) {
    const SDL_FColor ca = fcolor(a, a_alpha);
    const SDL_FColor cb = fcolor(b, b_alpha);
    SDL_Vertex v[4];
    v[0] = {{r.x0, r.y0}, ca, {0, 0}};
    v[1] = {{r.x1, r.y0}, horizontal ? cb : ca, {0, 0}};
    v[2] = {{r.x0, r.y1}, horizontal ? ca : cb, {0, 0}};
    v[3] = {{r.x1, r.y1}, cb, {0, 0}};
    const int idx[6] = {0, 1, 2, 1, 3, 2};
    SDL_RenderGeometry(renderer_, nullptr, v, 4, idx, 6);
}

void Painter::bullet(float x, float y, bool active) {
    // The menus' bullet: a square split along a diagonal, the border a size larger behind the face.
    const auto& border = active ? style_.bullet_border : style_.bullet_border_inactive;
    const auto& front = active ? style_.bullet_front : style_.bullet_front_inactive;
    const auto quad = [&](float half, const std::array<Rgb, 3>& c) {
        const float cx = sx(x), cy = sy(y), h = half * view_.scale;
        SDL_Vertex v[4];
        v[0] = {{cx - h, cy - h}, fcolor(c[0]), {0, 0}};
        v[1] = {{cx + h, cy - h}, fcolor(c[1]), {0, 0}};
        v[2] = {{cx - h, cy + h}, fcolor(c[1]), {0, 0}};
        v[3] = {{cx + h, cy + h}, fcolor(c[2]), {0, 0}};
        const int idx[6] = {0, 1, 2, 1, 3, 2};
        SDL_RenderGeometry(renderer_, nullptr, v, 4, idx, 6);
    };
    quad(4.0f, border);
    quad(2.8f, front);
}

void Painter::arrow(float x, float y, bool right, float size) {
    if (mirrored_) right = !right;
    const float cx = sx(x), cy = sy(y), h = size * view_.scale;
    const float dir = right ? 1.0f : -1.0f;
    const SDL_FColor c = fcolor(style_.arrow);
    SDL_Vertex v[3];
    v[0] = {{cx + dir * h, cy}, c, {0, 0}};
    v[1] = {{cx - dir * h * 0.6f, cy - h}, c, {0, 0}};
    v[2] = {{cx - dir * h * 0.6f, cy + h}, c, {0, 0}};
    SDL_RenderGeometry(renderer_, nullptr, v, 3, nullptr, 0);
}

void Painter::underline(float x0, float x1, float y) {
    fill_gradient(box(x0 - 6.0f, y - 10.0f, x1, y + 1.0f), {0, 0, 0}, 0, style_.vignette_edge, 110, false);
    const Rect line = box(x0 - 6.0f, y, x1, y + 1.4f);
    if (mirrored_) {
        fill_gradient(line, style_.underline[1], 255, style_.underline[0], 255, true);
    } else {
        fill_gradient(line, style_.underline[0], 255, style_.underline[1], 255, true);
    }
}

void Painter::edge_bands(int pixel_w, int pixel_h) {
    const float w = static_cast<float>(pixel_w), h = static_cast<float>(pixel_h);
    const float band = h * 0.14f;
    fill_gradient({0, 0, w, band}, style_.vignette_edge, 140, {0, 0, 0}, 0, false);
    fill_gradient({0, h - band, w, h}, {0, 0, 0}, 0, style_.vignette_edge, 140, false);
}

}  // namespace sh::screen_kit
