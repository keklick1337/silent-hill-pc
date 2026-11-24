#include "import/ui/picker.h"

#include <utility>

namespace sh::import::ui {

ImagePicker::ImagePicker(Lang lang, std::string text, std::string error)
    : lang_(lang), text_(std::move(text)), error_(std::move(error)) {}

void ImagePicker::set_text(std::string text) { text_ = std::move(text); }

bool ImagePicker::check() {
    ImageCheck result = check_image(text_);
    if (!result.ok()) {
        error_ = describe(lang_, result);
        image_.reset();
        return false;
    }
    error_.clear();
    image_ = std::move(result);
    return true;
}

void ImagePicker::choose(std::string path) {
    if (state_ != State::Picking) {
        return;
    }
    text_ = std::move(path);
    check();
}

bool ImagePicker::submit() {
    if (state_ != State::Picking || !check()) {
        return false;
    }
    state_ = State::ImportRequested;
    return true;
}

void ImagePicker::dialog_failed(const std::string& reason, bool touch) {
    if (state_ == State::Picking) {
        error_ = format(lang_, touch ? Str::ErrNoFileDialogTouch : Str::ErrNoFileDialog, {reason});
    }
}

void ImagePicker::fail(std::string error) {
    state_ = State::Picking;
    image_.reset();
    error_ = std::move(error);
}

void ImagePicker::quit() { state_ = State::Quit; }

}  // namespace sh::import::ui
