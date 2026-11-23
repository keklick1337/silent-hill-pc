// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/save/memory_card.h"

#include "formats/common/crc32.h"
#include "formats/common/json.h"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <set>
#include <system_error>

namespace sh::save {

namespace fs = std::filesystem;

namespace {

// PS1 memory card: 16 blocks of 8 KiB. Block 0 holds the card header (frame 0) and one 128-byte directory frame
// per data block (frames 1..15); data block n follows at n * 8 KiB.
constexpr std::size_t kCardSize = 128 * 1024;
constexpr std::size_t kBlockSize = 8192;
constexpr std::size_t kFrameSize = 128;
constexpr int kDataBlocks = 15;
constexpr std::uint8_t kStateFirstBlock = 0x51;

// Wrappers: DexDrive .gme puts a 3904-byte header in front, VGS .mem a 64-byte one.
constexpr std::size_t kGmeHeader = 3904;
constexpr std::size_t kVgsHeader = 64;

// One Silent Hill file: the list of save entries, the options and the saves.
constexpr std::size_t kListOffset = 0x200;
constexpr std::size_t kListSize = 256;
constexpr std::size_t kEntrySize = 12;
constexpr std::size_t kOptionsOffset = 0x300;
constexpr std::size_t kOptionsSize = 128;
constexpr std::size_t kSavesOffset = 0x380;
constexpr std::size_t kSaveSize = 640;
constexpr std::uint16_t kFooterMagic = 0xDCDC;

const char* const kNamePrefixes[] = {"BASLUS-00707SILENT", "BESLES-01514SILENT", "BISLPM-86192SILENT"};

std::uint32_t get_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// The game's record check: the XOR of all bytes (footer included) equals the first checksum byte, and the footer
// ends with the magic.
bool record_valid(std::span<const std::uint8_t> record) {
    if (record.size() < 4) {
        return false;
    }
    std::uint8_t x = 0;
    for (std::uint8_t b : record) {
        x ^= b;
    }
    const std::size_t f = record.size() - 4;
    const std::uint16_t magic = static_cast<std::uint16_t>(record[f + 2] | (record[f + 3] << 8));
    return x == record[f] && magic == kFooterMagic;
}

SlotSummary summary_of_entry(const std::uint8_t* e) {
    SlotSummary s;
    s.sequence = static_cast<std::int32_t>(get_u32(e));
    s.play_time = get_u32(e + 4);
    s.save_count = static_cast<std::uint16_t>(e[8] | (e[9] << 8));
    s.location = e[10];
    s.next_fear = (e[11] & 1) != 0;
    s.extra_hours = static_cast<std::uint8_t>((e[11] >> 1) & 3);
    s.special_items = static_cast<std::uint8_t>(e[11] >> 3);
    return s;
}

std::string hex32(std::uint32_t v) {
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%08x", v);
    return buf;
}

struct MigrationEntry {
    std::string source;
    std::uint64_t size = 0;
    std::string crc;
};

std::vector<MigrationEntry> load_migration(const fs::path& path) {
    std::vector<MigrationEntry> out;
    auto bytes = read_whole_file(path);
    if (!bytes) {
        return out;
    }
    auto doc = json::parse(std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()));
    if (!doc) {
        return out;
    }
    const json::Value* list = doc.value().find("imported");
    if (list == nullptr || !list->is_array()) {
        return out;
    }
    for (const json::Value& item : list->array()) {
        const json::Value* source = item.find("source");
        const json::Value* size = item.find("size");
        const json::Value* crc = item.find("crc32");
        if (source != nullptr && source->is_string() && size != nullptr && size->is_integer() && crc != nullptr &&
            crc->is_string()) {
            out.push_back({source->string(), static_cast<std::uint64_t>(size->integer()), crc->string()});
        }
    }
    return out;
}

Result<void> store_migration(const fs::path& path, const std::vector<MigrationEntry>& entries) {
    json::Writer w(true);
    w.begin_object();
    w.member("format", "sh-save-migration/1");
    w.key("imported").begin_array();
    for (const MigrationEntry& e : entries) {
        w.inline_next().begin_object();
        w.member("source", e.source);
        w.member("size", e.size);
        w.member("crc32", e.crc);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    std::string text = w.take();
    text += '\n';
    return write_file_atomic(path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                                                  text.size()));
}

// "0.MCD" .. "11.MCD": the card number of the old file layout, or -1.
int legacy_card_number(const std::string& name) {
    if (name.size() < 5 || name.size() > 6 || !name.ends_with(".MCD")) {
        return -1;
    }
    int n = 0;
    for (std::size_t i = 0; i + 4 < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return -1;
        }
        n = n * 10 + (name[i] - '0');
    }
    return n < 12 ? n : -1;
}

}  // namespace

