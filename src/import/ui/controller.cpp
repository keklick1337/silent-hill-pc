#include "import/ui/controller.h"
#include "import/ui/input_check.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <system_error>
#include <utility>

namespace sh::import::ui {

namespace fs = std::filesystem;

namespace {

Str stage_key(std::string_view stage) {
    if (stage == "read") return Str::StageRead;
    if (stage == "raw") return Str::StageRaw;
    if (stage == "convert") return Str::StageConvert;
    if (stage == "verify") return Str::StageVerify;
    if (stage == "commit") return Str::StageCommit;
    return Str::StageUnknown;
}

// The estimate is meaningless before a little work has been done.
constexpr double kEtaMinFraction = 0.01;
constexpr double kEtaMinSeconds = 1.0;

// What import_image() returns when Request::cancel stopped it (import.h).
constexpr std::string_view kCancelledMessage = "cancelled";

}  // namespace

Controller::Controller(Options options) : options_(std::move(options)), started_at_(Clock::now()) {
    input_text_ = options_.image_hint;
    settings_ = options_.settings;
}

bool apply_config_key(ImportSettings& settings, std::string_view key, std::string_view value) {
    auto flag = [&](bool& out) {
        if (value == "1" || value == "true") {
            out = true;
        } else if (value == "0" || value == "false") {
            out = false;
        } else {
            return false;
        }
        return true;
    };
    if (key == "import_overwrite") return flag(settings.overwrite);
    if (key == "import_verify") return flag(settings.verify);
    if (key == "import_threads") {
        if (value.empty() || value.size() > 4) return false;
        unsigned n = 0;
        for (char c : value) {
            if (c < '0' || c > '9') return false;
            n = n * 10 + static_cast<unsigned>(c - '0');
        }
        settings.threads = n;
        return true;
    }
    return false;
}

Controller::ImportFn Controller::make_import_fn(
    std::shared_ptr<const std::vector<std::unique_ptr<convert::Converter>>> converters) {
    return [converters = std::move(converters)](const Request& request, const ProgressFn& progress) {
        static const std::vector<std::unique_ptr<convert::Converter>> none;
        return import_image(request, converters ? *converters : none, progress);
    };
}

Controller::~Controller() {
    {
        std::lock_guard lock(mutex_);
        if (phase_ == Phase::Importing) {
            phase_ = Phase::Cancelling;
        }
        cancel_requested_ = true;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Controller::set_listener(std::function<void()> on_change) { listener_ = std::move(on_change); }

bool Controller::gamedata_ready(const fs::path& gamedata_dir) {
    std::error_code ec;
    return fs::is_regular_file(gamedata_dir / "version.cfg", ec);
}

Controller::Clock::time_point Controller::now() const { return options_.now ? options_.now() : Clock::now(); }

void Controller::start() {
    const bool ready = gamedata_ready(options_.gamedata_dir);
    {
        std::lock_guard lock(mutex_);
        if (phase_ != Phase::AskPath) {
            return;
        }
        if (ready) {
            phase_ = Phase::Ready;
        }
        changed_locked();
    }
    notify();
}

void Controller::handle(const Command& command) {
    switch (command.kind) {
    case Command::Kind::SetText:
        set_text(command.text);
        break;
    case Command::Kind::Submit:
        submit();
        break;
    case Command::Kind::Cancel:
        cancel();
        break;
    case Command::Kind::Retry:
        retry();
        break;
    case Command::Kind::Quit:
        quit();
        break;
    case Command::Kind::SetOverwrite:
        set_overwrite(command.number != 0);
        break;
    case Command::Kind::SetVerify:
        set_verify(command.number != 0);
        break;
    case Command::Kind::SetThreads:
        set_threads(command.number);
        break;
    }
}

void Controller::set_text(std::string text) {
    {
        std::lock_guard lock(mutex_);
        if (phase_ != Phase::AskPath || text == input_text_) {
            return;
        }
        input_text_ = std::move(text);
        changed_locked();
    }
    notify();
}

void Controller::set_overwrite(bool value) {
    {
        std::lock_guard lock(mutex_);
        if (phase_ != Phase::AskPath || settings_.overwrite == value) return;
        settings_.overwrite = value;
        changed_locked();
    }
    notify();
}

void Controller::set_verify(bool value) {
    {
        std::lock_guard lock(mutex_);
        if (phase_ != Phase::AskPath || settings_.verify == value) return;
        settings_.verify = value;
        changed_locked();
    }
    notify();
}

void Controller::set_threads(unsigned value) {
    {
        std::lock_guard lock(mutex_);
        if (phase_ != Phase::AskPath || settings_.threads == value) return;
        settings_.threads = value;
        changed_locked();
    }
    notify();
}

bool Controller::submit() {
    std::string text;
    {
        std::lock_guard lock(mutex_);
        if (phase_ != Phase::AskPath) {
            return false;
        }
        text = input_text_;
    }

    const ImageCheck check = check_image(text);
    Request request;

    {
        std::lock_guard lock(mutex_);
        if (phase_ != Phase::AskPath) {
            return false;
        }
        if (!check.ok()) {
            error_ = describe(options_.lang, check);
            changed_locked();
        } else {
            error_.clear();
            stage_.clear();
            fraction_ = 0.0;
            eta_.reset();
            cancel_requested_ = false;
            request.image = check.path;
            request.gamedata_dir = options_.gamedata_dir;
            request.overwrite = settings_.overwrite;
            request.verify = settings_.verify;
            request.threads = settings_.threads;
            request.cancel = &cancel_requested_;
            started_at_ = now();
            phase_ = Phase::Importing;
            final_delivered_ = false;
            changed_locked();
        }
    }
    if (!check.ok()) {
        notify();
        return false;
    }

    // The previous worker has already reached a final phase: it only has to return.
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread(&Controller::run_import, this, std::move(request));
    notify();
    return true;
}

void Controller::cancel() {
    {
        std::lock_guard lock(mutex_);
        if (phase_ != Phase::Importing) {
            return;
        }
        phase_ = Phase::Cancelling;
        cancel_requested_ = true;
        changed_locked();
    }
    notify();
}

void Controller::retry() {
    {
        std::lock_guard lock(mutex_);
        if (phase_ != Phase::Failed && phase_ != Phase::Cancelled) {
            return;
        }
        phase_ = Phase::AskPath;
        error_.clear();
        result_message_.clear();
        changed_locked();
    }
    notify();
}

void Controller::quit() {
    {
        std::lock_guard lock(mutex_);
        if (phase_ == Phase::Importing) {
            phase_ = Phase::Cancelling;
            cancel_requested_ = true;
        } else if (phase_ == Phase::AskPath || phase_ == Phase::Failed || phase_ == Phase::Cancelled) {
            phase_ = Phase::Aborted;
        } else {
            return;
        }
        changed_locked();
    }
    notify();
}

void Controller::run_import(Request request) {
    // A cancel that arrived before the thread started must not begin the import.
    bool cancelled = cancel_requested_.load();

    Result<void> result = Result<void>::ok();
    if (!cancelled) {
        const ProgressFn progress = [this](std::string_view stage, double fraction) { on_progress(stage, fraction); };
        try {
            if (options_.import) {
                result = options_.import(request, progress);
            } else {
                result = Result<void>::fail("no import function");
            }
        } catch (const std::exception& e) {
            result = Result<void>::fail(e.what());
        } catch (...) {
            result = Result<void>::fail("unknown exception");
        }
    }

    // A finished import stays Done even if the cancel arrived too late to stop it.
    if (cancelled || (!result && result.error() == kCancelledMessage)) {
        finish(Phase::Cancelled, {});
    } else if (result) {
        finish(Phase::Done, {});
    } else {
        finish(Phase::Failed, result.error());
    }
}

void Controller::on_progress(std::string_view stage, double fraction) {
    bool dirty = false;
    {
        std::lock_guard lock(mutex_);
        if (phase_ == Phase::Importing || phase_ == Phase::Cancelling) {
            if (std::isnan(fraction)) {
                fraction = fraction_;
            }
            fraction = std::clamp(fraction, fraction_, 1.0);
            if (stage != stage_ || fraction != fraction_) {
                stage_ = std::string(stage);
                fraction_ = fraction;
                const double elapsed = std::chrono::duration<double>(now() - started_at_).count();
                if (fraction_ >= kEtaMinFraction && elapsed >= kEtaMinSeconds) {
                    eta_ = elapsed * (1.0 - fraction_) / fraction_;
                } else {
                    eta_.reset();
                }
                changed_locked();
                dirty = true;
            }
        }
    }
    if (dirty) {
        notify();
    }
}

void Controller::finish(Phase phase, std::string message) {
    {
        std::lock_guard lock(mutex_);
        phase_ = phase;
        result_message_ = std::move(message);
        if (phase == Phase::Done) {
            fraction_ = 1.0;
            eta_.reset();
        }
        changed_locked();
    }
    notify();
    {
        std::lock_guard lock(mutex_);
        final_delivered_ = true;
    }
    cv_.notify_all();
}

void Controller::changed_locked() {
    ++revision_;
    cv_.notify_all();
}

void Controller::notify() {
    if (listener_) {
        listener_();
    }
}

Phase Controller::phase() const {
    std::lock_guard lock(mutex_);
    return phase_;
}

ViewModel Controller::snapshot() const {
    std::lock_guard lock(mutex_);
    return build_locked();
}

bool Controller::wait_for_change(std::uint64_t seen, std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return revision_ != seen; });
}

bool Controller::wait_final(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return is_final(phase_) && final_delivered_; });
}

