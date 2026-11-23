#include "engine/gameflow/scene_owner.h"

#include <algorithm>
#include <utility>

#include "engine/game_state/game_state.h"
#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

namespace gs = sh::game_state;

struct Named {
    const char* name;
    std::int32_t value;
};

constexpr Named kSysStates[] = {
    {"Gameplay", static_cast<int>(gs::SysState::Gameplay)},
    {"OptionsMenu", static_cast<int>(gs::SysState::OptionsMenu)},
    {"StatusMenu", static_cast<int>(gs::SysState::StatusMenu)},
    {"MapScreen", static_cast<int>(gs::SysState::MapScreen)},
    {"Fmv", static_cast<int>(gs::SysState::Fmv)},
    {"LoadOverlay", static_cast<int>(gs::SysState::LoadOverlay)},
    {"LoadRoom", static_cast<int>(gs::SysState::LoadRoom)},
    {"ReadMessage", static_cast<int>(gs::SysState::ReadMessage)},
    {"SaveMenu0", static_cast<int>(gs::SysState::SaveMenu0)},
    {"SaveMenu1", static_cast<int>(gs::SysState::SaveMenu1)},
    {"EventCallback", static_cast<int>(gs::SysState::EventCallback)},
    {"EventSetFlag", static_cast<int>(gs::SysState::EventSetFlag)},
    {"EventPlaySound", static_cast<int>(gs::SysState::EventPlaySound)},
    {"GameOver", static_cast<int>(gs::SysState::GameOver)},
    {"GamePaused", static_cast<int>(gs::SysState::GamePaused)},
};

constexpr Named kGameStates[] = {
    {"Init", static_cast<int>(gs::GameState::Init)},
    {"MainMenu", static_cast<int>(gs::GameState::MainMenu)},
    {"MainLoadScreen", static_cast<int>(gs::GameState::MainLoadScreen)},
    {"InGame", static_cast<int>(gs::GameState::InGame)},
    {"MapEvent", static_cast<int>(gs::GameState::MapEvent)},
    {"InventoryScreen", static_cast<int>(gs::GameState::InventoryScreen)},
    {"PaperMapScreen", static_cast<int>(gs::GameState::PaperMapScreen)},
    {"SaveScreen", static_cast<int>(gs::GameState::SaveScreen)},
    {"OptionScreen", static_cast<int>(gs::GameState::OptionScreen)},
    {"LoadStatusScreen", static_cast<int>(gs::GameState::LoadStatusScreen)},
    {"LoadMapScreen", static_cast<int>(gs::GameState::LoadMapScreen)},
};

template <std::size_t N>
Result<std::vector<std::int32_t>> names_to_values(const json::Value& doc, const char* key, const Named (&table)[N]) {
    using R = Result<std::vector<std::int32_t>>;
    const json::Value* arr = doc.find(key);
    if (arr == nullptr || !arr->is_array()) {
        return R::fail(std::string("scene_owner.json: \"") + key + "\" must be an array of state names");
    }
    std::vector<std::int32_t> out;
    for (const json::Value& v : arr->array()) {
        bool found = false;
        if (v.is_string()) {
            for (const Named& n : table) {
                if (v.string() == n.name) {
                    out.push_back(n.value);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return R::fail(std::string("scene_owner.json: unknown state in \"") + key + "\"");
        }
    }
    return R::ok(std::move(out));
}

std::vector<std::int32_t> values_of(std::initializer_list<gs::SysState> l) {
    std::vector<std::int32_t> v;
    for (auto s : l) v.push_back(static_cast<int>(s));
    return v;
}

}  // namespace

SceneOwnerParams SceneOwnerParams::builtin() {
    using S = gs::SysState;
    using G = gs::GameState;
    SceneOwnerParams p;
    p.player_sys_states = values_of({S::ReadMessage, S::StatusMenu, S::MapScreen, S::OptionsMenu, S::SaveMenu0,
                                     S::SaveMenu1, S::GamePaused});
    for (G g : {G::LoadStatusScreen, G::InventoryScreen, G::LoadMapScreen, G::PaperMapScreen, G::SaveScreen,
                G::OptionScreen}) {
        p.player_game_states.push_back(static_cast<int>(g));
    }
    return p;
}

Result<SceneOwnerParams> SceneOwnerParams::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<SceneOwnerParams>::fail("scene_owner.json: " + doc.error());
    }
    SceneOwnerParams out = builtin();
    if (const json::Value* b = doc.value().find("world_anchor_base")) {
        if (!b->is_integer() || b->integer() < -1000 || b->integer() > 1000) {
            return Result<SceneOwnerParams>::fail("scene_owner.json: \"world_anchor_base\" must be an integer from -1000 to 1000");
        }
        out.anchor_base = static_cast<std::int32_t>(b->integer());
    }
    auto sys = names_to_values(doc.value(), "player_sys_states", kSysStates);
    if (!sys) return Result<SceneOwnerParams>::fail(sys.error());
    auto game = names_to_values(doc.value(), "player_game_states", kGameStates);
    if (!game) return Result<SceneOwnerParams>::fail(game.error());
    out.player_sys_states = sys.value();
    out.player_game_states = game.value();
    return Result<SceneOwnerParams>::ok(std::move(out));
}

bool SceneOwner::contains(const std::vector<std::int32_t>& v, std::int32_t x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

bool SceneOwner::owns_scene(const SceneSnapshot& s) const {
    if (s.cutscene_flag || s.border_active) return true;
    if (contains(params_.player_sys_states, s.sys_state)) return false;
    if (contains(params_.player_game_states, s.game_state)) return false;
    return s.camera_user_flags && s.control_disabled;
}

bool SceneOwner::owns_shot(const SceneSnapshot& s) const {
    if (s.cutscene_flag || s.border_active) return true;
    if (s.fade_in_started) return false;
    return owns_scene(s);
}

std::int32_t SceneOwner::world_anchor(const AnchorSnapshot& s) {
    constexpr std::int32_t in_game = static_cast<int>(gs::GameState::InGame);
    constexpr std::int32_t gameplay = static_cast<int>(gs::SysState::Gameplay);
    if (!held_set_) {
        held_ = params_.anchor_base;
        held_set_ = true;
    }
    if (s.game_state == in_game && s.sys_state == gameplay && !s.pickup_active) {
        std::int32_t ofy = params_.anchor_base;
        if (s.cutscene_active) {
            ofy += s.cutscene_shift;
        } else if (!s.third_person) {
            ofy += s.world_shift;
        }
        held_ = ofy;
        return ofy;
    }
    if (s.game_state == in_game) {
        if (s.cutscene_active || s.border_active) return params_.anchor_base + s.cutscene_shift;
        return held_;
    }
    return params_.anchor_base;
}

}  // namespace sh::gameflow
