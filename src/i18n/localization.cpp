// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n/localization.h"

#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/log/sh_log.h"
#include "engine/text/game_text.h"
#include "engine/text/message_layout.h"
#include "i18n/core/text_prepare.h"
#include "i18n/core/unicode.h"
#include "i18n/game_font.h"
#include "i18n/text_bridge.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <deque>
#include <filesystem>
#include <unordered_map>

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::i18n {
namespace {

namespace fs = std::filesystem;
namespace gs = sh::game_state;

constexpr const char* kBaseLanguage = "en";
// Slots a map's message table always has, so a stray index reads an empty string.
constexpr std::size_t kMessageSlots = 176;
// The block of messages every map's table opens with.
constexpr int kCommonMessages = 15;
// Tables of maps left behind are kept this long: a message on screen may still point into one.
constexpr std::size_t kKeptTables = 4;
constexpr std::size_t kReadyCacheLimit = 4096;

struct MessageTable {
    std::deque<std::string> strings;  // the messages that had to be rewritten
    std::vector<const char*> slots;
};

struct ReadyText {
    bool changed = false;
    std::string text;
};

struct State {
    bool started = false;
    std::vector<Language> languages;
    int current = 0;
    unsigned generation = 1;
    DrawMode mode = DrawMode::Translate;
    std::unordered_map<std::string, ReadyText> ready;
    std::deque<MessageTable> tables;
    int installed_map = -1;
    std::array<std::string, 8> ring;
    std::size_t ring_at = 0;
};

State& state() {
    static State s;
    return s;
}

Language english_without_descriptor() {
    Language lang;
    lang.code = kBaseLanguage;
    lang.name = "English";
    lang.english_name = "English";
    return lang;
}

std::vector<Language> find_languages() {
    std::vector<fs::path> dirs;
    Pc_TextEachDir([](const char* dir, void* user) { static_cast<std::vector<fs::path>*>(user)->emplace_back(dir); },
                   &dirs);
    std::vector<std::string> warnings;
    std::vector<Language> found = discover_languages(dirs, warnings);
    for (const std::string& w : warnings) {
        SH_WARN("[LANG] %s", w.c_str());
    }
    // English is the base every other language falls back to: it exists even when its descriptor is lost.
    if (std::none_of(found.begin(), found.end(), [](const Language& l) { return l.code == kBaseLanguage; })) {
        SH_WARN("[LANG] gamedata/text/en/lang.json is missing: English is listed without it");
        found.insert(found.begin(), english_without_descriptor());
    }
    return found;
}

int index_of(const std::vector<Language>& list, const std::string& code) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].code == code) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Direction direction() { return language().direction; }

// What a string of the game font needs before it is drawn; cached, because menus draw the same strings every frame.
const ReadyText& make_ready(State& s, const char* text) {
    const auto it = s.ready.find(text);
    if (it != s.ready.end()) {
        return it->second;
    }
    if (s.ready.size() >= kReadyCacheLimit) {
        s.ready.clear();  // numbers and timers fill it up over a long session
    }
    PrepareOptions options;
    options.dialect = TextDialect::String;
    options.base = direction();
    PreparedText prepared = prepare_text(text, options);
    ReadyText entry;
    entry.changed = prepared.changed;
    entry.text = std::move(prepared.text);
    return s.ready.emplace(text, std::move(entry)).first->second;
}

int font_advance(char32_t code) { return GameFont::get().advance(static_cast<std::uint32_t>(code)); }

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Selects language `index` for the text store and the font; the config is the caller's business.
void apply_language(State& s, int index) {
    s.current = std::clamp(index, 0, static_cast<int>(s.languages.size()) - 1);
    const Language& lang = s.languages[static_cast<std::size_t>(s.current)];
    Pc_TextSetLang(lang.code.c_str());
    GameFont::get().set_preferred_families(lang.fonts);
    GameFont::get().set_drawn_cell_height(lang.glyph_cell_height);
    s.ready.clear();
    ++s.generation;
}

