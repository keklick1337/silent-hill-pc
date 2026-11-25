#pragma once

// The game's files after the import, without gamedata/raw/. The game still loads files of the disc by LBA (the
// original's in-memory layouts) and a few readers take a map overlay's bytes; every such file is served from what
// the import wrote, in this order of preference:
//
//   * a lossless converter's output (TIM pictures, overlays as text frames, KDT songs, cutscenes, animations, CMP
//     archives, demos): the file is rebuilt with the converter's encode() from gamedata/convert/;
//   * a unified map file (BG/*.IPD, *.PLM): the bytes packed in gamedata/maps/areas/<AREA>.glb;
//   * otherwise the file as it is, in gamedata/convert/files/<game path> ("kept": models, sound banks, movies and
//     a few small binaries that have no lossless converted form).
//
// gamedata/convert/files/files.tsv names the source of each file (path, size, stored size, crc32, source, the
// converter's outputs); sector_tails.bin holds the bytes past each file's logical size in its last disc sector when
// they are not all zeros, which DiscSectors serves as the disc did.
// The files the engine never reads are not served: the XA voice and music tracks (the sound engine plays their
// WAVs from convert/audio/xa/), SYSTEM.CNF and the executable (its file table is tables/filetable.json).
//
// build_store() writes this from raw/ at the end of an import (and once for an older import), checking every
// rebuilt file against index.tsv; remove_raw() then deletes raw/ unless the config keeps it (keep_raw = 1).

#include "formats/common/byte_source.h"
#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::convert::gamefiles {

inline constexpr const char* kStoreDir = "convert/files";            // under gamedata/
inline constexpr const char* kManifest = "convert/files/files.tsv";  // under gamedata/
inline constexpr const char* kTails = "convert/files/sector_tails.bin";

// Where a file's bytes come from.
enum class Origin : std::uint8_t {
    Kept,       // convert/files/<path>
    Converter,  // a lossless converter's outputs in convert/
    Area,       // maps/areas/<AREA>.glb
};

struct Entry {
    std::string path;          // canonical game path, "CHARA/HERO.TIM"
    std::uint64_t size = 0;    // logical
    std::uint64_t stored = 0;  // with the tail of the last disc sector (HILL. files have none)
    std::uint32_t crc32 = 0;   // of the stored bytes, as index.tsv
    Origin origin = Origin::Kept;
    std::string converter;             // Origin::Converter: the converter's name ("tim", "text", ...)
    std::vector<std::string> outputs;  // Origin::Converter: its outputs, relative to convert/ (from index.tsv)
};

// The files of one gamedata/ (convert/files/files.tsv). Immutable once open; reads are thread safe.
class Store {
public:
    // Null with `error` set when the manifest is missing or broken.
    static std::shared_ptr<const Store> open(const std::filesystem::path& gamedata_dir, std::string& error);
    ~Store();

    const std::vector<Entry>& entries() const;
    // `path` in any case, '\' or '/'.
    const Entry* find(std::string_view path) const;

    // The file's logical bytes. A rebuilt file whose CRC-32 differs from the manifest is still returned (someone
    // edited the converted file on purpose), with `changed` set.
    Result<std::vector<std::uint8_t>> read(const Entry& entry, bool* changed = nullptr) const;
    // The bytes past the logical size in the file's last disc sector (none for HILL. files).
    std::vector<std::uint8_t> tail(const Entry& entry) const;
    // Origin::Kept: the file as a stream of its logical size, for big files (movies). Null otherwise.
    std::unique_ptr<ByteSource> open_kept(const Entry& entry) const;

    struct Impl;

private:
    explicit Store(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// The store of a gamedata/ directory, opened once per process (null when there is none). forget() drops the cached
// one after the store was rebuilt.
std::shared_ptr<const Store> store_for(const std::filesystem::path& gamedata_dir);
void forget(const std::filesystem::path& gamedata_dir);

// A game file's bytes as raw/ keeps them (the logical bytes, then the sector tail): from the store, else from
// raw/<path> (an import that predates the store). Nothing when neither has the file.
std::optional<std::vector<std::uint8_t>> read_game_file(const std::filesystem::path& gamedata_dir,
                                                        std::string_view path);

// True when <gamedata>/raw/ exists.
bool has_raw(const std::filesystem::path& gamedata_dir);

struct BuildReport {
    std::size_t converted = 0;  // files rebuilt by a lossless converter
    std::size_t areas = 0;      // files served from maps/areas/*.glb
    std::size_t kept = 0;       // files in convert/files/
    std::uint64_t kept_bytes = 0;
    std::size_t skipped = 0;  // files the engine does not read (XA voice and music tracks, SYSTEM.CNF, the exe)
    std::uint64_t skipped_bytes = 0;
    std::size_t tails = 0;           // files with a sector tail
    std::vector<std::string> notes;  // rebuilds that did not match index.tsv (those files are kept instead)
};

// Writes convert/files/ (manifest, tails, kept files) from raw/ and index.tsv. With `move` the kept files are
// moved out of raw/ instead of copied (raw/ is deleted next). Needs tables/filetable.json: without raw/ it is the
// only copy of the file table. `threads` 0: one per hardware thread.
Result<BuildReport> build_store(const std::filesystem::path& gamedata_dir, bool move, unsigned threads);
std::string format_build_line(const BuildReport& report);

// Deletes raw/ once the store serves every file. The value is the number of bytes freed.
Result<std::uint64_t> remove_raw(const std::filesystem::path& gamedata_dir);

// Deletes the converted files the engine never reads and that only repeat what the store keeps: the WAVs of the
// movies' sound (a movie plays its sound from its own sectors in convert/files/XA/), and drops them from index.tsv.
struct UnreadReport {
    std::size_t files = 0;
    std::uint64_t bytes = 0;
};
Result<UnreadReport> remove_unread_outputs(const std::filesystem::path& gamedata_dir);

// The last step of an import, and of the first start of an older one: the store (built when missing, moving the
// files out of raw/ unless `keep_raw`), then raw/ and the unread converted files deleted unless `keep_raw`.
struct FinishReport {
    bool built = false;
    BuildReport build;
    bool removed = false;
    std::uint64_t freed_bytes = 0;
    UnreadReport unread;
};
Result<FinishReport> finish_import(const std::filesystem::path& gamedata_dir, bool keep_raw, unsigned threads);

}  // namespace sh::convert::gamefiles
