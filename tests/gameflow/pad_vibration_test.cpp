// Pad vibration against its specification (the original's vibration scripts): the base script file, device detection, the
// pause the effects start in, pulses, waveforms, skip-if-active, the 16-effect pool and the damage level.

#include "engine/gameflow/pad_vibration.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

std::string base_file() {
    std::ifstream f(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata/config/pad_vibration.json");
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct FakePad : gf::PadLink {
    int pad_state = kStable;
    int ext = 7;
    std::vector<int> sent_counts;
    bool ready() override { return true; }
    int state(int) override { return pad_state; }
    int info(int, int term) override { return term == kInfoExtendedId ? ext : 7; }
    int set_main_mode(int, int, int) override { return 1; }
    void set_actuators(int, std::uint8_t*, int count) override { sent_counts.push_back(count); }
};

gf::VibrationScripts load() {
    auto parsed = gf::VibrationScripts::parse(base_file());
    SH_CHECK(parsed);
    return parsed.value();
}

}  // namespace

SH_TEST(pad_vibration_base_file_parses) {
    const gf::VibrationScripts s = load();
    SH_CHECK(s.steps().size() == 48);
    SH_CHECK(s.index_of("Damage") == 10);
    SH_CHECK(s.index_of("LargeMotorPulse") == 21);
    SH_CHECK(s.index_of("Resume") == 1);
    SH_CHECK(s.steps()[11].op == gf::VibrationOp::Chain);
    SH_CHECK(s.steps()[11].spawned == 6 && s.steps()[11].next == 5);
    SH_CHECK(s.steps()[47].next == 47);  // the heartbeat rest loops on itself
}

SH_TEST(pad_vibration_parse_rejects_bad_steps) {
    SH_CHECK(!gf::VibrationScripts::parse(R"({"steps":[{"name":"A","op":"jump"}]})"));
    SH_CHECK(!gf::VibrationScripts::parse(R"({"steps":[{"name":"A","op":"start","next":"B"}]})"));
    SH_CHECK(!gf::VibrationScripts::parse(R"({"steps":[{"name":"A","op":"pulse","channel":2}]})"));
    SH_CHECK(!gf::VibrationScripts::parse(R"({"steps":[{"name":"A","op":"start"},{"name":"A","op":"start"}]})"));
}

SH_TEST(pad_vibration_starts_paused_until_resume) {
    const gf::VibrationScripts s = load();
    gf::PadVibration v(&s);
    FakePad pad;
    v.play(21, 0x90);  // before the first update: dropped
    SH_CHECK(v.update(pad, true, 1) == 0);
    SH_CHECK(v.kind() == gf::PadVibration::PadKind::DualShock);
    SH_CHECK(pad.sent_counts.size() == 1 && pad.sent_counts[0] == 0);  // paused: nothing sent to the motors
    v.play(1, 0x80);                                                   // Resume
    v.update(pad, true, 1);
    SH_CHECK(pad.sent_counts.size() == 2 && pad.sent_counts[1] == 2);
}

SH_TEST(pad_vibration_pulse_lasts_one_frame) {
    const gf::VibrationScripts s = load();
    gf::PadVibration v(&s);
    FakePad pad;
    v.update(pad, true, 1);
    v.play(1, 0x80);
    v.play(21, 0x90);
    SH_CHECK(v.update(pad, true, 1) == 1);
    SH_CHECK(v.actuators()[1] == 0x90);
    SH_CHECK(v.update(pad, true, 1) == 0);
    SH_CHECK(v.actuators()[1] == 0);
    v.play(20, 0x10);  // the small motor is on/off
    v.update(pad, true, 1);
    SH_CHECK(v.actuators()[0] == 1);
}

SH_TEST(pad_vibration_waveform_and_option) {
    const gf::VibrationScripts s = load();
    gf::PadVibration v(&s);
    FakePad pad;
    v.update(pad, true, 1);
    v.play(1, 0x80);
    v.play(10, 0x80);  // Damage: first sample 0xC0 for 24 ticks
    v.update(pad, true, 2);
    SH_CHECK(v.actuators()[1] == 0xC0);
    v.update(pad, true, 2);  // 10 more ticks, still the first sample
    SH_CHECK(v.actuators()[1] == 0xC0);
    v.update(pad, true, 2);  // started at tick 20, still below 24
    SH_CHECK(v.actuators()[1] == 0xC0);
    v.update(pad, true, 2);  // started at tick 30: sample 1 = 0x60
    SH_CHECK(v.actuators()[1] == 0x60);
    // The option off: the motors stop being sent (count 0) and the bytes are left as they were.
    const std::size_t sent = pad.sent_counts.size();
    v.update(pad, false, 2);
    SH_CHECK(pad.sent_counts.size() == sent + 1 && pad.sent_counts.back() == 0);
}

SH_TEST(pad_vibration_skip_if_active_and_pool) {
    const gf::VibrationScripts s = load();
    gf::PadVibration v(&s);
    FakePad pad;
    v.update(pad, true, 1);
    v.play(9, 0x80);
    v.play(9, 0x80);  // Machinery again while it runs: skipped
    SH_CHECK(v.update(pad, true, 0) == 1);
    for (int i = 0; i < 20; ++i) {
        v.play(21, 0x40);  // pulses have no skip: the pool of 16 fills up
    }
    SH_CHECK(v.update(pad, true, 0) == gf::PadVibration::kEffects);
    v.play(4, 0x80);  // ClearAll
    SH_CHECK(v.update(pad, true, 0) == 0);
}

SH_TEST(pad_vibration_disconnect_resets_device) {
    const gf::VibrationScripts s = load();
    gf::PadVibration v(&s);
    FakePad pad;
    v.update(pad, true, 1);
    SH_CHECK(v.kind() == gf::PadVibration::PadKind::DualShock);
    pad.pad_state = gf::PadLink::kDisconnected;
    v.update(pad, true, 1);
    SH_CHECK(v.kind() == gf::PadVibration::PadKind::None);
    pad.pad_state = gf::PadLink::kStable;
    v.update(pad, true, 1);
    SH_CHECK(v.kind() == gf::PadVibration::PadKind::DualShock);
}

SH_TEST(pad_vibration_damage_level) {
    SH_CHECK(gf::damage_rumble_level(0) == 100);
    SH_CHECK(gf::damage_rumble_level(5 * 4096) == 100);
    SH_CHECK(gf::damage_rumble_level(10 * 4096) == 125);
    SH_CHECK(gf::damage_rumble_level(25 * 4096) == 200);
    SH_CHECK(gf::damage_rumble_level(26 * 4096) == 200);
}

SH_TEST(pad_vibration_motor_weapon_rumble_wanders) {
    const gf::VibrationScripts s = load();
    gf::PadVibration v(&s);
    FakePad pad;
    v.update(pad, true, 1);
    v.play(1, 0x80);
    gf::MotorWeaponRumble motor;
    int calls = 0;
    // Chainsaw: the level moves once 6 vblanks have gathered; the RNG is asked only then.
    motor.frame(v, 21, true, 2, [&] { return ++calls, 0; });
    SH_CHECK(calls == 0);
    v.update(pad, true, 2);
    SH_CHECK(v.actuators()[1] == 32);
    motor.frame(v, 21, true, 4, [&] { return ++calls, 2048; });  // step (2048 * 16 >> 12) + 20 = 28
    SH_CHECK(calls == 1);
    v.update(pad, true, 2);
    SH_CHECK(v.actuators()[1] == 32 + 28);
}
