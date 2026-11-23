#pragma once

// Pad vibration: short scripts of steps drive the pad motors. A caller starts
// a script by its index in the step table (gamedata/config/pad_vibration.json); a started script becomes an effect
// in a pool of 16, and every frame each effect runs its steps until one has to wait for ticks to pass. Waveform and
// pulse steps set the effect's level on a motor channel; the strongest effect wins the channel, and the channels are
// packed into the actuator bytes the pad driver sends to the pad. Device detection picks how many channels the
// connected pad has and where their bytes go.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::gameflow {

enum class VibrationOp : std::uint8_t {
    Start = 0,     // starts an effect that runs the steps after this one
    Chain = 1,     // starts an effect and spawns a second one from another script
    Waveform = 2,  // plays samples on a motor for a fixed number of ticks
    Command = 3,   // changes the global state or removes effects
    Pulse = 4,     // sets a motor level for one frame
};

enum class VibrationCommand : std::uint8_t {
    Enable = 0,
    Disable = 1,
    Pause = 2,
    Resume = 3,
    ClearAll = 4,
    RemoveById = 7,  // removes the effects whose id equals `id`
};

struct VibrationStep {
    VibrationOp op = VibrationOp::Start;
    int next = -1;  // step run after this one; -1 ends the script

    // Start: skip when an effect with the same id runs, or remove those first.
    bool skip_if_active = false;
    bool replace_active = false;
    bool has_tag = false;  // Start, Chain: copy `tag` to the effect
    std::uint16_t tag = 0;
    std::int32_t id = 0;        // Start, Pulse: the effect id; Command RemoveById: the id removed
    std::int32_t priority = 0;  // Start, Chain, Pulse: the place in the effect list (higher first)
    int spawned = -1;           // Chain: the script of the second effect

    // Waveform, Pulse.
    std::uint8_t channel = 0;
    std::uint16_t mask = 0xFFFF;  // the effect only drives the channel when the frame's mask meets this
    // Waveform.
    std::vector<std::uint8_t> samples;  // 1-bit samples (bit 0 of each byte first) or big-endian levels
    std::uint16_t duration = 0;         // ticks
    std::uint16_t ticks_per_sample = 0;
    std::uint8_t bytes_per_sample = 0;  // 0 = 1-bit samples, else 1..4
    bool interpolate = false;

    // Command.
    VibrationCommand command = VibrationCommand::Enable;
};

// The step table. Steps are referred to by their position; the file names them.
class VibrationScripts {
public:
    static Result<VibrationScripts> parse(std::string_view json);
    const std::vector<VibrationStep>& steps() const { return steps_; }
    std::vector<VibrationStep>& steps() { return steps_; }
    // The position of the step named `name`, -1 if there is none.
    int index_of(std::string_view name) const;

private:
    std::vector<VibrationStep> steps_;
    std::vector<std::string> names_;
};

// The pad driver as the vibration needs it.
class PadLink {
public:
    // Driver states.
    static constexpr int kDisconnected = 0, kFindPad = 1, kFindCtp1 = 2, kRequestInfo = 4, kExecCommand = 5,
                         kStable = 6;
    // Info terms.
    static constexpr int kInfoId = 1, kInfoExtendedId = 2, kInfoExtendedOffset = 3;

    virtual ~PadLink() = default;
    virtual bool ready() = 0;                 // the pad exchange of this frame has completed
    virtual int state(int port) = 0;
    virtual int info(int port, int term) = 0;
    virtual int set_main_mode(int port, int mode, int lock) = 0;
    // The driver keeps `bytes` and sends it every frame from now on.
    virtual void set_actuators(int port, std::uint8_t* bytes, int count) = 0;
};

class PadVibration {
public:
    static constexpr int kEffects = 16;
    static constexpr int kChannels = 2;
    static constexpr int kFullStrength = 128;

    // Kinds of connected pads, decided by the detection.
    enum class PadKind : std::uint8_t { None = 0, DualShock = 1, SingleMotor = 2, NoMotor = 3, Other = 4 };

    struct Effect {
        int next = -1, prev = -1;  // links; a free effect only uses `next`
        std::int32_t priority = 0;
        std::int32_t id = 0;
        int step = -1;
        std::uint16_t elapsed = 0;  // ticks spent in the current waveform
        std::uint8_t level = 0;     // the level the script was started with (0x80 = 1.0)
        std::uint8_t channel = 0;
        bool has_output = false;
        std::uint32_t output = 0;
        std::uint16_t tag = 0;
        std::uint16_t mask = 0;
    };

