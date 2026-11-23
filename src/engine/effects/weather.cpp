// SPDX-License-Identifier: GPL-3.0-or-later
// Snow and rain simulation and drawing.

#include "engine/effects/weather.h"

#include <algorithm>
#include <cstdlib>

namespace sh::effects {

namespace {

constexpr std::int32_t kStep30 = 136;  // one 30 Hz step in Q12 seconds
constexpr std::int32_t q12(int whole) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(whole) << 12); }

// Q12 values the original wrote as float constants, fixed here as their truncated results.
constexpr std::int32_t kSnowFallStart = 100;  // 0.0245
constexpr std::int32_t kRainFallStart = 150;  // 0.03675
constexpr std::int32_t kRainFallStartLight = 37;  // 0.0091
constexpr std::int32_t kEighth = 512;
constexpr std::int32_t kRainRest = 4;  // 0.001
constexpr std::int32_t kMetreTenth = -819;  // -0.2
constexpr int kOrderingTableSize = 2048;
constexpr int kRainRange = 6;
constexpr int kWindFast = 600;

// A signed 32-bit product with the wrap-around the original's int arithmetic had.
std::int32_t mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

// A value scaled by the step relative to a 30 Hz step.
std::int32_t scaled(std::int32_t value, std::int32_t dt) { return mul(value, dt) / kStep30; }

std::int32_t clamp(std::int32_t v, std::int32_t lo, std::int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

std::uint8_t byte(std::int32_t v) { return static_cast<std::uint8_t>(v); }

bool contains(const std::vector<int>& list, int v) { return std::find(list.begin(), list.end(), v) != list.end(); }

std::uint16_t clut_y(int row) { return static_cast<std::uint16_t>(row); }

}  // namespace

struct WeatherSystem::Context {
    WeatherHost& host;
    WeatherWords& words;
    const WeatherFrame& frame;
};

WeatherSystem::WeatherSystem(WeatherParams params) : params_(std::move(params)) {
    parts_.assign(static_cast<std::size_t>(params_.particle_max), Particle{});
}

bool WeatherSystem::suspended(WeatherHost& host) const {
    return params_.suspend.has_value() && host.event_param() == params_.suspend->event_param &&
           host.event_step() == params_.suspend->step;
}

void WeatherSystem::stop_sound(WeatherHost& host) {
    rain_sound_on_ = false;
    host.rain_stop();
}

bool WeatherSystem::camera_moved(Context& c) const {
    const std::int32_t dx = std::abs(origin_.x - prev_origin_.x);
    const std::int32_t dz = std::abs(origin_.z - prev_origin_.z);
    const std::int32_t dist = c.host.square_root(mul(dx, dx) + mul(dz, dz));
    // The origin's turn is never recorded, so only the distance can trip it.
    return dist > 10000;
}

void WeatherSystem::update(WeatherHost& host, WeatherWords& words, const WeatherFrame& frame, int arg) {
    Context c{host, words, frame};

    if (params_.rain_on && params_.rain.sound) {
        rain_sound(c);
    }

    view_ = frame.view_position;
    if (params_.wind_x_off) {
        wind_x_ = 0;
    }

    const bool initial = arg == 0 || arg == -1;
    if (initial) {
        if (params_.any()) {
            apply_rules(c, params_.init_rules);
            if (!params_.rain_on || params_.snow_on) {
                snow_kind_flag_ = 1;
            }
        }
        if (arg == -1) {
            host.set_density_mode(host.header_density());
        }
        transition_tick_ = 0;
        if (params_.snow.drifting) {
            drift_clock_ = 0;
        }
        if (params_.any()) {
            prev_origin_.z = 0;
            prev_origin_.x = 0;
        }
        host.set_transition_ready(true);
        words.kind_bits = static_cast<std::uint16_t>(words.requested >> 12);
        words.current = static_cast<std::uint16_t>(words.requested);
        words.target = static_cast<std::uint16_t>(words.requested);
        if (params_.any()) {
            init(c);
        }
    } else {
        if (params_.snow.drifting) {
            words.paused = false;
            drift_clock_ += host.delta_time();
        } else {
            words.paused = host.delta_time() == 0;
        }
        if (params_.any()) {
            sim_tick(c);
            step_transitions(c);
            simulate_and_draw(c);
            // The particles past the live ones are dead.
            for (std::size_t i = static_cast<std::size_t>(std::max<std::int32_t>(0, counts_[0] + counts_[1]));
                 i < parts_.size(); i++) {
                parts_[i].step = 0;
            }
        }
    }
    prev_view_ = view_;
}

void WeatherSystem::sim_tick(Context& c) {
    if (c.host.sim_hz() == 30) {
        sim_dt_ = kStep30;
        sim_accum_ += c.host.delta_time();
        sim_step_ = sim_accum_ >= kStep30;
        if (sim_step_) {
            sim_accum_ -= kStep30;
            if (sim_accum_ > kStep30) {
                sim_accum_ = kStep30;
            }
        }
        return;
    }
    sim_accum_ = 0;
    sim_step_ = true;
    sim_dt_ = c.host.delta_time();
}

