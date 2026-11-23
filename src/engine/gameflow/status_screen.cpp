#include "engine/gameflow/status_screen.h"

#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

constexpr std::int32_t kRumbleThreshold = 256 * 4096;  // Q12 256.0

}  // namespace

Result<MapSoundStops> MapSoundStops::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<MapSoundStops>::fail("map_sound_stops.json: " + doc.error());
    }
    const json::Value* list = doc.value().find("stops");
    if (list == nullptr || !list->is_array()) {
        return Result<MapSoundStops>::fail("map_sound_stops.json: \"stops\" must be an array");
    }
    MapSoundStops out;
    for (const json::Value& item : list->array()) {
        const json::Value* map = item.is_object() ? item.find("map") : nullptr;
        const json::Value* sounds = item.is_object() ? item.find("sounds") : nullptr;
        if (map == nullptr || !map->is_string() || sounds == nullptr || !sounds->is_array()) {
            return Result<MapSoundStops>::fail("map_sound_stops.json: an entry needs a \"map\" name and a \"sounds\" array");
        }
        Entry e;
        e.map = map->string();
        for (const json::Value& s : sounds->array()) {
            if (!s.is_string()) {
                return Result<MapSoundStops>::fail("map_sound_stops.json: a sound must be a name");
            }
            e.sounds.push_back(s.string());
        }
        out.entries_.push_back(std::move(e));
    }
    return Result<MapSoundStops>::ok(std::move(out));
}

const std::vector<std::string>& MapSoundStops::sounds(std::string_view map) const {
    static const std::vector<std::string> kNone;
    for (const Entry& e : entries_) {
        if (e.map == map) {
            return e.sounds;
        }
    }
    return kNone;
}

void rumble_update(RumbleHost& h, const RumbleInput& in, const MapSoundStops& stops) {
    h.rumble_reset();
    if (in.level > kRumbleThreshold) {
        h.rumble_main(!in.rock_drill, (in.level - kRumbleThreshold) / 4096);
        if (in.rock_drill) {
            h.rumble_drill((in.drill_level - kRumbleThreshold) / 4096);
        }
    } else {
        h.rumble_main(!in.rock_drill, 0);
        if (in.rock_drill) {
            h.rumble_drill(0);
        }
    }
    for (const std::string& sound : stops.sounds(h.map_name())) {
        h.stop_sound(sound);
    }
}

void load_status_screen(StatusScreenHost& h, const MapSoundStops& stops) {
    if (h.step() == 0) {
        h.wait_for_draw();
        h.one_vblank_per_frame();
        h.reset_fade();
        rumble_update(h, h.rumble_input(), stops);
        if (h.streaming_audio()) {
            h.play_streaming_stop();
        }
        h.prepare_items();
        h.advance_step();
    }
    h.motion_blur();
    if (h.files_loaded()) {
        h.open_inventory();
    }
}

}  // namespace sh::gameflow
