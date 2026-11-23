// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh.render.* binds: screen effects an event script asks for by what they look like, as
// engine 2D and 3D commands placed in the engine frame by order key (render/frame). Registered through
// ShScriptGameBinderEntry (order 120, sh_script.h); the screen binds are in game_script_api.cpp.
//
// Layers: world = world slot 2 (met after the world; the full-screen background picture shares it), picture = world
// slot 1, overlay = overlay slot 15, screen = overlay slot 3 (after the overlay). Within one slot the command placed
// last is drawn first, so each sprite's draw mode is placed after it.
//
// Frames in video memory: the double buffer is 320x224 at y 32 and y 256; buffer 0 draws into y 256 and shows y 32,
// buffer 1 the other way round. The held picture is the spare 320x224 at (320, 256), right of the y-256 buffer.
//
// frame_hold: the source frame drawn over the whole screen as two 15-bit direct sprites (a texture page is 256 px
// wide: 256 + 64), each followed by its draw mode (blend), at the layer's slot. With layer overlay a mask pair goes
// with it: "set mask bit" at the far end of the world and off again at the start of the overlay, so the world's
// pixels carry the bit the semi-transparent sampling of the frame needs.
//
// world_redirect: the layers' sprites (held picture as source; the last layer placed first, so the first is drawn
// first), then a draw-area pair and a mask pair; the second of each pair at the far end of the world moves the
// world's drawing into the held picture (mask on), the first at slot 2 moves it back into the draw buffer (mask off)
// before the layers come onto the screen. The frame turns the offscreen area into a scene redirect and the area back
// into a capture of the held rect (render/gpu/frame_draw.h, resolve_scene_captures).
//
// held_capture, all at world slot 2: draw area and mask (off) of the draw buffer, the current frame's two opaque
// sprites, then the draw area and mask (on) of the held picture. Drawn in reverse, the copy goes into the held
// picture with the mask bit set, and the draw buffer is restored for what follows.
//
// tile: one fill, then a draw mode that carries only its blend; its page is the shown frame's page under the tile's
// left edge, as the original code set it (an untextured fill never samples it). A raw tpage replaces that word and
// dither sets the mode's dither bit.
//
// sprite: one sprite per rectangle, then one draw mode, all at the picture slot, so the mode is drawn first. Texture
// background is the inspection picture: its page, row and palette come from where the loader put it; a texel column
// past the first page moves to the next page. Only layer picture exists for sprites.

#include "engine/math/precise_capture.h"
#include "engine/scripting/render_binds.h"

#include <cstdint>
#include <cstdio>

#include "engine/app/game_view.h"
#include "engine/app/loader/game_files.h"
#include "engine/memory/work_buffers.h"
#include "engine/maps/map_runtime.h"
#include "engine/math/geometry_state.h"
#include "engine/math/precise_projection.h"
#include "engine/runtime/world_object_runtime.h"
#include "engine/screens/screen_fade.h"
#include "engine/screens/screens_view.h"
#include "engine/script/sh_script.h"
#include "render/draw2d/draw2d.h"
#include "render/draw3d/draw3d.h"
#include "render/frame/frame.h"
#include "render/gpu/render_state.h"
#include "render/gpu/renderer.h"
#include "engine/app/loader/picture_places.h"

