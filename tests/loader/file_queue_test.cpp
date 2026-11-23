// The file queue (engine/loader/queue/file_queue.h), on a scripted host:
// the update on which each request completes, the cursors, the retry timers and the blocking waits.

#include "engine/loader/queue/file_queue.h"

#include "framework/sh_test.h"

#include <string>
#include <vector>

namespace {

namespace ld = sh::loader;

class FakeHost : public ld::FileQueueHost {
public:
    std::vector<std::string> calls;
    ld::ReadStart start = ld::ReadStart::Issued;
    std::vector<ld::ReadPoll> polls;  // answered in order, then Done
    bool locate_ok = true;
    bool reset_ok = true;
    int resets = 0;
    int after_runs_needed = 1;  // after_load answers true on this call
    int after_runs = 0;

    bool file_valid(std::int32_t file) override { return file >= 0 && file < 100; }
    std::uint32_t file_bytes(std::int32_t file) override { return static_cast<std::uint32_t>(file) * 256u; }
    bool locate(std::int32_t t, const ld::QueueEntry&) override {
        calls.push_back("locate " + std::to_string(t));
        return locate_ok;
    }
    ld::ReadStart start_read(std::int32_t t, const ld::QueueEntry&) override {
        calls.push_back("start " + std::to_string(t));
        return start;
    }
    ld::ReadPoll poll_read() override {
        calls.push_back("poll");
        if (polls.empty()) {
            return ld::ReadPoll::Done;
        }
        const ld::ReadPoll p = polls.front();
        polls.erase(polls.begin());
        return p;
    }
    bool reset_drive() override {
        ++resets;
        return reset_ok;
    }
    void after_load_begin(std::int32_t t, const ld::QueueEntry&) override {
        calls.push_back("begin " + std::to_string(t));
    }
    bool after_load(std::int32_t t, const ld::QueueEntry&) override {
        calls.push_back("after " + std::to_string(t));
        return ++after_runs >= after_runs_needed;
    }
};

ld::FileRequest read(std::int32_t file, void* dest, ld::AfterLoad after = ld::AfterLoad::None) {
    ld::FileRequest r;
    r.file = file;
    r.op = ld::FileOp::Read;
    r.after = after;
    r.dest = dest;
    return r;
}

int updates_until_loaded(ld::FileQueue& q, FakeHost& h, std::int32_t ticket) {
    int n = 0;
    while (!q.loaded(ticket) && n < 1000) {
        q.update(h);
        ++n;
    }
    return n;
}

alignas(8) unsigned char g_buf[64 * 1024];

}  // namespace

SH_TEST(loader_queue_read_steps) {
    ld::FileQueue q;
    FakeHost h;
    const auto t = q.enqueue(h, read(8, g_buf));
    SH_CHECK(t.has_value());
    SH_CHECK(*t == 0);
    SH_CHECK(q.length() == 1);
    // Prepare, wait, locate, start, sync: read on the 5th update, where the after-load cursor
    // already begins; it runs on the 6th.
    for (int i = 0; i < 4; ++i) {
        q.update(h);
    }
    SH_CHECK(q.read_ticket() == 0);
    q.update(h);
    SH_CHECK(q.read_ticket() == 1);
    SH_CHECK(!q.loaded(0));
    q.update(h);
    SH_CHECK(q.loaded(0));
    SH_CHECK(q.length() == 0);
    SH_CHECK(q.entry(0).data == g_buf);
    SH_CHECK(q.entry(0).bytes == 8u * 256u);
}

SH_TEST(loader_queue_seek_and_rejects) {
    ld::FileQueue q;
    FakeHost h;
    ld::FileRequest s;
    s.file = 3;
    s.op = ld::FileOp::Seek;
    const auto t = q.enqueue(h, s);
    SH_CHECK(!q.enqueue(h, read(500, g_buf)).has_value());
    SH_CHECK(q.last_ticket() == 0);
    q.update(h);  // the seek is done; the after-load cursor begins on the same update
    SH_CHECK(q.read_ticket() == 1);
    q.update(h);
    SH_CHECK(q.loaded(*t));
    SH_CHECK(h.calls.size() == 1u);  // only the after-load begin
}

SH_TEST(loader_queue_finished_and_served) {
    ld::FileQueue q;
    FakeHost h;
    h.start = ld::ReadStart::Finished;
    const auto a = q.enqueue(h, read(1, g_buf));
    SH_CHECK(updates_until_loaded(q, h, *a) == 4 + 1);
    h.start = ld::ReadStart::Served;
    h.calls.clear();
    const auto b = q.enqueue(h, read(1, g_buf));
    SH_CHECK(updates_until_loaded(q, h, *b) == 5 + 1);
    for (const std::string& c : h.calls) {
        SH_CHECK(c != "poll");  // a served read never asks the drive
    }
}

SH_TEST(loader_queue_retry_and_reset_drive) {
    ld::FileQueue q;
    FakeHost h;
    h.polls = {ld::ReadPoll::Busy, ld::ReadPoll::Failed};
    const auto t = q.enqueue(h, read(2, g_buf));
    for (int i = 0; i < 6; ++i) {
        q.update(h);  // prepare, wait, locate, start, busy, failed
    }
    SH_CHECK(q.read_step() == ld::ReadStep::Retry);
    for (int i = 0; i < 7; ++i) {
        q.update(h);
    }
    SH_CHECK(q.read_step() == ld::ReadStep::Retry);
    q.update(h);  // 8th tick: back to locate
    SH_CHECK(q.read_step() == ld::ReadStep::Locate);
    SH_CHECK(q.retry_rounds() == 1);
    SH_CHECK(updates_until_loaded(q, h, *t) == 3 + 1);
    SH_CHECK(q.retry_rounds() == 0);

    // Locate always failing: the drive is reset after 9 rounds of 8 updates.
    h.locate_ok = false;
    q.enqueue(h, read(2, g_buf));
    for (int i = 0; i < 2 + 9 * 9; ++i) {
        q.update(h);
    }
    SH_CHECK(h.resets == 1);
    SH_CHECK(q.retry_rounds() == 0);
}

