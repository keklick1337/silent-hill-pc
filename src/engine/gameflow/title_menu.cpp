#include "engine/gameflow/title_menu.h"

#include <algorithm>

#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

constexpr int kMaxEntries = 32;  // the visibility set is a 32-bit mask

std::string at(const json::Value& v, std::string_view what) {
    return "line " + std::to_string(v.line()) + ", column " + std::to_string(v.column()) + ": " + std::string(what);
}

bool kind_from(std::string_view name, TitleEntryKind& out) {
    static constexpr struct {
        std::string_view name;
        TitleEntryKind kind;
    } kKinds[] = {{"load", TitleEntryKind::Load},       {"continue", TitleEntryKind::Continue},
                  {"new_game", TitleEntryKind::NewGame}, {"options", TitleEntryKind::Options},
                  {"exit", TitleEntryKind::Exit}};
    for (const auto& k : kKinds) {
        if (k.name == name) {
            out = k.kind;
            return true;
        }
    }
    return false;
}

// Reads an optional string member; a present member must be a string.
bool text(const json::Value& obj, std::string_view key, std::string& out, std::string& error) {
    const json::Value* v = obj.find(key);
    if (v == nullptr || v->is_null()) {
        return true;
    }
    if (!v->is_string()) {
        error = at(*v, "\"" + std::string(key) + "\" must be a string");
        return false;
    }
    out = v->string();
    return true;
}

// Reads an optional integer member in [lo, hi].
bool number(const json::Value& obj, std::string_view key, int lo, int hi, int& out, std::string& error) {
    const json::Value* v = obj.find(key);
    if (v == nullptr || v->is_null()) {
        return true;
    }
    if (!v->is_integer() || v->integer() < lo || v->integer() > hi) {
        error = at(*v, "\"" + std::string(key) + "\" must be an integer from " + std::to_string(lo) + " to " +
                           std::to_string(hi));
        return false;
    }
    out = static_cast<int>(v->integer());
    return true;
}

// An entry kind that is always listed (the others depend on saves).
bool always_shown(TitleEntryKind k) {
    return k == TitleEntryKind::NewGame || k == TitleEntryKind::Options || k == TitleEntryKind::Exit;
}

}  // namespace

