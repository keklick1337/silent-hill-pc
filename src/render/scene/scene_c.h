/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SH_SCENE_C_H
#define SH_SCENE_C_H

/* C face of the scene renderer (scene.h) for the code that describes the
 * frame from the game state: plain C types only, safe inside a C ABI
 * region. Values are in the units of scene.h. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ShSceneLighting
{
    int    mode;          /* 0 flat, 1 point light, 2 directional */
    float  tint[3];       /* 0..255 */
    float  farColor[3];   /* 0..255 */
    float  ambient;
    float  maxIntensity;
    float  depthCue;
    int    fog;
    double lightVec[3];   /* world axes, direction times intensity (Q12) */
    double lightPos[3];   /* world position */
} ShSceneLighting;

typedef struct ShScenePrelitVertex
{
    float         x, y, z, u, v;
    unsigned char r, g, b, a;
} ShScenePrelitVertex;

/* blend: 0 opaque, 1 additive by STP, 2 average. */
typedef struct ShScenePrelit
{
    unsigned long long          textureKey;
    const unsigned char*        rgba;
    int                         textureWidth, textureHeight;
    int                         blend;
    float                       fade;
    int                         noShadow;   /* not a flashlight shadow caster */
    const ShScenePrelitVertex*  vertices;
    int                         vertexCount;
    const unsigned int*         indices;
    int                         indexCount;
} ShScenePrelit;

/* The world of this frame: camera (world -> view, rotation row-major unit
 * scale), projection distance and centre. Marks the frame as having a scene. */
void ShScene_SetCamera(const double rot[9], const double trans[3], float h, float ofsX, float ofsY);
void ShScene_SetLighting(const ShSceneLighting* light);
void ShScene_AddCell(const char* stem, int cellX, int cellZ);
/* Copies the mesh (texture pixels are read during the call only) and returns
 * its index for a scene packet (its code is index + 1), or -1. */
int  ShScene_AddPrelit(const ShScenePrelit* mesh);
/* A library model (scene.h find_model: gamedata file, "<ROOT>/<MODEL>" or
 * "<ROOT>/#<index>") posed in the world: rot row-major model -> world at unit
 * scale, trans the world position (PS1 units). Returns the code of its scene
 * packet, 0 when the file has no such model; *otGroup
 * (may be null) gets the model's OT bucket group. */
unsigned ShScene_AddObject(const char* file, const char* name, const double rot[9], const double trans[3],
                           int* otGroup);
/* After the frame was drawn (or skipped). */
void ShScene_FrameEnd(void);

#ifdef __cplusplus
}
#endif

#endif
