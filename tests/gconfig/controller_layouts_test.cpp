#include "framework/sh_test.h"

#include "engine/gconfig/controller_layouts.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sh::gconfig::ControllerLayoutLibrary;

SH_TEST(controller_layouts_shipped_file_loads) {
    std::vector<std::string> warnings;
    const auto lib = ControllerLayoutLibrary::load(fs::path(SH_PROJECT_ROOT) / "gamedata", {}, warnings);
    SH_CHECK(static_cast<bool>(lib));
    const auto* usa = lib.value().region("usa");
    SH_CHECK(usa != nullptr);
    // Type A: enter = Start | Cross, aim = R2; type B swaps the shoulder buttons.
    SH_CHECK(((*usa)[0].masks[0]) == (0x0008u | 0x4000u));
    SH_CHECK(((*usa)[0].masks[4]) == (0x0200u));
    SH_CHECK(((*usa)[1].masks[4]) == (0x0800u));
    SH_CHECK(((*usa)[2].masks[11]) == (0x1000u));  // type C: item = Triangle
}

SH_TEST(controller_layouts_rejects_bad_data) {
    SH_CHECK(!static_cast<bool>(ControllerLayoutLibrary::parse("{}")));
    SH_CHECK(!static_cast<bool>(ControllerLayoutLibrary::parse("{\"layouts\": {\"usa\": []}}")));
}
