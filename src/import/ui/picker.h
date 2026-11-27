#pragma once

#include "import/ui/input_check.h"
#include "import/ui/strings.h"

#include <optional>
#include <string>
#include <string_view>

namespace sh::import::ui {

// The disc image picker of the first-run window: the path comes from the file dialog, the text
// field or a file dropped onto the window. No SDL here; the window draws it and forwards what the user did.
// What the Android activity answers about the document its system picker was asked for
// (SilentHillActivity.takeDocument): "" while nothing is there, "~<name>" while the chosen document is opened or
// copied, "-" for a cancelled pick, "!<reason>" when no picker could be shown, "?<name>" for a document that cannot
// be opened, else the path the importer reads.
struct PickAnswer {
    enum class Kind { Waiting, Opening, Cancelled, NoDialog, Unreadable, Path } kind = Kind::Waiting;
    std::string text;  // the name, the reason or the path
};
PickAnswer parse_pick_answer(std::string_view answer);

class ImagePicker {
public:
    enum class State { Picking, ImportRequested, Quit };

    explicit ImagePicker(Lang lang, std::string text = {}, std::string error = {});

    // A phone: the file comes only from the system's picker, so the errors name Browse instead of a path to type.
    void set_touch(bool touch) { touch_ = touch; }

    void set_text(std::string text);  // typing: keeps the error until the next check
    void choose(std::string path);    // a file from the dialog or a drop: fills the field and checks it at once
    bool submit();                    // Import: true and ImportRequested when the image passes the check
    void dialog_failed(const std::string& reason, bool touch = false);  // touch: no path to type or drop
    void opening(const std::string& name);            // the chosen document is being opened: a note, no error
    void document_failed(const std::string& name);    // the chosen document cannot be opened
    void apply(const PickAnswer& answer);             // the activity's answer, by its kind
    void fail(std::string error);     // the import did not succeed: back to Picking with its reason
    void quit();

    State state() const { return state_; }
    const std::string& text() const { return text_; }
    const std::string& error() const { return error_; }
    // Not an error: what the picker is busy with. Import waits while it is set.
    const std::string& note() const { return note_; }
    // Set while ImportRequested.
    const std::optional<ImageCheck>& image() const { return image_; }

private:
    bool check();

    Lang lang_;
    State state_ = State::Picking;
    std::string text_;
    std::string error_;
    std::string note_;
    bool touch_ = false;
    std::optional<ImageCheck> image_;
};

}  // namespace sh::import::ui
