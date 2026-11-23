// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The console model: scrollback, the prompt (LineEditor), history, the completion popup state and text
// selection. It consumes decoded input events (Key / text / wheel) and executes lines through a Registry; it
// draws nothing (console_view.h turns its state into quads) and includes no SDL, so it runs in unit tests.
//
// Key behaviour:
//  - typing recomputes the completion; the popup lists candidates below the prompt while there are any and
//    the user typed something (an empty prompt shows no popup until Tab);
//  - Down/Up move the highlighted candidate while the popup is open (Down from "none" selects the first, Up
//    selects the last); with no popup they browse the history;
//  - Tab accepts the highlighted candidate, or - with none highlighted - extends the word to the common
//    prefix and, when that gains nothing, highlights the first candidate; repeated Tab cycles;
//  - Enter accepts a highlighted candidate (does not run the line) else runs the line;
//  - Esc closes the popup, then clears the line;
//  - PageUp/PageDown/wheel scroll the scrollback; End on an empty prompt returns to the bottom.

#pragma once

#include "engine/console/completion.h"
#include "engine/console/history.h"
#include "engine/console/line_editor.h"
#include "engine/console/registry.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace sh::console {

enum class Key : std::uint8_t {
    Enter, Tab, Up, Down, Left, Right, Home, End, PageUp, PageDown, Backspace, Delete, Escape,
};

struct Mods {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

struct Line {
    std::uint64_t id = 0;   // grows with every printed line; stable while the line exists
    Level level = Level::Plain;
    std::string text;       // one logical line (no newline); the view wraps it
};

// A point in the scrollback text: a line id and a byte offset in its text.
struct TextPos {
    std::uint64_t line = 0;
    std::size_t offset = 0;
    friend bool operator==(const TextPos&, const TextPos&) = default;
};

class Console final : public Output {
public:
    explicit Console(Registry& registry, std::size_t max_lines = 2000);

    Registry& registry() { return registry_; }

    // Output: splits on '\n', drops '\r', keeps the newest max_lines lines.
    void print(Level level, std::string_view line) override;
    void clear();
    const std::deque<Line>& lines() const { return lines_; }
    std::uint64_t revision() const { return revision_; }  // bumps on any visible change (redraw trigger)

    // Prompt and history.
    const LineEditor& editor() const { return editor_; }
    LineEditor& editor_mut() { return editor_; }  // callers must call refresh() afterwards
    History& history() { return history_; }
    // Loads the file now and saves it after every submitted line.
    void set_history_file(std::filesystem::path file);

    // Input.
    void on_key(Key key, Mods mods = {});
    void on_text(std::string_view utf8);
    void on_wheel(int lines);                       // > 0 scrolls back (older), < 0 forward
    void on_ctrl_char(char letter);                 // Ctrl+<letter>: A E U K W C V L and friends
    // Runs `line` as if typed: echoes "> line", executes, records in history. Returns false when it failed.
    bool submit_line(std::string_view line);

    // Completion popup state.
    bool popup_open() const { return popup_open_ && !completion_.items.empty(); }
    int selected() const { return selected_; }      // -1 none
    const Completion& completion() const { return completion_; }
    void refresh();                                  // recompute completion for the prompt (after edits)
    void close_popup();
    // Registers `clear` (empties the scrollback) and `history [clear]` in this console's registry.
    void register_console_commands();

    // Scrollback position in wrapped rows from the bottom (0 = pinned to the newest); the view clamps it.
    int scroll() const { return scroll_; }
    void set_scroll(int rows) { scroll_ = rows < 0 ? 0 : rows; }

    // Selection (the view maps the mouse to TextPos). A selection exists when begin != end.
    void select_begin(TextPos pos);
    void select_update(TextPos pos);
    void select_clear();
    void select_word(TextPos pos);   // the run of word (or non-word, non-space) characters around pos
    void select_line(TextPos pos);   // the whole line
    // Puts the selection on the clipboard and says so; false (and silent) with nothing selected.
    bool copy_selection();
    bool has_selection() const;
    std::string selected_text() const;
    TextPos selection_begin() const { return sel_a_; }
    TextPos selection_end() const { return sel_b_; }

    // Host services.
    std::function<std::string()> clipboard_get;
    std::function<void(const std::string&)> clipboard_set;
    // Runs a submitted line in place of registry.execute(line, *this) (the game host also logs it); the console
    // has already shown "> line" and put it in the history.
    std::function<bool(std::string_view line)> run_line;
    // Called after every executed line (the game host uses it to unfreeze for a moment).
    std::function<void(std::string_view line, bool ok)> on_executed;

private:
    void accept_candidate(std::size_t index);
    void edited();                                   // after the editor changed: refresh + open the popup
    void bump() { ++revision_; }

    Registry& registry_;
    std::size_t max_lines_;
    std::deque<Line> lines_;
    std::uint64_t next_id_ = 1;
    std::uint64_t revision_ = 1;
    LineEditor editor_;
    History history_;
    std::filesystem::path history_file_;
    Completion completion_;
    bool popup_open_ = false;
    int selected_ = -1;
    bool tab_cycle_ = false;                         // the highlight came from Tab: Tab keeps cycling
    int scroll_ = 0;
    bool sel_valid_ = false;
    TextPos sel_a_, sel_b_;
};

}  // namespace sh::console
