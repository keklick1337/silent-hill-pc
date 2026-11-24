// SPDX-License-Identifier: GPL-3.0-or-later
// A test script for a screen (--setup-script, --import-script): one command per line, the verb and the rest of the
// line as its argument; blank lines and lines starting with '#' are left out. The verbs are each screen's own.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sh::screen_kit {

struct ScriptCommand {
    std::string verb;
    std::string arg;
    bool operator==(const ScriptCommand&) const = default;
};

std::vector<ScriptCommand> parse_script(std::string_view text);

}  // namespace sh::screen_kit
