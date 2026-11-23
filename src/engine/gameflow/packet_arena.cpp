// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gameflow/packet_arena.h"

#include <cstdlib>
#include <cstring>

#include "formats/common/json.h"

namespace sh::gameflow {

PacketArenaParams PacketArenaParams::builtin() {
    return {};
}

namespace {

bool read_uint(const json::Value& doc, const char* key, std::int64_t lo, std::int64_t hi, std::uint32_t& out,
               std::string& err) {
    const json::Value* v = doc.find(key);
    if (!v) {
        return true;
    }
    if (!v->is_integer() || v->integer() < lo || v->integer() > hi) {
        err = std::string("packet_arena.json: \"") + key + "\" must be an integer from " + std::to_string(lo) +
              " to " + std::to_string(hi);
        return false;
    }
    out = static_cast<std::uint32_t>(v->integer());
    return true;
}

}  // namespace

Result<PacketArenaParams> PacketArenaParams::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<PacketArenaParams>::fail("packet_arena.json: " + doc.error());
    }
    PacketArenaParams out = builtin();
    std::string err;
    if (!read_uint(doc.value(), "size_bytes", 4096, 1 << 30, out.size_bytes, err) ||
        !read_uint(doc.value(), "canary_bytes", 1, 4096, out.canary_bytes, err) ||
        !read_uint(doc.value(), "canary_value", 0, 255, out.canary_value, err) ||
        !read_uint(doc.value(), "stats_window_frames", 1, 100000, out.stats_window_frames, err) ||
        !read_uint(doc.value(), "headroom_log_percent", 0, 100, out.headroom_log_percent, err)) {
        return Result<PacketArenaParams>::fail(err);
    }
    return Result<PacketArenaParams>::ok(out);
}

PacketArena::~PacketArena() {
    std::free(buf_[0]);
    std::free(buf_[1]);
}

void PacketArena::ensure_allocated() {
    if (allocated()) {
        return;
    }
    const std::size_t total = static_cast<std::size_t>(params_.size_bytes) + params_.canary_bytes;
    for (int i = 0; i < 2; ++i) {
        buf_[i] = static_cast<std::uint8_t*>(std::calloc(1, total));
        end_[i] = buf_[i] + params_.size_bytes;
        std::memset(end_[i], static_cast<int>(params_.canary_value), params_.canary_bytes);
    }
}

bool PacketArena::canary_intact(int which) const {
    const std::uint8_t* e = end_[which & 1];
    for (std::uint32_t i = 0; i < params_.canary_bytes; ++i) {
        if (e[i] != params_.canary_value) {
            return false;
        }
    }
    return true;
}

PacketArenaReport PacketArenaMonitor::observe(long used, const bool canary_ok[2]) {
    PacketArenaReport r;
    r.used = used;
    if (used > peak_) {
        peak_ = used;
    }
    cull_sum_ += used;
    if (++cull_frames_ >= static_cast<long>(params_.stats_window_frames)) {
        r.cull_stat = true;
        r.cull_avg = cull_sum_ / cull_frames_;
        r.cull_frames = cull_frames_;
        cull_sum_ = 0;
        cull_frames_ = 0;
    }
    for (int b = 0; b < 2; ++b) {
        if (!canary_ok[b] && !overrun_reported_[b]) {
            overrun_reported_[b] = true;
            r.overrun_new[b] = true;
        }
    }
    const long half = static_cast<long>(static_cast<std::uint64_t>(params_.size_bytes) * params_.headroom_log_percent / 100u);
    if (!high_water_logged_ && peak_ > half) {
        high_water_logged_ = true;
        r.high_water_new = true;
    }
    r.peak = peak_;
    return r;
}

}  // namespace sh::gameflow
