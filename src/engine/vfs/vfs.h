#pragma once

#include "formats/common/byte_source.h"
#include "formats/disc/disc_image.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::vfs {

// Canonical game path: upper case, '/' separators, no leading slash, e.g. "CHARA/HERO.ILM".
// Callers may use '\' or lower case.
std::string normalize(std::string_view path);

// One source of game files: a folder, a mod, gamedata/ or the disc image.
class Layer {
public:
    virtual ~Layer() = default;

    virtual std::string name() const = 0;
    virtual bool exists(std::string_view path) const = 0;
    virtual std::optional<std::vector<std::uint8_t>> read(std::string_view path) const = 0;

    // The file as a stream of its logical size, for files too big to read whole (XA, movies). Null when the file is
    // missing or cannot be opened. The default reads the whole file through read().
    virtual std::unique_ptr<ByteSource> open(std::string_view path) const;

    // The bytes of the file's disc sectors: like open(), but a layer that keeps the sector tail past the logical size
    // (gamedata/raw) gives it too. Reads by LBA use this. The default is open().
    virtual std::unique_ptr<ByteSource> open_stored(std::string_view path) const;

    // Children of the folder `dir` ("" is the root), canonical names without the folder: files as "HERO.ILM",
    // folders as "CHARA/". The default lists nothing.
    virtual std::vector<std::string> list(std::string_view dir) const;

    // The file on the host file system when the layer keeps it as a plain file of the same bytes, for code that
    // needs a path (video decoders, WAV loaders). Empty otherwise.
    virtual std::filesystem::path host_path(std::string_view path) const;
};

// Layers are searched from the highest priority to the lowest. Equal priorities keep insertion order.
class Vfs {
public:
    void add_layer(std::unique_ptr<Layer> layer, int priority);

    bool exists(std::string_view path) const;
    std::optional<std::vector<std::uint8_t>> read(std::string_view path) const;

    // Same rule as read(): the first layer that has the file answers, even if opening it fails.
    std::unique_ptr<ByteSource> open(std::string_view path) const;

    // Union of the layers' list(), sorted and without repeats.
    std::vector<std::string> list(std::string_view dir) const;

    // The layer that answers for the path, or null.
    const Layer* find(std::string_view path) const;

    // Layer names from highest to lowest priority, for logs.
    std::vector<std::string> layer_names() const;

private:
    struct Slot {
        int priority;
        std::unique_ptr<Layer> layer;
    };

    std::vector<Slot> slots_;
};

// Files under `root`. Matching is case-insensitive: "chara/hero.ilm" finds "CHARA/HERO.ILM".
std::unique_ptr<Layer> make_directory_layer(std::filesystem::path root, std::string name);

// Files of the disc image, resolved through ISO 9660 and the SILENT. / HILL. containers.
std::unique_ptr<Layer> make_disc_layer(std::shared_ptr<const disc::Image> image);

// One row of gamedata/index.tsv.
struct GameDataFile {
    std::string path;        // canonical game path; the file is raw/<path>
    std::uint64_t size = 0;  // logical size: what a read of the path gives
    std::string source;      // "SILENT:id=12", "HILL:lba=39359+2152", "SLUS:file", "ISO:file"
};

// gamedata/index.tsv. Empty with `error` set when the file is missing or malformed.
std::vector<GameDataFile> read_gamedata_index(const std::filesystem::path& gamedata_dir, std::string& error);

// raw/ of an imported gamedata/ (version.cfg and index.tsv present). Sizes come from index.tsv, not from the file,
// because raw/ keeps sector tails; open_stored() gives the tail too. Null with `error` set when the folder
// is not a finished import of SLUS-007.07.
std::unique_ptr<Layer> make_gamedata_layer(const std::filesystem::path& gamedata_dir, std::string& error);

}  // namespace sh::vfs
