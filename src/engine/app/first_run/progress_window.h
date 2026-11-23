#pragma once

#include "import/ui/controller.h"
#include "import/ui/picker.h"

#include <string>

struct SDL_Window;
struct SDL_Renderer;

namespace sh::first_run {

// A small SDL window that draws the import ViewModel: stage marks, a progress bar and the percentage; the text (stage,
// ETA) goes to the window title. Before an import it can show the disc image picker (ImGui, the console's font).
class ProgressWindow {
public:
    ProgressWindow() = default;
    ~ProgressWindow();
    ProgressWindow(const ProgressWindow&) = delete;
    ProgressWindow& operator=(const ProgressWindow&) = delete;

    // False when there is no display; the caller then reports progress on stderr only.
    bool open();

    void draw(const sh::import::ui::ViewModel& view);

    // Pumps the window events. True once the window was closed or Esc was pressed.
    bool stop_requested();

    // True when someone can answer the picker: a real display (not the offscreen or dummy driver).
    bool can_pick() const;

    // Runs the picker screen until the user asks for the import or quits (picker.state() says which).
    void pick(sh::import::ui::ImagePicker& picker, sh::import::ui::Lang lang, const std::string& gamedata_dir);

    // Closes the window and its video subsystem; the destructor calls it too.
    void close();

private:
    static bool interactive();
    bool create(int width, int height);
    void destroy_window();
    bool init_imgui();
    void render_imgui();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool video_ = false;
    bool stop_ = false;
    std::string title_;
    bool imgui_ = false;
    bool font_loaded_ = false;
};

}  // namespace sh::first_run
