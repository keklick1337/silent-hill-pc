// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "features/dump/perf_recorder.h"

#include "engine/app/pc_paths.h"
#include "engine/app/texture_upscale_run.h"
#include "engine/config/config_keys.h"
#include "engine/config/pc_config.h"
#include "engine/devtools/perf_monitor.h"
#include "engine/devtools/perf_record.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/world/cell_stream.h"
#include "features/dump/perf_sampler.h"
#include "formats/common/json.h"
#include "platform/process_stats.h"
#include "render/gpu/render_state.h"
#include "render/gpu/renderer.h"
#include "render/gpu/split_renderer.h"
#include "render/gpu/textures.h"
#include "render/scene/scene.h"
#include "render/settings/render_preset.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <vector>

#ifndef SH_GIT_REVISION
#define SH_GIT_REVISION ""
#endif

namespace sh::dump {

namespace {

namespace dt = sh::devtools;
namespace fs = std::filesystem;
namespace gs = sh::game_state;

// The recorder's own files are written in blocks: a write per frame would itself show in the frame times.
constexpr std::size_t kFlushBytes = 64 * 1024;
// Sampled timing: of every kSampleCycle windows the last kSampleOff run without per-pass timestamps. Two in a row,
// because the GPU times arrive a frame or two late and the first window after a change is mixed.
constexpr int kSampleCycle = 8;
constexpr int kSampleOff = 2;

double metres(std::int32_t q12) { return static_cast<double>(q12) / 4096.0; }
double degrees(int angle) { return static_cast<double>(angle & 0xFFF) * 360.0 / 4096.0; }

std::string local_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &tm);
    return text;
}

class File {
public:
    ~File() { close(); }
    bool open(const fs::path& path) {
        close();
        file_ = std::fopen(path.string().c_str(), "wb");
        return file_ != nullptr;
    }
    void write(const std::string& text) {
        buffer_ += text;
        if (buffer_.size() >= kFlushBytes) flush();
    }
    void flush() {
        if (file_ != nullptr && !buffer_.empty()) {
            std::fwrite(buffer_.data(), 1, buffer_.size(), file_);
            std::fflush(file_);
        }
        buffer_.clear();
    }
    void close() {
        flush();
        if (file_ != nullptr) std::fclose(file_);
        file_ = nullptr;
    }

private:
    std::FILE* file_ = nullptr;
    std::string buffer_;
};

json::Value graphics_keys() {
    // The render keys are the "Picture" block of the key table: from culling up to the sound keys.
    json::Value keys = json::Value::empty_object();
    bool inside = false;
    const s_PcConfig& c = g_PcConfig;
    for (const sh::config::Setting& s : sh::config::kSettings) {
        if (s.key == "culling") inside = true;
        if (s.key == "xa_volume") break;
        if (!inside) continue;
        if (s.whole != nullptr) {
            keys.set(s.key, json::Value::from_integer(c.*s.whole));
        } else if (s.real != nullptr) {
            keys.set(s.key, json::Value::from_real(static_cast<double>(c.*s.real)));
        } else if (s.text != nullptr) {
            s_PcConfig copy = c;
            const std::span<char> field = s.text(copy);
            keys.set(s.key, json::Value::from_string(std::string(field.data(), strnlen(field.data(), field.size()))));
        }
    }
    return keys;
}

json::Value header(const PerfRecordRequest& request, const std::string& started) {
    json::Value h = json::Value::empty_object();
    h.set("started", json::Value::from_string(started));
    h.set("revision", json::Value::from_string(SH_GIT_REVISION));
    h.set("note", json::Value::from_string(request.note));
    h.set("pass_timing", json::Value::from_string(pass_timing_name(request.timing)));
    if (sh::gpu::Renderer* renderer = sh::gpu::renderer(); renderer != nullptr) {
        const sh::rhi::Caps& caps = renderer->device().caps();
        h.set("renderer", json::Value::from_string(std::string(sh::rhi::backendName(renderer->device().backend()))));
        h.set("gpu", json::Value::from_string(caps.adapter));
        h.set("driver", json::Value::from_string(caps.driver));
    }
    h.set("window", json::Value::from_string(std::to_string(g_PcConfig.windowWidth) + "x" +
                                             std::to_string(g_PcConfig.windowHeight)));
    h.set("render_size", json::Value::from_string(std::to_string(PcConfig_RenderWidth()) + "x" +
                                                  std::to_string(PcConfig_RenderHeight())));
    h.set("render_scale", json::Value::from_real(static_cast<double>(g_PcConfig.renderScale)));
    const int preset = g_PcConfig.graphicsPreset;
    const char* preset_name = RenderPreset_Name(preset);
    h.set("preset", json::Value::from_string(preset_name != nullptr ? preset_name : ""));
    h.set("preset_matches", json::Value::from_bool(RenderPreset_Matches(preset, &g_PcConfig) != 0));
    h.set("graphics", graphics_keys());
    return h;
}

