#include "engine/gameflow/map_load.h"

#include <algorithm>

#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

enum Step : std::int32_t {
    kStart = 0,
    kDemoWait = 1,
    kDemoPlayRead = 2,
    kQueueDrain = 3,
    kScreenSelect = 4,
    kCharaGroups = 5,
    kCharaWait = 6,
    kWorldClear = 7,
    kChunkWait = 8,
    kMusicStart = 9,
    kAmbientStart = 10,
    kScreenHold = 11,
    kHandOver = 12,
};

constexpr int kEventWorldFrozen = 0;
constexpr int kEventToPlay = 1;
constexpr int kEventNoAmbientWait = 2;
constexpr int kStartSound = 19;
constexpr int kCharaSlotFirst = 1;
constexpr int kCharaSlotLast = 3;
constexpr int kNoValue = -1;

struct Field {
    const char* name;
    std::int32_t MapLoadParams::*member;
    std::int32_t min;
};

constexpr Field kFields[] = {
    {"demo_wait_frames", &MapLoadParams::demo_wait_frames, 0},
    {"demo_attempts", &MapLoadParams::demo_attempts, 1},
    {"queue_flush_limit", &MapLoadParams::queue_flush_limit, 1},
    {"chunk_wait_frames", &MapLoadParams::chunk_wait_frames, 1},
    {"music_watchdog_frames", &MapLoadParams::music_watchdog_frames, 1},
    {"load_sound_frames", &MapLoadParams::load_sound_frames, 1},
    {"last_screen_step", &MapLoadParams::last_screen_step, 1},
    {"screen_fade_timestep", &MapLoadParams::screen_fade_timestep, 0},
    {"pace_step_limit", &MapLoadParams::pace_step_limit, 1},
    {"vblank_rate", &MapLoadParams::vblank_rate, 1},
    {"hold_frames", &MapLoadParams::hold_frames, 0},
    {"pace_vblanks", &MapLoadParams::pace_vblanks, 0},
    {"min_vblanks", &MapLoadParams::min_vblanks, 0},
};

}  // namespace

Result<MapLoadParams> MapLoadParams::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<MapLoadParams>::fail("map_load.json: " + doc.error());
    }
    MapLoadParams out;
    for (const Field& f : kFields) {
        const json::Value* v = doc.value().find(f.name);
        if (v == nullptr) {
            continue;  // keeps the built-in value
        }
        if (!v->is_integer() || v->integer() < f.min || v->integer() > 1000000) {
            return Result<MapLoadParams>::fail(std::string("map_load.json: \"") + f.name + "\" must be an integer from " +
                                               std::to_string(f.min) + " to 1000000");
        }
        out.*(f.member) = static_cast<std::int32_t>(v->integer());
    }
    return Result<MapLoadParams>::ok(out);
}

bool MapLoad::door_load() const {
    const LoadKind kind = host_.kind();
    return !host_.demo_active() && (kind == LoadKind::RoomTransition || kind == LoadKind::OverlayTransition);
}

bool MapLoad::player_run_screen() const {
    return host_.loading_screen_player_run() && host_.view().steps[0] < params_.last_screen_step;
}

bool MapLoad::screen_paced() const { return player_run_screen() && params_.pace_vblanks > 1; }

void MapLoad::clock_reset() {
    start_vblank_ = kNoValue;
    step_vblank_ = kNoValue;
    screen_step_ = 0;
}

void MapLoad::clock_tick() {
    const std::int32_t now = host_.vsync_count();
    if (!player_run_screen()) {
        screen_dt_ = host_.delta_time();
        return;
    }
    if (start_vblank_ == kNoValue) {
        start_vblank_ = now;
    }
    if (params_.pace_vblanks <= 1) {
        screen_dt_ = host_.delta_time();
        return;
    }
    std::int32_t elapsed = step_vblank_ == kNoValue ? params_.pace_vblanks : now - step_vblank_;
    if (elapsed < 0) {
        elapsed = params_.pace_vblanks;
    }
    if (elapsed >= params_.pace_vblanks) {
        screen_dt_ = (4096 * std::min(elapsed, params_.pace_step_limit)) / params_.vblank_rate;
        step_vblank_ = now;
        ++screen_step_;
    } else {
        screen_dt_ = 0;
        host_.hold_feedback_frame();
    }
}

bool MapLoad::holding_minimum() {
    return player_run_screen() && start_vblank_ != kNoValue && (host_.vsync_count() - start_vblank_) < params_.min_vblanks;
}

void MapLoad::step_next() {
    const LoadView v = host_.view();
    const std::int32_t step = v.steps[0];
    v.counters[1] = 0;
    v.steps[1] = 0;
    v.steps[2] = 0;
    v.steps[0] = step + 1;
}

void MapLoad::loading_screen() {
    clock_tick();
    if (host_.loading_screen_shown() && host_.view().steps[0] < params_.last_screen_step) {
        host_.loading_screen_draw(params_.screen_fade_timestep);
    }
    host_.background_motion_blur();
}

void MapLoad::screen_update() {
    loading_screen();
    startup();
    if (host_.load_flag()) {
        if (host_.load_flag_tick() >= params_.load_sound_frames) {
            host_.load_flag_clear();
            host_.load_done_sounds();
        }
    }
}

