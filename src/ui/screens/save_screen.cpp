// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The save and load screens: one list of every save in .savegames/, newest first,
// in the look of the original screen (its backdrop, the bevelled gold-and-silver boxes, the info box with Data,
// Save and Time). Saving makes a new save or overwrites the selected one; a save can be deleted. There are no
// memory cards, blocks or formatting. The save and the load are the engine's (engine/save/game_saves.h); the
// game around them goes through the PcSaveGame_* calls (ui/screens/save_screen_game.h).

#include "engine/save/game_saves.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/menu/scroll_window.h"
#include "engine/app/pc_paths.h"
#include "engine/save/save_c.h"
#include "engine/screens/save_layout.h"
#include "engine/text/game_text.h"
#include "engine/save/save_service.h"
#include "i18n/localization.h"
#include "ui/screens/save_screen_game.h"
#include "ui/menu/nm_draw.h"
#include "engine/mods/data_reload.h"
#include "engine/mods/save_mods_check.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

extern "C" const char* PcPort_GetGameDataPath(void);

namespace {

namespace gs = sh::game_state;
namespace nm = sh::ui::nm;
using sh::save::SaveEntry;
using sh::save::SlotState;

// Layout in the 320x224 text space (gamedata/menu/save_screen.json, built-in default on failure).
const sh::screens::SaveScreenLayout& L() {
    static sh::screens::SaveScreenLayout layout;
    static std::uint32_t layout_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(layout_epoch)) {
        layout = [] {
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            std::string source;
            sh::Result<sh::screens::SaveScreenLayout> r =
                sh::screens::load_save_layout(PcPort_GetGameDataPath(), mods, &source);
            if (!r) {
                SH_LOG_INFO(SAVE, "save screen layout: %s - built-in layout used", r.error().c_str());
                return sh::screens::SaveScreenLayout{};
            }
            return r.value();
        }();
    }
    return layout;
}


// The menu font colour names of save_screen.json are the game's text colours.
static_assert(sh::screens::kMenuTextColors[0].slot == gs::text_color::kGold &&
              sh::screens::kMenuTextColors[1].slot == gs::text_color::kDarkGrey &&
              sh::screens::kMenuTextColors[2].slot == gs::text_color::kGreen &&
              sh::screens::kMenuTextColors[3].slot == gs::text_color::kRed &&
              sh::screens::kMenuTextColors[4].slot == gs::text_color::kLightGrey &&
              sh::screens::kMenuTextColors[5].slot == gs::text_color::kWhite);

const sh::screens::SaveScreenColors& C() { return L().colors; }
const sh::screens::SaveScreenTexts& T() { return L().texts; }

enum class Step { List, Confirm, Message, AutoLoad, QuickLoad, Starting, Leaving, Done };
enum class Ask { Save, Overwrite, Delete, LoadMods };

struct Row {
    bool is_new = false;  // "New save" (the save screen's first row)
    SaveEntry entry;
    sh::mods::ModCheck mods;  // the save's mods against this run's
    bool newer = false;       // made by a newer version of the game: marked, never loaded
};

struct Screen {
    int kind = PcSaveScreen_Load;
    Step step = Step::Done;
    Ask ask = Ask::Save;
    bool yes = false;
    std::vector<Row> rows;
    sh::menu::ScrollWindow scroll;
    const char* message = nullptr;
    // The message in the font's spelling, cut in two when it is long; empty: `message` is drawn as it is.
    std::vector<std::string> message_lines;
    int message_ticks = 0;
    Step after_message = Step::List;
    int refresh = 0;
    int hover_answer = -1;
    int quick_slot = -1;
    std::string load_file;  // a save file to load instead of the quick slot
};

Screen s_screen;

const char* text(const char* key) { return sh::i18n::ui_menu_text(key); }

void sound(int id) { gs::sound_command(id); }

const char* location_key(const SaveEntry& e) {
    const auto& keys = L().location_keys;
    const int n = static_cast<int>(keys.size());
    return (e.location_id >= 0 && e.location_id < n ? keys[e.location_id] : keys[0]).c_str();
}

