#include <cstdint>
#include <string>
#include <vector>

#include "engine/assets/upscale_cache.h"
#include "framework/sh_test.h"
#include "sh-upscale/upscaler.h"

namespace {

using sh::upscale::Picture;

Picture make(int w, int h) {
    Picture p{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4, 0)};
    return p;
}

std::uint8_t* at(Picture& p, int x, int y) { return &p.rgba[(static_cast<std::size_t>(y) * p.width + x) * 4]; }

// The network is replaced by nearest scaling: the tiles must join into the picture without seams or shifts.
bool nearest_infer(int scale, const std::vector<std::uint8_t>& rgb, int w, int h, std::vector<std::uint8_t>& out) {
    out.resize(static_cast<std::size_t>(w) * scale * h * scale * 3);
    for (int y = 0; y < h * scale; ++y)
        for (int x = 0; x < w * scale; ++x)
            for (int c = 0; c < 3; ++c)
                out[(static_cast<std::size_t>(y) * w * scale + x) * 3 + c] =
                    rgb[(static_cast<std::size_t>(y / scale) * w + x / scale) * 3 + c];
    return true;
}

}  // namespace

SH_TEST(upscale_tiles_join_and_alpha_stays_in_classes) {
    Picture src = make(37, 21);
    for (int y = 0; y < src.height; ++y)
        for (int x = 0; x < src.width; ++x) {
            std::uint8_t* t = at(src, x, y);
            t[0] = static_cast<std::uint8_t>(x * 5);
            t[1] = static_cast<std::uint8_t>(y * 9);
            t[2] = static_cast<std::uint8_t>(x ^ y);
            t[3] = (x + y) % 3 == 0 ? 0 : ((x + y) % 3 == 1 ? 128 : 255);
        }
    for (const int scale : {2, 4}) {
        Picture dst;
        const auto infer = [scale](const std::vector<std::uint8_t>& rgb, int w, int h, std::vector<std::uint8_t>& out) {
            return nearest_infer(scale, rgb, w, h, out);
        };
        SH_CHECK(sh::upscale::upscale_tiled(src, scale, 8, 3, infer, dst));
        SH_CHECK(dst.width == src.width * scale && dst.height == src.height * scale);
        bool same = true;
        for (int y = 0; y < dst.height; ++y)
            for (int x = 0; x < dst.width; ++x)
                for (int c = 0; c < 4; ++c) same = same && at(dst, x, y)[c] == at(src, x / scale, y / scale)[c];
        SH_CHECK(same);
    }
}

SH_TEST(upscale_bleed_fills_transparent_colour_only) {
    Picture p = make(5, 1);
    std::uint8_t* solid = at(p, 0, 0);
    solid[0] = 200;
    solid[1] = 100;
    solid[2] = 50;
    solid[3] = 255;
    sh::upscale::bleed_colour(p);
    for (int x = 1; x < 5; ++x) {
        SH_CHECK(at(p, x, 0)[0] == 200 && at(p, x, 0)[1] == 100 && at(p, x, 0)[2] == 50);
        SH_CHECK(at(p, x, 0)[3] == 0);
    }
}

SH_TEST(upscale_cache_keys) {
    using sh::upscale::texture_key;
    SH_CHECK(texture_key("convert/textures/BG/CONV1.png").value_or("") == "BG/CONV1");
    SH_CHECK(texture_key("/x/gamedata/convert/textures/CHARA/HR.er-2.png").value_or("") == "CHARA/HR.er-2");
    SH_CHECK(texture_key("C:\\g\\convert\\textures\\TIM\\BLOOD.png").value_or("") == "TIM/BLOOD");
    SH_CHECK(!texture_key("convert/textures/TIM/FONT16.png"));
    SH_CHECK(!texture_key("convert/textures/TIM/BLOOD.subtract.png"));
    SH_CHECK(!texture_key("convert/textures/TEST/A.png"));
    SH_CHECK(!texture_key("convert/textures/BG/CONV1.tim.json"));
    SH_CHECK(!texture_key("convert/models/CHARA/HR.png"));
    const auto f = sh::upscale::cache_file("root", sh::upscale::kModel, "BG/CONV1", 4, 0x0123456789abcdefull);
    SH_CHECK(f.generic_string() == "root/realesrgan-x4plus-x4-v2/BG/CONV1.0123456789abcdef.png");
    SH_CHECK(sh::upscale::valid_scale(2) && sh::upscale::valid_scale(4));
    SH_CHECK(!sh::upscale::valid_scale(8) && !sh::upscale::valid_scale(16));
}

