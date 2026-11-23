// SPDX-License-Identifier: GPL-3.0-or-later
// The ending lake and sprites of map6_s02 (engine/effects/lake_surface.h) over the running game: the loaded map's
// registry records (gamedata/maps/map6_s02/tables.json, by name), the lake's view, the screen copy under the glow,
// the texture load and the page uploads. The lake surface is the effect overlay.json names "lake_surface"; the
// sprites are drawn by the map's script (sh.map.ufo_ending_sprite_draw / ufo_ending_slide_in_draw).

#include <array>
#include <cstdint>
#include <cstring>

#include "engine/loader/active_map.h"
#include "engine/maps/map_globals.h"
#include "engine/camera/camera_host.h"
#include "engine/camera/view_space.h"
#include "engine/effects/drawer_projection.h"
#include "engine/effects/effect_world.h"
#include "engine/effects/game/slot_hosts.h"
#include "engine/effects/lake_surface.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_world.h"
#include "engine/log/sh_log.h"
#include "engine/memory/work_buffers.h"
#include "engine/math/view_projection.h"
#include "engine/screens/screen_fade.h"
#include "engine/screens/screens_view.h"
#include "render/gpu/renderer.h"
#include "engine/loader/queue/disc_files.h"

extern "C" {
// The shake of the ending sprites the script sets (map_vars ending_jitter_x / _y).
std::int8_t g_EndingJitterX = 0;
std::int8_t g_EndingJitterY = 0;

// The water drawing's switch.
void Water_EffectEnable(void);
}


namespace {

namespace fx = sh::effects;
namespace hosts = sh::effects::slot_hosts;
using ImagePlace = fx::effect_world::ImagePlace;

// The buffer the textures pass through: both pages of the lake image, the second right behind the first; and where
// the ripples live in the map work buffer (after the lake's projection scratch).
constexpr sh::memory::WorkBuffer kFileBuffer = sh::memory::WorkBuffer::FileStaging;
constexpr std::size_t kPageWords = 0x2000;
constexpr std::size_t kRippleWordsOffset = 0x908;

// An entry of the map's ending sprite table: screen box and texel origin.
struct EndingSpriteRecord {
    std::int16_t x;
    std::int16_t y;
    std::uint8_t size[2];
    std::uint8_t texel[2];
};

// A rectangle of video memory as the registry keeps it.
struct VramRect {
    std::int16_t x, y, w, h;
};

std::uint32_t* ripple_words() {
    return sh::memory::work_buffers().as<std::uint32_t>(sh::memory::WorkBuffer::MapWork22, kRippleWordsOffset);
}

bool lake_pages(std::array<std::uint8_t, 8>* out) {
    const auto* pages = static_cast<const std::uint8_t*>(sh::loader::active_map_symbol("ending_sprite_pages"));
    if (pages == nullptr) return false;
    std::memcpy(out->data(), pages, out->size());
    return true;
}

// The pixel words of a TIM file (its header, the palette block when there is one, then the pixel block's header).
std::uint32_t* tim_pixels(void* data) {
    auto* w = static_cast<std::uint32_t*>(data);
    const std::uint32_t mode = w[1];
    w += 2;
    if (mode & 0x8) w += w[0] >> 2;
    return &w[3];
}

std::uint16_t palette_word(std::int16_t x, std::int16_t y) {
    return static_cast<std::uint16_t>((y << 6) | ((x >> 4) & 0x3F));
}

class LakeHostImpl final : public fx::LakeHost {
public:
    LakeHostImpl() : cursor_(GsOUT_PACKET_P) {}
    ~LakeHostImpl() override {
        GsOUT_PACKET_P = cursor_;
        if (begun_) sh::math::end_game_view_projection(view_);
    }

    std::int32_t rand16() override { return sh::loop::game_random().next15(); }
    std::int32_t delta_time() override { return sh::loop::frame_delta(); }

    std::int32_t begin_view() override {
        matrix_ = sh::camera::world_screen_at(sh::camera::camera_host().world_screen(),
                                              sh::camera::Vec3{0x3C000, 0, -0x14000});
        view_ = sh::math::game_view_projection();
        sh::effects::drawer::set_matrix(view_, &matrix_);
        begun_ = true;
        return view_.screen().h;
    }

    void project3(const fx::Vec3s v[3], fx::ScreenPoint out[3]) override {
        const auto r = view_.project3({sh::math::Vec3s{v[0].x, v[0].y, v[0].z}, sh::math::Vec3s{v[1].x, v[1].y, v[1].z},
                                       sh::math::Vec3s{v[2].x, v[2].y, v[2].z}});
        for (int k = 0; k < 3; k++) out[k] = fx::ScreenPoint{r[k].v.sx, r[k].v.sy, r[k].v.sz};
    }