const char* difficulty_key(const SaveEntry& e) {
    if (!e.has_difficulty) {
        return nullptr;
    }
    return (e.difficulty < 0 ? T().difficulty_easy
                             : (e.difficulty == 0 ? T().difficulty_normal : T().difficulty_hard)).c_str();
}

void refresh_rows(int select_number) {
    Screen& s = s_screen;
    const int old_sel = s.scroll.selected();
    s.rows.clear();
    if (s.kind == PcSaveScreen_Save) {
        Row row;
        row.is_new = true;
        s.rows.push_back(row);
    }
    const std::vector<sh::mods::RunningMod> installed = sh::mods::running_mods();
    for (SaveEntry& e : sh::save::active_store().list()) {
        Row row;
        row.mods = sh::mods::check_save_mods(e.mods, installed);
        row.newer = e.newer_format ||
                    (e.state == SlotState::Ok && sh::save::active_store().from_newer_game(e.game_version));
        row.entry = std::move(e);
        s.rows.push_back(std::move(row));
    }
    s.scroll.set_count(static_cast<int>(s.rows.size()));
    int sel = std::min(old_sel, static_cast<int>(s.rows.size()) - 1);
    if (select_number > 0) {
        for (std::size_t i = 0; i < s.rows.size(); ++i) {
            if (!s.rows[i].is_new && s.rows[i].entry.number == select_number) {
                sel = static_cast<int>(i);
            }
        }
    }
    s.scroll.select(std::max(sel, 0));
}

// The number of the selected save, 0 for "New save" or an empty list.
int selected_number() {
    const Screen& s = s_screen;
    if (s.scroll.selected() < 0 || s.scroll.selected() >= static_cast<int>(s.rows.size()) || s.rows[s.scroll.selected()].is_new) {
        return 0;
    }
    return s.rows[s.scroll.selected()].entry.number;
}

const Row* selected_row() {
    const Screen& s = s_screen;
    return s.scroll.selected() >= 0 && s.scroll.selected() < static_cast<int>(s.rows.size()) ? &s.rows[s.scroll.selected()] : nullptr;
}

void show_message(const char* key, Step after) {
    Screen& s = s_screen;
    s.message = key;
    s.message_lines.clear();
    s.message_ticks = L().message_ticks;
    s.after_message = after;
    s.step = Step::Message;
}

// The width a line of text under or in the panels may take.
int line_width() { return L().list_w - 16; }

// Interface text (UTF-8, reading order) wider than `max_w` in the game font as two lines of about the same length:
// cut at the space nearest its middle, where it has none after the nearest comma (Japanese, Chinese),
// else between two characters. The lines are in the font's spelling.
std::vector<std::string> two_lines(const std::string& utf8, int max_w) {
    if (sh::i18n::text_width(nm::font_text(utf8).c_str()) <= max_w) {
        return {nm::font_text(utf8)};
    }
    // Where a second line may start, by preference: after a space, after a comma, at any character.
    std::vector<std::size_t> cuts[3];
    for (std::size_t i = 0; i < utf8.size();) {
        std::size_t n = 1;
        while (i + n < utf8.size() && (static_cast<unsigned char>(utf8[i + n]) & 0xC0) == 0x80) {
            ++n;
        }
        const std::string_view ch(utf8.data() + i, n);
        i += n;
        if (i >= utf8.size()) {
            break;
        }
        cuts[ch == " " ? 0 : (ch == "\xE3\x80\x81" || ch == "\xEF\xBC\x8C" || ch == ",") ? 1 : 2].push_back(i);
    }
    for (const std::vector<std::size_t>& set : cuts) {
        if (set.empty()) {
            continue;
        }
        const std::size_t middle = utf8.size() / 2;
        std::size_t best = set.front();
        for (const std::size_t cut : set) {
            const auto away = [middle](std::size_t at) { return at > middle ? at - middle : middle - at; };
            if (away(cut) < away(best)) {
                best = cut;
            }
        }
        std::string first = utf8.substr(0, best);
        while (!first.empty() && first.back() == ' ') {
            first.pop_back();
        }
        return {nm::font_text(first), nm::font_text(utf8.substr(best))};
    }
    return {nm::font_text(utf8)};
}

