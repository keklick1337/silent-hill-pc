// SPDX-License-Identifier: GPL-3.0-or-later
// The ground morph of map6_s00 (engine/effects/ground_morph.h): its host over the camera's world-screen frame, the
// ground lighting of the atmosphere and the frame. The map script calls GroundMorph_Draw (sh.map.ground_morph_draw).

#include <cstdint>

#include "engine/loader/active_map.h"
#include "engine/camera/camera_host.h"
#include "engine/camera/camera_types.h"
#include "engine/camera/view_space.h"
#include "engine/effects/drawer_projection.h"
#include "engine/effects/effect_world.h"
#include "engine/effects/ground_morph.h"
#include "engine/game_state/session.h"
#include "engine/math/view_projection.h"
#include "engine/world/world_atmosphere.h"
#include "render/draw3d/draw3d.h"
#include "engine/app/loader/picture_places.h"

// The picture place the morph's ground texture shares with the load screen.

namespace {

namespace fx = sh::effects;

class MorphHost final : public fx::GroundMorphHost {
public:
    ~MorphHost() override {
        if (begun_) sh::math::end_game_view_projection(view_);
    }

    fx::Vec3 player_position() override {
        const auto& p = sh::game::session().system.characters.player.position;
        return {p.x, p.y, p.z};
    }

    std::int32_t begin_view(const fx::Vec3& origin) override {
        // A matrix record of its own, as the precise path looks its exact camera up by the record.
        const sh::camera::Matrix view = sh::camera::world_screen_at(sh::camera::camera_host().world_screen(),
                                                                    sh::camera::Vec3{origin.x, origin.y, origin.z});
        view_ = sh::math::game_view_projection();
        fx::drawer::set_matrix(view_, &view);
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

    fx::Rgb light(const fx::Vec3s& pos, const fx::Vec3s& normal, const fx::Vec3& offset) override {
        sh::math::Transform model;
        model.rot.m[0][0] = model.rot.m[1][1] = model.rot.m[2][2] = 4096;
        model.trans = {offset.x, offset.y, offset.z};
        const std::uint8_t amount = fx::effect_world::ground_light(pos, normal, model);
        const sh::world::Color8 c = sh::world::atmosphere().faded_tint(0, amount);
        return fx::Rgb{c.r, c.g, c.b};
    }

    std::uint16_t palette(int look) override {
        // The palette word: row in the high bits, column / 16 in the low six.
        const sh::loader::ImagePlace& place = sh::loader::picture_places().loading_screen;
        const int y = place.palette_y + look;
        return static_cast<std::uint16_t>((y << 6) | ((place.palette_x >> 4) & 0x3F));
    }

    void emit(const sh::draw3d::Polygon& polygon, std::int32_t bucket) override {
        sh::draw3d::submit_projected(polygon, view_.depths(),
                                     {sh::frame::Layer::World, static_cast<std::uint32_t>(bucket)});
    }

private:
    sh::math::ViewProjection view_;
    bool begun_ = false;
};

struct BandRecord {
    std::int16_t start, end;
};

}  // namespace

// The morph level of the map's script; the bands are the loaded map's table (tables.json "ground_morph_bands",
// [column][row]).
extern "C" void GroundMorph_Draw(std::int32_t level) {
    const auto* bands = static_cast<const BandRecord(*)[fx::kGroundMorphGrid]>(sh::loader::active_map_symbol("ground_morph_bands"));
    if (bands == nullptr) return;
    fx::GroundMorphTable table;
    for (int c = 0; c < fx::kGroundMorphGrid; c++) {
        for (int r = 0; r < fx::kGroundMorphGrid; r++) {
            table[static_cast<std::size_t>(c)][static_cast<std::size_t>(r)] = {bands[c][r].start, bands[c][r].end};
        }
    }
    MorphHost host;
    fx::GroundMorph::draw(host, table, level);
}
