#include "engine/loader/queue/boot_audio.h"

#include <string>

#include "formats/common/json.h"

namespace sh::loader {
namespace {

Result<std::vector<std::uint16_t>> table(const json::Value& root, const char* key) {
    using R = Result<std::vector<std::uint16_t>>;
    const json::Value* v = root.find(key);
    if (v == nullptr || !v->is_array() || v->array().empty()) {
        return R::fail(std::string("\"") + key + "\" must be a non-empty array");
    }
    std::vector<std::uint16_t> out;
    for (const json::Value& e : v->array()) {
        if (!e.is_integer() || e.integer() < 0 || e.integer() > 65535) {
            return R::fail(std::string("\"") + key + "\" holds only integers in 0..65535");
        }
        out.push_back(static_cast<std::uint16_t>(e.integer()));
    }
    return R::ok(std::move(out));
}

}  // namespace

Result<BootAudio> BootAudio::parse(std::string_view json_text) {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<BootAudio>::fail(doc.error());
    }
    BootAudio a;
    auto bl = table(doc.value(), "bgm_load_commands");
    if (!bl) return Result<BootAudio>::fail(bl.error());
    auto bc = table(doc.value(), "bgm_channel_commands");
    if (!bc) return Result<BootAudio>::fail(bc.error());
    auto al = table(doc.value(), "ambient_load_commands");
    if (!al) return Result<BootAudio>::fail(al.error());
    a.bgm_load_ = std::move(bl.value());
    a.bgm_channels_ = std::move(bc.value());
    a.ambient_load_ = std::move(al.value());
    return Result<BootAudio>::ok(std::move(a));
}

BootStep music_init_step(std::int32_t& step, MusicInitHost& host) {
    if (host.audio_or_queue_busy()) {
        return BootStep::Wait;
    }
    switch (step) {
    case 0:
        host.restore_kept_track();
        step++;
        [[fallthrough]];
    case 1:
        if (!host.track_differs()) {
            step += 2;  // nothing to change: skip the fade and the load
        } else {
            host.fade_out_and_mute();
            step++;
        }
        return BootStep::Run;
    case 2:
        if (host.sound_idle()) {
            host.load_map_track();
            step++;
        }
        return BootStep::Run;
    default:
        return BootStep::Done;
    }
}

BootStep ambient_init_step(std::int32_t& step, AmbientInitHost& host) {
    if (host.audio_or_queue_busy()) {
        return BootStep::Wait;
    }
    switch (step) {
    case 0:
        if (host.target_differs()) {
            host.fade_out_ambient();
            step++;
            return BootStep::Run;
        }
        return BootStep::Done;
    case 1:
        host.load_target_ambient();
        step++;
        return BootStep::Run;
    default:
        return BootStep::Done;
    }
}

}  // namespace sh::loader
