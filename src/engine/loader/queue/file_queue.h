#pragma once

// The file queue: the one place the game asks for a file. A request names a
// file of the imported file table and either reads it into a buffer the caller owns or only moves
// the drive to it. Requests run strictly in order, one step per update: the read cursor walks a
// request through its read steps, a second cursor follows it and runs the request's after-load step
// (an image upload, an animation bind) once the bytes are in. Callers keep the ticket enqueue gives
// them and ask whether it is loaded; blocking callers drain the queue.
//
// What a step does with a drive, a loose file or an image is the host's business (FileQueueHost);
// the queue owns the order, the cursors, the waits and the retry timers. With the same host answers
// a request completes on the same update as in the original's queue, which the replay hashes depend on.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sh::loader {

enum class FileOp : std::uint8_t {
    None = 0,
    Seek = 1,  // move the drive to the file; completes at once on PC
    Read = 2,  // read the whole file into the request's buffer
};

// What runs once the bytes are in.
enum class AfterLoad : std::uint8_t {
    None = 0,
    Image = 1,      // upload an image (the host reads the target from `extra`)
    Animation = 2,  // bind an animation file (the host reads the binding from `extra`)
};

// Read steps of the request under the read cursor.
enum class ReadStep : std::uint8_t {
    Prepare = 0,  // take the destination buffer
    Wait = 1,     // wait until no request still being finished uses the bytes this read would write
    Locate = 2,   // position the drive at the file
    Start = 3,    // start the transfer
    Sync = 4,     // poll the transfer
    Retry = 5,    // the drive failed: wait, reset the drive after a while, locate again
};

enum class AfterStep : std::uint8_t {
    Begin = 0,  // tell the host the bytes are in, then run the step on the next update
    Run = 2,
};

enum class ReadStart : std::uint8_t {
    Failed,    // retry
    Issued,    // a transfer runs; poll it
    Finished,  // the bytes are already in (a loose file); the request is done on this update
    Served,    // the bytes came from elsewhere; the request is done at the sync step, as an instant transfer
};

enum class ReadPoll : std::uint8_t { Busy, Done, Failed };

inline constexpr std::size_t kRequestExtraBytes = 24;

// Opaque to the queue: what the after-load step needs.
using RequestExtra = std::array<std::byte, kRequestExtraBytes>;

struct FileRequest {
    std::int32_t file = 0;
    FileOp op = FileOp::Read;
    AfterLoad after = AfterLoad::None;
    void* dest = nullptr;
    std::optional<RequestExtra> extra;
};

struct QueueEntry {
    std::int32_t file = 0;
    FileOp op = FileOp::None;
    AfterLoad after = AfterLoad::None;
    void* dest = nullptr;
    void* data = nullptr;     // set by the prepare step
    std::uint32_t bytes = 0;  // file size
    alignas(8) RequestExtra extra{};
};

struct QueueSpec {
    int slots = 32;                    // a power of two
    std::uint32_t sector_bytes = 2048;
    int retry_ticks = 8;               // updates per retry round
    int retry_rounds = 9;              // rounds before the drive is reset
    int wait_slack_vblanks = 600;      // a blocking wait gives up after one vertical blank per pending sector plus this
};

class FileQueueHost {
public:
    virtual ~FileQueueHost() = default;
    virtual bool file_valid(std::int32_t file) = 0;
    virtual std::uint32_t file_bytes(std::int32_t file) = 0;
    // `ticket` identifies the request; the queue reuses entries, a ticket is never reused.
    virtual bool locate(std::int32_t ticket, const QueueEntry& entry) = 0;
    virtual ReadStart start_read(std::int32_t ticket, const QueueEntry& entry) = 0;
    virtual ReadPoll poll_read() = 0;
    virtual bool reset_drive() = 0;
    virtual void after_load_begin(std::int32_t ticket, const QueueEntry& entry) = 0;
    // True when the step is finished; false runs it again on the next update.
    virtual bool after_load(std::int32_t ticket, const QueueEntry& entry) = 0;
};

// What a blocking wait does between updates.
class WaitHooks {
public:
    virtual ~WaitHooks() = default;
    virtual void before_wait() = 0;   // after the budget is taken: the fast drain, if any
    virtual void wait_vblank() = 0;
    virtual void stuck(int waited, int limit) = 0;  // the queue is abandoned right after
};

class FileQueue {
public:
    explicit FileQueue(const QueueSpec& spec = {});

    // Empties the queue; tickets start again from 0.
    void clear();

    // The ticket of the new request, or nothing when the host does not know the file. A full queue
    // is updated until a slot frees.
    std::optional<std::int32_t> enqueue(FileQueueHost& host, const FileRequest& request);

    void update(FileQueueHost& host);

    // Drops every request not yet read and every after-load step not yet run. The entry the read
    // cursor lands on loses its after-load step.
    void reset();

    // Forgets everything pending at once (a stuck wait).
    void abandon();

    // Updates until empty, at most `limit` times; returns the updates done.
    int drain(FileQueueHost& host, int limit);

    // Waits a vertical blank per update until empty, giving up when the wait outruns the drive.
    // Returns false when it gave up.
    bool wait_until_empty(FileQueueHost& host, WaitHooks& hooks);

    bool loaded(std::int32_t ticket) const { return ticket < after_; }
    int length() const { return last_ + 1 - after_; }
    // Sectors the pending reads still have to transfer.
    int pending_sectors() const;

    std::int32_t last_ticket() const { return last_; }
    std::int32_t read_ticket() const { return read_; }
    std::int32_t after_ticket() const { return after_; }
    ReadStep read_step() const { return read_step_; }
    AfterStep after_step() const { return after_step_; }
    int retry_ticks() const { return retry_ticks_; }
    int retry_rounds() const { return retry_rounds_; }
    const QueueEntry& entry(std::int32_t ticket) const { return entries_[slot(ticket)]; }
    const QueueSpec& spec() const { return spec_; }

private:
    std::size_t slot(std::int32_t ticket) const {
        return static_cast<std::size_t>(ticket) & static_cast<std::size_t>(spec_.slots - 1);
    }
    std::uint32_t read_span(const QueueEntry& e) const;
    bool step_read(FileQueueHost& host, QueueEntry& e);
    bool step_after(FileQueueHost& host, QueueEntry& e);
    bool buffer_free(const QueueEntry& e) const;
    bool retry_tick(FileQueueHost& host);

    QueueSpec spec_;
    std::vector<QueueEntry> entries_;
    std::int32_t last_ = -1;   // newest request
    std::int32_t read_ = 0;    // request under the read cursor
    std::int32_t after_ = 0;   // request under the after-load cursor
    ReadStep read_step_ = ReadStep::Prepare;
    AfterStep after_step_ = AfterStep::Begin;
    int retry_ticks_ = 0;
    int retry_rounds_ = 0;
    bool served_ = false;
};

}  // namespace sh::loader
