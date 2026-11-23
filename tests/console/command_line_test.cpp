// The arguments of the game (engine/app/command_line.h) and the lines of a -consolecmd file
// (engine/app/command_script.h).
#include "engine/app/command_line.h"
#include "engine/app/command_script.h"

#include "framework/sh_test.h"

#include <initializer_list>
#include <vector>

using sh::app::CommandLine;
using sh::app::ScriptLine;

namespace {

CommandLine parse(std::initializer_list<const char*> args) {
    std::vector<const char*> argv{"SilentHillPC"};
    argv.insert(argv.end(), args);
    return sh::app::parse_command_line(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

SH_TEST(command_line_defaults_and_values) {
    const CommandLine none = parse({});
    SH_CHECK(none.action == CommandLine::Action::Run);
    SH_CHECK(!none.data_dir && !none.image && !none.renderer && !none.command_file);
    SH_CHECK(!none.reimport && !none.quit_after_import && !none.dialogs_to_console && !none.skip_to_game);
    SH_CHECK(!none.setup && !none.setup_script);
    SH_CHECK(parse({"--setup-script", "s.txt"}).setup_script == "s.txt");

    const CommandLine all =
        parse({"-data", "/d", "--image", "a b.cue", "--reimport", "--quit-after-import", "--dialogs-to-console",
               "-skiptogame", "-renderer", "gl", "-consolecmd", "c.txt", "--setup"});
    SH_CHECK(all.action == CommandLine::Action::Run);
    SH_CHECK(all.data_dir == "/d" && all.image == "a b.cue" && all.renderer == "gl" && all.command_file == "c.txt");
    SH_CHECK(all.reimport && all.quit_after_import && all.dialogs_to_console && all.skip_to_game && all.setup);
}

SH_TEST(command_line_ignores_what_it_does_not_know) {
    const CommandLine line = parse({"--nonsense", "-data"});  // no value follows -data
    SH_CHECK(line.action == CommandLine::Action::Run && !line.data_dir);
    SH_CHECK(parse({"-data", "one", "-data", "two"}).data_dir == "two");
    SH_CHECK(parse({"-data", ""}).data_dir == "");  // an empty value is still a value
    // A value is taken as it is, even when it looks like an option.
    SH_CHECK(parse({"--image", "--reimport"}).image == "--reimport");
    SH_CHECK(!parse({"--image", "--reimport"}).reimport);
}

SH_TEST(command_line_actions) {
    SH_CHECK(parse({"--install-desktop"}).action == CommandLine::Action::InstallDesktop);
    SH_CHECK(parse({"--install-desktop", "--uninstall-desktop"}).action == CommandLine::Action::UninstallDesktop);
    SH_CHECK(parse({"-h"}).action == CommandLine::Action::ShowHelp);
    SH_CHECK(parse({"--help"}).action == CommandLine::Action::ShowHelp);
    // The first request for help or the version wins, wherever the menu entry is asked for.
    SH_CHECK(parse({"--install-desktop", "--version"}).action == CommandLine::Action::ShowVersion);
    SH_CHECK(parse({"--version", "--install-desktop", "--help"}).action == CommandLine::Action::ShowVersion);
    SH_CHECK(parse({"--help", "--version"}).action == CommandLine::Action::ShowHelp);
    const std::string usage = sh::app::usage_text();
    for (const char* option : {"-data", "--image", "--reimport", "--quit-after-import", "-skiptogame", "-renderer",
                               "-consolecmd", "--dialogs-to-console", "--install-desktop", "--uninstall-desktop",
                               "--version", "--help"}) {
        SH_CHECK(usage.find(option) != std::string::npos);
    }
}

SH_TEST(command_script_lines) {
    ScriptLine line;
    SH_CHECK(!ScriptLine::parse("", line));
    SH_CHECK(!ScriptLine::parse("\r\n", line));
    SH_CHECK(!ScriptLine::parse("# a comment", line));

    SH_CHECK(ScriptLine::parse("give Handgun\n", line));
    SH_CHECK(line.wait == ScriptLine::Wait::None && line.command == "give Handgun");

    SH_CHECK(ScriptLine::parse("@17.5 \t equip Handgun\r\n", line));
    SH_CHECK(line.wait == ScriptLine::Wait::Seconds && line.seconds == 17.5f && line.command == "equip Handgun");

    SH_CHECK(ScriptLine::parse("@+30 shot /tmp/a.png", line));
    SH_CHECK(line.wait == ScriptLine::Wait::PlayTime && line.sixtieths == 30 && line.command == "shot /tmp/a.png");

    // A wait alone holds back what follows.
    SH_CHECK(ScriptLine::parse("@4", line));
    SH_CHECK(line.wait == ScriptLine::Wait::Seconds && line.seconds == 4.0f && line.command.empty());
    SH_CHECK(ScriptLine::parse("@+0   ", line));
    SH_CHECK(line.wait == ScriptLine::Wait::PlayTime && line.sixtieths == 0 && line.command.empty());
    // No play time is negative: such a line is due at once.
    SH_CHECK(ScriptLine::parse("@+-5 quit", line));
    SH_CHECK(line.wait == ScriptLine::Wait::Seconds && line.seconds == 0.0f && line.command == "quit");
}
