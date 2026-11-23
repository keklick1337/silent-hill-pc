#include "engine/loader/queue/file_queue.h"

#include <cstdint>

namespace sh::loader {

FileQueue::FileQueue(const QueueSpec& spec) : spec_(spec) {
    if (spec_.slots < 1 || (spec_.slots & (spec_.slots - 1)) != 0) {
        spec_.slots = 32;
    }
    if (spec_.sector_bytes == 0) {
        spec_.sector_bytes = 2048;
    }
    clear();
}

void FileQueue::clear() {
    entries_.assign(static_cast<std::size_t>(spec_.slots), QueueEntry{});
    last_ = -1;
    read_ = 0;
    after_ = 0;
    read_step_ = ReadStep::Prepare;
    after_step_ = AfterStep::Begin;
    retry_ticks_ = 0;
    retry_rounds_ = 0;
    served_ = false;
}

std::uint32_t FileQueue::read_span(const QueueEntry& e) const {
    const std::uint32_t s = spec_.sector_bytes;
    return (e.bytes + s - 1) / s * s;
}

std::optional<std::int32_t> FileQueue::enqueue(FileQueueHost& host, const FileRequest& request) {
    if (!host.file_valid(request.file)) {
        return std::nullopt;
    }
    while (length() >= spec_.slots) {
        update(host);
    }
    ++last_;
    QueueEntry& e = entries_[slot(last_)];
    e.file = request.file;
    e.op = request.op;
    e.after = request.after;
    e.dest = request.dest;
    e.data = nullptr;
    e.bytes = host.file_bytes(request.file);
    if (request.extra) {
        e.extra = *request.extra;
    } else {
        e.extra = RequestExtra{};
    }
    return last_;
}

void FileQueue::update(FileQueueHost& host) {
    if (read_ <= last_) {
        QueueEntry& e = entries_[slot(read_)];
        bool done = false;
        if (e.op == FileOp::Seek) {
            // No drive to move on PC: the seek is over on its first update.
            done = true;
        } else if (e.op == FileOp::Read) {
            done = step_read(host, e);
        }
        if (done) {
            read_step_ = ReadStep::Prepare;
            retry_ticks_ = 0;
            retry_rounds_ = 0;
            ++read_;
        }
    } else {
        read_step_ = ReadStep::Prepare;
    }

    if (after_ < read_) {
        if (step_after(host, entries_[slot(after_)])) {
            after_step_ = AfterStep::Begin;
            ++after_;
        }
    } else {
        after_step_ = AfterStep::Begin;
    }
}

bool FileQueue::step_read(FileQueueHost& host, QueueEntry& e) {
    switch (read_step_) {
    case ReadStep::Prepare:
        e.data = e.dest;
        if (e.data != nullptr) {
            read_step_ = ReadStep::Wait;
        }
        return false;
    case ReadStep::Wait:
        if (buffer_free(e)) {
            read_step_ = ReadStep::Locate;
        }
        return false;
    case ReadStep::Locate:
        read_step_ = host.locate(read_, e) ? ReadStep::Start : ReadStep::Retry;
        return false;
    case ReadStep::Start:
        switch (host.start_read(read_, e)) {
        case ReadStart::Failed:
            read_step_ = ReadStep::Retry;
            return false;
        case ReadStart::Finished:
            return true;
        case ReadStart::Served:
            served_ = true;
            read_step_ = ReadStep::Sync;
            return false;
        case ReadStart::Issued:
            read_step_ = ReadStep::Sync;
            return false;
        }
        return false;
    case ReadStep::Sync:
        if (served_) {
            served_ = false;
            return true;
        }
        switch (host.poll_read()) {
        case ReadPoll::Failed:
            read_step_ = ReadStep::Retry;
            return false;
        case ReadPoll::Done:
            return true;
        case ReadPoll::Busy:
            return false;
        }
        return false;
    case ReadStep::Retry:
        if (retry_tick(host)) {
            read_step_ = ReadStep::Locate;
        }
        return false;
    }
    return false;
}

bool FileQueue::retry_tick(FileQueueHost& host) {
    if (++retry_ticks_ < spec_.retry_ticks) {
        return false;
    }
    retry_ticks_ = 0;
    if (++retry_rounds_ < spec_.retry_rounds) {
        return true;
    }
    if (!host.reset_drive()) {
        return false;
    }
    retry_rounds_ = 0;
    return true;
}

bool FileQueue::buffer_free(const QueueEntry& e) const {
    // The requests between the two cursors are read but not finished: one with an after-load step
    // still needs its bytes. The other request's span is its file size, this one's whole sectors.
    const auto mine = reinterpret_cast<std::uintptr_t>(e.data);
    const std::uintptr_t mine_end = mine + read_span(e);
    for (std::int32_t t = after_; t < read_; ++t) {
        const QueueEntry& o = entries_[slot(t)];
        if (o.after == AfterLoad::None) {
            continue;
        }
        const auto other = reinterpret_cast<std::uintptr_t>(o.data);
        if (!(other >= mine_end || mine >= other + o.bytes)) {
            return false;
        }
    }
    return true;
}

bool FileQueue::step_after(FileQueueHost& host, QueueEntry& e) {
    if (after_step_ == AfterStep::Begin) {
        host.after_load_begin(after_, e);
        after_step_ = AfterStep::Run;
        return false;
    }
    switch (e.after) {
    case AfterLoad::None:
        return true;
    case AfterLoad::Image:
    case AfterLoad::Animation:
        return host.after_load(after_, e);
    }
    return false;
}

void FileQueue::reset() {
    if (length() <= 0) {
        return;
    }
    // The read step is kept: a transfer in flight finishes under the new ticket, which names the
    // same entry.
    if (read_ <= last_) {
        read_ += spec_.slots;
        last_ = read_;
    }
    after_ = read_;
    after_step_ = AfterStep::Begin;
    entries_[slot(read_)].after = AfterLoad::None;
}

void FileQueue::abandon() {
    read_ = last_ + 1;
    after_ = read_;
}

int FileQueue::drain(FileQueueHost& host, int limit) {
    int done = 0;
    while (length() > 0 && limit-- > 0) {
        update(host);
        ++done;
    }
    return done;
}

int FileQueue::pending_sectors() const {
    int sectors = 0;
    for (std::int32_t t = read_; t <= last_; ++t) {
        const QueueEntry& e = entries_[slot(t)];
        if (e.op == FileOp::Read) {
            sectors += static_cast<int>((e.bytes + spec_.sector_bytes - 1) / spec_.sector_bytes);
        }
    }
    return sectors;
}

bool FileQueue::wait_until_empty(FileQueueHost& host, WaitHooks& hooks) {
    // A working drive reads at least one sector per vertical blank, so one per pending sector plus
    // the slack for locates and after-load steps cannot be outrun; the limit grows when requests
    // are added during the wait.
    int pending = pending_sectors();
    int limit = pending + spec_.wait_slack_vblanks;
    int waited = 0;
    hooks.before_wait();
    for (;;) {
        hooks.wait_vblank();
        if (length() <= 0) {
            return true;
        }
        const int now = pending_sectors();
        if (now > pending) {
            limit += now - pending;
        }
        pending = now;
        if (++waited > limit) {
            hooks.stuck(waited, limit);
            abandon();
            return false;
        }
        update(host);
    }
}

}  // namespace sh::loader
