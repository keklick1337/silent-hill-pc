#pragma once

// "Flat" meshes: the parsed LM/IPD model turned into indexed triangle lists that a writer (glTF) can take as they are.
//
// Space and orientation: the same as the model converter (model_converter.h), so a character exported there stands
// on a map exported here.
//  * positions are in world units (Q7.8 / 256, one unit taken as one metre) turned by 180 degrees about Z:
//    x' = -x, y' = -y, z' = z. PS1 space (+X right, +Y down, +Z forward) is right-handed and so is glTF; a rotation
//    keeps text and handedness as they are (negating Y alone would mirror the world);
//  * a quad (c0, c1, c2, c3) is drawn by the GPU as the triangles (c0, c1, c2) and (c1, c3, c2). PS1 front faces are
//    clockwise, glTF ones counter-clockwise, so they become (c0, c2, c1) and (c1, c2, c3);
//  * stored normals point inward, so the outward normal is -n, turned like positions: (nx, ny, -nz), unit length;
//  * uv is (u + 0.5, v + 0.5) in texels of the material's texture, origin at the top-left (the material's UV base,
//    uv_base, is 0 in every file of the image). Divide by the TIM's pixel size for glTF.
//
// The format has no vertex colors: lighting is computed at run time from the normals. Each flat mesh therefore carries
// positions, normals, uvs and indices only. Primitives are grouped by (material, clut, transparent), the state the GPU
// draw depends on.

#include "formats/convert/level/level_model.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sh::convert::level {

struct FlatMesh {
    std::string name;            // "<model>/<mesh index>/<material index>/<clut hex>[/alpha]"
    int material_index = -1;
    std::string material_name;   // texture sheet of the material, empty if the index is out of range
    std::uint16_t clut = 0;
    std::uint16_t clut_base = 0;  // LmMaterial::clut_base: the CLUT the primitives were authored against
    bool transparent = false;
    std::vector<float> positions;  // xyz per vertex
    std::vector<float> normals;    // xyz per vertex
    std::vector<float> uvs;        // uv per vertex, in texels
    std::vector<std::uint32_t> indices;  // triangles
};

struct FlatModel {
    std::string name;
    std::vector<FlatMesh> meshes;
    std::uint32_t source_primitives = 0;
    std::uint32_t skipped_primitives = 0;  // primitives with a vertex index outside the window of the model
    std::uint32_t degenerate_triangles = 0;  // halves of quads with a repeated corner, not emitted
};

// One placement of a model in a cell.
struct FlatInstance {
    std::uint32_t model_info_index = 0;
    std::string model_name;
    bool is_global_plm = false;
    int flat_model = -1;  // index into FlatLevel::models, -1 when the model could not be found
    // Column-major 4x4 in the space above, translation in world units and already offset by the cell origin.
    std::array<float, 16> matrix{};
};

struct FlatBillboard {
    std::array<float, 3> position{};  // world units, cell offset applied
    int kind = 0;
};

struct FlatLevel {
    std::vector<FlatModel> models;       // one per distinct model that an instance uses and that was found
    std::vector<FlatInstance> instances;
    std::vector<FlatBillboard> billboards;
    std::uint32_t unresolved_instances = 0;
};

// Flat meshes of one model. `materials` is the table of the LM the model belongs to.
FlatModel flatten_model(const LmModel& model, const std::vector<LmMaterial>& materials);

// Models of the cell's own LM, instances, billboards. Models marked global are looked up by name in `global_plm`
// (the area's BG/<AREA>_GLB.PLM); pass nullptr when it is not available, those instances stay unresolved.
FlatLevel flatten_ipd(const IpdFile& ipd, const LmFile* global_plm);

// Flat meshes of every model of a stand-alone LM.
std::vector<FlatModel> flatten_plm(const LmFile& plm);

}  // namespace sh::convert::level
