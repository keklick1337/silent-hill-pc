#include "framework/sh_test.h"
#include "sh-import/converters.h"
#include "sh-import/options.h"

#include <string>
#include <vector>

namespace {

using namespace sh::import_tool;

sh::Result<CliArgs> parse(std::vector<std::string> args) {
    return parse_args(args);
}

std::string names_of(const Options& options) {
    std::string out;
    for (const auto& c : build_converters(options, {})) {
        out += (out.empty() ? "" : ",") + std::string(c->name());
    }
    return out;
}

// The tool's resolution order: config first, then the command line over it.
sh::Result<Options> resolve(const std::string& config, std::vector<std::string> args,
                            std::vector<std::string>* warnings = nullptr) {
    auto cli = parse(std::move(args));
    if (!cli) {
        return sh::Result<Options>::fail(cli.error());
    }
    Options options;
    std::vector<std::string> local;
    const auto applied = apply_config(options, config, warnings ? *warnings : local);
    if (!applied) {
        return sh::Result<Options>::fail(applied.error());
    }
    apply_cli(options, cli.value());
    return sh::Result<Options>::ok(options);
}

}  // namespace

SH_TEST(import_tool_defaults) {
    const Options options;
    SH_CHECK(options.image.empty());
    SH_CHECK(options.out == "gamedata");
    SH_CHECK(!options.overwrite);
    SH_CHECK(options.verify);
    SH_CHECK(options.threads == 0);
    // import_levels = unified: the level group is on, but its output is written after the import
    // (gamedata/maps/areas/), so no converter runs for it; = glb adds the per-cell views.
    SH_CHECK(options.is_enabled(Category::Levels));
    SH_CHECK(!options.level_views);
    SH_CHECK(names_of(options) == "tim,model,anim,vab,xa,kdt,music,font,cutscene,cmp,demo");
    auto views = resolve("import_levels = glb\n", {});
    SH_CHECK(views && views.value().level_views && names_of(views.value()) == "tim,model,anim,vab,xa,kdt,music,font,level,cutscene,cmp,demo");
    auto unified = resolve("import_levels = glb\nimport_levels = unified\n", {});
    SH_CHECK(unified && !unified.value().level_views && unified.value().is_enabled(Category::Levels));
    auto none = resolve("import_levels = none\n", {});
    SH_CHECK(none && !none.value().is_enabled(Category::Levels));
}

SH_TEST(import_tool_music_formats) {
    auto opus = resolve("import_music = opus\n", {});
    SH_CHECK(opus && opus.value().music && opus.value().music_opus);
    SH_CHECK(opus && names_of(opus.value()).find("music") != std::string::npos);
    auto wav = resolve("import_music = opus\nimport_music = wav\n", {});
    SH_CHECK(wav && wav.value().music && !wav.value().music_opus);
    auto none = resolve("import_music = none\n", {});
    SH_CHECK(none && !none.value().music && names_of(none.value()).find("music") == std::string::npos);
    SH_CHECK(!resolve("import_music = mp3\n", {}));
}

SH_TEST(import_tool_args_basic) {
    const auto args = parse({"disc.cue", "--out", "dir", "--overwrite", "--no-verify", "--threads", "4", "--config", "c.cfg"});
    SH_CHECK(args);
    const CliArgs& a = args.value();
    SH_CHECK(a.image && *a.image == "disc.cue");
    SH_CHECK(a.out && *a.out == "dir");
    SH_CHECK(a.overwrite && *a.overwrite);
    SH_CHECK(a.verify && !*a.verify);
    SH_CHECK(a.threads && *a.threads == 4);
    SH_CHECK(a.config && *a.config == "c.cfg");
    SH_CHECK(!a.only);
    SH_CHECK(a.skip.empty());
    SH_CHECK(!a.help);
}

SH_TEST(import_tool_args_equals_form_and_position) {
    const auto args = parse({"--out=x", "--threads=0", "--only=audio,text", "image.bin"});
    SH_CHECK(args);
    SH_CHECK(args.value().image && *args.value().image == "image.bin");
    SH_CHECK(args.value().out && *args.value().out == "x");
    SH_CHECK(args.value().threads && *args.value().threads == 0);
    SH_CHECK(args.value().only && args.value().only->size() == 2);
}

