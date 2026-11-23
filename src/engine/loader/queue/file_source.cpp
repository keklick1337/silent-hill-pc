#include "engine/loader/queue/file_source.h"

#include <algorithm>

namespace sh::loader {

namespace {

// The journal names each picture and hit only this many times.
constexpr int kNoteLimit = 64;

}  // namespace

bool PendingPictures::stash(std::int32_t file, const std::string& path) {
    if (file < 0) return false;
    Slot* free = nullptr;
    for (Slot& s : slots_) {
        if (s.file == file) {
            free = &s;
            break;
        }
        if (s.file < 0 && free == nullptr) free = &s;
    }
    if (free == nullptr) return false;
    free->file = file;
    free->path = path;
    return true;
}

std::optional<std::string> PendingPictures::pop(std::int32_t file) {
    if (file < 0) return std::nullopt;
    for (Slot& s : slots_) {
        if (s.file != file) continue;
        std::string path = std::move(s.path);
        s.file = -1;
        s.path.clear();
        return path;
    }
    return std::nullopt;
}

void FileSource::count_probe(FileSourceHost& host) {
    const int total = hits_ + misses_ + pictures_ + warnings_;
    if (total > 0 && total % kNoteLimit == 0) {
        host.note("[LOOSE/SUMMARY] " + std::to_string(hits_) + " hits, " + std::to_string(misses_) + " misses, " +
                  std::to_string(pictures_) + " hi-res, " + std::to_string(warnings_) + " warnings (cumulative)");
    }
}

Source FileSource::read(FileSourceHost& host, const SourceRequest& r) {
    if (host.serve_cutscene(r)) {
        host.trace(r, "convert/cutscenes");
        return Source::Cutscene;
    }

    std::int32_t sector_bytes = static_cast<std::int32_t>(r.table_bytes + kSectorBytes - 1);
    if (sector_bytes < 0) sector_bytes += static_cast<std::int32_t>(kSectorBytes - 1);

    if (host.mods_active()) {
        // A replacement picture wins over a same-path TIM: the disc file still loads so the image step
        // knows the native rectangle to register the picture over.
        bool picture = false;
        if (r.image && host.mod_has_picture(r.path)) {
            if (!pending_.stash(r.file, r.path)) host.note("[HIRES] pending table full, dropping " + r.path);
            picture = true;
            pictures_++;
            if (pictures_ <= kNoteLimit) host.note("[LOOSE/HIRES] " + r.path + ": override present; deferring to PostLoadTim");
        }

        std::int64_t size = 0;
        if (!picture && host.mod_open(r.path, &size)) {
            const std::size_t table = (static_cast<std::size_t>(r.table_bytes) + kSectorBytes - 1) & ~std::size_t{kSectorBytes - 1};
            const std::size_t capacity = std::max(table, host.dest_capacity(r.dest));
            if (size < 0) {
                warnings_++;
                host.note("[LOOSE/WARN] " + r.path + ": the file size cannot be measured");
                size = 0;
            }
            if (size > 0 && static_cast<std::size_t>(size) > capacity) {
                host.mod_close();
                if (r.image) {
                    if (!pending_.stash(r.file, r.path)) host.note("[HIRES] pending table full, dropping " + r.path);
                    pictures_++;
                    if (pictures_ <= kNoteLimit) {
                        host.note("[LOOSE/HIRES] " + r.path + " (" + std::to_string(size) + " bytes) > buf " +
                                  std::to_string(capacity) + "; deferring to PostLoadTim for hi-res override");
                    }
                } else {
                    warnings_++;
                    host.note("[LOOSE/WARN] " + r.path + " (" + std::to_string(size) + " bytes) > buf " +
                              std::to_string(capacity) + " for non-TIM read; ignoring loose file");
                }
            } else {
                const std::size_t got = host.mod_read(r.dest, capacity);
                hits_++;
                if (hits_ <= kNoteLimit) {
                    host.note("[LOOSE] hit: " + r.path + " -> " + std::to_string(got) + "/" + std::to_string(capacity) +
                              " bytes (file=" + std::to_string(size) + ")");
                }
                if (got == 0) {
                    warnings_++;
                    host.note("[LOOSE/WARN] " + r.path + ": read returned 0 - falling back to disc");
                } else {
                    if (got < capacity && size > 0 && static_cast<std::int64_t>(got) < size) {
                        warnings_++;
                        host.note("[LOOSE/WARN] " + r.path + ": short read " + std::to_string(got) + " of " +
                                  std::to_string(size) + " bytes");
                    }
                    if (size > 0) host.mod_bytes_read(r, size);
                    host.trace(r, r.path.c_str());
                    return Source::Mod;
                }
            }
        } else if (!picture) {
            misses_++;
        }
        count_probe(host);
    }

    host.trace(r, nullptr);
    auto* dest = static_cast<std::uint8_t*>(r.dest);
    const std::int32_t sectors = sector_bytes >> 11;
    for (std::int32_t i = 0; i < sectors; i++) {
        host.read_sector(r.first_sector + static_cast<std::uint32_t>(i), dest + static_cast<std::size_t>(i) * kSectorBytes);
    }
    return Source::Disc;
}

}  // namespace sh::loader
