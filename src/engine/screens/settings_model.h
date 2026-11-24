// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's settings as the Options menu steps and shows them (gamedata/menus/options.lua, natives
// setting.<config key>): what values a setting takes, how a step changes it, the text of its value, the window
// sizes and shapes on offer, and the reset to defaults. All of it is data, gamedata/menu/settings.json; settings
// are named by their config.cfg key and the host stores, applies and saves them.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/screens/options_menu.h"
#include "formats/common/status.h"

namespace sh::screens {

// Which translation table a value label goes through.
enum class LabelTable : std::uint8_t { None, Quick, QuickMenu };

struct SettingLabel {
    std::string text;
    LabelTable table = LabelTable::None;
};

// A setting with a listed set of values or a range.
struct SettingRow {
    // choice: one of `values`; slider: a float in min..max by step; resolution: the
    // window size from the list; map: the new game's start map.
    enum class Kind : std::uint8_t { Choice, Slider, Resolution, Map };
    Kind kind = Kind::Choice;
    std::string label;    // the name the console echo of a change starts with
    std::string setting;  // the config key ("resolution" for the window size)
    std::vector<std::int32_t> values;
    std::vector<std::string> labels;  // parallel to values; empty: the number is shown
    std::vector<std::int32_t> mobile_values;  // choice: the values a phone or tablet offers (empty: all of them)
    float min = 0.0f;
    float max = 0.0f;
    float step = 0.0f;
    bool realtime = true;        // false: takes effect on the next start
    bool keep_on_reset = false;  // the reset to defaults leaves this setting alone
};

// A setting described by its kind of value rather than a list of them.
struct QuickSetting {
    // toggle: zero/non-zero. cycle: an index 0..n-1 over `labels` (optionally stored as
    // `stored` values). float: min..max by step. offset: an integer stepped by `step`.
    // volume: 0..max by step, shown in steps.
    enum class Kind : std::uint8_t { Toggle, Cycle, Float, Offset, Volume };
    // How a float or integer value is written.
    enum class Format : std::uint8_t {
        Fixed0, Fixed2, Fixed3, Percent, SignedPercent, Signed, Rows, OffOrFixed2, Steps, SizeSquare, Labels
    };
    std::string name;
    Kind kind = Kind::Toggle;
    std::string setting;
    std::vector<SettingLabel> labels;
    std::optional<SettingLabel> fallback;  // cycle: the label of a value past the list
    std::vector<std::int32_t> stored;      // cycle: the stored value of each index
    std::vector<std::int32_t> thresholds;  // cycle with stored values: index from value <= threshold
    std::int32_t default_index = 0;        // cycle with stored values and no thresholds
    bool mask = false;                     // cycle: index = value & (n - 1)
    bool wrap = true;                      // cycle: false = an error sound at the ends
    float min = 0.0f;
    float max = 0.0f;
    float step = 0.0f;
    std::int32_t decimals = 2;             // float: saved with 2 or 3 decimals
    bool round = false;                    // float: kept to thousandths, the change is logged
    bool error_if_unchanged = false;       // float: an error sound at the ends
    bool invert = false;                   // offset: shown and stepped upside down
    Format format = Format::Labels;
    std::optional<std::pair<float, float>> default_marker;  // shown as "(default)" inside
    std::optional<SettingLabel> suffix;    // appended (two spaces) while `suffix_setting` is 0 / not 0
    std::string suffix_setting;
    bool suffix_when_zero = true;
};

struct SettingsData {
    std::string reset_done;      // echoed after the reset to defaults
    std::string restart_notice;  // echoed after a change that needs a restart
    std::string default_map;     // shown while no start map is set
    std::vector<std::pair<std::int32_t, std::int32_t>> resolutions;
    // The shapes the Aspect Ratio row offers, as [width, height] of the ratio; a resolution belongs to the nearest.
    std::vector<std::pair<std::int32_t, std::int32_t>> aspects;
    std::vector<SettingRow> rows;
    std::vector<QuickSetting> quick;
};

class SettingsHost {
public:
    virtual ~SettingsHost() = default;

    // Settings by config key. The int setter stores, applies and saves; the float setter
    // stores and applies, and saves `text` when it is given.
    virtual std::int32_t int_setting(const std::string& key) = 0;
    virtual void set_int_setting(const std::string& key, std::int32_t value) = 0;
    virtual float float_setting(const std::string& key) = 0;
    virtual void set_float_setting(const std::string& key, float value, const std::string* text) = 0;
    virtual std::int32_t default_int(const std::string& key) = 0;
    virtual float default_float(const std::string& key) = 0;
    virtual void window_size(std::int32_t& width, std::int32_t& height) = 0;
    virtual void set_window_size(std::int32_t width, std::int32_t height) = 0;
    // The display's own mode (0, 0 when unknown): offered beside the listed resolutions.
    virtual void display_size(std::int32_t& width, std::int32_t& height) { width = height = 0; }
    virtual std::string start_map() = 0;
    virtual std::int32_t map_count() = 0;
    virtual std::int32_t map_index(const std::string& name) = 0;
    virtual std::string map_name(std::int32_t index) = 0;
    virtual std::string map_description(std::int32_t index) = 0;
    virtual void set_start_map(const std::string& name) = 0;

    virtual std::string translate(LabelTable table, const std::string& text) = 0;
    virtual std::string translate_rows(const std::string& count) = 0;  // "{n} rows"
    virtual void echo(const std::string& text) = 0;
    virtual void log_view_change(const std::string& key, const std::string& text) = 0;

    // A menu cue; play_sound is the one that is not cut by the next cue.
    virtual void play_sfx(MenuSfx sfx) = 0;
    virtual void play_sound(MenuSfx sfx) = 0;
};

class SettingsModel {
public:
    explicit SettingsModel(const SettingsData& data) : data_(data) {}

    // The listed settings by config key.
    const SettingRow* find(std::string_view key) const;
    std::string value_label(const SettingRow& row, SettingsHost& host) const;
    void adjust(const SettingRow& row, std::int32_t dir, SettingsHost& host) const;
    // Every setting back to its default, except the window size, the rows marked keep_on_reset, the start map,
    // and the floats, offsets and volumes of `quick`.
    void reset_all(SettingsHost& host) const;

    // The Aspect Ratio row: the shape of the current window size ("16:9"; "" when it has none of the listed shapes).
    // Stepping it moves to the next shape that has resolutions and picks the one nearest in height.
    std::string aspect_label(SettingsHost& host) const;
    std::vector<std::string> aspect_labels() const;
    void adjust_aspect(std::int32_t dir, SettingsHost& host) const;
    // Index into the data's aspects of the shape `width` x `height` has, -1 for none.
    std::int32_t aspect_of(std::int32_t width, std::int32_t height) const;
    // The resolutions of one shape (-1: every one), ascending, the display's own included.
    std::vector<std::pair<std::int32_t, std::int32_t>> resolutions_of(std::int32_t aspect, SettingsHost& host) const;

    // The settings of `quick` by name (their config key).
    const QuickSetting* quick(std::string_view name) const;
    std::string quick_label(const QuickSetting& s, SettingsHost& host) const;
    void quick_adjust(const QuickSetting& s, std::int32_t dir, SettingsHost& host) const;

private:
    const SettingsData& data_;
};

Result<SettingsData> parse_settings(std::string_view json_text);
// A phone's or tablet's rows: each choice with mobile_values keeps only those values (and their labels).
void keep_mobile_values(SettingsData& data);
// menu/settings.json: the gamedata directory's with every active mod's patch, in load order.
Result<SettingsData> load_settings(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                   std::string* source = nullptr);

}  // namespace sh::screens
