#include "framework/sh_test.h"
#include "import/ui/controller.h"
#include "import_ui/ui_test_util.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// SH_CHECK is not thread-safe: threads started here only store results in atomics, and the checks run on the test thread.

namespace {

namespace fs = std::filesystem;
using namespace sh::import::ui;
using namespace sh::import::ui::testing;
using namespace std::chrono_literals;

using sh::Result;
using sh::import::ProgressFn;
using sh::import::Request;

constexpr auto kWait = 10s;

struct Fixture {
    fs::path dir;
    fs::path image;
    fs::path gamedata;

    explicit Fixture(const std::string& name) {
        dir = work_dir(name);
        image = write_file(dir / "game.bin", 8 * kRaw, true);
        gamedata = dir / "gamedata";
    }
};

Controller::Options options_for(const Fixture& f, Controller::ImportFn fn, std::string hint = {}) {
    Controller::Options o;
    o.import = std::move(fn);
    o.gamedata_dir = f.gamedata;
    o.image_hint = std::move(hint);
    o.lang = Lang::En;
    return o;
}

// Spins until the predicate holds; false after the timeout.
template <typename Pred>
bool spin_until(Pred pred, std::chrono::milliseconds limit = kWait) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

}  // namespace

SH_TEST(import_ui_controller_ready_when_version_cfg_exists) {
    Fixture f("ctl_ready");
    fs::create_directories(f.gamedata);
    // The directory alone is not enough: the importer writes version.cfg last.
    {
        Controller c(options_for(f, nullptr));
        c.start();
        SH_CHECK(c.phase() == Phase::AskPath);
    }
    write_text(f.gamedata / "version.cfg", "importer = 1\n");
    Controller c(options_for(f, nullptr));
    c.start();
    const ViewModel v = c.snapshot();
    SH_CHECK(v.phase == Phase::Ready);
    SH_CHECK(is_final(v.phase));
    SH_CHECK(!v.has_input);
    SH_CHECK(v.actions.size() == 1 && v.actions[0].id == ActionId::Close);
    SH_CHECK(c.wait_final(0ms));
}

SH_TEST(import_ui_controller_asks_path_with_hint) {
    Fixture f("ctl_ask");
    Controller c(options_for(f, nullptr, "/some/where.cue"));
    c.start();
    const ViewModel v = c.snapshot();
    SH_CHECK(v.phase == Phase::AskPath);
    SH_CHECK(v.has_input);
    SH_CHECK(v.input_text == "/some/where.cue");
    SH_CHECK(v.input_hint.find("/some/where.cue") != std::string::npos);
    SH_CHECK(v.body.size() == 2);
    SH_CHECK(v.body[0].find(f.gamedata.string()) != std::string::npos);
    SH_CHECK(v.message.empty());
    SH_CHECK(v.actions.size() == 2 && v.actions[0].id == ActionId::Import && v.actions[1].id == ActionId::Quit);

    Controller no_hint(options_for(f, nullptr));
    no_hint.start();
    SH_CHECK(no_hint.snapshot().input_hint.empty());
    SH_CHECK(no_hint.snapshot().input_text.empty());
}

SH_TEST(import_ui_controller_bad_path_keeps_asking) {
    Fixture f("ctl_bad");
    std::atomic<int> calls{0};
    Controller c(options_for(f, [&](const Request&, const ProgressFn&) {
        ++calls;
        return Result<void>::ok();
    }));
    c.start();

    SH_CHECK(!c.submit());  // empty
    ViewModel v = c.snapshot();
    SH_CHECK(v.phase == Phase::AskPath && v.message_is_error && !v.message.empty());

    c.set_text((f.dir / "missing.bin").string());
    SH_CHECK(!c.submit());
    v = c.snapshot();
    SH_CHECK(v.message.find("missing.bin") != std::string::npos);

    c.set_text(write_file(f.dir / "cooked.iso", 10 * kCooked, false).string());
    SH_CHECK(!c.submit());
    SH_CHECK(c.snapshot().message.find("2048") != std::string::npos);
    SH_CHECK(c.phase() == Phase::AskPath);
    SH_CHECK(calls == 0);

    // Typing a good path afterwards works, and the error is gone once the import starts.
    c.set_text(f.image.string());
    SH_CHECK(c.submit());
    SH_CHECK(c.wait_final(kWait));
    SH_CHECK(calls == 1);
    SH_CHECK(c.snapshot().message.find(f.gamedata.string()) != std::string::npos);
}

