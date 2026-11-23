// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/world/object_queue.h"

#include <cstring>
#include <map>
#include <utility>

namespace sh::world {

namespace {

// The value a signed field `bits` wide reads back after `v` was stored in it.
constexpr std::int32_t wrap(std::int32_t v, int bits) {
    const int drop = 32 - bits;
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << drop) >> drop;
}

}  // namespace

std::string_view ObjectModel::name_view() const { return std::string_view(name, strnlen(name, sizeof(name))); }

void ObjectModel::set_name(const char* text) {
    library = 0;
    std::memset(name, 0, sizeof(name));
    for (std::size_t i = 0; text != nullptr && i < sizeof(name) && text[i] != '\0'; ++i) name[i] = text[i];
}

void ObjectQueue::push(ObjectModel& model, const ObjectPosition& pos, const ObjectRotation& rot) {
    // Geometry units: 1/256 m for the position, quarter steps for the x and z angles.
    const std::int32_t x = pos.x >> 4;
    const std::int32_t y = pos.y >> 4;
    const std::int32_t z = pos.z >> 4;
    const std::int32_t rx = rot.x >> 2;
    const std::int32_t ry = rot.y;
    const std::int32_t rz = rot.z >> 2;

    // The comparison is against the stored (wrapped) values, as the draw list compares.
    for (int i = 0; i < count_; ++i) {
        const Packed& o = items_[i];
        if (o.model == &model && o.x == x && o.z == z && o.y == y && o.rx == rx && o.ry == ry && o.rz == rz) return;
    }
    items_[count_++] = {&model, wrap(x, 18), wrap(y, 14), wrap(z, 18), wrap(rx, 10), wrap(ry, 12), wrap(rz, 10)};
}

QueuedObject ObjectQueue::at(int i) const {
    const Packed& o = items_[i];
    QueuedObject q;
    q.model = o.model;
    q.x = o.x;
    q.y = o.y;
    q.z = o.z;
    q.angle_x = static_cast<std::int16_t>(o.rx * 4);
    q.angle_y = static_cast<std::int16_t>(o.ry);
    q.angle_z = static_cast<std::int16_t>(o.rz * 4);
    return q;
}

namespace {

std::map<std::string, ObjectItemModel, std::less<>>& item_models() {
    static std::map<std::string, ObjectItemModel, std::less<>> models;
    return models;
}

}  // namespace

void object_item_models_clear() { item_models().clear(); }

void object_item_model_set(std::string_view name, ObjectItemModel model) {
    item_models().insert_or_assign(std::string(name.substr(0, 8)), std::move(model));
}

const ObjectItemModel* object_item_model(std::string_view name) {
    const auto it = item_models().find(name);
    return it == item_models().end() ? nullptr : &it->second;
}

ObjectQueue& object_queue() {
    static ObjectQueue queue;
    return queue;
}

}  // namespace sh::world
