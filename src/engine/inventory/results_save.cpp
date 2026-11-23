// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The results screen's "save?" dialog as drawing records.

#include "engine/inventory/inventory_draw.h"

namespace sh::inventory {

namespace {

struct Segment {
    std::int32_t x0, y0, x1, y1;
};

// The dialog frame (12 strokes) and the frame around the chosen answer (12 strokes), each
// relative to its own corner.
constexpr std::array<Segment, 24> kSegments = {{
    {0, 0, 0, 64},     {0, 0, 100, 0},    {208, 144, 208, 80}, {208, 144, 108, 144},
    {4, 8, 204, 8},    {4, 136, 204, 136}, {4, 8, 4, 136},      {204, 8, 204, 136},
    {0, 1, 100, 1},    {208, 143, 108, 143}, {4, 7, 204, 7},    {4, 137, 204, 137},
    {0, 0, 0, 20},     {0, 0, 22, 0},     {48, 48, 48, 28},    {48, 48, 26, 48},
    {2, 4, 46, 4},     {2, 44, 46, 44},   {2, 4, 2, 44},       {46, 4, 46, 44},
    {0, 1, 22, 1},     {48, 47, 26, 47},  {2, 3, 46, 3},       {2, 45, 46, 45},
}};

constexpr Point kDialogOrigin{-104, -72};
constexpr Point kAnswerOrigin{-76, 6};
constexpr std::int32_t kAnswerPitch = 102;

}  // namespace

ResultsSave layout_results_save(std::int32_t answer, std::int32_t next_difficulty)
{
    ResultsSave out;
    for (int i = 0; i < 24; i++) {
        // Shades per frame: four strokes at 96, four at 32, then two at 224 and two at 160 (the
        // game's 8-bit arithmetic wraps).
        const int k = i % 12;
        const int shade = k < 8 ? k >> 2 : (k - 4) / 2;
        const auto rg = static_cast<std::uint8_t>(static_cast<std::int8_t>(96 - (shade << 6)));
        const Segment& seg = kSegments[static_cast<std::size_t>(i)];
        const Point origin = i < 12 ? kDialogOrigin : Point{kAnswerOrigin.x + answer * kAnswerPitch, kAnswerOrigin.y};
        out.lines[static_cast<std::size_t>(i)] = {Point{static_cast<std::int16_t>(seg.x0 + origin.x), static_cast<std::int16_t>(seg.y0 + origin.y)},
                                                  Point{static_cast<std::int16_t>(seg.x1 + origin.x), static_cast<std::int16_t>(seg.y1 + origin.y)},
                                                  Rgb{rg, rg, 255}};
    }
    out.labels[out.label_count++] = {ResultsLabel::SaveQuestion, {90, 92}, 99, true};
    out.labels[out.label_count++] = {ResultsLabel::YesNo, {94, 122}, 99, true};
    out.labels[out.label_count++] = {ResultsLabel::NextGameMode, {82, 200}, 15, false};
    if (next_difficulty == -1)
        out.labels[out.label_count++] = {ResultsLabel::Normal, {123, 240}, 10, false};
    else if (next_difficulty == 0 || next_difficulty == 1)
        out.labels[out.label_count++] = {ResultsLabel::Hard, {136, 240}, 10, false};
    return out;
}

}  // namespace sh::inventory
