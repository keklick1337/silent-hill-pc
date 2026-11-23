#pragma once

#include "import/ui/input_check.h"
#include "import/ui/strings.h"

#include <optional>
#include <string>

namespace sh::import::ui {

// The disc image picker of the first-run window: the path comes from the file dialog, the text
// field or a file dropped onto the window. No SDL here; the window draws it and forwards what the user did.
class ImagePicker {
public:
    enum class State { Picking, ImportRequested, Quit };

    explicit ImagePicker(Lang lang, std::string text = {}, std::string error = {});

    void set_text(std::string text);  // typing: keeps the error until the next check
    void choose(std::string path);    // a file from the dialog or a drop: fills the field and checks it at once
    bool submit();                    // Import: true and ImportRequested when the image passes the check
    void dialog_failed(const std::string& reason);
    void fail(std::string error);     // the import did not succeed: back to Picking with its reason
    void quit();

    State state() const { return state_; }
    const std::string& text() const { return text_; }
    const std::string& error() const { return error_; }
    // Set while ImportRequested.
    const std::optional<ImageCheck>& image() const { return image_; }

private:
    bool check();

    Lang lang_;
    State state_ = State::Picking;
    std::string text_;
    std::string error_;
    std::optional<ImageCheck> image_;
};

}  // namespace sh::import::ui
