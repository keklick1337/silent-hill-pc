// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/inventory/item_screen_text.h"

#include <string>
#include <utility>

#include "engine/app/pc_paths.h"
#include "engine/inventory/item_description.h"
#include "engine/log/sh_log.h"
#include "engine/text/game_text.h"
#include "i18n/localization.h"
#include "i18n/text_bridge.h"
#include "engine/mods/data_reload.h"

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::inventory {

namespace {

// Items are numbered from 32 in the text tables.
constexpr int kFirstItemNumber = 32;
// Longest map notice the drawer types out.
constexpr std::int32_t kNoticeLetters = 99;

const char* item_text(int item, bool description)
{
    const int index = static_cast<int>(item) - kFirstItemNumber;
    const char* text = description ? sh::i18n::item_description(index) : sh::i18n::item_name(index);
    return text != nullptr ? text : "";
}

}  // namespace

const screens::ItemScreenStyle& item_screen_style()
{
    static screens::ItemScreenStyle s_style;
    static std::uint32_t s_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(s_epoch))
    {
        s_style = screens::ItemScreenStyle{};
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        Result<screens::ItemScreenStyle> style = screens::load_item_screen_style(PcPort_GetGameDataPath(), mods, &source);
        if (style)
        {
            s_style = std::move(style.value());
            SH_LOG("[ITEMSCREENS] %s", source.c_str());
        }
        else
        {
            SH_ERR("[ITEMSCREENS] %s - built-in item screen layout", style.error().c_str());
        }
    }
    return s_style;
}

const char* item_name_text(int item) { return item_text(item, false); }
const char* item_description_text(int item) { return item_text(item, true); }

std::int32_t label_growth(const char* english)
{
    const std::int32_t grow = sh::i18n::text_width(sh::i18n::menu_text(english)) - sh::i18n::text_width(english);
    return grow > 0 ? grow : 0;
}

std::int32_t item_name_x() { return name_x(label_growth("Name:")); }

std::int32_t place_name_row_tag(std::int32_t x, const char* tag, int item)
{
    const char* english = Pc_TextItemIn(static_cast<int>(item) - kFirstItemNumber, 0, PC_TEXT_BASE);
    const TagPlace place =
        place_name_tag(x, label_growth(tag), label_growth("Name:"), sh::i18n::text_width(english != nullptr ? english : ""),
                       sh::i18n::text_width(sh::i18n::menu_text(item_name_text(item))));
    text::game_text().set_position(place.x, place.y);
    return place.y;
}

void draw_map_notice(bool no_map)
{
    const screens::ItemScreenStyle& style = item_screen_style();
    const std::string& notice = no_map ? style.notice_no_map : style.notice_too_dark;
    text::game_text().set_position(style.notice_x, style.notice_y);
    text::game_text().set_color(style.notice_color);
    text::game_text().draw(notice.c_str(), kNoticeLetters);
}

}  // namespace sh::inventory