void MapLoad::startup() {
    const LoadView v = host_.view();
    const LoadKind kind = host_.kind();

    switch (v.steps[0]) {
        case kStart:
            clock_reset();
            host_.interval_vblanks_set(1);
            host_.background_clear();
            if (kind == LoadKind::RoomTransition) {
                host_.area_player_position_update();
                v.steps[0] = kWorldClear;
            } else if (kind == LoadKind::BootDemo) {
                demo_attempts_ = 0;
                v.steps[0] = kDemoWait;
                v.counters[1] = 1;
            } else {
                v.steps[0] = kQueueDrain;
            }
            host_.audio_call(kStartSound);
            break;

        case kDemoWait:
            if (v.counters[1] > params_.demo_wait_frames && host_.queue_length() == 0 && !host_.audio_streaming()) {
                host_.demo_savegame_update();
                host_.player_init();
                if (host_.demo_buffer_setup()) {
                    host_.demo_map_load();
                    v.steps[0] = kDemoPlayRead;
                    v.counters[1] = 0;
                    v.steps[1] = 0;
                    v.steps[2] = 0;
                    break;
                }
                host_.demo_sequence_advance();
                host_.demo_data_read();
                ++demo_attempts_;
                if (demo_attempts_ >= params_.demo_attempts) {
                    demo_attempts_ = 0;
                    v.counters[1] = 0;
                }
            }
            break;

        case kDemoPlayRead:
            if (host_.queue_length() == 0 && !host_.audio_streaming()) {
                host_.demo_play_data_read();
                v.steps[0] = kQueueDrain;
                v.counters[1] = 0;
                v.steps[1] = 0;
                v.steps[2] = 0;
            }
            break;

        case kQueueDrain:
            if (door_load()) {
                host_.queue_drain_now();
            }
            if (host_.queue_length() == 0) {
                v.steps[0] = kScreenSelect;
            }
            break;

        case kScreenSelect:
            if (kind == LoadKind::OverlayTransition) {
                host_.area_player_position_update();
            } else if (kind == LoadKind::LoadSave || kind == LoadKind::Continue) {
                host_.loading_screen_player_run_set();
            }
            ++v.steps[0];
            break;

        case kCharaGroups:
            for (int slot = kCharaSlotFirst; slot <= kCharaSlotLast; ++slot) {
                host_.chara_group_alloc(slot);
            }
            host_.chara_groups_load();
            ++v.steps[0];
            [[fallthrough]];

        case kCharaWait:
            if (door_load()) {
                host_.queue_drain_now();
            }
            if (host_.queue_length() == 0) {
                host_.chara_groups_loaded();
                ++v.steps[0];
            }
            break;

        case kWorldClear:
            if (host_.event_flag(kEventWorldFrozen)) {
                host_.world_clear();
            }
            host_.player_chunk_init();
            if (kind == LoadKind::OverlayTransition) {
                host_.radio_stop();
            }
            ++v.steps[0];
            [[fallthrough]];

        case kChunkWait: {
            int flushed = 0;
            while (host_.queue_length() > 0 && flushed < params_.queue_flush_limit) {
                host_.queue_update();
                ++flushed;
            }
            if (host_.chunks_ready()) {
                step_next();
            } else if (++chunk_frames_ > params_.chunk_wait_frames) {
                // Chunks that never arrive must not hold the loading screen up for good.
                chunk_frames_ = 0;
                step_next();
            }
            break;
        }

        case kMusicStart:
            if (door_load()) {
                host_.queue_drain_now();
            }
            if (holding_minimum()) {
                break;
            }
            {
                const int result = host_.music_init();
                ++music_frames_;
                if (result == 0) {
                    music_frames_ = 0;
                    host_.state_main_load_screen_set();
                    step_next();
                }
                if (music_frames_ > params_.music_watchdog_frames) {
                    host_.audio_state_force_clear();
                    v.steps[1] = 0;
                    music_frames_ = 0;
                }
            }
            break;

        case kAmbientStart:
            if (door_load()) {
                host_.queue_drain_now();
            }
            if (kind == LoadKind::BootDemo && !host_.demo_active()) {
                host_.demo_start();
                host_.demo_active_set();
            }
            if (host_.event_flag(kEventNoAmbientWait) || host_.ambient_init() == 0) {
                step_next();
            }
            break;

        case kScreenHold:
            if (door_load() || v.counters[0] >= params_.hold_frames) {
                if (kind == LoadKind::RoomTransition) {
                    host_.npc_init();
                } else {
                    host_.in_game_init();
                }
                if (kind == LoadKind::None || kind == LoadKind::RoomTransition || kind == LoadKind::OverlayTransition) {
                    host_.transition_sound();
                }
                ++v.steps[0];
            }
            break;

        case kHandOver:
            host_.state_in_game_next();
            if (host_.event_flag(kEventToPlay)) {
                v.steps[0] = kDemoWait;
                host_.fade_reset_timestep();
            }
            break;

        default:
            break;
    }
}

LoadKind session_load_kind()
{
    namespace pf = sh::game::process_flag;
    switch (sh::game::session().system.process.process_flags) {
    case 0: return LoadKind::None;
    case pf::kRoomTransition: return LoadKind::RoomTransition;
    case pf::kOverlayTransition: return LoadKind::OverlayTransition;
    case pf::kNewGame: return LoadKind::NewGame;
    case pf::kLoadSave: return LoadKind::LoadSave;
    case pf::kContinue: return LoadKind::Continue;
    case pf::kBootDemo: return LoadKind::BootDemo;
    default: return LoadKind::Other;
    }
}

}  // namespace sh::gameflow
