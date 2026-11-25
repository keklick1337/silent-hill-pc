#include "engine/vfs/vfs_c.h"

#include "engine/assets/anim_layer.h"
#include "engine/maps/unified_layer.h"
#include "engine/mods/data_merge.h"
#include "engine/mods/registry.h"
#include "engine/vfs/disc_sectors.h"
#include "engine/vfs/game_files_layer.h"
#include "engine/vfs/vfs.h"
#include "formats/convert/gamefiles/game_files.h"
#include "formats/convert/level/map_unified_files.h"
#include "formats/disc/disc_image.h"
#include "engine/platform/disc.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct State {
    sh::vfs::Vfs vfs;
    std::unique_ptr<sh::vfs::DiscSectors> sectors;
    // gamedata/raw/ and convert/files/ (the import's own copies of the game files, never a mod), owned by vfs
    std::vector<const sh::vfs::Layer*> base_layers;
    std::string gamedata_dir;  // converted assets: the mods first (engine/mods/registry.h), then <gamedata>/convert
    bool active = false;
    bool initialized = false;

    // Check mode (SH_VFS_VERIFY_IMAGE).
    std::unique_ptr<sh::disc::Image> verify_image;
    std::FILE* verify_out = stderr;  // SH_VFS_VERIFY_LOG, or stderr
    std::atomic<std::uint64_t> verified{0};
    std::atomic<std::uint64_t> verified_data{0};
    std::atomic<std::uint64_t> verified_xa{0};
    std::atomic<std::uint64_t> mismatched{0};
    std::atomic<std::uint64_t> unresolved{0};
};

State& state() {
    static State s;
    return s;
}

std::string config_value(const char* config_path, std::string_view key) {
    if (!config_path) {
        return {};
    }
    std::ifstream in(config_path);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        auto trim = [](std::string s) {
            const char* ws = " \t\r";
            s.erase(0, s.find_first_not_of(ws));
            const std::size_t last = s.find_last_not_of(ws);
            s.erase(last == std::string::npos ? 0 : last + 1);
            return s;
        };
        if (trim(line.substr(0, eq)) == key) {
            std::string value = trim(line.substr(eq + 1));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            return value;
        }
    }
    return {};
}

void log_maps(int level, const std::string& message) {
    std::fprintf(stderr, "%s%s\n", level ? "warning: " : "", message.c_str());
}

// map_format: "unified" reads BG/*.IPD and BG/*.PLM from
// gamedata/maps/areas/*.glb (and a mod's maps/areas/), "legacy" from the game files (and a mod's BG/).
bool unified_maps(const char* config_path) {
    const std::string value = config_value(config_path, "map_format");
    if (value == "legacy") {
        return false;
    }
    if (!value.empty() && value != "unified") {
        std::fprintf(stderr, "warning: [MAPS] map_format = %s is not valid (unified or legacy); using unified\n",
                     value.c_str());
    }
    return true;
}

// anim_source: "json" reads ANIM/*.ANM from convert/anim/**/*.anim.json (lossless, engine/assets/anim_layer.h),
// "disc" from the game files (and a mod's ANIM/). The bytes are the same.
bool json_anims(const char* config_path) {
    const std::string value = config_value(config_path, "anim_source");
    if (value == "disc") {
        return false;
    }
    if (!value.empty() && value != "json") {
        std::fprintf(stderr, "warning: [ANIM] anim_source = %s is not valid (json or disc); using json\n",
                     value.c_str());
    }
    return true;
}

