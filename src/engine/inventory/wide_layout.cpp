// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/wide_layout.h"

#include <algorithm>
#include <cmath>

namespace sh::inventory {

namespace {

constexpr std::int32_t kHalfFrame = 160;  // half the 4:3 frame
constexpr std::int32_t kSideColumnEdge = 146;  // outer edge of a side column's brackets

}  // namespace

std::int32_t WideLayout::x(Column column, std::int32_t x) const {
    switch (column) {
        case Column::Left: return x - shift;
        case Column::Right: return x + shift;
        case Column::CentreRight: return x + shift / 2;
        case Column::Centre: break;
    }
    return x;
}

WideLayout wide_layout(const WideRules& rules, float wide_factor, bool touch) {
    WideLayout out;
    if (!rules.enabled || !(wide_factor > 1.0f)) return out;
    out.extra = static_cast<std::int32_t>(std::floor(static_cast<double>(kHalfFrame) * (wide_factor - 1.0)));
    const std::int32_t margin = touch ? rules.edge_margin_touch : rules.edge_margin;
    const std::int32_t room = out.extra + kHalfFrame - kSideColumnEdge - margin;
    std::int32_t shift = static_cast<std::int32_t>(std::lround(static_cast<double>(out.extra) * rules.column_spread));
    shift = std::min({shift, rules.max_column_shift, room});
    out.shift = std::max(shift, 0);
    out.ring = static_cast<float>(1.0 + rules.ring_spread * static_cast<double>(out.shift) / kSideColumnCentre);
    return out;
}

Column column_of(Selection selection) {
    switch (selection) {
        case Selection::Option:
        case Selection::Health: return Column::Left;
        case Selection::Map:
        case Selection::ItemCommands:
        case Selection::EquippedCommands: return Column::Right;
        case Selection::Examine: return Column::CentreRight;
        case Selection::Items:
        case Selection::EquippedItem:
        case Selection::Exit: break;
    }
    return Column::Centre;
}

}  // namespace sh::inventory