json::Value summary_json(const dt::PerfRecordSummary& s, int rows) {
    json::Value j = json::Value::empty_object();
    j.set("rows", json::Value::from_integer(rows));
    j.set("frames", json::Value::from_integer(static_cast<std::int64_t>(s.frames)));
    j.set("seconds", json::Value::from_real(s.seconds));
    j.set("fps_avg", json::Value::from_real(s.fps_avg));
    j.set("frame_ms_p50", json::Value::from_real(s.frame_ms_p50));
    j.set("frame_ms_p99", json::Value::from_real(s.frame_ms_p99));
    j.set("frame_ms_p999", json::Value::from_real(s.frame_ms_p999));
    j.set("frame_ms_max", json::Value::from_real(s.frame_ms_max));
    j.set("frame_ms_stdev", json::Value::from_real(s.frame_ms_stdev));
    j.set("jitter_ms", json::Value::from_real(s.jitter_ms));
    j.set("hitches", json::Value::from_integer(static_cast<std::int64_t>(s.hitches)));
    json::Value o = json::Value::empty_object();
    o.set("windows_with", json::Value::from_integer(s.rows_timed));
    o.set("windows_without", json::Value::from_integer(s.rows_untimed));
    o.set("gpu_ms_with", json::Value::from_real(s.gpu_ms_timed));
    o.set("gpu_ms_without", json::Value::from_real(s.gpu_ms_untimed));
    o.set("frame_ms_with", json::Value::from_real(s.frame_ms_timed));
    o.set("frame_ms_without", json::Value::from_real(s.frame_ms_untimed));
    j.set("pass_timestamps", std::move(o));
    return j;
}

std::string overhead_text(const dt::PerfRecordSummary& s) {
    char text[200];
    if (s.rows_timed == 0 || s.rows_untimed == 0) return "pass timestamps: not compared (needs timing 'sampled')";
    std::snprintf(
        text, sizeof(text), "pass timestamps cost about %+.3f ms GPU, %+.3f ms frame (%d windows with, %d without)",
        s.gpu_ms_timed - s.gpu_ms_untimed, s.frame_ms_timed - s.frame_ms_untimed, s.rows_timed, s.rows_untimed);
    return text;
}

class Recorder {
public:
    bool active() const { return core_ != nullptr; }

    Result<std::string> start(const PerfRecordRequest& request) {
        if (active()) return Result<std::string>::fail("a recording is running: " + csv_path_.string());
        const std::string started = local_stamp();
        std::string name = dt::perf_record_file_name(request.name);
        if (name.empty()) name = started;
        fs::path folder = fs::path(PcPaths_ExeDir()) / "perfrec";
        if (const std::string sub = dt::perf_record_file_name(request.folder); !sub.empty()) folder /= sub;
        std::error_code ec;
        fs::create_directories(folder, ec);
        if (ec) return Result<std::string>::fail("cannot create " + folder.string() + ": " + ec.message());
        csv_path_ = folder / (name + ".csv");
        json_path_ = folder / (name + ".json");
        if (!csv_.open(csv_path_) || !frames_.open(folder / (name + ".frames.csv"))) {
            csv_.close();
            return Result<std::string>::fail("cannot write " + csv_path_.string());
        }
        request_ = request;
        header_ = header(request, started);
        write_json(nullptr);
        core_ = std::make_unique<dt::PerfRecordCore>();
        lines_.clear();
        written_passes_ = 0;
        csv_.write(dt::perf_csv_header({}) + "\n");
        frames_.write(dt::perf_frames_header() + "\n");
        process_before_ = sh::platform::read_process_stats();
        process_frames_ = 0;
        swaps_ = sh::app::texture_upscale_swaps();
        cells_.clear();
        count_cells();
        set_pass_timing(true);
        SH_LOG_INFO(RENDER, "[PERFREC] recording to %s (pass timing %s)", csv_path_.string().c_str(),
                    pass_timing_name(request.timing));
        return Result<std::string>::ok(csv_path_.string());
    }

