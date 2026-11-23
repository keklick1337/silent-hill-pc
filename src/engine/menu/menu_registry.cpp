// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/menu/menu_model.h"

namespace sh::menu {

Registry& registry() {
    static Registry r;
    return r;
}

HostHooks& host() {
    static HostHooks h;
    return h;
}

}  // namespace sh::menu