SH_TEST(import_tool_args_errors) {
    SH_CHECK(!parse({"a.cue", "b.cue"}));              // two images
    SH_CHECK(!parse({"--bogus"}));                      // unknown option
    SH_CHECK(!parse({"--out"}));                        // missing value
    SH_CHECK(!parse({"--threads", "many"}));            // not a number
    SH_CHECK(!parse({"--threads", "-1"}));
    SH_CHECK(!parse({"--threads", "5000"}));
    SH_CHECK(!parse({"--only", "textures,video"}));     // unknown group
    SH_CHECK(!parse({"--skip", ""}));                   // empty list
    SH_CHECK(!parse({"--overwrite=1"}));                // flag with a value
    const auto bad = parse({"--only", "movies"});
    SH_CHECK(!bad && bad.error().find("movies") != std::string::npos);
}

SH_TEST(import_tool_args_help) {
    SH_CHECK(parse({"--help"}).value().help);
    SH_CHECK(parse({"-h"}).value().help);
    SH_CHECK(usage().find("--only") != std::string::npos);
}

SH_TEST(import_tool_config_all_keys) {
    const std::string text =
        "# comment\n"
        "\n"
        "gamedata_dir = out/gamedata\n"
        "import_image = /discs/sh.cue   # trailing comment\n"
        "import_overwrite = 1\n"
        "import_verify = 0\n"
        "import_threads = 3\n"
        "import_textures = none\n"
        "import_models = glb\n"
        "import_audio = none\n"
        "import_text = json\n"
        "import_levels = none\n";
    Options options;
    std::vector<std::string> warnings;
    SH_CHECK(apply_config(options, text, warnings));
    SH_CHECK(warnings.empty());
    SH_CHECK(options.out == "out/gamedata");
    SH_CHECK(options.image == "/discs/sh.cue");
    SH_CHECK(options.overwrite);
    SH_CHECK(!options.verify);
    SH_CHECK(options.threads == 3);
    SH_CHECK(names_of(options) == "model,anim,font,cutscene,cmp,demo");
}

SH_TEST(import_tool_config_format) {
    Options options;
    std::vector<std::string> warnings;
    // Spaces around '=' are optional, CRLF is fine, keys are case-insensitive, '#' inside a value without a space
    // before it stays, the last of two equal keys wins, an empty import_image means "ask" and changes nothing.
    SH_CHECK(apply_config(options, "IMPORT_THREADS=2\r\ngamedata_dir = a#b\r\nimport_threads = 6\r\nimport_image =\r\n", warnings));
    SH_CHECK(options.threads == 6);
    SH_CHECK(options.out == "a#b");
    SH_CHECK(options.image.empty());
    SH_CHECK(warnings.empty());
    // No trailing newline.
    SH_CHECK(apply_config(options, "import_verify = false", warnings));
    SH_CHECK(!options.verify);
}

SH_TEST(import_tool_config_unknown_key_warns) {
    Options options;
    std::vector<std::string> warnings;
    SH_CHECK(apply_config(options, "render_scale = 2\nimport_threads = 2\nimport_speech = wav\n", warnings));
    SH_CHECK(warnings.size() == 2);
    SH_CHECK(warnings[0].find("render_scale") != std::string::npos);
    SH_CHECK(warnings[0].find("line 1") != std::string::npos);
    SH_CHECK(warnings[1].find("import_speech") != std::string::npos);
    SH_CHECK(options.threads == 2);  // the known key was still applied
}

SH_TEST(import_tool_config_errors_name_the_line) {
    Options options;
    std::vector<std::string> warnings;
    auto r = apply_config(options, "import_verify = 1\nimport_threads = lots\n", warnings);
    SH_CHECK(!r && r.error().find("line 2") != std::string::npos && r.error().find("import_threads") != std::string::npos);
    SH_CHECK(!apply_config(options, "import_textures = jpg\n", warnings));
    SH_CHECK(!apply_config(options, "import_models = png\n", warnings));  // each group has its own value
    SH_CHECK(!apply_config(options, "import_overwrite = maybe\n", warnings));
    SH_CHECK(!apply_config(options, "import_tables = none\n", warnings));  // not a key
    SH_CHECK(!apply_config(options, "import_tables = yaml\n", warnings));
    SH_CHECK(!apply_config(options, "just a line\n", warnings));
    SH_CHECK(!apply_config(options, "gamedata_dir =\n", warnings));
}

