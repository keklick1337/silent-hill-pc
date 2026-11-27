#include "import/ui/strings.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "formats/common/json.h"

namespace sh::import::ui {

namespace {

struct Entry {
    std::string_view key;
    std::string_view en;
};

// Order follows enum Str.
constexpr std::array<Entry, static_cast<std::size_t>(Str::Count_)> kTable = {{
    {"ui.import.title", "First run: game import"},
    {"ui.import.intro_need_image",
     "The game data has not been imported yet. A Silent Hill disc image (NTSC USA 1.1, SLUS-007.07) is "
     "required. It is unpacked once into {0}; after that the image is not needed."},
    {"ui.import.intro_formats",
     "Accepted: .cue, .bin (MODE2/2352), an .iso saved as raw (2352 bytes per sector), or a .zip holding them."},
    {"ui.import.path_label", "Image path"},
    {"ui.import.setting_overwrite", "Replace existing game data ({0})"},
    {"ui.import.setting_verify", "Verify the result after the import ({0})"},
    {"ui.import.setting_threads", "Worker threads, 0 = one per core ({0})"},
    {"ui.import.path_hint_prefix", "hint: {0}"},
    {"ui.import.console_prompt", "Image path (q to quit)"},
    {"ui.import.action_import", "Import"},
    {"ui.import.action_cancel", "Cancel"},
    {"ui.import.action_retry", "Try again"},
    {"ui.import.action_quit", "Quit"},
    {"ui.import.action_close", "Close"},
    {"ui.import.ready_title", "Nothing to import"},
    {"ui.import.ready_body", "The game data is already in {0}."},
    {"ui.import.importing_title", "Importing the game"},
    {"ui.import.cancelling_title", "Cancelling the import"},
    {"ui.import.cancelling_body", "Stopping the import; the unfinished result will be removed."},
    {"ui.import.stage_read", "Reading the image"},
    {"ui.import.stage_raw", "Extracting game files"},
    {"ui.import.stage_convert", "Converting resources"},
    {"ui.import.stage_verify", "Verifying"},
    {"ui.import.stage_commit", "Finishing"},
    {"ui.import.stage_unknown", "Stage: {0}"},
    {"ui.import.eta_unknown", "time estimate is not available yet"},
    {"ui.import.eta_left", "about {0} left"},
    {"ui.import.done_title", "Import finished"},
    {"ui.import.done_body", "The game data is saved in {0}."},
    {"ui.import.failed_title", "Import failed"},
    {"ui.import.failed_body", "Reason: {0}"},
    {"ui.import.failed_no_reason", "no reason given"},
    {"ui.import.cancelled_title", "Import cancelled"},
    {"ui.import.cancelled_body", "Nothing was written to the game data directory."},
    {"ui.import.aborted_title", "Import not done"},
    {"ui.import.aborted_body", "The game needs the imported data. Start it again and give the image."},
    {"ui.import.duration_hours", "{0} h {1} min"},
    {"ui.import.duration_minutes", "{0} min {1} s"},
    {"ui.import.duration_seconds", "{0} s"},
    {"ui.import.err_empty", "No image path given."},
    {"ui.import.err_not_found", "File not found: {0}"},
    {"ui.import.err_is_directory", "{0} is a directory. An image file (.cue or .bin) is required."},
    {"ui.import.err_unreadable", "Cannot read the file: {0}"},
    {"ui.import.err_cue_no_file", "The .cue file {0} has no FILE line naming the image."},
    {"ui.import.err_cue_target_missing", "The file named by the .cue was not found next to it: {0}"},
    {"ui.import.err_empty_file", "The file is empty: {0}"},
    {"ui.import.err_cooked_iso",
     "This is a plain .iso (2048 bytes per sector): it lacks the subheaders and the XA/FMV data the game "
     "needs. A .bin/.cue (MODE2/2352) image or an .iso saved as raw (2352 bytes per sector) is required."},
    {"ui.import.err_bad_sector_size",
     "The file size ({0} bytes) is not a multiple of 2352: the image is truncated or uses another sector "
     "size. MODE2/2352 (2352 bytes per sector) is required."},
    {"ui.import.err_not_raw",
     "The file does not look like a disc image: the CD sync pattern is missing at the start of the "
     "sector. A .bin/.cue (MODE2/2352) or an .iso saved as raw is required."},
    {"ui.import.err_already_running", "An import is already running."},
    {"ui.import.settings_dialog_title", "Silent Hill - settings"},
    {"ui.import.no_image_dialog_title", "Silent Hill - no disc image"},
    {"ui.import.no_image_body",
     "The first start imports the game from your disc image, and none was found.\n\nPut the image (.cue "
     "and .bin, MODE2/2352, or a .zip of them) next to the game:\n  {0}\nor into\n  {1}\nor start the game "
     "with --image <path>, or set {3} = <path> in\n  {2}\n\nSupported: Silent Hill NTSC USA 1.1 (SLUS-007.07)."},
    {"ui.import.import_dialog_title", "Silent Hill - import"},
    {"ui.import.picker_title", "Select your Silent Hill (NTSC USA v1.1) disc image"},
    {"ui.import.picker_hint",
     "Choose the .cue, .bin, .iso, .img or .zip file with Browse, type its path below, or drop the file onto this "
     "window."},
    {"ui.import.action_browse", "Browse..."},
    {"ui.import.picker_filter", "Disc images"},
    {"ui.import.err_no_file_dialog",
     "The file dialog is not available ({0}). Type the path or drop the file onto this window."},
    {"ui.import.err_zip_unreadable", "Cannot read the archive {0}: it is not a .zip file, or it is damaged."},
    {"ui.import.err_zip_no_image",
     "The archive {0} holds no disc image. Zip the .cue and .bin (MODE2/2352), or a .bin or raw .iso alone."},
    {"ui.import.picker_hint_touch", "Choose the .zip, .bin or .cue file of your disc with Browse."},
    {"ui.import.err_no_file_dialog_touch", "The file dialog is not available ({0})."},
    {"ui.import.stage_prepare", "Preparing the first start"},
    {"ui.import.prepare_tables", "Building the game tables and maps..."},
    {"ui.import.prepare_areas", "Assembling the map areas..."},
    {"ui.import.prepare_textures", "Preparing the textures..."},
    {"ui.import.prepare_materials", "Listing the materials..."},
    {"ui.import.prepare_starting", "Starting the game..."},
    {"ui.import.err_empty_touch",
     "No file is chosen yet. Tap Browse and choose the .zip of your disc image."},
    {"ui.import.picker_opening",
     "Reading {0}... A large file can take a few minutes."},
    {"ui.import.err_document_unreadable",
     "Cannot open the chosen file {0}. Choose it again with Browse; if it is kept in a cloud, download it to the device first."},
    {"ui.import.err_no_access_touch",
     "Cannot open {0}: on Android the game reads files only in its own folder. Choose the file with Browse."},
}};

// The Local text, by Str; empty where the translation has no string.
std::array<std::string, kTable.size()>& local_table() {
    static std::array<std::string, kTable.size()> table;
    return table;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

std::string_view text(Lang lang, Str key) {
    const std::size_t index = static_cast<std::size_t>(key);
    if (index >= kTable.size()) {
        return {};
    }
    if (lang == Lang::Local && !local_table()[index].empty()) {
        return local_table()[index];
    }
    return kTable[index].en;
}

std::string format(Lang lang, Str key, std::initializer_list<std::string_view> args) {
    const std::string_view pattern = text(lang, key);
    std::string out;
    out.reserve(pattern.size() + 32);
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 1] >= '0' && pattern[i + 1] <= '9' &&
            pattern[i + 2] == '}') {
            const std::size_t n = static_cast<std::size_t>(pattern[i + 1] - '0');
            if (n < args.size()) {
                out.append(*(args.begin() + static_cast<std::ptrdiff_t>(n)));
                i += 2;
                continue;
            }
        }
        out.push_back(pattern[i]);
    }
    return out;
}