Result<TitleMenuData> TitleMenuData::parse(std::string_view json_text) {
    using R = Result<TitleMenuData>;
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return R::fail("title_menu.json: " + doc.error());
    }
    const json::Value& root = doc.value();
    TitleMenuData out;
    std::string error;

    const json::Value* list = root.find("entries");
    if (list == nullptr || !list->is_array() || list->array().empty() ||
        list->array().size() > static_cast<std::size_t>(kMaxEntries)) {
        return R::fail("title_menu.json: \"entries\" must hold 1 to 32 entries");
    }
    for (const json::Value& item : list->array()) {
        if (!item.is_object()) {
            return R::fail("title_menu.json: " + at(item, "an entry must be an object"));
        }
        TitleEntry e;
        std::string kind;
        if (!text(item, "kind", kind, error) || !kind_from(kind, e.kind)) {
            return R::fail("title_menu.json: " +
                           (error.empty() ? at(item, "unknown entry kind \"" + kind + "\"") : error));
        }
        if (!text(item, "label", e.label, error) || !number(item, "offset_x", 0, 320, e.offset_x, error) ||
            !text(item, "sound", e.sound, error) || !text(item, "next", e.next, error) ||
            !text(item, "next_alt", e.next_alt, error)) {
            return R::fail("title_menu.json: " + error);
        }
        if (e.label.empty()) {
            return R::fail("title_menu.json: " + at(item, "an entry needs a \"label\""));
        }
        out.entries.push_back(std::move(e));
    }

    if (const json::Value* l = root.find("layout"); l != nullptr) {
        TitleLayout& y = out.layout;
        if (!l->is_object() || !number(*l, "column_x", 0, 640, y.column_x, error) ||
            !number(*l, "row_pitch", 1, 100, y.row_pitch, error) ||
            (l->find("touch_row_pitch") != nullptr &&
             !number(*l, "touch_row_pitch", 1, 100, y.touch_row_pitch, error)) ||
            !number(*l, "last_row_y", -512, 512, y.last_row_y, error) ||
            !number(*l, "hover_lead", 0, 100, y.hover_lead, error) ||
            !number(*l, "difficulty_y", -512, 512, y.difficulty_y, error) ||
            !number(*l, "difficulty_offset_x", 0, 320, y.difficulty_offset_x, error) ||
            !text(*l, "difficulty_align", y.difficulty_align, error) ||
            (y.difficulty_align != "center" && y.difficulty_align != "left")) {
            return R::fail("title_menu.json: " + (error.empty() ? std::string("\"layout\" must be an object") : error));
        }
    }

    const json::Value* c = root.find("exit_confirm");
    if (c == nullptr || !c->is_object()) {
        return R::fail("title_menu.json: \"exit_confirm\" must be an object");
    }
    std::string row = "options";
    if (!text(*c, "question", out.confirm.question, error) || !text(*c, "row", row, error) ||
        !number(*c, "answer_spread", 0, 320, out.confirm.answer_spread, error)) {
        return R::fail("title_menu.json: " + error);
    }
    if (!kind_from(row, out.confirm.row_entry)) {
        return R::fail("title_menu.json: " + at(*c, "unknown \"row\" entry kind \"" + row + "\""));
    }
    const json::Value* answers = c->find("answers");
    if (answers == nullptr || !answers->is_array() || answers->array().size() != 2 ||
        !answers->array()[0].is_string() || !answers->array()[1].is_string()) {
        return R::fail("title_menu.json: \"exit_confirm\".\"answers\" must be [yes, no]");
    }
    out.confirm.answers[0] = answers->array()[0].string();
    out.confirm.answers[1] = answers->array()[1].string();

    const json::Value* s = root.find("sounds");
    if (s == nullptr || !s->is_object() || !text(*s, "move", out.sounds.move, error) ||
        !text(*s, "cancel", out.sounds.cancel, error) || !text(*s, "start_game", out.sounds.start_game, error) ||
        !text(*s, "open_confirm", out.sounds.open_confirm, error)) {
        return R::fail("title_menu.json: " + (error.empty() ? std::string("\"sounds\" must be an object") : error));
    }

    if (!number(root, "idle_frames", 1, 1 << 30, out.idle_frames, error) ||
        !number(root, "intro_every", 1, 1000, out.intro_every, error) ||
        !text(root, "idle_state", out.idle_state, error)) {
        return R::fail("title_menu.json: " + error);
    }
    return R::ok(std::move(out));
}

int TitleMenuData::find(TitleEntryKind kind) const {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].kind == kind) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void TitleMenu::set_data(TitleMenuData data) {
    data_ = std::move(data);
    visible_ = 0;
    for (int i = 0; i < count(); ++i) {
        if (always_shown(data_.entries[i].kind)) {
            visible_ |= 1u << i;
        }
    }
    const int start = data_.find(TitleEntryKind::NewGame);
    selected_ = start >= 0 ? start : 0;
}

void TitleMenu::select_continue() {
    const int e = data_.find(TitleEntryKind::Continue);
    if (e >= 0) {
        selected_ = e;
    }
}

bool TitleMenu::request_pick(TitleEntryKind kind) {
    const int e = data_.find(kind);
    if (state_ != State::Main || confirm_open_ || e < 0 || !shown(e)) {
        return false;
    }
    pick_request_ = e;
    return true;
}

int TitleMenu::visible_rows(int* order) const {
    int n = 0;
    for (int i = 0; i < count(); ++i) {
        if (shown(i)) {
            order[n++] = i;
        }
    }
    return n;
}

int TitleMenu::row_y(int k) const {
    int order[kMaxEntries];
    const int n = visible_rows(order);
    return data_.layout.last_row_y - (n - 1 - k) * pitch();
}

