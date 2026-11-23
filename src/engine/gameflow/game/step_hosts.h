#pragma once

// The step counters of the running state (src/engine/gameflow/event_steps.h) on the session, and the command
// helpers' host (event_commands.h) on the game's fade, border, background and item code.

#include <cstdint>

#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/event_commands.h"
#include "engine/gameflow/event_steps.h"
#include "engine/gameflow/game/game_link.h"

namespace sh::gameflow::game {

template <class Base = StepHost>
class SessionSteps : public Base {
public:
    std::int32_t step(int level) const override { return sh::game::session().system.flow.sys_steps[level]; }
    void store_step(int level, std::int32_t value) override { sh::game::session().system.flow.sys_steps[level] = value; }
    std::int32_t hold() const override { return sh::game::session().system.flow.sys_step_data[0]; }
    void store_hold(std::int32_t value) override { sh::game::session().system.flow.sys_step_data[0] = value; }
    std::int32_t timer() const override { return sh::game::session().system.flow.sys_step_data[1]; }
    void store_timer(std::int32_t value) override { sh::game::session().system.flow.sys_step_data[1] = value; }

    std::int32_t raw_delta() const override { return sh::game_state::delta_time_raw(); }
    bool player_stopped() const override { return overlay_player_stopped(); }
    bool flag(std::int16_t flag) const override { return sh::game_state::event_flag(flag); }
    MessageResult show_message(int message) override { return static_cast<MessageResult>(map_message_draw(message)); }
};

template <class Base>
class SessionCommands : public SessionSteps<Base> {
public:
    void set_fade_rate(std::int32_t rate) override { sh::game_state::set_screen_fade_timestep(rate); }
    void start_fade(bool fade_in, bool white) override { sh::game_state::screen_fade_start(false, fade_in, white); }
    void show_borders() override { cutscene_borders_in(); }
    void hide_borders() override { cutscene_borders_out(); }
    void mark_cutscene() override { sh::game::session().system.process.sys_flags |= sh::game::sys_flag::kCutscene; }
    bool fade_idle() const override { return screen_fade_none(); }
    bool fade_done() const override { return sh::game_state::screen_fade_finished(); }
    bool borders_hidden() const override { return sh::game::session().system.flow.cutscene_border == kBordersNone; }
    bool borders_shown() const override { return sh::game::session().system.flow.cutscene_border == kBordersShown; }

    void load_background(int file, bool secondary) override {
        read_image(file, secondary ? Picture::EventSecondary : Picture::ItemInspection);
    }
    bool files_loaded() override { return file_chunks_loaded(); }
    void draw_background(bool secondary) override {
        draw_picture_alt(secondary ? Picture::EventSecondary : Picture::ItemInspection);
    }
    void store_vram() override { keep_event_background_area(); }
    void restore_vram() override { restore_event_background_area(); }

    void load_item_model(int item) override { load_unique_item_model(item); }
    void show_item_pickup(int item) override { start_item_pickup(item); }
    void add_item(int item, int count) override { sh::game_state::inventory_add(item, count); }

private:
    static constexpr std::int32_t kBordersNone = 1;   // the border state "none"
    static constexpr std::int32_t kBordersShown = 21;  // active (16) + shown (5)
};

}  // namespace sh::gameflow::game
