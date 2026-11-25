#include "engine/mods/mods_console.h"

#include "engine/mods/mod_import.h"
#include "engine/mods/mods_admin.h"

#include <string>
#include <utility>
#include <vector>

namespace sh::mods {

namespace {

using sh::console::ArgSpec;
using sh::console::ArgType;
using sh::console::Args;
using sh::console::Context;

void print_files(Context& ctx, Registry& reg, const std::string& id) {
    const std::vector<ModFile> files = reg.files(id);
    if (files.empty()) {
        ctx.out.info(id + ": no files in use (an inactive mod's files are not read)");
        return;
    }
    int replaced = 0;
    int overridden = 0;
    for (const ModFile& f : files) {
        replaced += f.replaces ? 1 : 0;
        overridden += f.wins ? 0 : 1;
        std::string line = std::string(f.replaces ? "replace " : "add     ") + f.path;
        if (!f.also_in.empty()) {
            std::string others;
            for (const std::string& o : f.also_in) {
                others += (others.empty() ? "" : ", ") + o;
            }
            line += (f.wins ? "  (wins over " : "  (overridden by a later mod; also in ") + others + ")";
        }
        ctx.out.info(line);
    }
    ctx.out.info(id + ": " + std::to_string(files.size()) + " files, " + std::to_string(replaced) + " replaced, " +
                 std::to_string(files.size() - static_cast<std::size_t>(replaced)) + " added, " +
                 std::to_string(overridden) + " overridden");
}

Result<void> run(Context& ctx, const Args& args) {
    Registry& reg = admin_registry();
    const std::string sub = args.str(0);
    const std::string id = args.str(1);
    if (sub.empty() || sub == "list") {
        for (const std::string& line : reg.report(false)) {
            ctx.out.info(line);
        }
        if (reg.restart_pending()) {
            ctx.out.warn("changed mods apply after restart");
        }
        return Result<void>::ok();
    }
    if (sub == "reload") {
        for (const std::string& line : hot_reload(reg)) {
            ctx.out.info(line);
        }
        return Result<void>::ok();
    }
    if (id.empty()) {
        return Result<void>::fail("usage: mods " + sub + " <id>");
    }
    if (sub == "files") {
        if (reg.plan().find(id) == nullptr) {
            return Result<void>::fail("no mod '" + id + "' (see 'mods')");
        }
        print_files(ctx, reg, id);
        return Result<void>::ok();
    }
    Result<std::string> done = edit_and_save(reg, id, sub);
    if (!done) {
        return Result<void>::fail(done.error());
    }
    ctx.out.info(done.value());
    return Result<void>::ok();
}

Result<void> run_import(Context& ctx, const Args& args) {
    const std::string path = args.str(0);
    const std::string what = args.str(1);
    if (path.empty()) {
        return Result<void>::fail("usage: modimport <folder or .zip> [install]");
    }
    ImportSession& session = import_session();
    session.scan(std::filesystem::path(std::u8string(path.begin(), path.end())));
    const ImportScan& scan = session.result();
    if (scan.error != ImportError::None) {
        return Result<void>::fail(std::string(import_error_name(scan.error)) + ": " + scan.detail);
    }
    for (const FoundMod& m : scan.mods) {
        std::string line = std::string(m.selected ? "[x] " : "[ ] ") + (m.id.empty() ? "?" : m.id) + " " + m.version +
                           "  " + found_status_name(m.status);
        if (!m.installed_version.empty()) {
            line += " (installed " + m.installed_version + ")";
        }
        if (m.status == FoundStatus::Invalid) {
            line += ": " + std::string(invalid_reason_name(m.reason)) + " " + m.detail;
        }
        for (const ImportNote& n : m.notes) {
            line += "; needs " + n.kind + " " + n.detail;
        }
        line += "  <" + (m.location.empty() ? std::string(".") : m.location) + ">";
        ctx.out.info(line);
    }
    if (what != "install") {
        ctx.out.info("'modimport <path> install' imports the marked mods");
        return Result<void>::ok();
    }
    Result<ImportReport> done = session.run(admin_registry());
    if (!done) {
        return Result<void>::fail("import failed: " + done.error());
    }
    for (const std::string& id : done.value().added) {
        ctx.out.info("added " + id + " (switched off: mods enable " + id + ")");
    }
    for (const std::string& id : done.value().replaced) {
        ctx.out.info("replaced " + id);
    }
    return Result<void>::ok();
}

}  // namespace

void register_console_commands(sh::console::Registry& console) {
    sh::console::CommandSpec spec;
    spec.name = "mods";
    spec.group = "mods";
    spec.help = "installed mods: list, files, switch on/off, load order, hot reload";
    spec.details =
        "mods                 every mod, its state and problems, notices about old mod folders\n"
        "mods files <id>      the files of an active mod: replace / add, and which are overridden\n"
        "mods enable <id>     switch a mod on; new mods start off (saved to mods/load_order.json, applies after\n"
        "                     restart; a mod with \"requires_restart\": false at once)\n"
        "mods disable <id>    switch a mod off (the same)\n"
        "mods up <id>         load it earlier (a later mod wins a file)\n"
        "mods down <id>       load it later\n"
        "mods reload          index the active mods' files again (added or removed files), read the texts again,\n"
        "                     define the menus and run the game and mod scripts again (at the next frame)";
    spec.args = {
        ArgSpec{"what", ArgType::Enum, true, "list, files, enable, disable, up, down, reload",
                {"list", "files", "enable", "disable", "up", "down", "reload"}, {}},
        ArgSpec{"id", ArgType::String, true, "mod id", {},
                [](std::string_view, std::vector<sh::console::Candidate>& out) {
                    for (const ResolvedMod& m : admin_registry().pending().mods) {
                        out.push_back({m.candidate.manifest.id, {}});
                    }
                }},
    };
    spec.run = run;
    (void)console.add(std::move(spec));

    sh::console::CommandSpec imp;
    imp.name = "modimport";
    imp.group = "mods";
    imp.help = "import mods from a folder or a .zip into the mods folder";
    imp.details =
        "modimport <path>           lists the mods found in a folder or .zip and what importing does\n"
        "                           ([x] = imported: new mods and newer versions)\n"
        "modimport <path> install   imports them into the mods folder (new mods start switched off)";
    imp.args = {
        ArgSpec{"path", ArgType::String, false, "folder or .zip", {}, {}},
        ArgSpec{"install", ArgType::Enum, true, "install", {"install"}, {}},
    };
    imp.run = run_import;
    (void)console.add(std::move(imp));
}

}  // namespace sh::mods
