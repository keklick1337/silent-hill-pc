// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands that write what the game shows or holds to files: shot, shotwin, shotseq, vramdump,
// dump.

#include "engine/debug/command_services.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/frame_capture.h"
#include "engine/debug/commands/frame_image.h"
#include "engine/debug/overlay/debug_overlay.h"
#include "engine/platform/window.h"
#include "features/dump/game_dump.h"
#include "render/gpu/device_host.h"
#include "render/gpu/renderer.h"
#include "render/gpu/split_renderer.h"
#include "render/gpu/targets.h"

#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace sh::debug {

namespace {

using namespace sh::debug::commands;

constexpr std::size_t kLongestPath = 511;

// The renderer's composed frame; empty when there is none or it cannot be read back.
enum class GrabFailure { None, NoFrame, ReadFailed };

GrabFailure grab_frame(FrameImage& image) {
    sh::gpu::Renderer* renderer = sh::gpu::renderer();
    int width = 0, height = 0;
    if (renderer == nullptr || !renderer->frame_size(&width, &height) || width <= 0 || height <= 0) {
        return GrabFailure::NoFrame;
    }
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    if (!renderer->read_frame_rgba(image.rgba.data(), width * height * 4)) return GrabFailure::ReadFailed;
    return GrabFailure::None;
}

// `shotseq`: the frames are kept in memory and encoded after the last one, so writing PNGs does not slow the
// frames being captured.
class ShotSequence {
public:
    void start(std::string prefix, int frames) {
        prefix_ = std::move(prefix);
        left_ = frames;
        frames_.clear();
        notes_.clear();
    }

    void frame(const char* note) {
        if (left_ <= 0) return;
        FrameImage image;
        if (grab_frame(image) != GrabFailure::None) return;
        frames_.push_back(std::move(image));
        notes_.emplace_back(note != nullptr ? note : "");
        if (--left_ == 0) write();
    }

private:
    void write() {
        std::FILE* index = std::fopen((prefix_ + ".txt").c_str(), "w");
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            char number[16];
            std::snprintf(number, sizeof(number), "%03d", static_cast<int>(i));
            frames_[i].make_opaque();
            (void)write_png(frames_[i], prefix_ + "_" + number + ".png");
            if (index != nullptr) std::fprintf(index, "%s %s\n", number, notes_[i].c_str());
        }
        if (index != nullptr) std::fclose(index);
        char line[kLineLimit + 1];
        std::snprintf(line, sizeof(line), "shotseq: %d frames at %s_NNN.png", static_cast<int>(frames_.size()),
                      prefix_.c_str());
        console_print(line);
        frames_.clear();
        notes_.clear();
    }

    std::string prefix_;
    int left_ = 0;
    std::vector<FrameImage> frames_;
    std::vector<std::string> notes_;
};

ShotSequence& shot_sequence() {
    static ShotSequence sequence;
    return sequence;
}

RunFn shot_command(bool whole_window) {
    return [whole_window](Context& ctx, const Args& args) {
        say_text(ctx, save_screenshot(args.joined(0), whole_window));
        return done();
    };
}

}  // namespace

std::string save_screenshot(const std::string& path, bool whole_window) {
    FrameImage image;
    switch (grab_frame(image)) {
        case GrabFailure::NoFrame: return "shot: no frame to capture";
        case GrabFailure::ReadFailed: return "shot: frame read failed";
        case GrabFailure::None: break;
    }
    image.make_opaque();

    if (whole_window) {
        sh::gpu::Renderer* renderer = sh::gpu::renderer();
        int window_w = 0, window_h = 0, left = 0, top = 0, width = 0, height = 0;
        renderer->host().drawable_size(&window_w, &window_h);
        renderer->targets().present_rect(&left, &top, &width, &height);
        image = place_in_window(image, window_w, window_h, left, top, width, height);
        if (image.empty()) return "shotwin: no window to capture";
    }

    const std::string file =
        path.empty() ? "screenshot-" + std::to_string(static_cast<unsigned>(SDL_GetTicks())) + ".png"
                     : path.substr(0, kLongestPath);
    const Result<std::size_t> written = write_png(image, file);
    if (!written) return "shot: " + written.error();
    return "shot: " + file + " (" + std::to_string(image.width) + "x" + std::to_string(image.height) + ", " +
           std::to_string(written.value()) + " bytes)";
}

void shot_sequence_frame(const char* note) { shot_sequence().frame(note); }

namespace commands {

void register_capture_commands(Registry& registry) {
    add(registry, "shot", "system", "save a screenshot (PNG)", {arg("file", ArgType::String, true, "output path")},
        shot_command(false), {"screenshot"});
    add(registry, "shotwin", "system",
        "save a screenshot of the whole window (PNG): the frame where the present places it",
        {arg("file", ArgType::String, true, "output path")}, shot_command(true));

    add(registry, "shotseq", "debug", "save every presented frame as <prefix>_NNN.png with a <prefix>.txt index",
        {arg("prefix", ArgType::String, false, "output path prefix"), arg("frames", ArgType::Int, false, "1..400")},
        [](Context& ctx, const Args& args) {
            const std::vector<std::string> words = spaced_words(args);
            const int frames = words.size() >= 2 ? whole(words[1]) : 0;
            if (frames <= 0 || frames > 400) {
                say(ctx, "shotseq <path prefix> <frames 1..400>: every presented frame from the next one on");
                return done();
            }
            shot_sequence().start(words[0].substr(0, kLongestPath), frames);
            return done();
        });

    add(registry, "vramdump", "debug", "write the video memory (1024x512) to a TGA",
        {arg("file", ArgType::String, true, "output path, default VRAM.TGA")}, [](Context& ctx, const Args& args) {
            const std::string typed = args.joined(0);
            const char* path = typed.empty() ? "VRAM.TGA" : typed.c_str();
            say(ctx, sh::platform::save_vram_tga(path) ? "VRAM written to %s" : "vramdump: cannot write %s", path);
            return done();
        });

    // The state dump of a bug report: a folder with dump.json, shot.png and state.sav under a short id.
    add(registry, "dump", "debug",
        "snapshot the situation into dumps/<id>/ (dump.json, shot.png, state.sav), or restore one",
        {arg("note", ArgType::Rest, true, "a note for the report, or: load <id>")},
        [](Context& ctx, const Args& args) -> Result<void> {
            const std::string text = args.joined(0);
            char id[32] = {0};
            char extra = 0;
            if (std::sscanf(text.c_str(), "load %31s %c", id, &extra) == 1) {
                auto folder = sh::dump::load(id);
                if (!folder) return Result<void>::fail(folder.error());
                ctx.out.info(std::string("dump ") + id + " is being restored from " + folder.value());
                return done();
            }
            auto written = sh::dump::write(text);
            if (!written) return Result<void>::fail(written.error());
            ctx.out.info("dump " + written.value().id + " written to " + written.value().folder);
            return done();
        },
        {}, "dump [note...]   writes <exe dir>/dumps/<id>/ and prints the id\ndump load <id>   restores that dump");
}

}  // namespace commands
}  // namespace sh::debug
