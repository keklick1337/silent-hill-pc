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

SH_TEST(import_ui_picker_touch_empty_names_browse) {
    // A desktop: the typed path is missing.
    ImagePicker desktop(Lang::En);
    SH_CHECK(!desktop.submit());
    SH_CHECK(desktop.error() == format(Lang::En, Str::ErrEmpty));

    // A phone: nothing was chosen with the system's picker, and the error says what to tap.
    ImagePicker phone(Lang::En);
    phone.set_touch(true);
    SH_CHECK(!phone.submit());
    SH_CHECK(phone.state() == ImagePicker::State::Picking);
    SH_CHECK(phone.error() == format(Lang::En, Str::ErrEmptyTouch));
    SH_CHECK(phone.error().find("Browse") != std::string::npos);

    // Any other problem reads the same on both.
    phone.set_text((work_dir("picker_touch") / "missing.zip").string());
    SH_CHECK(!phone.submit());
    SH_CHECK(phone.error().find("not found") != std::string::npos);
}

SH_TEST(import_ui_picker_parses_the_activity_answers) {
    using Kind = PickAnswer::Kind;
    SH_CHECK(parse_pick_answer("").kind == Kind::Waiting);
    SH_CHECK(parse_pick_answer("-").kind == Kind::Cancelled);
    PickAnswer a = parse_pick_answer("~Silent Hill.zip");
    SH_CHECK(a.kind == Kind::Opening && a.text == "Silent Hill.zip");
    a = parse_pick_answer("!no activity");
    SH_CHECK(a.kind == Kind::NoDialog && a.text == "no activity");
    SH_CHECK(parse_pick_answer("!").text == "?");
    a = parse_pick_answer("?sh.zip");
    SH_CHECK(a.kind == Kind::Unreadable && a.text == "sh.zip");
    a = parse_pick_answer("/data/user/0/app/cache/picked/sh.zip");
    SH_CHECK(a.kind == Kind::Path && a.text == "/data/user/0/app/cache/picked/sh.zip");
}

SH_TEST(import_ui_picker_document_answers) {
    const fs::path dir = work_dir("picker_answers");
    const fs::path bin = write_file(dir / "game.bin", 20 * kRaw, true);
    ImagePicker picker(Lang::En);
    picker.set_touch(true);

    // While the chosen document is read there is a note, no error, and Import waits.
    picker.apply(parse_pick_answer("~game.bin"));
    SH_CHECK(picker.error().empty());
    SH_CHECK(picker.note().find("game.bin") != std::string::npos);
    SH_CHECK(!picker.submit());
    SH_CHECK(picker.error().empty() && !picker.note().empty());
    SH_CHECK(picker.state() == ImagePicker::State::Picking);

    // The path arrives: checked at once, the note goes, Import starts.
    picker.apply(parse_pick_answer(bin.string()));
    SH_CHECK(picker.note().empty() && picker.error().empty());
    SH_CHECK(picker.text() == bin.string());
    SH_CHECK(picker.submit());
    SH_CHECK(picker.image().has_value() && picker.image()->path == bin);

    // A cancelled pick keeps the file chosen before.
    ImagePicker kept(Lang::En);
    kept.set_touch(true);
    kept.choose(bin.string());
    kept.apply(parse_pick_answer("-"));
    SH_CHECK(kept.text() == bin.string() && kept.error().empty() && kept.note().empty());

    // A document that cannot be opened: the field is emptied, the error names the file and Browse.
    kept.apply(parse_pick_answer("~cloud.zip"));
    kept.apply(parse_pick_answer("?cloud.zip"));
    SH_CHECK(kept.text().empty() && kept.note().empty());
    SH_CHECK(kept.error() == format(Lang::En, Str::ErrDocumentUnreadable, {"cloud.zip"}));
    SH_CHECK(!kept.submit());
    SH_CHECK(kept.error() == format(Lang::En, Str::ErrEmptyTouch));

    // No picker on the device.
    kept.apply(parse_pick_answer("!none"));
    SH_CHECK(kept.error() == format(Lang::En, Str::ErrNoFileDialogTouch, {"none"}));

    // A failed import returns to the picker with the file still named, and without a stale note.
    ImagePicker failed(Lang::En, bin.string());
    failed.set_touch(true);
    SH_CHECK(failed.submit());
    failed.fail("import failed");
    SH_CHECK(failed.text() == bin.string() && failed.note().empty());
    SH_CHECK(failed.submit());
}
