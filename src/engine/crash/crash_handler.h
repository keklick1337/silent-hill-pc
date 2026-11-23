// SPDX-License-Identifier: GPL-3.0-or-later
//
// The report of a fatal fault. When the process is about to die of a memory fault, an illegal instruction, an
// arithmetic trap or abort(), the handler first writes what happened and the chain of calls that led there, every
// frame as module+offset (`addr2line -f -C -e <module> <offset>` names the function), and then lets the fault take
// its normal course: the exit status, the core dump and a waiting debugger see the original fault.
//
// The report goes to stderr and to the log file, after the lines the journal still held in memory (those are the
// lines just before the fault, and without the handler they die with the process). Without a log file it goes to
// the crash file given to install().
//
// POSIX: signal handlers on their own stack, so a stack overflow is reported too; everything the handler calls is
// async-signal-safe. Windows: the unhandled-exception filter; the call chain is unwound from the fault's own
// register state with the system's unwind tables (x64).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sh::crash {

// Arms the handler for the rest of the run. `crash_file` (may be empty: no such file) receives the report when
// there is no log file; its directory is created when the report is written. `build` names the program and its
// version in the report's first line. Calling again replaces both.
void install(std::string_view crash_file, std::string_view build);

// Ends the process with a real fault, to see the report it leaves (console `crash`, the tests): "abort" calls
// abort(), anything else writes through a null pointer.
[[noreturn]] void provoke(std::string_view kind);

// One line of a report, built without the heap and without stdio so it can be composed inside a signal handler.
// Text that does not fit is cut; the line is always terminated.
class ReportLine {
public:
    ReportLine& text(std::string_view s) noexcept;
    ReportLine& decimal(std::int64_t value) noexcept;
    // "0x" and at least `min_digits` hexadecimal digits, upper case.
    ReportLine& hex(std::uint64_t value, int min_digits = 1) noexcept;

    const char* data() const noexcept { return buffer_; }
    std::size_t size() const noexcept { return size_; }
    std::string_view view() const noexcept { return {buffer_, size_}; }

private:
    static constexpr std::size_t kCapacity = 384;
    void put(char c) noexcept;

    char buffer_[kCapacity + 1] = {};
    std::size_t size_ = 0;
};

// The name the system headers give a fatal signal ("SIGSEGV"), or "signal" for one the handler does not arm.
const char* signal_name(int signal_number) noexcept;

}  // namespace sh::crash
