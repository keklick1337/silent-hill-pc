#include "engine/app/dialog.h"
#include "engine/app/first_run/first_run.h"
#include "engine/app/first_run/gamedata_entries.h"

#include "engine/app/first_run/picked_document.h"
#include "engine/app/first_run/progress_window.h"
#include "formats/convert/level/map_unified_files.h"
#include "formats/convert/filetable/file_table.h"
#include "formats/convert/mapdata/mapdata_files.h"
#include "formats/convert/materials/materials_manifest.h"
#include "formats/convert/materials/surface_inference.h"
#include "formats/convert/materials/texture_bake.h"
#include "import/import.h"
#include "import/ui/controller.h"
#include "import/ui/input_check.h"
#include "import/ui/picker.h"
#include "import/ui/strings.h"
#include "sh-import/converters.h"
#include "sh-import/game_files.h"
#include "sh-import/options.h"
#include "sh-import/progress.h"
#include "sh-import/tables.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <functional>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace ui = sh::import::ui;

namespace {

std::atomic<bool> g_interrupted{false};

extern "C" void on_interrupt(int) { g_interrupted.store(true); }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

fs::path from_u8(std::string_view s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string u8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// Value of `key` in a "key = value" file; empty when absent.
std::string read_key(const fs::path& file, std::string_view key) {
    std::string value;
    const std::string text = read_file(file);
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string_view line = trim(std::string_view(text).substr(pos, end - pos));
        pos = end + 1;
        const std::size_t eq = line.find('=');
        if (eq != std::string_view::npos && lower(trim(line.substr(0, eq))) == key) {
            value = std::string(trim(line.substr(eq + 1)));  // the last one wins, as in apply_config
        }
    }
    return value;
}

// The lines of config.cfg that belong to the importer: gamedata_dir and import_*. The game's own keys are not passed
// on, or apply_config would warn about each of them.
std::string import_lines(const std::string& text, bool& has_gamedata_dir) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string_view raw = std::string_view(text).substr(pos, end - pos);
        pos = end + 1;
        const std::string_view line = trim(raw);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            out += '\n';  // keeps the line numbers of apply_config's messages
            continue;
        }
        const std::size_t eq = line.find('=');
        const std::string key = eq == std::string_view::npos ? std::string() : lower(trim(line.substr(0, eq)));
        if (key == "gamedata_dir" || key.rfind("import_", 0) == 0) {
            has_gamedata_dir = has_gamedata_dir || key == "gamedata_dir";
            out += raw;
        }
        out += '\n';
    }
    return out;
}

void report(bool error, const std::string& title, const std::string& text) {
    ShDialog_Show(error ? 1 : 0, title.c_str(), text.c_str());
}

bool has_ext(const fs::path& p, std::string_view ext) { return lower(u8(p.extension())) == ext; }

bool is_disc_image(const fs::path& p) {
    return has_ext(p, ".bin") || has_ext(p, ".cue") || has_ext(p, ".iso") || has_ext(p, ".img");
}

// Images in one directory: .cue first (it names its .bin), then .bin, then .iso, then .zip; by name inside a group.
std::vector<fs::path> images_in(const fs::path& dir) {
    std::vector<fs::path> groups[4];
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) {
            continue;
        }
        const fs::path& p = it->path();
        if (has_ext(p, ".cue")) {
            groups[0].push_back(p);
        } else if (has_ext(p, ".bin")) {
            groups[1].push_back(p);
        } else if (has_ext(p, ".iso")) {
            groups[2].push_back(p);
        } else if (has_ext(p, ".zip")) {
            groups[3].push_back(p);
        }
    }
    std::vector<fs::path> all;
    for (auto& g : groups) {
        std::sort(g.begin(), g.end());
        all.insert(all.end(), g.begin(), g.end());
    }
    return all;
}

struct ImageChoice {
    std::optional<ui::ImageCheck> image;
    std::string error;  // set when an image named by --image or import_image is unusable
};

