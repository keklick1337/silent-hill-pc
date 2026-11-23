// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/precision/pc_smooth_xform.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "engine/camera/view_space.h"
#include "engine/camera/view_system.h"
#include "engine/math/precise_capture.h"
#include "render/game_view.h"
#include "render/gpu/render_state.h"


namespace {

namespace cam = ::sh::camera;
using Frame = cam::FrameHost::Frame;

// Twin of one frame's local transform. The integer values it was
// recorded against are the key: the game may overwrite a bone after the
// animation (IK, head tracking, scripted poses), and such a bone must draw
// from its integers.
struct LocalTwin
{
    const void* owner = nullptr;
    short       rotKey[9]{};
    int         transKey[3]{};
    double      rot[9]{};   // Q12 units
    double      trans[3]{}; // integer units of the local translation
    bool        rotValid   = false;
    bool        transValid = false;
};

constexpr unsigned kBits = 12;
constexpr unsigned kSize = 1u << kBits;
constexpr unsigned kMask = kSize - 1u;
constexpr int      kProbe = 16;

std::array<LocalTwin, kSize> s_twins;

unsigned Hash(const void* p)
{
    return static_cast<unsigned>((reinterpret_cast<uintptr_t>(p) >> 3) * 2654435761u) & kMask;
}

LocalTwin* Put(const void* owner)
{
    const unsigned h = Hash(owner);
    for (int i = 0; i < kProbe; i++)
    {
        LocalTwin& e = s_twins[(h + i) & kMask];
        if (e.owner == owner || e.owner == nullptr)
        {
            e.owner = owner;
            return &e;
        }
    }
    // Table full around this slot: evict the home slot. The evicted frame
    // simply draws from its integers until it is recorded again.
    LocalTwin& e = s_twins[h];
    e = LocalTwin{};
    e.owner = owner;
    return &e;
}

const LocalTwin* Get(const void* owner)
{
    const unsigned h = Hash(owner);
    for (int i = 0; i < kProbe; i++)
    {
        const LocalTwin& e = s_twins[(h + i) & kMask];
        if (e.owner == owner)
            return &e;
        if (e.owner == nullptr)
            return nullptr;
    }
    return nullptr;
}

void RotKey(const cam::Matrix& m, short k[9])
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            k[r * 3 + c] = m.m[r][c];
}

bool SameRot(const cam::Matrix& m, const short k[9])
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            if (m.m[r][c] != k[r * 3 + c])
                return false;
    return true;
}

struct Xform
{
    double rot[9];   // Q12 units
    double trans[3]; // Q8 world units
};

// Local transform of `f` in double: the twin where it still matches the
// integers, the integers themselves otherwise (they are exact values too).
bool Local(cam::FrameHost& host, Frame f, Xform& out)
{
    const cam::Matrix local = host.local(f);
    const LocalTwin*  t     = Get(f);
    bool              exact = true;
    if (t && t->rotValid && SameRot(local, t->rotKey))
    {
        for (int i = 0; i < 9; i++)
            out.rot[i] = t->rot[i];
    }
    else
    {
        if (t && t->rotValid)
            exact = false;
        for (int r = 0; r < 3; r++)
            for (int k = 0; k < 3; k++)
                out.rot[r * 3 + k] = local.m[r][k];
    }
    const bool transKeyOk = t && t->transValid && t->transKey[0] == local.t[0] &&
                            t->transKey[1] == local.t[1] && t->transKey[2] == local.t[2];
    for (int i = 0; i < 3; i++)
        out.trans[i] = transKeyOk ? t->trans[i] : static_cast<double>(local.t[i]);
    if (t && t->transValid && !transKeyOk)
        exact = false;
    return exact;
}

