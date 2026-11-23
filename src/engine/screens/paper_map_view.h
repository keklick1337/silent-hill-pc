// SPDX-License-Identifier: GPL-3.0-or-later
//
// The paper map screen's view of the running game: the screen's per-frame entry
// point, the draws the map scripts call and the disc files of a page. Implemented
// by src/ui/screens/paper_map_host.cpp over the session and the screens' views (screens_view.h).
#pragma once

#include <cstdint>

namespace sh::screens {

// One frame of the paper map game state.
void run_paper_map_screen();

// The draws of sh.map_screen_vertex_draw / marking_draw / player_mark_draw (view: left, top texel, Q12 scale).
void draw_paper_map_page(int left, int top, int scale);
bool draw_paper_map_marks(int mode, int page, int pick, int shade, int left, int top, int scale);
std::int32_t draw_paper_map_player(int page, int left, int top, int scale);

// Disc files of a page (menu/map_screen.json page_files); -1 when it has none.
int paper_map_image_file(int page);
int paper_map_marking_file(int page);

}  // namespace sh::screens