// The mod registry (engine/mods/registry.h) of this run: <exe>/mods, then the config's mods_dir.
void configure_mods(const fs::path& exe, const fs::path& gamedata, const char* config_path) {
    sh::mods::Registry& reg = sh::mods::registry();
    if (reg.configured()) {
        return;
    }
    sh::mods::RegistryConfig config;
    config.dirs.push_back(exe / "mods");
    if (const std::string extra = config_value(config_path, "mods_dir"); !extra.empty()) {
        const fs::path p{extra};
        config.dirs.push_back(p.is_absolute() ? p : exe / p);
    }
    config.base_dir = gamedata;
    // Mod folders of earlier layouts; they only produce the one-time migration notice.
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    std::set<fs::path> legacy;
    for (const fs::path& root : {gamedata, cwd / "gamedata", exe / "gamedata"}) {
        legacy.insert(fs::weakly_canonical(root / "load", ec));
        legacy.insert(fs::weakly_canonical(root / "texturemods", ec));
    }
    legacy.insert(fs::weakly_canonical(exe / "data" / "mods", ec));
    config.legacy_dirs.assign(legacy.begin(), legacy.end());
    reg.configure(std::move(config));
    for (const std::string& line : reg.report(false)) {
        std::fprintf(stderr, "[MODS] %s\n", line.c_str());
    }
    for (const std::string& notice : reg.take_new_notices()) {
        std::fprintf(stderr, "warning: [MODS] %s\n", notice.c_str());
    }
}

void add_mod_layers(State& s, bool unified, bool anims) {
    const std::vector<const sh::mods::ResolvedMod*> active = sh::mods::registry().active();
    // Load order is precedence: a later mod gets a higher layer priority. Every base layer is below 0.
    for (const sh::mods::ResolvedMod* mod : active) {
        const std::string& id = mod->candidate.manifest.id;
        const fs::path& root = mod->candidate.root;
        const int priority = mod->load_index;
        s.vfs.add_layer(sh::vfs::make_directory_layer(root, "mod: " + id), priority);
        // Equal priorities keep insertion order: the mod's own BG/ files win over its maps/areas/.
        if (unified) {
            if (auto maps = sh::maps::make_unified_maps_layer(root, "mod maps: " + id, log_maps)) {
                s.vfs.add_layer(std::move(maps), priority);
            }
        }
        // The sector tail comes from the game files, which only the base layers see; a mod's file has zeros there.
        if (anims) {
            if (auto anim = sh::assets::make_anim_json_layer(root, "mod anims: " + id, nullptr, log_maps)) {
                s.vfs.add_layer(std::move(anim), priority);
            }
        }
    }
}

void verify_report(const char* tag) {
    State& s = state();
    std::fprintf(state().verify_out, "[VFS/VERIFY] %s%llu sectors equal to the image (data %llu, xa %llu), %llu differ, %llu outside "
                "gamedata\n",
                tag, static_cast<unsigned long long>(s.verified.load()),
                static_cast<unsigned long long>(s.verified_data.load()),
                static_cast<unsigned long long>(s.verified_xa.load()),
                static_cast<unsigned long long>(s.mismatched.load()),
                static_cast<unsigned long long>(s.unresolved.load()));
    std::fflush(s.verify_out);
}

bool read_gamedata_sector(int lba, sh::disc::SectorKind kind, void* dst) {
    State& s = state();
    const auto k = kind == sh::disc::SectorKind::Xa ? sh::vfs::SectorKind::Xa : sh::vfs::SectorKind::Data;
    auto* out = static_cast<std::uint8_t*>(dst);
    const bool ok = lba >= 0 && s.sectors->read(static_cast<std::uint32_t>(lba), k, out);

    if (s.verify_image) {
        std::uint8_t ref[sh::vfs::kXaSectorBytes];
        sh::vfs::read_image_sector(*s.verify_image, static_cast<std::uint32_t>(lba), k, ref);
        const sh::vfs::SectorRun* run = s.sectors->run_at(static_cast<std::uint32_t>(lba));
        if (!ok) {
            const std::uint64_t n = ++s.unresolved;
            if (n <= 32) {
                std::fprintf(state().verify_out, "[VFS/VERIFY] lba %d (%s) not in gamedata%s\n", lba, k == sh::vfs::SectorKind::Xa ? "xa" : "data",
                            run ? (", in " + run->path).c_str() : "");
            }
        }
        if (std::memcmp(out, ref, sh::vfs::sector_bytes(k)) != 0) {
            const std::uint64_t n = ++s.mismatched;
            if (n <= 32) {
                std::fprintf(state().verify_out, "[VFS/VERIFY] MISMATCH lba %d (%s) %s\n", lba, k == sh::vfs::SectorKind::Xa ? "xa" : "data",
                            run ? run->path.c_str() : "(no file)");
            }
        } else {
            ++s.verified;
            ++(k == sh::vfs::SectorKind::Xa ? s.verified_xa : s.verified_data);
        }
        const std::uint64_t total = s.verified + s.mismatched;
        if (total % 256 == 0) {
            verify_report("");
        }
    }
    return ok;
}