// A save of a newer version of the game was chosen: said in full, nothing is loaded and nothing asked.
void show_newer_refusal() {
    Screen& s = s_screen;
    show_message(T().newer_refused.c_str(), Step::List);
    s.message_lines = two_lines(sh::i18n::ui_text(T().newer_refused.c_str()), line_width());
    s.message_ticks = L().message_ticks * 2;  // two lines to read
}

void open_confirm(Ask ask) {
    Screen& s = s_screen;
    s.ask = ask;
    s.yes = false;
    s.hover_answer = -1;
    s.step = Step::Confirm;
}

void leave(bool boot) {
    Screen& s = s_screen;
    PcSaveGame_Leave(s.kind, boot ? 1 : 0);
    s.step = Step::Leaving;
}

void load(int number) {
    for (const Row& row : s_screen.rows) {
        if (!row.is_new && row.entry.number == number && !row.mods.empty()) {
            SH_LOG_WARN(SAVE, "save %d was made with other mods: %s", number,
                        sh::mods::describe_check(row.mods).c_str());
        }
    }
    const int result = sh::save::game_saves().load(number);
    if (result > 0) {
        sound(gs::sfx::kMenuConfirm);
        PcSaveGame_Start();
        s_screen.message = T().loading.c_str();
        s_screen.step = Step::Starting;
        return;
    }
    sound(gs::sfx::kMenuError);
    if (result == -2) {
        show_newer_refusal();
        return;
    }
    show_message(T().load_failed.c_str(), Step::List);
}

int row_at(const nm::Input& in) {
    const Screen& s = s_screen;
    if (!in.pointer || in.pointer_x < L().list_x || in.pointer_x >= L().list_x + L().list_w) {
        return -1;
    }
    for (int i = 0; i < L().visible_rows; ++i) {
        const int y = L().row_y + i * L().row_pitch - L().row_hit_dy;
        if (in.pointer_y >= y && in.pointer_y < y + L().row_pitch && s.scroll.top() + i < static_cast<int>(s.rows.size())) {
            return s.scroll.top() + i;
        }
    }
    return -1;
}

void activate() {
    Screen& s = s_screen;
    const Row* row = selected_row();
    if (row == nullptr) {
        sound(gs::sfx::kMenuError);
        return;
    }
    if (s.kind == PcSaveScreen_Save) {
        sound(gs::sfx::kMenuConfirm);
        open_confirm(row->is_new ? Ask::Save : Ask::Overwrite);
        return;
    }
    if (row->newer) {
        const SaveEntry& e = row->entry;
        SH_LOG_WARN(SAVE, "save %d was made with %s, newer than this game (%s): not loaded", e.number,
                    e.newer_format ? "a later save format" : ("version " + e.game_version).c_str(),
                    sh::save::active_store().game_version().c_str());
        sound(gs::sfx::kMenuError);
        show_newer_refusal();
        return;
    }
    if (row->entry.state != SlotState::Ok) {
        sound(gs::sfx::kMenuError);
        show_message(T().damaged.c_str(), Step::List);
        return;
    }
    // Made with mods that are not active (or older now): asked first, in the screen's own question.
    if (row->mods.warn) {
        sound(gs::sfx::kMenuConfirm);
        open_confirm(Ask::LoadMods);
        return;
    }
    load(row->entry.number);
}

void update_list(const nm::Input& in, bool del) {
    Screen& s = s_screen;
    const int old_sel = s.scroll.selected();

    if (in.cancel || in.right_click) {
        sound(gs::sfx::kMenuCancel);
        leave(false);
        return;
    }
    if (in.wheel != 0) {
        s.scroll.wheel(in.wheel);
    }
    const int hit = row_at(in);
    if (hit >= 0 && in.pointer_moved) {
        s.scroll.select(hit);
    }
    if (in.up) {
        s.scroll.move(-1, false);
    } else if (in.down) {
        s.scroll.move(+1, false);
    }
    if (s.scroll.selected() != old_sel) {
        sound(gs::sfx::kMenuMove);
    }
    if (del) {
        if (selected_number() > 0) {
            sound(gs::sfx::kMenuConfirm);
            open_confirm(Ask::Delete);
        } else {
            sound(gs::sfx::kMenuError);
        }
        return;
    }
    if (in.confirm || (in.click && hit >= 0)) {
        if (hit >= 0 && in.click) {
            s.scroll.select(hit);
        }
        activate();
    }
}