int TitleMenu::pitch() const {
    return touch_ && data_.layout.touch_row_pitch > 0 ? data_.layout.touch_row_pitch : data_.layout.row_pitch;
}

// The pointer's band of a row starts this far above its text; a touch row's band is centred on it.
int TitleMenu::lead() const {
    return touch_ && data_.layout.touch_row_pitch > 0 ? std::max(data_.layout.hover_lead, (pitch() - 14) / 2)
                                                      : data_.layout.hover_lead;
}

// The taller touch rows of the difficulty list end no lower than the menu's last row.
int TitleMenu::difficulty_top(const TitleHost& host) const {
    const int rows = host.difficulty_count();
    if (!touch_ || rows <= 0) return data_.layout.difficulty_y;
    return std::min(data_.layout.difficulty_y, data_.layout.last_row_y - (rows - 1) * pitch());
}

int TitleMenu::confirm_y() const {
    int order[kMaxEntries];
    const int n = visible_rows(order);
    int k = 0;
    while (k < n && data_.entries[order[k]].kind != data_.confirm.row_entry) {
        ++k;
    }
    return row_y(k);
}

int TitleMenu::half_width(const TitleHost& host, const char* label) const {
    return (host.text_width(label) + host.text_width("[")) >> 1;
}

void TitleMenu::frame(TitleHost& host) {
    touch_ = host.touch_screen();
    if (data_.entries.empty()) {
        return;  // no menu data (the bridge logged why)
    }
    if (skip_step_ == 0 && host.skip_to_game()) {
        skip_step_ = 1;
    }
    host.poll_saves();

    // Every intro_every-th idle timeout plays the intro movie; the others boot the attract demo, which loads in the
    // background while the menu is up. Not while the player answers the exit question.
    const bool demo = host.attract_demos() && !confirm_open_ &&
                      (host.demo_count() + 1) % data_.intro_every != 0;

    if (host.state_step() == 0) {
        state_ = State::Start;
        if (demo) {
            host.request_demo();
        } else {
            host.set_state_step(1);
        }
    }

    switch (state_) {
    case State::Start:
        host.open_title_screen();
        state_ = State::Main;
        main_frame(host, demo);
        break;
    case State::Main:
        main_frame(host, demo);
        break;
    case State::Difficulty:
        difficulty_frame(host, demo);
        break;
    case State::LoadGame:
    case State::NewGameStart:
        hand_off(host);
        break;
    }
    if (quit_) {
        return;
    }

    if (host.pad().held_any) {
        host.reset_idle();
    }
    // Reading the exit question is not idling.
    if (confirm_open_) {
        host.reset_idle();
    }
    idle_timer(host, demo);
    draw(host);
}

void TitleMenu::demo_tick(TitleHost& host) {
    host.demo_startup();
    if (host.state_step() == 1 && host.idle_frames() == 0) {
        host.set_demo_count(host.demo_count() + 1);
    }
    if (host.demo_loaded()) {
        host.set_demo_count(host.demo_count() + 1);
    }
}

void TitleMenu::update_visible(TitleHost& host) {
    const int load = data_.find(TitleEntryKind::Load);
    const int cont = data_.find(TitleEntryKind::Continue);
    auto bit = [](int entry) { return entry >= 0 ? 1u << entry : 0u; };

    visible_ = 0;
    for (int i = 0; i < count(); ++i) {
        if (always_shown(data_.entries[i].kind)) {
            visible_ |= 1u << i;
        }
    }
    if (host.autosave_alive()) {
        visible_ |= bit(cont);
    }
    const int saves = host.save_count();
    if (saves > 0) {
        visible_ |= bit(load) | bit(cont);
        // A save that appeared (a memory card put in) moves the cursor to continue, unless it is on load.
        if (previous_saves_ < saves && selected_ != load && cont >= 0) {
            selected_ = cont;
        }
    } else if (previous_saves_ > 0) {
        // The saves went away: step down the list to an entry that is still shown.
        while (selected_ < count() - 1 && !shown(selected_)) {
            ++selected_;
        }
    }

    // An entry hidden under the cursor: up the list first, so the cursor lands on
    // the entry above rather than on one below it, where the next confirm could quit.
    int e = selected_;
    while (e >= 0 && !shown(e)) {
        --e;
    }
    if (e < 0) {
        e = selected_;
        while (e < count() - 1 && !shown(e)) {
            ++e;
        }
    }
    selected_ = e;
}