bool WeatherSystem::apply_rules(Context& c, const std::vector<RoomRule>& rules) {
    if (rules.empty()) {
        return false;
    }
    const int room = c.host.room();
    const Vec3 player = c.host.player();
    for (const RoomRule& r : rules) {
        if (!r.rooms.empty() && !contains(r.rooms, room)) {
            continue;
        }
        const RuleWhen& w = r.when;
        if ((w.x_gt && !(player.x > *w.x_gt)) || (w.x_lt && !(player.x < *w.x_lt)) ||
            (w.x_le && !(player.x <= *w.x_le)) || (w.z_gt && !(player.z > *w.z_gt)) ||
            (w.z_lt && !(player.z < *w.z_lt))) {
            continue;
        }
        if (r.mode) {
            mode_ = *r.mode;
        }
        for (std::size_t i = 0; i < r.corners.size(); i++) {
            corners_[i].x = r.corners[i].x;
            corners_[i].z = r.corners[i].z;
            if (r.corners[i].has_y) {
                corners_[i].y = r.corners[i].y;
            }
        }
        return r.skip;
    }
    return false;
}

// The weather asked for changes: the particle counts of each kind follow a sine ramp
// until the new weather has fully arrived.
void WeatherSystem::step_transitions(Context& c) {
    WeatherWords& w = c.words;
    if (w.current == w.target) {
        return;
    }
    if (!w.paused && sim_step_) {
        transition_tick_++;
    }
    const std::int32_t max = params_.particle_max;
    const std::int32_t rise = ((c.host.sin(3072 + transition_tick_ * 2) * (max / 2)) >> 12) + max / 2;
    const std::int32_t fall = ((c.host.sin(2048 + transition_tick_ * 2) * (max / 2)) >> 12) + max / 2;
    const std::uint32_t current_kind = w.current >> 14;

    for (int i = 0; i < 2; i++) {
        const std::uint32_t code = (static_cast<std::uint32_t>(w.target) >> ((1 - i) * 6)) & 0x3F;
        const std::uint32_t first = (static_cast<std::uint32_t>(w.target) >> 6) & 0x3F;
        switch (code) {
        case 4:
        case 6:
            counts_[i] = rise;
            break;
        case 5:
        case 7:
            counts_[i] = rise * 2;
            break;
        case 32:
        case 48:
            counts_[i] = fall;
            break;
        case 37:
        case 39:
        case 53:
        case 55:
            counts_[i] = rise + max / 2;
            break;
        case 40:
        case 56:
            counts_[i] = fall * 2;
            break;
        case 44:
        case 46:
        case 60:
        case 62:
            counts_[i] = fall + max / 2;
            break;
        case 0:
            counts_[i] = 0;
            break;
        case 36:
        case 38:
        case 52:
        case 54:
            counts_[i] = max / 2;
            break;
        case 45:
        case 47:
        case 61:
        case 63:
            counts_[i] = max;
            break;
        default:
            break;
        }

        if (params_.snow_on && i == 0) {
            if (current_kind < 2) {
                if (current_kind != 1 || (static_cast<std::uint32_t>(w.target) >> 14) < 2) {
                    snow_kind_flag_ = 1;
                    continue;
                }
            }
            switch (first) {
            case 4:
            case 6:
            case 32:
            case 48:
                snow_kind_flag_ = (counts_[i] < max / 4 + 1) ? 0 : 1;
                break;
            case 5:
            case 7:
            case 40:
            case 56:
                snow_kind_flag_ = (counts_[i] < max / 2 + 1) ? 0 : 1;
                break;
            default:
                snow_kind_flag_ = 1;
                break;
            }
        }
    }

    if (transition_tick_ >= 0x200) {
        transition_tick_ = 0;
        c.host.set_transition_ready(true);
        w.current = w.target;
        switch ((w.kind_bits & 0xF) >> 2) {
        case 0:
            w.snow_flag = 0;
            w.rain_flag = 0;
            break;
        case 1:
            w.snow_flag = 1;
            w.rain_flag = 0;
            break;
        case 2:
            w.snow_flag = 0;
            w.rain_flag = 1;
            break;
        default:
            break;
        }
    }
}


// Drops a particle at a random spot of the square of `range` metres around the origin,
// biased toward the middle by summing two draws.
void WeatherSystem::randomize_position(Context& c, std::uint16_t& rnd, Vec3& v, int range) {
    const std::int32_t n = q12(range);
    const std::int32_t sum = static_cast<std::int32_t>(rnd % static_cast<std::uint32_t>(n)) +
                             c.host.rand16() % n;
    const std::int32_t x = sum - n;
    v.x = x;
    const std::int32_t t = n - std::abs(x);
    if (t == 0) {
        v.z = 0;
        return;
    }
    const std::int32_t zsum = static_cast<std::int32_t>(rnd % static_cast<std::uint32_t>(t)) + c.host.rand16() % t;
    v.z = zsum - t;
}

void WeatherSystem::spawn(Context& c, int pass, Particle& p, std::uint16_t& rnd) {
    switch (pass) {
    case 0:
        if (params_.snow_on) {
            p.type = snow_kind_flag_ != 0 ? 0 : 1;
            p.head.y = origin_.y;
            p.mz = 0;
            p.mx = 0;
            p.my = static_cast<std::int16_t>(kSnowFallStart);
            randomize_position(c, rnd, p.head, params_.snow.range);
            p.tail = Vec3{};
        }
        break;
    case 1:
        if (params_.rain_on) {
            p.type = 2;
            p.head.y = origin_.y + q12(c.host.rand16() % 3);
            if (params_.rain.light) {
                p.tail.y = p.head.y - kEighth;
                p.my = static_cast<std::int16_t>(kRainFallStartLight);
            } else {
                p.tail.y = origin_.y;
                p.my = static_cast<std::int16_t>(kRainFallStart);
            }
            randomize_position(c, rnd, p.head, kRainRange);
            p.tail.x = p.head.x;
            p.tail.z = p.head.z;
        }
        break;
    default:
        break;
    }
    p.step++;
}

