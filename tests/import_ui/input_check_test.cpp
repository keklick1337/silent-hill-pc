#include "framework/sh_test.h"
#include "import/ui/input_check.h"
#include "import/ui/strings.h"
#include "import_ui/ui_test_util.h"

#include <string>

namespace {

namespace fs = std::filesystem;
using namespace sh::import::ui;
using namespace sh::import::ui::testing;

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// The Russian of the base data: Cyrillic expected below is the translation, read the way the game reads it.
bool load_text(const char* locale) {
    return load_translation(fs::path(SH_PROJECT_ROOT) / "gamedata" / "text", locale);
}

}  // namespace

SH_TEST(import_ui_check_raw_bin_accepted) {
    const fs::path dir = work_dir("check_raw_bin");
    const fs::path bin = write_file(dir / "game.bin", 20 * kRaw, true);
    const ImageCheck check = check_image(bin.string());
    SH_CHECK(check.ok());
    SH_CHECK(check.size == 20 * kRaw);
    SH_CHECK(check.path == bin);
    SH_CHECK(describe(Lang::En, check).empty());
}

SH_TEST(import_ui_check_raw_iso_accepted) {
    const fs::path dir = work_dir("check_raw_iso");
    SH_CHECK(check_image(write_file(dir / "game.iso", 17 * kRaw, true).string()).ok());
}

