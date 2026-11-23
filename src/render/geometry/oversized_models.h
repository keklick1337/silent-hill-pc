// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Character model files of mods that are larger than the disc file they replace (the
// section on models and textures). The game reads a character's skeleton and parts from its model file into a buffer
// sized for the disc file; a larger file of a mod is checked (formats/game/model_file_check.h) and gets a buffer of its
// own, which stands in for the game's buffer in the read and in everything that follows the model's address. A file
// that is absent, fits, or fails the check changes nothing: the game's buffer and the ordinary read are used.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sh::models {

// What the registry asks of the game: the mods' files and the disc file table.
class OversizedModelHost {
public:
    virtual ~OversizedModelHost() = default;
    virtual bool mods_active() = 0;
    // The mods' path of disc file `file` ("CHARA/HERO.ILM"); nothing for a number outside the file table.
    virtual std::optional<std::string> mod_path(int file) = 0;
    // Bytes the game's buffer holds for `file`: the disc file's size in whole sectors.
    virtual std::int64_t disc_bytes(int file) = 0;
    // Size of a mod's file, -1 when no active mod has it.
    virtual std::int64_t mod_file_bytes(const std::string& path) = 0;
    // The first `bytes` bytes of a mod's file; nothing when it cannot be read whole.
    virtual std::optional<std::vector<std::uint8_t>> mod_file(const std::string& path, std::int64_t bytes) = 0;
    virtual void note(const std::string& line) = 0;
};

class OversizedModels {
public:
    // Readable bytes kept behind the file in every buffer handed out here (formats::game::kModelBufferTail).
    static constexpr std::size_t kTail = 16;

    explicit OversizedModels(OversizedModelHost& host) : host_(host) {}

    // The buffer the model file `file` is to be read into: a buffer of this registry for an accepted oversized
    // file of a mod, else the game's buffer. `buffer` may itself be a buffer of this registry (a reused slot hands
    // over the previous character's): the game's buffer it stood in for is used in its place.
    void* redirect(int file, void* buffer);

    // Bytes a read may write to `dest`: for a buffer handed out by redirect and for a registered buffer of the
    // game (set_game_buffer); 0 for any other address.
    std::size_t capacity(const void* dest) const;

    // Whether `address` lies in a buffer of this registry, and the game's buffer that one stands in for.
    bool owns(const void* address) const;
    void* game_buffer_of(const void* address) const;

    // Size of the accepted oversized file of a mod for `file`; -1 when there is none.
    std::int32_t accepted_bytes(int file);

    // A buffer the game allocated itself for an oversized file (the character pool), under a key of the game's
    // choosing: `capacity` bytes may be read into it. A null buffer forgets the key.
    void set_game_buffer(int key, const void* buffer, std::size_t capacity);

private:
    struct Buffer {
        std::unique_ptr<std::uint8_t[]> bytes;
        std::size_t size = 0;
        void* stands_for = nullptr;

        bool holds(const void* address) const;
    };
    struct Entry {
        std::int64_t file_bytes = -1;  // the size the verdict is about
        bool accepted = false;
        Buffer buffer;  // allocated by the first redirect
    };
    struct GameBuffer {
        const void* buffer = nullptr;
        std::size_t capacity = 0;
    };

    Entry* examine(int file, const std::string& path);
    const Buffer* buffer_holding(const void* address) const;

    OversizedModelHost& host_;
    std::map<int, Entry> entries_;  // by file number: only oversized files, accepted or refused
    // Buffers a file that grew left behind: a loaded model may still live in one, so its address keeps resolving.
    std::vector<Buffer> retired_;
    std::map<int, GameBuffer> game_buffers_;
};

// The registry of the running game.
OversizedModels& oversized_models();

}  // namespace sh::models
