#include "framework/sh_test.h"
#include "sh-import/progress.h"
#include "sh-import/run.h"
#include "sh-import/summary.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace {

namespace fs = std::filesystem;
using namespace sh::import_tool;

struct RunResult {
    int code;
    std::string out;
    std::string err;
};

RunResult run_tool(std::vector<std::string> args) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run(args, out, err, nullptr);
    return {code, out.str(), err.str()};
}

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(SH_PROJECT_ROOT) / ".tmp" / "integrate-import" / "tests" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void write_text(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

}  // namespace

SH_TEST(import_tool_run_help_and_usage_errors) {
    const RunResult help = run_tool({"--help"});
    SH_CHECK(help.code == 0);
    SH_CHECK(help.out.find("usage: sh-import") != std::string::npos);
    SH_CHECK(help.err.empty());

    const RunResult unknown = run_tool({"--no-such-option"});
    SH_CHECK(unknown.code == 2);
    SH_CHECK(unknown.err.find("--no-such-option") != std::string::npos);

    const RunResult no_image = run_tool({"--out", "x"});
    SH_CHECK(no_image.code == 2);
    SH_CHECK(no_image.err.find("no image") != std::string::npos);
}

SH_TEST(import_tool_run_config_file_problems) {
    const fs::path dir = scratch("config_problems");
    const RunResult missing = run_tool({"--config", (dir / "absent.cfg").string(), "disc.cue"});
    SH_CHECK(missing.code == 2);

    write_text(dir / "bad.cfg", "import_threads = lots\n");
    const RunResult bad = run_tool({"--config", (dir / "bad.cfg").string(), "disc.cue"});
    SH_CHECK(bad.code == 2);
    SH_CHECK(bad.err.find("line 1") != std::string::npos);

    // An unknown key is a warning, not a failure: the run goes on and fails later, on the missing image.
    write_text(dir / "warn.cfg", "render_scale = 2\nimport_image = " + (dir / "no_such.cue").string() + "\ngamedata_dir = " +
                                     (dir / "out").string() + "\n");
    const RunResult warn = run_tool({"--config", (dir / "warn.cfg").string()});
    SH_CHECK(warn.code == 1);
    SH_CHECK(warn.err.find("warning") != std::string::npos && warn.err.find("render_scale") != std::string::npos);
    SH_CHECK(warn.err.find("no_such.cue") != std::string::npos);
    SH_CHECK(!fs::exists(dir / "out"));  // a failed import leaves nothing
}

SH_TEST(import_tool_run_real_image_text_only) {
    const fs::path image = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).cue";
    if (!fs::exists(image)) {
        SH_SKIP("data/Silent Hill (v1.1).cue is not present");
    }
    const fs::path dir = scratch("real_text_only");
    const std::string out_dir = (dir / "gamedata").string();

    const RunResult first = run_tool({image.string(), "--out", out_dir, "--only", "text"});
    SH_CHECK(first.code == 0);
    SH_CHECK(first.out.find("text") != std::string::npos);
    SH_CHECK(fs::exists(dir / "gamedata" / "version.cfg"));
    SH_CHECK(fs::exists(dir / "gamedata" / "index.tsv"));
    // The text group is the game font: the English text is gamedata/text/en of the repository, not imported.
    SH_CHECK(fs::exists(dir / "gamedata" / "convert" / "fonts" / "game.font.json"));
    SH_CHECK(!fs::exists(dir / "gamedata" / "convert" / "text"));
    SH_CHECK(!fs::exists(dir / "gamedata" / "convert" / "textures"));

    // Again over the existing directory: refused without --overwrite, allowed with it.
    const RunResult refused = run_tool({image.string(), "--out", out_dir, "--only", "text"});
    SH_CHECK(refused.code == 1);
    SH_CHECK(refused.err.find("import_overwrite") != std::string::npos);
    const RunResult again = run_tool({image.string(), "--out", out_dir, "--only", "text", "--overwrite", "--no-verify"});
    SH_CHECK(again.code == 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

SH_TEST(import_tool_progress_format) {
    SH_CHECK(format_duration(0) == "0:00");
    SH_CHECK(format_duration(65) == "1:05");
    SH_CHECK(format_duration(3723) == "1:02:03");
    SH_CHECK(format_duration(-1) == "-");

    SH_CHECK(format_progress_line("convert", 0.5, 60) == "[convert]  50.0%  elapsed 1:00  eta 1:00");
    SH_CHECK(format_progress_line("raw", 0.0, 0).find("eta ?") != std::string::npos);
    SH_CHECK(format_progress_line("read", 0.001, 30).find("eta ?") != std::string::npos);  // too early to tell
    SH_CHECK(format_progress_line("commit", 1.0, 125) == "[commit] 100.0%  elapsed 2:05  eta 0:00");
    SH_CHECK(format_progress_line("raw", 7.0, 10).find("100.0%") != std::string::npos);  // clamped
}

SH_TEST(import_tool_summary_sizes) {
    SH_CHECK(format_bytes(980) == "980 B");
    SH_CHECK(format_bytes(1536) == "1.5 KiB");
    SH_CHECK(format_bytes(5ull << 20) == "5.0 MiB");

    const fs::path dir = scratch("summary_sizes");
    fs::create_directories(dir / "a" / "deep");
    fs::create_directories(dir / "b");
    write_text(dir / "top.txt", "12345");
    write_text(dir / "a" / "one", "1234");
    write_text(dir / "a" / "deep" / "two", "123456");
    write_text(dir / "b" / "three", "1");

    const std::vector<DirSize> sizes = directory_sizes(dir);
    SH_CHECK(sizes.size() == 3);
    SH_CHECK(sizes[0].name == "a" && sizes[0].files == 2 && sizes[0].bytes == 10);
    SH_CHECK(sizes[1].name == "b" && sizes[1].files == 1 && sizes[1].bytes == 1);
    SH_CHECK(sizes[2].name == "(files)" && sizes[2].files == 1 && sizes[2].bytes == 5);
    SH_CHECK(directory_sizes(dir / "absent").empty());

    sh::import::Report report;
    report.raw_files = 3;
    report.failures.push_back({"CHARA/X.TIM", "tim", "bad header"});
    sh::import::ConverterStats stats;
    stats.name = "tim";
    stats.lossless = true;
    stats.accepted = 2;
    stats.round_trips = 2;
    report.converters.push_back(stats);
    const std::string text = format_summary(report, sizes, {}, 61);
    SH_CHECK(text.find("CHARA/X.TIM [tim]: bad header") != std::string::npos);
    SH_CHECK(text.find("round trips 2") != std::string::npos);
    SH_CHECK(text.find("raw/ by directory") != std::string::npos);
    SH_CHECK(text.find("convert/ by directory") == std::string::npos);  // empty table is left out
    SH_CHECK(text.find("time:           1:01") != std::string::npos);

    std::error_code ec;
    fs::remove_all(dir, ec);
}