    fx::ScreenPoint project(const fx::Vec3s& v) override {
        const sh::math::PreciseVertex r = view_.project({v.x, v.y, v.z});
        return fx::ScreenPoint{r.v.sx, r.v.sy, r.v.sz};
    }

    void screen_glow() override {
        int buffer = sh::game_state::frame_buffer_index();
        const std::int64_t fade = sh::screens::screen_fade().visible_progress();
        const std::int32_t level = static_cast<std::int32_t>(
            ((sh::game::session().game.settings.brightness * 8 + 4) * fade) >> 12);
        const sh::frame::OrderKey glow{sh::frame::Layer::Overlay, 3};

        // An add / subtract fill pair under the display buffer's page sets the contrast.
        for (int j = 0; j < 4; j++) {
            const int i = j & 1;
            sh::draw2d::Fill contrast;
            contrast.at = {static_cast<std::int16_t>((i << 8) - 0xA0), -112};
            contrast.w = i ? 64 : 256;
            contrast.h = kFrameHeight;
            contrast.color = {static_cast<std::uint8_t>(level), static_cast<std::uint8_t>(level),
                              static_cast<std::uint8_t>(level)};
            contrast.translucent = true;
            hosts::place_2d(glow, cursor_, contrast);
            cursor_ += hosts::packet::kTile;

            sh::draw2d::DrawMode page;
            page.page = sh::draw2d::TexturePage::from_word(hosts::display_page(j >= 2 ? 2 : 1, buffer, i));
            hosts::place_2d(glow, cursor_, page);
            cursor_ += hosts::packet::kDrawPage;
        }

        if (sh::game_state::event_flag(sh::game_state::flag_number("M6S02_CameraCutThisFrame"))) buffer ^= 1;

        // Then the frame on display drawn back over the scene, one translucent sprite per 256-pixel page.
        for (int i = 0; i < 2; i++) {
            sh::draw2d::Sprite copy;
            copy.at = {static_cast<std::int16_t>((i << 8) - 0xA0), -112};
            copy.w = i ? 64 : 256;
            copy.h = kFrameHeight;
            copy.uv = {0, static_cast<std::uint8_t>((buffer == 0) << 5)};
            copy.palette = hosts::stale_palette(cursor_);
            copy.color = {0x70, 0x70, 0x70};
            copy.shade = {true, false};
            hosts::place_2d(glow, cursor_, copy);
            cursor_ += hosts::packet::kSprite;

            sh::draw2d::DrawMode page;
            page.page = sh::draw2d::TexturePage::from_word(hosts::display_page(0, buffer, i));
            hosts::place_2d(glow, cursor_, page);
            cursor_ += hosts::packet::kDrawPage;
        }

        sh::draw2d::Mask mask;
        mask.set = true;
        hosts::place_2d({sh::frame::Layer::World, sh::frame::slot_count(sh::frame::Layer::World) - 1}, cursor_, mask);
        cursor_ += hosts::packet::kDrawMask;
        mask.set = false;
        hosts::place_2d({sh::frame::Layer::Overlay, 0}, cursor_, mask);
        cursor_ += hosts::packet::kDrawMask;
    }

    void emit(const sh::draw3d::Polygon& polygon, std::int32_t bucket) override {
        // Before begin_view the depths are the ones the geometry state holds.
        sh::draw3d::submit_projected(polygon, begun_ ? view_.depths() : sh::math::game_view_projection().depths(),
                                     {sh::frame::Layer::World, static_cast<std::uint32_t>(bucket)});
    }

    void upload_page(std::int32_t page, const std::uint32_t* words) override {
        if (sh::gpu::Renderer* r = sh::gpu::renderer()) {
            r->load_image({static_cast<std::int16_t>((page & 0xF) << 6), static_cast<std::int16_t>((page * 16) & 0x100),
                           64, 256},
                          reinterpret_cast<const std::uint16_t*>(words));
        }
    }

private:
    static constexpr std::uint16_t kFrameHeight = 224;