// The first fall of a freshly spawned particle during the weather's start-up.
void WeatherSystem::spawn_move(Context& c, int pass, Particle& p, std::uint16_t& rnd, std::int32_t dt) {
    switch (pass) {
    case 0:
        if (params_.snow_on) {
            p.mx = static_cast<std::int16_t>(p.mx + static_cast<std::int32_t>(rnd % 15) - 7);
            rnd = static_cast<std::uint16_t>(c.host.rand16());
            const std::uint16_t second = rnd;
            p.mz = static_cast<std::int16_t>(p.mz + static_cast<std::int32_t>(second % 15) - 7);
            p.my = static_cast<std::int16_t>(p.my + static_cast<std::int32_t>(rnd % 5) - 1);
            p.head.y += scaled((p.my >> 1) << 2, dt);
        }
        break;
    case 1:
        if (params_.rain_on) {
            p.tail.x = p.head.x;
            p.tail.z = p.head.z;
            p.tail.y = p.head.y - p.my;
            p.my = static_cast<std::int16_t>(p.my + params_.rain.gravity());
            p.head.y += scaled(p.my << 2, dt);
        }
        break;
    default:
        break;
    }
    if (p.head.y >= 0) {
        p.head.y = 0;
    }
}

void WeatherSystem::init(Context& c) {
    constexpr int kRestTicks = 16;
    const int max = params_.particle_max;
    const int spawn_rounds = max / 2;
    std::int32_t dt = 136;  // a tenth of a 30 Hz second, as the original's start-up ran

    for (int i = 0; i < max; i++) {
        parts_[static_cast<std::size_t>(i)].step = 0;
    }
    counts_[1] = 0;
    counts_[0] = 0;

    int kind = (c.words.kind_bits & 0xC) >> 2;
    int density = 0;
    if (kind == 1) {
        for (int i = 0; i < max; i++) {
            parts_[static_cast<std::size_t>(i)].type = 0;
        }
        density = c.host.density_mode() == 1 ? 1 : 3;
    } else {
        for (int i = 0; i < max; i++) {
            parts_[static_cast<std::size_t>(i)].type = static_cast<std::uint8_t>(kind);
        }
        density = params_.rain.density();
    }

    if (kind != 0 && kind <= 2) {
        const int variant = c.words.kind_bits & 0x3;
        kind--;
        if (variant >= 2) {
            wind_z_ = kWindFast;
            wind_x_ = kWindFast;
        } else {
            wind_z_ = 0;
            wind_x_ = 0;
        }
        origin_.y = params_.snow.start_height;
        const int mult = (variant == 0 || variant == 2) ? 1 : 2;

        for (int j = 0; j < spawn_rounds; j++) {
            spawn_count_ = 0;
            std::uint16_t rnd = static_cast<std::uint16_t>(c.host.rand16());
            counts_[static_cast<std::size_t>(kind)] += mult;
            if (variant == 0 || variant == 2) {
                counts_[static_cast<std::size_t>(kind)] = clamp(counts_[static_cast<std::size_t>(kind)], 0, spawn_rounds);
            } else {
                counts_[static_cast<std::size_t>(kind)] = clamp(counts_[static_cast<std::size_t>(kind)], 0, max);
            }

            for (int i = 0; i < counts_[static_cast<std::size_t>(kind)]; i++) {
                Particle& p = parts_[static_cast<std::size_t>(i)];
                switch (p.step) {
                case 0:
                    if (spawn_count_ < density) {
                        spawn_count_++;
                        spawn(c, kind, p, rnd);
                    }
                    break;
                case 1:
                    spawn_move(c, kind, p, rnd, dt);
                    if (p.head.y == 0) {
                        if (params_.snow.landing == SnowParams::Landing::Recycle) {
                            p.step = 0;
                        } else {
                            p.step++;
                        }
                    }
                    break;
                default:
                    p.step++;
                    if (p.step >= kRestTicks) {
                        p.step = 0;
                    }
                    break;
                }
            }
        }
    }
    if (params_.rain_on) {
        rain_sound_ramp_ = counts_[1];
    }
}

void WeatherSystem::update_wind(Context& c) {
    if (c.words.variant >= 2) {
        if (wind_x_ < 200) {
            wind_x_++;
        } else {
            wind_x_ += c.host.rand16() % 17 - 8;
            wind_x_ = clamp(wind_x_, 200, 800);
        }
        if (wind_z_ < 200) {
            wind_z_++;
        } else {
            wind_z_ += c.host.rand16() % 17 - 8;
            // The lower bound is below the ramp's 200 as the original had it.
            wind_z_ = clamp(wind_z_, 20, 800);
        }
    } else {
        if (wind_x_ != 0) {
            wind_x_--;
        }
        if (wind_z_ != 0) {
            wind_z_--;
        }
    }
}