// a * b with the GTE's composition (Vw_MultiplyAndTransformMatrix): rotation
// a.rot * b.rot, translation a.rot * b.trans + a.trans, rotations in Q12.
Xform Compose(const Xform& a, const Xform& b)
{
    Xform o;
    for (int r = 0; r < 3; r++)
    {
        for (int c = 0; c < 3; c++)
        {
            o.rot[r * 3 + c] = (a.rot[r * 3 + 0] * b.rot[0 * 3 + c] + a.rot[r * 3 + 1] * b.rot[1 * 3 + c] +
                                a.rot[r * 3 + 2] * b.rot[2 * 3 + c]) /
                               4096.0;
        }
        o.trans[r] = (a.rot[r * 3 + 0] * b.trans[0] + a.rot[r * 3 + 1] * b.trans[1] + a.rot[r * 3 + 2] * b.trans[2]) /
                         4096.0 +
                     a.trans[r];
    }
    return o;
}

constexpr int kMaxDepth = 32;

bool World(Frame c, Xform& out)
{
    cam::FrameHost& host = sh::camera::frame_records();
    Frame           chain[kMaxDepth];
    int             n = 0;
    // Same end-of-chain rule as the view space walk: a link the host cannot
    // follow is treated as no parent.
    for (Frame p = c; p != nullptr && n < kMaxDepth; p = host.parent(p))
    {
        if (!host.usable(p))
            break;
        chain[n++] = p;
    }
    if (n == 0 || n == kMaxDepth)
        return false;

    // The walk reuses the cached world matrix of the first frame up the chain
    // whose world is current. A frame this module recorded is rebuilt from its
    // local transform instead (that is where the precision is); any other
    // cached frame is taken as the game left it, which is also how a drawer
    // that fills the world matrix directly (static map models) is read.
    int top = n - 1;
    for (int i = 0; i < n; i++)
    {
        if (host.world_current(chain[i]) && Get(chain[i]) == nullptr)
        {
            top = i;
            break;
        }
    }

    bool exact = true;
    if (host.world_current(chain[top]) && Get(chain[top]) == nullptr)
    {
        const cam::Matrix m = host.world(chain[top]);
        for (int r = 0; r < 3; r++)
            for (int k = 0; k < 3; k++)
                out.rot[r * 3 + k] = m.m[r][k];
        for (int i = 0; i < 3; i++)
            out.trans[i] = m.t[i];
    }
    else
    {
        exact = Local(host, chain[top], out);
    }
    for (int i = top - 1; i >= 0; i--)
    {
        Xform l;
        exact &= Local(host, chain[i], l);
        out = Compose(out, l);
    }
    return exact;
}

} // namespace

// The world-to-view rotation (Q12 units) of the camera twin when it stands for `ws`, else of the integers; `trans`
// is the eye.
static void ViewCamera(const cam::WorldScreen& ws, Xform& out)
{
    sh::math::Transform key;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            key.rot.m[r][c] = ws.world_screen.m[r][c];
    key.trans = {ws.world_screen.t[0], ws.world_screen.t[1], ws.world_screen.t[2]};
    const sh::math::capture::CameraTwin* twin = sh::math::capture::camera_of(key.rot, key.trans);
    for (int r = 0; r < 3; r++)
    {
        for (int c = 0; c < 3; c++)
            out.rot[r * 3 + c] = twin != nullptr ? twin->rot[r * 3 + c] * 4096.0 : ws.world_to_view.m[r][c];
        out.trans[r] = twin != nullptr ? twin->eye[r] : static_cast<double>(ws.eye.t[r]);
    }
}

extern "C" int Pc_Smooth_Active(void)
{
    return sh::render::smooth_view() ? 1 : 0;
}

extern "C" void Pc_Smooth_BoneLocal(void* frame, const double* rotQ12, const double* trans)
{
    if (!frame || !Pc_Smooth_Active())
        return;
    const cam::Matrix local = sh::camera::frame_records().local(frame);
    LocalTwin*        t     = Put(frame);
    if (rotQ12)
    {
        RotKey(local, t->rotKey);
        for (int i = 0; i < 9; i++)
            t->rot[i] = rotQ12[i];
        t->rotValid = true;
    }
    if (trans)
    {
        for (int i = 0; i < 3; i++)
        {
            t->transKey[i] = local.t[i];
            t->trans[i]    = trans[i];
        }
        t->transValid = true;
    }
}

