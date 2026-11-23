// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Surface materials of the map (RENDER.md section 10): how rough, metallic and reflective a surface is, by the
// name of its texture page, a rectangle of that page (texture pages are atlases: a window and a wall share one), or
// a pictureless material. The data is one file per material, gamedata/materials/<Name>.json, over the defaults the
// import inferred (convert/surface_materials.json), patched by mods key by key; the engine hands the merged table
// over (set_material_source). A surface no entry names is neutral and reflects nothing, so the original look is the
// default.
#pragma once

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::json {
class Value;
}

namespace sh::scene {

struct CellMaterial;

// A rectangle of a texture page: x0, y0, x1, y1 in pixels of the imported picture (convert/textures/BG/X.png).
struct SurfaceRegion {
    std::string page;  // the page's stem ("DR0101F")
    std::array<float, 4> rect{0, 0, 0, 0};
};

struct SurfaceMaterial {
    std::string name;
    float roughness = 1.0f;          // 0 mirror .. 1 matte; rougher than the reflection limit reflects nothing
    float metalness = 0.0f;          // 0 dielectric .. 1 metal (the reflection takes the surface's colour)
    float reflective = 0.0f;         // 0..1: the strength of the mirror lobe on every face
    float wet = 0.0f;                // 0..1: the strength of the mirror lobe on faces turned up (puddles, wet floors)
    std::vector<std::string> match;  // texture page stems ("DR0101F") or pictureless material names; "X*" a prefix
    std::vector<SurfaceRegion> regions;  // parts of pages; they win over a whole page
    bool inferred = false;               // proposed by the import, not written by hand: the weakest match

    // What the shaders take: roughness, metalness, reflective, wet.
    std::array<float, 4> packed() const { return {roughness, metalness, reflective, wet}; }
};

// The material a map surface takes as a whole, or the neutral one. Among the entries that name it the most specific
// wins: an exact page before a prefix (the longer prefix first), a hand-written entry before an inferred one. The
// table is asked for on first use and again after forget_assets() (`mods reload`).
const SurfaceMaterial& surface_material(const CellMaterial& m);

// A region's material for the shaders: the rectangle normalised to the page and the packed values.
struct RegionMaterial {
    std::array<float, 4> rect;
    std::array<float, 4> surface;
};
// The regions of page `stem` (the first that holds a point wins), normalised by the page's size in pixels.
std::vector<RegionMaterial> surface_regions(std::string_view stem, int width, int height);
// The regions of the page at gamedata path `texture`, normalised by the imported picture's own size (an upscaled
// replacement keeps the UVs, so the rectangles stay in the import's pixels); empty when its size is unknown.
std::vector<RegionMaterial> surface_regions_of(const std::string& texture);

// The size in pixels of the imported picture at a gamedata path, before any replacement or upscaling.
using PictureSize = std::function<bool(const std::string& path, int* width, int* height)>;
void set_picture_size_query(PictureSize query);
// True when some material names a region of page `stem`.
bool has_surface_regions(std::string_view stem);

// The key a texture is matched by: "convert/textures/BG/DR0101F.p09.png" -> "DR0101F".
std::string_view texture_stem(std::string_view path);

// Where the table comes from: the engine merges the material files (gamedata, the import's inferred defaults, the
// mods). Without a source the table is empty.
using MaterialSource = std::function<std::vector<SurfaceMaterial>(std::vector<std::string>* errors)>;
void set_material_source(MaterialSource source);

// One material's keys over `inout` (keys absent from `object` keep their value): its name is set by the caller.
// False with a message in `errors` for a value out of range or a malformed key (unit tested).
bool apply_surface_material(const json::Value& object, SurfaceMaterial* inout, std::vector<std::string>* errors);
// A document of several: {"materials": {"<Name>": {...}, ...}} (the import's inferred file). Entries without a match
// or a region are rejected with a message.
std::vector<SurfaceMaterial> parse_surface_materials(std::string_view text, std::vector<std::string>* errors);

}  // namespace sh::scene
