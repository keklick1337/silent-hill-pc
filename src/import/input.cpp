#include "import/input.h"

#include "import/sha256.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <system_error>

namespace sh::import {

namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t kRawSectorBytes = 2352;
constexpr std::uint64_t kCookedSectorBytes = 2048;
constexpr std::uint64_t kDescriptorSector = 16;
constexpr char kSupportedGame[] = "Silent Hill, NTSC USA v1.1 (SLUS-007.07)";

std::string lower_extension(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

struct Head {
    std::vector<std::uint8_t> bytes;  // the start of the file; short for a short file
    std::uint64_t file_size = 0;
};

// Enough to see the volume descriptor of a 2048-byte image (sector 16, identifier at byte 1).
Head read_head(const fs::path& path) {
    Head head;
    std::error_code ec;
    head.file_size = fs::file_size(path, ec);
    if (ec) {
        head.file_size = 0;
        return head;
    }
    std::ifstream in(path, std::ios::binary);
    head.bytes.resize(static_cast<std::size_t>(std::min<std::uint64_t>(head.file_size, 0x8006)));
    in.read(reinterpret_cast<char*>(head.bytes.data()), static_cast<std::streamsize>(head.bytes.size()));
    head.bytes.resize(static_cast<std::size_t>(std::max<std::streamsize>(in.gcount(), 0)));
    return head;
}

bool has_cd001_at(const Head& head, std::uint64_t offset) {
    return head.bytes.size() >= offset + 5 && std::memcmp(head.bytes.data() + offset, "CD001", 5) == 0;
}

std::string refusal_cooked_iso() {
    return "this is a plain .iso with 2048 bytes per sector. The game needs the raw sectors of the disc "
           "(2352 bytes per sector): HILL. keeps XA subheaders and EDC that a plain .iso drops. "
           "Give the .cue or .bin dump of the disc (MODE2/2352), or an .iso saved as raw 2352.";
}

std::string refusal_single_file() {
    return "this is a single game file, not a disc image. Without the image there are no archives, XA tracks or "
           "movies. Give the .cue or .bin dump of the disc (MODE2/2352).";
}

}  // namespace

Result<std::shared_ptr<const disc::Image>> open_image(const fs::path& input, const fs::path& alias) {
    using R = Result<std::shared_ptr<const disc::Image>>;
    std::error_code ec;
    if (!fs::is_regular_file(input, ec)) {
        return R::fail("image not found: " + input.string());
    }

    const std::string ext = lower_extension(input);
    if (ext == ".cue") {
        std::shared_ptr<const disc::Image> image(disc::Image::open(input));
        if (!image) {
            return R::fail(
                "cannot open the cue sheet " + input.string() +
                ": it needs one FILE ... BINARY line naming an existing .bin of 2352-byte sectors, and one "
                "TRACK MODE2/2352 with INDEX 01 00:00:00");
        }
        return R::ok(std::move(image));
    }

    const Head head = read_head(input);
    if (head.bytes.size() >= 8 && std::memcmp(head.bytes.data(), "PS-X EXE", 8) == 0) {
        return R::fail(input.string() + ": " + refusal_single_file());
    }
    if (ext != ".bin" && ext != ".iso") {
        if (has_cd001_at(head, kDescriptorSector * kCookedSectorBytes + 1)) {
            return R::fail(input.string() + ": " + refusal_cooked_iso());
        }
        return R::fail(input.string() + ": unsupported input. Give a .cue sheet, a .bin dump (MODE2/2352) or a raw .iso "
                                        "(2352 bytes per sector); single game files are not enough");
    }
    if (head.file_size == 0) {
        return R::fail(input.string() + " is empty");
    }
    const bool raw_sync = head.bytes.size() >= 12 && head.bytes[0] == 0x00 && head.bytes[1] == 0xff &&
                          head.bytes[11] == 0x00;
    if (!raw_sync && has_cd001_at(head, kDescriptorSector * kCookedSectorBytes + 1)) {
        return R::fail(input.string() + ": " + refusal_cooked_iso());
    }
    if (head.file_size % kRawSectorBytes != 0) {
        return R::fail(input.string() + ": the size is not a multiple of 2352, so this is not a raw MODE2/2352 image" +
                       (head.file_size % kCookedSectorBytes == 0 ? " (it looks like a 2048-byte image). " + refusal_cooked_iso()
                                                                 : std::string()));
    }

    fs::path source = input;
    if (ext == ".iso") {
        fs::remove(alias, ec);
        fs::create_symlink(fs::absolute(input), alias, ec);
        if (ec) {
            std::error_code link_ec;
            fs::create_hard_link(fs::absolute(input), alias, link_ec);
            if (link_ec) {
                return R::fail("cannot open a raw .iso: sh_disc recognises .bin and .cue only, and a link named " +
                               alias.string() + " could not be made (" + ec.message() + "). Rename the file to .bin");
            }
        }
        source = alias;
    }
    std::shared_ptr<const disc::Image> image(disc::Image::open(source));
    if (!image) {
        return R::fail("cannot open the image " + input.string());
    }
    if (!image->sync_ok()) {
        return R::fail(input.string() + " does not start with the CD sync pattern, so it is not a raw MODE2/2352 image");
    }
    return R::ok(std::move(image));
}

Result<GameDisc> open_game(std::shared_ptr<const disc::Image> image, const std::atomic<bool>* cancel,
                           const std::function<void(double)>& hash_progress) {
    using R = Result<GameDisc>;
    auto volume = iso9660::Volume::open(image);
    if (!volume) {
        return R::fail("the image has no ISO 9660 volume at sector 16: this is not a PlayStation disc");
    }

    const auto system_cnf = volume->find("SYSTEM.CNF");
    const auto slus = volume->find("SLUS_007.07");
    const auto silent = volume->find("SILENT.");
    const auto hill = volume->find("HILL.");
    if (!slus) {
        return R::fail(std::string("this is not ") + kSupportedGame + ": SLUS_007.07 is missing from the disc");
    }
    if (!system_cnf || !silent || !hill) {
        return R::fail(std::string("the disc is not ") + kSupportedGame + ": SYSTEM.CNF, SILENT. or HILL. is missing");
    }

    GameDisc game;
    game.image = image;
    game.system_cnf = volume->open_file(*system_cnf);
    game.slus = volume->open_file(*slus);
    game.silent = volume->open_file(*silent);
    game.system_cnf_lba = system_cnf->lba;
    game.slus_lba = slus->lba;
    if (!game.system_cnf || !game.slus || !game.silent) {
        return R::fail("cannot open the disc files of the ISO 9660 volume");
    }

    std::vector<std::uint8_t> exe(static_cast<std::size_t>(game.slus->size()));
    if (!game.slus->read_at(0, exe.data(), exe.size())) {
        return R::fail("cannot read SLUS_007.07 from the image");
    }
    const std::string exe_sha = sha256_hex(exe.data(), exe.size());
    if (exe_sha != kSupportedExeSha256) {
        return R::fail(std::string("unsupported game version: SLUS_007.07 has SHA-256 ") + exe_sha + ". Only " +
                       kSupportedGame + " is supported");
    }

    // Both containers are measured in 2048-byte sectors by ISO 9660; HILL. data is 2336 bytes per sector.
    const auto sectors_for = [](std::uint32_t bytes) { return (bytes + 2047) / 2048; };
    game.silent_layout = {silent->lba, sectors_for(silent->size)};
    game.hill_layout = {hill->lba, sectors_for(hill->size)};
    if (std::uint64_t{game.hill_layout.start_lba} + game.hill_layout.sector_count > image->sector_count() ||
        std::uint64_t{game.silent_layout.start_lba} + game.silent_layout.sector_count > game.hill_layout.start_lba) {
        return R::fail("SILENT. and HILL. do not fit the layout of the " + std::string(kSupportedGame) + " disc");
    }
    game.hill = archive::open_hill(image, game.hill_layout);
    if (!game.hill) {
        return R::fail("cannot open HILL. on the image");
    }

    auto entries = archive::read_file_table(*game.slus, game.silent_layout, game.hill_layout);
    if (!entries) {
        return R::fail("the file table of SLUS_007.07 does not fit the disc layout");
    }
    game.entries = std::move(*entries);

    Sha256 hash;
    std::array<std::uint8_t, kRawSectorBytes> sector{};
    const std::uint32_t sectors = image->sector_count();
    for (std::uint32_t lba = 0; lba < sectors; ++lba) {
        if (!image->read_raw(lba, sector.data())) {
            return R::fail("cannot read sector " + std::to_string(lba) + " of the image");
        }
        hash.update(sector.data(), sector.size());
        if (hash_progress && (lba & 0xFFF) == 0) {
            hash_progress(static_cast<double>(lba) / sectors);
        }
        if ((lba & 0xFF) == 0 && cancel && cancel->load()) {
            return R::fail("cancelled");
        }
    }
    game.image_sha256 = to_hex(hash.finish());
    if (hash_progress) {
        hash_progress(1.0);
    }
    if (game.image_sha256 != kSupportedImageSha256) {
        return R::fail(std::string("the image is not the supported dump of ") + kSupportedGame + ": SHA-256 is " +
                       game.image_sha256 + ", expected " + kSupportedImageSha256 +
                       ". SLUS_007.07 is the right version, so the dump differs elsewhere (another rip or a modified image)");
    }
    return R::ok(std::move(game));
}

}  // namespace sh::import
