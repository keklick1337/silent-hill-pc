#include "framework/sh_test.h"
#include "import/ui/console.h"
#include "import_ui/ui_test_util.h"

#include <atomic>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace {

namespace fs = std::filesystem;
using namespace sh::import::ui;
using namespace sh::import::ui::testing;
using namespace std::chrono_literals;

using sh::Result;
using sh::import::ProgressFn;
using sh::import::Request;

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

Controller::Options options(const fs::path& gamedata, Controller::ImportFn fn, Lang lang = Lang::En,
                            std::string hint = {}) {
    Controller::Options o;
    o.import = std::move(fn);
    o.gamedata_dir = gamedata;
    o.image_hint = std::move(hint);
    o.lang = lang;
    return o;
}

Result<void> quick_import(const Request&, const ProgressFn& progress) {
    progress("read", 0.0);
    progress("raw", 0.5);
    progress("commit", 1.0);
    return Result<void>::ok();
}

}  // namespace

SH_TEST(import_ui_console_asks_then_imports) {
    const fs::path dir = work_dir("con_ok");
    const fs::path image = write_file(dir / "g.bin", 4 * kRaw, true);
    Controller c(options(dir / "gamedata", quick_import));
    std::istringstream in(image.string() + "\n");
    std::ostringstream out;
    SH_CHECK(run_console(c, in, out) == kConsoleOk);
    const std::string text = out.str();
    SH_CHECK(contains(text, "First run: game import"));
    SH_CHECK(contains(text, "Image path"));
    SH_CHECK(contains(text, "Import finished"));
    SH_CHECK(c.phase() == Phase::Done);
}

SH_TEST(import_ui_console_prints_progress_lines) {
    const fs::path dir = work_dir("con_progress");
    const fs::path image = write_file(dir / "g.bin", 4 * kRaw, true);
    // The steps are spaced out so that the polling loop sees each of them.
    Controller c(options(dir / "gamedata", [](const Request&, const ProgressFn& progress) {
        for (auto [stage, fraction] : {std::pair{"read", 0.0}, {"raw", 0.5}, {"commit", 1.0}}) {
            progress(stage, fraction);
            std::this_thread::sleep_for(60ms);
        }
        return Result<void>::ok();
    }));
    std::istringstream in(image.string() + "\n");
    std::ostringstream out;
    ConsoleOptions co;
    co.poll = 5ms;
    SH_CHECK(run_console(c, in, out, co) == kConsoleOk);
    const std::string text = out.str();
    SH_CHECK(contains(text, "[  0%] Reading the image"));
    SH_CHECK(contains(text, "[ 50%] Extracting game files, time estimate is not available yet"));
    SH_CHECK(contains(text, "[100%] Finishing"));
}

SH_TEST(import_ui_console_empty_answer_takes_hint) {
    const fs::path dir = work_dir("con_hint");
    const fs::path image = write_file(dir / "g.bin", 4 * kRaw, true);
    fs::path used;
    Controller c(options(
        dir / "gamedata",
        [&](const Request& request, const ProgressFn& p) {
            used = request.image;
            return quick_import(request, p);
        },
        Lang::En, image.string()));
    std::istringstream in("\n");
    std::ostringstream out;
    SH_CHECK(run_console(c, in, out) == kConsoleOk);
    SH_CHECK(used == image);
    SH_CHECK(contains(out.str(), "hint: " + image.string()));
}

SH_TEST(import_ui_console_reasks_after_bad_path) {
    const fs::path dir = work_dir("con_bad");
    const fs::path cooked = write_file(dir / "c.iso", 9 * kCooked, false);
    const fs::path image = write_file(dir / "g.bin", 4 * kRaw, true);
    SH_CHECK(load_translation(fs::path(SH_PROJECT_ROOT) / "gamedata" / "text", "ru"));
    Controller c(options(dir / "gamedata", quick_import, Lang::Local));
    std::istringstream in("\n" + (dir / "none.bin").string() + "\n  \"" + cooked.string() + "\"\n" + image.string() + "\n");
    std::ostringstream out;
    SH_CHECK(run_console(c, in, out) == kConsoleOk);
    const std::string text = out.str();
    SH_CHECK(contains(text, "Путь к образу не указан."));
    SH_CHECK(contains(text, "Файл не найден"));
    SH_CHECK(contains(text, "обычный .iso"));
    SH_CHECK(contains(text, "Импорт завершён"));
}