SH_TEST(import_tool_priority_command_line_over_config) {
    const std::string config =
        "gamedata_dir = from_config\n"
        "import_image = config.cue\n"
        "import_overwrite = 1\n"
        "import_verify = 0\n"
        "import_threads = 2\n"
        "import_audio = none\n";
    // Nothing on the command line: the config values hold.
    auto only_config = resolve(config, {});
    SH_CHECK(only_config);
    SH_CHECK(only_config.value().out == "from_config");
    SH_CHECK(only_config.value().image == "config.cue");
    SH_CHECK(only_config.value().overwrite);
    SH_CHECK(!only_config.value().verify);
    SH_CHECK(only_config.value().threads == 2);
    SH_CHECK(names_of(only_config.value()) == "tim,model,anim,font,cutscene,cmp,demo");

    // The command line wins, flag by flag.
    auto mixed = resolve(config, {"cli.cue", "--out", "from_cli", "--no-overwrite", "--verify", "--threads", "8"});
    SH_CHECK(mixed);
    SH_CHECK(mixed.value().out == "from_cli");
    SH_CHECK(mixed.value().image == "cli.cue");
    SH_CHECK(!mixed.value().overwrite);
    SH_CHECK(mixed.value().verify);
    SH_CHECK(mixed.value().threads == 8);

    // A flag that the command line does not mention keeps the config value.
    auto partial = resolve(config, {"--threads", "0"});
    SH_CHECK(partial.value().threads == 0);
    SH_CHECK(partial.value().overwrite);
}

SH_TEST(import_tool_only_and_skip) {
    // --only replaces what the config enabled, even a group the config switched off.
    auto only = resolve("import_audio = none\n", {"--only", "audio"});
    SH_CHECK(names_of(only.value()) == "vab,xa,kdt,music");
    auto textures = resolve("", {"--only", "textures"});
    SH_CHECK(names_of(textures.value()) == "tim");
    auto two = resolve("", {"--only", "levels,models"});
    SH_CHECK(names_of(two.value()) == "model,anim");  // levels: written after the import, see import_tool_defaults
    SH_CHECK(two.value().is_enabled(Category::Levels));
    auto two_views = resolve("import_levels = glb\n", {"--only", "levels,models"});
    SH_CHECK(names_of(two_views.value()) == "model,anim,level");  // fixed order, not the order given
    auto text_only = resolve("", {"--only", "text"});
    SH_CHECK(names_of(text_only.value()) == "font");

    // --skip removes, on top of the config.
    auto skip = resolve("", {"--skip", "audio,levels"});
    SH_CHECK(names_of(skip.value()) == "tim,model,anim,font,cutscene,cmp,demo");
    auto both = resolve("", {"--only", "textures,models,audio", "--skip", "models"});
    SH_CHECK(names_of(both.value()) == "tim,vab,xa,kdt,music");
    auto all = resolve("", {"--skip", "textures,models,audio,text,levels,data"});
    SH_CHECK(names_of(all.value()).empty());  // raw only
    // Case and spaces in the list do not matter; the order of --only and --skip on the line does not either.
    auto loose = resolve("", {"--skip", "Audio, LEVELS", "--only", "textures,audio,levels"});
    SH_CHECK(names_of(loose.value()) == "tim");
}

SH_TEST(import_tool_converter_properties) {
    Options options;
    options.level_views = true;
    const auto list = build_converters(options, {});
    SH_CHECK(list.size() == 12);
    for (const auto& c : list) {
        SH_CHECK(c != nullptr);
        // Textures, animations, music sequences, cutscenes, CMP and demos round-trip byte for byte (the importer
        // verifies them); models, sounds, rendered music, the game font and levels are representations.
        SH_CHECK(c->lossless() == (c->name() != "model" && c->name() != "vab" && c->name() != "xa" &&
                                   c->name() != "music" && c->name() != "font" && c->name() != "level"));
    }
}
