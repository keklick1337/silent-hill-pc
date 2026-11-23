// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/console/console.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace sh::console {

namespace {

constexpr int kPageRows = 10;  // the model does not know the panel height; the view clamps anyway

bool is_blank(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

}  // namespace

Console::Console(Registry& registry, std::size_t max_lines)
    : registry_(registry), max_lines_(max_lines ? max_lines : 1) {}

void Console::print(Level level, std::string_view text) {
    std::size_t pos = 0;
    for (;;) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view piece = text.substr(pos, nl == std::string_view::npos ? nl : nl - pos);
        Line line;
        line.id = next_id_++;
        line.level = level;
        for (char c : piece) {
            if (c != '\r') line.text.push_back(c);
        }
        lines_.push_back(std::move(line));
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    while (lines_.size() > max_lines_) lines_.pop_front();
    bump();
}

void Console::clear() {
    lines_.clear();
    scroll_ = 0;
    select_clear();
    bump();
}

void Console::set_history_file(std::filesystem::path file) {
    history_file_ = std::move(file);
    (void)history_.load(history_file_);
}

void Console::refresh() {
    completion_ = complete(registry_, editor_.text(), editor_.caret());
}

void Console::close_popup() {
    popup_open_ = false;
    selected_ = -1;
    tab_cycle_ = false;
    bump();
}

void Console::edited() {
    history_.reset_browsing();
    selected_ = -1;
    tab_cycle_ = false;
    refresh();
    popup_open_ = !editor_.empty() && !completion_.items.empty();
    bump();
}

void Console::accept_candidate(std::size_t index) {
    if (index >= completion_.items.size()) return;
    const std::size_t old_word = completion_.word_index;
    const Applied applied = apply(registry_, editor_.text(), completion_, completion_.items[index]);
    editor_.set(applied.line, applied.caret);
    history_.reset_browsing();
    selected_ = -1;
    tab_cycle_ = false;
    refresh();
    // Accepting the last word of a command leaves the same word in place: nothing more to offer.
    popup_open_ = completion_.word_index != old_word && !completion_.items.empty();
    bump();
}

bool Console::submit_line(std::string_view line_in) {
    const std::string line(line_in);
    if (is_blank(line)) return true;
    print(Level::Command, "> " + line);
    history_.add(line);
    const bool ok = run_line ? run_line(line) : registry_.execute(line, *this);
    if (!history_file_.empty()) (void)history_.save(history_file_);
    if (on_executed) on_executed(line, ok);
    editor_.clear();
    popup_open_ = false;
    selected_ = -1;
    tab_cycle_ = false;
    scroll_ = 0;
    refresh();
    bump();
    return ok;
}

void Console::on_text(std::string_view utf8) {
    if (editor_.insert(utf8) == 0) return;
    edited();
}

void Console::on_wheel(int lines) {
    set_scroll(scroll_ + lines);
    bump();
}

void Console::on_ctrl_char(char letter) {
    switch (std::tolower(static_cast<unsigned char>(letter))) {
        case 'a': editor_.home(); refresh(); close_popup(); break;
        case 'e': editor_.end(); refresh(); close_popup(); break;
        case 'u': editor_.kill_to_start(); edited(); break;
        case 'k': editor_.kill_to_end(); edited(); break;
        case 'w': editor_.kill_word_back(); edited(); break;
        case 'c':
            (void)copy_selection();
            break;
        case 'v':
            if (clipboard_get) {
                const std::string clip = clipboard_get();
                // insert() stops at a newline, so only the first line is pasted.
                if (editor_.insert(clip) > 0) edited();
            }
            break;
        case 'l': clear(); break;
        default: break;
    }
}

void Console::on_key(Key key, Mods mods) {
    const int n = static_cast<int>(completion_.items.size());
    switch (key) {
        case Key::Enter:
            if (popup_open() && selected_ >= 0) {
                accept_candidate(static_cast<std::size_t>(selected_));
            } else {
                submit_line(editor_.text());
            }
            break;
        case Key::Escape:
            if (popup_open()) {
                close_popup();
            } else {
                editor_.clear();
                history_.reset_browsing();
                refresh();
                close_popup();
            }
            break;
        case Key::Tab: {
            if (!popup_open()) {
                refresh();
                if (completion_.items.empty()) break;
                popup_open_ = true;
                selected_ = -1;
            }
            const int count = static_cast<int>(completion_.items.size());
            if (mods.shift) {
                selected_ = selected_ <= 0 ? count - 1 : selected_ - 1;
                tab_cycle_ = true;
            } else if (selected_ >= 0 && !tab_cycle_) {
                accept_candidate(static_cast<std::size_t>(selected_));
            } else if (selected_ >= 0) {
                selected_ = (selected_ + 1) % count;
            } else if (count == 1) {
                accept_candidate(0);
            } else {
                const std::string cp = common_prefix(completion_.items);
                if (cp.size() > completion_.prefix.size()) {
                    std::string text = editor_.text();
                    text.replace(completion_.replace_begin, completion_.replace_end - completion_.replace_begin, cp);
                    editor_.set(text, completion_.replace_begin + cp.size());
                    refresh();
                    selected_ = -1;
                    popup_open_ = !completion_.items.empty();
                } else {
                    selected_ = 0;
                    tab_cycle_ = true;
                }
            }
            bump();
            break;
        }
        case Key::Up:
            if (popup_open()) {
                selected_ = selected_ < 0 ? n - 1 : std::max(0, selected_ - 1);
                tab_cycle_ = false;
            } else if (const std::string* h = history_.prev(editor_.text())) {
                editor_.set(*h);
                selected_ = -1;
                refresh();
            }
            bump();
            break;
        case Key::Down:
            if (popup_open()) {
                selected_ = selected_ < 0 ? 0 : std::min(n - 1, selected_ + 1);
                tab_cycle_ = false;
            } else if (const std::string* h = history_.next()) {
                editor_.set(*h);
                selected_ = -1;
                refresh();
            }
            bump();
            break;
        case Key::Left:
            mods.ctrl ? editor_.word_left() : editor_.left();
            refresh();
            close_popup();
            break;
        case Key::Right:
            mods.ctrl ? editor_.word_right() : editor_.right();
            refresh();
            close_popup();
            break;
        case Key::Home:
            editor_.home();
            refresh();
            close_popup();
            break;
        case Key::End:
            if (editor_.empty()) {
                scroll_ = 0;
            } else {
                editor_.end();
                refresh();
            }
            close_popup();
            break;
        case Key::PageUp:
            set_scroll(scroll_ + kPageRows);
            bump();
            break;
        case Key::PageDown:
            set_scroll(scroll_ - kPageRows);
            bump();
            break;
        case Key::Backspace:
            editor_.backspace();
            edited();
            break;
        case Key::Delete:
            editor_.del();
            edited();
            break;
    }
}

void Console::select_begin(TextPos pos) {
    sel_valid_ = true;
    sel_a_ = sel_b_ = pos;
    bump();
}

void Console::select_update(TextPos pos) {
    if (!sel_valid_) return;
    sel_b_ = pos;
    bump();
}

void Console::select_clear() {
    if (sel_valid_) bump();
    sel_valid_ = false;
    sel_a_ = sel_b_ = {};
}

namespace {

enum class CharClass { Space, Word, Other };

CharClass char_class(unsigned char c) {
    if (c == ' ' || c == '\t') return CharClass::Space;
    const bool word = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
                      c == '-' || c >= 0x80;
    return word ? CharClass::Word : CharClass::Other;
}

}  // namespace

void Console::select_word(TextPos pos) {
    for (const Line& l : lines_) {
        if (l.id != pos.line) continue;
        if (l.text.empty()) return;
        // A click past the end of the text takes the last character's run.
        const std::size_t at = std::min(pos.offset, l.text.size() - 1);
        const CharClass cls = char_class(static_cast<unsigned char>(l.text[at]));
        std::size_t b = at, e = at + 1;
        while (b > 0 && char_class(static_cast<unsigned char>(l.text[b - 1])) == cls) --b;
        while (e < l.text.size() && char_class(static_cast<unsigned char>(l.text[e])) == cls) ++e;
        sel_valid_ = true;
        sel_a_ = {l.id, b};
        sel_b_ = {l.id, e};
        bump();
        return;
    }
}

void Console::select_line(TextPos pos) {
    for (const Line& l : lines_) {
        if (l.id != pos.line) continue;
        sel_valid_ = true;
        sel_a_ = {l.id, 0};
        sel_b_ = {l.id, l.text.size()};
        bump();
        return;
    }
}

bool Console::copy_selection() {
    if (!has_selection() || !clipboard_set) return false;
    const std::string text = selected_text();
    clipboard_set(text);
    const std::size_t rows = 1 + static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
    info("copied " + std::to_string(text.size()) + " bytes, " + std::to_string(rows) + (rows == 1 ? " line" : " lines"));
    return true;
}

bool Console::has_selection() const {
    return sel_valid_ && !(sel_a_ == sel_b_);
}

std::string Console::selected_text() const {
    if (!has_selection()) return {};
    TextPos lo = sel_a_;
    TextPos hi = sel_b_;
    if (std::pair(hi.line, hi.offset) < std::pair(lo.line, lo.offset)) std::swap(lo, hi);
    std::string out;
    bool first = true;
    for (const Line& l : lines_) {
        if (l.id < lo.line || l.id > hi.line) continue;
        const std::size_t b = l.id == lo.line ? std::min(lo.offset, l.text.size()) : 0;
        const std::size_t e = l.id == hi.line ? std::min(hi.offset, l.text.size()) : l.text.size();
        if (!first) out.push_back('\n');
        first = false;
        if (e > b) out.append(l.text, b, e - b);
    }
    return out;
}

void Console::register_console_commands() {
    CommandSpec clear_cmd;
    clear_cmd.name = "clear";
    clear_cmd.help = "Clear the console scrollback";
    clear_cmd.group = "console";
    clear_cmd.run = [this](Context&, const Args&) {
        clear();
        return Result<void>::ok();
    };
    (void)registry_.add(std::move(clear_cmd));

    CommandSpec hist;
    hist.name = "history";
    hist.help = "List the command history, or 'history clear' to forget it";
    hist.group = "console";
    ArgSpec what;
    what.name = "action";
    what.type = ArgType::Enum;
    what.optional = true;
    what.values = {"clear"};
    what.help = "clear: forget every entry";
    hist.args.push_back(std::move(what));
    hist.run = [this](Context& ctx, const Args& args) {
        if (args.has(0)) {
            history_.clear();
            if (!history_file_.empty()) (void)history_.save(history_file_);
            ctx.out.info("history cleared");
            return Result<void>::ok();
        }
        const auto& entries = history_.entries();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            ctx.out.print(Level::Plain, std::to_string(i + 1) + "  " + entries[i]);
        }
        return Result<void>::ok();
    };
    (void)registry_.add(std::move(hist));
}

}  // namespace sh::console