SH_TEST(import_ui_controller_success) {
    Fixture f("ctl_ok");
    Request got;
    Controller::Options o = options_for(f, [&](const Request& request, const ProgressFn& progress) {
        got = request;
        progress("read", 0.0);
        progress("raw", 0.4);
        progress("convert", 0.8);
        progress("commit", 1.0);
        return Result<void>::ok();
    });
    o.settings.overwrite = true;
    o.settings.verify = false;
    o.settings.threads = 3;
    Controller c(std::move(o));
    c.start();
    c.set_text(f.image.string());
    SH_CHECK(c.submit());
    SH_CHECK(c.wait_final(kWait));

    const ViewModel v = c.snapshot();
    SH_CHECK(v.phase == Phase::Done);
    SH_CHECK(!v.message_is_error);
    SH_CHECK(v.fraction == 1.0 || !v.has_progress);
    SH_CHECK(got.image == f.image);
    SH_CHECK(got.gamedata_dir == f.gamedata);
    SH_CHECK(got.overwrite && !got.verify && got.threads == 3);
    SH_CHECK(got.cancel != nullptr && got.report == nullptr);
    SH_CHECK(v.actions.size() == 1 && v.actions[0].id == ActionId::Close);
    // Nothing more happens after the end.
    SH_CHECK(!c.submit());
    c.cancel();
    c.retry();
    SH_CHECK(c.phase() == Phase::Done);
}

SH_TEST(import_ui_controller_progress_view) {
    Fixture f("ctl_progress");
    std::atomic<std::int64_t> clock_ms{0};
    std::atomic<int> step{0};
    Controller::Options o = options_for(f, [&](const Request&, const ProgressFn& progress) {
        progress("raw", 0.0);
        step = 1;
        while (step != 2) std::this_thread::sleep_for(1ms);
        progress("convert", 0.25);
        step = 3;
        while (step != 4) std::this_thread::sleep_for(1ms);
        return Result<void>::ok();
    });
    o.now = [&] { return Controller::Clock::time_point(std::chrono::milliseconds(clock_ms.load())); };
    Controller c(std::move(o));
    c.start();
    c.set_text(f.image.string());
    SH_CHECK(c.submit());

    SH_CHECK(spin_until([&] { return step == 1; }));
    ViewModel v = c.snapshot();
    SH_CHECK(v.phase == Phase::Importing);
    SH_CHECK(v.has_progress && v.stage == "raw" && v.percent == 0);
    SH_CHECK(!v.eta_seconds.has_value());
    SH_CHECK(!v.eta_text.empty());
    SH_CHECK(v.actions.size() == 1 && v.actions[0].id == ActionId::Cancel && v.actions[0].enabled);

    clock_ms = 30000;  // 25% in 30 s: 90 s left
    step = 2;
    SH_CHECK(spin_until([&] { return step == 3; }));
    v = c.snapshot();
    SH_CHECK(v.stage == "convert" && v.percent == 25);
    SH_CHECK(v.eta_seconds.has_value() && std::abs(*v.eta_seconds - 90.0) < 0.01);
    SH_CHECK(v.eta_text.find("1 min 30 s") != std::string::npos);
    SH_CHECK(v.stage_text == "Converting resources");
    step = 4;
    SH_CHECK(c.wait_final(kWait));
}

SH_TEST(import_ui_controller_progress_is_monotonic_and_sanitised) {
    Fixture f("ctl_clamp");
    std::vector<double> seen;
    std::mutex m;
    Controller c(options_for(f, [&](const Request&, const ProgressFn& progress) {
        progress("raw", 0.5);
        progress("raw", 0.2);  // goes back: ignored
        progress("raw", std::nan(""));
        progress("raw", 7.0);  // out of range: clamped
        return Result<void>::ok();
    }));
    c.set_listener([&] {
        std::lock_guard lock(m);
        seen.push_back(c.snapshot().fraction);
    });
    c.start();
    c.set_text(f.image.string());
    SH_CHECK(c.submit());
    SH_CHECK(c.wait_final(kWait));
    std::lock_guard lock(m);
    for (std::size_t i = 1; i < seen.size(); ++i) {
        SH_CHECK(seen[i] >= seen[i - 1]);
    }
    for (double x : seen) {
        SH_CHECK(x >= 0.0 && x <= 1.0);
    }
    SH_CHECK(c.snapshot().phase == Phase::Done);
}