SH_TEST(upscale_lanczos_keeps_flat_colour_and_edges) {
    Picture src = make(6, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 6; ++x) {
            std::uint8_t* t = at(src, x, y);
            t[0] = x < 3 ? 20 : 220;  // a hard vertical edge between x 2 and 3
            t[1] = 90;
            t[2] = 140;
            t[3] = 255;
        }
    Picture dst;
    sh::upscale::scale_lanczos(src, 4, dst);
    SH_CHECK(dst.width == 24 && dst.height == 16);
    bool flat = true, ordered = true;
    for (int y = 0; y < dst.height; ++y) {
        for (int x = 0; x < dst.width; ++x) flat = flat && at(dst, x, y)[1] == 90 && at(dst, x, y)[2] == 140;
        // Away from the edge the colour is near the source's (Lanczos rings a little); the step stays in place.
        ordered = ordered && at(dst, 0, y)[0] < 30 && at(dst, 23, y)[0] > 210 && at(dst, 10, y)[0] < 120 &&
                  at(dst, 13, y)[0] > 120;
    }
    SH_CHECK(flat);
    SH_CHECK(ordered);
}

SH_TEST(upscale_lettering_rects_replace_inside_and_fade_out) {
    Picture net = make(16, 16), sharp = make(16, 16);
    for (std::size_t i = 0; i < net.rgba.size(); i += 4) {
        net.rgba[i] = 0;
        sharp.rgba[i] = 200;
    }
    // Source texel (1, 1) of a picture at x4; the fade reaches one texel (4 output texels) past it.
    sh::upscale::paste_rects(sharp, {sh::upscale::Rect{1, 1, 1, 1}}, 4, net);
    SH_CHECK(at(net, 5, 5)[0] == 200 && at(net, 7, 6)[0] == 200);
    SH_CHECK(at(net, 9, 5)[0] > 0 && at(net, 9, 5)[0] < 200);
    SH_CHECK(at(net, 13, 5)[0] == 0 && at(net, 5, 13)[0] == 0);
}

SH_TEST(upscale_lettering_list) {
    std::string error;
    const auto list = sh::upscale::parse_lettering(
        R"({"description": "x", "lettering": [{"name": "TIM/MP_0TOWN"},
            {"name": "TIM/ALESSA", "rects": [[220, 210, 70, 25]], "description": "her name"}]})",
        error);
    SH_CHECK(list.has_value());
    if (!list) return;
    const sh::upscale::Lettering* map = sh::upscale::lettering_for(*list, "TIM/MP_0TOWN");
    SH_CHECK(map != nullptr && map->rects.empty() && map->signature() == "whole");
    const sh::upscale::Lettering* sign = sh::upscale::lettering_for(*list, "tim/alessa.alt1");
    SH_CHECK(sign != nullptr && sign->rects.size() == 1 && sign->signature() == "220,210,70,25");
    SH_CHECK(sh::upscale::lettering_for(*list, "TIM/ALESSAX") == nullptr);
    SH_CHECK(!sh::upscale::parse_lettering(R"({"lettering": [{"rects": [[0, 0, 1, 1]]}]})", error));
    SH_CHECK(!sh::upscale::parse_lettering(R"({"lettering": [{"name": "A/B", "rects": [[0, 0, 0, 1]]}]})", error));
    SH_CHECK(!sh::upscale::parse_lettering(R"({"lettering": [{"name": "A/B", "rects": []}]})", error));
    const auto none = sh::upscale::parse_lettering("{}", error);
    SH_CHECK(none.has_value() && none->empty());
}