void TitleMenu::main_frame(TitleHost& host, bool demo) {
    if (demo) {
        demo_tick(host);
    }
    update_visible(host);

    const bool confirm_active = confirm_open_;
    if (confirm_active) {
        confirm_frame(host);
        if (quit_) {
            return;
        }
    }

    TitlePad p = host.pad();
    if (p.up || p.down) {
        host.play_sound(data_.sounds.move);
        host.stay_in_title();
        if (host.state_step() != 1) {
            host.set_state_step(1);
            host.reset_file_queue();
        }
    }
    const int n = count();
    if (p.up) {
        for (int k = 1; k <= n; ++k) {
            const int e = ((selected_ - k) % n + n) % n;
            if (shown(e)) {
                selected_ = e;
                break;
            }
        }
    }
    if (p.down) {
        for (int k = 1; k <= n; ++k) {
            const int e = (selected_ + k) % n;
            if (shown(e)) {
                selected_ = e;
                break;
            }
        }
    }

    if (!confirm_active) {
        mouse_rows(host);
    }

    if (skip_step_ == 1) {
        const int start = data_.find(TitleEntryKind::NewGame);
        if (start >= 0) {
            selected_ = start;
        }
        host.press_enter();
        skip_step_ = 2;
    }

    if (pick_request_ >= 0) {
        if (!confirm_active && shown(pick_request_)) {
            selected_ = pick_request_;
            host.press_enter();
        }
        pick_request_ = -1;
    }

    if (data_.entries[selected_].kind == TitleEntryKind::Exit && host.pad().enter) {
        confirm_open_ = true;
        confirm_yes_ = false;
        host.play_sound(data_.sounds.open_confirm);
        host.release_enter();
    }

    if (host.pad().enter) {
        pick(host);
        if (quit_) {
            return;
        }
    }
    previous_saves_ = host.save_count();
}

int TitleMenu::confirm_hover(const TitleHost& host, const TitleMouse& m) const {
    const int pitch = this->pitch();
    const int top = confirm_y() + pitch - lead();
    if (!m.in_view || m.y < top || m.y >= top + pitch) {
        return -1;
    }
    for (int i = 0; i < 2; ++i) {
        const char* label = data_.confirm.answers[i].c_str();
        const int cx = data_.layout.column_x + (i == 0 ? -data_.confirm.answer_spread : data_.confirm.answer_spread);
        const int x = cx - half_width(host, label);
        const int w = host.text_width("[") + host.text_width(label) + host.text_width("]");
        if (m.x >= x && m.x < x + w) {
            return i;
        }
    }
    return -1;
}

void TitleMenu::confirm_frame(TitleHost& host) {
    ConfirmInput in = host.confirm_input();
    const int hover = confirm_hover(host, host.mouse());
    if (hover == 0 || hover == 1) {
        if (in.mouse_moved || in.mouse_left) {
            confirm_yes_ = hover == 0;
        }
        if (in.mouse_left) {
            in.confirm = true;
        }
    }
    if (in.mouse_right) {
        in.cancel = true;
    }

    if (in.cancel) {
        confirm_open_ = false;
        host.play_sound(data_.sounds.cancel);
    } else {
        if (in.left || in.right) {
            confirm_yes_ = !confirm_yes_;
        }
        if (in.confirm) {
            confirm_open_ = false;
            if (confirm_yes_) {
                host.quit();
                quit_ = true;
                return;
            }
            host.play_sound(data_.sounds.cancel);
        }
    }
    host.consume_pad();
}