    Result<std::string> stop() {
        if (!active()) return Result<std::string>::fail("no recording is running");
        const dt::PerfRecordContext here = context(true);
        core_->flush();
        // The window cut short by the stop has no context yet.
        for (dt::PerfRecordRow& row : core_->rows()) {
            if (!row.hitch && row.context.room < 0 && row.context.map.empty()) row.context = here;
        }
        take_rows();
        const dt::PerfRecordSummary s = core_->summary();
        last_ = s;
        const int rows = static_cast<int>(lines_.size());
        json::Value summary = summary_json(s, rows);
        write_json(&summary);
        csv_.close();
        frames_.close();
        char text[400];
        std::snprintf(text, sizeof(text),
                      "perfrec: %s, %.1f s, %llu frames, %.1f fps; frame ms p50 %.2f, p99 %.2f, p99.9 %.2f, max %.1f; "
                      "stdev %.2f, jitter %.2f; %llu hitches",
                      csv_path_.string().c_str(), s.seconds, static_cast<unsigned long long>(s.frames), s.fps_avg,
                      s.frame_ms_p50, s.frame_ms_p99, s.frame_ms_p999, s.frame_ms_max, s.frame_ms_stdev, s.jitter_ms,
                      static_cast<unsigned long long>(s.hitches));
        std::string out = text;
        out += "\n" + overhead_text(s);
        SH_LOG_INFO(RENDER, "[PERFREC] %s", out.c_str());
        core_.reset();
        set_pass_timing(true);
        return Result<std::string>::ok(out);
    }

    const dt::PerfRecordSummary& last() const { return last_; }

    std::string status() const {
        if (!active()) return "perfrec: not recording";
        char text[300];
        std::snprintf(text, sizeof(text), "perfrec: recording %s, %.1f s, %d rows, pass timing %s",
                      csv_path_.string().c_str(), core_->time_s(), static_cast<int>(lines_.size()),
                      pass_timing_name(request_.timing));
        return text;
    }

    std::string brief() const {
        if (!active()) return {};
        const dt::PerfRecordSummary s = core_->summary();
        char text[200];
        std::snprintf(text, sizeof(text), "%.0f fps  p99 %.1f ms  p99.9 %.1f ms  jitter %.2f ms  %llu hitches",
                      s.fps_avg, s.frame_ms_p99, s.frame_ms_p999, s.jitter_ms,
                      static_cast<unsigned long long>(s.hitches));
        return text;
    }

    void frame(double frame_ms, bool held) {
        if (!active() || held) return;
        const Uint64 begun = SDL_GetPerformanceCounter();
        fill_frame(frame_ms);
        const dt::PerfRecordCore::Added added = core_->add(frame_);
        frames_.write(dt::perf_frames_line(core_->time_s(), frame_) + "\n");
        ++process_frames_;
        if (added.hitch_began) {
            if (dt::PerfRecordRow* hitch = core_->pending_hitch(); hitch != nullptr) {
                hitch->context = context(false);
            }
        }
        if (added.window_closed) {
            core_->rows().back().context = context(true);
            take_rows();
            if (request_.timing == PassTiming::Sampled) {
                set_pass_timing(core_->windows_closed() % kSampleCycle < kSampleCycle - kSampleOff);
            }
        }
        own_ms_ += static_cast<double>(SDL_GetPerformanceCounter() - begun) * 1000.0 /
                   static_cast<double>(SDL_GetPerformanceFrequency());
    }

private:
    void set_pass_timing(bool on) {
        const bool wanted = request_.timing == PassTiming::All || (request_.timing == PassTiming::Sampled && on);
        if (sh::gpu::Renderer* renderer = sh::gpu::renderer(); renderer != nullptr) {
            renderer->device().setPassTiming(active() ? wanted : true);
        }
    }

