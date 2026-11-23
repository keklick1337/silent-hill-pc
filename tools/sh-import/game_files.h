#pragma once

// Reads single files of the game straight from the disc image, by game path ("ANIM/HB_BASE.ANM"). The model
// converter uses it for neighbouring files (poses, texture sizes) while the importer is still writing raw/.

#include "formats/common/status.h"
#include "formats/convert/model/model_converter.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sh::import_tool {

class ImageFileReader {
public:
    // `alias_dir` receives a short-lived link when the image is a raw .iso (the disc module opens by extension).
    // Nothing is opened before the first read(); the image is checked again on that first read.
    ImageFileReader(std::filesystem::path image, std::filesystem::path alias_dir);
    ~ImageFileReader();
    ImageFileReader(const ImageFileReader&) = delete;
    ImageFileReader& operator=(const ImageFileReader&) = delete;

    // Safe to call from several threads. Empty when the path is not in the game's file table or the image cannot be
    // read; error() tells which.
    std::optional<std::vector<std::uint8_t>> read(std::string_view game_path) const;

    // Empty when the image opened (or has not been needed yet).
    std::string error() const;

    // A reader for sh::convert::model::Options. It refers to this object, which must outlive the converters.
    sh::convert::model::SiblingReader as_sibling_reader() const;

private:
    struct State;
    void open() const;

    std::filesystem::path image_;
    std::filesystem::path alias_;
    mutable std::once_flag once_;
    mutable std::unique_ptr<State> state_;
    mutable std::string error_;
    mutable std::mutex error_mutex_;
};

}  // namespace sh::import_tool