int answer_at(const nm::Input& in) {
    const int half = L().answer_hit_half_w;
    if (!in.pointer || in.pointer_y < L().info_line2 - L().answer_hit_top ||
        in.pointer_y >= L().info_line2 + L().answer_hit_bottom) {
        return -1;
    }
    if (in.pointer_x >= L().yes_cx - half && in.pointer_x < L().yes_cx + half) {
        return 1;
    }
    if (in.pointer_x >= L().no_cx - half && in.pointer_x < L().no_cx + half) {
        return 0;
    }
    return -1;
}

void confirmed() {
    Screen& s = s_screen;
    const int number = selected_number();
    if (s.ask == Ask::LoadMods) {
        load(number);
        return;
    }
    if (s.ask == Ask::Delete) {
        if (ShSave_Delete(number) > 0) {
            refresh_rows(0);
            show_message(T().deleted.c_str(), Step::List);
        } else {
            sound(gs::sfx::kMenuError);
            show_message(T().delete_failed.c_str(), Step::List);
        }
        return;
    }
    const int written = sh::save::game_saves().save(s.ask == Ask::Save ? 0 : number);
    if (written > 0) {
        refresh_rows(written);
        show_message(T().saved.c_str(), Step::List);
    } else {
        sound(gs::sfx::kMenuError);
        refresh_rows(0);
        show_message(T().save_failed.c_str(), Step::List);
    }
}

void update_confirm(const nm::Input& in) {
    Screen& s = s_screen;
    const int hit = answer_at(in);
    if (in.cancel || in.right_click) {
        sound(gs::sfx::kMenuCancel);
        s.step = Step::List;
        return;
    }
    if (hit >= 0 && in.pointer_moved && hit != s.hover_answer && (hit != 0) != s.yes) {
        s.yes = hit != 0;
        sound(gs::sfx::kMenuMove);
    }
    s.hover_answer = hit;
    if (in.left || in.right) {
        s.yes = !s.yes;
        sound(gs::sfx::kMenuMove);
    }
    if (in.confirm || (in.click && hit >= 0)) {
        if (in.click && hit >= 0) {
            s.yes = hit != 0;
        }
        if (!s.yes) {
            sound(gs::sfx::kMenuCancel);
            s.step = Step::List;
            return;
        }
        sound(gs::sfx::kMenuConfirm);
        confirmed();
    }
}

// The quick save was checked in gameplay before the screen opened; if it fails to load now, the game goes on
// unchanged (GameSaves::quick_load leaves the session alone).
void update_quick_load() {
    Screen& s = s_screen;
    const int loaded = s.load_file.empty() ? sh::save::game_saves().quick_load(s.quick_slot)
                                           : sh::save::game_saves().load_file(s.load_file);
    if (loaded > 0) {
        PcSaveGame_Start();
        s.message = T().loading.c_str();
        s.step = Step::Starting;
        return;
    }
    SH_LOG_WARN(SAVE, "quick load %d failed, back to the game", s.quick_slot + 1);
    SH_DBG_ECHO("%s", loaded == -2 ? sh::i18n::ui_text(T().newer_refused.c_str()) : text(T().load_failed.c_str()));
    leave(false);
}

void update_auto_load() {
    Screen& s = s_screen;
    for (const Row& row : s.rows) {
        if (row.newer) {
            // CONTINUE takes the newest save this game can load.
            SH_LOG_WARN(SAVE, "continue: save %d was made with %s, newer than this game (%s): skipped",
                        row.entry.number,
                        row.entry.newer_format ? "a later save format" : ("version " + row.entry.game_version).c_str(),
                        sh::save::active_store().game_version().c_str());
            continue;
        }
        if (row.entry.state == SlotState::Ok) {
            if (sh::save::game_saves().load(row.entry.number) > 0) {
                PcSaveGame_Start();
                s.message = T().loading.c_str();
                s.step = Step::Starting;
                return;
            }
            break;
        }
    }
    // Nothing to load: from the boot logo the title comes next, from the title the load list.
    if (PcSaveGame_FromBoot()) {
        leave(true);
        return;
    }
    PcSaveGame_ToLoadScreen();
    s.kind = PcSaveScreen_Load;
    s.step = Step::List;
    refresh_rows(0);
}

