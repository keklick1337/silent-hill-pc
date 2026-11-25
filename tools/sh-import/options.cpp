#include "sh-import/options.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace sh::import_tool {

namespace {

constexpr std::array<std::string_view, kCategoryCount> kCategoryNames{"textures", "models", "audio", "text", "levels",
                                                                       "data"};

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<bool> parse_bool(std::string_view text) {
    const std::string v = lower(trim(text));
    if (v == "1" || v == "true" || v == "yes" || v == "on") {
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off") {
        return false;
    }
    return std::nullopt;
}

std::optional<unsigned> parse_threads(std::string_view text) {
    text = trim(text);
    unsigned value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || ec != std::errc() || end != text.data() + text.size() || value > 1024) {
        return std::nullopt;
    }
    return value;
}

std::optional<Category> parse_category(std::string_view text) {
    const std::string v = lower(trim(text));
    for (std::size_t i = 0; i < kCategoryCount; ++i) {
        if (v == kCategoryNames[i]) {
            return static_cast<Category>(i);
        }
    }
    return std::nullopt;
}

Result<std::vector<Category>> parse_category_list(std::string_view text, std::string_view option) {
    using R = Result<std::vector<Category>>;
    std::vector<Category> list;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find(',', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view item = trim(text.substr(start, end - start));
        if (!item.empty()) {
            const auto category = parse_category(item);
            if (!category) {
                return R::fail(std::string(option) + ": unknown group \"" + std::string(item) +
                               "\" (textures, models, audio, text, levels, data)");
            }
            if (std::find(list.begin(), list.end(), *category) == list.end()) {
                list.push_back(*category);
            }
        }
        start = end + 1;
    }
    if (list.empty()) {
        return R::fail(std::string(option) + " needs at least one group (textures, models, audio, text, levels, data)");
    }
    return R::ok(std::move(list));
}

// import_textures = png turns a group on, = none turns it off; anything else is an error.
struct Switch {
    std::string_view key;
    Category category;
    std::string_view on;
};
constexpr Switch kSwitches[] = {
    {"import_textures", Category::Textures, "png"}, {"import_models", Category::Models, "glb"},
    {"import_audio", Category::Audio, "wav"},       {"import_text", Category::Text, "json"},
    {"import_levels", Category::Levels, "unified"}, {"import_data", Category::Data, "json"},
};

std::string invalid(std::size_t line, std::string_view key, std::string_view value, std::string_view expected) {
    return "config line " + std::to_string(line) + ": " + std::string(key) + " = " + std::string(value) +
           " is not valid, expected " + std::string(expected);
}

}  // namespace

std::string_view category_name(Category category) {
    return kCategoryNames[static_cast<std::size_t>(category)];
}

Result<CliArgs> parse_args(std::span<const std::string> args) {
    using R = Result<CliArgs>;
    CliArgs out;
    bool have_image = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg.size() < 2 || arg[0] != '-') {
            if (have_image) {
                return R::fail("more than one image given: \"" + std::string(arg) + "\"");
            }
            have_image = true;
            out.image = std::filesystem::path(std::string(arg));
            continue;
        }

        std::string_view name = arg;
        std::optional<std::string_view> inline_value;
        if (const std::size_t eq = arg.find('='); eq != std::string_view::npos && arg.starts_with("--")) {
            name = arg.substr(0, eq);
            inline_value = arg.substr(eq + 1);
        }
        // The value of an option: after '=' or the next argument.
        const auto value = [&]() -> std::optional<std::string> {
            if (inline_value) {
                return std::string(*inline_value);
            }
            if (i + 1 < args.size()) {
                return args[++i];
            }
            return std::nullopt;
        };
        const auto needs_value = [&] { return R::fail(std::string(name) + " needs a value"); };

        if (name == "--help" || name == "-h") {
            out.help = true;
        } else if (name == "--out" || name == "--config") {
            const auto v = value();
            if (!v || v->empty()) {
                return needs_value();
            }
            (name == "--out" ? out.out : out.config) = std::filesystem::path(*v);
        } else if (name == "--threads") {
            const auto v = value();
            if (!v) {
                return needs_value();
            }
            out.threads = parse_threads(*v);
            if (!out.threads) {
                return R::fail("--threads needs a number from 0 to 1024, got \"" + *v + "\"");
            }
        } else if (name == "--only" || name == "--skip") {
            const auto v = value();
            if (!v) {
                return needs_value();
            }
            auto list = parse_category_list(*v, name);
            if (!list) {
                return R::fail(list.error());
            }
            if (name == "--only") {
                out.only = std::move(list.value());
            } else {
                for (Category c : list.value()) {
                    if (std::find(out.skip.begin(), out.skip.end(), c) == out.skip.end()) {
                        out.skip.push_back(c);
                    }
                }
            }
        } else if (name == "--keep-raw") {
            if (inline_value) {
                return R::fail(std::string(name) + " takes no value");
            }
            out.keep_raw = true;
        } else if (name == "--overwrite" || name == "--no-overwrite" || name == "--verify" || name == "--no-verify") {
            if (inline_value) {
                return R::fail(std::string(name) + " takes no value");
            }
            const bool on = !name.starts_with("--no-");
            (name.ends_with("overwrite") ? out.overwrite : out.verify) = on;
        } else {
            return R::fail("unknown option " + std::string(name));
        }
    }
    return R::ok(std::move(out));
}