namespace {

namespace d2 = sh::draw2d;
namespace fr = sh::frame;

constexpr std::uint32_t kWorldSlot = 2;
constexpr std::uint32_t kPictureSlot = 1;
constexpr std::uint32_t kOverlaySlot = 15;
constexpr std::uint32_t kScreenSlot = 3;
constexpr std::uint32_t kWorldSlots = fr::slot_count(fr::Layer::World);
constexpr fr::OrderKey kWorldFarEnd{fr::Layer::World, kWorldSlots - 1};
constexpr fr::OrderKey kOverlayStart{fr::Layer::Overlay, 0};

constexpr int kScreenLeft = -160;
constexpr int kScreenTop = -112;
constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 224;
constexpr int kPageWidth = 256;
constexpr int kDirect15 = 2;  // texture page colour mode: 15-bit direct
constexpr int kFrameTopA = 32;
constexpr int kFrameTopB = 256;
constexpr int kHeldX = 320;
constexpr int kHeldY = 256;

// Where the texture loads stage a picture file before it is uploaded.
constexpr sh::memory::WorkBuffer kStagingFile = sh::memory::WorkBuffer::FileStaging;
constexpr sh::memory::WorkBuffer kStagingImage = sh::memory::WorkBuffer::EventImage;
constexpr sh::memory::WorkBuffer kStagingPaperMap = sh::memory::WorkBuffer::PaperMapSheet;

enum Source { kPrevious = 0, kHeld = 1, kCurrent = 2 };

int refuse(char* err, int errCap, const char* why) {
    if (err != nullptr && errCap > 0) {
        std::snprintf(err, static_cast<std::size_t>(errCap), "%s", why);
    }
    return -1;
}

// The texture page and palette words of the GPU's draw-mode and palette fields.
std::uint16_t page_word(int colours, int blend, int x, int y) {
    return static_cast<std::uint16_t>(((colours & 3) << 7) | ((blend & 3) << 5) | ((y & 0x100) >> 4) |
                                      ((x & 0x3FF) >> 6) | ((y & 0x200) << 2));
}

std::uint16_t palette_word(int x, int y) { return static_cast<std::uint16_t>((y << 6) | ((x >> 4) & 0x3F)); }

int buffer() { return fr::build_buffer(); }

// VRAM top row of the frame the GPU draws into now / showed last.
int current_top() { return buffer() == 0 ? kFrameTopB : kFrameTopA; }
int previous_top() { return buffer() == 0 ? kFrameTopA : kFrameTopB; }

bool layer_key(int layer, fr::OrderKey* key) {
    switch (layer) {
        case 0: *key = {fr::Layer::World, kWorldSlot}; return true;
        case 1: *key = {fr::Layer::Overlay, kOverlaySlot}; return true;
        case 2: *key = {fr::Layer::World, kPictureSlot}; return true;
        case 3: *key = {fr::Layer::Overlay, kScreenSlot}; return true;
        default: return false;
    }
}

void place(fr::OrderKey key, const d2::Command& command) { d2::submit(command, key); }

void place_mask(fr::OrderKey key, bool set) {
    d2::Mask mask;
    mask.set = set;
    place(key, mask);
}

void place_area(fr::OrderKey key, int x, int y) {
    place(key, d2::DrawArea{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), kScreenWidth, kScreenHeight});
}

// Two sprites covering the screen from a 320x224 frame at VRAM (srcX, srcY), each followed by its draw mode. The
// frame's pages are direct colour, so the sprites name no palette.
void place_frame(fr::OrderKey key, int srcX, int srcY, const int tint[3], int blend, int dx, int dy) {
    const int abr = blend >= 0 ? (blend & 3) : 0;
    for (int strip = 0; strip < 2; ++strip) {
        d2::Sprite sprite;
        sprite.color = {static_cast<std::uint8_t>(tint[0]), static_cast<std::uint8_t>(tint[1]),
                        static_cast<std::uint8_t>(tint[2])};
        sprite.shade.translucent = blend >= 0;
        sprite.at = {static_cast<std::int16_t>(kScreenLeft + strip * kPageWidth + dx),
                     static_cast<std::int16_t>(kScreenTop + dy)};
        sprite.uv = {0, static_cast<std::uint8_t>(srcY & 0xFF)};
        sprite.w = static_cast<std::uint16_t>(strip == 0 ? kPageWidth : kScreenWidth - kPageWidth);
        sprite.h = kScreenHeight;
        place(key, sprite);

        d2::DrawMode mode;
        mode.page = d2::TexturePage::from_word(page_word(kDirect15, abr, srcX + strip * kPageWidth, srcY & 0x100));
        place(key, mode);
    }
}

void source_origin(int source, int* x, int* y) {
    *x = source == kHeld ? kHeldX : 0;
    *y = source == kHeld ? kHeldY : (source == kCurrent ? current_top() : previous_top());
}

int frame_hold(const ShRenderFrameHold* spec, char* err, int errCap) {
    if (spec == nullptr) {
        return refuse(err, errCap, "no request");
    }
    if (spec->source < kPrevious || spec->source > kCurrent || spec->blend < -1 || spec->blend > 3) {
        return refuse(err, errCap, "bad source or blend");
    }
    if (spec->layer != 0 && spec->layer != 1) {
        return refuse(err, errCap, "frame_hold draws on layer world or overlay");
    }
    int srcX = 0;
    int srcY = 0;
    source_origin(spec->source, &srcX, &srcY);
    fr::OrderKey key;
    layer_key(spec->layer, &key);
    place_frame(key, srcX, srcY, spec->tint, spec->blend, spec->offset[0], spec->offset[1]);
    if (spec->layer == 1) {
        place_mask(kWorldFarEnd, true);
        place_mask(kOverlayStart, false);
    }
    return 0;
}

