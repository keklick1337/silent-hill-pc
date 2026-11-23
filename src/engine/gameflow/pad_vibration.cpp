#include "engine/gameflow/pad_vibration.h"

#include <algorithm>
#include <climits>
#include <string>

#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

constexpr std::uint32_t kMaxLevel = UINT32_MAX;

std::uint32_t scale_level(std::uint32_t level, std::uint32_t scale) {
    const std::uint64_t v = (static_cast<std::uint64_t>(level) * scale) >> 7;
    return v > kMaxLevel ? kMaxLevel : static_cast<std::uint32_t>(v);
}

bool any_motor_id(int id) { return id == 4 || id == 5 || id == 7; }

}  // namespace

std::uint8_t damage_rumble_level(std::int32_t damage) {
    constexpr std::int32_t kQ12 = 4096;
    if (damage > 25 * kQ12) {
        return 200;
    }
    if (damage < 5 * kQ12) {
        return 100;
    }
    return static_cast<std::uint8_t>(((damage + 15 * kQ12) / (5 * kQ12)) * 25);
}

// ---- the effect list --------------------------------------------------------------------------------------------

const VibrationStep* PadVibration::step_at(int index) const {
    if (scripts_ == nullptr || index < 0 || static_cast<std::size_t>(index) >= scripts_->steps().size()) {
        return nullptr;
    }
    return &scripts_->steps()[static_cast<std::size_t>(index)];
}

int PadVibration::add_effect(std::int32_t priority) {
    const int e = free_;
    if (e < 0) {
        return -1;
    }
    free_ = effects_[e].next;
    const int first = effects_[kHead].next;
    const int last = effects_[kHead].prev;
    int prev = kHead;
    int cur = kHead;
    if (first == kHead) {
        // Empty list.
    } else {
        // The list is kept high priority first, but a priority below the middle of the ends goes to the tail
        // without a search (the game's shortcut, so equal priorities do not always keep their order).
        const std::int32_t mid = static_cast<std::int32_t>(static_cast<std::uint32_t>(effects_[first].priority) +
                                                           static_cast<std::uint32_t>(effects_[last].priority)) >>
                                 1;
        if (mid <= priority) {
            cur = first;
            while (cur != kHead && priority < effects_[cur].priority) {
                prev = cur;
                cur = effects_[cur].next;
            }
        } else {
            prev = last;
        }
    }
    effects_[cur].prev = e;
    effects_[prev].next = e;
    effects_[e].next = cur;
    effects_[e].prev = prev;
    effects_[e].priority = priority;
    return e;
}

bool PadVibration::remove_effect(int e) {
    if (e < 0 || e == kHead || effects_[e].prev < 0) {
        return false;
    }
    const int next = effects_[e].next;
    const int prev = effects_[e].prev;
    effects_[prev].next = next;
    effects_[next].prev = prev;
    // A freed effect keeps its stale `prev`, as in the game.
    effects_[e].next = free_;
    free_ = e;
    return true;
}

void PadVibration::remove_if(bool (*match)(const Effect&, std::int32_t), std::int32_t value) {
    int cur = effects_[kHead].next;
    while (cur != kHead && cur >= 0) {
        const int next = effects_[cur].next;
        if (match(effects_[cur], value)) {
            remove_effect(cur);
        }
        cur = next;
    }
}

// ---- steps ------------------------------------------------------------------------------------------------------

bool PadVibration::run_step(int e, int step, std::uint32_t& ticks) {
    const VibrationStep* st = step_at(step);
    if (st == nullptr) {
        return false;
    }
    switch (st->op) {
        case VibrationOp::Start:
            return step_start(e, step, ticks);
        case VibrationOp::Chain:
            return step_chain(e, step, ticks);
        case VibrationOp::Waveform:
            return step_waveform(e, step, ticks);
        case VibrationOp::Command:
            return step_command(e, step);
        case VibrationOp::Pulse:
            return step_pulse(e, step, ticks);
    }
    return false;
}

