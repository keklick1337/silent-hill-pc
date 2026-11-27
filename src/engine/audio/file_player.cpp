#include "engine/audio/file_player.h"

#include <algorithm>
#include <cmath>

namespace sh::audio {

namespace {
constexpr std::uint32_t kMixRate = 44100;
}

void FilePlayer::play(std::shared_ptr<const std::vector<std::int16_t>> pcm, int channels, std::uint32_t rate,
                      double rate_scale, int left, int right, bool loop, int tag) {
    if (!pcm || pcm->empty() || channels < 1 || channels > 2 || rate == 0 || !(rate_scale > 0.0)) {
        return;
    }
    Voice* slot = nullptr;
    for (Voice& v : voices_) {
        if (!v.on) {
            slot = &v;
            break;
        }
    }
    if (slot == nullptr) {
        slot = std::min_element(std::begin(voices_), std::end(voices_),
                                [](const Voice& a, const Voice& b) { return a.serial < b.serial; });
    }
    Voice& v = *slot;
    v.frames = pcm->size() / static_cast<std::size_t>(channels);
    v.pcm = std::move(pcm);
    v.channels = channels;
    v.position = 0;
    v.step = static_cast<std::uint64_t>(std::llround(static_cast<double>(rate) * rate_scale / kMixRate * 4294967296.0));
    v.step = std::max<std::uint64_t>(v.step, 1);
    v.left = std::clamp(left, 0, 4 * kUnity);
    v.right = std::clamp(right, 0, 4 * kUnity);
    v.loop = loop;
    v.tag = tag;
    v.serial = ++serial_;
    v.on = v.frames > 0;
    v.fade = v.fade_target = static_cast<std::int64_t>(kFadeUnity) << 32;
    v.fade_step = 0;
    v.fade_stops = false;
    for (auto it = start_levels_.begin(); it != start_levels_.end(); ++it) {
        if (it->tag == tag) {
            v.fade = v.fade_target = static_cast<std::int64_t>(it->level) << 32;
            start_levels_.erase(it);
            break;
        }
    }
}

bool FilePlayer::fade(int tag, int level, std::uint32_t frames, bool stop_at_end) {
    level = std::clamp(level, 0, 4 * kFadeUnity);
    const std::int64_t target = static_cast<std::int64_t>(level) << 32;
    bool any = false;
    for (Voice& v : voices_) {
        if (!v.on || v.tag != tag) {
            continue;
        }
        any = true;
        v.fade_target = target;
        v.fade_stops = stop_at_end;
        if (frames == 0 || v.fade == target) {
            v.fade = target;
            v.fade_step = 0;
            if (stop_at_end) {
                v = Voice{};
            }
        } else {
            v.fade_step = (target - v.fade) / static_cast<std::int64_t>(frames);
            if (v.fade_step == 0) {
                v.fade_step = target > v.fade ? 1 : -1;
            }
        }
    }
    if (!any && !stop_at_end) {
        for (StartLevel& s : start_levels_) {
            if (s.tag == tag) {
                s.level = level;
                return false;
            }
        }
        start_levels_.push_back(StartLevel{tag, level});
    }
    return any;
}

void FilePlayer::stop(int tag) {
    for (Voice& v : voices_) {
        if (v.on && v.tag == tag) {
            v = Voice{};
        }
    }
}

void FilePlayer::stop_all() {
    for (Voice& v : voices_) {
        v = Voice{};
    }
}

void FilePlayer::set_volume(int tag, int left, int right) {
    for (Voice& v : voices_) {
        if (v.on && v.tag == tag) {
            v.left = std::clamp(left, 0, 4 * kUnity);
            v.right = std::clamp(right, 0, 4 * kUnity);
        }
    }
}

int FilePlayer::active(int tag) const {
    return static_cast<int>(std::count_if(std::begin(voices_), std::end(voices_),
                                          [tag](const Voice& v) { return v.on && v.tag == tag; }));
}

int FilePlayer::active_count() const {
    return static_cast<int>(std::count_if(std::begin(voices_), std::end(voices_), [](const Voice& v) { return v.on; }));
}

std::vector<FilePlayer::VoiceInfo> FilePlayer::voices() const {
    std::vector<VoiceInfo> out;
    for (const Voice& v : voices_) {
        if (v.on) {
            out.push_back(VoiceInfo{v.tag, v.left, v.right, v.loop, static_cast<int>(v.fade >> 32)});
        }
    }
    return out;
}

void FilePlayer::mix(std::int32_t* left, std::int32_t* right, std::uint32_t frames) {
    for (Voice& v : voices_) {
        if (!v.on) {
            continue;
        }
        const std::int16_t* data = v.pcm->data();
        const int ch = v.channels;
        const std::int64_t unity = static_cast<std::int64_t>(kFadeUnity) << 32;
        for (std::uint32_t i = 0; i < frames; ++i) {
            std::uint64_t index = v.position >> 32;
            if (index >= v.frames) {
                if (!v.loop) {
                    v = Voice{};
                    break;
                }
                v.position %= v.frames << 32;
                index = v.position >> 32;
            }
            const std::uint64_t next = index + 1 < v.frames ? index + 1 : (v.loop ? 0 : index);
            const auto frac = static_cast<std::int32_t>((v.position >> 17) & 0x7FFF);  // 15 bits
            const auto sample = [&](int c) {
                const std::int32_t a = data[index * ch + c];
                const std::int32_t b = data[next * ch + c];
                return a + (((b - a) * frac) >> 15);
            };
            const std::int32_t l = sample(0);
            const std::int32_t r = ch == 2 ? sample(1) : l;
            if (v.fade == unity && v.fade_step == 0) {
                left[i] += (l * v.left) >> 14;
                right[i] += (r * v.right) >> 14;
            } else {
                const std::int64_t level = v.fade >> 32;  // of kFadeUnity
                left[i] += static_cast<std::int32_t>((((static_cast<std::int64_t>(l) * v.left) >> 14) * level) >> 16);
                right[i] += static_cast<std::int32_t>((((static_cast<std::int64_t>(r) * v.right) >> 14) * level) >> 16);
                if (v.fade_step != 0) {
                    v.fade += v.fade_step;
                    if ((v.fade_step > 0) == (v.fade >= v.fade_target)) {
                        v.fade = v.fade_target;
                        v.fade_step = 0;
                        if (v.fade_stops) {
                            v = Voice{};
                            break;
                        }
                    }
                }
            }
            v.position += v.step;
        }
    }
}

}  // namespace sh::audio
