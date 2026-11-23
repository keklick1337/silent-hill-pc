// The disc drive model (engine/audio/disc_drive.h): seek time, paced reads, head and epoch.

#include "engine/audio/disc_drive.h"

#include "framework/sh_test.h"

#include <vector>

namespace {

namespace au = sh::audio;

struct Clock {
    int vb = 0;
    bool instant = false;
    int busy = 0;
};

au::DiscDrive make(Clock& c) {
    return au::DiscDrive(au::DiscDriveHost{
        [&c] { return c.vb; }, [] { return 60; }, [] { return true; }, [] { return 1000; },
        [](int lba, void* dst) { *static_cast<std::uint8_t*>(dst) = static_cast<std::uint8_t>(lba); return true; },
        [&c] { return c.instant; }, [&c] { ++c.busy; }});
}

// 00:02:00 + lba, BCD.
void pos(int lba, std::uint8_t out[4]) {
    const int t = lba + 150;
    auto bcd = [](int v) { return static_cast<std::uint8_t>(v / 10 * 16 + v % 10); };
    out[0] = bcd(t / 75 / 60);
    out[1] = bcd(t / 75 % 60);
    out[2] = bcd(t % 75);
    out[3] = 1;
}

}  // namespace

SH_TEST(disc_drive_seek_time_matches_the_model) {
    SH_CHECK(au::DiscDrive::seek_vblanks(0, 0, 60) == 3);
    SH_CHECK(au::DiscDrive::seek_vblanks(0, 100, 60) == 3);
    SH_CHECK(au::DiscDrive::seek_vblanks(0, 100000, 60) > 18);
    SH_CHECK(au::DiscDrive::seek_vblanks(0, 100000, 60) == au::DiscDrive::seek_vblanks(100000, 0, 60));
}

SH_TEST(disc_drive_paced_read_delivers_by_time) {
    Clock c;
    au::DiscDrive d = make(c);
    std::uint8_t p[4];
    pos(10, p);
    SH_CHECK(d.command(au::DiscCommand::SetLocation, p));
    SH_CHECK(d.head_lba() == 10 && d.head_epoch() == 1);
    std::vector<std::uint8_t> buf(2048 * 4);
    d.read(4, buf.data());
    SH_CHECK(d.head_epoch() == 2);
    SH_CHECK(d.read_remaining() == 4);  // positioning not finished: nothing yet
    SH_CHECK(c.busy == 1);
    c.vb = 20;
    SH_CHECK(d.read_remaining() == 0);  // 150 sectors/s: all four are due
    SH_CHECK(buf[0] == 10 && buf[2048 * 3] == 13);
    SH_CHECK(d.head_lba() == 14);
    SH_CHECK(d.read_remaining() == -1);
}

SH_TEST(disc_drive_instant_and_seek_ready) {
    Clock c;
    au::DiscDrive d = make(c);
    std::uint8_t p[4];
    pos(500, p);
    SH_CHECK(d.command(au::DiscCommand::SeekAudio, p));
    SH_CHECK(!d.ready());
    c.vb = 100;
    SH_CHECK(d.ready());
    c.instant = true;
    pos(50000, p);
    d.command(au::DiscCommand::SeekAudio, p);
    SH_CHECK(d.ready());
    std::vector<std::uint8_t> buf(2048 * 3);
    d.read(3, buf.data());
    SH_CHECK(d.read_remaining() == 0);
}