bool map_is_loaded() {
    return gs::game_state() == gs::GameState::InGame && gs::map_header_loaded() && gs::save_active();
}

}  // namespace

void init() {
    State& s = state();
    s.started = true;
    s.languages = find_languages();
    std::string wanted = g_PcConfig.languageCode;
    if (wanted.empty() || wanted == "auto") {
        wanted = kBaseLanguage;
    }
    int index = index_of(s.languages, wanted);
    if (index < 0) {
        SH_WARN("[LANG] language '%s' (config key language) has no text directory with a lang.json: English is used",
                wanted.c_str());
        index = index_of(s.languages, kBaseLanguage);
    }
    apply_language(s, index);
    GameFont::get().active();
    std::string list;
    for (const Language& l : s.languages) {
        list += (list.empty() ? "" : " ") + l.code;
    }
    SH_LOG("[LANG] %zu language(s): %s; in force: %s (%s)%s", s.languages.size(), list.c_str(), language().code.c_str(),
           language().english_name.c_str(), right_to_left() ? ", right to left" : "");
}

void reload() {
    State& s = state();
    const std::string code = language().code;
    Pc_TextReload();
    GameFont::get().reload();
    s.languages = find_languages();
    int index = index_of(s.languages, code);
    if (index < 0) {
        SH_WARN("[LANG] language '%s' is gone with the mod that brought it: English is used", code.c_str());
        index = index_of(s.languages, kBaseLanguage);
    }
    apply_language(s, index);
    if (map_is_loaded()) {
        install_map_messages(gs::save_map_index());
    }
}

const std::vector<Language>& languages() {
    State& s = state();
    if (s.languages.empty()) {
        s.languages.push_back(english_without_descriptor());  // asked before init: only English is known
    }
    return s.languages;
}

int language_index() { return state().current; }

const Language& language() {
    const std::vector<Language>& list = languages();
    const int i = state().current;
    return list[static_cast<std::size_t>(i >= 0 && i < static_cast<int>(list.size()) ? i : 0)];
}

bool set_language(int index) {
    State& s = state();
    if (index < 0 || index >= static_cast<int>(languages().size())) {
        return false;
    }
    if (index == s.current) {
        return true;
    }
    apply_language(s, index);
    // The code, not the position: installing a mod with a language moves the positions.
    std::snprintf(g_PcConfig.languageCode, sizeof g_PcConfig.languageCode, "%s", language().code.c_str());
    PcConfig_SaveKeyValue("language", language().code.c_str());
    if (map_is_loaded()) {
        install_map_messages(gs::save_map_index());
    }
    SH_LOG("[LANG] language switched to '%s'", language().code.c_str());
    return true;
}

bool right_to_left() { return language().direction == Direction::Rtl; }

unsigned language_generation() { return state().generation; }

std::string language_label(int index) {
    const std::vector<Language>& list = languages();
    if (index < 0 || index >= static_cast<int>(list.size())) {
        return {};
    }
    std::string label = list[static_cast<std::size_t>(index)].name;
    std::replace(label.begin(), label.end(), ' ', '_');
    return label;
}

bool language_switch_allowed() { return gs::game_state_previous() == gs::GameState::MainMenu; }

const char* menu_text(const char* literal) {
    if (literal == nullptr) {
        return literal;
    }
    const char* text = Pc_TextMenuText(literal);
    return text != nullptr ? text : literal;
}

const char* ui_menu_text(const char* key) {
    if (key == nullptr) {
        return "";
    }
    const char* text = Pc_TextGetIn(key, PC_TEXT_UPPER, 1);
    if (text == nullptr || text[0] == '\0') {
        text = Pc_TextGetIn(key, PC_TEXT_EN, 1);
    }
    return text != nullptr ? text : key;
}

ScopedDrawMode::ScopedDrawMode(DrawMode mode) : before_(state().mode) { state().mode = mode; }
ScopedDrawMode::~ScopedDrawMode() { state().mode = before_; }

