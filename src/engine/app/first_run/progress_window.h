#pragma once

#include "import/ui/controller.h"
#include "import/ui/picker.h"

#include <filesystem>
#include <memory>
#include <string>

namespace sh::first_run {

struct WindowOptions {
    std::filesystem::path base_data;      // the gamedata/ beside the executable: font/, menu/, text/
    std::filesystem::path icons;          // the window icon's folder
    std::filesystem::path controller_db;  // gamecontrollerdb.txt
    std::string language;                 // the code of the interface language ("ru"); English when empty
    // --import-script: the screen is driven by these commands instead of a player, and may then run on SDL's
    // offscreen video driver (tests and screenshots).
    std::filesystem::path script;
};

// The window of the first-run import: the disc image picker (the path field, Browse, a dropped file) and then the
// progress of the import (stages, bar, percentage, time left), drawn in the look of the quick setup and the game's
// menus (screen_kit) for any window size, a phone's screen included. Mouse, keyboard, game controllers and touch
// drive it; a right-to-left language mirrors it.
class ProgressWindow {
public:
    explicit ProgressWindow(WindowOptions options);
    ~ProgressWindow();
    ProgressWindow(const ProgressWindow&) = delete;
    ProgressWindow& operator=(const ProgressWindow&) = delete;

    // False when there is no display; the caller then reports progress on stderr only.
    bool open();

    void draw(const sh::import::ui::ViewModel& view);
    // What is imported (the image's file name), shown under the progress screen's title.
    void set_subject(std::string subject);

    // Pumps the window events. True once the window was closed, Esc or Back was pressed or Cancel was chosen.
    bool stop_requested();

    // True when someone can answer the picker: a real display (not the offscreen or dummy driver), or a script.
    bool can_pick() const;

    // Runs the picker screen until the user asks for the import or quits (picker.state() says which).
    void pick(sh::import::ui::ImagePicker& picker, sh::import::ui::Lang lang, const std::string& gamedata_dir);

    // Closes the window and its video subsystem; the destructor calls it too.
    void close();

private:
    class Screen;
    std::unique_ptr<Screen> screen_;
};

}  // namespace sh::first_run