bool PadVibration::step_start(int e, int step, std::uint32_t& ticks) {
    const VibrationStep& st = *step_at(step);
    if (st.skip_if_active) {
        for (int cur = effects_[kHead].next; cur != kHead && cur >= 0; cur = effects_[cur].next) {
            if (effects_[cur].id == st.id) {
                // Running scripts that reach this step end here (the running effect itself counts).
                if (e >= 0) {
                    effects_[e].step = -1;
                }
                return false;
            }
        }
    } else if (st.replace_active) {
        int cur = effects_[kHead].next;
        while (cur != kHead && cur >= 0) {
            const int next = effects_[cur].next;
            if (cur != e && effects_[cur].id == st.id) {
                remove_effect(cur);
            }
            cur = next;
        }
    }
    if (e < 0) {
        e = add_effect(st.priority);
        if (e < 0) {
            return false;
        }
        effects_[e].level = static_cast<std::uint8_t>(ticks);
    }
    Effect& fx = effects_[e];
    fx.step = st.next;
    fx.elapsed = 0;
    fx.id = st.id;
    if (st.has_tag) {
        fx.tag = st.tag;
    }
    return true;
}

bool PadVibration::step_chain(int e, int step, std::uint32_t& ticks) {
    const VibrationStep& st = *step_at(step);
    std::int32_t priority;
    if (e < 0) {
        priority = st.priority;
        e = add_effect(priority);
        if (e < 0) {
            return false;
        }
        effects_[e].level = static_cast<std::uint8_t>(ticks);
    } else {
        if (effects_[e].step != step) {
            effects_[e].step = -1;
            return false;
        }
        priority = effects_[e].priority;
    }
    effects_[e].step = st.next;
    effects_[e].elapsed = 0;
    if (st.has_tag) {
        effects_[e].tag = st.tag;
    }
    const int second = add_effect(priority);
    if (second >= 0) {
        // The spawned effect runs right after its parent, whatever its priority placed it.
        Effect& s = effects_[second];
        effects_[s.next].prev = s.prev;
        effects_[s.prev].next = s.next;
        Effect& p = effects_[e];
        s.prev = e;
        s.next = p.next;
        effects_[p.next].prev = second;
        p.next = second;
        s.step = st.spawned;
        s.elapsed = 0;
        s.id = p.id;
        s.level = p.level;
        s.tag = p.tag;
    }
    return true;
}

bool PadVibration::step_waveform(int e, int step, std::uint32_t& ticks) {
    if (e < 0) {
        return false;
    }
    const VibrationStep& st = *step_at(step);
    Effect& fx = effects_[e];
    if (fx.step != step) {
        fx.step = -1;
        return false;
    }
    std::uint32_t t = fx.elapsed;
    // The level is sampled once per frame, where the effect stood when the frame began.
    if (!fx.has_output) {
        fx.has_output = true;
        fx.channel = st.channel & 0x7F;
        fx.mask = st.mask;
        const std::uint32_t period = st.ticks_per_sample & 0xFFF;
        std::uint32_t index = period == 0 ? t : t / period;
        std::uint32_t frac = period == 0 ? 0 : t % period;
        if (!st.interpolate) {
            frac = 0;
        }
        const auto byte = [&st](std::uint32_t i) -> std::uint32_t {
            return i < st.samples.size() ? st.samples[i] : 0u;
        };
        std::uint32_t a = 0, b = 0;
        std::uint32_t width = st.bytes_per_sample & 7;
        if (width == 0) {
            a = 0u - ((byte(index >> 3) >> (index & 7)) & 1u);
            const std::uint32_t n = index + 1;
            b = 0u - ((byte(n >> 3) >> (n & 7)) & 1u);
        } else {
            width = std::min<std::uint32_t>(width, 4);
            index *= width;
            for (std::uint32_t k = 0; k < width; ++k) {
                a += byte(index + k) << (24 - 8 * k);
                b += byte(index + width + k) << (24 - 8 * k);
            }
        }
        std::uint32_t v = a;
        if (frac != 0) {
            const std::uint32_t w = ((UINT32_MAX / period) + 1u) * frac;
            v = static_cast<std::uint32_t>((static_cast<std::uint64_t>(a) * (0u - w)) >> 32) +
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(b) * w) >> 32);
        }
        fx.output = scale_level(v, fx.level);
    }
    t += ticks;
    if (t >= st.duration) {
        fx.step = st.next;
        fx.elapsed = 0;
        ticks = t - st.duration;
        return true;
    }
    fx.elapsed = static_cast<std::uint16_t>(t);
    ticks = 0;
    return false;
}

