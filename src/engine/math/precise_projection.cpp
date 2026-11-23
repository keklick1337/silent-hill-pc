// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/math/precise_projection.h"

namespace sh::math {

PreciseVertex project_precise(const Projection& p, const Vec3s& v, const Vec3d* exact_vertex,
                              const ExactTransform* exact_camera, const PreciseOptions& opt) {
    PreciseVertex out;
    std::int64_t depth = 0;  // the unclamped depth, the transform's third accumulator shifted by 12
    bool far_hit = false;
    double far_x = 0, far_y = 0;
    if (opt.whole_map_far) {
        const ProjectedFar f = project_vertex_far(p, v);
        out.v = f.v;
        out.unclamped_depth = f.unclamped_depth;
    } else {
        out.v = project_vertex(p, v);
    }
    {
        const auto& r = p.rot.m;
        depth = ((static_cast<std::int64_t>(p.trans.z) << 12) + r[2][0] * v.x + r[2][1] * v.y + r[2][2] * v.z) >> 12;
    }
    if (opt.whole_map_far && depth > 0 &&
        (depth > 0xffff || out.v.view.x != out.v.view_short.x || out.v.view.y != out.v.view_short.y)) {
        const double ratio = static_cast<double>(p.h) / static_cast<double>(depth);
        far_x = static_cast<double>(p.ofx) / 65536.0 + static_cast<double>(out.v.view.x) * ratio;
        far_y = static_cast<double>(p.ofy) / 65536.0 + static_cast<double>(out.v.view.y) * ratio;
        far_hit = true;
    }

    const double ofx = static_cast<double>(p.ofx) / 65536.0;
    const double ofy = static_cast<double>(p.ofy) / 65536.0;
    const double h = static_cast<double>(p.h);
    double fx, fy;
    float w;
    if (out.v.sz > 0) {
        // A true divide: the hardware reciprocal saturates at 2.0 for vertices close to the camera, which would
        // make a polygon straddling that depth half perspective, half affine.
        const double ratio = h / static_cast<double>(out.v.sz);
        fx = ofx + static_cast<double>(out.v.view_short.x) * ratio;
        fy = ofy + static_cast<double>(out.v.view_short.y) * ratio;
        w = opt.unquantized_depth ? static_cast<float>(depth) : static_cast<float>(out.v.sz);
    } else {
        fx = out.v.sx;
        fy = out.v.sy;
        w = 0.0f;
    }
    if (exact_camera && exact_camera->has_rotation && exact_camera->has_translation) {
        const double* r = exact_camera->rot;
        const double* t = exact_camera->trans;
        const double vx = exact_vertex ? exact_vertex->x : static_cast<double>(v.x);
        const double vy = exact_vertex ? exact_vertex->y : static_cast<double>(v.y);
        const double vz = exact_vertex ? exact_vertex->z : static_cast<double>(v.z);
        const double tx = t[0] + r[0] * vx + r[1] * vy + r[2] * vz;
        const double ty = t[1] + r[3] * vx + r[4] * vy + r[5] * vz;
        const double tz = t[2] + r[6] * vx + r[7] * vy + r[8] * vz;
        if (tz > 0.0) {
            const double ratio = h / tz;
            fx = ofx + tx * ratio;
            fy = ofy + ty * ratio;
            w = opt.unquantized_depth ? static_cast<float>(tz) : static_cast<float>(out.v.sz);
        }
    }
    if (far_hit) {
        fx = far_x;
        fy = far_y;
        w = static_cast<float>(depth);
    }
    out.sx = static_cast<float>(fx);
    out.sy = static_cast<float>(fy);
    out.w = w;
    out.view_x = static_cast<float>(out.v.view.x);
    out.view_y = static_cast<float>(out.v.view.y);
    out.view_z = static_cast<float>(out.v.view.z);
    out.ofx = static_cast<float>(ofx);
    out.ofy = static_cast<float>(ofy);
    out.h = static_cast<float>(h);
    return out;
}

}  // namespace sh::math