void TitleMenu::mouse_rows(TitleHost& host) {
    const TitleMouse m = host.mouse();
    if (!m.in_view) {
        return;
    }
    int order[kMaxEntries];
    const int n = visible_rows(order);
    const int pitch = this->pitch();
    for (int k = 0; k < n; ++k) {
        const int top = row_y(k) - lead();
        if (m.y < top || m.y >= top + pitch) {
            continue;
        }
        const int row = order[k];
        if (m.left) {
            selected_ = row;
            host.press_enter();
        } else if (m.moved && selected_ != row) {
            selected_ = row;
            host.play_sound(data_.sounds.move);
        }
        break;
    }
}

void TitleMenu::pick(TitleHost& host) {
    const TitleEntry& e = data_.entries[selected_];
    host.stay_in_title();
    if (host.state_step() != 1) {
        host.set_state_step(1);
        host.reset_file_queue();
    }
    host.fade_out();
    state_ = State::LoadGame;
    host.play_sound(e.sound);

    switch (e.kind) {
    case TitleEntryKind::Continue:
        host.continue_game();
        break;
    case TitleEntryKind::Load:
        host.load_save_screen();
        break;
    case TitleEntryKind::NewGame:
        host.fade_reset();
        state_ = State::Difficulty;
        break;
    case TitleEntryKind::Options:
        host.load_options_screen();
        break;
    case TitleEntryKind::Exit:
        host.quit();
        quit_ = true;
        break;
    }
}

void TitleMenu::difficulty_frame(TitleHost& host, bool demo) {
    if (!difficulty_from_data_) {
        difficulty_from_data_ = true;
        difficulty_ = host.difficulty_default();
    }
    if (demo) {
        demo_tick(host);
    }

    const int rows = host.difficulty_count();
    const TitleMouse m = host.mouse();
    if (m.in_view) {
        const int pitch = this->pitch();
        for (int i = 0; i < rows; ++i) {
            const int top = difficulty_top(host) + i * pitch - lead();
            if (m.y < top || m.y >= top + pitch) {
                continue;
            }
            if (m.left) {
                difficulty_ = i;
                host.press_enter();
            } else if (m.moved && difficulty_ != i) {
                difficulty_ = i;
                host.play_sound(data_.sounds.move);
            }
            break;
        }
    }
    if (m.right) {
        host.press_cancel();
    }

    TitlePad p = host.pad();
    if (p.up || p.down || p.enter || p.cancel) {
        host.stay_in_title();
        if (host.state_step() != 1) {
            host.set_state_step(1);
            host.reset_file_queue();
        }
    }
    if (p.up) {
        difficulty_ = difficulty_ > 0 ? difficulty_ - 1 : rows - 1;
    }
    if (p.down) {
        difficulty_ = difficulty_ < rows - 1 ? difficulty_ + 1 : 0;
    }
    if (p.up || p.down) {
        host.play_sound(data_.sounds.move);
    }

    // Skipping to the game confirms the default after the input above, so a stray hover cannot change it.
    if (skip_step_ == 2) {
        difficulty_ = host.difficulty_default();
        host.press_enter();
        skip_step_ = 3;
    }

    p = host.pad();
    if (p.enter) {
        host.start_new_game(difficulty_);
        host.load_movie_stream();
        host.play_sound(data_.sounds.start_game);
        host.fade_out();
        state_ = State::NewGameStart;
    } else if (p.cancel) {
        host.play_sound(data_.sounds.cancel);
        state_ = State::Main;
    }
}

