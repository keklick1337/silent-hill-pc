// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/world/held_item.h"

namespace sh::world {

HeldItemState& held_item() {
    static HeldItemState state;
    return state;
}

}  // namespace sh::world
