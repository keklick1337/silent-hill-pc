// The system's dialogs for importing mods (engine/mods/mod_import.h): SDL's file and folder dialogs on the desktop;
// on Android SDL's document picker for a .zip and the activity's tree picker for a folder, whose files the activity
// copies into the app's storage (SilentHillActivity.pickModFolder), since no path reaches a picked tree.
#include "engine/app/mod_import_picker.h"

#include "engine/app/first_run/picked_document.h"
#include "engine/mods/mod_import.h"
#include "engine/platform/host_state.h"

#include <SDL3/SDL.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace sh::app {

namespace {

struct Pending {
    sh::mods::PickDone done;
};

void SDLCALL on_picked(void* userdata, const char* const* files, int) {
    std::unique_ptr<Pending> pending(static_cast<Pending*>(userdata));
    std::string path;
    if (files == nullptr) {
        SDL_Log("mods: the file dialog failed: %s", SDL_GetError());
    } else if (files[0] != nullptr) {
        path = sh::first_run::picked_document(files[0]);
    }
    pending->done(std::move(path));
}

const SDL_DialogFileFilter kZipFilter{"Zip archives", "zip"};

#ifdef __ANDROID__
std::mutex g_tree_mutex;
sh::mods::PickDone g_tree_done;  // the folder pick the activity is copying
#endif

bool open(sh::mods::PickKind kind, sh::mods::PickDone done) {
    if (kind == sh::mods::PickKind::Folder) {
#ifdef __ANDROID__
        {
            std::lock_guard lock(g_tree_mutex);
            g_tree_done = std::move(done);
        }
        if (sh::first_run::activity_call("pickModFolder", std::string()) != "1") {
            std::lock_guard lock(g_tree_mutex);
            g_tree_done = nullptr;
            return false;
        }
        return true;
#else
        SDL_ShowOpenFolderDialog(on_picked, new Pending{std::move(done)}, g_window, nullptr, false);
        return true;
#endif
    }
    SDL_ShowOpenFileDialog(on_picked, new Pending{std::move(done)}, g_window, &kZipFilter, 1, nullptr, false);
    return true;
}

void poll() {
#ifdef __ANDROID__
    // "" while the activity still waits or copies, "-" when the pick was cancelled or failed, else the copy's path.
    const std::string answer = sh::first_run::activity_call("takeModFolder", std::string());
    if (answer.empty()) {
        return;
    }
    sh::mods::PickDone done;
    {
        std::lock_guard lock(g_tree_mutex);
        done = std::move(g_tree_done);
        g_tree_done = nullptr;
    }
    if (done) {
        done(answer == "-" ? std::string() : answer);
    }
#endif
}

}  // namespace

void install_mod_import_picker() {
    sh::mods::Picker p;
    p.open = open;
    p.available = [](sh::mods::PickKind) { return true; };
    p.poll = poll;
#ifdef __ANDROID__
    // Only the copy the activity made of a picked .zip (picked-copies/<file>); anything else it leaves alone.
    p.release = [](const std::string& path) { sh::first_run::activity_call("forgetDocument", path); };
#endif
    sh::mods::set_import_picker(std::move(p));
}

}  // namespace sh::app