    void count_cells() {
        const sh::world::CellStream& cells = sh::world::cell_stream();
        const std::size_t count = static_cast<std::size_t>(std::max(cells.slot_count(), 0));
        int loaded = 0;
        cells_.resize(count, {sh::world::kNoFile, false});
        for (std::size_t i = 0; i < count; ++i) {
            const sh::world::CellStream::Slot& slot = cells.slot(i);
            if (slot.ready && (!cells_[i].second || cells_[i].first != slot.file)) ++loaded;
            cells_[i] = {slot.file, slot.ready};
        }
        cells_loaded_ = loaded;
    }

    void fill_frame(double frame_ms) {
        dt::PerfRecordFrame& f = frame_;
        f.frame_ms = frame_ms;
        const sh::rhi::ProfileStats& st = perf_frame_stats();
        f.gpu_ms = st.gpuMs;
        f.passes.clear();
        for (std::uint32_t i = 0; i < st.passCount && i < st.passes.size(); ++i) {
            f.passes.emplace_back(core_->pass_id(st.passes[i].name.data()), st.passes[i].ms);
        }
        f.draws = st.drawCalls;
        f.triangles = static_cast<double>(st.triangles);
        f.patch_triangles = static_cast<double>(st.patchTriangles);
        f.pipelines_built = std::max(st.pipelinesBuilt, 0);
        f.pipeline_build_ms = std::max(st.pipelineBuildMs, 0.0);
        f.texture_upload_kb = st.textureUploadBytes > 0 ? static_cast<double>(st.textureUploadBytes) / 1024.0 : 0.0;
        const unsigned long long swaps = sh::app::texture_upscale_swaps();
        f.upscale_swaps = static_cast<double>(swaps - swaps_);
        swaps_ = swaps;
        count_cells();
        f.cells_loaded = cells_loaded_;

        dt::PerfMonitor& m = dt::perf_monitor();
        static const int update = m.section("game_update");
        static const int prepare = m.section("world_list_prepare");
        static const int world = m.section("world_list_draw");
        static const int ui = m.section("ui_list_draw");
        static const int end = m.section("end_scene");
        static const int submit = m.section("render_submit_and_swap", "end_scene");
        static const int pacing = m.section("frame_pacing_wait");
        static const int swap = m.section("swap_buffers");
        const double submit_ms = m.newest(submit);
        f.cpu_update_ms = m.newest(update);
        f.cpu_render_ms = std::max(0.0, m.newest(prepare) + m.newest(world) + m.newest(ui) + m.newest(end) - submit_ms);
        f.wait_ms = m.newest(pacing) + m.newest(swap) + submit_ms;
    }

    dt::PerfRecordContext context(bool window) {
        dt::PerfRecordContext c;
        const char* map = gs::map_overlay_name();
        if (map != nullptr) {
            c.map = map;
            c.room = gs::save_room();
            const sh::entities::Vec3 at = gs::chara_position(0);
            c.player = {metres(at.x), metres(at.y), metres(at.z)};
            c.player_yaw = degrees(gs::chara_rotation(0).y);
            const gs::CameraDebug cam = gs::camera_debug();
            c.camera = {metres(cam.position[0]), metres(cam.position[1]), metres(cam.position[2])};
            c.camera_yaw = degrees(cam.yaw);
            c.cutscene = gs::game_state() != gs::GameState::InGame || gs::sys_state() != gs::SysState::Gameplay;
        }
        c.rt = sh::render::graphics().ray_tracing != 0;
        if (sh::gpu::Renderer* renderer = sh::gpu::renderer(); renderer != nullptr) {
            c.uploads_pending = static_cast<int>(renderer->textures().pending_uploads());
            sh::gpu::SplitRenderer& splits = renderer->splits();
            if (sh::scene::SceneRenderer* scene = splits.stats().scenes > 0 ? splits.scene_renderer() : nullptr;
                scene != nullptr) {
                c.lights = static_cast<int>(scene->stats().lights);
            }
        }
        const sh::rhi::ProfileStats& st = perf_frame_stats();
        if (st.appVramKb >= 0) c.vram_mb = static_cast<double>(st.appVramKb) / 1024.0;
        if (window) {
            const sh::platform::ProcessStats now = sh::platform::read_process_stats();
            if (now.threadCpuSec >= 0.0 && process_before_.threadCpuSec >= 0.0 && process_frames_ > 0) {
                c.cpu_main_ms = (now.threadCpuSec - process_before_.threadCpuSec) * 1000.0 / process_frames_;
            }
            process_before_ = now;
            process_frames_ = 0;
            const sh::platform::GpuUsage usage = sh::platform::read_gpu_usage();
            if (usage.busyPercent >= 0) c.gpu_busy_pct = usage.busyPercent;
        }
        return c;
    }

