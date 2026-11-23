// SPDX-License-Identifier: GPL-3.0-or-later
//
// The world objects drawn this frame (docs/RENDER.md section 6): map props, pickups and script objects
// are queued while the frame's logic runs and drawn once with the world. The queue keeps the geometry-space pose in
// the packed widths the game's draw list has (position 18/14/18 bits in 1/256 m, rotation 10/12/10 bits), so a
// request whose pose does not fit is stored wrapped and then never matches its duplicate: both are observable, both
// are kept. The model slot remembers which model library the object's name was found in.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace sh::world {

// A world object's model as the records that place it carry it: the 8-character name and the library it was found
// in (0: not looked up yet; 1 the item library, 2 the map's global library, 3 + n the cell in resident slot n).
struct ObjectModel {
    char name[8];
    // Wider than a byte: the cell stream has up to 256 resident slots, and a slot past 124 would wrap to a negative
    // library, so its objects (the Key of Woodman's cell on map2_s00) would never be drawn.
    std::int32_t library;

    std::string_view name_view() const;
    // A new model name (at most 8 characters are kept): the next draw looks it up again.
    void set_name(const char* text);
};

// The library of a model drawn from an item model file (ObjectItemModel) instead of a model library.
inline constexpr std::int32_t kItemModelLibrary = -1;

// An item model file a world object is drawn from ("ITEM/UNQ46.TMD", the converted glb's first model) and the
// size it is drawn at: the item models are made for the item camera, about ten times the world's scale.
struct ObjectItemModel {
    std::string file;
    double scale = 1.0;
};

// The item models of the map's world objects by object model name (world_objects.json "item_model"); a map load
// starts them over.
void object_item_models_clear();
void object_item_model_set(std::string_view name, ObjectItemModel model);
const ObjectItemModel* object_item_model(std::string_view name);

struct ObjectPosition {
    std::int32_t x, y, z;  // Q12 world units
};
struct ObjectRotation {
    std::int16_t x, y, z;  // Q12 angles
};

// One queued object as the draw reads it back: the stored geometry pose.
struct QueuedObject {
    ObjectModel* model = nullptr;
    std::int32_t x = 0, y = 0, z = 0;          // 1/256 m
    std::int16_t angle_x = 0, angle_y = 0, angle_z = 0;  // Q12 angles
};

class ObjectQueue {
public:
    static constexpr int kCapacity = 29;

    // Queues `model` at the pose unless the queue is full or the same model already stands there. `resolve` runs
    // when the model has no library yet; it returns the library (0: not found, the object is not queued).
    template <typename Resolve>
    void add(ObjectModel& model, const ObjectPosition& pos, const ObjectRotation& rot, Resolve&& resolve) {
        if (count_ >= kCapacity) return;
        if (model.library == 0) {
            const int library = resolve(model);
            if (library == 0) return;
            model.library = library;
        }
        push(model, pos, rot);
    }

    void clear() { count_ = 0; }
    int count() const { return count_; }
    QueuedObject at(int i) const;

    // Hands every queued object to `draw` in order and empties the queue.
    template <typename Draw>
    void drain(Draw&& draw) {
        for (int i = 0; i < count_; ++i) draw(at(i));
        count_ = 0;
    }

private:
    void push(ObjectModel& model, const ObjectPosition& pos, const ObjectRotation& rot);

    struct Packed {
        ObjectModel* model;
        std::int32_t x, y, z;     // stored 18, 14, 18 bits wide
        std::int32_t rx, ry, rz;  // stored 10, 12, 10 bits wide (x and z in quarter steps)
    };
    Packed items_[kCapacity] = {};
    int count_ = 0;
};

ObjectQueue& object_queue();

}  // namespace sh::world