int held_clear(char*, int) {
    if (sh::gpu::Renderer* r = sh::gpu::renderer()) {
        r->clear(sh::gpu::Rect{kHeldX, kHeldY, kScreenWidth, kScreenHeight}, 0, 0, 0);
    }
    return 0;
}

// The original also moves the draw offset to the centre of each area; the display environment places the picture,
// so no offset change is placed.
int held_capture(char*, int) {
    const fr::OrderKey key{fr::Layer::World, kWorldSlot};
    place_area(key, 0, current_top());
    place_mask(key, false);
    static const int kUnchanged[3] = {128, 128, 128};
    int srcX = 0;
    int srcY = 0;
    source_origin(kCurrent, &srcX, &srcY);
    place_frame(key, srcX, srcY, kUnchanged, -1, 0, 0);
    place_area(key, kHeldX, kHeldY);
    place_mask(key, true);
    return 0;
}

int world_redirect(const ShRenderLayers* spec, char* err, int errCap) {
    if (spec == nullptr || spec->count < 1 || spec->items == nullptr) {
        return refuse(err, errCap, "no layers");
    }
    for (int i = 0; i < spec->count; ++i) {
        if (spec->items[i].blend < -1 || spec->items[i].blend > 3) {
            return refuse(err, errCap, "bad blend");
        }
    }
    const fr::OrderKey key{fr::Layer::World, kWorldSlot};
    for (int i = spec->count - 1; i >= 0; --i) {
        const ShRenderLayer& layer = spec->items[i];
        place_frame(key, kHeldX, kHeldY, layer.tint, layer.blend, layer.offset[0], layer.offset[1]);
    }
    place_area(key, 0, current_top());
    place_area(kWorldFarEnd, kHeldX, kHeldY);
    place_mask(key, false);
    place_mask(kWorldFarEnd, true);
    return 0;
}

int tile(const ShRenderTile* spec, char* err, int errCap) {
    if (spec == nullptr) {
        return refuse(err, errCap, "no request");
    }
    if (spec->blend < -1 || spec->blend > 3) {
        return refuse(err, errCap, "bad blend");
    }
    fr::OrderKey key;
    if (!layer_key(spec->layer, &key)) {
        return refuse(err, errCap, "bad layer");
    }
    d2::Fill fill;
    fill.color = {static_cast<std::uint8_t>(spec->colour[0]), static_cast<std::uint8_t>(spec->colour[1]),
                  static_cast<std::uint8_t>(spec->colour[2])};
    fill.translucent = spec->blend >= 0;
    fill.at = {static_cast<std::int16_t>(spec->x), static_cast<std::int16_t>(spec->y)};
    fill.w = static_cast<std::uint16_t>(spec->w);
    fill.h = static_cast<std::uint16_t>(spec->h);
    place(key, fill);

    d2::DrawMode mode;
    mode.dither = spec->dither != 0;
    if (spec->tpage >= 0) {
        mode.page = d2::TexturePage::from_word(static_cast<std::uint16_t>(spec->tpage));
    } else {
        const int pageX = ((spec->x - kScreenLeft) & ~63) & 0x3FF;
        mode.page = d2::TexturePage::from_word(
            page_word(kDirect15, spec->blend >= 0 ? (spec->blend & 3) : 0, pageX, previous_top() & 0x100));
    }
    place(key, mode);
    return 0;
}

int fade_in_level() { return sh::screens::screen_fade().visible_progress(); }

// Texels one 64-halfword VRAM page holds per row: 4-bit 256, 8-bit 128, 15-bit 64.
int texels_per_page(int colourMode) { return 64 << (2 - colourMode); }

d2::Rgb rgb_of(const int c[3]) {
    return {static_cast<std::uint8_t>(c[0]), static_cast<std::uint8_t>(c[1]), static_cast<std::uint8_t>(c[2])};
}

