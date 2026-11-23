#pragma once

#include "formats/common/status.h"
#include "formats/convert/converter.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sh::import {

// Called only on the thread that called import_image(), also when the work runs on several threads (progress is
// aggregated there). It must not throw; to cancel, set Request::cancel. `stage` is one of "read", "raw", "convert", "verify", "commit".
// `fraction` runs from 0 to 1 over the whole import and never goes down.
using ProgressFn = std::function<void(std::string_view stage, double fraction)>;

// A file that a converter accepted but could not convert, or whose round trip did not give the original bytes.
struct ConversionFailure {
    std::string path;       // game path, e.g. "CHARA/HERO.TIM"
    std::string converter;  // Converter::name()
    std::string message;
};

struct ConverterStats {
    std::string name;
    bool lossless = false;
    std::uint64_t accepted = 0;     // raw files the converter took
    std::uint64_t outputs = 0;      // files written to convert/
    std::uint64_t round_trips = 0;  // lossless only: files with encode(decode(x)) == x (needs Request::verify)
};

// Numbers of one import, filled in on success and on failure.
struct Report {
    std::string image_sha256;
    std::uint64_t raw_files = 0;
    std::uint64_t raw_bytes = 0;         // stored bytes, sector tails included
    std::uint64_t crc_checked = 0;       // files re-read from disk and compared with index.tsv
    std::uint64_t converted_files = 0;
    std::uint64_t converted_bytes = 0;
    std::vector<ConverterStats> converters;
    std::vector<ConversionFailure> failures;
};

// Fields may be added; the import_image() signature stays.
// Every field maps to a config.cfg key.
struct Request {
    std::filesystem::path image;         // .cue, .bin (MODE2/2352) or raw .iso (2352 bytes per sector)
    std::filesystem::path gamedata_dir;  // built in a temporary sibling and renamed at the end
    bool overwrite = false;              // import_overwrite: replace an existing gamedata_dir
    bool verify = true;                  // import_verify: CRC32 of every file against index.tsv, lossless round trips
    unsigned threads = 0;                // import_threads: workers for raw and convert; 0 = one per hardware thread.
                                         // Converters are called from several threads at once when this is above 1.
                                         // The result does not depend on it.
    Report* report = nullptr;            // optional; written before import_image() returns
    // Optional cancel token, polled between files and inside the long loops (image hash, file copy, verify). When it
    // turns true import_image() returns Result::fail("cancelled"), the temporary directory is removed and the target
    // is not touched. It may be set from the progress callback or from another thread. A converter call that is
    // already running is not interrupted; the import stops after it. Once the final rename has started it is not
    // cancelled.
    const std::atomic<bool>* cancel = nullptr;
};

// The importer version written to version.cfg ("importer = N"); a gamedata/ with a lower number has an older layout.
int importer_version();

// Always writes raw/, index.tsv and version.cfg. Each converter that accepts a raw file adds its outputs to convert/.
// An empty converter list gives raw/ only. Nothing is left at gamedata_dir if the import fails: the work is done in
// a temporary sibling that is deleted on failure and renamed to gamedata_dir last, with version.cfg written just
// before. An existing gamedata_dir is kept unless overwrite is set (an empty directory is replaced either way).
//
// Layout of raw/ (docs/FORMATS.md): the files of the 2074-entry game table by game path
// ("CHARA/HERO.ILM"), HILL. tracks as "XA/<name>" in 2336-byte sectors, SLUS_007.07 and SYSTEM.CNF at the top.
// SILENT. entries are stored with their sector tail (sectors x 2048 bytes); converters get the logical `size` bytes.
Result<void> import_image(const Request& request,
                          const std::vector<std::unique_ptr<convert::Converter>>& converters,
                          const ProgressFn& progress);

}  // namespace sh::import
