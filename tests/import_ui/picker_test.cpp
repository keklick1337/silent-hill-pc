#include "framework/sh_test.h"
#include "import/ui/picker.h"
#include "import_ui/ui_test_util.h"

#include <string>

namespace {

namespace fs = std::filesystem;
using namespace sh::import::ui;
using namespace sh::import::ui::testing;

}  // namespace

SH_TEST(import_ui_picker_good_path_requests_import) {
    const fs::path bin = write_file(work_dir("picker_good") / "game.bin", 20 * kRaw, true);
    ImagePicker picker(Lang::En);
    SH_CHECK(picker.state() == ImagePicker::State::Picking);
    picker.set_text(bin.string());
    SH_CHECK(picker.submit());
    SH_CHECK(picker.state() == ImagePicker::State::ImportRequested);
    SH_CHECK(picker.error().empty());
    SH_CHECK(picker.image().has_value() && picker.image()->path == bin);
}

SH_TEST(import_ui_picker_bad_path_shows_error) {
    const fs::path dir = work_dir("picker_bad");
    ImagePicker picker(Lang::En);
    picker.set_text((dir / "missing.cue").string());
    SH_CHECK(!picker.submit());
    SH_CHECK(picker.state() == ImagePicker::State::Picking);
    SH_CHECK(!picker.error().empty());
    SH_CHECK(!picker.image().has_value());

    // A dropped file is checked at once but does not start the import by itself.
    const fs::path cooked = write_file(dir / "cooked.iso", 20 * 2048, false);
    picker.choose(cooked.string());
    SH_CHECK(picker.text() == cooked.string());
    SH_CHECK(picker.state() == ImagePicker::State::Picking);
    SH_CHECK(!picker.error().empty());

    const fs::path bin = write_file(dir / "game.bin", 20 * kRaw, true);
    picker.choose(bin.string());
    SH_CHECK(picker.error().empty());
    SH_CHECK(picker.state() == ImagePicker::State::Picking);
    SH_CHECK(picker.submit());
}

SH_TEST(import_ui_picker_failed_import_returns_to_picker) {
    const fs::path bin = write_file(work_dir("picker_fail") / "game.bin", 20 * kRaw, true);
    ImagePicker picker(Lang::En, bin.string(), "first error");
    SH_CHECK(picker.error() == "first error");
    SH_CHECK(picker.submit());
    picker.fail("import failed");
    SH_CHECK(picker.state() == ImagePicker::State::Picking);
    SH_CHECK(picker.error() == "import failed");
    SH_CHECK(picker.text() == bin.string());
    SH_CHECK(!picker.image().has_value());

    picker.dialog_failed("no portal");
    SH_CHECK(picker.error().find("no portal") != std::string::npos);
    picker.quit();
    SH_CHECK(picker.state() == ImagePicker::State::Quit);
    SH_CHECK(!picker.submit());
}
