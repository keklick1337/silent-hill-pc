#include "engine/audio/disc_drive.h"

namespace sh::audio {

namespace {

constexpr double kCpuHz = 33868800.0;
constexpr int kDiscSectors = 72 * 60 * 75;

int from_bcd(std::uint8_t v) { return (v >> 4) * 10 + (v & 15); }

// minute, second, sector (BCD); the lead-in of 150 sectors is not part of an LBA.
int position_lba(const std::uint8_t* p) {
    return 75 * (60 * from_bcd(p[0]) + from_bcd(p[1])) + from_bcd(p[2]) - 150;
}

}  // namespace

int DiscDrive::seek_vblanks(int from_lba, int to_lba, int vblank_rate) {
    const int diff = to_lba > from_lba ? to_lba - from_lba : from_lba - to_lba;
    double cycles = static_cast<double>(diff) * kCpuHz / kDiscSectors;
    if (cycles < 20000.0) {
        cycles = 20000.0;
    }
    // Long seeks pay a fixed settle; short ones pay the restart from the paused state.
    cycles += diff >= 2250 ? kCpuHz * 0.3 : 1237952.0;
    const double vblanks = cycles * vblank_rate / kCpuHz;
    const int whole = static_cast<int>(vblanks);
    return static_cast<double>(whole) < vblanks ? whole + 1 : whole;
}

void DiscDrive::seek_start(int lba, int vb) {
    seek_done_vb_ = vb + seek_vblanks(phys_head_, lba, host_.vblank_rate());
    phys_head_ = lba;
    head_lba_ = lba;
}

// Data starts at the later of now and the end of any positioning; a read that begins elsewhere
// than the head seeks there first.
int DiscDrive::position_for(int lba, int vb) {
    if (lba != phys_head_) {
        seek_start(lba, vb);
    }
    return seek_done_vb_ > vb ? seek_done_vb_ : vb;
}

bool DiscDrive::command(DiscCommand command, const std::uint8_t* params) {
    if (command == DiscCommand::SetLocation && params != nullptr) {
        head_lba_ = position_lba(params);
        ++head_epoch_;
    } else if (command == DiscCommand::SeekAudio) {
        // Without a parameter the seek goes to the position the last command set.
        const int lba = params != nullptr ? position_lba(params) : cursor_;
        if (host_.available()) {
            seek_start(lba, host_.vblank_now());
        } else {
            head_lba_ = lba;
        }
        ++head_epoch_;
    }

    if (!host_.available()) {
        return false;
    }
    if ((command == DiscCommand::SetLocation || command == DiscCommand::SeekAudio) && params != nullptr) {
        cursor_ = position_lba(params);
    }
    return true;
}

void DiscDrive::read(int sectors, void* dst) {
    const int vb = host_.vblank_now();
    ++head_epoch_;
    for (Read& r : queue_) {
        if (!r.pending) {
            r.pending = true;
            r.dst = static_cast<std::uint8_t*>(dst);
            r.count = sectors;
            r.pos = cursor_;
            r.vblank_start = position_for(r.pos, vb);
            r.drive_sectors = 0;
            return;
        }
    }
}

bool DiscDrive::read_one(Read& r) {
    if (r.pos >= host_.sector_count()) {
        return false;
    }
    host_.read_data(r.pos, r.dst);
    ++r.pos;
    ++r.drive_sectors;
    cursor_ = r.pos;
    r.dst += 2048;
    --r.count;
    return true;
}

int DiscDrive::read_remaining() {
    for (Read& r : queue_) {
        if (!r.pending) {
            continue;
        }
        // Only the sectors the drive has read by now, at its speed.
        int due = -1;
        if (!host_.instant()) {
            const int vb = host_.vblank_now();
            due = vb <= r.vblank_start ? 0 : (vb - r.vblank_start) * kSectorsPerSecond2x / host_.vblank_rate();
        }
        while (r.count > 0 && (due < 0 || r.drive_sectors < due)) {
            if (!read_one(r)) {
                r.count = 0;
                break;
            }
        }
        phys_head_ = r.pos;
        head_lba_ = r.pos;
        if (r.count == 0) {
            r.pending = false;
        } else {
            host_.busy();
        }
        return r.count;
    }
    return -1;
}

bool DiscDrive::ready() {
    if (!host_.instant() && host_.vblank_now() < seek_done_vb_) {
        host_.busy();
        return false;
    }
    return true;
}

}  // namespace sh::audio