bool PadVibration::step_command(int e, int step) {
    // Commands only act when started directly; inside a running script they stall it.
    if (e >= 0) {
        return false;
    }
    const VibrationStep& st = *step_at(step);
    switch (st.command) {
        case VibrationCommand::Enable:
            if (!enabled_) {
                actuators_sent_ = false;
                enabled_ = true;
            }
            break;
        case VibrationCommand::Disable:
            if (enabled_) {
                actuators_sent_ = false;
                enabled_ = false;
            }
            break;
        case VibrationCommand::Pause:
            if (!paused_) {
                actuators_sent_ = false;
                paused_ = true;
            }
            break;
        case VibrationCommand::Resume:
            if (paused_) {
                actuators_sent_ = false;
                paused_ = false;
            }
            break;
        case VibrationCommand::ClearAll:
            remove_if([](const Effect&, std::int32_t) { return true; }, 0);
            break;
        case VibrationCommand::RemoveById:
            remove_if([](const Effect& fx, std::int32_t id) { return fx.id == id; }, st.id);
            break;
    }
    return true;
}

bool PadVibration::step_pulse(int e, int step, std::uint32_t& ticks) {
    const VibrationStep& st = *step_at(step);
    if (e >= 0) {
        Effect& fx = effects_[e];
        fx.channel = st.channel & 0x7F;
        fx.mask = st.mask;
        fx.step = st.next;
        fx.has_output = true;
        fx.output = static_cast<std::uint32_t>(fx.level) << 24;
        return false;
    }
    e = add_effect(st.priority);
    if (e < 0) {
        return false;
    }
    effects_[e].step = step;  // the effect runs this same step on its first frame
    effects_[e].level = static_cast<std::uint8_t>(ticks);
    effects_[e].id = st.id;
    return true;
}

bool PadVibration::run_effect(int e, std::uint16_t mask, std::uint32_t vblanks) {
    std::uint32_t ticks = vblanks * 5;  // ticks of 1/300 s
    Effect& fx = effects_[e];
    fx.output = 0;
    fx.has_output = false;
    while (fx.step >= 0) {
        if (!run_step(e, fx.step, ticks)) {
            break;
        }
    }
    if (fx.has_output && (mask & fx.mask) != 0 && fx.channel < kChannels) {
        Channel& ch = channels_[fx.channel];
        ch.level = std::max(ch.level, fx.output);
    }
    return fx.step >= 0;
}

// ---- device -----------------------------------------------------------------------------------------------------

void PadVibration::init() {
    port_ = 0;
    strength_ = kFullStrength;
    initialized_ = true;
    paused_ = true;
    enabled_ = true;
    actuator_count_ = 0;
    alignment_done_ = false;
    actuators_sent_ = false;
    kind_ = PadKind::None;
    actuators_.fill(0);
    channel_count_ = 0;
    channel_capacity_ = kChannels;
    channels_ = {};
    for (int i = 0; i < kEffects; ++i) {
        effects_[i].next = i == 0 ? -1 : i - 1;
        effects_[i].prev = -1;
    }
    free_ = kEffects - 1;
    effects_[kHead].next = kHead;
    effects_[kHead].prev = kHead;
}

void PadVibration::reset_device() {
    for (int i = 0; i < 6; ++i) {
        actuators_[static_cast<std::size_t>(i)] = 0;
    }
    actuator_count_ = 0;
    actuators_sent_ = false;
    alignment_done_ = false;
}

