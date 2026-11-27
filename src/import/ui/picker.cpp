#include "import/ui/picker.h"

#include <utility>

namespace sh::import::ui {

ImagePicker::ImagePicker(Lang lang, std::string text, std::string error)
    : lang_(lang), text_(std::move(text)), error_(std::move(error)) {}

PickAnswer parse_pick_answer(std::string_view answer) {
    using Kind = PickAnswer::Kind;
    if (answer.empty()) return {Kind::Waiting, {}};
    const std::string rest(answer.substr(1));
    switch (answer.front()) {
        case '~':
            return {Kind::Opening, rest};
        case '-':
            return {Kind::Cancelled, {}};
        case '!':
            return {Kind::NoDialog, rest.empty() ? std::string("?") : rest};
        case '?':
            return {Kind::Unreadable, rest};
        default:
            return {Kind::Path, std::string(answer)};
    }
}

void ImagePicker::set_text(std::string text) { text_ = std::move(text); }

bool ImagePicker::check() {
    ImageCheck result = check_image(text_);
    note_.clear();
    if (!result.ok()) {
        error_ = touch_ && result.problem == Problem::Empty ? format(lang_, Str::ErrEmptyTouch)
                                                           : describe(lang_, result);
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
    // The chosen document is still on its way: the note says so, and Import waits for it.
    if (state_ != State::Picking || !note_.empty() || !check()) {
        return false;
    }
    state_ = State::ImportRequested;
    return true;
}

void ImagePicker::dialog_failed(const std::string& reason, bool touch) {
    if (state_ == State::Picking) {
        note_.clear();
        error_ = format(lang_, touch || touch_ ? Str::ErrNoFileDialogTouch : Str::ErrNoFileDialog, {reason});
    }
}

void ImagePicker::opening(const std::string& name) {
    if (state_ == State::Picking) {
        error_.clear();
        note_ = format(lang_, Str::PickerOpening, {name});
    }
}

void ImagePicker::document_failed(const std::string& name) {
    if (state_ != State::Picking) {
        return;
    }
    // The field must not keep a file that is not the one just chosen.
    text_.clear();
    image_.reset();
    note_.clear();
    error_ = format(lang_, Str::ErrDocumentUnreadable, {name});
}

void ImagePicker::apply(const PickAnswer& answer) {
    switch (answer.kind) {
        case PickAnswer::Kind::Waiting:
            break;
        case PickAnswer::Kind::Opening:
            opening(answer.text);
            break;
        case PickAnswer::Kind::Cancelled:
            note_.clear();
            break;
        case PickAnswer::Kind::NoDialog:
            dialog_failed(answer.text, true);
            break;
        case PickAnswer::Kind::Unreadable:
            document_failed(answer.text);
            break;
        case PickAnswer::Kind::Path:
            choose(answer.text);
            break;
    }
}

void ImagePicker::fail(std::string error) {
    state_ = State::Picking;
    image_.reset();
    note_.clear();
    error_ = std::move(error);
}

void ImagePicker::quit() { state_ = State::Quit; }

}  // namespace sh::import::ui
