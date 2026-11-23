#include "engine/gameflow/boot_machine.h"

#include <utility>

#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

constexpr int kMainMenu = 7;  // game state the title menu runs as

}  // namespace

BootParams BootParams::builtin() {
    BootParams p;
    p.base_audio_tasks = {160, 162};
    p.state_handlers = {"Boot", "KonamiLogo", "KcetLogo", "MovieIntroFadeIn", "AutoLoadSavegame", "MovieIntroAlternate",
                        "MovieIntro", "MainMenu", "LoadSavegameScreen", "MovieOpening", "LoadMapScreen", "InGame",
                        "MapEvent", "ExitMovie", "ItemScreens", "PaperMapScreen", "LoadSavegameScreen",
                        "DebugMoviePlayer", "Options", "LoadStatusScreen", "LoadMapScreen", "Unk15"};
    return p;
}

Result<BootParams> BootParams::parse(std::string_view text) {
    using R = Result<BootParams>;
    const Result<json::Value> doc = json::parse(text);
    if (!doc) return R::fail("boot.json: " + doc.error());
    BootParams out = builtin();
    if (const json::Value* t = doc.value().find("base_audio_tasks")) {
        if (!t->is_array()) return R::fail("boot.json: \"base_audio_tasks\" must be an array");
        out.base_audio_tasks.clear();
        for (const json::Value& v : t->array()) {
            if (!v.is_integer() || v.integer() <= 0 || v.integer() > 100000) {
                return R::fail("boot.json: audio task numbers must be integers from 1 to 100000");
            }
            out.base_audio_tasks.push_back(static_cast<std::int32_t>(v.integer()));
        }
    }
    if (const json::Value* h = doc.value().find("state_handlers")) {
        if (!h->is_array() || h->array().empty()) return R::fail("boot.json: \"state_handlers\" must be a non-empty array");
        out.state_handlers.clear();
        for (const json::Value& v : h->array()) {
            if (!v.is_string() || v.string().empty()) return R::fail("boot.json: handler names must be strings");
            out.state_handlers.push_back(v.string());
        }
    }
    return R::ok(std::move(out));
}

std::string_view state_handler_name(const BootParams& params, int game_state) {
    if (game_state < 0 || static_cast<std::size_t>(game_state) >= params.state_handlers.size()) return {};
    return params.state_handlers[static_cast<std::size_t>(game_state)];
}

void BootMachine::update() {
    const LoadView v = host_.view();
    switch (v.steps[0]) {
        case 0:
            host_.background_black();
            host_.screen_init();
            v.counters[1] = 0;
            v.steps[1] = 0;
            v.steps[2] = 0;
            v.steps[0]++;
            break;

        case 1:
            if (!host_.audio_streaming()) {
                const auto index = static_cast<std::size_t>(v.steps[1]);
                const std::int32_t task = index < params_.base_audio_tasks.size() ? params_.base_audio_tasks[index] : 0;
                if (task != 0) {
                    host_.audio_call(task);
                    v.steps[1]++;
                } else {
                    v.counters[1] = 0;
                    v.steps[1] = 0;
                    v.steps[2] = 0;
                    v.steps[0]++;
                }
            }
            break;

        case 2:
            host_.graphics_queue();
            if (host_.skip_intros()) host_.intro_loads_run();
            host_.fade_in_start();
            v.steps[0]++;
            break;

        case 3:
            if (host_.fade_finished()) {
                host_.queue_wait_empty();
                const int state = host_.game_state();
                v.counters[0] = 0;
                v.counters[1] = 0;
                v.steps[1] = 0;
                v.steps[2] = 0;
                host_.play_state_gameplay_next();
                if (host_.skip_intros()) {
                    host_.settings_restore_defaults();
                    host_.game_state_set(kMainMenu);
                } else {
                    host_.game_state_set(state + 1);
                }
                host_.game_state_previous_set(state);
                v.steps[0] = 0;
            }
            break;
    }
    host_.pad_hook(1);
}

}  // namespace sh::gameflow