std::string language_code(std::string_view locale) {
    const std::size_t end = locale.find_first_of("_-.@");
    const std::string code = lower(locale.substr(0, end));
    return code == "c" || code == "posix" ? std::string() : code;
}

Lang lang_from_code(std::string_view code) {
    const std::string language = language_code(code);
    return language.empty() || language == "en" ? Lang::En : Lang::Local;
}

std::string locale_from_environment() {
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);
        if (value && *value) {
            return value;
        }
    }
    return {};
}

Lang detect_lang_from_environment() { return lang_from_code(locale_from_environment()); }

bool load_translation(const std::filesystem::path& text_root, std::string_view locale) {
    auto& table = local_table();
    table.fill({});
    const std::string language = language_code(locale);
    if (language.empty() || language.find_first_of("/\\") != std::string::npos) {
        return false;
    }
    std::ifstream in(text_root / language / "import_ui.json", std::ios::binary);
    if (!in) {
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const Result<json::Value> doc = json::parse(text);
    const json::Value* strings = doc ? doc.value().find("strings") : nullptr;
    if (!strings || !strings->is_object()) {
        return false;
    }
    for (std::size_t i = 0; i < kTable.size(); ++i) {
        const json::Value* value = strings->find(kTable[i].key);
        if (value && value->is_string()) {
            table[i] = value->string();
        }
    }
    return true;
}

std::string format_duration(Lang lang, double seconds) {
    if (!(seconds > 0.0)) {
        seconds = 0.0;
    }
    const std::uint64_t total = static_cast<std::uint64_t>(std::llround(seconds));
    const std::string s = std::to_string(total % 60);
    const std::string m = std::to_string((total / 60) % 60);
    if (total >= 3600) {
        return format(lang, Str::DurationHours, {std::to_string(total / 3600), m});
    }
    if (total >= 60) {
        return format(lang, Str::DurationMinutes, {std::to_string(total / 60), s});
    }
    return format(lang, Str::DurationSeconds, {s});
}

}  // namespace sh::import::ui
