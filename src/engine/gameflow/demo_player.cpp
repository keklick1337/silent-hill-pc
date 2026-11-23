#include "engine/gameflow/demo_player.h"

#include <algorithm>
#include <utility>

#include "engine/game_state/game_state.h"
#include "engine/gameflow/state_names.h"
#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

namespace gs = sh::game_state;

using RulesResult = Result<DemoRules>;

// Offsets in the setup file and in one recorded frame.
constexpr std::size_t kSetupFrameCount = 0x7F8;
constexpr std::size_t kSetupSeed = 0x7FC;
constexpr std::size_t kFrameExpectedState = 8;
constexpr std::size_t kFrameInterval = 9;
constexpr std::size_t kFrameSeed = 12;

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

Result<std::int32_t> state_name(const json::Value* v, const char* key, bool sys) {
    using R = Result<std::int32_t>;
    const std::int32_t id = v != nullptr && v->is_string()
                                ? (sys ? sys_state_by_name(v->string()) : game_state_by_name(v->string()))
                                : -1;
    if (id < 0) return R::fail(std::string("demos.json: \"") + key + "\" must be a state name");
    return R::ok(id);
}

}  // namespace

DemoRules DemoRules::builtin() {
    DemoRules r;
    r.rotation = {{"MISC/DEMO0009.DAT", "MISC/PLAY0009.DAT"},
                  {"MISC/DEMO000A.DAT", "MISC/PLAY000A.DAT"},
                  {"MISC/DEMO0003.DAT", "MISC/PLAY0003.DAT"},
                  {"MISC/DEMO000B.DAT", "MISC/PLAY000B.DAT"},
                  {"MISC/DEMO0005.DAT", "MISC/PLAY0005.DAT"}};
    r.step_states = {static_cast<int>(gs::GameState::InGame),          static_cast<int>(gs::GameState::MapEvent),
                     static_cast<int>(gs::GameState::ExitMovie),       static_cast<int>(gs::GameState::InventoryScreen),
                     static_cast<int>(gs::GameState::PaperMapScreen),  static_cast<int>(gs::GameState::OptionScreen)};
    r.exit_from_state = static_cast<int>(gs::GameState::InGame);
    r.exit_on_sys_state = static_cast<int>(gs::SysState::GameOver);
    r.exit_after_state = static_cast<int>(gs::GameState::SaveScreen);
    r.exit_timer = 999 * 60;
    r.seed_advance = 0x3C6EF35Fu;
    return r;
}

RulesResult DemoRules::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) return RulesResult::fail("demos.json: " + doc.error());
    const json::Value& root = doc.value();
    if (!root.is_object()) return RulesResult::fail("demos.json: the root must be an object");
    DemoRules out = builtin();

    if (const json::Value* list = root.find("rotation")) {
        if (!list->is_array() || list->array().empty()) {
            return RulesResult::fail("demos.json: \"rotation\" must be a non-empty array");
        }
        out.rotation.clear();
        for (const json::Value& e : list->array()) {
            const json::Value* setup = e.is_object() ? e.find("setup") : nullptr;
            const json::Value* play = e.is_object() ? e.find("play") : nullptr;
            if (setup == nullptr || !setup->is_string() || setup->string().empty() || play == nullptr ||
                !play->is_string() || play->string().empty()) {
                return RulesResult::fail("demos.json: a demo is {\"setup\": <disc path>, \"play\": <disc path>}");
            }
            out.rotation.push_back({setup->string(), play->string()});
        }
    }
    if (const json::Value* list = root.find("step_states")) {
        if (!list->is_array()) return RulesResult::fail("demos.json: \"step_states\" must be an array of state names");
        out.step_states.clear();
        for (const json::Value& e : list->array()) {
            auto id = state_name(&e, "step_states", false);
            if (!id) return RulesResult::fail(id.error());
            out.step_states.push_back(id.value());
        }
    }
    if (const json::Value* exit = root.find("exit")) {
        if (!exit->is_object()) return RulesResult::fail("demos.json: \"exit\" must be an object");
        auto from = state_name(exit->find("in_state"), "exit.in_state", false);
        if (!from) return RulesResult::fail(from.error());
        auto sys = state_name(exit->find("on_sys_state"), "exit.on_sys_state", true);
        if (!sys) return RulesResult::fail(sys.error());
        auto after = state_name(exit->find("after_state"), "exit.after_state", false);
        if (!after) return RulesResult::fail(after.error());
        out.exit_from_state = from.value();
        out.exit_on_sys_state = sys.value();
        out.exit_after_state = after.value();
    }
    if (const json::Value* t = root.find("exit_timer_vblanks")) {
        if (!t->is_integer() || t->integer() < 0 || t->integer() > 10000000) {
            return RulesResult::fail("demos.json: \"exit_timer_vblanks\" must be from 0 to 10000000");
        }
        out.exit_timer = static_cast<std::int32_t>(t->integer());
    }
    if (const json::Value* s = root.find("seed_advance")) {
        if (!s->is_integer() || s->integer() < 0 || s->integer() > 0xFFFFFFFFll) {
            return RulesResult::fail("demos.json: \"seed_advance\" must be a 32-bit unsigned integer");
        }
        out.seed_advance = static_cast<std::uint32_t>(s->integer());
    }
    return RulesResult::ok(std::move(out));
}

void DemoPlayer::advance(std::int32_t by) {
    const auto count = static_cast<std::int32_t>(rules_.rotation.size());
    if (count == 0) return;
    index_ += by;
    while (index_ < 0) index_ += count;
    while (static_cast<std::uint32_t>(index_) >= static_cast<std::uint32_t>(count)) index_ -= count;
    setup_file_ = rules_.rotation[static_cast<std::size_t>(index_)].setup;
    play_file_ = rules_.rotation[static_cast<std::size_t>(index_)].play;
}

