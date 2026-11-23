#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

namespace sh::import::ui {

// En is the built-in English; Local is the translation load_translation() read, English where it has no string.
enum class Lang { En, Local };

// Every user-visible string of the first-run screen: English in strings.cpp, translations in
// gamedata/text/<lang>/import_ui.json under the key "ui.import.<snake_case name>".
enum class Str {
    Title,
    IntroNeedImage,    // {0} = gamedata directory
    IntroFormats,
    PathLabel,
    SettingOverwrite,  // {0} = config key
    SettingVerify,     // {0} = config key
    SettingThreads,    // {0} = config key
    PathHintPrefix,    // {0} = suggested path
    ConsolePrompt,
    ActionImport,
    ActionCancel,
    ActionRetry,
    ActionQuit,
    ActionClose,

    ReadyTitle,
    ReadyBody,         // {0} = gamedata directory

    ImportingTitle,
    CancellingTitle,
    CancellingBody,
    StageRead,
    StageRaw,
    StageConvert,
    StageVerify,
    StageCommit,
    StageUnknown,      // {0} = stage name from the importer
    EtaUnknown,
    EtaLeft,           // {0} = duration

    DoneTitle,
    DoneBody,          // {0} = gamedata directory
    FailedTitle,
    FailedBody,        // {0} = reason from the importer
    FailedNoReason,
    CancelledTitle,
    CancelledBody,
    AbortedTitle,
    AbortedBody,

    DurationHours,     // {0} hours, {1} minutes
    DurationMinutes,   // {0} minutes, {1} seconds
    DurationSeconds,   // {0} seconds

    ErrEmpty,
    ErrNotFound,       // {0} = path
    ErrIsDirectory,    // {0} = path
    ErrUnreadable,     // {0} = path
    ErrCueNoFile,      // {0} = path
    ErrCueTargetMissing,  // {0} = file named by the cue
    ErrEmptyFile,      // {0} = path
    ErrCookedIso,
    ErrBadSectorSize,  // {0} = size in bytes
    ErrNotRaw,
    ErrAlreadyRunning,

    SettingsDialogTitle,
    NoImageDialogTitle,
    NoImageBody,       // {0} = exe directory, {1} = its data directory, {2} = config file, {3} = config key
    ImportDialogTitle,

    PickerTitle,
    PickerHint,
    ActionBrowse,
    PickerFilter,
    ErrNoFileDialog,   // {0} = reason from the system

    Count_
};

// Falls back to the English text if the translation has none; never returns an empty view for a valid key.
std::string_view text(Lang lang, Str key);

// Replaces {0}, {1}, ... in the localized text. Unknown placeholders are left as they are.
std::string format(Lang lang, Str key, std::initializer_list<std::string_view> args);
inline std::string format(Lang lang, Str key) { return std::string(text(lang, key)); }

// The language of a locale, lower case, without region or encoding: "ru" of "ru_RU.UTF-8"; "" for C and POSIX.
std::string language_code(std::string_view locale);

// En for an English, empty, C or POSIX locale, otherwise Local.
Lang lang_from_code(std::string_view code);

// LC_ALL, then LC_MESSAGES, then LANG; "" when none is set.
std::string locale_from_environment();
Lang detect_lang_from_environment();

// Reads <text_root>/<language_code(locale)>/import_ui.json as the Local text, replacing any earlier one. False
// (and English only) when the file is missing or unreadable.
bool load_translation(const std::filesystem::path& text_root, std::string_view locale);

// "2 min 10 s" in the interface language. Rounds to whole seconds.
std::string format_duration(Lang lang, double seconds);

}  // namespace sh::import::ui