Result<std::vector<CardFile>> read_memory_card(std::span<const std::uint8_t> image) {
    using R = Result<std::vector<CardFile>>;
    if (image.size() == kCardSize + kGmeHeader && std::memcmp(image.data(), "123-456-STD", 11) == 0) {
        image = image.subspan(kGmeHeader);
    } else if (image.size() == kCardSize + kVgsHeader && std::memcmp(image.data(), "VgsM", 4) == 0) {
        image = image.subspan(kVgsHeader);
    }
    if (image.size() != kCardSize) {
        return R::fail("not a memory card image: " + std::to_string(image.size()) + " bytes, expected 131072");
    }
    if (image[0] != 'M' || image[1] != 'C') {
        return R::fail("not a formatted memory card (no MC header)");
    }

    std::vector<CardFile> files;
    for (int block = 1; block <= kDataBlocks; ++block) {
        const std::uint8_t* frame = image.data() + block * kFrameSize;
        if (frame[0] != kStateFirstBlock) {
            continue;
        }
        CardFile file;
        char name[21] = {};
        std::memcpy(name, frame + 10, 20);
        file.name = name;
        file.size = get_u32(frame + 4);
        // Follow the chain; the visited set stops a looping chain on a corrupted card.
        std::set<int> visited;
        int current = block;
        while (current >= 1 && current <= kDataBlocks && visited.insert(current).second) {
            const std::uint8_t* data = image.data() + current * kBlockSize;
            file.data.insert(file.data.end(), data, data + kBlockSize);
            const std::uint8_t* link = image.data() + current * kFrameSize + 8;
            const int next = link[0] | (link[1] << 8);
            current = next == 0xFFFF ? -1 : next + 1;
        }
        files.push_back(std::move(file));
    }
    return R::ok(std::move(files));
}

bool is_silent_hill_file(const std::string& name, int* group) {
    for (const char* prefix : kNamePrefixes) {
        const std::size_t n = std::strlen(prefix);
        if (name.size() == n + 2 && name.compare(0, n, prefix) == 0 && name[n] >= '0' && name[n] <= '9' &&
            name[n + 1] >= '0' && name[n + 1] <= '9') {
            const int g = (name[n] - '0') * 10 + (name[n + 1] - '0');
            if (g >= kLegacyFilesPerCard) {
                return false;
            }
            if (group != nullptr) {
                *group = g;
            }
            return true;
        }
    }
    return false;
}

namespace {

// The game state and summary of every readable save of the store: what an import must not add twice.
struct Known {
    std::vector<std::pair<SlotSummary, std::string>> records;

    explicit Known(const SaveStore& store) {
        for (int number : store.numbers()) {
            SlotRead r = store.read(number);
            if (r.state == SlotState::Ok) {
                records.emplace_back(r.record.summary, std::move(r.record.state));
            }
        }
    }

    bool has(const SlotSummary& summary, const std::string& state) const {
        return std::any_of(records.begin(), records.end(),
                           [&](const auto& k) { return k.first == summary && k.second == state; });
    }
};

// Writes the records oldest first (by sequence), so the save numbers follow the order they were made in. A record
// whose game state fails the game's checksum is not copied (the game could never load it).
Result<int> add_new(SaveStore& store, std::vector<SlotRecord>& records, int* already, int* damaged,
                    std::vector<std::string>& notes) {
    std::stable_sort(records.begin(), records.end(), [](const SlotRecord& a, const SlotRecord& b) {
        return a.summary.sequence < b.summary.sequence;
    });
    Known known(store);
    int written = 0;
    for (SlotRecord& record : records) {
        Warnings warnings;
        auto state = store.state_of(record, warnings);
        if (!state) {
            ++*damaged;
            notes.push_back("save " + std::to_string(record.summary.sequence) + " not copied: " + state.error());
            continue;
        }
        if (known.has(record.summary, state.value())) {
            ++*already;
            continue;
        }
        record.state = state.value();
        if (auto r = store.write(0, record); !r) {
            return Result<int>::fail(r.error());
        }
        known.records.emplace_back(record.summary, std::move(state.value()));
        ++written;
    }
    return Result<int>::ok(written);
}

}  // namespace

