// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The item stage: the 3D models of the item screens (the carousel, the item in
// hand, the results screen's awards), of the world pickup and of the map5_s01 lock reels. Ten model places, each a
// frame (the model's matrix and position), a transform (scale, rotation, the script's translation) and the model it
// shows: one object of an item model file, drawn by the scene from the import (render/scene/item_model.h). A camera
// is a frame the reference view hangs from; the item screens and the pickup share one, the lock reels' map event has
// its own.

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "engine/camera/view_system.h"
#include "engine/math/geometry_state.h"

namespace sh::inventory {

inline constexpr int kStagePlaces = 10;

// A model's transform: scale per axis (Q12), rotation (Q12 angles, Y X Z order of the camera convention) and a
// translation the lock reels' script keeps (the draw does not use it).
struct ModelTransform {
    std::array<std::int32_t, 3> scale{4096, 4096, 4096};
    std::array<std::int16_t, 3> rotate{};
    std::array<std::int32_t, 3> translate{};
};

struct StagePlace {
    camera::FrameRecord frame{};  // local: rotation times scale; t: the position
    ModelTransform transform;
    bool framed = false;          // the frame was set up for this place (rotate and draw use it)
    bool shown = false;           // a model was linked here (the draw skips a place without one)
    std::string model;            // game path of the model file ("ITEM/IT_001.TMD"); "" none
    int object = -1;
    std::int32_t item = 0;        // the item the model shows (its "close_up_fix")
};

// A camera of the item views: its frame, the frame's rotation and the reference view looking down +z from it.
struct ItemCamera {
    camera::FrameRecord frame{};
    std::array<std::int16_t, 3> rotation{};
    camera::ReferenceView view{};
};

// How a place is drawn: the item screens' modes.
enum class StageDraw : int {
    Screen = 0,   // the inventory row and the item in hand, the lock reels
    Pickup = 2,   // the world pickup: its own table under the scene's
    Results = 3,  // the results screen's awards
};

class ItemStage {
public:
    StagePlace& place(int index) { return places_[static_cast<std::size_t>(index)]; }
    ItemCamera& camera() { return camera_; }

    // The camera 10240 units behind the origin looking down +z, its rotation cleared; the view is installed.
    void camera_init(ItemCamera& camera);
    // The camera turned by its rotation; the view is installed.
    void view_set(ItemCamera& camera);

    // Place `index` shows object `object` of `model` for `item` ("" model: nothing); the frame is reset to the
    // identity at the origin and the place is framed.
    void link(int index, const std::string& model, int object, int item);
    // Place `index` shows the map pack's model of `item`; false (nothing changes) when the pack has none.
    bool link_from_pack(int index, int item);
    // Nothing is shown at `index` (its frame stays).
    void unlink(int index);
    // The frame's matrix from the transform's rotation and scale (the position stays).
    void rotate(int index);
    // Draws place `index` with the view installed now.
    void draw(int index, StageDraw mode);

    // The item screens open: the projection distance becomes 1000 (the rotation, translation, distance and centre in
    // force are kept for close_projection) and the weapon in hand is noted.
    void open_projection();
    void close_projection();

    // The packed weapon attack when the item screens opened (the exit compares the new weapon with it).
    std::int32_t weapon_on_open = 0;
    // Bit per results award shown.
    std::uint8_t results_mask = 0;
    // The map's item pack list (the overlay's loadable items, 0-terminated) the item screens link from.
    std::array<std::int32_t, 48> pack{};
    // The model files the game read last: the map's item pack and the close-up model of the item picked up.
    std::string pack_model;
    std::string close_up_model;

private:
    std::array<StagePlace, kStagePlaces> places_{};
    ItemCamera camera_{};
    math::Transform saved_view_;     // the view and screen of the geometry state while the stage is open
    math::ScreenSetup saved_screen_;
};

ItemStage& item_stage();

}  // namespace sh::inventory