SH_TEST(import_ui_controller_failure_and_retry) {
    Fixture f("ctl_fail");
    std::atomic<int> calls{0};
    Controller c(options_for(f, [&](const Request&, const ProgressFn& progress) {
        progress("read", 0.1);
        if (++calls == 1) {
            return Result<void>::fail("sha256 mismatch");
        }
        return Result<void>::ok();
    }));
    c.start();
    c.set_text(f.image.string());
    SH_CHECK(c.submit());
    SH_CHECK(c.wait_final(kWait));

    ViewModel v = c.snapshot();
    SH_CHECK(v.phase == Phase::Failed);
    SH_CHECK(v.message_is_error);
    SH_CHECK(v.message.find("sha256 mismatch") != std::string::npos);
    SH_CHECK(v.actions.size() == 2 && v.actions[0].id == ActionId::Retry && v.actions[1].id == ActionId::Quit);

    c.retry();
    v = c.snapshot();
    SH_CHECK(v.phase == Phase::AskPath);
    SH_CHECK(v.input_text == f.image.string());
    SH_CHECK(v.message.empty());

    SH_CHECK(c.submit());
    SH_CHECK(c.wait_final(kWait));
    SH_CHECK(c.phase() == Phase::Done);
    SH_CHECK(calls == 2);
}

SH_TEST(import_ui_controller_exceptions_become_failure) {
    Fixture f("ctl_throw");
    Controller c(options_for(f, [&](const Request&, const ProgressFn&) -> Result<void> {
        throw std::runtime_error("disk exploded");
    }));
    c.start();
    c.set_text(f.image.string());
    SH_CHECK(c.submit());
    SH_CHECK(c.wait_final(kWait));
    const ViewModel v = c.snapshot();
    SH_CHECK(v.phase == Phase::Failed);
    SH_CHECK(v.message.find("disk exploded") != std::string::npos);

    Controller none(options_for(f, nullptr));
    none.start();
    none.set_text(f.image.string());
    SH_CHECK(none.submit());
    SH_CHECK(none.wait_final(kWait));
    SH_CHECK(none.phase() == Phase::Failed);
}

SH_TEST(import_ui_controller_cancel_sets_the_token) {
    Fixture f("ctl_cancel");
    std::atomic<int> step{0};
    std::atomic<bool> token_seen_before{true};
    Controller c(options_for(f, [&](const Request& request, const ProgressFn& progress) {
        token_seen_before = request.cancel->load();
        progress("raw", 0.1);
        step = 1;
        while (!request.cancel->load()) std::this_thread::sleep_for(1ms);
        progress("raw", 0.2);  // reports are still accepted while the import winds down
        return Result<void>::fail("cancelled");
    }));
    c.start();
    c.set_text(f.image.string());
    SH_CHECK(c.submit());
    SH_CHECK(spin_until([&] { return step == 1; }));

    c.cancel();
    ViewModel v = c.snapshot();
    SH_CHECK(v.phase == Phase::Cancelling);
    SH_CHECK(v.actions.size() == 1 && v.actions[0].id == ActionId::Cancel && !v.actions[0].enabled);
    SH_CHECK(!v.body.empty());
    c.cancel();  // second cancel is harmless
    SH_CHECK(c.wait_final(kWait));
    SH_CHECK(!token_seen_before);

    v = c.snapshot();
    SH_CHECK(v.phase == Phase::Cancelled);
    SH_CHECK(v.actions.size() == 2 && v.actions[0].id == ActionId::Retry);

    c.retry();
    SH_CHECK(c.phase() == Phase::AskPath);
}

SH_TEST(import_ui_controller_token_is_clear_again_after_retry) {
    Fixture f("ctl_token_reset");
    std::atomic<int> calls{0};
    std::atomic<bool> second_started_cancelled{true};
    Controller c(options_for(f, [&](const Request& request, const ProgressFn&) {
        if (++calls == 1) {
            while (!request.cancel->load()) std::this_thread::sleep_for(1ms);
            return Result<void>::fail("cancelled");
        }
        second_started_cancelled = request.cancel->load();
        return Result<void>::ok();
    }));
    c.start();
    c.set_text(f.image.string());
    SH_CHECK(c.submit());
    SH_CHECK(spin_until([&] { return calls == 1; }));
    c.cancel();
    SH_CHECK(c.wait_final(kWait));
    SH_CHECK(c.phase() == Phase::Cancelled);
    c.retry();
    SH_CHECK(c.submit());
    SH_CHECK(c.wait_final(kWait));
    SH_CHECK(c.phase() == Phase::Done);
    SH_CHECK(!second_started_cancelled);
}

