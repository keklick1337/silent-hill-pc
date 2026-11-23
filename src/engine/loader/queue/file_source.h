// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Where the bytes of one queued read come from: a cutscene file the
// engine player serves, an active mod's replacement at the file's disc path, or the disc sectors. A
// mod picture that replaces a TIM (or a mod TIM too big for the destination) leaves the disc read in
// place and is remembered against the file, for the image step to register over the native upload.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace sh::loader {

struct SourceRequest {
    std::int32_t file = -1;          // file table index: the key of a remembered picture
    std::string path;                // disc path, "CHARA/HERO.TIM"
    std::uint32_t table_bytes = 0;   // the file's size in the file table
    std::uint32_t first_sector = 0;
    bool image = false;              // a TIM read: its picture can be replaced
    void* dest = nullptr;
};

enum class Source { Cutscene, Mod, Disc };

class FileSourceHost {
public:
    virtual ~FileSourceHost() = default;

    virtual bool serve_cutscene(const SourceRequest& r) = 0;
    virtual bool mods_active() const = 0;
    virtual bool mod_has_picture(const std::string& path) const = 0;
    // Opens a mod's file at the disc path; false when no mod ships it. *size is -1 when it cannot be measured.
    virtual bool mod_open(const std::string& path, std::int64_t* size) = 0;
    // Reads up to `max` bytes of the open file and closes it.
    virtual std::size_t mod_read(void* dest, std::size_t max) = 0;
    virtual void mod_close() = 0;
    // A destination registered larger than the table size (oversized models, chunks); 0 = the table size.
    virtual std::size_t dest_capacity(const void* dest) const = 0;
    // A mod file of `size` bytes is in place (model diagnostics).
    virtual void mod_bytes_read(const SourceRequest& r, std::int64_t size) = 0;
    virtual void read_sector(std::uint32_t sector, void* dest) = 0;
    // The read's origin for the file trace: a path, or nullptr for the disc.
    virtual void trace(const SourceRequest& r, const char* from) = 0;
    virtual void note(const std::string& line) = 0;  // debug journal
};

// Pictures waiting for their file's image step, keyed by file (queue slots are recycled, files are not).
class PendingPictures {
public:
    static constexpr std::size_t kSlots = 32;

    // False when the table is full (the picture is dropped).
    bool stash(std::int32_t file, const std::string& path);
    std::optional<std::string> pop(std::int32_t file);

private:
    struct Slot {
        std::int32_t file = -1;
        std::string path;
    };
    std::array<Slot, kSlots> slots_{};
};

class FileSource {
public:
    static constexpr std::uint32_t kSectorBytes = 2048;

    Source read(FileSourceHost& host, const SourceRequest& r);

    PendingPictures& pending() { return pending_; }

private:
    void count_probe(FileSourceHost& host);

    PendingPictures pending_;
    int hits_ = 0, misses_ = 0, pictures_ = 0, warnings_ = 0;
};

}  // namespace sh::loader