void WeatherSystem::simulate_and_draw(Context& c) {
    WeatherHost& host = c.host;
    WeatherWords& words = c.words;
    constexpr int kRestTicksUpdate = 32;

    std::int32_t rain_shift = 3;
    if (params_.rain_on) {
        if (words.rain_ramp < 27000) {
            rain_shift = 3;
        } else if (words.rain_ramp < 135000) {
            rain_shift = 4;
        } else {
            rain_shift = 5;
        }
    }

    // The spot three metres in front of the camera that every particle is relative to.
    origin_.x = view_.x + ((host.sin(c.frame.view_yaw) * q12(3)) >> 12);
    origin_.z = view_.z + ((host.cos(c.frame.view_yaw) * q12(3)) >> 12);
    origin_.y = params_.snow.start_height;

    const Vec3 origin_now = origin_;
    if (prev_origin_.x == 0 && prev_origin_.z == 0) {
        prev_origin_.x = origin_now.x;
        prev_origin_.z = origin_now.z;
    }
    moved_ = camera_moved(c);
    host.begin_view(origin_.x >> 4, origin_.z >> 4);

    std::uint16_t rnd = static_cast<std::uint16_t>(host.rand16());

    if (sim_step_) {
        update_wind(c);
    }
    spawn_count_ = 0;

    if (apply_rules(c, params_.update_rules)) {
        return;
    }

    if (sim_step_) {
        int density = 0;
        std::size_t first = 0;
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 0) {
                if (params_.snow_on) {
                    density = host.density_mode() == 1 ? 1 : 3;
                }
            } else if (params_.rain_on) {
                density = params_.rain.density();
            }

            for (std::size_t k = first; k < first + static_cast<std::size_t>(std::max<std::int32_t>(0, counts_[static_cast<std::size_t>(pass)]));
                 k++) {
                Particle& p = parts_[k];
                p.type = static_cast<std::uint8_t>(p.type % 0xF0);
                if (pass == 0) {
                    if (params_.snow_on && p.type >= 2) {
                        p.step = 0;
                        p.type = 0;
                    }
                } else if (params_.rain_on && (p.type < 2 || p.type > 3)) {
                    p.step = 0;
                    p.type = 2;
                }
                if (params_.snow.drifting) {
                    drift_step_ = static_cast<std::int16_t>(mul(sim_dt_, 10936) / q12(1));
                }

                if (words.paused) {
                    // Nothing moves while the game is paused; the draw below still runs.
                    continue;
                }
                switch (p.step) {
                case 0:
                    if (spawn_count_ < density) {
                        spawn_count_++;
                        spawn(c, pass, p, rnd);
                    }
                    break;
                case 1:
                    move(c, pass, p, rnd, sim_dt_);
                    break;
                default:
                    p.step++;
                    if (p.step < kRestTicksUpdate) {
                        rest(c, pass, p);
                    } else if (params_.rain_on && params_.rain.after_rest) {
                        rain_after_rest(c, p, sim_dt_);
                    }
                    break;
                }
            }
            first += static_cast<std::size_t>(std::max<std::int32_t>(0, counts_[static_cast<std::size_t>(pass)]));
        }
    }

    std::size_t first = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (params_.rain_on && pass != 0) {
            if (sim_step_) {
                words.rain_ramp += counts_[static_cast<std::size_t>(pass)];
            }
            words.rain_ramp = clamp(words.rain_ramp, 0, 135000);
        }
        const std::size_t n = static_cast<std::size_t>(std::max<std::int32_t>(0, counts_[static_cast<std::size_t>(pass)]));
        for (std::size_t k = first; k < first + n; k++) {
            Particle& p = parts_[k];
            if (pass == 0) {
                if (params_.snow_on) {
                    draw_snow(c, p);
                }
            } else if (params_.rain_on) {
                draw_rain(c, p, rain_shift);
            }
        }
        first += n;
    }

    if (sim_step_) {
        prev_origin_.x = origin_now.x;
        prev_origin_.z = origin_now.z;
    }
}

// A particle in the resting state follows the camera and goes back to spawning once it
// has drifted out of the area.
void WeatherSystem::rest(Context& c, int pass, Particle& p) {
    p.head.x += prev_origin_.x - origin_.x;
    p.head.z += prev_origin_.z - origin_.z;
    if (params_.snow.drifting) {
        p.head.z += (prev_origin_.z - origin_.z) - drift_step_;
    }
    if (params_.snow_on && pass == 0 && c.words.variant == 3) {
        p.head.x += wind_x_ >> 4;
        p.head.z += wind_z_ >> 4;
    }

    if (params_.rest_gate && mode_ != 0) {
        // The tail of a windy streak is carried along before leaving; the particle is
        // otherwise left alone.
        if (p.type == 3 || p.type == 243) {
            p.tail.x = p.head.x;
            p.tail.z = p.head.z;
        }
        return;
    }

    const std::int32_t ax = std::abs(p.head.x);
    const std::int32_t reach = (p.head.z >= 0) ? ax + p.head.z : ax - p.head.z;
    if (reach > q12(pass + params_.snow.range)) {
        p.step = 0;
    }
    if (p.type == 3 || p.type == 243) {
        p.tail.x = p.head.x;
        p.tail.z = p.head.z;
    }
}