SH_TEST(import_ui_controller_error_after_cancel_is_failure_unless_cancelled) {
    Fixture f("ctl_cancel_err");
    std::atomic<int> step{0};
    Controller c(options_for(f, [&](const Request&, const ProgressFn&) {
        step = 1;
        while (step != 2) std::this_thread::sleep_for(1ms);
        return Result<void>::fail("disk full");
    }));
    c.start();
    c.set_text(f.image.string());
    SH_CHECK(c.submit());
    SH_CHECK(spin_until([&] { return step == 1; }));
    c.cancel();
    step = 2;
    SH_CHECK(c.wait_final(kWait));
    SH_CHECK(c.phase() == Phase::Failed);
    SH_CHECK(c.snapshot().message.find("disk full") != std::string::npos);
}

SH_TEST(import_ui_controller_cancel_too_late_keeps_success) {
    Fixture f("ctl_cancel_late");
    std::atomic<int> step{0};
    Controller c(options_for(f, [&](const Request&, const ProgressFn&) {
        step = 1;
        while (step != 2) std::this_thread::sleep_for(1ms);
        return Result<void>::ok();
    }));
    c.start();
    c.set_text(f.image.string());
    SH_CHECK(c.submit());
    SH_CHECK(spin_until([&] { return step == 1; }));
    c.cancel();
    step = 2;
    SH_CHECK(c.wait_final(kWait));
    SH_CHECK(c.phase() == Phase::Done);
}

SH_TEST(import_ui_controller_settings_model) {
    Fixture f("ctl_settings");
    Request got;
    Controller c(options_for(f, [&](const Request& request, const ProgressFn&) {
        got = request;
        return Result<void>::ok();
    }));
    c.start();
    ViewModel v = c.snapshot();
    SH_CHECK(!v.settings.overwrite && v.settings.verify && v.settings.threads == 0);
    SH_CHECK(!v.overwrite_label.empty() && !v.verify_label.empty() && !v.threads_label.empty());

    const std::uint64_t rev = v.revision;
    c.handle({Command::Kind::SetOverwrite, {}, 1});
    c.handle({Command::Kind::SetVerify, {}, 0});
    c.handle({Command::Kind::SetThreads, {}, 6});
    v = c.snapshot();
    SH_CHECK(v.settings.overwrite && !v.settings.verify && v.settings.threads == 6);
    SH_CHECK(v.revision > rev);

    c.set_text(f.image.string());
    SH_CHECK(c.submit());
    SH_CHECK(c.wait_final(kWait));
    SH_CHECK(got.overwrite && !got.verify && got.threads == 6);

    // Changes after the start are ignored.
    c.set_overwrite(false);
    SH_CHECK(c.snapshot().settings.overwrite);
}

SH_TEST(import_ui_config_keys) {
    ImportSettings s;
    SH_CHECK(apply_config_key(s, "import_overwrite", "1") && s.overwrite);
    SH_CHECK(apply_config_key(s, "import_overwrite", "false") && !s.overwrite);
    SH_CHECK(apply_config_key(s, "import_verify", "0") && !s.verify);
    SH_CHECK(apply_config_key(s, "import_verify", "true") && s.verify);
    SH_CHECK(apply_config_key(s, "import_threads", "12") && s.threads == 12);
    SH_CHECK(apply_config_key(s, "import_threads", "0") && s.threads == 0);
    SH_CHECK(!apply_config_key(s, "import_threads", "-1"));
    SH_CHECK(!apply_config_key(s, "import_threads", ""));
    SH_CHECK(!apply_config_key(s, "import_verify", "maybe"));
    SH_CHECK(!apply_config_key(s, "import_textures", "png"));
}

SH_TEST(import_ui_controller_quit) {
    Fixture f("ctl_quit");
    Controller c(options_for(f, nullptr));
    c.start();
    c.quit();
    const ViewModel v = c.snapshot();
    SH_CHECK(v.phase == Phase::Aborted);
    SH_CHECK(!v.body.empty());
    SH_CHECK(!c.submit());
    c.set_text("x");
    SH_CHECK(c.snapshot().phase == Phase::Aborted);
}

