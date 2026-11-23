// UI model previews (render/scene/preview.h): the depth reset covers the rectangle, the model fits inside it, and
// the turn moves it.

#include "framework/sh_test.h"
#include "render/scene/preview.h"
#include "render/scene/scene.h"

#include <cmath>

namespace {

namespace sc = sh::scene;

// A box 100 wide, 400 tall and 50 deep, its front face (towards +z, the model's own front) red.
sc::PreviewModel box_model() {
    sc::PreviewModel m;
    for (int i = 0; i < 8; ++i) {
        m.positions.push_back({(i & 1) ? 50.0f : -50.0f, (i & 2) ? 200.0f : -200.0f, (i & 4) ? 25.0f : -25.0f});
        m.normals.push_back({0, 0, (i & 4) ? 1.0f : -1.0f});
        m.uvs.push_back({0, 0});
    }
    sc::PreviewModel::Group g;
    g.indices = {0, 1, 2, 1, 3, 2, 4, 5, 6, 5, 7, 6};
    m.groups.push_back(g);
    m.radius_xz = std::sqrt(50.0f * 50.0f + 25.0f * 25.0f);
    m.half_height = 200;
    return m;
}

struct Projected {
    float x, y;
};

Projected project(const sc::PrelitMesh& m, const sc::PrelitVertex& v) {
    return {m.geom_ofs_x + m.h * v.x / v.z, m.geom_ofs_y + m.h * v.y / v.z};
}

}  // namespace

SH_TEST(scene_preview_layout) {
    sc::current_frame().prelit.clear();
    const sc::PreviewModel model = box_model();
    const sc::PreviewPlacement where{40, -60, 80, 120, 1};
    const std::vector<int> items = sc::add_preview(model, where, 0.0f);
    SH_CHECK(items.size() == 2);
    const auto& prelit = sc::current_frame().prelit;
    SH_CHECK(prelit.size() == 2);

    const sc::PrelitMesh& quad = prelit[static_cast<std::size_t>(items[0])];
    SH_CHECK(quad.isolated && quad.depth_reset);
    float zmax = 0;
    for (const sc::PrelitVertex& v : quad.vertices) {
        const Projected p = project(quad, v);
        SH_CHECK(std::fabs(p.x - 40) < 0.01f || std::fabs(p.x - 120) < 0.01f);
        SH_CHECK(std::fabs(p.y + 60) < 0.01f || std::fabs(p.y - 60) < 0.01f);
        zmax = v.z;
    }

    const sc::PrelitMesh& body = prelit[static_cast<std::size_t>(items[1])];
    SH_CHECK(body.isolated && !body.depth_reset && !body.casts_shadow);
    SH_CHECK(body.vertices.size() == 8);
    for (const sc::PrelitVertex& v : body.vertices) {
        const Projected p = project(body, v);
        SH_CHECK(p.x > 40 && p.x < 120);
        SH_CHECK(p.y > -60 && p.y < 60);
        SH_CHECK(v.z > 8 && v.z < zmax);  // past the near plane, in front of the depth reset
    }
    // Facing the viewer at yaw 0: the model's front face is the nearer one, and lit brighter.
    float front_z = 0, back_z = 0;
    int front_r = 0, back_r = 0;
    for (std::size_t i = 0; i < body.vertices.size(); ++i) {
        if (model.normals[i][2] > 0) {
            front_z += body.vertices[i].z;
            front_r += body.vertices[i].r;
        } else {
            back_z += body.vertices[i].z;
            back_r += body.vertices[i].r;
        }
    }
    SH_CHECK(front_z < back_z);
    SH_CHECK(front_r > back_r);
    sc::current_frame().prelit.clear();
}

SH_TEST(scene_preview_turns) {
    sc::current_frame().prelit.clear();
    const sc::PreviewModel model = box_model();
    const sc::PreviewPlacement where{0, 0, 100, 100, 1};
    const std::vector<int> a = sc::add_preview(model, where, 0.0f);
    const std::vector<int> b = sc::add_preview(model, where, 1.0f);
    const auto& prelit = sc::current_frame().prelit;
    const sc::PrelitVertex& va = prelit[static_cast<std::size_t>(a[1])].vertices[0];
    const sc::PrelitVertex& vb = prelit[static_cast<std::size_t>(b[1])].vertices[0];
    SH_CHECK(std::fabs(va.x - vb.x) > 1.0f || std::fabs(va.z - vb.z) > 1.0f);
    SH_CHECK(va.y == vb.y);  // about the vertical axis
    sc::current_frame().prelit.clear();
}
