#include "framework/sh_test.h"

#include "engine/script/sh_script.h"

#include <string>

namespace {

std::string g_trace;
bool g_saw_earlier = false;

int flag_one(int) { return 1; }

void binder_late(ShScriptGameApi*) { g_trace += "c"; }
void binder_first(ShScriptGameApi* api) {
    g_trace += "a";
    api->flag_get = flag_one;
}
void binder_first_again(ShScriptGameApi*) { g_trace += "b"; }
void binder_middle(ShScriptGameApi* api) {
    g_trace += "m";
    g_saw_earlier = api->flag_get == flag_one;
    api->flag_get = nullptr;
}

}  // namespace

// Binders run by ascending order, equal orders in registration order, over one API record; a later binder
// sees (and may wrap) what an earlier one set.
SH_TEST(game_binders_run_in_order) {
    g_trace.clear();
    ShScript_RegisterGameBinder(30, binder_late);
    ShScript_RegisterGameBinder(10, binder_first);
    ShScript_RegisterGameBinder(20, binder_middle);
    ShScript_RegisterGameBinder(10, binder_first_again);
    ShScript_RegisterGameBinder(5, nullptr);  // ignored
    ShScript_InstallGameApi();
    SH_CHECK(g_trace == "abmc");
    SH_CHECK(g_saw_earlier);
    ShScript_SetGameApi(nullptr);
}
