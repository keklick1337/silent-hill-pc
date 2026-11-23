#include "framework/sh_test.h"

#include "engine/input/controls_ref.h"

#include <set>
#include <string>

using namespace sh::input;
using namespace sh::input::ref;

namespace {

int mono(std::string_view s) { return static_cast<int>(s.size()) * 4; }

std::vector<Row> all_action_rows() {
    std::vector<Row> rows;
    for (int i = 0; i < kActionCount; ++i) {
        Row r;
        r.category = category_of(static_cast<Action>(i));
        r.label = std::string(info(static_cast<Action>(i)).label);
        r.keyboard = "W/Up";
        r.pad = "-";
        rows.push_back(r);
    }
    return rows;
}

}  // namespace

SH_TEST(controls_ref_every_action_has_a_category) {
    std::set<std::string_view> keys;
    for (int i = 0; i < kActionCount; ++i) {
        const Category c = category_of(static_cast<Action>(i));
        SH_CHECK(static_cast<int>(c) >= 0 && static_cast<int>(c) < kCategoryCount);
        keys.insert(category_key(c));
    }
    // No category is empty, so no heading is dead data.
    SH_CHECK(static_cast<int>(keys.size()) == kCategoryCount);
}

SH_TEST(controls_ref_fit_ellipsis) {
    SH_CHECK(fit_ellipsis("LSHIFT", 40, mono) == "LSHIFT");
    SH_CHECK(fit_ellipsis("LEFT SHIFT", 28, mono) == "LEFT...");
    SH_CHECK(fit_ellipsis("LEFT SHIFT", 8, mono).empty());
    SH_CHECK(fit_ellipsis("LEFT SHIFT", 12, mono) == "...");
    // UTF-8 sequences are cut whole.
    SH_CHECK(fit_ellipsis("\xD0\x91\xD0\xB5\xD0\xB3\xD1\x83\xD1\x82", 18, [](std::string_view s) {
                 int n = 0;
                 for (unsigned char ch : s) n += ((ch & 0xC0) != 0x80) ? 4 : 0;
                 return n;
             }) == "\xD0\x91...");
}

SH_TEST(controls_ref_join_slots) {
    SH_CHECK(join_slots("W", "Up") == "W/Up");
    SH_CHECK(join_slots("W", "NONE") == "W");
    SH_CHECK(join_slots("", "Up") == "Up");
    SH_CHECK(join_slots("none", "") == "-");
}

SH_TEST(controls_ref_layout_fits_the_screen) {
    const auto rows = all_action_rows();
    const Params p;
    const Layout l = compute_layout(rows, p);
    SH_CHECK(!l.truncated);
    SH_CHECK(l.columns == 2);

    int placed_rows = 0;
    int headings = 0;
    std::set<int> row_ids;
    for (const Placed& it : l.items) {
        SH_CHECK(it.y >= p.top && it.y + p.row_h <= p.bottom);
        SH_CHECK(it.x >= p.margin && it.x + it.col_w <= p.width - p.margin);
        SH_CHECK(it.label_w + it.keyboard_w + it.pad_w == it.col_w);
        if (it.kind == Placed::Kind::Row) {
            ++placed_rows;
            row_ids.insert(it.row);
        } else {
            ++headings;
        }
    }
    SH_CHECK(placed_rows == kActionCount);
    SH_CHECK(static_cast<int>(row_ids.size()) == kActionCount);
    SH_CHECK(headings == kCategoryCount);
}

SH_TEST(controls_ref_layout_headings_lead_their_rows) {
    const Layout l = compute_layout(all_action_rows(), Params{});
    // A heading is never the last line of a column.
    for (std::size_t i = 0; i < l.items.size(); ++i) {
        if (l.items[i].kind == Placed::Kind::Heading) {
            SH_CHECK(i + 1 < l.items.size());
            SH_CHECK(l.items[i + 1].column == l.items[i].column);
            SH_CHECK(l.items[i + 1].kind == Placed::Kind::Row);
        }
    }
}

SH_TEST(controls_ref_few_rows_use_one_column) {
    std::vector<Row> rows = all_action_rows();
    rows.resize(6);
    const Layout l = compute_layout(rows, Params{});
    SH_CHECK(l.columns == 1);
    SH_CHECK(!l.truncated);
}

SH_TEST(controls_ref_overflow_is_truncated) {
    Params p;
    p.bottom = p.top + 3 * p.row_h;  // 3 lines per column, 2 columns
    const Layout l = compute_layout(all_action_rows(), p);
    SH_CHECK(l.truncated);
    for (const Placed& it : l.items) {
        SH_CHECK(it.y + p.row_h <= p.bottom);
    }
}

SH_TEST(controls_ref_wide_space_gets_more_columns_only_when_needed) {
    Params p;
    p.width = 640;
    const Layout l = compute_layout(all_action_rows(), p);
    SH_CHECK(l.columns == 2);  // the fewest that fit, even if more would
}