ViewModel Controller::build_locked() const {
    const Lang lang = options_.lang;
    const std::string dir = options_.gamedata_dir.string();

    ViewModel v;
    v.revision = revision_;
    v.phase = phase_;
    v.settings = settings_;

    auto action = [&](ActionId id, Str label, bool enabled = true) {
        v.actions.push_back({id, std::string(text(lang, label)), enabled});
    };

    switch (phase_) {
    case Phase::Ready:
        v.title = std::string(text(lang, Str::ReadyTitle));
        v.body.push_back(format(lang, Str::ReadyBody, {dir}));
        action(ActionId::Close, Str::ActionClose);
        break;
    case Phase::AskPath:
        v.title = std::string(text(lang, Str::Title));
        v.body.push_back(format(lang, Str::IntroNeedImage, {dir}));
        v.body.push_back(std::string(text(lang, Str::IntroFormats)));
        v.has_input = true;
        v.input_label = std::string(text(lang, Str::PathLabel));
        v.input_text = input_text_;
        if (!options_.image_hint.empty()) {
            v.input_hint = format(lang, Str::PathHintPrefix, {options_.image_hint});
        }
        v.message = error_;
        v.message_is_error = !error_.empty();
        v.overwrite_label = format(lang, Str::SettingOverwrite, {"import_overwrite"});
        v.verify_label = format(lang, Str::SettingVerify, {"import_verify"});
        v.threads_label = format(lang, Str::SettingThreads, {"import_threads"});
        action(ActionId::Import, Str::ActionImport);
        action(ActionId::Quit, Str::ActionQuit);
        break;
    case Phase::Importing:
    case Phase::Cancelling:
        v.title = std::string(text(lang, phase_ == Phase::Importing ? Str::ImportingTitle : Str::CancellingTitle));
        if (phase_ == Phase::Cancelling) {
            v.body.push_back(std::string(text(lang, Str::CancellingBody)));
        }
        v.has_progress = true;
        v.stage = stage_;
        v.stage_text = stage_.empty() ? std::string(text(lang, Str::StageRead))
                                      : format(lang, stage_key(stage_), {stage_});
        v.fraction = fraction_;
        v.percent = static_cast<int>(fraction_ * 100.0);
        v.eta_seconds = eta_;
        v.eta_text = eta_ ? format(lang, Str::EtaLeft, {format_duration(lang, *eta_)})
                          : std::string(text(lang, Str::EtaUnknown));
        action(ActionId::Cancel, Str::ActionCancel, phase_ == Phase::Importing);
        break;
    case Phase::Done:
        v.title = std::string(text(lang, Str::DoneTitle));
        v.message = format(lang, Str::DoneBody, {dir});
        v.fraction = 1.0;
        v.percent = 100;
        action(ActionId::Close, Str::ActionClose);
        break;
    case Phase::Failed:
        v.title = std::string(text(lang, Str::FailedTitle));
        v.message = format(lang, Str::FailedBody,
                           {result_message_.empty() ? text(lang, Str::FailedNoReason) : std::string_view(result_message_)});
        v.message_is_error = true;
        action(ActionId::Retry, Str::ActionRetry);
        action(ActionId::Quit, Str::ActionQuit);
        break;
    case Phase::Cancelled:
        v.title = std::string(text(lang, Str::CancelledTitle));
        v.body.push_back(std::string(text(lang, Str::CancelledBody)));
        action(ActionId::Retry, Str::ActionRetry);
        action(ActionId::Quit, Str::ActionQuit);
        break;
    case Phase::Aborted:
        v.title = std::string(text(lang, Str::AbortedTitle));
        v.body.push_back(std::string(text(lang, Str::AbortedBody)));
        action(ActionId::Close, Str::ActionClose);
        break;
    }
    return v;
}

}  // namespace sh::import::ui
