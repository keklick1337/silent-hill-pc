#include "engine/gameflow/state_names.h"

#include "engine/game_state/game_state.h"

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
    {"KonamiLogo", static_cast<int>(gs::GameState::KonamiLogo)},
    {"KcetLogo", static_cast<int>(gs::GameState::KcetLogo)},
    {"MovieIntroFadeIn", static_cast<int>(gs::GameState::MovieIntroFadeIn)},
    {"AutoLoadSavegame", static_cast<int>(gs::GameState::AutoLoadSavegame)},
    {"MovieIntroAlternate", static_cast<int>(gs::GameState::MovieIntroAlternate)},
    {"MovieIntro", static_cast<int>(gs::GameState::MovieIntro)},
    {"MainMenu", static_cast<int>(gs::GameState::MainMenu)},
    {"LoadSavegameScreen", static_cast<int>(gs::GameState::LoadSavegameScreen)},
    {"MovieOpening", static_cast<int>(gs::GameState::MovieOpening)},
    {"MainLoadScreen", static_cast<int>(gs::GameState::MainLoadScreen)},
    {"InGame", static_cast<int>(gs::GameState::InGame)},
    {"MapEvent", static_cast<int>(gs::GameState::MapEvent)},
    {"ExitMovie", static_cast<int>(gs::GameState::ExitMovie)},
    {"InventoryScreen", static_cast<int>(gs::GameState::InventoryScreen)},
    {"PaperMapScreen", static_cast<int>(gs::GameState::PaperMapScreen)},
    {"SaveScreen", static_cast<int>(gs::GameState::SaveScreen)},
    {"DebugMoviePlayer", static_cast<int>(gs::GameState::DebugMoviePlayer)},
    {"OptionScreen", static_cast<int>(gs::GameState::OptionScreen)},
    {"LoadStatusScreen", static_cast<int>(gs::GameState::LoadStatusScreen)},
    {"LoadMapScreen", static_cast<int>(gs::GameState::LoadMapScreen)},
};

template <std::size_t N>
std::int32_t find(const Named (&table)[N], std::string_view name) {
    for (const Named& n : table) {
        if (name == n.name) return n.value;
    }
    return -1;
}

template <std::size_t N>
std::string_view name_of(const Named (&table)[N], std::int32_t value) {
    for (const Named& n : table) {
        if (n.value == value) return n.name;
    }
    return {};
}

}  // namespace

std::string_view game_state_name(std::int32_t value) {
    return name_of(kGameStates, value);
}

std::string_view sys_state_name(std::int32_t value) {
    return name_of(kSysStates, value);
}

std::int32_t game_state_by_name(std::string_view name) {
    return find(kGameStates, name);
}

std::int32_t sys_state_by_name(std::string_view name) {
    return find(kSysStates, name);
}

}  // namespace sh::gameflow