    struct Channel {
        std::uint8_t out_index = 0;  // first actuator byte
        std::uint8_t out_count = 0;  // bytes written; 0 = one on/off byte
        std::uint32_t level = 0;     // strongest effect this frame (high byte first)
    };

    explicit PadVibration(const VibrationScripts* scripts) : scripts_(scripts) {}

    void set_scripts(const VibrationScripts* scripts) { scripts_ = scripts; }
    bool initialized() const { return initialized_; }

    // Asks the pad for a main mode (analog); resends the actuators afterwards.
    bool request_main_mode(PadLink& pad, int mode);
    // One frame: init on first use, the option, detection, effects, actuator bytes. Returns the effects run.
    int update(PadLink& pad, bool enabled_by_option, std::uint32_t vblanks);
    // Starts the script at `step` (ignored before the first update).
    void play(int step, std::uint8_t level);

    // Inspection (tests).
    const std::array<std::uint8_t, 8>& actuators() const { return actuators_; }
    PadKind kind() const { return kind_; }
    const Effect& effect(int i) const { return effects_[i]; }
    int first_effect() const { return effects_[kHead].next; }
    const Channel& channel(int i) const { return channels_[i]; }

private:
    static constexpr int kHead = kEffects;  // the sentinel of the running list

    const VibrationStep* step_at(int index) const;
    void init();
    void reset_device();
    int detect(PadLink& pad);
    void send(PadLink& pad);
    void pack_actuators();
    bool run_effect(int effect, std::uint16_t mask, std::uint32_t ticks);
    bool run_step(int effect, int step, std::uint32_t& ticks);
    bool step_start(int effect, int step, std::uint32_t& ticks);
    bool step_chain(int effect, int step, std::uint32_t& ticks);
    bool step_waveform(int effect, int step, std::uint32_t& ticks);
    bool step_command(int effect, int step);
    bool step_pulse(int effect, int step, std::uint32_t& ticks);
    int add_effect(std::int32_t priority);
    bool remove_effect(int effect);
    void remove_if(bool (*match)(const Effect&, std::int32_t), std::int32_t value);

    const VibrationScripts* scripts_;

    // Status.
    std::uint8_t port_ = 0;
    std::uint8_t strength_ = 0;
    bool initialized_ = false;
    bool paused_ = false;
    bool enabled_ = false;
    std::uint8_t actuator_count_ = 0;  // bytes the driver sends
    bool alignment_done_ = false;
    bool actuators_sent_ = false;
    PadKind kind_ = PadKind::None;

    // The bytes the driver keeps a pointer to: the address must not change.
    std::array<std::uint8_t, 8> actuators_{};
    std::uint8_t channel_count_ = 0;
    std::uint8_t channel_capacity_ = 0;
    std::array<Channel, kChannels> channels_{};

    std::array<Effect, kEffects + 1> effects_{};
    int free_ = -1;
};

// The motor weapons' rumble (chainsaw and rock drill): a level wandering in [32, 159] on the large motor, played
// through the pulse script `pulse_step`. The step of the wander is random, from the game RNG (`rng_q12`, [0, 4096)).
class MotorWeaponRumble {
public:
    template <class Rng>
    void frame(PadVibration& vibration, int pulse_step, bool chainsaw, std::uint32_t vblanks, Rng&& rng_q12) {
        timer_ = static_cast<std::uint8_t>(timer_ + vblanks);
        if (static_cast<int>(timer_) >= (chainsaw ? 6 : 24)) {
            const std::int32_t r = rng_q12();
            const std::int32_t step = chainsaw ? ((r * 16) >> 12) + 20 : ((r * 20) >> 12) + 60;
            phase_ = static_cast<std::uint8_t>((phase_ + step) & 0x7F);
        }
        vibration.play(pulse_step, static_cast<std::uint8_t>(phase_ + 32));
    }

private:
    std::uint8_t phase_ = 0;
    std::uint8_t timer_ = 0;  // wraps; never reset, as in the game
};

// The level of the damage rumble for a hit of `damage` (Q12 health): 100 up to 5.0, 200 above 25.0, steps of 25.
std::uint8_t damage_rumble_level(std::int32_t damage);

}  // namespace sh::gameflow