// A drop that has landed keeps sinking as a short streak until it is far enough away.
void WeatherSystem::rain_after_rest(Context&, Particle& p, std::int32_t dt) {
    const std::int32_t x = prev_origin_.x - origin_.x;
    const std::int32_t z = prev_origin_.z - origin_.z;
    p.tail.y = p.head.y - p.my;
    p.head.x += x;
    p.my = static_cast<std::int16_t>(p.my + kRainRest);
    p.head.y += scaled(p.my << 2, dt);
    p.tail.y = p.head.y - 128;
    p.head.z += z;
    p.tail.x = p.head.x;
    p.tail.z = p.head.z;
    if (std::abs(p.head.x) + std::abs(p.head.z) > q12(6)) {
        p.step = 0;
    }
}

void WeatherSystem::move(Context& c, int pass, Particle& p, std::uint16_t& rnd, std::int32_t dt) {
    WeatherHost& host = c.host;
    const int range = params_.snow.range;
    std::int32_t dx_snow = 0;
    std::int32_t dz_snow = 0;
    std::int32_t dx_rain = 0;
    std::int32_t dz_rain = 0;
    if (params_.snow_on) {
        dx_snow = prev_origin_.x - origin_.x;
        dz_snow = prev_origin_.z - origin_.z;
    } else if (params_.rain_on && params_.rain.light) {
        dx_rain = prev_origin_.x - origin_.x;
        dz_rain = prev_origin_.z - origin_.z;
    }

    switch (pass) {
    case 0:
        if (!params_.snow_on) {
            break;
        }
        if (p.type == 0) {
            // Snow: a random walk sideways and a slowly growing fall speed.
            p.mx = static_cast<std::int16_t>(p.mx + static_cast<std::int32_t>(rnd % 15) - 7);
            rnd = static_cast<std::uint16_t>(host.rand16());
            const std::uint16_t second = rnd;
            p.mz = static_cast<std::int16_t>(p.mz + static_cast<std::int32_t>(second % 15) - 7);
            // The fall speed grows by a random amount per 30 Hz step; the fraction the
            // step did not cover is kept in the tail's y as a credit.
            const std::int32_t grow = static_cast<std::int32_t>(rnd % 5) - 1;
            p.tail.y += scaled(grow * 256, dt);
            while (p.tail.y >= 256) {
                p.my++;
                p.tail.y -= 256;
            }
            while (p.tail.y <= -256) {
                p.my--;
                p.tail.y += 256;
            }
        } else {
            const std::int32_t pick = host.rand16();
            const std::int32_t temp = 12;
            p.my = static_cast<std::int16_t>(p.my + (((temp - c.words.prev_height) - (c.words.prev_height << 1) -
                                                      (counts_[0] >> 4)) +
                                                     (pick % 5) - 2));
            p.my = static_cast<std::int16_t>(clamp(p.my, 5, 1000));
        }

        // The camera's per-frame travel is added unscaled; only the fall and the wind
        // scale with the step.
        p.head.x += scaled((p.mx << 2) + wind_x_, dt) + dx_snow;
        p.head.y += scaled((p.my >> 1) << 2, dt);
        p.head.z += scaled((p.mz << 2) + wind_z_, dt) + dz_snow;
        if (params_.snow.drifting) {
            p.head.z += dz_snow - drift_step_;
        }

        if (std::abs(p.head.x) + std::abs(p.head.z) > q12(range)) {
            if (moved_) {
                randomize_position(c, rnd, p.head, range);
            } else {
                p.head.x = -p.head.x;
                p.head.z = -p.head.z;
                p.type = static_cast<std::uint8_t>(p.type + 240);
            }
        }

        if (p.head.y >= 0) {
            if (params_.snow.landing == SnowParams::Landing::Flat) {
                p.head.y = 0;
                p.my = 0;
                p.step++;
            } else {
                const GroundSample g = host.ground(p.head.x + origin_.x, p.head.z + origin_.z);
                if (p.head.y >= g.height) {
                    if (params_.snow.landing == SnowParams::Landing::Recycle) {
                        p.step = 0;
                    } else {
                        p.head.y = g.height;
                        if (g.height < 0 && g.height > kMetreTenth) {
                            p.head.y = 0;
                        }
                        p.my = 0;
                        p.step++;
                    }
                }
            }
        }
        break;

    case 1: {
        if (!params_.rain_on) {
            break;
        }
        if (!params_.rain.light) {
            dx_rain = prev_view_.x - view_.x;
            dz_rain = prev_view_.z - view_.z;
            p.tail.x = p.head.x;
            p.tail.z = p.head.z;
            p.tail.y = p.head.y - p.my;
        }
        p.head.x += scaled(wind_x_, dt) + dx_rain;
        p.my = static_cast<std::int16_t>(p.my + params_.rain.gravity());
        p.head.y += scaled(p.my << 2, dt);
        if (params_.rain.light) {
            p.tail.y = p.head.y - kEighth;
        }
        p.head.z += scaled(wind_z_, dt) + dz_rain;
        // The tail is last frame's head, so it takes the camera's shift too.
        p.tail.x += dx_rain;
        p.tail.z += dz_rain;
        if (params_.rain.light) {
            p.tail.x = p.head.x;
            p.tail.z = p.head.z;
        }
        if (moved_) {
            p.tail.x = p.head.x;
            p.tail.z = p.head.z;
        }

        if (std::abs(p.head.x) + std::abs(p.head.z) > q12(kRainRange)) {
            if (moved_) {
                randomize_position(c, rnd, p.head, kRainRange);
                p.tail.x = p.head.x;
                p.tail.z = p.head.z;
            } else {
                p.tail.x -= p.head.x;
                p.tail.z -= p.head.z;
                p.head.x = -p.head.x;
                p.head.z = -p.head.z;
                p.tail.x += p.head.x;
                p.tail.z += p.head.z;
            }
        }

        if (p.head.y >= 0) {
            const GroundSample g = host.ground(p.head.x + origin_.x, p.head.z + origin_.z);
            p.head.y = g.height;
            p.mx = static_cast<std::int16_t>(static_cast<std::int8_t>(g.type));
            if (g.type == 11) {
                // Grating: the drop becomes a windy streak.
                p.type = 3;
                p.tail.x = p.head.x;
                p.tail.z = p.head.z;
                p.tail.y = p.head.y - 96;
            }
            p.mz = 0;
            p.my = 0;
            p.step++;
        }
        break;
    }
    default:
        break;
    }
}

