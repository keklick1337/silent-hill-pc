#include "framework/sh_test.h"

#include "engine/log/log.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Captured
{
    int category;
    int level;
    std::string line;
};

std::vector<Captured>& captured()
{
    static std::vector<Captured> lines;
    return lines;
}

void capture(int category, int level, const char* line)
{
    captured().push_back({category, level, line});
}

// Puts the journal into a known state: terminal off, nothing queued.
void reset_journal()
{
    ShLog_SetTerminalStream(nullptr);
    ShLog_ApplySpec("*=info");
    ShLog_SetConsoleLevel(SH_LOGLV_INFO);
    ShLog_ConsoleDrain([](const char*, int, int) {});
    ShLog_SetListener(capture);
    captured().clear();
}

int g_evaluated = 0;

int side_effect()
{
    return ++g_evaluated;
}

}  // namespace

SH_TEST(log_levels_and_categories_parse_with_aliases)
{
    SH_CHECK(ShLog_ParseLevel("trace") == SH_LOGLV_TRACE);
    SH_CHECK(ShLog_ParseLevel("WARN") == SH_LOGLV_WARN);
    SH_CHECK(ShLog_ParseLevel("warning") == SH_LOGLV_WARN);
    SH_CHECK(ShLog_ParseLevel("off") == SH_LOGLV_OFF);
    SH_CHECK(ShLog_ParseLevel("loud") == -1);
    SH_CHECK(ShLog_ParseCategory("texture") == SH_LOGCAT_TEXTURE);
    SH_CHECK(ShLog_ParseCategory("TEX") == SH_LOGCAT_TEXTURE);
    SH_CHECK(ShLog_ParseCategory("chara") == SH_LOGCAT_SPAWN);
    SH_CHECK(ShLog_ParseCategory("xa") == SH_LOGCAT_AUDIO);
    SH_CHECK(ShLog_ParseCategory("vfs") == SH_LOGCAT_FS);
    SH_CHECK(ShLog_ParseCategory("overlay") == SH_LOGCAT_MAP);
    SH_CHECK(ShLog_ParseCategory("nope") == -1);
}

SH_TEST(log_legacy_tags_pick_the_category)
{
    SH_CHECK(ShLog_CategoryFromText("[POOL] chara 4") == SH_LOGCAT_SPAWN);
    SH_CHECK(ShLog_CategoryFromText("[POOLTEX] slot") == SH_LOGCAT_TEXTURE);
    SH_CHECK(ShLog_CategoryFromText("[FSQ-CORRUPT] entry") == SH_LOGCAT_FS);
    SH_CHECK(ShLog_CategoryFromText("[LOOSE/HIRES] x") == SH_LOGCAT_TEXTURE);
    SH_CHECK(ShLog_CategoryFromText("[LOOSE/MISS] x") == SH_LOGCAT_FS);
    SH_CHECK(ShLog_CategoryFromText("  [MapOverlay]   Nulling") == SH_LOGCAT_MAP);
    SH_CHECK(ShLog_CategoryFromText("[XATIME] t") == SH_LOGCAT_AUDIO);
    SH_CHECK(ShLog_CategoryFromText("[GLYPHS] a") == SH_LOGCAT_TEXTURE);
    SH_CHECK(ShLog_CategoryFromText("[GL] b") == SH_LOGCAT_RENDER);
    SH_CHECK(ShLog_CategoryFromText("[FMV] c") == SH_LOGCAT_FMV);
    SH_CHECK(ShLog_CategoryFromText("[SH] d") == SH_LOGCAT_GAME);
    SH_CHECK(ShLog_CategoryFromText("no tag") == SH_LOGCAT_GAME);
    SH_CHECK(ShLog_CategoryFromText("[unterminated") == SH_LOGCAT_GAME);
}

SH_TEST(log_spec_sets_levels_per_category)
{
    reset_journal();
    SH_CHECK(ShLog_ApplySpec("texture=trace,spawn=debug,*=warn") == 0);
    // "*" applies to every category, including the ones named before it.
    SH_CHECK(ShLog_CategoryLevel(SH_LOGCAT_TEXTURE) == SH_LOGLV_WARN);
    SH_CHECK(ShLog_ApplySpec("*=info, texture=trace; spawn:debug") == 0);
    SH_CHECK(ShLog_CategoryLevel(SH_LOGCAT_TEXTURE) == SH_LOGLV_TRACE);
    SH_CHECK(ShLog_CategoryLevel(SH_LOGCAT_SPAWN) == SH_LOGLV_DEBUG);
    SH_CHECK(ShLog_CategoryLevel(SH_LOGCAT_FMV) == SH_LOGLV_INFO);
    SH_CHECK(ShLog_ApplySpec("bogus=info,fmv=loud,model") == 3);
    SH_CHECK(g_ShLogGate[SH_LOGCAT_TEXTURE] == SH_LOGLV_TRACE);
    SH_CHECK(g_ShLogGateAny == SH_LOGLV_TRACE);
    reset_journal();
}

