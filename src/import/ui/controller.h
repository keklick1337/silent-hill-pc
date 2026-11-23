#pragma once

#include "formats/common/status.h"
#include "import/import.h"
#include "import/ui/strings.h"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Core of the first-run screen (item 9). No rendering and no SDL: a renderer draws
// ViewModel and sends Commands, nothing else.

namespace sh::import::ui {

enum class Phase {
    Ready,       // gamedata/ is already imported; nothing to show
    AskPath,     // waiting for the image path
    Importing,
    Cancelling,  // cancel requested, the import has not stopped yet
    Done,
    Failed,
    Cancelled,
    Aborted,     // the user left without importing
};

constexpr bool is_final(Phase p) {
    return p == Phase::Ready || p == Phase::Done || p == Phase::Failed || p == Phase::Cancelled || p == Phase::Aborted;
}

enum class ActionId { Import, Cancel, Retry, Quit, Close };

// The import_overwrite, import_verify and import_threads keys of config.cfg.
struct ImportSettings {
    bool overwrite = false;
    bool verify = true;
    unsigned threads = 0;  // 0 = one per hardware thread
};

// Applies one config.cfg key. False for any other key or a value that is not 0/1/true/false (threads: a number).
bool apply_config_key(ImportSettings& settings, std::string_view key, std::string_view value);

struct Action {
    ActionId id;
    std::string label;
    bool enabled = true;
};

// Everything a renderer needs, already localised. Built under the lock, so it is consistent.
struct ViewModel {
    std::uint64_t revision = 0;  // grows with every change
    Phase phase = Phase::AskPath;
    std::string title;
    std::vector<std::string> body;  // paragraphs

    bool has_input = false;
    std::string input_label;
    std::string input_text;
    std::string input_hint;  // empty when there is none

    std::string message;  // validation error, failure reason, result line; empty when there is none
    bool message_is_error = false;

    bool has_progress = false;
    std::string stage;       // raw stage name from the importer: "read", "raw", ...
    std::string stage_text;  // localised
    double fraction = 0.0;   // 0..1, never decreases during one import
    int percent = 0;
    std::optional<double> eta_seconds;
    std::string eta_text;  // localised, always set while has_progress

    // Shown while the path is asked; the same values go into the Request.
    ImportSettings settings;
    std::string overwrite_label;
    std::string verify_label;
    std::string threads_label;

    std::vector<Action> actions;
};

// What a renderer sends back.
struct Command {
    enum class Kind { SetText, Submit, Cancel, Retry, Quit, SetOverwrite, SetVerify, SetThreads } kind = Kind::Submit;
    std::string text;  // SetText
    unsigned number = 0;  // SetOverwrite and SetVerify: 0 or 1; SetThreads: the count
};

class Controller {
public:
    // import_image() with the converter list bound. The request carries the cancel token (Request::cancel).
    using ImportFn = std::function<Result<void>(const Request& request, const ProgressFn& progress)>;
    using Clock = std::chrono::steady_clock;

    struct Options {
        ImportFn import;
        std::filesystem::path gamedata_dir;
        std::string image_hint;  // value of the import_image config key; empty if unset
        ImportSettings settings;
        Lang lang = Lang::En;
        std::function<Clock::time_point()> now;  // tests replace the clock
    };

    explicit Controller(Options options);
    // Cancels a running import and waits for the worker thread.
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    // Called with no lock held, from the UI thread or from the import thread. Set it before start(); it must not call
    // back into submit() or the destructor. It should only wake the renderer, which then calls snapshot().
    void set_listener(std::function<void()> on_change);

    // Ready when gamedata_dir holds version.cfg (the importer writes it last), AskPath otherwise.
    void start();

    // All of these may be called from any thread. A command that does not fit the phase is ignored.
    void handle(const Command& command);
    void set_text(std::string text);
    bool submit();  // checks the typed path; true when the import was started
    void cancel();  // Importing -> Cancelling: sets the token in the Request; the importer stops at its next poll
    void set_overwrite(bool value);  // the three setters work while the path is asked
    void set_verify(bool value);
    void set_threads(unsigned value);
    void retry();   // Failed or Cancelled -> AskPath, the typed path is kept
    void quit();    // AskPath -> Aborted; while importing it is a cancel

    ViewModel snapshot() const;

    // Waits until revision() != seen or the timeout passes. Returns true if the state changed.
    bool wait_for_change(std::uint64_t seen, std::chrono::milliseconds timeout) const;
    // Waits for a final phase. Returns false on timeout.
    bool wait_final(std::chrono::milliseconds timeout) const;

    Phase phase() const;

    static bool gamedata_ready(const std::filesystem::path& gamedata_dir);

    // An ImportFn that runs sh::import::import_image with these converters (shared, so the function can be copied).
    static ImportFn make_import_fn(std::shared_ptr<const std::vector<std::unique_ptr<convert::Converter>>> converters);

private:
    void run_import(Request request);
    void on_progress(std::string_view stage, double fraction);
    void finish(Phase phase, std::string message);
    void changed_locked();  // bumps revision, wakes waiters; the listener is called by notify()
    void notify();
    ViewModel build_locked() const;
    Clock::time_point now() const;

    Options options_;
    std::function<void()> listener_;

    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    std::uint64_t revision_ = 0;
    Phase phase_ = Phase::AskPath;
    // False from the start of an import until finish() has called the listener for the final phase, so
    // wait_final() cannot return while that last callback still runs.
    bool final_delivered_ = true;
    std::string input_text_;
    std::string error_;  // already localised
    std::string stage_;
    double fraction_ = 0.0;
    Clock::time_point started_at_;
    std::optional<double> eta_;
    std::string result_message_;  // failure reason as given by the importer
    ImportSettings settings_;
    std::atomic<bool> cancel_requested_{false};  // Request::cancel points here; also read under the lock

    std::thread worker_;
};

}  // namespace sh::import::ui
