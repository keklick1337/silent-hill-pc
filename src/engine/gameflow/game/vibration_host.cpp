// The game's pad vibration on the engine (src/engine/gameflow/pad_vibration.h):
// the pad driver calls, the option, the frame's vblanks and the game's random numbers. The scripts come from
// gamedata/config/pad_vibration.json.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/pad_vibration.h"
#include "engine/log/sh_log.h"
#include "engine/loop/game_random.h"
#include "engine/mods/data_reload.h"

namespace {

namespace gf = sh::gameflow;
namespace link = sh::gameflow::game;

constexpr std::uint8_t kFullLevel = 0x80;

const gf::VibrationScripts& scripts() {
    static gf::VibrationScripts loaded;
    static std::uint32_t loaded_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(loaded_epoch)) {
        loaded = [] {
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            std::vector<std::string> warnings;
            std::string source;
            const sh::Result<std::string> text =
                gf::read_config_file(link::gamedata_path(), mods, "pad_vibration.json", &source, warnings);
            for (const std::string& w : warnings) {
                SH_WARN("[GAMEFLOW] %s", w.c_str());
            }
            if (!text) {
                SH_ERR("[GAMEFLOW] config/pad_vibration.json: %s", text.error().c_str());
                return gf::VibrationScripts{};
            }
            sh::Result<gf::VibrationScripts> parsed = gf::VibrationScripts::parse(text.value());
            if (!parsed) {
                SH_ERR("[GAMEFLOW] %s", parsed.error().c_str());
                return gf::VibrationScripts{};
            }
            return parsed.value();
        }();
    }
    return loaded;
}

gf::PadVibration& vibration() {
    // Static: the pad driver keeps a pointer to its actuator bytes.
    static gf::PadVibration v(nullptr);
    return v;
}

class GamePad final : public gf::PadLink {
public:
    bool ready() override { return link::pad_exchange_done(); }
    int state(int port) override { return link::pad_state(port); }
    int info(int port, int term) override { return link::pad_info(port, term); }
    int set_main_mode(int port, int mode, int lock) override { return link::pad_set_main_mode(port, mode, lock); }
    void set_actuators(int port, std::uint8_t* bytes, int count) override { link::pad_set_actuators(port, bytes, count); }
};

// The scripts the engine side plays by name: the motor weapons' pulse on the large motor and the damage rumble.
int named_step(const char* name) {
    const int step = scripts().index_of(name);
    if (step < 0) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            SH_ERR("[GAMEFLOW] config/pad_vibration.json: no step \"%s\"", name);
        }
    }
    return step;
}

gf::MotorWeaponRumble& motor_rumble() {
    static gf::MotorWeaponRumble r;
    return r;
}

}  // namespace

extern "C" {

void Vibration_MainModeRequest(std::int32_t mode) {
    GamePad pad;
    vibration().request_main_mode(pad, mode);
}

std::int32_t Vibration_Update(void) {
    if (!vibration().initialized()) {
        vibration().set_scripts(&scripts());
    }
    GamePad pad;
    return vibration().update(pad, sh::game::session().game.settings.vibration != 0,
                              static_cast<std::uint32_t>(link::vblanks()));
}

void Vibration_Play(std::int32_t step) {
    vibration().play(step, kFullLevel);
}

void Vibration_PlayAtLevel(std::int32_t step, std::uint8_t level) {
    vibration().play(step, level);
}

void Vibration_MotorWeaponRumble(std::int32_t chainsaw) {
    static const int pulse = named_step("LargeMotorPulse");
    motor_rumble().frame(vibration(), pulse, chainsaw != 0, static_cast<std::uint32_t>(link::vblanks()),
                         [] { return static_cast<std::int32_t>(sh::loop::game_random().next_q12_fraction()); });
}

void Vibration_DamageRumble(std::int32_t damage) {
    static const int step = named_step("Damage");
    vibration().play(step, gf::damage_rumble_level(damage));
}

}  // extern "C"