Result<ImportStats> import_memory_card(std::span<const std::uint8_t> image, SaveStore& store) {
    auto files = read_memory_card(image);
    if (!files) {
        return Result<ImportStats>::fail(files.error());
    }
    ImportStats stats;
    std::set<int> seen;
    std::vector<SlotRecord> records;
    for (const CardFile& file : files.value()) {
        int group = -1;
        if (!is_silent_hill_file(file.name, &group)) {
            continue;
        }
        if (!seen.insert(group).second) {
            stats.notes.push_back(file.name + ": file " + std::to_string(group + 1) +
                                  " is on the card twice (two regions), the first one was taken");
            continue;
        }
        if (file.data.size() < kSavesOffset + kLegacySlotsPerFile * kSaveSize) {
            stats.notes.push_back(file.name + ": shorter than a Silent Hill file, skipped");
            continue;
        }
        ++stats.files;
        const std::span<const std::uint8_t> list(file.data.data() + kListOffset, kListSize);
        if (!record_valid(list)) {
            ++stats.damaged;
            stats.notes.push_back(file.name + ": the list of saves fails its checksum (the game shows the file as "
                                  "damaged), skipped");
            continue;
        }
        const std::span<const std::uint8_t> options(file.data.data() + kOptionsOffset, kOptionsSize);
        for (int slot = 0; slot < kLegacySlotsPerFile; ++slot) {
            const SlotSummary summary = summary_of_entry(list.data() + 4 + slot * kEntrySize);
            if (summary.sequence == 0) {
                continue;
            }
            SlotRecord record;
            record.summary = summary;
            const std::uint8_t* save = file.data.data() + kSavesOffset + slot * kSaveSize;
            record.legacy_game.assign(save, save + kSaveSize);
            if (record_valid(options)) {
                record.legacy_options.assign(options.begin(), options.end());
            }
            records.push_back(std::move(record));
        }
    }
    auto written = add_new(store, records, &stats.already_there, &stats.damaged, stats.notes);
    if (!written) {
        return Result<ImportStats>::fail(written.error());
    }
    stats.saves = written.value();
    return Result<ImportStats>::ok(std::move(stats));
}

namespace {

// Imports each (image, card) once; migration.json remembers path, size and CRC-32.
MigrationReport import_once(SaveStore& store, const std::vector<fs::path>& images) {
    MigrationReport report;
    const fs::path marker = store.root() / "migration.json";
    std::vector<MigrationEntry> done = load_migration(marker);
    bool changed = false;
    std::set<std::string> visited;

    for (const fs::path& image_path : images) {
        std::error_code ec;
        const fs::path source = fs::weakly_canonical(image_path, ec);
        if (!visited.insert(source.string()).second) {
            continue;
        }
        ++report.sources;
        auto bytes = read_whole_file(image_path);
        if (!bytes) {
            report.lines.push_back("memory card " + image_path.string() + ": " + bytes.error());
            continue;
        }
        const std::string crc = hex32(crc32(bytes.value().data(), bytes.value().size()));
        const bool already = std::any_of(done.begin(), done.end(), [&](const MigrationEntry& e) {
            return e.source == source.string() && e.size == bytes.value().size() && e.crc == crc;
        });
        if (already) {
            continue;
        }
        auto stats = import_memory_card(bytes.value(), store);
        if (!stats) {
            report.lines.push_back("memory card " + image_path.string() + ": not imported: " + stats.error());
            continue;
        }
        ++report.imported;
        report.saves += stats.value().saves;
        report.lines.push_back("memory card " + image_path.string() + " -> " + store.root().string() + ": " +
                               std::to_string(stats.value().saves) + " save(s) imported, " +
                               std::to_string(stats.value().already_there) +
                               " already there, " + std::to_string(stats.value().damaged) +
                               " damaged file(s); the image is no longer used and can be deleted");
        for (const std::string& note : stats.value().notes) {
            report.lines.push_back("  " + note);
        }
        done.push_back({source.string(), bytes.value().size(), crc});
        changed = true;
    }
    if (changed) {
        if (auto r = store_migration(marker, done); !r) {
            report.lines.push_back("cannot record the migration: " + r.error());
        }
    }
    return report;
}

bool has_card_extension(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".mcd" || ext == ".mcr" || ext == ".mc" || ext == ".gme" || ext == ".mem" || ext == ".srm";
}

}  // namespace