// --image, then import_image, then a search of `dirs`.
ImageChoice choose_image(const fs::path& arg, const fs::path& config_image, const std::vector<fs::path>& dirs,
                         ui::Lang lang) {
    ImageChoice choice;
    for (const fs::path& named : {arg, config_image}) {
        if (named.empty()) {
            continue;
        }
        ui::ImageCheck check = ui::check_image(u8(named));
        if (check.ok()) {
            choice.image = std::move(check);
        } else {
            choice.error = ui::describe(lang, check);
        }
        return choice;
    }
    for (const fs::path& dir : dirs) {
        for (const fs::path& candidate : images_in(dir)) {
            ui::ImageCheck check = ui::check_image(u8(candidate));
            if (check.ok()) {
                choice.image = std::move(check);
                return choice;
            }
        }
    }
    return choice;
}

std::string pid_text() {
#ifdef _WIN32
    return std::to_string(_getpid());
#else
    return std::to_string(getpid());
#endif
}

fs::path unused_sibling(const fs::path& target, const std::string& suffix) {
    fs::path p = target;
    p += suffix;
    std::error_code ec;
    for (int n = 2; fs::exists(fs::symlink_status(p, ec)); ++n) {
        p = target;
        p += suffix + "-" + std::to_string(n);
    }
    return p;
}

using sh::first_run::is_import_output;
using sh::first_run::is_old_port_asset;

// The shipped base data inside maps/ (handlers.json, events.lua, world_objects.json, spawn_overrides.json
// and whatever a map adds) moves aside with the stale import outputs because the whole maps/
// directory is one import output. After a successful import the importer has rewritten its own files,
// so every file it did not write is copied back.
void restore_shipped_map_data(const fs::path& legacy_maps, const fs::path& gamedata_maps) {
    std::error_code ec;
    if (!fs::exists(legacy_maps, ec)) {
        return;
    }
    for (fs::recursive_directory_iterator it(legacy_maps, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const fs::path relative = fs::relative(it->path(), legacy_maps, ec);
        const fs::path target = gamedata_maps / relative;
        if (ec || fs::exists(target, ec)) {
            ec.clear();
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        fs::copy_file(it->path(), target, ec);
        ec.clear();
    }
}

template <typename Pred>
void move_entries(const fs::path& from, const fs::path& to, Pred keep) {
    std::error_code ec;
    std::vector<fs::path> entries;
    for (fs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        if (keep(u8(it->path().filename()), it->path())) {
            entries.push_back(it->path());
        }
    }
    for (const fs::path& e : entries) {
        std::error_code mec;
        fs::create_directories(to, mec);
        fs::rename(e, to / e.filename(), mec);
        if (mec) {
            std::fprintf(stderr, "first run: cannot move %s to %s: %s\n", u8(e).c_str(), u8(to).c_str(),
                         mec.message().c_str());
        }
    }
}

bool dir_is_empty(const fs::path& dir) {
    std::error_code ec;
    return fs::is_empty(dir, ec) && !ec;
}

bool same_bytes(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) {
        return false;
    }
    std::ifstream fa(a, std::ios::binary);
    std::ifstream fb(b, std::ios::binary);
    return fa && fb &&
           std::equal(std::istreambuf_iterator<char>(fa), std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(fb));
}

// After restore_shipped_map_data the moved-aside base files are copies of what gamedata/ holds again; left there,
// every fresh install would keep a .legacy of nothing but duplicates.
void drop_restored_copies(const fs::path& legacy, const fs::path& gamedata_dir) {
    std::error_code ec;
    std::vector<fs::path> files;
    std::vector<fs::path> dirs;
    for (fs::recursive_directory_iterator it(legacy, ec), end; !ec && it != end; it.increment(ec)) {
        (it->is_directory(ec) ? dirs : files).push_back(it->path());
    }
    for (const fs::path& f : files) {
        const fs::path relative = fs::relative(f, legacy, ec);
        if (!ec && same_bytes(f, gamedata_dir / relative)) {
            fs::remove(f, ec);
        }
        ec.clear();
    }
    // Deepest first, so a directory is empty once its children are gone.
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
        return std::distance(a.begin(), a.end()) > std::distance(b.begin(), b.end());
    });
    for (const fs::path& d : dirs) {
        if (dir_is_empty(d)) {
            fs::remove(d, ec);
        }
    }
}