// ---- drawing ----

void text_draw(int x, int y, int color, const char* s) { nm::text(x, y, color, s); }

void verbatim_draw(int x, int y, int color, const char* s) { nm::text_verbatim(x, y, color, s); }

void verbatim_right(int right, int y, int color, const char* s) {
    verbatim_draw(right - sh::i18n::text_width(s), y, color, s);
}

void play_time_text(const SaveEntry& e, char* out, std::size_t size) {
    const long long seconds = static_cast<long long>(e.play_time_seconds);
    const long long hours = seconds / 3600 + static_cast<long long>(e.summary.extra_hours) * 290;
    std::snprintf(out, size, "%lld_:_%02lld_:_%02lld", hours, (seconds / 60) % 60, seconds % 60);
}

void date_text(const SaveEntry& e, char* out, std::size_t size) {
    out[0] = '\0';
    if (e.saved_at <= 0) {
        return;
    }
    const std::time_t t = static_cast<std::time_t>(e.saved_at);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::snprintf(out, size, "%s",
                  sh::menu::date_spelling(tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min).c_str());
}

void rows_draw() {
    const Screen& s = s_screen;
    PcSaveGame_PanelDraw(L().list_x, L().list_y, L().list_w, L().visible_rows * L().row_pitch + L().list_extra_h);
    if (s.rows.empty()) {
        nm::text_centred(L().center_x, L().row_y + L().empty_row * L().row_pitch, C().no_saves,
                            text(T().no_saves.c_str()));
        return;
    }
    for (int i = 0; i < L().visible_rows && s.scroll.top() + i < static_cast<int>(s.rows.size()); ++i) {
        const int index = s.scroll.top() + i;
        const Row& row = s.rows[index];
        const int y = L().row_y + i * L().row_pitch;
        const bool selected = index == s.scroll.selected();
        if (selected) {
            const int glow = s.step == Step::List ? L().select_glow : L().select_glow_idle;
            PcSaveGame_FrameDraw(L().list_x + L().select_inset_x, y - L().select_inset_y,
                                 L().list_w - 2 * L().select_inset_x, L().row_pitch - 1, glow);
        }
        if (row.is_new) {
            nm::text_centred(L().center_x, y, C().new_save, text(T().new_save.c_str()));
            continue;
        }
        const SaveEntry& e = row.entry;
        if (e.state != SlotState::Ok) {
            const char* what = text((e.newer_format ? T().newer_data : T().damaged).c_str());
            text_draw(L().row_text_x, y, C().damaged, what);
            if (e.newer_format) {
                verbatim_draw(L().row_text_x + nm::text_width(what) + 6, y, C().newer_version, "!");
            }
            continue;
        }
        // Next Fear saves are gold, as on the original screen.
        const int color = e.summary.next_fear ? C().row_next_fear : C().row;
        text_draw(L().row_text_x, y, color, text(location_key(e)));
        if (row.newer || row.mods.warn) {
            // a save of a newer game, or made with mods that are not active: marked in the list, the line below
            // says which
            verbatim_draw(L().row_text_x + nm::text_width(text(location_key(e))) + 6, y,
                          row.newer ? C().newer_version : C().mods_warn, "!");
        }
        if (const char* key = difficulty_key(e)) {
            const char* word = text(key);
            nm::text_right(L().row_text_right, y, C().difficulty, word);
        }
    }
    if (static_cast<int>(s.rows.size()) > L().visible_rows) {
        nm::scroll_arrow(L().arrow_x, L().row_y + L().arrow_up_dy, true, s.scroll.can_scroll_up());
        nm::scroll_arrow(L().arrow_x, L().row_y + (L().visible_rows - 1) * L().row_pitch + L().arrow_down_dy, false,
                         s.scroll.can_scroll_down());
    }
}