int PadVibration::detect(PadLink& pad) {
    const int state = pad.state(port_);
    const int id = pad.info(port_, PadLink::kInfoId);
    const int ext = pad.info(port_, PadLink::kInfoExtendedId);

    // No pad, or one being searched for.
    if (state == PadLink::kDisconnected || state == PadLink::kFindPad) {
        if (kind_ != PadKind::None) {
            kind_ = PadKind::None;
            if (state == PadLink::kDisconnected) {
                enabled_ = false;
            }
            reset_device();
            channel_count_ = 0;
        }
        return state;
    }
    if (kind_ != PadKind::None && kind_ != PadKind::NoMotor) {
        return state;
    }

    // DualShock: a small on/off motor (one byte) and a large one with a level (one byte).
    if (state == PadLink::kRequestInfo || state == PadLink::kExecCommand) {
        if (kind_ == PadKind::DualShock) {
            return state;
        }
    } else if (state == PadLink::kStable && (ext == 4 || ext == 7)) {
        kind_ = PadKind::DualShock;
        reset_device();
        const int n = channel_capacity_ < 3 ? channel_capacity_ : 2;
        int count = 0;
        if (n > 0) {
            channels_[0].out_index = 0;
            channels_[0].out_count = 0;
            actuator_count_ = 1;
            count = 1;
        }
        if (n >= 2) {
            channels_[1].out_index = 1;
            channels_[1].out_count = 1;
            actuator_count_ = 2;
            count = 2;
        }
        channel_count_ = static_cast<std::uint8_t>(count);
        for (int i = count; i < channel_capacity_; ++i) {
            channels_[static_cast<std::size_t>(i)].out_index = 0;
            channels_[static_cast<std::size_t>(i)].out_count = 0;
        }
        return state;
    }

    // A pad found in the first step of the search: single-motor pads drive their motor through byte 1, with byte 0
    // holding the 0x40 the protocol wants.
    if (state == PadLink::kFindCtp1 && any_motor_id(id)) {
        kind_ = PadKind::SingleMotor;
        reset_device();
        actuators_[0] = 0x40;
        const int n = channel_capacity_ < 2 ? channel_capacity_ : 1;
        int count = 0;
        if (n > 0) {
            channels_[0].out_index = 1;
            channels_[0].out_count = 0;
            actuator_count_ = 2;
            count = 1;
        }
        channel_count_ = static_cast<std::uint8_t>(count);
        for (int i = count; i < channel_capacity_; ++i) {
            channels_[static_cast<std::size_t>(i)].out_index = 0;
            channels_[static_cast<std::size_t>(i)].out_count = 0;
        }
        return state;
    }

    // Anything else: a pad without motors, an unknown one, or none yet.
    PadKind kind = PadKind::None;
    if (state == PadLink::kFindCtp1) {
        kind = any_motor_id(id) ? PadKind::NoMotor : PadKind::Other;
    }
    if (kind != kind_) {
        kind_ = kind;
        reset_device();
        channel_count_ = 0;
    }
    return state;
}

void PadVibration::send(PadLink& pad) {
    if (!actuators_sent_) {
        actuators_sent_ = true;
        pad.set_actuators(port_, actuators_.data(), enabled_ && !paused_ ? actuator_count_ : 0);
    }
    // The PS1 sent the DualShock its motor layout here; the PC driver needs none.
    alignment_done_ = true;
}