MigrationReport migrate_legacy_cards(SaveStore& store, const std::vector<fs::path>& dirs) {
    std::vector<fs::path> images;
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) {
            continue;
        }
        std::vector<fs::path> found;
        for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec) && legacy_card_number(entry.path().filename().string()) >= 0) {
                found.push_back(entry.path());
            }
        }
        std::sort(found.begin(), found.end());
        images.insert(images.end(), found.begin(), found.end());
    }
    return import_once(store, images);
}

MigrationReport import_drop_folder(SaveStore& store) {
    std::vector<fs::path> images;
    const fs::path base = store.root() / "import";
    for (const fs::path& dir : {base, base / "card1", base / "card2"}) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) {
            continue;
        }
        std::vector<fs::path> found;
        for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec) && has_card_extension(entry.path())) {
                found.push_back(entry.path());
            }
        }
        std::sort(found.begin(), found.end());
        images.insert(images.end(), found.begin(), found.end());
    }
    return import_once(store, images);
}

MigrationReport migrate_card_layout(SaveStore& store) {
    MigrationReport report;
    const fs::path marker = store.root() / "migration.json";
    std::vector<MigrationEntry> done = load_migration(marker);

    struct Pending {
        SlotRecord record;
        MigrationEntry source;
    };
    std::vector<Pending> pending;
    for (int card = 0; card < kLegacyCardCount; ++card) {
        std::error_code ec;
        if (!fs::is_directory(store.legacy_card_dir(card), ec)) {
            continue;
        }
        for (int file = 0; file < kLegacyFilesPerCard; ++file) {
            for (int slot = 0; slot < kLegacySlotsPerFile; ++slot) {
                const fs::path path = store.legacy_slot_path(card, file, slot);
                if (!fs::is_regular_file(path, ec)) {
                    continue;
                }
                ++report.sources;
                auto bytes = read_whole_file(path);
                if (!bytes) {
                    report.lines.push_back(path.string() + ": " + bytes.error());
                    continue;
                }
                const fs::path source = fs::weakly_canonical(path, ec);
                MigrationEntry entry{source.string(), bytes.value().size(),
                                     hex32(crc32(bytes.value().data(), bytes.value().size()))};
                if (std::any_of(done.begin(), done.end(), [&](const MigrationEntry& e) {
                        return e.source == entry.source && e.size == entry.size && e.crc == entry.crc;
                    })) {
                    continue;
                }
                SlotRead r = store.read_legacy_slot(card, file, slot);
                if (r.state != SlotState::Ok) {
                    report.lines.push_back(path.string() + ": not copied, damaged: " + r.error);
                    continue;
                }
                if (r.record.summary.sequence == 0) {
                    continue;
                }
                pending.push_back({std::move(r.record), std::move(entry)});
            }
        }
    }
    if (pending.empty()) {
        return report;
    }

    std::stable_sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b) {
        return a.record.summary.sequence < b.record.summary.sequence;
    });
    Known known(store);
    int already = 0;
    for (Pending& p : pending) {
        if (known.has(p.record.summary, p.record.state)) {
            ++already;
        } else {
            auto written = store.write(0, p.record);
            if (!written) {
                report.lines.push_back(p.source.source + ": " + written.error());
                continue;
            }
            known.records.emplace_back(p.record.summary, p.record.state);
            ++report.saves;
        }
        ++report.imported;
        done.push_back(p.source);
    }
    report.lines.push_back("old card1/card2 layout -> " + store.root().string() + ": " + std::to_string(report.saves) +
                           " save(s) copied, " + std::to_string(already) +
                           " already there; the card1/ and card2/ folders are no longer used and can be deleted");
    if (auto r = store_migration(marker, done); !r) {
        report.lines.push_back("cannot record the migration: " + r.error());
    }
    return report;
}

}  // namespace sh::save
