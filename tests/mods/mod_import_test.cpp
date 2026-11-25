// Importing mods from a folder or a .zip (engine/mods/mod_import.h): discovery, checks, the all-or-nothing copy and
// the `modimport` console command. The archives are made here with miniz.

#include "engine/console/registry.h"
#include "engine/mods/mod_import.h"
#include "engine/mods/mods_admin.h"
#include "engine/mods/mods_console.h"
#include "engine/mods/registry.h"
#include "framework/sh_test.h"

#include "miniz/include/miniz.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace sh::mods;

namespace {

struct Dir {
    fs::path path;
    explicit Dir(const char* name) : path(fs::path(SH_PROJECT_ROOT) / ".tmp" / "mod_import" / name) {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
    }
    ~Dir() {
        set_admin_registry(nullptr);
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void write(const std::string& rel, std::string_view text) const {
        fs::create_directories((path / rel).parent_path());
        std::ofstream(path / rel, std::ios::binary) << text;
    }
};

std::string manifest(std::string_view id, std::string_view version = "1.0.0", std::string_view extra = "") {
    return std::string(R"({"id": ")") + std::string(id) + R"(", "version": ")" + std::string(version) + "\"" +
           std::string(extra) + "}";
}

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// A zip of `entries` (name, data); names listed in `links` become symbolic links (Unix host, mode 0120777).
void make_zip(const fs::path& file, const std::vector<std::pair<std::string, std::string>>& entries,
              const std::vector<std::string>& links = {},
              const std::vector<std::pair<std::string, std::string>>& renames = {}) {
    mz_zip_archive zip{};
    SH_CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
    for (const auto& [name, data] : entries) {
        SH_CHECK(mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION));
    }
    void* buf = nullptr;
    size_t size = 0;
    SH_CHECK(mz_zip_writer_finalize_heap_archive(&zip, &buf, &size));
    std::string bytes(static_cast<const char*>(buf), size);
    mz_zip_writer_end(&zip);
    // The writer sets no file type: patch the central directory records of the links.
    for (std::size_t at = 0; (at = bytes.find("PK\x01\x02", at)) != std::string::npos; at += 4) {
        const auto u16 = [&](std::size_t o) {
            return static_cast<unsigned>(static_cast<unsigned char>(bytes[at + o])) |
                   static_cast<unsigned>(static_cast<unsigned char>(bytes[at + o + 1])) << 8;
        };
        const std::string name = bytes.substr(at + 46, u16(28));
        if (std::find(links.begin(), links.end(), name) == links.end()) continue;
        bytes[at + 5] = 3;  // version made by: Unix
        const std::uint32_t attr = 0120777u << 16;
        std::memcpy(&bytes[at + 38], &attr, 4);
    }
    // Names the writer refuses (absolute ones) are written under a stand-in of the same length and renamed here.
    for (const auto& [from, to] : renames) {
        for (std::size_t at = 0; (at = bytes.find(from, at)) != std::string::npos; at += to.size()) {
            bytes.replace(at, from.size(), to);
        }
    }
    std::ofstream(file, std::ios::binary) << bytes;
}

const FoundMod* by_id(const ImportScan& scan, std::string_view id) {
    for (const FoundMod& m : scan.mods) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

Environment env() { return Environment{}; }

}  // namespace

SH_TEST(mod_import_folder_is_one_mod) {
    Dir d("one");
    d.write("pick/mod.json", manifest("alpha"));
    d.write("pick/text/en/x.json", "{}");
    d.write("pick/.git/HEAD", "x");
    const ImportScan scan = scan_import(d.path / "pick", {}, env());
    SH_CHECK(scan.error == ImportError::None);
    SH_CHECK(scan.mods.size() == 1);
    SH_CHECK(scan.mods[0].location.empty());
    SH_CHECK(scan.mods[0].status == FoundStatus::New);
    SH_CHECK(scan.mods[0].selected);
    SH_CHECK(scan.mods[0].files == 2);  // hidden files stay out
    // The picked mod.json stands for its folder.
    SH_CHECK(scan_import(d.path / "pick" / "mod.json", {}, env()).mods.size() == 1);
}

SH_TEST(mod_import_folder_of_mods_nested_and_invalid) {
    Dir d("many");
    d.write("pick/a/mod.json", manifest("a"));
    d.write("pick/b/mod.json", manifest("b", "2.0.0"));
    d.write("pick/b/sub/mod.json", manifest("inner"));  // part of b, not a mod of its own
    d.write("pick/pack/mods/c/mod.json", manifest("c"));
    d.write("pick/broken/mod.json", "{ not json");
    d.write("pick/notes/readme.txt", "hi");
    d.write("pick/.hidden/mod.json", manifest("hidden"));
    d.write("pick/1/2/3/4/5/mod.json", manifest("deep"));  // deeper than kImportMaxDepth
    const ImportScan scan = scan_import(d.path / "pick", {}, env());
    SH_CHECK(scan.error == ImportError::None);
    SH_CHECK(scan.mods.size() == 4);
    SH_CHECK(by_id(scan, "a") != nullptr && by_id(scan, "b") != nullptr && by_id(scan, "c") != nullptr);
    SH_CHECK(by_id(scan, "c")->location == "pack/mods/c");
    SH_CHECK(by_id(scan, "inner") == nullptr && by_id(scan, "hidden") == nullptr && by_id(scan, "deep") == nullptr);
    const auto broken = std::find_if(scan.mods.begin(), scan.mods.end(),
                                     [](const FoundMod& m) { return m.location == "broken"; });
    SH_CHECK(broken != scan.mods.end());
    SH_CHECK(broken->status == FoundStatus::Invalid && broken->reason == InvalidReason::Manifest);
    SH_CHECK(!broken->selected);
}

SH_TEST(mod_import_status_against_installed_and_notes) {
    Dir d("status");
    d.write("pick/up/mod.json", manifest("up", "1.2.0"));
    d.write("pick/same/mod.json", manifest("same", "1.0.0"));
    d.write("pick/old/mod.json", manifest("old", "0.9.0"));
    d.write("pick/dep/mod.json", manifest("dep", "1.0.0", R"(, "dependencies": {"lib": ">=2.0", "up": "^1.0"})"));
    d.write("pick/eng/mod.json", manifest("eng", "1.0.0", R"(, "min_engine_version": "99.0.0")"));
    d.write("pick/zz_twice/mod.json", manifest("up", "1.3.0"));
    std::vector<InstalledMod> installed = {
        {"up", Version{1, 0, 0, {}}, "1.0.0", "up_folder"},
        {"same", Version{1, 0, 0, {}}, "1.0.0", "same"},
        {"old", Version{1, 0, 0, {}}, "1.0.0", "old"},
    };
    const ImportScan scan = scan_import(d.path / "pick", installed, env());
    const FoundMod* up = by_id(scan, "up");
    SH_CHECK(up != nullptr && up->status == FoundStatus::Update && up->selected);
    SH_CHECK(up->installed_version == "1.0.0" && up->installed_folder == "up_folder");
    SH_CHECK(by_id(scan, "same")->status == FoundStatus::Same && !by_id(scan, "same")->selected);
    SH_CHECK(by_id(scan, "old")->status == FoundStatus::Older && !by_id(scan, "old")->selected);
    const auto twice = std::find_if(scan.mods.begin(), scan.mods.end(),
                                    [](const FoundMod& m) { return m.location == "zz_twice"; });
    SH_CHECK(twice->status == FoundStatus::Invalid && twice->reason == InvalidReason::Duplicate);
    const FoundMod* dep = by_id(scan, "dep");
    SH_CHECK(dep->selected);  // still importable
    SH_CHECK(dep->notes.size() == 1 && dep->notes[0].kind == "dependency" && dep->notes[0].detail == "lib >=2.0");
    const FoundMod* eng = by_id(scan, "eng");
    SH_CHECK(eng->notes.size() == 1 && eng->notes[0].kind == "engine");
}

SH_TEST(mod_import_zip_layouts) {
    Dir d("zip");
    // One top folder with several mods in it, as tools/ci/package_mods.sh makes it.
    make_zip(d.path / "pack.zip", {{"mods/", ""},
                                   {"mods/README.md", "x"},
                                   {"mods/a/mod.json", manifest("a")},
                                   {"mods/a/text/en/x.json", "{}"},
                                   {"mods/b/mod.json", manifest("b")},
                                   {"__MACOSX/mods/a/mod.json", manifest("fork")}});
    ImportScan scan = scan_import(d.path / "pack.zip", {}, env());
    SH_CHECK(scan.zip && scan.error == ImportError::None);
    SH_CHECK(scan.mods.size() == 2);
    SH_CHECK(by_id(scan, "a")->location == "mods/a" && by_id(scan, "a")->files == 2);

    // A mod at the root of the archive, '\' separators of a Windows tool.
    make_zip(d.path / "root.zip", {{"mod.json", manifest("rooted")}, {"scripts\\main.lua", "--"}});
    scan = scan_import(d.path / "root.zip", {}, env());
    SH_CHECK(scan.mods.size() == 1 && scan.mods[0].location.empty() && scan.mods[0].files == 2);

    make_zip(d.path / "empty.zip", {{"readme.txt", "no mods"}});
    SH_CHECK(scan_import(d.path / "empty.zip", {}, env()).error == ImportError::NoMods);

    d.write("garbage.zip", "this is not an archive at all");
    SH_CHECK(scan_import(d.path / "garbage.zip", {}, env()).error == ImportError::BrokenZip);
    SH_CHECK(scan_import(d.path / "missing.zip", {}, env()).error == ImportError::NotFound);
}

SH_TEST(mod_import_zip_rejects_traversal_and_links) {
    Dir d("unsafe");
    make_zip(d.path / "dots.zip", {{"m/mod.json", manifest("m")}, {"m/../../evil.txt", "x"}});
    ImportScan scan = scan_import(d.path / "dots.zip", {}, env());
    SH_CHECK(scan.mods.size() == 1 && scan.mods[0].status == FoundStatus::Invalid);
    SH_CHECK(scan.mods[0].reason == InvalidReason::UnsafePath);

    make_zip(d.path / "abs.zip", {{"m/mod.json", manifest("m")}, {"_etc/evil", "x"}}, {}, {{"_etc/evil", "/etc/evil"}});
    SH_CHECK(scan_import(d.path / "abs.zip", {}, env()).mods[0].reason == InvalidReason::UnsafePath);

    make_zip(d.path / "drive.zip", {{"m/mod.json", manifest("m")}, {"m/C:evil", "x"}});
    SH_CHECK(scan_import(d.path / "drive.zip", {}, env()).mods[0].reason == InvalidReason::UnsafePath);

    make_zip(d.path / "link.zip", {{"m/mod.json", manifest("m")}, {"m/link", "/etc/passwd"}}, {"m/link"});
    scan = scan_import(d.path / "link.zip", {}, env());
    SH_CHECK(scan.mods[0].reason == InvalidReason::Link && !scan.mods[0].selected);
    SH_CHECK(!install_import(scan, d.path / "mods"));  // nothing chosen
    SH_CHECK(!fs::exists(d.path / "mods" / "m"));
}

SH_TEST(mod_import_install_from_zip_and_folder) {
    Dir d("install");
    make_zip(d.path / "pack.zip", {{"top/a/mod.json", manifest("a")},
                                   {"top/a/text/en/x.json", "{\"k\": 1}"},
                                   {"top/a/.DS_Store", "junk"},
                                   {"top/b/mod.json", manifest("b")}});
    const fs::path mods = d.path / "mods";  // made by the import
    ImportScan scan = scan_import(d.path / "pack.zip", {}, env());
    sh::Result<ImportReport> done = install_import(scan, mods);
    SH_CHECK(done);
    SH_CHECK(done.value().added.size() == 2);
    SH_CHECK(read(mods / "a" / "text" / "en" / "x.json") == "{\"k\": 1}");
    SH_CHECK(!fs::exists(mods / "a" / ".DS_Store"));
    SH_CHECK(fs::exists(mods / "b" / "mod.json"));
    // No staging folder is left behind.
    for (const auto& e : fs::directory_iterator(mods)) {
        SH_CHECK(!e.path().filename().string().starts_with("."));
    }

    // An update from a folder replaces the installed copy whole, in its own folder.
    d.write("mods/a/stale.txt", "old");
    d.write("pick/mod.json", manifest("a", "2.0.0"));
    d.write("pick/new.txt", "new");
    scan = scan_import(d.path / "pick", {{"a", Version{1, 0, 0, {}}, "1.0.0", "a"}}, env());
    SH_CHECK(scan.mods[0].status == FoundStatus::Update);
    done = install_import(scan, mods);
    SH_CHECK(done && done.value().replaced.size() == 1);
    SH_CHECK(fs::exists(mods / "a" / "new.txt") && !fs::exists(mods / "a" / "stale.txt"));

    // An unrelated folder with the new mod's name is left alone.
    d.write("mods/c/notes.txt", "mine");
    d.write("pick2/mod.json", manifest("c"));
    done = install_import(scan_import(d.path / "pick2", {}, env()), mods);
    SH_CHECK(done && read(mods / "c" / "notes.txt") == "mine" && fs::exists(mods / "c-2" / "mod.json"));
}

SH_TEST(mod_import_failure_leaves_mods_folder_as_it_was) {
    Dir d("atomic");
    d.write("mods/a/mod.json", manifest("a"));
    d.write("mods/a/keep.txt", "keep");
    d.write("pick/a/mod.json", manifest("a", "2.0.0"));
    d.write("pick/b/mod.json", manifest("b"));
    ImportScan scan = scan_import(d.path / "pick", {{"a", Version{1, 0, 0, {}}, "1.0.0", "a"}}, env());
    SH_CHECK(scan.mods.size() == 2);
    fs::remove_all(d.path / "pick" / "b");  // gone between the scan and the import
    SH_CHECK(!install_import(scan, d.path / "mods"));
    SH_CHECK(read(d.path / "mods" / "a" / "keep.txt") == "keep");
    SH_CHECK(!fs::exists(d.path / "mods" / "b"));
    std::size_t entries = 0;
    for (const auto& e : fs::directory_iterator(d.path / "mods")) {
        (void)e;
        ++entries;
    }
    SH_CHECK(entries == 1);
}

SH_TEST(mod_import_console_command_and_session) {
    Dir d("console");
    d.write("mods/old/mod.json", manifest("old"));
    make_zip(d.path / "pack.zip", {{"mods/new_one/mod.json", manifest("new_one")}});
    Registry reg;
    RegistryConfig cfg;
    cfg.dirs = {d.path / "mods"};
    cfg.base_dir = d.path / "gamedata";
    reg.configure(cfg);
    set_admin_registry(&reg);
    sh::console::Registry console;
    register_console_commands(console);
    struct Lines : sh::console::Output {
        std::vector<std::string> lines;
        void print(sh::console::Level, std::string_view line) override { lines.emplace_back(line); }
    } out;
    SH_CHECK(console.execute("modimport " + (d.path / "pack.zip").string(), out));
    SH_CHECK(!fs::exists(d.path / "mods" / "new_one"));
    SH_CHECK(console.execute("modimport " + (d.path / "pack.zip").string() + " install", out));
    SH_CHECK(fs::exists(d.path / "mods" / "new_one" / "mod.json"));
    // Listed at once, switched off like every new mod.
    const ResolvedMod* m = reg.pending().find("new_one");
    SH_CHECK(m != nullptr && m->state == ModState::Disabled);
    SH_CHECK(import_session().state() == ImportState::Idle);
    SH_CHECK(import_session().last_report().has_value());

    // A drop is taken only while the import page polls.
    import_session().clear();
    SH_CHECK(import_session().poll());
    SH_CHECK(import_session().offer_drop((d.path / "pack.zip").string()));
    SH_CHECK(import_session().poll());
    SH_CHECK(import_session().state() == ImportState::Ready);
    SH_CHECK(import_session().result().mods.size() == 1);
    SH_CHECK(import_session().result().mods[0].status == FoundStatus::Same);
    import_session().clear();
}
