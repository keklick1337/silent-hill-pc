// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/crash/crash_handler.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/log/log.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
// Android's libc has backtrace() only from API level 33.
#if __has_include(<execinfo.h>) && (!defined(__ANDROID__) || __ANDROID_API__ >= 33)
#include <execinfo.h>
#define SH_CRASH_HAS_BACKTRACE 1
#endif
#endif

namespace sh::crash {

void ReportLine::put(char c) noexcept {
    if (size_ < kCapacity) {
        buffer_[size_++] = c;
        buffer_[size_] = '\0';
    }
}

ReportLine& ReportLine::text(std::string_view s) noexcept {
    for (const char c : s) {
        put(c);
    }
    return *this;
}

ReportLine& ReportLine::decimal(std::int64_t value) noexcept {
    // The magnitude in unsigned arithmetic: the most negative value has no positive twin.
    std::uint64_t rest = value < 0 ? 0 - static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(value);
    char digits[20];
    int count = 0;
    do {
        digits[count++] = static_cast<char>('0' + rest % 10);
        rest /= 10;
    } while (rest != 0);
    if (value < 0) {
        put('-');
    }
    while (count > 0) {
        put(digits[--count]);
    }
    return *this;
}

ReportLine& ReportLine::hex(std::uint64_t value, int min_digits) noexcept {
    int digits = 1;
    for (std::uint64_t rest = value >> 4; rest != 0; rest >>= 4) {
        ++digits;
    }
    digits = std::clamp(std::max(digits, min_digits), 1, 16);
    put('0');
    put('x');
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        put("0123456789ABCDEF"[(value >> shift) & 0xF]);
    }
    return *this;
}

