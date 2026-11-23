#include "framework/sh_test.h"
#include "import/ui/controller.h"
#include "import_ui/ui_test_util.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// The controller on top of the real import_image() with an empty converter list. The tests that need the disc image are
// skipped when data/ has none.

namespace {

namespace fs = std::filesystem;
using namespace sh::import::ui;
using namespace sh::import::ui::testing;
using namespace std::chrono_literals;

constexpr auto kWait = 600s;

fs::path require_image() {
    const fs::path cue = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).cue";
    if (!fs::exists(cue) || !fs::exists(cue.parent_path() / "Silent Hill (v1.1).bin")) {
        SH_SKIP("data/Silent Hill (v1.1).bin/.cue is not present");
    }
    return cue;
}

Controller::ImportFn raw_only() {
    return Controller::make_import_fn(std::make_shared<const std::vector<std::unique_ptr<sh::convert::Converter>>>());
}

// Phases and progress seen through the listener, in order.
struct Recorder {
    std::mutex m;
    std::vector<Phase> phases;
    std::vector<std::string> stages;
    std::vector<double> fractions;

    void record(const ViewModel& v) {
        std::lock_guard lock(m);
        if (phases.empty() || phases.back() != v.phase) phases.push_back(v.phase);
        if (v.has_progress && !v.stage.empty()) {
            if (stages.empty() || stages.back() != v.stage) stages.push_back(v.stage);
            fractions.push_back(v.fraction);
        }
    }
};

}  // namespace

SH_TEST(import_ui_integration_not_a_game_image_fails_cleanly) {
    // Passes the format check (sync pattern, size) but is not Silent Hill: the real importer must refuse it.
    const fs::path dir = work_dir("int_fake");
    const fs::path image = write_file(dir / "fake.bin", 40 * kRaw, true);
    Controller::Options o;
    o.import = raw_only();
    o.gamedata_dir = dir / "gamedata";
    o.settings.threads = 2;
    Controller c(std::move(o));
    Recorder rec;
    c.set_listener([&] { rec.record(c.snapshot()); });
    c.start();
    c.set_text(image.string());
    SH_CHECK(c.submit());
    SH_CHECK(c.wait_final(kWait));

    const ViewModel v = c.snapshot();
    SH_CHECK(v.phase == Phase::Failed);
    SH_CHECK(v.message_is_error);
    SH_CHECK(v.message.size() > std::string("Reason: ").size());
    SH_CHECK(!fs::exists(dir / "gamedata"));
    SH_CHECK(!Controller::gamedata_ready(dir / "gamedata"));
    std::lock_guard lock(rec.m);
    SH_CHECK(!rec.phases.empty() && rec.phases.back() == Phase::Failed);
}

SH_TEST(import_ui_integration_real_image_imports_raw_only) {
    const fs::path cue = require_image();
    const fs::path dir = work_dir("int_real");
    Controller::Options o;
    o.import = raw_only();
    o.gamedata_dir = dir / "gamedata";
    o.settings.verify = false;
    Controller c(std::move(o));
    Recorder rec;
    c.set_listener([&] { rec.record(c.snapshot()); });
    c.start();
    SH_CHECK(c.phase() == Phase::AskPath);
    c.set_text(cue.string());
    SH_CHECK(c.submit());
    SH_CHECK(c.wait_final(kWait));

    const ViewModel v = c.snapshot();
    if (v.phase != Phase::Done) {
        std::printf("    import message: %s\n", v.message.c_str());
    }
    SH_CHECK(v.phase == Phase::Done);
    SH_CHECK(Controller::gamedata_ready(dir / "gamedata"));
    SH_CHECK(fs::file_size(dir / "gamedata" / "raw" / "SLUS_007.07") == 81920);

    std::lock_guard lock(rec.m);
    SH_CHECK((rec.phases == std::vector<Phase>{Phase::AskPath, Phase::Importing, Phase::Done}));
    SH_CHECK(!rec.fractions.empty());
    for (std::size_t i = 1; i < rec.fractions.size(); ++i) {
        SH_CHECK(rec.fractions[i] >= rec.fractions[i - 1]);
    }
    // "read" comes first and "commit" is the end of a raw-only import.
    SH_CHECK(!rec.stages.empty() && rec.stages.front() == "read");
    for (const std::string& stage : rec.stages) {
        SH_CHECK(stage == "read" || stage == "raw" || stage == "convert" || stage == "verify" || stage == "commit");
    }

    // The next start finds the data and asks nothing.
    Controller::Options again;
    again.gamedata_dir = dir / "gamedata";
    Controller c2(std::move(again));
    c2.start();
    SH_CHECK(c2.phase() == Phase::Ready);
}

SH_TEST(import_ui_integration_real_image_cancel_leaves_nothing) {
    const fs::path cue = require_image();
    const fs::path dir = work_dir("int_cancel");
    Controller::Options o;
    o.import = raw_only();
    o.gamedata_dir = dir / "gamedata";
    Controller c(std::move(o));
    Recorder rec;
    // The listener runs on the import thread inside the progress callback: the first "raw" report cancels.
    c.set_listener([&] {
        const ViewModel v = c.snapshot();
        rec.record(v);
        if (v.phase == Phase::Importing && v.stage == "raw") c.cancel();
    });
    c.start();
    c.set_text(cue.string());
    SH_CHECK(c.submit());
    SH_CHECK(c.wait_final(kWait));

    const ViewModel v = c.snapshot();
    SH_CHECK(v.phase == Phase::Cancelled);
    SH_CHECK(!fs::exists(dir / "gamedata"));
    // Only the work directory itself is left: no gamedata.tmp-* sibling.
    SH_CHECK(fs::directory_iterator(dir) == fs::directory_iterator());
    std::lock_guard lock(rec.m);
    SH_CHECK((rec.phases == std::vector<Phase>{Phase::AskPath, Phase::Importing, Phase::Cancelling, Phase::Cancelled}));
}