int sprites(const ShRenderSprites* spec, char* err, int errCap) {
    if (spec == nullptr || spec->count < 0 || (spec->count > 0 && spec->items == nullptr)) {
        return refuse(err, errCap, "no request");
    }
    if (spec->texture != 0) {
        return refuse(err, errCap, "unknown texture");
    }
    if (spec->layer != 2) {
        return refuse(err, errCap, "only layer 'picture' is supported for sprites");
    }
    const fr::OrderKey key{fr::Layer::World, kPictureSlot};

    // Raw sprites carry their own draw mode and palette: each is followed by its own draw mode.
    int raw = 0;
    for (int i = 0; i < spec->count; ++i) {
        raw += spec->items[i].tpage >= 0 ? 1 : 0;
    }
    if (raw > 0) {
        if (raw != spec->count) {
            return refuse(err, errCap, "mix of raw (tpage) and texture sprites in one call");
        }
        for (int i = 0; i < spec->count; ++i) {
            const ShRenderSpriteRect& r = spec->items[i];
            const int* tint = r.tint[0] >= 0 ? r.tint : spec->tint;
            const int blend = r.blend != -2 ? r.blend : spec->blend;
            d2::Sprite sprite;
            sprite.color = rgb_of(tint);
            sprite.shade.translucent = blend >= 0;
            sprite.at = {static_cast<std::int16_t>(r.x), static_cast<std::int16_t>(r.y)};
            sprite.uv = {static_cast<std::uint8_t>(r.u), static_cast<std::uint8_t>(r.v)};
            sprite.w = static_cast<std::uint16_t>(r.w);
            sprite.h = static_cast<std::uint16_t>(r.h);
            sprite.palette = d2::Palette::from_word(static_cast<std::uint16_t>(r.clut));
            place(key, sprite);

            d2::DrawMode mode;
            mode.page = d2::TexturePage::from_word(static_cast<std::uint16_t>(r.tpage));
            place(key, mode);
        }
        return 0;
    }

    const sh::loader::ImagePlace& image = *sh::app::view::picture(sh::app::view::Picture::ItemInspection);
    const int colourMode = image.page[0] & 3;
    if (colourMode == 3) {
        return refuse(err, errCap, "the background texture is not loaded");
    }
    const int perPage = texels_per_page(colourMode);
    const int firstPage = image.page[1] & 0xF;
    // All sprites share one draw mode, so they must all sit on the first one's page.
    int page = -1;
    for (int i = 0; i < spec->count; ++i) {
        const ShRenderSpriteRect& r = spec->items[i];
        const int itemPage = firstPage + r.u / perPage;
        const int u = r.u % perPage;
        const int v = image.v + r.v;
        if (itemPage > 15 || u + r.w > 256 || v + r.h > 256) {
            return refuse(err, errCap, "a sprite leaves its texture page");
        }
        if (page >= 0 && itemPage != page) {
            return refuse(err, errCap, "the sprites of one call must share a texture page");
        }
        page = itemPage;
    }
    if (page < 0) {
        page = firstPage + spec->u / perPage;
        if (page > 15) {
            return refuse(err, errCap, "uv leaves the texture");
        }
    }

    for (int i = 0; i < spec->count; ++i) {
        const ShRenderSpriteRect& r = spec->items[i];
        d2::Sprite sprite;
        sprite.color = rgb_of(spec->tint);
        sprite.shade.translucent = spec->blend >= 0;
        sprite.at = {static_cast<std::int16_t>(r.x), static_cast<std::int16_t>(r.y)};
        sprite.uv = {static_cast<std::uint8_t>(r.u % perPage), static_cast<std::uint8_t>(image.v + r.v)};
        sprite.w = static_cast<std::uint16_t>(r.w);
        sprite.h = static_cast<std::uint16_t>(r.h);
        sprite.palette = d2::Palette::from_word(palette_word(image.palette_x, image.palette_y));
        place(key, sprite);
    }

    // Page row: bit 4 of the place's page byte (VRAM y 256).
    d2::DrawMode mode;
    mode.page = d2::TexturePage::from_word(
        page_word(colourMode, spec->blend >= 0 ? spec->blend : 0, page << 6, (image.page[1] << 4) & 0x100));
    place(key, mode);
    return 0;
}

