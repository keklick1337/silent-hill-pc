// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/input/controls_ref.h"

#include <algorithm>

namespace sh::input::ref {

namespace {

struct CategoryInfo {
    Category category;
    std::string_view key;
};

constexpr CategoryInfo kCategories[] = {
    {Category::Movement, "ui.controls_ref.category.movement"},
    {Category::Combat, "ui.controls_ref.category.combat"},
    {Category::Camera, "ui.controls_ref.category.camera"},
    {Category::Menu, "ui.controls_ref.category.menu"},
    {Category::System, "ui.controls_ref.category.system"},
    {Category::Debug, "ui.controls_ref.category.debug"},
};
static_assert(sizeof(kCategories) / sizeof(kCategories[0]) == static_cast<std::size_t>(kCategoryCount));

}  // namespace

Category category_of(Action action) {
    switch (action) {
        case Action::MoveForward:
        case Action::MoveBack:
        case Action::TurnLeft:
        case Action::TurnRight:
        case Action::StepLeft:
        case Action::StepRight:
        case Action::Run:
        case Action::QuickTurn:
            return Category::Movement;
        case Action::Interact:
        case Action::Attack:
        case Action::Aim:
        case Action::Reload:
        case Action::Flashlight:
        case Action::QuickHeal:
        case Action::CycleWeapons:
            return Category::Combat;
        case Action::View:
        case Action::RearLook:
        case Action::ChangeCamera:
        case Action::SwapShoulder:
            return Category::Camera;
        case Action::Back:
        case Action::Confirm:
        case Action::Inventory:
        case Action::Map:
        case Action::Pause:
            return Category::Menu;
        case Action::QuickSave:
        case Action::QuickLoad:
            return Category::System;
        case Action::DebugMenu:
            return Category::Debug;
        case Action::Count:
            break;
    }
    return Category::Menu;
}

std::string_view category_key(Category category) {
    return kCategories[static_cast<int>(category)].key;
}

std::string fit_ellipsis(std::string_view text, int max_w, const Measure& measure) {
    if (measure(text) <= max_w) {
        return std::string(text);
    }
    constexpr std::string_view kDots = "...";
    // Drop whole UTF-8 sequences from the end until the text plus the dots fits.
    std::size_t len = text.size();
    while (len > 0) {
        --len;
        while (len > 0 && (static_cast<unsigned char>(text[len]) & 0xC0) == 0x80) {
            --len;
        }
        std::string candidate(text.substr(0, len));
        candidate += kDots;
        if (measure(candidate) <= max_w) {
            return candidate;
        }
    }
    return measure(kDots) <= max_w ? std::string(kDots) : std::string();
}

std::string join_slots(std::string_view first, std::string_view second, std::string_view unbound) {
    const bool a = !is_unbound(first);
    const bool b = !is_unbound(second);
    if (a && b) {
        return std::string(first) + "/" + std::string(second);
    }
    if (a) {
        return std::string(first);
    }
    if (b) {
        return std::string(second);
    }
    return std::string(unbound);
}

namespace {

struct Line {
    bool heading;
    Category category;
    int row;
};

// Fills `ncol` columns of `target` lines top to bottom. A heading never ends a column.
// Returns the number of columns used and fills `column_of` per line (-1: dropped).
int flow(const std::vector<Line>& lines, int ncol, int target, std::vector<int>& column_of,
         std::vector<int>& line_in_column) {
    column_of.assign(lines.size(), -1);
    line_in_column.assign(lines.size(), 0);
    int col = 0;
    int used = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const bool heading_needs_two = lines[i].heading && i + 1 < lines.size();
        const int need = heading_needs_two ? 2 : 1;
        if (used > 0 && used + need > target) {
            ++col;
            used = 0;
        }
        if (col >= ncol) {
            break;
        }
        column_of[i] = col;
        line_in_column[i] = used++;
    }
    return col + 1;
}

}  // namespace

Layout compute_layout(const std::vector<Row>& rows, const Params& params) {
    Layout out;

    std::vector<Line> lines;
    for (int c = 0; c < kCategoryCount; ++c) {
        const Category cat = static_cast<Category>(c);
        bool opened = false;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].category != cat) {
                continue;
            }
            if (!opened) {
                lines.push_back({true, cat, -1});
                opened = true;
            }
            lines.push_back({false, cat, static_cast<int>(i)});
        }
    }

    const int avail = params.width - 2 * params.margin;
    const int max_cols = std::max(1, (avail + params.col_gap) / (params.min_col_w + params.col_gap));
    const int capacity = std::max(1, (params.bottom - params.top) / std::max(1, params.row_h));
    const int total = static_cast<int>(lines.size());

    std::vector<int> column_of, line_in_column;
    int ncol = 1;
    int target = capacity;
    bool fitted = false;
    for (ncol = 1; ncol <= max_cols && !fitted; ++ncol) {
        // Balanced first (short columns look better), then as full as the screen allows.
        for (target = (total + ncol - 1) / ncol; target <= capacity; ++target) {
            if (flow(lines, ncol, target, column_of, line_in_column) <= ncol) {
                fitted = true;
                break;
            }
        }
    }
    if (fitted) {
        --ncol;  // the loop incremented once more after the successful pass
    } else {
        ncol = max_cols;
        target = capacity;
        flow(lines, ncol, target, column_of, line_in_column);
        out.truncated = true;
    }

    out.columns = ncol;
    out.col_w = (avail - params.col_gap * (ncol - 1)) / ncol;
    const int label_w = out.col_w * params.label_pct / 100;
    const int kb_w = out.col_w * params.keyboard_pct / 100;
    const int pad_w = out.col_w - label_w - kb_w;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (column_of[i] < 0) {
            out.truncated = true;
            continue;
        }
        Placed p;
        p.kind = lines[i].heading ? Placed::Kind::Heading : Placed::Kind::Row;
        p.column = column_of[i];
        p.x = params.margin + p.column * (out.col_w + params.col_gap);
        p.y = params.top + line_in_column[i] * params.row_h;
        p.col_w = out.col_w;
        p.category = lines[i].category;
        p.row = lines[i].row;
        p.label_x = p.x;
        p.label_w = label_w;
        p.keyboard_x = p.x + label_w;
        p.keyboard_w = kb_w;
        p.pad_x = p.x + label_w + kb_w;
        p.pad_w = pad_w;
        out.items.push_back(p);
    }
    return out;
}

}  // namespace sh::input::ref
