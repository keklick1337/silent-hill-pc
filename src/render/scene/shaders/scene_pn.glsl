// PN triangles (curved point-normal triangles) over view-space corners: the one surface of model upscaling that the
// tessellation path (scene_pn.tese) and the mesh path (scene_pn.mesh) both evaluate (RENDER.md section 7.11).
// Included after #version and scene_lib.glsl; no resources of its own.
//
// A corner's normal is the patch normal of its welded position (scene/model_upscale.h), w bit 1 where the model's own
// normals split there (a hard edge). An edge whose two corners both lie on such a split stays straight, so hard edges
// stay hard. Every edge control point depends on that edge's two corners only, and the evaluation below adds the terms
// of an edge in an order both triangles of the edge share, so neighbouring patches meet without cracks. Map surfaces
// bring their edge control points made (PnMakeEdges, scene/model_upscale.h pn_surface); w bit 2 of a corner marks its
// edge to the next corner straight.

struct PnPatch {
    vec3 b300, b030, b003;
    vec3 b210, b120, b021, b012, b102, b201;
    vec3 b111;
};

bool PnFlag(vec4 n, int bit) { return (int(n.w + 0.5) & bit) != 0; }

// The control point of edge i -> j next to corner i.
vec3 PnEdgePoint(vec3 pi, vec4 ni, vec3 pj, vec4 nj, bool straight) {
    precise vec3 r;
    if (straight || (PnFlag(ni, 1) && PnFlag(nj, 1))) {
        r = (2.0 * pi + pj) / 3.0;
    } else {
        vec3 n = normalize(ni.xyz);
        precise float w = dot(pj - pi, n);
        r = (2.0 * pi + pj - w * n) / 3.0;
    }
    return r;
}

PnPatch PnMake(vec3 p0, vec3 p1, vec3 p2, vec4 n0, vec4 n1, vec4 n2) {
    PnPatch b;
    b.b300 = p0;
    b.b030 = p1;
    b.b003 = p2;
    bool s01 = PnFlag(n0, 2), s12 = PnFlag(n1, 2), s20 = PnFlag(n2, 2);
    b.b210 = PnEdgePoint(p0, n0, p1, n1, s01);
    b.b120 = PnEdgePoint(p1, n1, p0, n0, s01);
    b.b021 = PnEdgePoint(p1, n1, p2, n2, s12);
    b.b012 = PnEdgePoint(p2, n2, p1, n1, s12);
    b.b102 = PnEdgePoint(p2, n2, p0, n0, s20);
    b.b201 = PnEdgePoint(p0, n0, p2, n2, s20);
    precise vec3 e = (b.b210 + b.b120 + b.b021 + b.b012 + b.b102 + b.b201) / 6.0;
    precise vec3 v = (p0 + p1 + p2) / 3.0;
    b.b111 = e + (e - v) * 0.5;
    return b;
}

// A map surface's patch from control points made per edge (scene/model_upscale.h pn_surface): corner i's offsets to
// the control point next to it on its edge to the next corner (`ni`) and on its edge to the previous one (`ei`).
PnPatch PnMakeEdges(vec3 p0, vec3 p1, vec3 p2, vec3 n0, vec3 e0, vec3 n1, vec3 e1, vec3 n2, vec3 e2) {
    PnPatch b;
    b.b300 = p0;
    b.b030 = p1;
    b.b003 = p2;
    precise vec3 b210 = p0 + n0, b201 = p0 + e0;
    precise vec3 b021 = p1 + n1, b120 = p1 + e1;
    precise vec3 b102 = p2 + n2, b012 = p2 + e2;
    b.b210 = b210;
    b.b201 = b201;
    b.b021 = b021;
    b.b120 = b120;
    b.b102 = b102;
    b.b012 = b012;
    precise vec3 e = (b.b210 + b.b120 + b.b021 + b.b012 + b.b102 + b.b201) / 6.0;
    precise vec3 v = (p0 + p1 + p2) / 3.0;
    b.b111 = e + (e - v) * 0.5;
    return b;
}

// One corner's share: its own term and the terms of its two edges. On an edge the third corner's weight is exactly 0,
// so its share and the interior term vanish and what is left is the sum of the edge's two corner shares, the same in
// either triangle (a + b == b + a).
vec3 PnCorner(float a, vec3 p, float toNext, vec3 bNext, float toPrev, vec3 bPrev) {
    precise vec3 r = (a * a * a * p + 3.0 * a * a * toNext * bNext) + 3.0 * a * a * toPrev * bPrev;
    return r;
}

// The surface at barycentric (u, v, w) of corners 0, 1, 2.
vec3 PnEval(PnPatch b, vec3 t) {
    float u = t.x, v = t.y, w = t.z;
    precise vec3 cu = PnCorner(u, b.b300, v, b.b210, w, b.b201);
    precise vec3 cv = PnCorner(v, b.b030, w, b.b021, u, b.b120);
    precise vec3 cw = PnCorner(w, b.b003, u, b.b102, v, b.b012);
    precise vec3 r = ((cu + cv) + cw) + 6.0 * u * v * w * b.b111;
    return r;
}

// Whether the patch lies wholly outside one side of the view volume: the patch is inside its control points' hull.
bool PnPatchCulled(PnPatch b) {
    vec3 pts[10] = vec3[10](b.b300, b.b030, b.b003, b.b210, b.b120, b.b021, b.b012, b.b102, b.b201, b.b111);
    bvec4 allOut = bvec4(true);
    bool allNear = true;
    for (int k = 0; k < 10; ++k) {
        float unused;
        vec4 c = SceneProject(pts[k], unused);
        allOut = bvec4(allOut.x && c.x < -c.w, allOut.y && c.x > c.w, allOut.z && c.y < -c.w, allOut.w && c.y > c.w);
        allNear = allNear && c.z < 0.0;
    }
    return any(allOut) || allNear;
}