extern "C" void Pc_Smooth_BoneCopyTrans(void* dst, const void* src)
{
    if (!dst || !src || !Pc_Smooth_Active())
        return;
    const LocalTwin* s = Get(src);
    if (!s || !s->transValid)
        return;
    const cam::Matrix srcLocal = sh::camera::frame_records().local(const_cast<void*>(src));
    if (s->transKey[0] != srcLocal.t[0] || s->transKey[1] != srcLocal.t[1] || s->transKey[2] != srcLocal.t[2])
        return;
    double tr[3] = {s->trans[0], s->trans[1], s->trans[2]};
    Pc_Smooth_BoneLocal(dst, nullptr, tr);
}

extern "C" void Pc_Smooth_RootPosition(void* root, int posX, int posY, int posZ)
{
    if (!root || !Pc_Smooth_Active())
        return;
    const cam::Matrix local  = sh::camera::frame_records().local(root);
    const int         q12[3] = {posX, posY, posZ};
    double            tr[3];
    // Per component: a root whose height the character code offsets keeps its
    // integer height but still gets the fractional ground-plane position.
    for (int i = 0; i < 3; i++)
    {
        if ((q12[i] >> 4) == local.t[i])
        {
            tr[i] = static_cast<double>(q12[i]) / 16.0;
        }
        else
        {
            tr[i] = static_cast<double>(local.t[i]);
        }
    }
    Pc_Smooth_BoneLocal(root, nullptr, tr);
}

extern "C" int Pc_Smooth_WorldOf(const void* frame, double rotQ12[9], double trans[3])
{
    Xform w;
    if (!frame)
        return 0;
    const bool exact = World(const_cast<void*>(frame), w);
    for (int i = 0; i < 9; i++)
        rotQ12[i] = w.rot[i];
    for (int i = 0; i < 3; i++)
        trans[i] = w.trans[i];
    return exact ? 1 : 0;
}

extern "C" void Pc_Smooth_CoordView(void* frame, void* viewMatPtr)
{
    if (!frame || !viewMatPtr || !Pc_Smooth_Active())
        return;
    // The caller's view matrix has the engine matrix layout (checked by the bridge).
    cam::Matrix viewMat;
    std::memcpy(&viewMat, viewMatPtr, sizeof(viewMat));

    Xform w;
    World(frame, w);
    // (world - eye) seen through the world-to-view rotation: the camera twin every draw path shares, so the models
    // stay put against the map, effects and decals.
    const cam::WorldScreen ws = sh::camera::view_system().world_screen();
    Xform                  cam;
    ViewCamera(ws, cam);
    for (int i = 0; i < 3; i++)
        w.trans[i] -= cam.trans[i];
    for (int i = 0; i < 3; i++)
        cam.trans[i] = 0.0;
    const Xform v = Compose(cam, w);

    // The double result must describe the same matrix the integer path built,
    // up to the rounding it removes. Anything further off means this module
    // misread the hierarchy for this model; draw it from the integers.
    double rot[9];
    double trans[3];
    for (int r = 0; r < 3; r++)
    {
        for (int c = 0; c < 3; c++)
        {
            const double q = v.rot[r * 3 + c];
            if (std::fabs(q - viewMat.m[r][c]) > 48.0)
                return;
            rot[r * 3 + c] = q / 4096.0;
        }
        if (std::fabs(v.trans[r] - viewMat.t[r]) > 24.0)
            return;
        trans[r] = v.trans[r];
    }
    sh::math::capture::note_rotation(viewMatPtr, rot);
    sh::math::capture::note_translation(viewMatPtr, trans);
}