// The disc reader's sectors come from gamedata/ (and the mods) by LBA.
class GamedataSectors final : public sh::disc::SectorSource {
public:
    int sector_count() override { return static_cast<int>(state().sectors->sector_count()); }
    bool read_sector(int lba, sh::disc::SectorKind kind, void* dst) override { return read_gamedata_sector(lba, kind, dst); }
};

void verify_summary() {
    verify_report("final: ");
}

}  // namespace

extern "C" int ShVfs_InitGame(const char* gamedata_dir, const char* exe_dir, const char* config_path) {
    State& s = state();
    if (s.initialized) {
        return s.active ? 1 : 0;
    }
    s.initialized = true;

    const fs::path exe{exe_dir && exe_dir[0] ? exe_dir : "."};
    const fs::path gamedata{gamedata_dir ? gamedata_dir : ""};
    s.gamedata_dir = gamedata.string();

    configure_mods(exe, gamedata, config_path);
    const bool unified = unified_maps(config_path);
    const bool anims = json_anims(config_path);
    add_mod_layers(s, unified, anims);

    std::string error;
    std::unique_ptr<sh::vfs::Layer> raw = sh::vfs::make_gamedata_layer(gamedata, error);
    if (raw) {
        std::error_code ec;
        const bool have_raw = sh::convert::gamefiles::has_raw(gamedata);
        if (have_raw && unified && !fs::exists(gamedata / "maps" / "areas", ec)) {
            // An import made before the unified map files: write them once from raw/. No texture bake here:
            // such an import predates importer 3, its textures/NAME.png is the indexed source itself (no
            // indexed/), and baking would overwrite it. The new glbs name NAME.pNN.png, which the texture
            // reader builds on demand (png::read_texture_or_variant); first run re-imports when it can.
            const sh::convert::level::UnifiedReport made = sh::convert::level::generate_unified_maps(gamedata, false);
            for (const std::string& w : made.warnings) {
                std::fprintf(stderr, "warning: [MAPS] %s\n", w.c_str());
            }
            std::fprintf(stderr, "[MAPS] wrote maps/areas: %s\n",
                         sh::convert::level::format_unified_line(made).c_str());
        }
        if (have_raw && anims && !fs::exists(gamedata / "convert" / "anim", ec)) {
            // An import made before the anim converter: write convert/anim once from raw/.
            const int made = sh::assets::write_anim_json_from_raw(gamedata, *raw, log_maps);
            std::fprintf(stderr, "[ANIM] wrote convert/anim: %d files\n", made);
        }
        if (have_raw) {
            // An import made before the game files store, or one that keeps raw/: the store (built once), then raw/
            // goes unless keep_raw = 1. The game reads only the store and the converted files from here on.
            const bool keep_raw = config_value(config_path, "keep_raw") == "1";
            const auto finished = sh::convert::gamefiles::finish_import(gamedata, keep_raw, 0);
            if (!finished) {
                std::fprintf(stderr, "warning: [VFS] raw/ is kept: %s\n", finished.error().c_str());
            } else {
                if (finished.value().built) {
                    std::fprintf(stderr, "[VFS] wrote %s: %s\n", sh::convert::gamefiles::kStoreDir,
                                 sh::convert::gamefiles::format_build_line(finished.value().build).c_str());
                    for (const std::string& note : finished.value().build.notes) {
                        std::fprintf(stderr, "warning: [VFS] %s\n", note.c_str());
                    }
                }
                if (finished.value().removed) {
                    std::fprintf(stderr,
                                 "[VFS] deleted %s (%llu MB): the game files come from %s and the converted "
                                 "data (keep_raw = 1 keeps it)\n",
                                 (gamedata / "raw").string().c_str(),
                                 static_cast<unsigned long long>(finished.value().freed_bytes >> 20),
                                 sh::convert::gamefiles::kStoreDir);
                }
                if (finished.value().unread.files != 0) {
                    std::fprintf(stderr,
                                 "[VFS] deleted %zu WAVs of the movies' sound (%llu MB): the movies play it "
                                 "from their own sectors\n",
                                 finished.value().unread.files,
                                 static_cast<unsigned long long>(finished.value().unread.bytes >> 20));
                }
            }
        }

        std::string store_error;
        std::unique_ptr<sh::vfs::Layer> files = sh::vfs::make_game_files_layer(gamedata, log_maps, store_error);
        if (!files) {
            std::fprintf(stderr, "[VFS] %s: the game files come from raw/\n", store_error.c_str());
        }
        if (unified) {
            if (std::unique_ptr<sh::vfs::Layer> maps = sh::maps::make_unified_maps_layer(gamedata, "maps", log_maps)) {
                // Below every mod (their layer priority is the load index, 0 and up), above the game files.
                s.vfs.add_layer(std::move(maps), INT_MIN + 2);
            } else {
                std::fprintf(stderr, "[MAPS] no unified map files in %s: the map files come from %s\n",
                             (gamedata / "maps" / "areas").string().c_str(), files ? "convert/files" : "raw/");
            }
        }
        if (anims) {
            // The sector tail of an animation comes from the layer of the game files.
            const sh::vfs::Layer* tails = files                                       ? files.get()
                                          : sh::convert::gamefiles::has_raw(gamedata) ? raw.get()
                                                                                      : nullptr;
            if (auto anim = sh::assets::make_anim_json_layer(gamedata, "anims", tails, log_maps)) {
                // Like the unified maps: below every mod, above the game files.
                s.vfs.add_layer(std::move(anim), INT_MIN + 2);
            } else {
                std::fprintf(stderr, "[ANIM] no convert/anim in %s: the animations come from the game files\n",
                             s.gamedata_dir.c_str());
            }
        }
        if (files) {
            s.base_layers.push_back(files.get());
            s.vfs.add_layer(std::move(files), INT_MIN + 1);
        }
        if (sh::convert::gamefiles::has_raw(gamedata)) {
            s.base_layers.push_back(raw.get());
            s.vfs.add_layer(std::move(raw), INT_MIN);
        }
        s.sectors = sh::vfs::DiscSectors::open(s.vfs, gamedata, error);
    }
    if (!s.sectors) {
        std::fprintf(stderr, "[VFS] gamedata not used (%s): reading the disc image\n", error.c_str());
        return 0;
    }

    if (const char* verify = std::getenv("SH_VFS_VERIFY_IMAGE"); verify && verify[0]) {
        s.verify_image = sh::disc::Image::open(verify);
        // The game points stderr at its own log, which it also rewrites; a separate file keeps the lines whole.
        if (const char* out = std::getenv("SH_VFS_VERIFY_LOG"); out && out[0]) {
            if (std::FILE* f = std::fopen(out, "a")) {
                s.verify_out = f;
            }
        }
        std::fprintf(state().verify_out, "[VFS/VERIFY] check mode against %s: %s\n", verify, s.verify_image ? "open" : "cannot open");
        if (s.verify_image) {
            std::atexit(verify_summary);
        }
    }

    static GamedataSectors source;
    sh::disc::set_source(&source);
    s.active = true;

    std::fprintf(stderr, "[VFS] gamedata %s: %zu files by LBA, %u sectors; layers:", s.gamedata_dir.c_str(),
                s.sectors->runs().size(), s.sectors->sector_count());
    for (const std::string& name : s.vfs.layer_names()) {
        std::fprintf(stderr, " [%s]", name.c_str());
    }
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    return 1;
}

