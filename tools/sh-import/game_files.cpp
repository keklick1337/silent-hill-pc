#include "sh-import/game_files.h"

#include "import/input.h"

#include <cstdint>
#include <system_error>
#include <utility>

namespace sh::import_tool {

namespace fs = std::filesystem;

struct ImageFileReader::State {
    sh::import::GameDisc game;
    std::unordered_map<std::string, std::size_t> by_name;  // game path -> index in game.entries
};

ImageFileReader::ImageFileReader(fs::path image, fs::path alias_dir) : image_(std::move(image)) {
    alias_ = std::move(alias_dir) / ("sh-import-sibling-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".bin");
}

ImageFileReader::~ImageFileReader() {
    std::error_code ec;
    state_.reset();  // the image goes first, then its alias
    fs::remove(alias_, ec);
}

void ImageFileReader::open() const {
    const auto fail = [this](std::string message) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        error_ = std::move(message);
    };
    auto image = sh::import::open_image(image_, alias_);
    if (!image) {
        fail(image.error());
        return;
    }
    // The same checks as the import itself (a second SHA-256 pass over the image): the reader never serves files of
    // a disc the import would refuse.
    auto game = sh::import::open_game(image.value(), nullptr, [](double) {});
    if (!game) {
        fail(game.error());
        return;
    }
    auto state = std::make_unique<State>();
    state->game = std::move(game.value());
    for (std::size_t i = 0; i < state->game.entries.size(); ++i) {
        state->by_name.emplace(state->game.entries[i].name, i);
    }
    state_ = std::move(state);
}

std::optional<std::vector<std::uint8_t>> ImageFileReader::read(std::string_view game_path) const {
    std::call_once(once_, [this] { open(); });
    if (!state_) {
        return std::nullopt;
    }
    const auto it = state_->by_name.find(std::string(game_path));
    if (it == state_->by_name.end()) {
        return std::nullopt;
    }
    const sh::archive::Entry& entry = state_->game.entries[it->second];
    const sh::ByteSource& container =
        entry.region == sh::archive::Region::Silent ? *state_->game.silent : *state_->game.hill;
    return sh::archive::read_entry(container, entry);
}

std::string ImageFileReader::error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_;
}

sh::convert::model::SiblingReader ImageFileReader::as_sibling_reader() const {
    return [this](std::string_view game_path) { return read(game_path); };
}

}  // namespace sh::import_tool