    void take_rows() {
        std::vector<dt::PerfRecordRow>& rows = core_->rows();
        const std::size_t passes = core_->pass_names().size();
        if (passes != written_passes_) {
            // A pass seen for the first time is a new column: the header is rewritten with the rows so far.
            csv_.close();
            csv_.open(csv_path_);
            csv_.write(dt::perf_csv_header(core_->pass_names()) + "\n");
            for (const std::string& line : lines_) csv_.write(line);
        }
        written_passes_ = passes;
        for (const dt::PerfRecordRow& row : rows) {
            lines_.push_back(dt::perf_csv_line(row) + "\n");
            csv_.write(lines_.back());
        }
        rows.clear();
        // About once a second the rows reach the disk, so a crash keeps them.
        if (++windows_since_flush_ >= 4) {
            windows_since_flush_ = 0;
            csv_.flush();
            frames_.flush();
        }
    }

    void write_json(const json::Value* summary) {
        json::Value doc = header_;
        if (summary != nullptr) {
            doc.set("summary", *summary);
            doc.set("recorder_cpu_ms", json::Value::from_real(own_ms_));
            if (core_ != nullptr) {
                json::Value passes = json::Value::empty_array();
                for (const std::string& p : core_->pass_names())
                    passes.array_mut().push_back(json::Value::from_string(p));
                doc.set("passes", std::move(passes));
            }
        }
        if (std::FILE* f = std::fopen(json_path_.string().c_str(), "wb"); f != nullptr) {
            const std::string text = json::write(doc, true) + "\n";
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
    }

    std::unique_ptr<dt::PerfRecordCore> core_;
    PerfRecordRequest request_;
    json::Value header_;
    fs::path csv_path_, json_path_;
    File csv_, frames_;
    std::vector<std::string> lines_;
    std::size_t written_passes_ = 0;
    dt::PerfRecordFrame frame_;
    sh::platform::ProcessStats process_before_;
    int process_frames_ = 0;
    unsigned long long swaps_ = 0;
    std::vector<std::pair<std::int32_t, bool>> cells_;
    int cells_loaded_ = 0;
    int windows_since_flush_ = 0;
    double own_ms_ = 0.0;
    dt::PerfRecordSummary last_;
};

Recorder& recorder() {
    static Recorder r;
    return r;
}

void stop_at_exit() {
    if (recorder().active()) (void)recorder().stop();
}

}  // namespace

Result<std::string> perf_record_start(const PerfRecordRequest& request) {
    // Made before the exit hook is registered, so the hook runs while the recorder still exists.
    Recorder& r = recorder();
    static bool exit_hook = false;
    if (!exit_hook) exit_hook = std::atexit(stop_at_exit) == 0;
    return r.start(request);
}

Result<std::string> perf_record_stop() { return recorder().stop(); }
bool perf_record_active() { return recorder().active(); }
const dt::PerfRecordSummary& perf_record_last_summary() { return recorder().last(); }
std::string perf_record_status() { return recorder().status(); }
std::string perf_record_brief() { return recorder().brief(); }

void perf_record_frame(double frame_ms, bool held) {
    static bool asked = false;
    if (!asked) {
        asked = true;
        if (g_PcConfig.perfRecord != 0 && !recorder().active()) {
            if (auto r = perf_record_start({}); !r)
                SH_LOG_WARN(RENDER, "[PERFREC] perf_record = 1: %s", r.error().c_str());
        }
    }
    recorder().frame(frame_ms, held);
}

PassTiming pass_timing_from_name(std::string_view name, bool* known) {
    if (known != nullptr) *known = true;
    if (name == "all" || name == "passes") return PassTiming::All;
    if (name == "off" || name == "total") return PassTiming::Off;
    if (known != nullptr) *known = name.empty() || name == "sampled";
    return PassTiming::Sampled;
}

const char* pass_timing_name(PassTiming timing) {
    switch (timing) {
        case PassTiming::All:
            return "all";
        case PassTiming::Off:
            return "off";
        case PassTiming::Sampled:
            break;
    }
    return "sampled";
}

}  // namespace sh::dump