// Mirrors the point across the line a-b when it is on the far side; flag 1 only tests.
// point.y reports the side: 0 = crossed, 1 = not.
void WeatherSystem::clamp_to_line(const Vec3& a, const Vec3& b, int flag, Vec3& point) {
    if (a.x == -1) {
        point.y = 1;
        return;
    }
    point.x += origin_.x;
    point.z += origin_.z;
    const std::int32_t ox = point.x;
    const std::int32_t oz = point.z;

    const std::int32_t pa_x = (a.x - ox) >> 4;
    const std::int32_t pb_x = (b.x - ox) >> 4;
    const std::int32_t pa_z = (a.z - oz) >> 4;
    const std::int32_t pb_z = (b.z - oz) >> 4;
    const std::int32_t cross1 = mul(pa_x, pb_z);
    const std::int32_t cross2 = mul(pa_z, pb_x);

    if (cross2 < cross1) {
        if (flag == 0) {
            if (a.x == b.x) {
                const std::int32_t d = std::abs(ox - a.x) * 2;
                point.x += a.x < ox ? -d : d;
            } else {
                const std::int32_t d = std::abs(oz - a.z) * 2;
                point.z += a.z < oz ? -d : d;
            }
        }
        point.y = 0;
    } else {
        point.y = 1;
    }
    point.x -= origin_.x;
    point.z -= origin_.z;
}

void WeatherSystem::apply_aperture(Context& c, const ApertureKind& kind, Vec3& point, const Particle& p) {
    const auto run_chain = [&](const std::vector<ApertureLine>& lines) {
        for (const ApertureLine& l : lines) {
            clamp_to_line(corners_[static_cast<std::size_t>(l.a)], corners_[static_cast<std::size_t>(l.b)], l.flag, point);
            if (point.y != 0) {
                break;
            }
        }
    };

    if (kind.type == ApertureKind::Type::Roof) {
        if (p.head.y < corners_[0].y) {
            clamp_to_line(corners_[0], corners_[1], 0, point);
        } else {
            Vec3 a;
            Vec3 b;
            a.x = corners_[0].x;
            b.x = corners_[1].x;
            a.z = b.z = corners_[0].z + 0x1400 + (p.head.y >> 2);
            clamp_to_line(a, b, 0, point);
        }
        return;
    }
    if (kind.x_below && c.host.player().x < *kind.x_below) {
        run_chain(kind.override_lines);
        return;
    }
    run_chain(kind.lines);
    const int room = c.host.room();
    for (const ApertureKind::Extra& e : kind.extras) {
        if (e.room == room) {
            clamp_to_line(corners_[static_cast<std::size_t>(e.line.a)], corners_[static_cast<std::size_t>(e.line.b)],
                          e.line.flag, point);
        }
    }
}