// A line in the game font's spelling centred on the panels. One wider than they are is drawn smaller to fit, and
// past the smallest size that still reads it pans inside their width. `sized_as` (optional): a wider line drawn
// with this one, whose size both take.
void fitted_centred(int y, int color, const std::string& line, const std::string* sized_as = nullptr) {
    constexpr int kFull = 4096, kSmallest = 2662, kRowHeight = 16;
    const int max_w = line_width();
    const int w = sh::i18n::text_width(line.c_str());
    const int widest = std::max(w, sized_as != nullptr ? sh::i18n::text_width(sized_as->c_str()) : 0);
    const int scale = widest > max_w ? std::max(kSmallest, max_w * kFull / widest) : kFull;
    const int shown = std::min(max_w, (w * scale) >> 12);
    sh::text::game_text().set_scale(scale);
    nm::text_fitted(L().center_x - shown / 2, y + (kRowHeight - ((kRowHeight * scale) >> 12)) / 2, color, line.c_str(),
                    max_w, true);
    sh::text::game_text().set_scale(kFull);
}

void info_draw() {
    const Screen& s = s_screen;
    PcSaveGame_PanelDraw(L().list_x, L().info_y, L().list_w, L().info_h);

    if (s.step == Step::Confirm) {
        const std::string& question = s.ask == Ask::Save        ? T().confirm_save
                                      : s.ask == Ask::Overwrite ? T().confirm_overwrite
                                      : s.ask == Ask::LoadMods  ? T().confirm_mods
                                                                : T().confirm_delete;
        nm::text_centred(L().center_x, L().info_line1, C().question, text(question.c_str()));
        nm::text_centred(L().yes_cx, L().info_line2, C().answer, text(T().yes.c_str()));
        nm::text_centred(L().no_cx, L().info_line2, C().answer, text(T().no.c_str()));
        PcSaveGame_FrameDraw((s.yes ? L().yes_cx : L().no_cx) - L().answer_frame_w / 2,
                             L().info_line2 - L().answer_frame_dy, L().answer_frame_w, L().answer_frame_h,
                             L().answer_frame_glow);
        return;
    }
    if (s.step == Step::Message || s.step == Step::Starting) {
        if (s.step == Step::Message && s.message_lines.size() == 2) {
            fitted_centred(L().info_line1, C().message, s.message_lines[0], &s.message_lines[1]);
            fitted_centred(L().info_line2, C().message, s.message_lines[1], &s.message_lines[0]);
        } else if (s.step == Step::Message && s.message_lines.size() == 1) {
            fitted_centred((L().info_line1 + L().info_line2) / 2, C().message, s.message_lines[0]);
        } else if (s.message != nullptr) {
            nm::text_centred(L().center_x, (L().info_line1 + L().info_line2) / 2, C().message,
                                text(s.message));
        }
        return;
    }

    const Row* row = selected_row();
    if (row == nullptr) {
        return;
    }
    if (row->is_new) {
        nm::text_centred(L().center_x, (L().info_line1 + L().info_line2) / 2, C().message,
                            text(T().new_save_info.c_str()));
        return;
    }
    const SaveEntry& e = row->entry;
    char buf[64];
    if (e.state != SlotState::Ok) {
        std::snprintf(buf, sizeof(buf), "%0*d", L().number_digits, e.number);
        text_draw(L().info_label_x, L().info_line1, C().info_label, text(T().data.c_str()));
        verbatim_draw(L().info_value_x, L().info_line1, C().info_value, buf);
        text_draw(L().info_label_x, L().info_line2, C().damaged,
                  text((e.newer_format ? T().newer_data : T().damaged).c_str()));
        return;
    }
    // Data (the save's number), Time (play time) on the first line; Save (its save count), the date on the second.
    text_draw(L().info_label_x, L().info_line1, C().info_label, text(T().data.c_str()));
    std::snprintf(buf, sizeof(buf), "%0*d", L().number_digits, e.number);
    verbatim_draw(L().info_value_x, L().info_line1, C().info_value, buf);
    text_draw(L().info_time_x, L().info_line1, C().info_label, text(T().time.c_str()));
    play_time_text(e, buf, sizeof(buf));
    verbatim_right(L().row_text_right, L().info_line1, C().info_value, buf);

    text_draw(L().info_label_x, L().info_line2, C().info_label, text(T().save_count.c_str()));
    std::snprintf(buf, sizeof(buf), "%d", std::clamp<int>(e.summary.save_count, 0, L().save_count_max));
    verbatim_draw(L().info_value_x, L().info_line2, C().info_value, buf);
    date_text(e, buf, sizeof(buf));
    if (buf[0] != '\0') {
        verbatim_right(L().row_text_right, L().info_line2, C().date, buf);
    }
}

