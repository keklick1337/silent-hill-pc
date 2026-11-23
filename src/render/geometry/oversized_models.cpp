// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "render/geometry/oversized_models.h"

#include <cstring>

#include "formats/game/model_file_check.h"

namespace sh::models {

static_assert(OversizedModels::kTail == sh::formats::game::kModelBufferTail);

bool OversizedModels::Buffer::holds(const void* address) const {
    const auto* p = static_cast<const std::uint8_t*>(address);
    return bytes != nullptr && p >= bytes.get() && p < bytes.get() + size;
}

const OversizedModels::Buffer* OversizedModels::buffer_holding(const void* address) const {
    if (address == nullptr) return nullptr;
    for (const auto& [file, entry] : entries_) {
        if (entry.buffer.holds(address)) return &entry.buffer;
    }
    for (const Buffer& b : retired_) {
        if (b.holds(address)) return &b;
    }
    return nullptr;
}

bool OversizedModels::owns(const void* address) const { return buffer_holding(address) != nullptr; }

void* OversizedModels::game_buffer_of(const void* address) const {
    const Buffer* b = buffer_holding(address);
    return b != nullptr ? b->stands_for : nullptr;
}

// The verdict on the mod's file for `file`; null when the mod has none or it fits the game's buffer. A file is
// read and checked once per size: a file that changed size on disk is checked again before anything trusts it.
OversizedModels::Entry* OversizedModels::examine(int file, const std::string& path) {
    const std::int64_t bytes = host_.mod_file_bytes(path);
    const std::int64_t disc = host_.disc_bytes(file);
    if (bytes <= disc) return nullptr;

    Entry& e = entries_[file];
    if (e.file_bytes == bytes) return &e;
    e.file_bytes = bytes;
    e.accepted = false;

    std::string refusal;
    if (const std::optional<std::vector<std::uint8_t>> content = host_.mod_file(path, bytes)) {
        sh::formats::game::ModelFileRules rules;
        rules.readable_tail = static_cast<std::uint32_t>(kTail);
        rules.skeletal = true;
        // The animation may not be loaded yet, so the bone rule waits for the authoring tools.
        if (const auto broken = sh::formats::game::check_model_file(*content, rules)) refusal = broken->text;
    } else {
        refusal = "the file could not be read";
    }
    if (!refusal.empty()) {
        host_.note("[BIGLM] " + path + " (" + std::to_string(bytes) + " bytes) refused: " + refusal +
                   "; the disc model is used");
        return &e;
    }

    const std::size_t needed = static_cast<std::size_t>(bytes) + kTail;
    if (e.buffer.bytes != nullptr && e.buffer.size < needed) {
        retired_.push_back(std::move(e.buffer));
        e.buffer = Buffer{};
    } else if (e.buffer.bytes != nullptr) {
        // The read writes only the file's bytes: what the larger file before it left behind must not show.
        std::memset(e.buffer.bytes.get(), 0, e.buffer.size);
    }
    e.accepted = true;
    host_.note("[BIGLM] " + path + " accepted: " + std::to_string(bytes) + " bytes (the disc file has " +
               std::to_string(disc) + ")");
    return &e;
}

void* OversizedModels::redirect(int file, void* buffer) {
    void* game = owns(buffer) ? game_buffer_of(buffer) : buffer;
    if (buffer == nullptr || !host_.mods_active()) return game;
    const std::optional<std::string> path = host_.mod_path(file);
    if (!path) return game;
    Entry* e = examine(file, *path);
    if (e == nullptr || !e->accepted) return game;

    if (e->buffer.bytes == nullptr) {
        e->buffer.size = static_cast<std::size_t>(e->file_bytes) + kTail;
        e->buffer.bytes = std::make_unique<std::uint8_t[]>(e->buffer.size);  // zeroed, the tail stays so
        host_.note("[BIGLM] " + *path + " (file " + std::to_string(file) + ") is read into its own buffer of " +
                   std::to_string(e->buffer.size) + " bytes");
    }
    e->buffer.stands_for = game;
    return e->buffer.bytes.get();
}

std::size_t OversizedModels::capacity(const void* dest) const {
    if (dest == nullptr) return 0;
    for (const auto& [file, entry] : entries_) {
        if (entry.accepted && entry.buffer.bytes.get() == dest) return entry.buffer.size;
    }
    for (const auto& [key, game] : game_buffers_) {
        if (game.buffer == dest) return game.capacity;
    }
    return 0;
}

std::int32_t OversizedModels::accepted_bytes(int file) {
    if (!host_.mods_active()) return -1;
    const std::optional<std::string> path = host_.mod_path(file);
    if (!path) return -1;
    const Entry* e = examine(file, *path);
    return e != nullptr && e->accepted ? static_cast<std::int32_t>(e->file_bytes) : -1;
}

void OversizedModels::set_game_buffer(int key, const void* buffer, std::size_t capacity) {
    if (buffer == nullptr) {
        game_buffers_.erase(key);
    } else {
        game_buffers_[key] = GameBuffer{buffer, capacity};
    }
}

}  // namespace sh::models
