#pragma once

// The frame's world as the engine's scene renderer draws it (docs/RENDER.md section 6): the camera, the
// map light and fog from the atmosphere (sh::world::atmosphere()), the resident cells of the map, the world objects
// and the held item from the models of the converted glb files. Each drawn thing goes into the frame by order key
// (render/frame/frame.h), where the PS1 drawers put the same thing, so the effects and characters around it keep their
// order.

#include <cstdint>
#include <string_view>

namespace sh::world_scene {

// A model's frame in the world: Q12 rotation rows (model to world) and the world position (PS1 units).
struct Pose {
    std::int16_t m[3][3] = {};
    std::int32_t t[3] = {};
};

// Describes the frame's world: camera, light, the drawable cells (one scene entry in the farthest world slot) and
// their glow sprites. `shift` is the OT sort shift of the world drawers. False when the scene renderer is off.
bool draw_world(int shift);

// A world object whose model the game found in its model library `library` (1 the item library BG_ITEM, 2 the map's
// global library, 3 + n the cell in resident slot n), drawn from the same model of the area file. False when not drawn.
bool draw_object(int library, std::string_view name, const Pose& pose, int shift);
// Whether LM slot `library`'s library has the world object's model (the game re-resolves a missing one).
bool has_object(int library, std::string_view name);

// The held item: the first model of `model_file` ("ITEM/HANDGUN.PLM") from its converted glb, at the hand's pose.
bool draw_held_item(std::string_view model_file, const Pose& pose, int shift);

// A world object drawn from an item model file ("ITEM/UNQ46.TMD"): the first model of its converted glb at the
// pose, `scale` times its size, lit like any other world object.
bool draw_item_model(std::string_view model_file, const Pose& pose, double scale, int shift);

}  // namespace sh::world_scene