void PadVibration::pack_actuators() {
    if (!enabled_ || paused_) {
        return;
    }
    for (int i = 0; i < channel_count_ && i < kChannels; ++i) {
        const Channel& ch = channels_[static_cast<std::size_t>(i)];
        std::uint32_t v = scale_level(ch.level, strength_);
        int count = ch.out_count;
        int index = ch.out_index;
        if (count == 0) {
            // An on/off motor: any level turns it on.
            count = 1;
            if (v != 0) {
                v = 0x01FFFFFF;
            }
        }
        for (; count > 0; --count, ++index, v <<= 8) {
            if (index < static_cast<int>(actuators_.size())) {
                actuators_[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(v >> 24);
            }
        }
    }
}

// ---- entry points -----------------------------------------------------------------------------------------------

bool PadVibration::request_main_mode(PadLink& pad, int mode) {
    pad.info(port_, PadLink::kInfoExtendedId);
    const int state = pad.state(port_);
    bool done = pad.info(port_, PadLink::kInfoExtendedOffset) == mode;
    if (!done && state == PadLink::kStable) {
        pad.set_main_mode(port_, mode, 0);
        done = pad.set_main_mode(port_, 1, 0) != 0;
        actuators_sent_ = false;
        alignment_done_ = false;
    }
    return done;
}

int PadVibration::update(PadLink& pad, bool enabled_by_option, std::uint32_t vblanks) {
    if (!initialized_) {
        init();
    }
    if (enabled_by_option) {
        if (!enabled_) {
            actuators_sent_ = false;
            enabled_ = true;
        }
        strength_ = kFullStrength;
    } else {
        if (enabled_) {
            actuators_sent_ = false;
            enabled_ = false;
        }
        strength_ = 0;
    }
    if (pad.ready()) {
        detect(pad);
        send(pad);
    }
    channels_[0].level = 0;
    channels_[1].level = 0;

    const std::uint32_t clock = paused_ ? 0 : vblanks;
    int count = 0;
    for (int e = effects_[kHead].next; e != kHead && e >= 0; ++count) {
        const int next = effects_[e].next;
        if (!run_effect(e, 0xFFFF, clock)) {
            remove_effect(e);
        }
        e = next;
    }
    pack_actuators();
    return count;
}

void PadVibration::play(int step, std::uint8_t level) {
    if (!initialized_ || step_at(step) == nullptr) {
        return;
    }
    std::uint32_t ticks = level;
    run_step(-1, step, ticks);
}

// ---- data -------------------------------------------------------------------------------------------------------

namespace {

std::string where(const json::Value& v) {
    return "line " + std::to_string(v.line()) + ", column " + std::to_string(v.column());
}

}  // namespace

Result<VibrationScripts> VibrationScripts::parse(std::string_view text) {
    using R = Result<VibrationScripts>;
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return R::fail("pad_vibration.json: " + doc.error());
    }
    const json::Value* list = doc.value().find("steps");
    if (list == nullptr || !list->is_array()) {
        return R::fail("pad_vibration.json: \"steps\" must be an array");
    }
    // Names first: steps refer to each other by name.
    std::vector<std::string> names;
    for (const json::Value& item : list->array()) {
        const json::Value* name = item.is_object() ? item.find("name") : nullptr;
        if (name == nullptr || !name->is_string() || name->string().empty()) {
            return R::fail("pad_vibration.json: " + where(item) + ": a step needs a \"name\"");
        }
        if (std::find(names.begin(), names.end(), name->string()) != names.end()) {
            return R::fail("pad_vibration.json: " + where(item) + ": step \"" + name->string() + "\" named twice");
        }
        names.push_back(name->string());
    }
    const auto ref = [&names](const json::Value& item, const char* key, int& out, std::string& error) {
        const json::Value* v = item.find(key);
        out = -1;
        if (v == nullptr || v->is_null()) {
            return true;
        }
        if (!v->is_string()) {
            error = where(*v) + ": \"" + key + "\" must be a step name";
            return false;
        }
        const auto it = std::find(names.begin(), names.end(), v->string());
        if (it == names.end()) {
            error = where(*v) + ": no step \"" + v->string() + "\"";
            return false;
        }
        out = static_cast<int>(it - names.begin());
        return true;
    };
    const auto integer = [](const json::Value& item, const char* key, std::int64_t lo, std::int64_t hi,
                            std::int64_t fallback, std::int64_t& out, std::string& error) {
        const json::Value* v = item.find(key);
        out = fallback;
        if (v == nullptr) {
            return true;
        }
        if (!v->is_integer() || v->integer() < lo || v->integer() > hi) {
            error = where(*v) + ": \"" + key + "\" must be an integer in " + std::to_string(lo) + ".." +
                    std::to_string(hi);
            return false;
        }
        out = v->integer();
        return true;
    };
    const auto flag = [](const json::Value& item, const char* key) {
        const json::Value* v = item.find(key);
        return v != nullptr && v->is_bool() && v->boolean();
    };

    VibrationScripts out;
    for (const json::Value& item : list->array()) {
        VibrationStep st;
        std::string error;
        const json::Value* op = item.find("op");
        const std::string kind = op != nullptr && op->is_string() ? op->string() : "";
        std::int64_t n = 0;
        bool ok = ref(item, "next", st.next, error);
        if (kind == "start" || kind == "chain") {
            st.op = kind == "start" ? VibrationOp::Start : VibrationOp::Chain;
            st.skip_if_active = flag(item, "skip_if_active");
            st.replace_active = flag(item, "replace_active");
            ok = ok && integer(item, "tag", 0, 0xFFFF, -1, n, error);
            st.has_tag = n >= 0;
            st.tag = static_cast<std::uint16_t>(n < 0 ? 0 : n);
            ok = ok && integer(item, "id", INT32_MIN, INT32_MAX, 0, n, error);
            st.id = static_cast<std::int32_t>(n);
            ok = ok && integer(item, "priority", INT32_MIN, INT32_MAX, 0, n, error);
            st.priority = static_cast<std::int32_t>(n);
            if (ok && kind == "chain") {
                ok = ref(item, "spawn", st.spawned, error);
            }
        } else if (kind == "waveform" || kind == "pulse") {
            st.op = kind == "waveform" ? VibrationOp::Waveform : VibrationOp::Pulse;
            ok = ok && integer(item, "channel", 0, PadVibration::kChannels - 1, 0, n, error);
            st.channel = static_cast<std::uint8_t>(n);
            ok = ok && integer(item, "mask", 0, 0xFFFF, 0xFFFF, n, error);
            st.mask = static_cast<std::uint16_t>(n);
            if (kind == "pulse") {
                ok = ok && integer(item, "id", INT32_MIN, INT32_MAX, 0, n, error);
                st.id = static_cast<std::int32_t>(n);
                ok = ok && integer(item, "priority", INT32_MIN, INT32_MAX, 0, n, error);
                st.priority = static_cast<std::int32_t>(n);
            } else {
                ok = ok && integer(item, "duration", 0, 0xFFFF, 0, n, error);
                st.duration = static_cast<std::uint16_t>(n);
                ok = ok && integer(item, "ticks_per_sample", 0, 0xFFF, 0, n, error);
                st.ticks_per_sample = static_cast<std::uint16_t>(n);
                ok = ok && integer(item, "bytes_per_sample", 0, 4, 0, n, error);
                st.bytes_per_sample = static_cast<std::uint8_t>(n);
                st.interpolate = flag(item, "interpolate");
                const json::Value* samples = item.find("samples");
                if (ok && (samples == nullptr || !samples->is_array())) {
                    ok = false;
                    error = where(item) + ": a waveform needs \"samples\" (bytes)";
                }
                for (std::size_t i = 0; ok && i < samples->array().size(); ++i) {
                    const json::Value& b = samples->array()[i];
                    if (!b.is_integer() || b.integer() < 0 || b.integer() > 255) {
                        ok = false;
                        error = where(b) + ": a sample byte must be 0..255";
                        break;
                    }
                    st.samples.push_back(static_cast<std::uint8_t>(b.integer()));
                }
            }
        } else if (kind == "command") {
            st.op = VibrationOp::Command;
            const json::Value* c = item.find("command");
            const std::string cmd = c != nullptr && c->is_string() ? c->string() : "";
            static const struct {
                const char* name;
                VibrationCommand command;
            } kCommands[] = {{"enable", VibrationCommand::Enable},     {"disable", VibrationCommand::Disable},
                             {"pause", VibrationCommand::Pause},       {"resume", VibrationCommand::Resume},
                             {"clear_all", VibrationCommand::ClearAll}, {"remove_id", VibrationCommand::RemoveById}};
            bool known = false;
            for (const auto& k : kCommands) {
                if (cmd == k.name) {
                    st.command = k.command;
                    known = true;
                }
            }
            if (ok && !known) {
                ok = false;
                error = where(item) + ": unknown command \"" + cmd + "\"";
            }
            ok = ok && integer(item, "id", INT32_MIN, INT32_MAX, 0, n, error);
            st.id = static_cast<std::int32_t>(n);
        } else {
            ok = false;
            error = where(item) + ": \"op\" must be start, chain, waveform, pulse or command";
        }
        if (!ok) {
            return R::fail("pad_vibration.json: " + error);
        }
        out.steps_.push_back(std::move(st));
    }
    out.names_ = std::move(names);
    return R::ok(std::move(out));
}

int VibrationScripts::index_of(std::string_view name) const {
    const auto it = std::find(names_.begin(), names_.end(), name);
    return it == names_.end() ? -1 : static_cast<int>(it - names_.begin());
}

}  // namespace sh::gameflow