// texture_load: the TIM goes through the file queue into the staging buffer, and the queue uploads it where the
// texture's place says.
int texture_load(const ShRenderTextureLoad* spec, char* err, int errCap) {
    if (spec == nullptr) {
        return refuse(err, errCap, "no request");
    }
    const sh::loader::ImagePlace* image = nullptr;
    switch (spec->texture) {
        case 0: image = sh::app::view::picture(sh::app::view::Picture::ItemInspection); break;
        case 1: image = &sh::loader::picture_places().paper_map; break;
        default: return refuse(err, errCap, "unknown texture");
    }
    sh::memory::WorkBuffer staging = kStagingFile;
    switch (spec->buffer) {
        case 0: staging = kStagingFile; break;
        case 1: staging = kStagingImage; break;
        case 2: staging = kStagingPaperMap; break;
        default: return refuse(err, errCap, "unknown buffer");
    }
    sh::loader::queue_read_image(spec->file, sh::memory::work_buffers().data(staging), image);
    return 0;
}

// The effects view while an effect-slot draw hook runs: the pass has loaded its camera into the transform unit and
// keeps the view origin (the player's whole-metre position, Q8).
struct EffectsView {
    bool active = false;
    int origin[3] = {};
};
EffectsView g_effects_view;

// The projection the effects pass left in the geometry state, with the exact camera the precise projection pairs with it.
struct ViewSetup {
    sh::math::Projection projection;
    sh::math::ExactTransform camera;
    sh::math::PreciseOptions options;
};

ViewSetup read_view_setup() {
    ViewSetup s;
    s.projection = sh::math::geometry().projection();
    double rot[9];
    double trans[3];
    s.camera.has_rotation = sh::math::capture::current_rotation(rot);
    s.camera.has_translation = sh::math::capture::current_translation(trans);
    for (int i = 0; s.camera.has_rotation && i < 9; ++i) {
        s.camera.rot[i] = rot[i];
    }
    for (int i = 0; s.camera.has_translation && i < 3; ++i) {
        s.camera.trans[i] = trans[i];
    }
    s.options.unquantized_depth = sh::render::precise().unquantized_depth != 0;
    s.options.whole_map_far = sh::render::whole_town().reproject_far != 0;
    return s;
}

int abs_of(int v) { return v < 0 ? -v : v; }