// The importer writes only the image name; the readers that take the disc directly need the full path, so it is
// kept next to it.
void record_image_path(const fs::path& gamedata, const fs::path& image) {
    std::ofstream out(gamedata / "version.cfg", std::ios::binary | std::ios::app);
    out << "image_path = " << u8(image) << "\n";
}

// import_image = <path> in config.cfg: every uncommented line of the key is rewritten, or one is added at the end.
void remember_image(const fs::path& config_path, const fs::path& image) {
    const std::string line = "import_image = " + u8(image);
    std::string text = read_file(config_path);
    std::string out;
    bool found = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        const std::size_t next = end == std::string::npos ? text.size() : end + 1;
        std::string_view current(text.data() + pos, (end == std::string::npos ? text.size() : end) - pos);
        const std::string_view t = trim(current);
        const std::size_t eq = t.find('=');
        if (eq != std::string_view::npos && trim(t.substr(0, eq)) == "import_image") {
            out += line;
            out += current.ends_with('\r') ? "\r\n" : "\n";
            found = true;
        } else {
            out.append(text, pos, next - pos);
        }
        pos = next;
    }
    if (!found) {
        if (!out.empty() && out.back() != '\n') {
            out += '\n';
        }
        out += line + "\n";
    }
    std::ofstream file(config_path, std::ios::binary | std::ios::trunc);
    file << out;
    if (!file) {
        std::fprintf(stderr, "first run: cannot write import_image to %s\n", u8(config_path).c_str());
    }
}

fs::path absolute_from(const fs::path& base, const fs::path& p) {
    std::error_code ec;
    return fs::weakly_canonical(p.is_absolute() ? p : base / p, ec);
}

struct Outcome {
    ui::Phase phase = ui::Phase::Aborted;
    std::string title;
    std::vector<std::string> body;
    std::string message;
};

Outcome drive(ui::Controller& controller, sh::first_run::ProgressWindow* window) {
#ifdef _WIN32
    const bool terminal = _isatty(_fileno(stderr)) != 0;
#else
    const bool terminal = isatty(fileno(stderr)) != 0;
#endif
    sh::import_tool::ProgressPrinter printer(stderr, terminal);
    std::uint64_t seen = ~std::uint64_t{0};
    bool stop_sent = false;
    for (;;) {
        const bool stop = g_interrupted.load() || (window && window->stop_requested());
        if (stop && !stop_sent) {
            stop_sent = true;
            controller.quit();
        }
        const ui::ViewModel v = controller.snapshot();
        if (v.revision != seen) {
            seen = v.revision;
            if (v.has_progress && !v.stage.empty()) {
                printer(v.stage, v.fraction);
            }
        }
        if (window) {
            window->draw(v);
        }
        if (ui::is_final(v.phase)) {
            printer.finish();
            return Outcome{v.phase, v.title, v.body, v.message};
        }
        controller.wait_for_change(v.revision, std::chrono::milliseconds(50));
    }
}

// A step the first start derives from the import, with its share of the time those steps took on a desktop (the
// bar's progress) and the line the window shows while it runs.
struct PrepareStep {
    ui::Str text;
    double weight;
    std::function<void()> run;
};

