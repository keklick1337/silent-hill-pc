// SPDX-License-Identifier: GPL-3.0-or-later
// Lens flares: a glow drawn in screen space around the projected
// position of a world point. A flare is a few fans of rays (Gouraud triangles as quads, lit at
// the centre and dark at the edges, each fan turning by its own spin per draw) and a corona of
// rings (spokes x rings quads whose colours fall off along a shade ramp). Its size follows the
// distance: geometry screen distance * size_numerator / (depth + 1); past max_size (too near)
// it is not drawn. The map7_s03 incubus death draws its flash this way. The module decides
// the quads; the game gives the trigonometry, the view and the projection and turns the quads
// into primitives (LensFlareHost). Every flare is data: gamedata/effects/lens_flares.json.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "engine/effects/drain_splash.h"
#include "engine/effects/effect_prims.h"
#include "formats/common/status.h"

namespace sh::effects {

// A fan of `rays` rays around the centre: ray i spans the angle range [a + 2i*h, a + 2(i+1)*h],
// h = (4096 / rays) / 2, a = angle + the spin accumulated so far; its base corners sit at
// size * inner and its tip at size * outer (Q12 factors), the centre takes `color`.
struct FlareFan {
    int rays = 1;
    std::int32_t angle = 0;
    std::int32_t spin = 0;   // added to the fan's angle after every draw
    std::int32_t outer = 0;  // Q12 factor of the size
    std::int32_t inner = 0;
    std::uint32_t color = 0;  // colour word of the centre corner
};

// `spokes` x `rings` quads around the centre out to size * scale (Q12 factor); ring j runs
// from brightness start - j*step to start - (j+1)*step, looked up in `shade`.
struct FlareCorona {
    bool present = false;
    int spokes = 32;
    int rings = 8;
    std::int32_t scale = 4096;
    std::int32_t brightness_start = 0;
    std::int32_t brightness_step = 0;
    std::vector<std::uint32_t> shade = std::vector<std::uint32_t>(256, 0);  // colour word by brightness
};

struct LensFlareParams {
    std::vector<Vec3> origins_q8;  // the world points a flare can be drawn at (Q8 world units)
    std::int32_t yaw = 0;
    std::int32_t slot = 0;          // ordering-table entry of every primitive
    std::int32_t size_numerator = 0;
    std::int32_t max_size = 0;
    std::vector<FlareFan> fans;
    FlareCorona corona;
    std::int32_t prim_code = 0x3A;
    bool mode = false;  // a draw-mode primitive closes the flare
    std::int32_t mode_tpage = 0;
    std::int32_t mode_dfe = 0;
    std::int32_t mode_dtd = 1;
};

class LensFlareHost {
public:
    virtual ~LensFlareHost() = default;

    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t geom_screen() = 0;  // the projection's screen distance

    // Sets the view for the flare: origin in Q8 world units and yaw.
    virtual void begin_view(const Vec3& origin_q8, std::int32_t yaw) = 0;
    virtual ScreenPoint project_origin() = 0;

    virtual void emit_quad(const SplashQuad& q, std::int32_t slot) = 0;
    virtual void emit_mode(std::int32_t slot, std::int32_t tpage, std::int32_t dfe, std::int32_t dtd) = 0;
};

class LensFlare {
public:
    explicit LensFlare(LensFlareParams params);
    const LensFlareParams& params() const { return params_; }
    // The spin accumulated by each fan.
    const std::vector<std::int32_t>& phases() const { return phases_; }

    // Draws the flare at origins_q8[origin]; nothing for an origin out of range. Returns
    // whether it was drawn (in view and not too near).
    bool draw(LensFlareHost& host, int origin);

private:
    void draw_fan(LensFlareHost& host, const FlareFan& fan, std::int32_t angle, std::int32_t size, int cx, int cy);
    void draw_corona(LensFlareHost& host, std::int32_t size, int cx, int cy);

    LensFlareParams params_;
    std::vector<std::int32_t> phases_;
};

// Reads every flare of gamedata/effects/lens_flares.json (or the last mod's copy), by name.
Result<std::map<std::string, LensFlareParams>> parse_lens_flares(std::string_view json_text);
Result<std::map<std::string, LensFlareParams>> load_lens_flares(const std::filesystem::path& gamedata_dir,
                                                               const std::filesystem::path& mods_dir,
                                                               std::string* source);

}  // namespace sh::effects