// world_quads: each point relative to the view origin in geometry units (Q12 -> Q8, cut to 16 bits) through the
// effects camera; one textured quad per quad that passes its tests, at the world slot of its depth. A corner that
// keeps its sub-pixel projection carries its precise and view-space position; the quad's pixels are ordered by the
// depths of its sort points.
int world_quads(const ShRenderWorldQuads* spec, char* err, int errCap) {
    namespace sg = sh::math;
    if (spec == nullptr) {
        return refuse(err, errCap, "no request");
    }
    if (!g_effects_view.active) {
        return refuse(err, errCap, "world quads are drawn from an effect-slot draw hook (effect_slot_draw)");
    }
    if (spec->point_count < 0 || spec->point_count > 256 || spec->quad_count < 0 || spec->blend < -1 ||
        spec->blend > 3 || spec->colours < 0 || spec->colours > 2) {
        return refuse(err, errCap, "bad request");
    }

    const ViewSetup setup = read_view_setup();
    sg::PreciseVertex projected[256];
    for (int i = 0; i < spec->point_count; ++i) {
        const int* p = &spec->points[i * 3];
        const sg::Vec3s v{static_cast<std::int16_t>((p[0] >> 4) - g_effects_view.origin[0]),
                          static_cast<std::int16_t>((p[1] >> 4) - g_effects_view.origin[1]),
                          static_cast<std::int16_t>((p[2] >> 4) - g_effects_view.origin[2])};
        projected[i] = sg::project_precise(setup.projection, v, nullptr, &setup.camera, setup.options);
    }

    const std::uint16_t tpage = page_word(spec->colours, spec->blend >= 0 ? spec->blend : 0, spec->page_x, spec->page_y);
    const std::uint16_t clut = palette_word(spec->palette_x, spec->palette_y);
    const bool tracked = sh::render::precise().enabled || sh::render::flashlight().per_pixel;
    const bool view_space = tracked && sh::render::view_space_wanted();
    int drawn = 0;

    for (int i = 0; i < spec->quad_count; ++i) {
        const ShRenderWorldQuad& q = spec->quads[i];
        const sg::PreciseVertex* c[4];
        bool valid = true;
        for (int k = 0; k < 4; ++k) {
            valid = valid && q.corners[k] >= 0 && q.corners[k] < spec->point_count && q.sort_points[k] >= 0 &&
                    q.sort_points[k] < spec->point_count;
            c[k] = valid ? &projected[q.corners[k]] : nullptr;
        }
        if (!valid) {
            return refuse(err, errCap, "a quad names a point that is not in the list");
        }
        const int average = (c[0]->v.sz + c[1]->v.sz + c[2]->v.sz + c[3]->v.sz) >> 2;
        const int tested = q.depth_test < 0 ? average : c[q.depth_test & 3]->v.sz;
        if (tested <= 0 || (tested >> 3) >= static_cast<int>(kWorldSlots)) {
            continue;
        }
        bool inside = true;
        for (int k = 0; k < 4; ++k) {
            if (q.screen_test < 0 || q.screen_test == k) {
                inside = inside && abs_of(c[k]->v.sx) <= spec->clip_x && abs_of(c[k]->v.sy) <= spec->clip_y;
            }
        }
        if (!inside) {
            continue;
        }
        // The slot past the depth test can still be past the layer's end (the original code linked such a quad
        // beyond its table); it is skipped here.
        const int slot = (average + spec->depth_bias) >> 3;
        if (slot < 0 || slot >= static_cast<int>(kWorldSlots)) {
            continue;
        }

        sh::draw3d::Polygon out;
        out.count = 4;
        out.textured = true;
        out.shade = {spec->blend >= 0, false};
        out.page = sh::draw3d::TexturePage::from_word(tpage);
        out.palette = sh::draw3d::Palette::from_word(clut);
        // Colour and texel pairs are packed as the GPU words were, so an out-of-range value carries the same.
        const std::uint16_t rg = static_cast<std::uint16_t>(q.tint[0] + (q.tint[1] << 8));
        out.v[0].color = {static_cast<std::uint8_t>(rg), static_cast<std::uint8_t>(rg >> 8),
                          static_cast<std::uint8_t>(q.tint[2])};
        for (int k = 0; k < 4; ++k) {
            sh::draw3d::Vertex& v = out.v[k];
            const std::uint16_t st = static_cast<std::uint16_t>(q.uv[k * 2] + (q.uv[k * 2 + 1] << 8));
            v.xy = {c[k]->v.sx, c[k]->v.sy};
            v.uv = {static_cast<std::uint8_t>(st), static_cast<std::uint8_t>(st >> 8)};
            if (tracked && (q.precise & (1 << k))) {
                const sg::PreciseVertex& r = *c[k];
                if (sh::render::precise().enabled) {
                    v.precise = sh::draw3d::Precise{r.sx, r.sy, r.w};
                }
                if (view_space) {
                    v.view = sh::draw3d::ViewPosition{r.view_x, r.view_y, r.view_z, !fr::state().no_shadow_cast,
                                                      fr::state().chara_fade, r.ofx, r.ofy, r.h};
                }
            }
        }
        // The quad's pixels are ordered by its sort points' depths; they are also left as the transform unit's
        // newest depths, which the drawer after this one continues from.
        sh::draw3d::Depth depth;
        std::uint32_t depth_max = 0;
        for (int k = 0; k < 4; ++k) {
            const std::uint16_t z = static_cast<std::uint16_t>(projected[q.sort_points[k]].v.sz);
            depth.sz[static_cast<std::size_t>(k)] = z;
            depth_max = z > depth_max ? z : depth_max;
            sh::math::geometry().set_depth(k, z);
        }
        out.depth = depth;
        sh::draw3d::submit(out, {fr::Layer::World, static_cast<std::uint32_t>(slot)}, depth_max);
        ++drawn;
    }
    return drawn;
}

void bind_render(ShScriptGameApi* api) {
    api->render_frame_hold = frame_hold;
    api->render_sprites = sprites;
    api->render_texture_load = texture_load;
    api->render_held_clear = held_clear;
    api->render_held_capture = held_capture;
    api->render_world_redirect = world_redirect;
    api->render_tile = tile;
    api->render_fade_in_level = fade_in_level;
    api->render_world_quads = world_quads;
}

const ShScriptGameBinderEntry g_render_binder(120, bind_render);

}  // namespace

namespace sh::script {

void draw_effect_slot_script(int slot, std::int32_t origin_x, std::int32_t origin_y, std::int32_t origin_z) {
    g_effects_view.active = true;
    g_effects_view.origin[0] = origin_x;
    g_effects_view.origin[1] = origin_y;
    g_effects_view.origin[2] = origin_z;
    sh::world::world_object_runtime().call("effect_slot_draw", slot,
                                           sh::maps::map_runtime().effect_slots()[slot].kind, 0);
    g_effects_view.active = false;
}

}  // namespace sh::script
