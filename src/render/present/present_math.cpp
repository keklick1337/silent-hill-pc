#include "render/present/present_math.h"

#include <algorithm>
#include <cmath>

namespace sh::present {

Size scaled_render_size(int w, int h, float scale)
{
    if (!(scale > 0.0f))
        scale = 1.0f;
    return {std::max(1, static_cast<int>(static_cast<float>(w) * scale + 0.5f)),
            std::max(1, static_cast<int>(static_cast<float>(h) * scale + 0.5f))};
}

int msaa_sample_count(int requested, int device_max)
{
    int n = requested >= 8 ? 8 : requested >= 4 ? 4 : requested >= 2 ? 2 : 1;
    while (n > 1 && n > device_max)
        n /= 2;
    return n;
}

Size cap_supersampled_size(Size s, int w, int h, long long max_pixels, int max_side)
{
    if (s.w <= w && s.h <= h)
        return s;
    double f = 1.0;
    const double pixels = static_cast<double>(s.w) * static_cast<double>(s.h);
    if (max_pixels > 0 && pixels > static_cast<double>(max_pixels))
        f = std::sqrt(static_cast<double>(max_pixels) / pixels);
    if (max_side > 0)
        f = std::min({f, static_cast<double>(max_side) / s.w, static_cast<double>(max_side) / s.h});
    if (f >= 1.0)
        return s;
    return {std::max(w, static_cast<int>(s.w * f)), std::max(h, static_cast<int>(s.h * f))};
}

float ui2d_wide_factor(bool native_render, int window_w, int window_h)
{
    if (native_render || window_w <= 0 || window_h <= 0)
        return 1.0f;
    const float f = (static_cast<float>(window_w) / static_cast<float>(window_h)) / (4.0f / 3.0f);
    return f > 1.0f ? f : 1.0f;
}

PlacedRect present_rect(const PresentRectInput& in)
{
    PlacedRect r{0, 0, in.present_w, in.present_h};

    if (in.native_render && r.w > 0 && r.h > 0) {
        if (r.w * 3 > r.h * 4) {
            r.w = (r.h * 4 + 1) / 3;
            r.x = (in.present_w - r.w) / 2;
        } else {
            r.h = (r.w * 3 + 2) / 4;
            r.y = (in.present_h - r.h) / 2;
        }
        return r;
    }

    const bool pillarbox = !in.native_render &&
        ((in.hor_plus && in.widescreen_mode == 0) || (!in.hor_plus && in.menu_pillarbox));
    if (pillarbox && in.internal_w > 0 && in.internal_h > 0 && r.h > 0) {
        const float src = static_cast<float>(in.internal_w) / static_cast<float>(in.internal_h);
        const float dst = static_cast<float>(r.w) / static_cast<float>(r.h);
        if (dst > src) {
            r.w = static_cast<int>(static_cast<float>(r.h) * src + 0.5f);
            r.x = (in.present_w - r.w) / 2;
        } else if (dst < src) {
            r.h = static_cast<int>(static_cast<float>(r.w) / src + 0.5f);
            r.y = (in.present_h - r.h) / 2;
        }
    }
    return r;
}

PointF pointer_to_overlay(const PointerInput& in, float x, float y) {
    if (in.window_w > 0 && in.window_h > 0 && in.drawable_w > 0 && in.drawable_h > 0) {
        x = x * static_cast<float>(in.drawable_w) / static_cast<float>(in.window_w);
        y = y * static_cast<float>(in.drawable_h) / static_cast<float>(in.window_h);
    }
    if (!in.overlay_to_window && in.present.w > 0 && in.present.h > 0) {
        x = (x - static_cast<float>(in.present.x)) * static_cast<float>(in.target_w) / static_cast<float>(in.present.w);
        y = (y - static_cast<float>(in.present.y)) * static_cast<float>(in.target_h) / static_cast<float>(in.present.h);
    }
    return {x, y};
}

float display_pixel_aspect(const PixelAspectInput& in)
{
    if (in.raw || in.disp_w <= 0.0f || in.disp_h <= 0.0f)
        return in.configured_par;

    const float trim = in.crt_trim > 0.0f ? in.crt_trim : 1.0f;
    const float target = ((4.0f / 3.0f) * trim) / (in.disp_w / in.disp_h);

    // Only a Hor+ world pass carries the hfov/vfov knobs; 2D passes and the item-take screen run unscaled.
    const bool world = in.hor_plus && !in.ui_pass;
    const float hs = world ? (in.world_hscale > 0.0f ? in.world_hscale : 1.0f) : 1.0f;
    float vs;
    if (!world || in.item_take)
        vs = 1.0f;
    else if (in.cutscene && in.cutscene_vscale > 0.0f)
        vs = in.cutscene_vscale;
    else
        vs = in.world_vscale > 0.0f ? in.world_vscale : 1.0f;
    return (hs * vs) / target;
}

PlacedRect picture_rect(const PictureRectInput& in)
{
    PlacedRect r{0, 0, in.target_w, in.target_h};
    if (in.native_render || r.w <= 0 || r.h <= 0)
        return r;

    const bool pillarbox = in.hor_plus ? in.widescreen_mode == 0 : in.menu_pillarbox;
    if (r.w * 3 > r.h * 4) {
        if (pillarbox && !in.ui2d_wide) {
            r.w = static_cast<int>(static_cast<float>(in.target_h) * (4.0f / 3.0f) + 0.5f);
            r.x = (in.target_w - r.w) / 2;
        }
    } else if (r.w * 3 < r.h * 4) {
        r.h = static_cast<int>(static_cast<float>(in.target_w) * 0.75f + 0.5f);
        r.y = (in.target_h - r.h) / 2;
    }
    return r;
}

namespace {

float target_over_disp_aspect(const ScreenOrthoInput& in)
{
    const float disp_aspect = in.disp_w / in.disp_h;
    const float target_aspect = in.target_h > 0
        ? static_cast<float>(in.target_w) / static_cast<float>(in.target_h) : disp_aspect;
    return target_aspect / disp_aspect;
}

// Hor+ margin past each side of the display buffer: the extra window aspect, at the pass's pixel aspect.
float hor_plus_margin(const ScreenOrthoInput& in, float wider)
{
    return in.disp_w * (wider * in.pixel_aspect - 1.0f) * 0.5f;
}

} // namespace

OrthoRect screen_ortho(const ScreenOrthoInput& in)
{
    OrthoRect o{0.0f, in.disp_w, 0.0f, in.disp_h};
    if (!(in.disp_w > 0.0f) || !(in.disp_h > 0.0f))
        return o;

    if (in.hor_plus) {
        // The world is cropped vertically (vfov), anchored by vcrop_anchor; the UI pass and the item-take screen
        // stay at full height so subtitles, fades and letterbox bars are not cut off.
        float vs;
        if (in.ui_pass || in.item_take)
            vs = 1.0f;
        else if (in.cutscene && in.cutscene_vscale > 0.0f)
            vs = in.cutscene_vscale;
        else
            vs = in.world_vscale;
        o.top = in.disp_h * (1.0f - vs) * in.vcrop_anchor;
        o.bottom = o.top + in.disp_h * vs;
    }

    const float centre = in.disp_w * 0.5f;
    // The authored-frame paths only carry hfov on a raw-aspect picture: the CRT solve already divides it out.
    float hs43 = 1.0f;
    if (in.aspect_raw && in.hor_plus && !in.ui_pass)
        hs43 = in.world_hscale;
    const float half43 = centre / (hs43 > 0.0f ? hs43 : 1.0f);
    const float wider = target_over_disp_aspect(in);

    if (in.ui2d_wide) {
        const float half = centre * in.ui2d_wide_factor;
        o.left = centre - half;
        o.right = centre + half;
    } else if (in.hor_plus && wider > 1.0f && in.widescreen_mode == 1) {
        const float hscale = in.ui_pass ? 1.0f : in.world_hscale;
        const float half = (centre + hor_plus_margin(in, wider)) / hscale;
        o.left = centre - half;
        o.right = centre + half;
    } else if (hs43 != 1.0f) {
        o.left = centre - half43;
        o.right = centre + half43;
    }
    return o;
}

std::array<float, 16> ortho_matrix(float left, float right, float bottom, float top, float znear, float zfar)
{
    std::array<float, 16> m{};
    m[0] = 2.0f / (right - left);
    m[5] = 2.0f / (top - bottom);
    m[10] = 2.0f / (znear - zfar);
    m[12] = (left + right) / (left - right);
    m[13] = (bottom + top) / (bottom - top);
    m[14] = (znear + zfar) / (znear - zfar);
    m[15] = 1.0f;
    return m;
}

bool hor_plus_half_widths(const ScreenOrthoInput& in, HalfWidths* out)
{
    if (!out || !in.hor_plus || in.widescreen_mode != 1 || !(in.disp_w > 0.0f) || !(in.disp_h > 0.0f) ||
        in.target_h <= 0)
        return false;
    const float wider = target_over_disp_aspect(in);
    if (wider <= 1.0f)
        return false;
    // The authored reference is the true display frame, without hfov: hfov widens what is shown in every mode.
    const float hscale = in.world_hscale > 0.0f ? in.world_hscale : 1.0f;
    out->authored = in.disp_w * 0.5f;
    out->installed = (in.disp_w * 0.5f + hor_plus_margin(in, wider)) / hscale;
    return true;
}

bool clip_scissor(const ClipInput& in, PlacedRect* out)
{
    const int dx = in.clip_x - in.disp_x, dy = in.clip_y - in.disp_y;
    const bool partial = dx > 0 || dy > 0 || in.clip_w < in.disp_w - 1 || in.clip_h < in.disp_h - 1;
    const bool wide = in.wide && in.ortho_right > in.ortho_left;
    const bool clipped = partial || (in.interlaced && !wide);
    if (!clipped || !out || in.disp_w <= 0 || in.disp_h <= 0)
        return false;
    const float tw = static_cast<float>(in.target_w), th = static_cast<float>(in.target_h);
    if (wide) {
        const float k = tw / (in.ortho_right - in.ortho_left);
        const float sy = th / static_cast<float>(in.disp_h);
        const float x0 = (static_cast<float>(dx) - in.ortho_left) * k;
        const float x1 = (static_cast<float>(dx + in.clip_w) - in.ortho_left) * k;
        out->x = static_cast<int>(std::lround(x0));
        out->w = static_cast<int>(std::lround(x1)) - out->x;
        out->y = static_cast<int>(std::lround(th - static_cast<float>(dy + in.clip_h) * sy));
        out->h = static_cast<int>(std::lround(th - static_cast<float>(dy) * sy)) - out->y;
        return true;
    }
    // Reciprocals, not divisions: the truncation below must land on the same pixel on every backend.
    const float inv_w = 1.0f / static_cast<float>(in.disp_w), inv_h = 1.0f / static_cast<float>(in.disp_h);
    const float fx = static_cast<float>(dx) * inv_w;
    const float fy = static_cast<float>(dy) * inv_h;
    const float fw = static_cast<float>(in.clip_w) * inv_w;
    const float fh = static_cast<float>(in.clip_h) * inv_h;
    // The area's top edge, measured up from the target's bottom.
    const float top = th - fh * th;
    out->x = static_cast<int>(fx * tw);
    out->y = static_cast<int>(top - fy * th);
    out->w = static_cast<int>(fw * tw);
    out->h = static_cast<int>(fh * th);
    return true;
}

PlacedRect authored_frame_rect(const OrthoRect& o, float disp_w, int target_w, int target_h)
{
    if (!(o.right > o.left))
        return {0, 0, target_w, target_h};
    const float k = static_cast<float>(target_w) / (o.right - o.left);
    const int x0 = static_cast<int>(std::lround((0.0f - o.left) * k));
    const int x1 = static_cast<int>(std::lround((disp_w - o.left) * k));
    return {x0, 0, x1 - x0, target_h};
}

PlacedRect intersect(const PlacedRect& a, const PlacedRect& b)
{
    const int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const int x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

bool display_area(const OrthoRect& o, float disp_w, float disp_h, const PlacedRect& vp, AreaRect* out)
{
    if (!out || !(o.right > o.left) || !(o.bottom > o.top))
        return false;
    const float sx = static_cast<float>(vp.w) / (o.right - o.left);
    const float sy = static_cast<float>(vp.h) / (o.bottom - o.top);
    const float vp_top = static_cast<float>(vp.y + vp.h);
    out->x0 = static_cast<float>(vp.x) - o.left * sx;
    out->x1 = static_cast<float>(vp.x) + (disp_w - o.left) * sx;
    out->y1 = vp_top + o.top * sy;
    out->y0 = vp_top - (disp_h - o.top) * sy;
    return true;
}

void NearestTextureSet::mark(std::uint32_t tex)
{
    if (tex == 0 || contains(tex) || tex_.size() >= kCapacity)
        return;
    tex_.push_back(tex);
}

bool NearestTextureSet::contains(std::uint32_t tex) const
{
    return std::find(tex_.begin(), tex_.end(), tex) != tex_.end();
}

} // namespace sh::present