DrawMode draw_mode() { return state().mode; }

const char* ready(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return text;
    }
    const ReadyText& r = make_ready(state(), text);
    return r.changed ? r.text.c_str() : text;
}

const char* shown(const char* text) {
    switch (state().mode) {
        case DrawMode::Ready: return text;
        case DrawMode::Verbatim: return ready(text);
        case DrawMode::Translate: break;
    }
    return ready(menu_text(text));
}

int text_width(const char* text) {
    if (text == nullptr) {
        return 0;
    }
    GameFont& font = GameFont::get();
    const sh::text::FontAtlas atlas = font.atlas();
    int width = 0;
    for (const char* p = ready(text); *p != '\0' && *p != '\n';) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '_') {
            width += atlas.space_advance;
        } else if (c == '\v') {
            width += atlas.wide_space_advance;
        } else if (c == 0x01) {
            width -= 1;
        } else if (c >= static_cast<unsigned char>(atlas.glyph_byte_min)) {
            std::uint32_t code = c == '!' ? '\\' : c == '&' ? '^' : c;
            if (c >= 0x80) {
                p += sh::text::read_wide_char(p, code) - 1;
            }
            width += font.advance(code);
        }
        ++p;
    }
    return width;
}

int glyph_count(const char* text) {
    if (text == nullptr) {
        return 0;
    }
    const int first = GameFont::get().atlas().glyph_byte_min;
    int count = 0;
    for (const char* p = ready(text); *p != '\0';) {
        const unsigned char c = static_cast<unsigned char>(*p);
        std::uint32_t code = c;
        p += c >= 0x80 ? sh::text::read_wide_char(p, code) : 1;
        if (c != '_' && c >= first) {
            ++count;
        }
    }
    return count;
}

const char* ui_text(const char* key) {
    if (key == nullptr) {
        return "";
    }
    const char* text = Pc_TextGetIn(key, PC_TEXT_UPPER, 0);
    if (text == nullptr || text[0] == '\0') {
        text = Pc_TextGetIn(key, PC_TEXT_EN, 0);
    }
    return text != nullptr ? text : key;
}

const char* ui_text_of(const char* english) {
    if (english == nullptr) {
        return "";
    }
    const char* text = Pc_TextUiText(english);
    return text != nullptr && text[0] != '\0' ? text : english;
}

const char* ui_text_filled(const char* english, const char* placeholder, const char* value) {
    State& s = state();
    std::string text = ui_text_of(english);
    std::size_t at = text.find(placeholder);
    if (at == std::string::npos) {
        // A translation that lost the placeholder: the English keeps it.
        text = english != nullptr ? english : "";
        at = text.find(placeholder);
    }
    if (at != std::string::npos) {
        text.replace(at, std::strlen(placeholder), value != nullptr ? value : "");
    }
    s.ring_at = (s.ring_at + 1) % s.ring.size();
    s.ring[s.ring_at] = std::move(text);
    return s.ring[s.ring_at].c_str();
}

const char* item_name(int index) {
    const char* text = Pc_TextItemIn(index, 0, PC_TEXT_UPPER);
    return text != nullptr ? text : Pc_TextItemEnglish(index, 0);
}

const char* item_description(int index) {
    const char* text = Pc_TextItemIn(index, 1, PC_TEXT_UPPER);
    return text != nullptr ? text : Pc_TextItemEnglish(index, 1);
}