    unsigned char* cursor_;
    sh::camera::Matrix matrix_;
    sh::math::ViewProjection view_;
    bool begun_ = false;
};

bool ending_look(fx::EndingLook* look) {
    const auto* images = static_cast<const ImagePlace*>(sh::loader::active_map_symbol("ufo_ending_images"));
    if (images == nullptr) return false;
    look->jitter_x = g_EndingJitterX;
    look->jitter_y = g_EndingJitterY;
    look->palette = palette_word(images[4].palette_x, images[4].palette_y);
    // The sprites' page byte lies 33 bytes into the first image's page pair (the original's index past it).
    look->sprite_page = reinterpret_cast<const std::uint8_t*>(&images[0].page[0])[33];
    look->title_page = images[4].page[1];
    return true;
}

// The sprites are not projected: they take the depths of the projections made before them.
void link_flat(const sh::draw3d::Polygon& p) {
    sh::draw3d::submit_projected(p, sh::math::game_view_projection().depths(), {sh::frame::Layer::World, 0});
}

}  // namespace

extern "C" {

void UfoEnding_SpriteDraw(std::int32_t idx) {
    fx::EndingLook look;
    const auto* sprites = static_cast<const EndingSpriteRecord*>(sh::loader::active_map_symbol("ending_sprites"));
    if (sprites == nullptr || !ending_look(&look)) return;
    const EndingSpriteRecord& r = sprites[idx];
    const fx::EndingSprite s{r.x, r.y, r.size[0], r.size[1], r.texel[0], r.texel[1]};
    link_flat(fx::UfoEnding::sprite(s, idx, look));
}

void UfoEnding_SlideInDraw(std::int16_t progress) {
    fx::EndingLook look;
    if (!ending_look(&look)) return;
    for (const sh::draw3d::Polygon& p : fx::UfoEnding::slide_in(progress, look)) link_flat(p);
}

// 0 loads LHEFFECT.TIM and builds the textures, 1 rebuilds them from the video memory copy.
void LakeSurface_TexturesInit(std::int32_t rebuild) {
    auto* image = static_cast<ImagePlace*>(sh::loader::active_map_symbol("ending_lake_image"));
    const auto* stored = static_cast<const VramRect*>(sh::loader::active_map_symbol("ending_lake_rect"));
    std::array<std::uint8_t, 8> pages;
    if (image == nullptr || stored == nullptr || !lake_pages(&pages)) return;
    std::uint32_t* buffers[2];
    int first;
    if (!rebuild) {
        Water_EffectEnable();
        sh::loader::queue_read_image(sh::loader::disc_files().find("TIM/LHEFFECT.TIM"), sh::memory::work_buffers().data(kFileBuffer), image);
        sh::loader::queue_wait_empty();
        buffers[0] = tim_pixels(sh::memory::work_buffers().data(kFileBuffer));
        buffers[1] = buffers[0] + kPageWords;
        first = 0;
    } else {
        std::memset(ripple_words(), 0, fx::kLakeRippleWords * sizeof(std::uint32_t));
        const std::size_t stored_bytes = static_cast<std::size_t>(stored->w) * static_cast<std::size_t>(stored->h) * 2;
        if (stored_bytes > sh::memory::WorkBuffers::capacity(kFileBuffer)) {
            SH_ERR("[LAKE] the stored lake image (%zu bytes) does not fit its buffer", stored_bytes);
            return;
        }
        if (sh::gpu::Renderer* r = sh::gpu::renderer()) {
            r->store_image({stored->x, stored->y, stored->w, stored->h}, sh::memory::work_buffers().as<std::uint16_t>(kFileBuffer));
        }
        buffers[0] = sh::memory::work_buffers().as<std::uint32_t>(kFileBuffer);
        buffers[1] = buffers[0] + kPageWords;
        first = 2;
    }
    LakeHostImpl host;
    fx::LakeSurface::build_pages(host, pages, first, buffers);
    fx::LakeSurface::seed(host, ripple_words());
}

void LakeSurface_Draw(void) {
    std::array<std::uint8_t, 8> pages;
    if (!lake_pages(&pages)) return;
    LakeHostImpl host;
    fx::LakeSurface::draw(host, ripple_words(), pages);
}

// The texture set-up only on a map whose overlay.json declares the lake surface; 0 elsewhere.
int LakeSurface_TexturesInitIfDeclared(std::int32_t rebuild) {
    if (!sh::maps::map_runtime().draws(sh::maps::MapEffect::LakeSurface)) return 0;
    LakeSurface_TexturesInit(rebuild);
    return 1;
}

}  // extern "C"

namespace {

// The ending's map scripts write the jitter by name (map_vars "ending_jitter_x/y").
[[maybe_unused]] const bool g_map_globals_bound = [] {
    sh::maps::map_globals().bind("ending_jitter_x", &g_EndingJitterX);
    sh::maps::map_globals().bind("ending_jitter_y", &g_EndingJitterY);
    return true;
}();

}  // namespace