// "Mods: Cybil: The Other Side (not enabled), ..." in the game font, cut to the list's width ("+N" for the rest).
std::string mods_line(const sh::mods::ModCheck& check) {
    using sh::mods::ModChange;
    const auto word = [](ModChange c) -> const std::string& {
        switch (c) {
        case ModChange::Missing: return T().mod_missing;
        case ModChange::NotEnabled: return T().mod_disabled;
        case ModChange::OlderNow: return T().mod_older;
        case ModChange::NewerNow: return T().mod_newer;
        case ModChange::AddedNow: break;
        }
        return T().mod_added;
    };
    const std::string head = sh::i18n::ui_text(T().mods_line.c_str());
    const int width = line_width();
    std::string line;
    for (std::size_t i = 0; i < check.differences.size(); ++i) {
        const sh::mods::ModDifference& d = check.differences[i];
        std::string next = (line.empty() ? head + " " : line + ", ") + d.name + " (" +
                           sh::i18n::ui_text(word(d.change).c_str()) + ")";
        const std::size_t rest = check.differences.size() - i - 1;
        const std::string shown = nm::font_text(rest > 0 ? next + " +" + std::to_string(rest) : next);
        if (!line.empty() && sh::i18n::text_width(shown.c_str()) > width) {
            return nm::font_text(line + " +" + std::to_string(check.differences.size() - i));
        }
        line = std::move(next);
    }
    return nm::font_text(line);
}

// "Version 1.4.0: newer than this game (1.3.0)" in the game font.
std::string newer_line(const SaveEntry& e) {
    std::string line = sh::i18n::ui_text(T().newer_line.c_str());
    const auto fill = [&line](std::string_view name, const std::string& value) {
        if (const std::size_t at = line.find(name); at != std::string::npos) {
            line.replace(at, name.size(), value);
        }
    };
    fill("{saved}", e.game_version);
    fill("{game}", sh::save::active_store().game_version());
    return nm::font_text(line);
}

// How to delete the selected save, for the device in use: the keys, the touch gesture and button, or the pad's button
// as its picture where the text has its {button}.
void delete_hint_draw() {
    using sh::screens::SaveHintDevice;
    const auto device = static_cast<SaveHintDevice>(PcSaveGame_HintDevice());
    const std::string& key = sh::screens::delete_hint_key(T(), device);
    if (device == SaveHintDevice::Keys) {
        nm::text_centred(L().center_x, L().hint_y, C().hint, text(key.c_str()));
        return;
    }
    if (device == SaveHintDevice::Touch) {
        fitted_centred(L().hint_y, C().hint, nm::font_text(sh::i18n::ui_text(key.c_str())));
        return;
    }
    constexpr int kGap = 2;
    const sh::screens::ButtonSplit parts = sh::screens::split_at_button(sh::i18n::ui_text(key.c_str()));
    const int glyph_w = parts.found ? PcSaveGame_DeleteGlyphWidth() : 0;
    const std::string before = nm::font_text(parts.before), after = nm::font_text(parts.after);
    const std::string name = parts.found && glyph_w == 0 ? nm::font_text(PcSaveGame_DeleteButtonName()) : "";
    const int before_w = sh::i18n::text_width(before.c_str());
    const int middle_w = glyph_w > 0 ? glyph_w + 2 * kGap : sh::i18n::text_width(name.c_str());
    int x = L().center_x - (before_w + middle_w + sh::i18n::text_width(after.c_str())) / 2;
    verbatim_draw(x, L().hint_y, C().hint, before.c_str());
    x += before_w;
    if (glyph_w > 0) {
        PcSaveGame_DeleteGlyphDraw(x + kGap, L().hint_y);
    } else {
        verbatim_draw(x, L().hint_y, C().hint, name.c_str());
    }
    verbatim_draw(x + middle_w, L().hint_y, C().hint, after.c_str());
}

