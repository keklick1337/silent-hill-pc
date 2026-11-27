// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/carried_lights.h"

#include <utility>

namespace sh::world {

namespace {
std::vector<CarriedLight>& store() {
    static std::vector<CarriedLight> lights;
    return lights;
}
}  // namespace

void carry_light(CarriedLight light) {
    for (CarriedLight& l : store()) {
        if (l.owner == light.owner && l.id == light.id) {
            l = std::move(light);
            return;
        }
    }
    store().push_back(std::move(light));
}

bool drop_carried_lights(std::string_view owner, std::string_view id) {
    const std::size_t before = store().size();
    std::erase_if(store(), [&](const CarriedLight& l) { return l.owner == owner && (id.empty() || l.id == id); });
    return store().size() != before;
}

void clear_carried_lights() { store().clear(); }

const std::vector<CarriedLight>& carried_lights() { return store(); }

}  // namespace sh::world
