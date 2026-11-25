#include "sh-import/run.h"

#include "formats/convert/level/map_unified_files.h"
#include "formats/convert/filetable/file_table.h"
#include "formats/convert/gamefiles/game_files.h"
#include "formats/convert/mapdata/mapdata_files.h"
#include "formats/convert/materials/materials_manifest.h"
#include "formats/convert/materials/texture_bake.h"
#include "import/import.h"
#include "sh-import/converters.h"
#include "sh-import/game_files.h"
#include "sh-import/options.h"
#include "sh-import/progress.h"
#include "sh-import/summary.h"
#include "sh-import/tables.h"

#include <chrono>
#include <memory>
#include <optional>
#include <fstream>
#include <iterator>
#include <ostream>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace sh::import_tool {

namespace fs = std::filesystem;

namespace {

bool is_terminal(std::FILE* stream) {
#ifdef _WIN32
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
}

std::string first_line(const std::string& text) {
    return text.substr(0, text.find('\n'));
}

}  // namespace

int run(std::span<const std::string> args, std::ostream& out, std::ostream& err, std::FILE* progress,
        const std::atomic<bool>* cancel) {
    auto parsed = parse_args(args);
    if (!parsed) {
        err << "sh-import: " << parsed.error() << "\ntry \"sh-import --help\"\n";
        return 2;
    }
    const CliArgs& cli = parsed.value();
    if (cli.help) {
        out << usage();
        return 0;
    }

    Options options;
    if (cli.config) {
        std::ifstream in(*cli.config, std::ios::binary);
        if (!in) {
            err << "sh-import: cannot read the config file " << cli.config->string() << "\n";
            return 2;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<std::string> warnings;
        const Result<void> applied = apply_config(options, text, warnings);
        for (const std::string& w : warnings) {
            err << "warning: " << cli.config->string() << ": " << w << "\n";
        }
        if (!applied) {
            err << "sh-import: " << cli.config->string() << ": " << applied.error() << "\n";
            return 2;
        }
    }
    apply_cli(options, cli);

    if (options.image.empty()) {
        err << "sh-import: no image given (an argument or import_image in the config)\ntry \"sh-import --help\"\n";
        return 2;
    }

    std::error_code ec;
    const fs::path target = fs::absolute(options.out, ec);
    ImageFileReader reader(options.image, target.parent_path());
    std::vector<std::unique_ptr<sh::convert::Converter>> converters = build_converters(options, reader.as_sibling_reader());

    sh::import::Request request;
    request.image = options.image;
    request.gamedata_dir = options.out;
    request.overwrite = options.overwrite;
    request.verify = options.verify;
    request.threads = options.threads;
    sh::import::Report report;
    request.report = &report;
    request.cancel = cancel;

    err << "importing " << options.image.string() << " -> " << options.out.string() << " (";
    if (converters.empty()) {
        err << "raw only";
    }
    for (std::size_t i = 0; i < converters.size(); ++i) {
        err << (i ? ", " : "") << converters[i]->name();
    }
    err << (options.verify ? "; verify" : "; no verify") << ")\n";
    err.flush();

    std::optional<ProgressPrinter> printer;
    if (progress) {
        printer.emplace(progress, is_terminal(progress));
    }
    const sh::import::ProgressFn progress_fn = [&](std::string_view stage, double fraction) {
        if (printer) {
            (*printer)(stage, fraction);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    const Result<void> result = sh::import::import_image(request, converters, progress_fn);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (printer) {
        printer->finish();
    }

    if (const std::string reader_error = reader.error(); !reader_error.empty()) {
        err << "warning: neighbouring game files could not be read, models have no rest pose: " << reader_error << "\n";
    }

    if (!result) {
        err << "sh-import: import failed: " << first_line(result.error()) << "\n";
        out << format_summary(report, {}, {}, seconds);
        return 1;
    }

    out << format_summary(report, directory_sizes(options.out / "raw"), directory_sizes(options.out / "convert"), seconds);
    int code = 0;
    {
        const TablesReport tables = generate_tables(options.out, options.overwrite);
        for (const std::string& w : tables.warnings) {
            err << "warning: tables: " << w << "\n";
        }
        out << "tables:         " << format_tables_line(tables) << "\n";
        if (tables.failed) {
            err << "sh-import: " << tables.failed << " tables could not be written\n";
            code = 1;
        }
    }
    // gamedata/tables/filetable.json: the game's file table from the executable.
    if (const sh::Result<bool> ft = sh::convert::filetable::write_from_raw(options.out, options.overwrite); !ft) {
        err << "sh-import: file table: " << ft.error() << "\n";
        code = 1;
    } else {
        out << "file table:     " << (ft.value() ? "written" : "kept") << "\n";
    }
    // gamedata/maps/: the data parts of the map overlays, checked against raw/ byte for byte.
    const sh::convert::mapdata::GenerateReport maps = sh::convert::mapdata::generate_map_files(options.out, options.overwrite);
    for (const std::string& w : maps.warnings) {
        err << "warning: maps: " << w << "\n";
    }
    out << "maps:           " << sh::convert::mapdata::format_report_line(maps) << "\n";
    if (maps.failed) {
        err << "sh-import: " << maps.failed << " map data files could not be written or do not match the disc\n";
        code = 1;
    }
    // gamedata/maps/areas/: geometry, collision and map placements in one .glb per area, with the original map
    // files packed losslessly.
    if (options.is_enabled(Category::Levels)) {
        const sh::convert::level::UnifiedReport areas =
            sh::convert::level::generate_unified_maps(options.out, options.overwrite, options.threads);
        for (const std::string& w : areas.warnings) {
            err << "warning: map areas: " << w << "\n";
        }
        out << "map areas:      " << sh::convert::level::format_unified_line(areas) << "\n";
        if (areas.failed) {
            err << "sh-import: " << areas.failed << " map area files could not be written or do not match raw/\n";
            code = 1;
        }
    } else {
        out << "map areas:      off (import_levels = none, the map files are kept in convert/files/)\n";
    }
    // convert/textures/: true-colour pictures in the CLUT rows the geometry draws (importer 3). It rewrites the
    // model, level and map area glbs, so it comes after the map areas and before the manifest.
    if (options.is_enabled(Category::Textures)) {
        // The report lines start with "textures: " / "materials: "; the table above has its own labels.
        auto body = [](const std::string& line) { return line.substr(line.find(": ") + 2); };
        if (const auto baked = sh::convert::materials::bake_textures(options.out, options.threads); !baked) {
            err << "sh-import: textures: " << baked.error() << "\n";
            code = 1;
        } else {
            for (const std::string& w : baked.value().warnings) {
                err << "warning: textures: " << w << "\n";
            }
            out << "texture bake:   " << body(sh::convert::materials::format_bake_line(baked.value())) << "\n";
        }
        // convert/materials.json: which pictures each glb uses, as the game's first run writes it.
        if (const auto materials = sh::convert::materials::write_manifest(options.out); !materials) {
            err << "sh-import: materials: " << materials.error() << "\n";
            code = 1;
        } else {
            for (const std::string& w : materials.value().warnings) {
                err << "warning: materials: " << w << "\n";
            }
            out << "materials:      " << body(sh::convert::materials::format_report_line(materials.value())) << "\n";
        }
    } else {
        out << "texture bake:   off (import_textures = none)\n";
    }
    // Last, as every step above reads raw/: the game files without it (convert/files/), then raw/ goes.
    if (const auto finished = sh::convert::gamefiles::finish_import(options.out, options.keep_raw, options.threads);
        !finished) {
        err << "sh-import: game files: " << finished.error() << "\n";
        code = 1;
    } else {
        for (const std::string& note : finished.value().build.notes) {
            err << "warning: game files: " << note << "\n";
        }
        out << "game files:     " << sh::convert::gamefiles::format_build_line(finished.value().build) << "\n";
        out << "raw/:           "
            << (finished.value().removed ? "deleted (" + std::to_string(finished.value().freed_bytes >> 20) + " MB)"
                                         : std::string("kept (keep_raw = 1)"))
            << "\n";
        if (finished.value().unread.files != 0) {
            out << "unread files:   " << finished.value().unread.files << " movie sound WAVs deleted ("
                << (finished.value().unread.bytes >> 20) << " MB)\n";
        }
    }
    out << "result:         " << fs::absolute(options.out, ec).string() << "\n";
    return code;
}

}  // namespace sh::import_tool