void WeatherSystem::draw_snow(Context& c, Particle& p) {
    WeatherHost& host = c.host;
    const SnowParams& snow = params_.snow;

    if (!snow.hidden_rooms.empty() && contains(snow.hidden_rooms, host.room())) {
        return;
    }
    if (!snow.only_rooms.empty() && !contains(snow.only_rooms, host.room())) {
        return;
    }

    if (p.type >= 240) {
        // A flake that wrapped around the area fades out without being drawn.
        p.type = static_cast<std::uint8_t>(p.type + 16);
        if (p.step >= 31) {
            p.step = 0;
            return;
        }
        return;
    }
    if (p.step == 0) {
        return;
    }

    Vec3 at = p.head;
    if (snow.aperture && mode_ != 0) {
        const ApertureKind* kind = nullptr;
        for (const auto& [m, k] : snow.aperture->by_mode) {
            if (m == mode_) {
                kind = &k;
                break;
            }
        }
        if (kind == nullptr && snow.aperture->fallback) {
            kind = &*snow.aperture->fallback;
        }
        if (kind != nullptr) {
            apply_aperture(c, *kind, at, p);
        }
    }
    Vec3s rel;
    rel.x = static_cast<std::int16_t>(at.x >> 4);
    rel.y = static_cast<std::int16_t>(p.head.y >> 4);
    rel.z = static_cast<std::int16_t>(at.z >> 4);
    const ScreenPoint sp = host.project(rel);

    std::int32_t depth = sp.depth;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    const int light = host.light_mode();
    if (light == 1 || light == 4) {
        r = g = b = byte(c.frame.snow_color.r - (host.fog(depth) >> 2));
    } else {
        r = c.frame.snow_color.r;
        g = c.frame.snow_color.g;
        b = c.frame.snow_color.b;
    }
    depth >>= 1;

    if (p.step >= 2) {
        if (p.step < 31) {
            r = byte(r - ((r * p.step) >> 5));
            g = byte(g - ((g * p.step) >> 5));
            b = byte(b - ((b * p.step) >> 5));
        } else {
            p.step = 0;
            return;
        }
    }

    if (depth > 32 && depth < kOrderingTableSize - 1) {
        EffectPrim prim;
        prim.kind = EffectPrim::Kind::Sprite;
        prim.semi_transparent = true;
        prim.rgb[0] = Rgb{r, g, b};
        prim.x[0] = sp.x;
        prim.y[0] = sp.y;
        prim.v[0] = 112;
        prim.texture.page = 44;
        prim.texture.palette_x = 768;
        int size = 0;
        if (depth < 64) {
            prim.texture.palette_y = 96;
            prim.u[0] = 3;
            size = 4;
        } else if (depth >= 256) {
            prim.texture.palette_y = 103;
            prim.u[0] = 0;
            size = 1;
        } else {
            prim.texture.palette_y = clut_y((depth >> 5) + 96);
            prim.u[0] = 0;
            size = 3;
        }

        std::int32_t slot = depth;
        bool draw = true;
        if (snow.drifting) {
            if (slot > 64 && drift_clock_ > q12(7)) {
                slot = 64;
            }
        } else if (snow.far_slot_mode >= 0 && mode_ == snow.far_slot_mode) {
            slot = depth + 256;
            draw = slot < kOrderingTableSize - 1;
        }
        if (draw) {
            // The flake is a right triangle with its square corner at the top left, one
            // texel per pixel. The console fills the pixels strictly inside the slanted
            // edge, so row j of a flake `size` pixels wide has size - j pixels (10 of the
            // 16 for the near flake). Drawn as those rows, the flake keeps that stepped
            // shape at any resolution instead of a smooth triangle or a full square.
            prim.slot = slot;
            prim.h = 1;
            const std::int16_t top = prim.y[0];
            const std::uint8_t v0 = prim.v[0];
            for (int row = 0; row < size; row++) {
                prim.w = static_cast<std::int16_t>(size - row);
                prim.y[0] = static_cast<std::int16_t>(top + row);
                prim.v[0] = static_cast<std::uint8_t>(v0 + row);
                host.emit(prim);
            }
            host.note_flake(size);
        }
    }
}