SH_TEST(import_ui_controller_quit_while_importing_cancels) {
    Fixture f("ctl_quit_busy");
    Controller c(options_for(f, [&](const Request& request, const ProgressFn& progress) {
        for (int i = 0; i < 5000; ++i) {
            if (request.cancel->load()) return Result<void>::fail("cancelled");
            progress("raw", i / 5000.0);
            std::this_thread::sleep_for(1ms);
        }
        return Result<void>::ok();
    }));
    c.start();
    c.set_text(f.image.string());
    SH_CHECK(c.submit());
    c.handle({Command::Kind::Quit, {}});
    SH_CHECK(c.wait_final(kWait));
    SH_CHECK(c.phase() == Phase::Cancelled);
}

SH_TEST(import_ui_controller_destructor_cancels_running_import) {
    Fixture f("ctl_dtor");
    std::atomic<bool> started{false};
    std::atomic<bool> finished_normally{false};
    {
        Controller c(options_for(f, [&](const Request& request, const ProgressFn& progress) {
            started = true;
            for (int i = 0; i < 100000; ++i) {
                if (request.cancel->load()) return Result<void>::fail("cancelled");
                progress("raw", i / 100000.0);
                std::this_thread::sleep_for(1ms);
            }
            finished_normally = true;
            return Result<void>::ok();
        }));
        c.start();
        c.set_text(f.image.string());
        SH_CHECK(c.submit());
        SH_CHECK(spin_until([&] { return started.load(); }));
    }
    SH_CHECK(!finished_normally);
}

SH_TEST(import_ui_controller_commands_and_listener) {
    Fixture f("ctl_commands");
    std::atomic<int> notified{0};
    Controller c(options_for(f, [&](const Request&, const ProgressFn& progress) {
        progress("verify", 0.9);
        return Result<void>::ok();
    }));
    c.set_listener([&] { ++notified; });
    c.start();
    const std::uint64_t rev = c.snapshot().revision;
    c.handle({Command::Kind::SetText, f.image.string()});
    SH_CHECK(c.snapshot().revision > rev);
    c.handle({Command::Kind::Submit, {}});
    SH_CHECK(c.wait_final(kWait));
    SH_CHECK(notified >= 3);
    SH_CHECK(!c.wait_for_change(c.snapshot().revision, 20ms));
}

// Several threads drive one controller while the import reports progress from its thread. Meant for a ThreadSanitizer
// build; without one it still checks that the snapshots stay consistent.
SH_TEST(import_ui_controller_threaded_stress) {
    Fixture f("ctl_stress");
    constexpr int kRounds = 40;
    std::atomic<int> bad{0};

    for (int round = 0; round < kRounds; ++round) {
        std::atomic<bool> stop{false};
        Controller c(options_for(f, [&](const Request& request, const ProgressFn& progress) {
            for (int i = 0; i <= 200; ++i) {
                if (request.cancel->load()) return Result<void>::fail("cancelled");
                progress(i < 100 ? "raw" : "convert", i / 200.0);
            }
            return round % 3 == 0 ? Result<void>::fail("boom") : Result<void>::ok();
        }));
        c.set_listener([&] {
            const ViewModel v = c.snapshot();
            if (v.percent < 0 || v.percent > 100) ++bad;
        });
        c.start();
        c.set_text(f.image.string());

        std::vector<std::thread> pool;
        for (int t = 0; t < 4; ++t) {
            pool.emplace_back([&, t] {
                std::uint64_t last_revision = 0;
                double last_fraction = 0.0;
                Phase last_phase = Phase::AskPath;
                while (!stop) {
                    const ViewModel v = c.snapshot();
                    if (v.revision < last_revision) ++bad;
                    last_revision = v.revision;
                    if (v.phase == last_phase && v.has_progress && v.fraction < last_fraction) ++bad;
                    last_phase = v.phase;
                    last_fraction = v.has_progress ? v.fraction : 0.0;
                    if (v.has_progress && v.eta_text.empty()) ++bad;
                    if (t == 1) c.submit();
                    if (t == 2 && (round % 4 == 1)) c.cancel();
                    c.wait_for_change(v.revision, 1ms);
                }
            });
        }
        c.submit();
        const bool finished = c.wait_final(kWait);
        stop = true;
        for (std::thread& t : pool) t.join();
        SH_CHECK(finished);
        const Phase p = c.phase();
        SH_CHECK(p == Phase::Done || p == Phase::Failed || p == Phase::Cancelled);
    }
    SH_CHECK(bad == 0);
}