void install_map_messages(int map_index) {
    State& s = state();
    const char* map = gs::map_name(map_index);
    const int count = map != nullptr ? Pc_TextMapCount(map) : 0;
    Pc_TextSetMap(count > 0 ? map : "");
    if (count <= 0 || !gs::map_header_loaded()) {
        return;  // no table of message keys for the map: its own messages stay
    }
    const int total = std::max(count, Pc_TextMapTotal(map));

    // Lines are measured the way the message box measures them and broken to its width: the widest line that
    // stays on the screen, less for lines that start at the inset.
    const sh::text::MessageSpec box;
    const sh::text::FontAtlas atlas = GameFont::get().atlas();
    PrepareOptions options;
    options.dialect = TextDialect::Message;
    options.base = direction();
    options.wrap_width = box.fit_half_width * 2;
    options.inset_wrap_width = box.fit_half_width - box.tab_x;
    options.advance = font_advance;
    options.space_advance = atlas.space_advance;
    options.wide_space_advance = atlas.wide_space_advance;
    options.initial_color = sh::text::game_text().message_color();

    s.tables.emplace_back();
    if (s.tables.size() > kKeptTables) {
        s.tables.pop_front();
    }
    MessageTable& table = s.tables.back();
    table.slots.assign(std::max<std::size_t>(kMessageSlots, static_cast<std::size_t>(total)), "");
    int translated = 0;
    int wrapped = 0;
    int rewritten = 0;
    for (int slot = 0; slot < total; ++slot) {
        const char* text = Pc_TextMapMsg(map, slot, PC_TEXT_UPPER);
        if (text != nullptr) {
            ++translated;
        } else {
            text = Pc_TextMapMsg(map, slot, PC_TEXT_EN);
        }
        if (text == nullptr) {
            continue;
        }
        PreparedText prepared = prepare_text(text, options);
        if (prepared.changed) {
            wrapped += prepared.wrapped;
            ++rewritten;
            table.strings.push_back(std::move(prepared.text));
            text = table.strings.back().c_str();
        }
        table.slots[static_cast<std::size_t>(slot)] = text;
    }
    gs::map_messages_install(gs::MapMessageCopy::Lang, table.slots.data());
    s.installed_map = map_index;
    GameFont::get().flush();
    SH_LOG("[LANG] %s: %d messages, %d in '%s'; %d made ready for the drawer, %d line break(s) added", map, total,
           translated, language().code.c_str(), rewritten, wrapped);
}

std::string message_voice_name(int map_index, int slot) {
    if (slot < 0) {
        return {};
    }
    if (slot < kCommonMessages) {
        return "COMMON." + std::to_string(slot);
    }
    const char* map = gs::map_name(map_index);
    if (map == nullptr) {
        return {};
    }
    std::string name = map;
    for (char& c : name) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return name + "." + std::to_string(slot);
}

void describe(void (*out)(const char*, ...)) {
    State& s = state();
    out("languages (text directories with a lang.json): %zu", languages().size());
    for (std::size_t i = 0; i < languages().size(); ++i) {
        const Language& l = languages()[i];
        std::string fonts;
        for (const std::string& f : l.fonts) {
            fonts += (fonts.empty() ? "" : ", ") + f;
        }
        out("  %c %-6s %s (%s), %s%s%s", static_cast<int>(i) == s.current ? '*' : ' ', l.code.c_str(), l.name.c_str(),
            l.english_name.c_str(), l.direction == Direction::Rtl ? "right to left" : "left to right",
            fonts.empty() ? "" : ", fonts: ", fonts.c_str());
    }
    out("in force: %s; its own text: %d keys (%d menu.*, %d ui.*, %d item.*, %d common.*)", language().code.c_str(),
        Pc_TextCountKeys(nullptr), Pc_TextCountKeys("menu."), Pc_TextCountKeys("ui."), Pc_TextCountKeys("item."),
        Pc_TextCountKeys("common."));
    if (s.installed_map >= 0 && gs::map_name(s.installed_map) != nullptr) {
        const std::string prefix = lower(gs::map_name(s.installed_map)) + ".";
        out("map %s: %d message slots, %d of its keys in the language's own text", gs::map_name(s.installed_map),
            Pc_TextMapTotal(gs::map_name(s.installed_map)), Pc_TextCountKeys(prefix.c_str()));
    }
    out("strings made ready and kept: %zu", s.ready.size());
    GameFont::get().describe(out);
}

}  // namespace sh::i18n