// Runs the steps after the import. With a window they run on a thread of their own while the window keeps drawing a
// stage of its own (a phone would otherwise take the app for hung); the time of each step goes to stderr.
void run_prepare(const std::vector<PrepareStep>& steps, sh::first_run::ProgressWindow* window, ui::Lang lang,
                 const std::string& title) {
    std::atomic<std::size_t> at{0};
    const auto work = [&] {
        for (const PrepareStep& step : steps) {
            const auto from = std::chrono::steady_clock::now();
            step.run();
            const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
            const std::string name(ui::text(ui::Lang::En, step.text));
            std::fprintf(stderr, "first run: prepare: %s in %.2f s\n", name.c_str(), took);
            at.fetch_add(1);
        }
    };
    if (window == nullptr) {
        work();
        return;
    }
    double total = 0.0;
    for (const PrepareStep& step : steps) total += step.weight;
    std::thread worker(work);
    std::uint64_t revision = 0;
    for (;;) {
        // The events are taken so the window stays alive; the steps cannot be cancelled half-way.
        (void)window->stop_requested();
        const std::size_t done = at.load();
        ui::ViewModel v;
        v.revision = ++revision;
        v.phase = ui::Phase::Importing;
        v.title = title;
        v.has_progress = true;
        v.stage = "prepare";
        v.stage_text = ui::format(lang, ui::Str::StagePrepare);
        double reached = 0.0;
        for (std::size_t i = 0; i < done && i < steps.size(); ++i) reached += steps[i].weight;
        v.fraction = total > 0.0 ? reached / total : 1.0;
        v.percent = static_cast<int>(v.fraction * 100.0);
        v.indeterminate = done < steps.size();
        v.eta_text = ui::format(lang, done < steps.size() ? steps[done].text : ui::Str::PrepareStarting);
        v.actions.push_back({ui::ActionId::Cancel, ui::format(lang, ui::Str::ActionCancel), false});
        window->draw(v);
        if (done >= steps.size()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    worker.join();
}

struct ImportContext {
    ui::Lang lang;
    fs::path exe_dir;
    fs::path gamedata;
    bool ready;
    sh::import_tool::Options options;
};

// Imports `image` into the gamedata directory and derives the rest from it. With a window a failure is not shown in a
// dialog when `offer_picker`: its text goes to `failure` and the caller shows the picker again.
int import_from(const ImportContext& context, ui::ImageCheck image, sh::first_run::ProgressWindow* window,
                bool offer_picker, std::string& failure, PcFirstRunResult& result) {
    // The image may be a link inside an old gamedata/ that is about to move: keep the file it points to.
    const ui::Lang lang = context.lang;
    const fs::path& exe_dir = context.exe_dir;
    const fs::path& gamedata = context.gamedata;
    const bool ready = context.ready;
    sh::import_tool::Options options = context.options;
    {
        std::error_code ec;
        const fs::path real = fs::weakly_canonical(image.path, ec);
        if (!ec) {
            image.path = real;
        }
    }

    // Prepare the target. A re-import keeps what the importer does not write (saves, mods, base game data)
    // and puts it back. A gamedata/ without version.cfg holds either a stale partial import or the shipped base
    // data: the base entries stay in place while the import runs and nothing is lost on failure; stale
    // import outputs, the files of earlier builds and disc images dropped into gamedata/ move aside to .legacy.
    std::error_code ec;
    fs::path keep_dir;
    fs::path legacy_dir;
    fs::path shipped_maps;  // a re-import replaces maps/ whole: its shipped base files wait here
    if (fs::exists(gamedata, ec) && !dir_is_empty(gamedata)) {
        if (ready) {
            keep_dir = unused_sibling(gamedata, ".keep-" + pid_text());
            move_entries(gamedata, keep_dir, [](const std::string& name, const fs::path&) { return !is_import_output(name); });
            if (fs::exists(gamedata / "maps", ec)) {
                shipped_maps = unused_sibling(gamedata, ".maps-" + pid_text());
                fs::copy(gamedata / "maps", shipped_maps, fs::copy_options::recursive, ec);
                if (ec) {
                    std::fprintf(stderr, "first run: cannot keep the shipped map data of %s: %s\n", u8(gamedata).c_str(),
                                 ec.message().c_str());
                    ec.clear();
                }
            }
        } else {
            keep_dir = unused_sibling(gamedata, ".keep-" + pid_text());
            move_entries(gamedata, keep_dir, [](const std::string& name, const fs::path& p) {
                return !is_import_output(name) && !is_old_port_asset(name) && !is_disc_image(p);
            });
            legacy_dir = unused_sibling(gamedata, ".legacy");
            move_entries(gamedata, legacy_dir, [](const std::string& name, const fs::path& p) {
                return is_import_output(name) || is_old_port_asset(name) || is_disc_image(p);
            });
            for (fs::path* dir : {&keep_dir, &legacy_dir}) {
                if (dir_is_empty(*dir)) {
                    fs::remove(*dir, ec);
                    dir->clear();
                }
            }
            if (!legacy_dir.empty()) {
                std::fprintf(stderr, "first run: %s has no version.cfg; its stale import files are moved aside to %s\n",
                             u8(gamedata).c_str(), u8(legacy_dir).c_str());
                // The image may be a file inside gamedata/ that just moved to legacy_dir: keep reading it there.
                std::error_code cec;
                const fs::path g_real = fs::weakly_canonical(gamedata, cec);
                const fs::path i_real = fs::weakly_canonical(image.path, cec);
                if (!cec && !g_real.empty()) {
                    const auto [o, i] = std::mismatch(g_real.begin(), g_real.end(), i_real.begin(), i_real.end());
                    if (o == g_real.end() && i != i_real.end()) {
                        fs::path rest;
                        for (auto it = i; it != i_real.end(); ++it) {
                            rest /= *it;
                        }
                        std::error_code exc;
                        if (fs::exists(legacy_dir / rest, exc)) {
                            image.path = legacy_dir / rest;
                        }
                    }
                }
            }
        }
    }

    // The .cue may have moved with the old entries: find its .bin again.
    image = ui::check_image(u8(image.path));
    if (!image.ok()) {
        if (offer_picker) {
            failure = ui::describe(lang, image);
        } else {
            report(true, ui::format(lang, ui::Str::ImportDialogTitle), ui::describe(lang, image));
        }
        if (!shipped_maps.empty()) {
            fs::remove_all(shipped_maps, ec);
        }
        if (!legacy_dir.empty()) {
            move_entries(legacy_dir, gamedata, [](const std::string&, const fs::path&) { return true; });
            if (dir_is_empty(legacy_dir)) {
                fs::remove(legacy_dir, ec);
            }
        }
        if (!keep_dir.empty()) {
            move_entries(keep_dir, gamedata, [](const std::string&, const fs::path&) { return true; });
            fs::remove(keep_dir, ec);
        }
        return 1;
    }
    const std::string image_text = u8(image.path);
    std::fprintf(stderr, "first run: importing %s into %s\n", image_text.c_str(), u8(gamedata).c_str());
    std::fflush(stderr);
    if (window) {
        window->set_subject(u8(image.path.filename()));
    }

    ui::Controller::Options copts;
    copts.gamedata_dir = gamedata;
    copts.lang = lang;
    copts.settings.overwrite = ready;  // only an imported directory is replaced; everything else was moved aside
    copts.settings.verify = options.verify;
    copts.settings.threads = options.threads;
    // The base game data waits beside the target while the import runs: the glyph sheets are read from there.
    options.font_dir = !keep_dir.empty() && fs::is_directory(keep_dir / "font", ec) ? keep_dir / "font" : gamedata / "font";
    copts.import = [options, alias = gamedata.parent_path()](const sh::import::Request& request,
                                                            const sh::import::ProgressFn& progress) {
        sh::import_tool::ImageFileReader reader(request.image, alias);
        const auto converters = sh::import_tool::build_converters(options, reader.as_sibling_reader());
        return sh::import::import_image(request, converters, progress);
    };

    const auto started = std::chrono::steady_clock::now();
    Outcome outcome;
    {
        g_interrupted.store(false);
        const auto old_int = std::signal(SIGINT, on_interrupt);
        const auto old_term = std::signal(SIGTERM, on_interrupt);

        ui::Controller controller(copts);
        // Not start(): it reports Ready for an imported directory, and a re-import must run anyway.
        controller.set_text(image_text);
        if (controller.submit()) {
            outcome = drive(controller, window);
        } else {
            const ui::ViewModel v = controller.snapshot();
            outcome = Outcome{ui::Phase::Failed, v.title, {}, v.message};
        }

        std::signal(SIGINT, old_int);
        std::signal(SIGTERM, old_term);
    }
    // Only a failure goes back to the picker; a finished import keeps the window for the steps after it.
    if (window && outcome.phase != ui::Phase::Done && !(offer_picker && outcome.phase == ui::Phase::Failed)) {
        window->close();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    const bool done = outcome.phase == ui::Phase::Done;
    if (!shipped_maps.empty()) {
        if (done) {
            restore_shipped_map_data(shipped_maps, gamedata / "maps");
        }
        fs::remove_all(shipped_maps, ec);
    }
    if (!keep_dir.empty()) {
        move_entries(keep_dir, gamedata, [](const std::string&, const fs::path&) { return true; });
        if (dir_is_empty(keep_dir)) {
            fs::remove(keep_dir, ec);
        }
    }
    if (!legacy_dir.empty()) {
        if (done) {
            // The stale import outputs were replaced and the engine's own files live in assets/; what is left in
            // .legacy (the user's disc images, anything unrecognized) is kept there.
            restore_shipped_map_data(legacy_dir / "maps", gamedata / "maps");
            drop_restored_copies(legacy_dir, gamedata);
            if (dir_is_empty(legacy_dir)) {
                fs::remove(legacy_dir, ec);
            } else {
                std::fprintf(stderr, "first run: the old files are left in %s (the engine's own files are in %s now)\n",
                             u8(legacy_dir).c_str(), u8(exe_dir / "assets").c_str());
            }
        } else {
            move_entries(legacy_dir, gamedata, [](const std::string&, const fs::path&) { return true; });
            if (dir_is_empty(legacy_dir)) {
                fs::remove(legacy_dir, ec);
            }
        }
    }

    if (done) {
        record_image_path(gamedata, image.path);
        // Everything below derives from the raw/ just written. The shipped maps/ data was copied back with the
        // previous import's derivatives (map files, maps/areas/*.glb) still in it, so they are rewritten here:
        // keeping them would hide every converter change a newer importer version brings.
        const bool rewrite = true;
        std::vector<PrepareStep> steps;
        steps.push_back({ui::Str::PrepareTables, 0.02, [&] {
            // The same step as sh-import: gamedata/tables/ from the disc files in raw/ (the game requires them).
            const sh::import_tool::TablesReport tables = sh::import_tool::generate_tables(gamedata, rewrite);
            for (const std::string& w : tables.warnings) {
                std::fprintf(stderr, "first run: tables: %s\n", w.c_str());
            }
            std::fprintf(stderr, "first run: tables: %s\n", sh::import_tool::format_tables_line(tables).c_str());
            if (const sh::Result<bool> ft = sh::convert::filetable::write_from_raw(gamedata, rewrite); !ft) {
                // The same step as sh-import. Not fatal here: the game reports a missing table itself.
                std::fprintf(stderr, "first run: file table: %s\n", ft.error().c_str());
            }
            // The same step as sh-import: gamedata/maps/ from the map overlays in raw/.
            // The shipped handler tables come back first: map.json joins them with the disc data.
            if (!legacy_dir.empty()) {
                restore_shipped_map_data(legacy_dir / "maps", gamedata / "maps");
            }
            const sh::convert::mapdata::GenerateReport maps =
                sh::convert::mapdata::generate_map_files(gamedata, rewrite);
            for (const std::string& w : maps.warnings) {
                std::fprintf(stderr, "first run: maps: %s\n", w.c_str());
            }
            std::fprintf(stderr, "first run: maps: %s\n", sh::convert::mapdata::format_report_line(maps).c_str());
        }});
        if (options.is_enabled(sh::import_tool::Category::Levels)) {
            steps.push_back({ui::Str::PrepareAreas, 0.08, [&] {
                // The same step as sh-import: gamedata/maps/areas/, one .glb per area.
                const sh::convert::level::UnifiedReport areas =
                    sh::convert::level::generate_unified_maps(gamedata, rewrite, options.threads);
                for (const std::string& w : areas.warnings) {
                    std::fprintf(stderr, "first run: map areas: %s\n", w.c_str());
                }
                std::fprintf(stderr, "first run: map areas: %s\n",
                             sh::convert::level::format_unified_line(areas).c_str());
            }});
        }
        if (options.is_enabled(sh::import_tool::Category::Textures)) {
            steps.push_back({ui::Str::PrepareTextures, 0.66, [&] {
                // The same step as sh-import: true-colour textures in the CLUT rows the geometry draws (importer
                // 3); rewrites the glbs above, so it runs after the map areas and before the manifest.
                const auto baked = sh::convert::materials::bake_textures(gamedata, options.threads);
                if (baked) {
                    for (const std::string& w : baked.value().warnings) {
                        std::fprintf(stderr, "first run: textures: %s\n", w.c_str());
                    }
                    std::fprintf(stderr, "first run: %s\n",
                                 sh::convert::materials::format_bake_line(baked.value()).c_str());
                } else {
                    std::fprintf(stderr, "first run: textures: %s\n", baked.error().c_str());
                }
            }});
        }
        steps.push_back({ui::Str::PrepareMaterials, 0.24, [&] {
            // convert/materials.json: which texture PNGs each model/level uses (texture-variant
            // task, PLAN 6.0) — the modder's map of replaceable textures.
            const auto materials = sh::convert::materials::write_manifest(gamedata);
            if (materials) {
                std::fprintf(stderr, "first run: %s\n",
                             sh::convert::materials::format_report_line(materials.value()).c_str());
            } else {
                std::fprintf(stderr, "first run: materials: %s\n", materials.error().c_str());
            }
            // convert/surface_materials.json: the modern lighting's proposed materials of the map pages.
            const auto surfaces = sh::convert::materials::write_surface_inference(gamedata);
            if (surfaces) {
                std::fprintf(stderr, "first run: %s\n",
                             sh::convert::materials::format_inference_line(surfaces.value()).c_str());
            } else {
                std::fprintf(stderr, "first run: surface materials: %s\n", surfaces.error().c_str());
            }
        }});
        run_prepare(steps, window, lang, outcome.title);
        if (window) {
            // The game opens its own window next.
            window->close();
        }
        std::snprintf(result.disc_image, sizeof result.disc_image, "%s", u8(image.bin).c_str());
        std::fprintf(stderr, "first run: imported into %s in %.1f s\n", u8(gamedata).c_str(), seconds);
        std::fflush(stderr);
        return 0;
    }

    std::string text;
    for (const std::string& paragraph : outcome.body) {
        text += paragraph + "\n";
    }
    if (!outcome.message.empty()) {
        text += outcome.message;
    }
    if (outcome.phase == ui::Phase::Failed) {
        if (offer_picker) {
            failure = text;
        } else {
            report(true, "Silent Hill - " + outcome.title, text);
        }
        return 1;
    }
    std::fprintf(stderr, "%s\n%s\n", outcome.title.c_str(), text.c_str());
    return 2;
}

int run(const PcFirstRunArgs& args, PcFirstRunResult& result) {
    // The language the quick setup chose (config.cfg), else the system's.
    const bool configured = args.language && args.language[0] && std::string_view(args.language) != "auto";
    const std::string locale = configured ? std::string(args.language) : ui::locale_from_environment();
    const ui::Lang lang = ui::lang_from_code(ui::language_code(locale));
    const fs::path exe_dir = from_u8(args.exe_dir ? args.exe_dir : "./");
    const fs::path config_path = from_u8(args.config_path ? args.config_path : "config.cfg");

    sh::import_tool::Options options;
    bool has_gamedata_dir = false;
    std::vector<std::string> warnings;
    const sh::Result<void> applied =
        sh::import_tool::apply_config(options, import_lines(read_file(config_path), has_gamedata_dir), warnings);
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "warning: %s: %s\n", u8(config_path).c_str(), w.c_str());
    }

    // -data (relative to the current directory, like any command line path), then gamedata_dir (relative to the
    // exe), then <exe>/gamedata.
    fs::path gamedata;
    if (args.gamedata_arg && args.gamedata_arg[0]) {
        gamedata = absolute_from(fs::current_path(), from_u8(args.gamedata_arg));
    } else if (has_gamedata_dir && applied) {
        gamedata = absolute_from(exe_dir, options.out);
    } else {
        gamedata = absolute_from(exe_dir, "gamedata");
    }
    std::snprintf(result.gamedata_dir, sizeof result.gamedata_dir, "%s", u8(gamedata).c_str());
    result.disc_image[0] = '\0';
    // The base data ships the import screen's translations even before anything is imported; a gamedata_dir set
    // elsewhere may hold none yet, so the one beside the exe is the fallback.
    if (!ui::load_translation(gamedata / "text", locale)) {
        ui::load_translation(exe_dir / "gamedata" / "text", locale);
    }

    const fs::path image_arg =
        args.image_arg && args.image_arg[0] ? absolute_from(fs::current_path(), from_u8(args.image_arg)) : fs::path();
    const fs::path config_image = options.image.empty() ? fs::path() : absolute_from(exe_dir, options.image);
    const std::vector<fs::path> search_dirs{exe_dir, exe_dir / "data", gamedata};

    const bool ready = ui::Controller::gamedata_ready(gamedata);
    bool again = args.reimport != 0 || (applied && options.overwrite);
    if (ready && !again) {
        // An import of an older layout (importer < current, e.g. palette PNG copies of every TIM) is redone when the
        // disc image it came from is still there; without it the old layout keeps working (the readers accept both).
        const std::string version = read_key(gamedata / "version.cfg", "importer");
        const std::string recorded = read_key(gamedata / "version.cfg", "image_path");
        if (std::atoi(version.c_str()) < sh::import::importer_version() && !recorded.empty() &&
            ui::check_image(recorded).ok()) {
            std::fprintf(stderr, "first run: the import in %s is older (importer %s, now %d); importing again\n",
                         u8(gamedata).c_str(), version.c_str(), sh::import::importer_version());
            again = true;
        }
    }
    if (!applied) {
        std::fprintf(stderr, "warning: %s: %s\n", u8(config_path).c_str(), applied.error().c_str());
    }
    if (ready && !again) {
        // Already imported: only the disc path is needed, for the readers that take the disc directly.
        const std::string recorded = read_key(gamedata / "version.cfg", "image_path");
        std::optional<ui::ImageCheck> image;
        if (!recorded.empty()) {
            ui::ImageCheck check = ui::check_image(recorded);
            if (check.ok()) {
                image = std::move(check);
            }
        }
        if (!image) {
            image = choose_image(image_arg, config_image, search_dirs, lang).image;
        }
        if (image) {
            std::snprintf(result.disc_image, sizeof result.disc_image, "%s", u8(image->bin).c_str());
        }
        return 0;
    }

    if (!applied) {
        report(true, ui::format(lang, ui::Str::SettingsDialogTitle),
               u8(config_path) + ": " + applied.error());
        return 1;
    }

    // Without a usable image a window asks for one; without a display the reason goes to stderr as before.
    sh::first_run::WindowOptions window_options;
    window_options.base_data = exe_dir / "gamedata";
    window_options.icons = exe_dir / "assets" / "icon";
    window_options.controller_db = exe_dir / "gamecontrollerdb.txt";
    window_options.language = lang == ui::Lang::En ? std::string("en") : ui::language_code(locale);
    if (args.import_script && args.import_script[0]) {
        window_options.script = from_u8(args.import_script);
    }
    sh::first_run::ProgressWindow window(std::move(window_options));
    const bool windowed = window.open();
    const bool can_pick = windowed && window.can_pick();
    ImageChoice choice = choose_image(image_arg, config_image, search_dirs, lang);
    if (!choice.image && !can_pick) {
        std::string text;
        if (!choice.error.empty()) {
            text = choice.error;
        } else {
            text = ui::format(lang, ui::Str::NoImageBody,
                              {u8(exe_dir), u8(exe_dir / "data"), u8(config_path), "import_image"});
        }
        report(true, ui::format(lang, ui::Str::NoImageDialogTitle), text);
        return 1;
    }
    const fs::path named = !image_arg.empty() ? image_arg : config_image;
    ui::ImagePicker picker(lang, choice.error.empty() ? std::string() : u8(named), choice.error);
    const ImportContext context{lang, exe_dir, gamedata, ready, options};
    std::optional<ui::ImageCheck> image = choice.image;
    bool picked = false;
    for (;;) {
        if (!image) {
            window.pick(picker, lang, u8(gamedata));
            if (picker.state() != ui::ImagePicker::State::ImportRequested) {
                std::fprintf(stderr, "first run: no disc image was chosen\n");
                return 2;
            }
            image = picker.image();
            picked = true;
        }
        std::string failure;
        const int code = import_from(context, *image, windowed ? &window : nullptr, can_pick, failure, result);
        if (code == 0 && picked && !sh::first_run::is_picked_document(u8(image->path))) {
            remember_image(config_path, image->path);
        }
        // A copy of a document the dialog gave is needed by this import only; another pick makes a new one.
        sh::first_run::forget_picked_documents();
        if (code != 1 || failure.empty()) {
            return code;
        }
        picker.fail(failure);
        image.reset();
    }
}

}  // namespace

extern "C" int PcFirstRun_Run(const PcFirstRunArgs* args, PcFirstRunResult* out) {
    try {
        return run(*args, *out);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "first run: %s\n", e.what());
        return 1;
    }
}
