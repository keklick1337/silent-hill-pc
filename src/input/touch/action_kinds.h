// SPDX-License-Identifier: GPL-3.0-or-later
// What the touch screen's action button shows for the event in reach (gamedata/config/touch_actions.json): the
// kind of an event by the state it runs ("LoadRoom" is a door) or, for a map handler, by its name.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::input::touch {

enum class ActionKind { Act, Take, Read, Open, Use, Check };

// "act", "take", ...; nullopt-free: an unknown name is Act.
ActionKind action_kind_by_name(std::string_view name, bool* known = nullptr);

class ActionKinds {
public:
    struct Rule {
        std::string match;  // '*' stands for any run of characters
        ActionKind kind = ActionKind::Act;
    };

    static Result<ActionKinds> parse(std::string_view json);

    // The kind of an event: a handler that matches a rule takes the first such rule's kind, else the state's.
    ActionKind classify(std::string_view state, std::string_view handler) const;

    std::vector<std::pair<std::string, ActionKind>> states;
    std::vector<Rule> handlers;
};

// A '*' glob of the whole text.
bool glob_match(std::string_view pattern, std::string_view text);

}  // namespace sh::input::touch