SH_TEST(import_ui_console_quit_and_eof) {
    const fs::path dir = work_dir("con_quit");
    {
        Controller c(options(dir / "gamedata", quick_import));
        std::istringstream in("q\n");
        std::ostringstream out;
        SH_CHECK(run_console(c, in, out) == kConsoleCancelled);
        SH_CHECK(c.phase() == Phase::Aborted);
    }
    {
        Controller c(options(dir / "gamedata", quick_import));
        std::istringstream in("");
        std::ostringstream out;
        SH_CHECK(run_console(c, in, out) == kConsoleCancelled);
    }
}

SH_TEST(import_ui_console_image_argument_skips_question) {
    const fs::path dir = work_dir("con_arg");
    const fs::path image = write_file(dir / "g.bin", 4 * kRaw, true);
    Controller c(options(dir / "gamedata", quick_import));
    std::istringstream in("");
    std::ostringstream out;
    ConsoleOptions co;
    co.image_arg = image.string();
    SH_CHECK(run_console(c, in, out, co) == kConsoleOk);
    SH_CHECK(!contains(out.str(), "Image path:"));

    // A bad argument falls back to asking, and the input ends there.
    Controller c2(options(dir / "gamedata", quick_import));
    std::ostringstream out2;
    co.image_arg = (dir / "none.bin").string();
    SH_CHECK(run_console(c2, in, out2, co) == kConsoleCancelled);
    SH_CHECK(contains(out2.str(), "File not found"));
}

SH_TEST(import_ui_console_failure_exit_code) {
    const fs::path dir = work_dir("con_fail");
    const fs::path image = write_file(dir / "g.bin", 4 * kRaw, true);
    Controller c(options(dir / "gamedata", [](const Request&, const ProgressFn&) {
        return Result<void>::fail("unsupported game version");
    }));
    std::istringstream in(image.string() + "\n");
    std::ostringstream out;
    SH_CHECK(run_console(c, in, out) == kConsoleFailed);
    SH_CHECK(contains(out.str(), "Import failed"));
    SH_CHECK(contains(out.str(), "unsupported game version"));
}

SH_TEST(import_ui_console_ready_needs_no_input) {
    const fs::path dir = work_dir("con_ready");
    fs::create_directories(dir / "gamedata");
    write_text(dir / "gamedata" / "version.cfg", "importer = 1\n");
    Controller c(options(dir / "gamedata", nullptr));
    std::istringstream in("");
    std::ostringstream out;
    SH_CHECK(run_console(c, in, out) == kConsoleOk);
    SH_CHECK(contains(out.str(), "already in"));
}

SH_TEST(import_ui_console_interrupt_cancels) {
    const fs::path dir = work_dir("con_int");
    const fs::path image = write_file(dir / "g.bin", 4 * kRaw, true);
    std::atomic<bool> interrupt{false};
    Controller c(options(dir / "gamedata", [&](const Request& request, const ProgressFn& progress) {
        for (int i = 0; i < 10000; ++i) {
            if (request.cancel->load()) return Result<void>::fail("cancelled");
            progress("raw", i / 10000.0);
            if (i == 3) interrupt = true;
            std::this_thread::sleep_for(1ms);
        }
        return Result<void>::ok();
    }));
    std::istringstream in(image.string() + "\n");
    std::ostringstream out;
    ConsoleOptions co;
    co.interrupted = [&] { return interrupt.load(); };
    co.poll = 5ms;
    SH_CHECK(run_console(c, in, out, co) == kConsoleCancelled);
    SH_CHECK(contains(out.str(), "Import cancelled"));
}