SH_TEST(loader_queue_waits_for_buffer_in_use) {
    ld::FileQueue q;
    FakeHost h;
    h.after_runs_needed = 4;  // the image upload of the first request takes four updates
    const auto a = q.enqueue(h, read(8, g_buf, ld::AfterLoad::Image));
    const auto b = q.enqueue(h, read(8, g_buf + 1024));  // overlaps the first file
    for (int i = 0; i < 5; ++i) {
        q.update(h);
    }
    SH_CHECK(q.read_ticket() == 1);
    // b prepares, then waits while a's upload runs.
    q.update(h);
    SH_CHECK(q.read_step() == ld::ReadStep::Wait);
    q.update(h);
    q.update(h);
    SH_CHECK(q.read_step() == ld::ReadStep::Wait);
    SH_CHECK(!q.loaded(*a));
    q.update(h);  // a's fourth run: done; b still waits this update
    SH_CHECK(q.loaded(*a));
    q.update(h);
    SH_CHECK(q.read_step() == ld::ReadStep::Locate);
    SH_CHECK(updates_until_loaded(q, h, *b) == 3 + 1);

    // A file ending where the other begins does not overlap; the other's span is its file size.
    ld::FileQueue q2;
    FakeHost h2;
    h2.after_runs_needed = 100;
    q2.enqueue(h2, read(1, g_buf, ld::AfterLoad::Image));  // 256 bytes at g_buf
    q2.enqueue(h2, read(1, g_buf + 256));
    for (int i = 0; i < 7; ++i) {
        q2.update(h2);
    }
    SH_CHECK(q2.read_step() == ld::ReadStep::Locate);
}

SH_TEST(loader_queue_full_queue_updates) {
    ld::QueueSpec spec;
    spec.slots = 4;
    ld::FileQueue q(spec);
    FakeHost h;
    for (int i = 0; i < 4; ++i) {
        q.enqueue(h, read(1, g_buf));
    }
    SH_CHECK(q.length() == 4);
    const auto t = q.enqueue(h, read(1, g_buf));
    SH_CHECK(*t == 4);
    SH_CHECK(q.loaded(0));
    SH_CHECK(q.length() == 4);
}

SH_TEST(loader_queue_reset) {
    ld::FileQueue q;
    FakeHost h;
    q.enqueue(h, read(1, g_buf, ld::AfterLoad::Image));
    q.enqueue(h, read(1, g_buf + 4096, ld::AfterLoad::Image));
    q.enqueue(h, read(1, g_buf + 8192));
    for (int i = 0; i < 3; ++i) {
        q.update(h);  // first request at its start step
    }
    q.reset();
    // The request in flight continues under a new ticket that names the same entry; nothing else
    // is left, and it lost its after-load step.
    SH_CHECK(q.read_ticket() == 32);
    SH_CHECK(q.last_ticket() == 32);
    SH_CHECK(q.after_ticket() == 32);
    SH_CHECK(q.length() == 1);
    SH_CHECK(q.read_step() == ld::ReadStep::Start);
    SH_CHECK(q.entry(32).after == ld::AfterLoad::None);
    SH_CHECK(updates_until_loaded(q, h, 32) == 2 + 1);
    q.reset();  // empty: nothing happens
    SH_CHECK(q.read_ticket() == 33);
}

SH_TEST(loader_queue_drain_and_pending) {
    ld::FileQueue q;
    FakeHost h;
    q.enqueue(h, read(8, g_buf));    // 2048 bytes: 1 sector
    q.enqueue(h, read(9, g_buf));    // 2304 bytes: 2 sectors
    ld::FileRequest s;
    s.file = 50;
    s.op = ld::FileOp::Seek;
    q.enqueue(h, s);
    SH_CHECK(q.pending_sectors() == 3);
    SH_CHECK(q.drain(h, 3) == 3);
    SH_CHECK(q.length() == 3);
    const int more = q.drain(h, 1000);
    SH_CHECK(q.length() == 0);
    SH_CHECK(more == 5 + 5 + 1 + 2 - 3);
}

namespace {

class FakeWait : public ld::WaitHooks {
public:
    int vblanks = 0;
    bool before = false;
    int stuck_waited = -1;
    void before_wait() override { before = true; }
    void wait_vblank() override { ++vblanks; }
    void stuck(int waited, int) override { stuck_waited = waited; }
};

}  // namespace

SH_TEST(loader_queue_wait_until_empty) {
    ld::FileQueue q;
    FakeHost h;
    q.enqueue(h, read(8, g_buf));
    FakeWait w;
    SH_CHECK(q.wait_until_empty(h, w));
    SH_CHECK(w.before);
    SH_CHECK(w.vblanks == 6 + 1);  // one per update, and the one that sees it empty
    FakeWait idle;
    SH_CHECK(q.wait_until_empty(h, idle));
    SH_CHECK(idle.vblanks == 1);

    // A drive that never answers: given up after 1 sector + 600 vertical blanks.
    h.polls.assign(5000, ld::ReadPoll::Busy);
    q.enqueue(h, read(8, g_buf));
    FakeWait stuck;
    SH_CHECK(!q.wait_until_empty(h, stuck));
    SH_CHECK(stuck.stuck_waited == 602);
    SH_CHECK(q.length() == 0);
}