void TitleMenu::hand_off(TitleHost& host) {
    if (!host.fade_finished()) {
        return;
    }
    host.finish_loading();
    const TitleEntry& e = data_.entries[selected_];
    std::string_view next = e.next;
    if (e.kind == TitleEntryKind::Continue && host.autosave_alive() && !e.next_alt.empty()) {
        next = e.next_alt;
    }
    if (e.kind == TitleEntryKind::NewGame) {
        host.place_new_game_player();
    }
    // Skipping to the game drops the opening movie, once.
    if (skip_step_ == 3) {
        if (e.kind == TitleEntryKind::NewGame && !e.next_alt.empty()) {
            next = e.next_alt;
        }
        skip_step_ = 4;
    }
    host.change_game_state(next);
}

void TitleMenu::idle_timer(TitleHost& host, bool demo) {
    if (demo) {
        return;
    }
    switch (host.state_step()) {
    case 1:
        if (host.idle_frames() > data_.idle_frames) {
            host.load_movie_stream();
            host.set_state_step(2);
        }
        break;
    case 2:
        if (host.file_queue_length() == 0) {
            host.set_demo_count(host.demo_count() + 1);
            host.clear_background();
            host.change_game_state(data_.idle_state);
        }
        break;
    default:
        break;
    }
}

void TitleMenu::draw(TitleHost& host) {
    touch_ = host.touch_screen();
    if (!host.in_title()) {
        host.leave_title_screen();
        return;
    }
    // Skipping to the game passes through the menu for a few frames; drawing it would flash the title.
    if (skip_step_ != 0 && skip_step_ < 4) {
        return;
    }
    host.draw_background();
    if (state_ == State::Start || state_ == State::Main || state_ == State::LoadGame) {
        if (confirm_open_) {
            draw_confirm(host);
        } else {
            draw_rows(host);
        }
    } else {
        draw_difficulty(host);
    }
    host.show_cursor();
}

void TitleMenu::draw_rows(TitleHost& host) const {
    int order[kMaxEntries];
    const int n = visible_rows(order);
    for (int k = 0; k < n; ++k) {
        const TitleEntry& e = data_.entries[order[k]];
        const char* label = e.label.c_str();
        // Hand-measured centres hold for the untranslated retail labels only.
        const int off = (!host.text_translated(label) && e.offset_x >= 0) ? e.offset_x : half_width(host, label);
        host.text_position(data_.layout.column_x - off, row_y(k));
        host.text_white();
        const bool sel = order[k] == selected_;
        host.text(sel ? "[" : "_");
        host.text(label);
        if (sel) {
            host.text("]");
        }
        host.text("\n");
    }
}

void TitleMenu::draw_confirm(TitleHost& host) const {
    const int y = confirm_y();
    const char* question = data_.confirm.question.c_str();
    host.text_white();
    host.text_position(data_.layout.column_x - (host.text_width(question) >> 1), y);
    host.text(question);
    for (int i = 0; i < 2; ++i) {
        const bool sel = i == 0 ? confirm_yes_ : !confirm_yes_;
        const char* label = data_.confirm.answers[i].c_str();
        const int cx = data_.layout.column_x + (i == 0 ? -data_.confirm.answer_spread : data_.confirm.answer_spread);
        host.text_position(cx - half_width(host, label), y + pitch());
        host.text(sel ? "[" : "_");
        host.text(label);
        if (sel) {
            host.text("]");
        }
    }
}

void TitleMenu::draw_difficulty(TitleHost& host) const {
    const int rows = host.difficulty_count();
    for (int i = 0; i < rows; ++i) {
        const char* label = host.difficulty_label(i);
        int off = !host.text_translated(label) ? data_.layout.difficulty_offset_x : half_width(host, label);
        if (data_.layout.difficulty_align == "center") {
            // Centre the label itself; the "[" / "_" marker hangs to its left.
            off = (host.text_width(label) >> 1) + host.text_width("[");
        }
        host.text_position(data_.layout.column_x - off, difficulty_top(host) + i * pitch());
        host.text_white();
        const bool sel = i == difficulty_;
        host.text(sel ? "[" : "_");
        host.text(label);
        if (sel) {
            host.text("]");
        }
        host.text("\n");
    }
}

}  // namespace sh::gameflow
