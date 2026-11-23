// The menus' selection widgets (src/engine/screens/menu_widgets.h): records, colors
// and the arrow flash ramp, worked out by hand from the specification.

#include "engine/screens/menu_widgets.h"

#include "framework/sh_test.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace sc = sh::screens;

class Recorder final : public sc::MenuSink {
public:
    std::vector<sc::MenuPrim> prims;
    void prim(const sc::MenuPrim& r) override { prims.push_back(r); }
};

sc::MenuStyle shipped() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/menu/menu_style.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto s = sc::parse_menu_style(text);
    return s ? s.value() : sc::MenuStyle{};
}

}  // namespace

SH_TEST(menu_widgets_shipped_style_parses) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/menu/menu_style.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto s = sc::parse_menu_style(text);
    SH_CHECK(s);
    SH_CHECK(s.value().underline[0] == 0xB0B0B0 && s.value().underline[1] == 0x4080A0);
    SH_CHECK(s.value().shadow[1] == 0x606060 && s.value().arrow_base == 0xFF0000);
    SH_CHECK(!sc::parse_menu_style("{\"underline\": {}}"));
}

SH_TEST(menu_widgets_underline_with_shadow) {
    const sc::MenuStyle style = shipped();
    sc::MenuWidgets w(style);
    Recorder rec;
    w.underline({-50, 20}, {60, 22}, true, rec);
    SH_CHECK(rec.prims.size() == 3);
    SH_CHECK(rec.prims[0].kind == sc::MenuPrim::Kind::Line && rec.prims[0].layer == sc::MenuPrim::Layer::Shadow);
    const sc::MenuPrim& band = rec.prims[1];
    SH_CHECK(band.kind == sc::MenuPrim::Kind::Quad && band.semi_transparent);
    SH_CHECK(band.x[0] == -50 && band.y[0] == 4 && band.y[1] == 20 && band.x[2] == 60 && band.y[2] == 6);
    SH_CHECK(band.rgb[0] == 0 && band.rgb[1] == 0x606060);
    SH_CHECK(rec.prims[2].kind == sc::MenuPrim::Kind::Blend);
    rec.prims.clear();
    w.underline({0, 0}, {1, 1}, false, rec);
    SH_CHECK(rec.prims.size() == 1 && rec.prims[0].layer == sc::MenuPrim::Layer::Underline);
}

SH_TEST(menu_widgets_arrow_flash_ramp) {
    const sc::MenuStyle style = shipped();
    sc::MenuWidgets w(style);
    const sc::MenuPoint v[3] = {{0, 0}, {4, 4}, {0, 8}};
    // (timer, tip green, base green): tip ramps 0..32 first, then the base.
    const int cases[][3] = {{10, 0, 70}, {40, 56, 224}, {80, 224, 112}, {100, 196, 0}, {128 + 10, 0, 70}};
    for (const auto& c : cases) {
        Recorder rec;
        w.arrow(v, true, c[0], rec);
        SH_CHECK(rec.prims.size() == 1);
        SH_CHECK(rec.prims[0].rgb[0] == (0xFF0000u | (static_cast<unsigned>(c[1]) << 8)));
        SH_CHECK(rec.prims[0].rgb[1] == (0xFF0000u | (static_cast<unsigned>(c[2]) << 8)));
        SH_CHECK(rec.prims[0].rgb[2] == rec.prims[0].rgb[1]);
    }
    Recorder rec;
    w.arrow(v, false, 50, rec);
    SH_CHECK(rec.prims[0].rgb[0] == 0xF0F000 && rec.prims[0].layer == sc::MenuPrim::Layer::Arrow);
}

SH_TEST(menu_widgets_bullet_triangles) {
    const sc::MenuStyle style = shipped();
    sc::MenuWidgets w(style);
    const sc::MenuPoint q[4] = {{0, 0}, {10, 0}, {0, 10}, {10, 10}};
    Recorder rec;
    w.bullet(q, true, false, rec);
    SH_CHECK(rec.prims.size() == 2);
    SH_CHECK(rec.prims[0].x[1] == 0 && rec.prims[0].y[1] == 10 && rec.prims[0].x[2] == 10 && rec.prims[0].y[2] == 10);
    SH_CHECK(rec.prims[1].x[1] == 10 && rec.prims[1].y[1] == 0);
    SH_CHECK(rec.prims[0].rgb[0] == 0xFFFFFF && rec.prims[0].rgb[2] == 0x404040);
    rec.prims.clear();
    w.bullet(q, false, true, rec);
    SH_CHECK(rec.prims[1].x[0] == 10 && rec.prims[1].y[0] == 10 && rec.prims[1].rgb[0] == 0x204050);
}

SH_TEST(menu_widgets_vignette_bands) {
    const sc::MenuStyle style = shipped();
    sc::MenuWidgets w(style);
    Recorder rec;
    w.vignette(rec);
    SH_CHECK(rec.prims.size() == 3);
    SH_CHECK(rec.prims[0].x[0] == -160 && rec.prims[0].y[0] == -112 && rec.prims[0].y[1] == -96);
    SH_CHECK(rec.prims[0].x[2] == 160 && rec.prims[0].y[3] == -96 && rec.prims[0].rgb[0] == 0x606060);
    SH_CHECK(rec.prims[1].y[0] == 112 && rec.prims[1].y[1] == 72 && rec.prims[1].rgb[1] == 0);
    SH_CHECK(rec.prims[2].kind == sc::MenuPrim::Kind::Blend && rec.prims[2].layer == sc::MenuPrim::Layer::Vignette);
}