void WeatherSystem::draw_rain(Context& c, Particle& p, int shift) {
    WeatherHost& host = c.host;
    const RainParams& rain = params_.rain;

    if (rain.boundary && p.step != 0 && mode_ != 0) {
        Vec3 at = p.head;
        at.y = -1;
        clamp_to_line(corners_[0], corners_[1], 0, at);
        // The streak's tail follows the head's mirror so the drop keeps its fall vector.
        p.tail.x += at.x - p.head.x;
        p.tail.z += at.z - p.head.z;
        p.head.x = at.x;
        p.head.z = at.z;
        if (at.y == 0) {
            p.step = 0;
            return;
        }
    }

    const auto project3 = [&](const Vec3& v, std::int32_t dx, std::int32_t dz) {
        Vec3s rel;
        rel.x = static_cast<std::int16_t>((v.x + dx) >> 4);
        rel.y = static_cast<std::int16_t>(v.y >> 4);
        rel.z = static_cast<std::int16_t>((v.z + dz) >> 4);
        return host.project(rel);
    };
    const auto lowest_clut = [](std::int32_t depth) -> std::uint16_t {
        return (depth > 63 && depth < 256) ? static_cast<std::uint16_t>((depth >> 5) + 0x68) : 104;
    };
    const auto texture = [&](std::uint16_t page, std::uint16_t row) {
        TextureRef t;
        t.page = page;
        t.palette_x = 768;
        t.palette_y = row;
        return t;
    };

    switch (p.step) {
    case 0:
        return;
    case 1: {
        // A falling drop: a one-pixel-wide streak from its tail to its head.
        const ScreenPoint a = project3(p.tail, 0, 0);
        std::int32_t depth = a.depth;
        const ScreenPoint b = project3(p.head, 0, 0);

        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t bl;
        const int light = host.light_mode();
        if (light == 1 || light == 4) {
            r = g = bl = byte(c.frame.rain_color.r - (host.fog(depth) >> 5));
        } else if (rain.light) {
            r = byte((c.frame.rain_color.r * 3) >> 1);
            g = byte((c.frame.rain_color.g * 3) >> 1);
            bl = byte((c.frame.rain_color.b * 3) >> 1);
        } else {
            r = c.frame.rain_color.r;
            g = c.frame.rain_color.g;
            bl = c.frame.rain_color.b;
        }
        depth >>= 1;

        if (depth > 32 && depth < kOrderingTableSize - 1) {
            EffectPrim prim;
            prim.kind = EffectPrim::Kind::Quad;
            prim.semi_transparent = true;
            prim.x = {a.x, b.x, static_cast<std::int16_t>(a.x + 1), static_cast<std::int16_t>(b.x + 1)};
            prim.y = {a.y, b.y, a.y, b.y};
            prim.u = {10, 10, 13, 13};
            prim.v = {112, 128, 112, 128};
            std::uint16_t page = 44;
            if (rain.match_anim && host.match_anim()) {
                prim.rgb[0] = Rgb{0x12, 8, 8};
            } else if (rain.pool_splash && p.mx == 5) {
                page = 0x4C;
                prim.rgb[0] = Rgb{byte((r * 2) / 3), byte((g * 2) / 3), bl};
            } else {
                prim.rgb[0] = Rgb{r, g, bl};
            }
            prim.texture = texture(page, lowest_clut(depth));
            prim.slot = depth;
            host.emit(prim);
        }
        return;
    }
    default:
        break;
    }

    if (p.type == 2 || p.type == 242) {
        // A drop that has landed: a splash square on the ground.
        std::int32_t cc;
        if (rain.pool_splash && p.mx == 5) {
            if (p.step < 25) {
                cc = 0xA3;
            } else {
                p.step = 0;
                return;
            }
        } else {
            if (p.step < 17) {
                cc = p.step;
                cc = cc << shift;
                if (p.head.y == INT32_MAX) {
                    return;
                }
            } else {
                p.step = 0;
                return;
            }
        }

        const ScreenPoint p0 = project3(p.head, -cc, -cc);
        const ScreenPoint p1 = project3(p.head, -cc, cc);
        const ScreenPoint p2 = project3(p.head, cc, -cc);
        const ScreenPoint p3 = project3(p.head, cc, cc);
        const std::int32_t depth = p0.depth >> 1;

        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t bl = 0;
        EffectPrim prim;
        prim.kind = EffectPrim::Kind::Quad;
        prim.semi_transparent = true;
        std::uint16_t page = 44;
        bool pool = rain.pool_splash && p.mx == 5;
        if (pool) {
            const std::uint8_t s = p.step;
            r = s >= 9 ? byte(0x40 - ((s - 8) * 4)) : 0x40;
            prim.u = {0x20, 0x20, 0x2F, 0x2F};
            prim.v = {0x70, 0x7F, 0x70, 0x7F};
            page = 0x4C;
        } else {
            std::int32_t comp = cc >> (shift - 3);
            comp = 0x80 - comp;
            const int light = host.light_mode();
            if (light == 2 || light == 6) {
                r = host.flashlight_on() ? byte(comp * 2) : byte(comp);
                g = bl = byte(comp);
            } else {
                r = g = bl = byte(comp);
            }
            prim.u = {16, 16, 31, 31};
            prim.v = {112, 127, 112, 127};
            const std::int32_t step = p.step;
            r = byte(r - ((r * step) >> 4));
            g = byte(g - ((g * step) >> 4));
            bl = byte(bl - ((bl * step) >> 4));
        }
        prim.x = {p0.x, p1.x, p2.x, p3.x};
        prim.y = {p0.y, p1.y, p2.y, p3.y};

        if (depth > 32 && depth < kOrderingTableSize - 1) {
            if (pool) {
                prim.texture = texture(page, static_cast<std::uint16_t>((depth >> 5) + 0x68));
                prim.rgb[0] = Rgb{r, r, r};
            } else {
                prim.texture = texture(page, lowest_clut(depth));
                if (rain.match_anim && host.match_anim()) {
                    prim.rgb[0] = Rgb{byte(r + 10), g, bl};
                } else {
                    prim.rgb[0] = Rgb{r, g, bl};
                }
            }
            prim.slot = depth;
            host.emit(prim);
        }
        return;
    }

    // Any other landed drop (grating): a streak that fades with the step.
    const std::int32_t comp = p.step < 31 ? p.step : 0x20;
    if (p.step >= 49) {
        p.step = 0;
        return;
    }
    const ScreenPoint a = project3(p.tail, 0, 0);
    const ScreenPoint b = project3(p.head, 0, 0);
    const std::int32_t depth = a.depth >> 1;
    if (depth > 32 && depth < kOrderingTableSize - 1) {
        EffectPrim prim;
        prim.kind = EffectPrim::Kind::Quad;
        prim.semi_transparent = true;
        prim.u = {10, 10, 13, 13};
        prim.v = {112, 128, 112, 128};
        prim.rgb[0] = Rgb{byte(comp), byte(comp), byte(comp + 0x18)};
        prim.x = {a.x, b.x, static_cast<std::int16_t>(a.x + 1), static_cast<std::int16_t>(b.x + 1)};
        prim.y = {a.y, static_cast<std::int16_t>(b.y + 1), a.y, static_cast<std::int16_t>(b.y + 1)};
        prim.texture = texture(44, lowest_clut(depth));
        prim.slot = depth;
        host.emit(prim);
    }
}

// The rain's loudness follows the number of drops, in steps of 15 per update.
void WeatherSystem::rain_sound(Context& c) {
    const int volume = static_cast<std::uint8_t>(rain_sound_ramp_ >> 2);
    if (rain_sound_ramp_ - counts_[1] > 15) {
        rain_sound_ramp_ -= 15;
    } else if (counts_[1] - rain_sound_ramp_ > 15) {
        rain_sound_ramp_ += 15;
    }
    if (rain_sound_ramp_ == 0) {
        rain_sound_on_ = false;
        c.host.rain_stop();
    }
    if (rain_sound_on_) {
        c.host.rain_loop(volume);
    } else if (rain_sound_ramp_ != 0) {
        c.host.rain_start();
        rain_sound_on_ = true;
    }
}

}  // namespace sh::effects