SH_TEST(import_ui_check_cue_follows_bin) {
    const fs::path dir = work_dir("check_cue");
    write_file(dir / "Silent Hill (v1.1).bin", 30 * kRaw, true);
    const fs::path cue = write_text(dir / "game.CUE",
                                    "\xEF\xBB\xBF" "FILE \"Silent Hill (v1.1).bin\" BINARY\r\n"
                                    "  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    const ImageCheck check = check_image(cue.string());
    SH_CHECK(check.ok());
    SH_CHECK(check.path == cue);
    SH_CHECK(check.bin == dir / "Silent Hill (v1.1).bin");
    SH_CHECK(check.size == 30 * kRaw);

    write_text(dir / "plain.cue", "file track.bin BINARY\nTRACK 01 MODE2/2352\n");
    write_file(dir / "track.bin", 5 * kRaw, true);
    SH_CHECK(check_image((dir / "plain.cue").string()).ok());
}

SH_TEST(import_ui_check_cue_problems) {
    const fs::path dir = work_dir("check_cue_bad");
    const fs::path missing = write_text(dir / "missing.cue", "FILE \"gone.bin\" BINARY\n");
    ImageCheck check = check_image(missing.string());
    SH_CHECK(check.problem == Problem::CueTargetMissing);
    SH_CHECK(contains(describe(Lang::En, check), "gone.bin"));

    const fs::path empty = write_text(dir / "empty.cue", "REM nothing here\nTRACK 01 MODE2/2352\n");
    SH_CHECK(check_image(empty.string()).problem == Problem::CueNoFile);

    // The cue is followed to a plain ISO: refused for the ISO, not for the cue.
    write_file(dir / "cooked.iso", 40 * kCooked, false);
    const fs::path to_iso = write_text(dir / "cooked.cue", "FILE \"cooked.iso\" BINARY\n");
    SH_CHECK(check_image(to_iso.string()).problem == Problem::CookedIso);
}

SH_TEST(import_ui_check_plain_iso_refused) {
    const fs::path dir = work_dir("check_cooked");
    const fs::path iso = write_file(dir / "game.iso", 64 * kCooked, false);
    const ImageCheck check = check_image(iso.string());
    SH_CHECK(check.problem == Problem::CookedIso);
    SH_CHECK(load_text("ru"));
    const std::string ru = describe(Lang::Local, check);
    const std::string en = describe(Lang::En, check);
    SH_CHECK(contains(ru, "2048"));
    SH_CHECK(contains(en, "2048"));
    SH_CHECK(ru != en);
}

SH_TEST(import_ui_check_size_both_multiples_decided_by_sync) {
    // 301056 = lcm(2048, 2352): only the sync pattern tells the layouts apart.
    const fs::path dir = work_dir("check_lcm");
    constexpr std::size_t kBoth = 301056;
    SH_CHECK(check_image(write_file(dir / "raw.bin", kBoth, true).string()).ok());
    SH_CHECK(check_image(write_file(dir / "cooked.iso", kBoth, false).string()).problem == Problem::CookedIso);
}

SH_TEST(import_ui_check_wrong_sector_size) {
    const fs::path dir = work_dir("check_size");
    // Sync present, but a truncated last sector.
    const fs::path truncated = write_file(dir / "cut.bin", 20 * kRaw + 100, true);
    ImageCheck check = check_image(truncated.string());
    SH_CHECK(check.problem == Problem::BadSectorSize);
    SH_CHECK(contains(describe(Lang::En, check), std::to_string(20 * kRaw + 100)));
    // MODE2/2336 dump: no sync, size is a multiple of 2336 only.
    SH_CHECK(check_image(write_file(dir / "xa.bin", 21 * 2336, false).string()).problem == Problem::BadSectorSize);
}

SH_TEST(import_ui_check_not_a_disc_image) {
    const fs::path dir = work_dir("check_other");
    SH_CHECK(check_image(write_file(dir / "SLUS_007.07", 1000003, false).string()).problem == Problem::NotRawImage);
    SH_CHECK(check_image(write_file(dir / "tiny.bin", 5, false).string()).problem == Problem::NotRawImage);
}

SH_TEST(import_ui_check_missing_dir_empty) {
    const fs::path dir = work_dir("check_basic");
    SH_CHECK(check_image("").problem == Problem::Empty);
    SH_CHECK(check_image("   ").problem == Problem::Empty);
    SH_CHECK(check_image("\"\"").problem == Problem::Empty);

    ImageCheck check = check_image((dir / "nope.bin").string());
    SH_CHECK(check.problem == Problem::NotFound);
    SH_CHECK(contains(describe(Lang::Local, check), "nope.bin"));

    check = check_image(dir.string());
    SH_CHECK(check.problem == Problem::IsDirectory);
    SH_CHECK(!describe(Lang::En, check).empty());

    SH_CHECK(check_image(write_file(dir / "zero.bin", 0, false).string()).problem == Problem::EmptyFile);

    // A path through a regular file does not exist either.
    SH_CHECK(check_image((dir / "zero.bin" / "x").string()).problem != Problem::None);
}

SH_TEST(import_ui_check_path_text_is_normalised) {
    const fs::path dir = work_dir("check_text");
    const fs::path bin = write_file(dir / "my image.bin", 3 * kRaw, true);
    SH_CHECK(check_image("  \"" + bin.string() + "\"  ").ok());
    SH_CHECK(check_image("'" + bin.string() + "'").ok());
    SH_CHECK(check_image((dir / "." / "my image.bin").string()).path == bin);
    SH_CHECK(normalize_path_text("").empty());
}

SH_TEST(import_ui_strings_complete) {
    // The base data's English is the built-in one, string for string.
    SH_CHECK(load_text("en"));
    for (int i = 0; i < static_cast<int>(Str::Count_); ++i) {
        SH_CHECK(text(Lang::Local, static_cast<Str>(i)) == text(Lang::En, static_cast<Str>(i)));
    }
    SH_CHECK(load_text("ru_RU.UTF-8"));
    for (int i = 0; i < static_cast<int>(Str::Count_); ++i) {
        const Str key = static_cast<Str>(i);
        SH_CHECK(!text(Lang::Local, key).empty());
        SH_CHECK(!text(Lang::En, key).empty());
        SH_CHECK(text(Lang::Local, key) != text(Lang::En, key));
    }
}

SH_TEST(import_ui_strings_format) {
    SH_CHECK(format(Lang::En, Str::ErrNotFound, {"a/b"}) == "File not found: a/b");
    SH_CHECK(load_text("ru"));
    SH_CHECK(format(Lang::Local, Str::ErrNotFound, {"a/b"}) == "Файл не найден: a/b");
    // A missing argument leaves the placeholder visible instead of failing.
    SH_CHECK(format(Lang::En, Str::ErrNotFound, {}) == "File not found: {0}");
    SH_CHECK(lang_from_code("ru_RU.UTF-8") == Lang::Local);
    SH_CHECK(lang_from_code("RU") == Lang::Local);
    SH_CHECK(lang_from_code("en_US") == Lang::En);
    SH_CHECK(lang_from_code("") == Lang::En);
    SH_CHECK(lang_from_code("C.UTF-8") == Lang::En);
    SH_CHECK(language_code("pt_BR.UTF-8") == "pt");
    SH_CHECK(!load_translation(fs::path(SH_PROJECT_ROOT) / "gamedata" / "text", "xx"));
}

SH_TEST(import_ui_strings_duration) {
    SH_CHECK(format_duration(Lang::En, 7.2) == "7 s");
    SH_CHECK(format_duration(Lang::En, 130) == "2 min 10 s");
    SH_CHECK(load_text("ru"));
    SH_CHECK(format_duration(Lang::Local, 130) == "2 мин 10 с");
    SH_CHECK(format_duration(Lang::En, 3 * 3600 + 5 * 60 + 9) == "3 h 5 min");
    SH_CHECK(format_duration(Lang::En, -4) == "0 s");
}