namespace {

// Set once by install() before the handler is armed and only read by it afterwards.
struct Setup {
    char crash_file[1024] = {};
    char crash_dir[1024] = {};
    char build[128] = {};
};
Setup g_setup;

void remember(char* out, std::size_t capacity, std::string_view value) {
    const std::size_t n = std::min(value.size(), capacity - 1);
    std::memcpy(out, value.data(), n);
    out[n] = '\0';
}

[[maybe_unused]] constexpr int kMaxFrames = 64;  // unused where no backtrace is available (Android before API 33)

#ifndef _WIN32

void write_all(int fd, const char* bytes, std::size_t size) noexcept {
    while (size > 0) {
        const ssize_t written = ::write(fd, bytes, size);
        if (written <= 0) {
            return;  // a full disk or a closed terminal: the other outputs still get the report
        }
        bytes += written;
        size -= static_cast<std::size_t>(written);
    }
}

bool same_file(int a, int b) noexcept {
    struct stat sa {};
    struct stat sb {};
    return ::fstat(a, &sa) == 0 && ::fstat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

// The files one report is written to.
class Outputs {
public:
    Outputs() noexcept {
        const sh::log::FileTail log = sh::log::unwritten_file_tail();
        if (log.descriptor >= 0) {
            // First what the journal had not written: the report then follows the last lines before the fault.
            write_all(log.descriptor, log.bytes, log.size);
            add(log.descriptor);
        } else if (g_setup.crash_file[0] != '\0') {
            if (g_setup.crash_dir[0] != '\0') {
                ::mkdir(g_setup.crash_dir, 0755);
            }
            opened_ = ::open(g_setup.crash_file, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
            add(opened_);
        }
        // The game may have pointed stderr at its log file: one copy there is enough.
        if (count_ == 0 || !same_file(STDERR_FILENO, files_[0])) {
            add(STDERR_FILENO);
        }
    }

    ~Outputs() {
        if (opened_ >= 0) {
            ::close(opened_);
        }
    }

    void write(const ReportLine& line) const noexcept {
        for (int i = 0; i < count_; ++i) {
            write_all(files_[i], line.data(), line.size());
            write_all(files_[i], "\n", 1);
        }
    }

    void write_frames(void* const* frames, int count) const noexcept {
#ifdef SH_CRASH_HAS_BACKTRACE
        for (int i = 0; i < count_; ++i) {
            ::backtrace_symbols_fd(frames, count, files_[i]);
        }
#else
        (void)frames;
        (void)count;
#endif
    }

private:
    void add(int fd) noexcept {
        if (fd >= 0) {
            files_[count_++] = fd;
        }
    }

    int files_[2] = {-1, -1};
    int count_ = 0;
    int opened_ = -1;
};

constexpr int kFatalSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

// The handler runs here, so a fault caused by an exhausted stack can still be reported.
alignas(16) char g_handler_stack[128 * 1024];

volatile std::sig_atomic_t g_reporting = 0;

void on_fatal_signal(int number, siginfo_t* info, void* /*context*/) {
    // A fault inside the report itself: nothing more can be said, die of it.
    if (g_reporting == 0) {
        g_reporting = 1;
        const Outputs out;
        if (g_setup.build[0] != '\0') {
            out.write(ReportLine().text("[CRASH] ").text(g_setup.build));
        }
        ReportLine what;
        what.text("[CRASH] ").text(signal_name(number)).text(" (signal ").decimal(number).text(")");
        // A signal sent by kill() or raise() carries no address.
        if (number != SIGABRT && info != nullptr && info->si_code > 0) {
            what.text(", fault address ").hex(reinterpret_cast<std::uintptr_t>(info->si_addr), 16);
        }
        out.write(what);
#ifdef SH_CRASH_HAS_BACKTRACE
        void* frames[kMaxFrames];
        const int count = ::backtrace(frames, kMaxFrames);
        out.write(ReportLine().text("[CRASH] call chain, innermost first; the first frames are the handler's own. ")
                      .text("Name a frame with: addr2line -f -C -e <module> <+offset>"));
        out.write_frames(frames, count);
#endif
        out.write(ReportLine().text("[CRASH] end of report"));
    }
    // The default action again, and the same signal: the process ends the way it would have without the handler.
    ::signal(number, SIG_DFL);
    ::raise(number);
}

void arm() {
    stack_t stack{};
    stack.ss_sp = g_handler_stack;
    stack.ss_size = sizeof(g_handler_stack);
    ::sigaltstack(&stack, nullptr);

#ifdef SH_CRASH_HAS_BACKTRACE
    // The first backtrace() loads the unwinder's library, which allocates: do that now, not inside the handler.
    void* warm_up[2];
    ::backtrace(warm_up, 2);
#endif

    struct sigaction action {};
    action.sa_sigaction = on_fatal_signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);  // a macro on macOS: no :: qualifier
    for (const int number : kFatalSignals) {
        ::sigaction(number, &action, nullptr);
    }
}

#else  // _WIN32

bool same_file(HANDLE a, HANDLE b) noexcept {
    BY_HANDLE_FILE_INFORMATION ia{};
    BY_HANDLE_FILE_INFORMATION ib{};
    return GetFileInformationByHandle(a, &ia) && GetFileInformationByHandle(b, &ib) &&
           ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber && ia.nFileIndexHigh == ib.nFileIndexHigh &&
           ia.nFileIndexLow == ib.nFileIndexLow;
}

bool usable(HANDLE h) noexcept { return h != nullptr && h != INVALID_HANDLE_VALUE; }

// The files one report is written to.
class Outputs {
public:
    Outputs() noexcept {
        const sh::log::FileTail log = sh::log::unwritten_file_tail();
        if (log.descriptor >= 0) {
            add(reinterpret_cast<HANDLE>(_get_osfhandle(log.descriptor)));
        } else if (g_setup.crash_file[0] != '\0') {
            if (g_setup.crash_dir[0] != '\0') {
                CreateDirectoryA(g_setup.crash_dir, nullptr);
            }
            opened_ = CreateFileA(g_setup.crash_file, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
            add(opened_);
        }
        // The game may have pointed stderr at its log file: one copy there is enough.
        const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        if (usable(err) && (count_ == 0 || !same_file(err, files_[0]))) {
            add(err);
        }
    }

    ~Outputs() {
        if (usable(opened_)) {
            CloseHandle(opened_);
        }
    }

    void write(const ReportLine& line) const noexcept {
        for (int i = 0; i < count_; ++i) {
            DWORD written = 0;
            WriteFile(files_[i], line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
            WriteFile(files_[i], "\r\n", 2, &written, nullptr);
        }
    }

private:
    void add(HANDLE h) noexcept {
        if (usable(h)) {
            files_[count_++] = h;
        }
    }

    HANDLE files_[2] = {nullptr, nullptr};
    int count_ = 0;
    HANDLE opened_ = INVALID_HANDLE_VALUE;
};

// "<module file>+0x<offset>" of a code address, or "?" outside every loaded module.
void append_place(ReportLine& line, DWORD64 address) noexcept {
    HMODULE module = nullptr;
    char path[MAX_PATH];
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(static_cast<std::uintptr_t>(address)), &module) ||
        module == nullptr || GetModuleFileNameA(module, path, sizeof(path)) == 0) {
        line.text("?");
        return;
    }
    path[sizeof(path) - 1] = '\0';
    std::string_view name = path;
    if (const std::size_t slash = name.find_last_of("\\/"); slash != std::string_view::npos) {
        name.remove_prefix(slash + 1);
    }
    line.text(name).text("+").hex(address - reinterpret_cast<std::uintptr_t>(module));
}

void write_call_chain(const Outputs& out, const CONTEXT& at_fault) noexcept {
#if defined(_M_X64) || defined(__x86_64__)
    out.write(ReportLine().text("[CRASH] call chain, innermost first. ")
                  .text("Name a frame with: addr2line -f -C -e <module> <offset>"));
    CONTEXT regs = at_fault;
    for (int depth = 0; depth < kMaxFrames && regs.Rip != 0; ++depth) {
        ReportLine frame;
        frame.text("[CRASH]   #").decimal(depth).text(" ").hex(regs.Rip, 16).text(" ");
        append_place(frame, regs.Rip);
        out.write(frame);

        DWORD64 image_base = 0;
        if (const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(regs.Rip, &image_base, nullptr)) {
            PVOID handler_data = nullptr;
            DWORD64 frame_base = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, regs.Rip, function, &regs, &handler_data, &frame_base,
                             nullptr);
        } else if (regs.Rsp != 0) {
            // A function without unwind data moved no stack: its return address is on top.
            regs.Rip = *reinterpret_cast<const DWORD64*>(static_cast<std::uintptr_t>(regs.Rsp));
            regs.Rsp += sizeof(DWORD64);
        } else {
            break;
        }
    }
#else
    (void)out;
    (void)at_fault;
#endif
}

LONG WINAPI on_unhandled_exception(EXCEPTION_POINTERS* fault) {
    // The journal's buffered lines first, so the report follows the last lines before the fault.
    if (g_ShDebugLog != nullptr) {
        std::fflush(g_ShDebugLog);
    }
    const Outputs out;
    const EXCEPTION_RECORD& record = *fault->ExceptionRecord;
    if (g_setup.build[0] != '\0') {
        out.write(ReportLine().text("[CRASH] ").text(g_setup.build));
    }
    const DWORD64 where = reinterpret_cast<std::uintptr_t>(record.ExceptionAddress);
    ReportLine what;
    what.text("[CRASH] exception ").hex(record.ExceptionCode, 8).text(" at ").hex(where, 16).text(" ");
    append_place(what, where);
    out.write(what);
    const bool memory = record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
                        record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR;
    if (memory && record.NumberParameters >= 2) {
        const ULONG_PTR kind = record.ExceptionInformation[0];
        out.write(ReportLine().text("[CRASH] ")
                      .text(kind == 1 ? "writing" : kind == 8 ? "executing" : "reading")
                      .text(" address ")
                      .hex(record.ExceptionInformation[1], 16));
    }
    write_call_chain(out, *fault->ContextRecord);
    out.write(ReportLine().text("[CRASH] end of report"));
    // The system's own handling goes on: a debugger or the error reporting still gets the exception.
    return EXCEPTION_CONTINUE_SEARCH;
}

void arm() { SetUnhandledExceptionFilter(on_unhandled_exception); }

#endif

}  // namespace

const char* signal_name(int signal_number) noexcept {
#ifndef _WIN32
    switch (signal_number) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        case SIGABRT: return "SIGABRT";
        default: break;
    }
#else
    (void)signal_number;
#endif
    return "signal";
}

void provoke(std::string_view kind) {
    if (kind != "abort") {
        // Volatile, or the compiler removes a store it can prove invalid.
        volatile int* const nowhere = nullptr;
        *nowhere = 1;
    }
    std::abort();
}

void install(std::string_view crash_file, std::string_view build) {
    remember(g_setup.crash_file, sizeof(g_setup.crash_file), crash_file);
    const std::size_t slash = crash_file.find_last_of("/\\");
    remember(g_setup.crash_dir, sizeof(g_setup.crash_dir),
             slash == std::string_view::npos ? std::string_view{} : crash_file.substr(0, slash));
    remember(g_setup.build, sizeof(g_setup.build), build);
    arm();
}

}  // namespace sh::crash
