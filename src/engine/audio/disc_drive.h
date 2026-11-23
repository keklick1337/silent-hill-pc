#pragma once
// The model of the console's disc drive that the sound driver talks to: where the head is, how long a
// seek takes, and how many sectors of a read the drive has delivered by now. Time is counted in
// VBlanks so the pace does not depend on the frame rate. The seek formula is the one of the voice
// seek model (Mednafen and DuckStation); it is not checked against hardware.

#include <array>
#include <cstdint>
#include <functional>

#include "engine/audio/sound_driver.h"

namespace sh::audio {

struct DiscDriveHost {
    std::function<int()> vblank_now;
    std::function<int()> vblank_rate;                  // vertical blanks per second
    std::function<bool()> available;                   // a disc (or the VFS behind it) can be read
    std::function<int()> sector_count;
    std::function<bool(int lba, void* dst)> read_data;  // 2048 bytes of user data
    std::function<bool()> instant;                     // reads and seeks take no time
    std::function<void()> busy;                        // a poll found the drive not ready
};

class DiscDrive {
public:
    explicit DiscDrive(DiscDriveHost host) : host_(std::move(host)) {}

    // Seek time between two positions in whole VBlanks, rounded up.
    static int seek_vblanks(int from_lba, int to_lba, int vblank_rate);

    // Every command moves the head model; the return value says whether the drive took it.
    bool command(DiscCommand command, const std::uint8_t* params);
    // Starts a read of `sectors` data sectors from the position the last command set, into `dst`.
    void read(int sectors, void* dst);
    // Sectors of the oldest unfinished read still to come (0: done, -1: no read pending).
    int read_remaining();
    // True once the last positioning has finished.
    bool ready();

    // Where a real drive's head would be; the epoch counts every move.
    int head_lba() const { return head_lba_; }
    unsigned head_epoch() const { return head_epoch_; }

private:
    struct Read {
        bool pending = false;
        std::uint8_t* dst = nullptr;
        int count = 0;
        int pos = 0;
        int vblank_start = 0;
        int drive_sectors = 0;
    };

    static constexpr int kQueueSize = 128;
    static constexpr int kSectorsPerSecond2x = 150;

    int position_for(int lba, int vb);
    void seek_start(int lba, int vb);
    bool read_one(Read& r);

    DiscDriveHost host_;
    std::array<Read, kQueueSize> queue_{};
    int cursor_ = 0;       // LBA the next read starts from
    int seek_done_vb_ = 0;
    int phys_head_ = 0;
    int head_lba_ = 0;
    unsigned head_epoch_ = 0;
};

}  // namespace sh::audio