Result<void> apply_config(Options& options, std::string_view text, std::vector<std::string>& warnings) {
    using R = Result<void>;
    std::size_t line_no = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++line_no;

        // A comment starts with '#' at the start of the line or after white space.
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '#' && (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1])))) {
                line = line.substr(0, i);
                break;
            }
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            return R::fail("config line " + std::to_string(line_no) + ": expected \"key = value\", got \"" +
                           std::string(line) + "\"");
        }
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string_view value = trim(line.substr(eq + 1));

        if (key == "gamedata_dir") {
            if (value.empty()) {
                return R::fail(invalid(line_no, key, value, "a directory"));
            }
            options.out = std::filesystem::path(std::string(value));
        } else if (key == "import_image") {
            if (!value.empty()) {  // empty means "ask", which a command line tool cannot do
                options.image = std::filesystem::path(std::string(value));
            }
        } else if (key == "import_overwrite" || key == "import_verify") {
            const auto flag = parse_bool(value);
            if (!flag) {
                return R::fail(invalid(line_no, key, value, "0 or 1"));
            }
            (key == "import_overwrite" ? options.overwrite : options.verify) = *flag;
        } else if (key == "keep_raw") {
            const auto flag = parse_bool(value);
            if (!flag) {
                return R::fail(invalid(line_no, key, value, "0 or 1"));
            }
            options.keep_raw = *flag;
        } else if (key == "import_threads") {
            const auto threads = parse_threads(value);
            if (!threads) {
                return R::fail(invalid(line_no, key, value, "a number from 0 to 1024"));
            }
            options.threads = *threads;
        } else if (key == "import_tables") {
            const std::string v = lower(value);
            if (v != "json" && v != "none") {
                return R::fail(invalid(line_no, key, value, "json or none"));
            }
            /* "none" does not turn the step off: the game requires the tables. */
            if (v == "none") {
                return R::fail(invalid(line_no, key, value,
                                       "json: the game requires the tables, a gamedata without them cannot "
                                       "be played"));
            }
        } else if (key == "import_music") {
            const std::string v = lower(value);
            if (v != "wav" && v != "opus" && v != "none") {
                return R::fail(invalid(line_no, key, value, "wav, opus or none"));
            }
            options.music = v != "none";
            options.music_opus = v == "opus";
        } else {
            const auto sw = std::find_if(std::begin(kSwitches), std::end(kSwitches),
                                         [&](const Switch& s) { return s.key == key; });
            if (sw == std::end(kSwitches)) {
                warnings.push_back("config line " + std::to_string(line_no) + ": unknown key \"" + key + "\" ignored");
                continue;
            }
            const std::string v = lower(value);
            if (sw->category == Category::Levels && v == "glb") {
                // The unified map files and the per-cell views of convert/levels/.
                options.set_enabled(Category::Levels, true);
                options.level_views = true;
                continue;
            }
            if (sw->category == Category::Levels) {
                options.level_views = false;
            }
            if (v != sw->on && v != "none") {
                return R::fail(invalid(line_no, key, value,
                                       sw->category == Category::Levels ? std::string("unified, glb or none")
                                                                        : std::string(sw->on) + " or none"));
            }
            options.set_enabled(sw->category, v == sw->on);
        }
    }
    return R::ok();
}

void apply_cli(Options& options, const CliArgs& args) {
    if (args.image) {
        options.image = *args.image;
    }
    if (args.out) {
        options.out = *args.out;
    }
    if (args.config) {
        options.config = *args.config;
    }
    if (args.overwrite) {
        options.overwrite = *args.overwrite;
    }
    if (args.verify) {
        options.verify = *args.verify;
    }
    if (args.keep_raw) {
        options.keep_raw = *args.keep_raw;
    }
    if (args.threads) {
        options.threads = *args.threads;
    }
    if (args.only) {
        options.enabled.fill(false);
        for (Category c : *args.only) {
            options.set_enabled(c, true);
        }
    }
    for (Category c : args.skip) {
        options.set_enabled(c, false);
    }
}

std::string usage() {
    return "usage: sh-import <image> [options]\n"
           "\n"
           "Imports a Silent Hill (NTSC USA 1.1) disc image into a gamedata directory: index.tsv, version.cfg,\n"
           "convert/ (PNG, glTF, WAV, JSON, and files/ for the game files no converter rebuilds), tables/ and\n"
           "maps/. The disc's files are extracted to raw/ first and deleted at the end.\n"
           "\n"
           "  <image>             .cue, .bin (MODE2/2352), a raw .iso (2352 bytes per sector) or a .zip of them\n"
           "  --out DIR           result directory (gamedata_dir), default \"gamedata\"\n"
           "  --overwrite         replace an existing DIR (import_overwrite)\n"
           "  --no-verify         skip the CRC32 and round-trip checks (import_verify = 0)\n"
           "  --threads N         worker threads, 0 = one per hardware thread (import_threads)\n"
           "  --keep-raw          keep raw/, the disc's files as they are (keep_raw = 1)\n"
           "  --only LIST         convert only these groups: textures, models, audio, text, levels, data\n"
           "  --skip LIST         do not convert these groups\n"
           "  --config FILE       read \"key = value\" lines (import_image, gamedata_dir, import_overwrite,\n"
           "                      import_verify, import_threads, import_textures, import_models, import_audio,\n"
           "                      import_text, import_levels, import_data, import_tables, import_music,\n"
           "                      keep_raw); the command line wins\n"
           "  -h, --help          this text\n"
           "\n"
           "Exit code 0 on success, 1 when the import failed, 2 on a usage error.\n";
}

}  // namespace sh::import_tool