void draw() {
    const Screen& s = s_screen;
    PcSaveGame_BackgroundDraw(s.kind);
    nm::text_centred(L().center_x, L().heading_y, C().heading,
                        text((s.kind == PcSaveScreen_Save ? T().heading_save : T().heading_load).c_str()));
    if (s.kind == PcSaveScreen_AutoLoad || s.kind == PcSaveScreen_QuickLoad) {
        nm::text_centred(L().center_x, (L().info_line1 + L().info_line2) / 2, C().message,
                            text(T().loading.c_str()));
        return;
    }
    rows_draw();
    const Row* sel = selected_row();
    const bool lists = s.step == Step::List || s.step == Step::Confirm;
    if (lists && sel != nullptr && !sel->is_new && sel->newer && !sel->entry.newer_format) {
        // in place of the delete hint: the version that made the save, which this game does not load
        fitted_centred(L().hint_y, C().newer_version, newer_line(sel->entry));
    } else if (lists && sel != nullptr && !sel->is_new && !sel->mods.empty()) {
        // in place of the delete hint: what differs in the save's mods
        fitted_centred(L().hint_y, sel->mods.warn ? C().mods_warn : C().mods_note, mods_line(sel->mods));
    } else if (s.step == Step::List && selected_number() > 0) {
        delete_hint_draw();
    }
    info_draw();
}

}  // namespace

extern "C" void Pc_SaveScreen_Update(int kind, int entering) {
    Screen& s = s_screen;
    if (entering) {
        s = Screen{};
        s.kind = kind;
        s.scroll = sh::menu::ScrollWindow(0, L().visible_rows);
        PcSaveGame_Enter(kind);
        refresh_rows(0);
        s.step = kind == PcSaveScreen_AutoLoad ? Step::AutoLoad : Step::List;
        if (kind == PcSaveScreen_QuickLoad) {
            s.quick_slot = PcSaveGame_TakeQuickLoadSlot();
            s.load_file = PcSaveGame_TakeLoadFile();
            s.step = Step::QuickLoad;
        }
        static const char* const kKindNames[] = {"save", "load", "auto load", "quick load"};
        SH_LOG_INFO(SAVE, "save screen: %s, %d save(s), from state %d", kKindNames[kind & 3],
                    static_cast<int>(s.rows.size()) - (kind == PcSaveScreen_Save ? 1 : 0),
                    static_cast<int>(gs::game_state_previous()));
    }

    const nm::Input in = nm::read_input();
    const bool del = PcSaveGame_DeletePressed() != 0;

    switch (s.step) {
        case Step::AutoLoad:
            update_auto_load();
            break;
        case Step::QuickLoad:
            update_quick_load();
            break;
        case Step::List:
            // Saves copied in or removed by hand show up while the screen is open.
            if (++s.refresh >= L().refresh_ticks) {
                s.refresh = 0;
                refresh_rows(selected_number());
            }
            if (selected_number() > 0) {
                PcSaveGame_AskTouchDelete();
            }
            update_list(in, del);
            break;
        case Step::Confirm:
            update_confirm(in);
            break;
        case Step::Message:
            if (--s.message_ticks <= 0 || in.confirm || in.cancel || in.click) {
                s.step = s.after_message;
            }
            break;
        case Step::Starting:
            if (PcSaveGame_StartDone()) {
                s.step = Step::Done;
            }
            break;
        case Step::Leaving:
            if (PcSaveGame_LeaveDone(s.kind)) {
                s.step = Step::Done;
            }
            break;
        case Step::Done:
            break;
    }
    draw();
}