extern "C" int ShVfs_GameDataActive(void) {
    return state().active ? 1 : 0;
}

extern "C" const char* ShVfs_GameDataDir(void) {
    return state().gamedata_dir.c_str();
}

extern "C" unsigned char* ShVfs_ReadFile(const char* path, size_t* out_size) {
    if (!path) {
        return nullptr;
    }
    const std::optional<std::vector<std::uint8_t>> data = state().vfs.read(path);
    if (!data) {
        return nullptr;
    }
    auto* buf = static_cast<unsigned char*>(std::malloc(data->empty() ? 1 : data->size()));
    if (!buf) {
        return nullptr;
    }
    if (!data->empty()) {
        std::memcpy(buf, data->data(), data->size());
    }
    if (out_size) {
        *out_size = data->size();
    }
    return buf;
}

namespace {

int find_in_roots(const char* subdir, const char* relative, char* out, size_t out_size, bool mods_only) {
    if (!relative || !out || out_size == 0) {
        return 0;
    }
    const std::string rel = *subdir ? std::string(subdir) + "/" + relative : std::string(relative);
    std::optional<fs::path> found = sh::mods::registry().find(rel);
    if (!found && !mods_only && !state().gamedata_dir.empty()) {
        std::error_code ec;
        const fs::path base = fs::path(state().gamedata_dir) / fs::path(rel);
        if (fs::is_regular_file(base, ec)) {
            found = base;
        }
    }
    if (!found) {
        return 0;
    }
    const std::string host = found->string();
    if (host.size() + 1 > out_size) {
        return 0;
    }
    std::memcpy(out, host.c_str(), host.size() + 1);
    return 1;
}

}  // namespace