SH_TEST(log_disabled_message_is_not_evaluated)
{
    reset_journal();
    g_evaluated = 0;
    SH_LOG_DEBUG(TEXTURE, "value %d", side_effect());
    SH_DBG("[POOL] value %d", side_effect());
    SH_CHECK(g_evaluated == 0);
    SH_CHECK(captured().empty());

    ShLog_SetLevel(SH_LOGCAT_TEXTURE, SH_LOGLV_DEBUG);
    SH_LOG_DEBUG(TEXTURE, "value %d", side_effect());
    SH_DBG("[POOL] value %d", side_effect());  // spawn stays at info
    SH_CHECK(g_evaluated == 1);
    SH_CHECK(captured().size() == 1);
    SH_CHECK(captured()[0].category == SH_LOGCAT_TEXTURE);
    SH_CHECK(captured()[0].line.find("value 1") != std::string::npos);
    reset_journal();
}

SH_TEST(log_legacy_macros_keep_their_levels)
{
    reset_journal();
    SH_LOG("[XA] started %d", 7);
    SH_WARN("[POOL] full");
    SH_ERR("[FSQ] broken");
    SH_DBG("[GL] hidden at info");
    SH_CHECK(captured().size() == 3);
    if (captured().size() == 3)
    {
        SH_CHECK(captured()[0].category == SH_LOGCAT_AUDIO && captured()[0].level == SH_LOGLV_INFO);
        SH_CHECK(captured()[1].category == SH_LOGCAT_SPAWN && captured()[1].level == SH_LOGLV_WARN);
        SH_CHECK(captured()[2].category == SH_LOGCAT_FS && captured()[2].level == SH_LOGLV_ERROR);
    }
    reset_journal();
}

SH_TEST(log_console_sink_takes_up_to_its_level_and_echoes)
{
    reset_journal();
    ShLog_SetLevel(SH_LOGCAT_MODEL, SH_LOGLV_TRACE);
    SH_LOG_INFO(MODEL, "shown");
    SH_LOG_TRACE(MODEL, "terminal only");
    ShLog_SetLevel(-1, SH_LOGLV_ERROR);
    SH_DBG_ECHO("[CHEAT] god %s", "on");

    static std::vector<std::pair<std::string, int>> got;
    got.clear();
    ShLog_ConsoleDrain([](const char* text, int, int toast) { got.emplace_back(text, toast); });
    SH_CHECK(got.size() == 2);
    if (got.size() == 2)
    {
        SH_CHECK(got[0].first == "model: shown" && got[0].second == 0);
        SH_CHECK(got[1].first == "[CHEAT] god on" && got[1].second == 1);
    }
    reset_journal();
}

SH_TEST(log_long_message_is_not_truncated)
{
    reset_journal();
    const std::string big(3000, 'x');
    SH_LOG_INFO(GAME, "%s", big.c_str());
    SH_CHECK(captured().size() == 1);
    if (!captured().empty())
        SH_CHECK(captured()[0].line.find(big) != std::string::npos);
    reset_journal();
}

SH_TEST(log_rotation_keeps_the_newest_files)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "sh_log_rotation_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    for (int i = 0; i < 14; ++i)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "silenthill-20260101-0000%02d.log", i);
        std::ofstream(dir / name) << i;
    }
    std::ofstream(dir / "other.log") << "kept";

    sh::log::rotate_logs(dir, 10);

    int logs = 0;
    for (const auto& e : fs::directory_iterator(dir))
    {
        if (e.path().filename().string().rfind("silenthill-", 0) == 0)
            ++logs;
    }
    SH_CHECK(logs == 9);  // room for the file about to be opened
    SH_CHECK(fs::exists(dir / "other.log"));
    SH_CHECK(!fs::exists(dir / "silenthill-20260101-000004.log"));
    SH_CHECK(fs::exists(dir / "silenthill-20260101-000005.log"));
    fs::remove_all(dir, ec);
}