void DemoPlayer::read_setup(DemoHost& host) const {
    if (!setup_file_.empty()) host.queue_read(setup_file_, host.setup_buffer());
}

void DemoPlayer::read_play(DemoHost& host) {
    advance(0);
    if (!play_file_.empty()) host.queue_read(play_file_, play_);
}

bool DemoPlayer::place_play_buffer(DemoHost& host) {
    // The recording is read in whole sectors and ends where the setup begins. Without a recording to measure there
    // is no place for one: a read sent to the setup buffer's own address would run past the end of the block.
    const std::int32_t bytes = play_file_.empty() ? 0 : host.file_size(play_file_);
    if (bytes <= 0) return false;
    const std::int32_t size = (bytes + 0x7FF) & ~0x7FF;
    play_ = host.setup_buffer() - size;
    return reinterpret_cast<std::uintptr_t>(play_) >= host.map_overlay_end();
}

void DemoPlayer::start(DemoHost& host) {
    playing_ = true;
    host.set_demo_flag(true);
    host.apply_demo_settings();
    const std::uint8_t* setup = host.setup_buffer();
    start_seed_ = static_cast<std::uint16_t>(setup[kSetupSeed] | setup[kSetupSeed + 1] << 8);
    seed_before_ = host.random_seed();
    host.set_random_seed(start_seed_);
    host.mark_started();
}

void DemoPlayer::stop(DemoHost& host) {
    playing_ = false;
    host.set_demo_flag(false);
    host.restore_settings();
    host.set_random_seed(seed_before_);
}

void DemoPlayer::exit(DemoHost& host) {
    host.set_demo_timer(rules_.exit_timer);
    frame_ = nullptr;
    step_ = 0;
    host.request_warm_reset();
}

void DemoPlayer::back_up_seed(DemoHost& host) {
    if (host.demo_flag()) seed_backup_ = host.random_seed();
}

void DemoPlayer::restore_seed(DemoHost& host) const {
    if (host.demo_flag()) host.set_random_seed(seed_backup_);
}

void DemoPlayer::advance_seed(DemoHost& host) const {
    if (host.demo_flag()) host.set_random_seed(seed_backup_ + rules_.seed_advance);
}

bool DemoPlayer::fade_blocks(std::int32_t fade_status) {
    // Fade-out states 2..5, black or white; bit 0 does not tell them apart.
    const std::int32_t s = fade_status & ~1;
    return s == 2 || s == 4 || s == 10 || s == 12;
}

DemoPlayer::Decision DemoPlayer::decide(std::int32_t game_state, std::int32_t previous, std::int32_t sys_state) const {
    if (game_state == rules_.exit_from_state) {
        if (sys_state == rules_.exit_on_sys_state || previous == rules_.exit_after_state) return Decision::Exit;
        return Decision::Step;
    }
    if (std::find(rules_.step_states.begin(), rules_.step_states.end(), game_state) != rules_.step_states.end()) {
        return Decision::Step;
    }
    return Decision::None;
}

bool DemoPlayer::update(DemoHost& host) {
    const std::int32_t fade_before = previous_fade_;
    const bool loading_chunks = loading_chunks_ != 0;
    loading_chunks_ = 0;
    const std::int32_t fade = host.fade_status();
    previous_fade_ = fade;

    if (!host.demo_flag()) {
        frame_ = nullptr;
        step_ = 0;
        return true;
    }
    if (play_ == nullptr) {
        frame_ = nullptr;
        return false;
    }
    if (read_u32(host.setup_buffer() + kSetupFrameCount) <= static_cast<std::uint32_t>(step_)) {
        exit(host);
        return false;
    }
    if (fade_blocks(fade_before) || fade_blocks(fade) || loading_chunks) {
        frame_ = nullptr;
        return true;
    }

    const std::int32_t state = host.game_state();
    switch (decide(state, host.previous_game_state(), host.sys_state())) {
        case Decision::Step: {
            frame_ = play_ + static_cast<std::size_t>(step_) * kFrameSize;
            const auto expected = static_cast<std::int32_t>(static_cast<std::int8_t>(frame_[kFrameExpectedState]));
            if (expected != state) {
                host.step_error(expected, state);
                frame_ = nullptr;
            }
            step_++;
            return true;
        }
        case Decision::Exit:
            exit(host);
            return false;
        case Decision::None:
            break;
    }
    frame_ = nullptr;
    return true;
}

bool DemoPlayer::feed_pad(DemoHost& host) {
    if (!host.demo_flag()) return false;
    const std::uint16_t buttons = host.pad_buttons();
    if (buttons != 0xFFFF) {  // any button leaves the demo
        exit(host);
        return true;
    }
    host.set_demo_timer(0);
    if (frame_ != nullptr) {
        host.pad_from_recording(frame_);
        return true;
    }
    host.pad_neutral(buttons);
    return true;
}

bool DemoPlayer::update_present_interval() {
    present_interval_ = 1;
    if (frame_ == nullptr) return false;
    present_interval_ = frame_[kFrameInterval];
    return true;
}

bool DemoPlayer::apply_frame_seed(DemoHost& host) const {
    if (!host.demo_flag()) return true;
    if (frame_ == nullptr) {
        host.set_random_seed(start_seed_);
        return false;
    }
    host.set_random_seed(read_u32(frame_ + kFrameSeed));
    return true;
}

bool DemoPlayer::spawn_allowed(const DemoHost& host, std::int32_t x, std::int32_t z) const {
    return host.demo_flag() ? host.near_active_cell(x, z) : true;
}

}  // namespace sh::gameflow