extern "C" int ShVfs_FindConverted(const char* relative, char* out, size_t out_size) {
    return find_in_roots("convert", relative, out, out_size, false);
}

extern "C" int ShVfs_FindModConverted(const char* relative, char* out, size_t out_size) {
    return find_in_roots("convert", relative, out, out_size, true);
}

extern "C" int ShVfs_FindDataFile(const char* relative, char* out, size_t out_size) {
    return find_in_roots("", relative, out, out_size, false);
}

extern "C" int ShVfs_FindModDataFile(const char* relative, char* out, size_t out_size) {
    return find_in_roots("", relative, out, out_size, true);
}

extern "C" char* ShVfs_ReadDataJson(const char* relative, size_t* out_size) {
    if (!relative) {
        return nullptr;
    }
    std::vector<std::string> warnings;
    const fs::path base = state().gamedata_dir.empty() ? fs::path() : fs::path(state().gamedata_dir);
    sh::Result<sh::mods::MergedFile> merged = sh::mods::read_registry_data_file(relative, base, warnings);
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "warning: [MODS] %s\n", w.c_str());
    }
    if (!merged) {
        // A missing file is the caller's business (most such files are optional).
        if (!merged.error().starts_with("no file")) {
            std::fprintf(stderr, "warning: [MODS] %s: %s\n", relative, merged.error().c_str());
        }
        return nullptr;
    }
    const std::string& text = merged.value().text;
    auto* buf = static_cast<char*>(std::malloc(text.size() + 1));
    if (!buf) {
        return nullptr;
    }
    std::memcpy(buf, text.data(), text.size());
    buf[text.size()] = '\0';
    if (out_size) {
        *out_size = text.size();
    }
    return buf;
}

extern "C" int ShVfs_PathAtLba(int lba, char* out, size_t out_size) {
    State& s = state();
    if (!s.sectors || lba < 0 || !out || out_size == 0) {
        return 0;
    }
    const sh::vfs::SectorRun* run = s.sectors->run_at(static_cast<std::uint32_t>(lba));
    if (!run || run->lba != static_cast<std::uint32_t>(lba) || run->path.size() + 1 > out_size) {
        return 0;
    }
    std::memcpy(out, run->path.c_str(), run->path.size() + 1);
    return 1;
}

extern "C" int ShVfs_ModFilePath(const char* path, char* out, size_t out_size) {
    if (!path || !out || out_size == 0) {
        return 0;
    }
    const sh::vfs::Layer* layer = state().vfs.find(path);
    const std::vector<const sh::vfs::Layer*>& base = state().base_layers;
    if (!layer || std::find(base.begin(), base.end(), layer) != base.end()) {
        return 0;
    }
    const std::string host = layer->host_path(path).string();
    if (host.empty() || host.size() + 1 > out_size) {
        return 0;
    }
    std::memcpy(out, host.c_str(), host.size() + 1);
    return 1;
}
