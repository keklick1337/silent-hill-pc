// The report of a fatal fault: its line builder, and on POSIX a child process that faults under the handler.
#include "engine/crash/crash_handler.h"

#include "framework/sh_test.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using sh::crash::ReportLine;

SH_TEST(crash_report_line_formats_numbers) {
    SH_CHECK(ReportLine().text("a ").decimal(0).text(" ").decimal(-42).text(" ").decimal(1234567).view() ==
             "a 0 -42 1234567");
    SH_CHECK(ReportLine().decimal(std::numeric_limits<std::int64_t>::min()).view() == "-9223372036854775808");
    SH_CHECK(ReportLine().hex(0).view() == "0x0");
    SH_CHECK(ReportLine().hex(0xC0000005u, 8).view() == "0xC0000005");
    SH_CHECK(ReportLine().hex(0x1F, 4).view() == "0x001F");
    SH_CHECK(ReportLine().hex(0x123456789ABCDEF0ull, 16).view() == "0x123456789ABCDEF0");
    SH_CHECK(ReportLine().hex(0xABCDE, 2).view() == "0xABCDE");
}

SH_TEST(crash_report_line_cuts_what_does_not_fit) {
    const std::string big(1000, 'x');
    ReportLine line;
    line.text(big).text("tail").decimal(7).hex(7);
    SH_CHECK(line.size() < big.size());
    SH_CHECK(line.data()[line.size()] == '\0');
    SH_CHECK(line.view().find_first_not_of('x') == std::string_view::npos);
}

#if !defined(_WIN32) && !defined(__SANITIZE_ADDRESS__)

namespace {

struct Death {
    bool by_signal = false;
    int signal = 0;
    std::string report;
};

// Runs a child that arms the handler and faults; its stderr is dropped, the crash file is what is read.
Death die_in_child(const std::string& kind) {
    const fs::path dir = fs::path(SH_PROJECT_ROOT) / ".tmp" / "crash" / kind;
    fs::remove_all(dir);
    const fs::path file = dir / "logs" / "crash.log";
    fs::create_directories(dir);
    Death death;
    const pid_t child = ::fork();
    if (child == 0) {
        if (std::freopen("/dev/null", "w", stderr) == nullptr) {
            ::_exit(3);
        }
        sh::crash::install(file.string(), "test build 1.2.3");
        sh::crash::provoke(kind);
    }
    int status = 0;
    if (child < 0 || ::waitpid(child, &status, 0) != child) {
        return death;
    }
    death.by_signal = WIFSIGNALED(status);
    death.signal = death.by_signal ? WTERMSIG(status) : 0;
    std::ifstream in(file, std::ios::binary);
    death.report.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return death;
}

}  // namespace

SH_TEST(crash_handler_reports_a_memory_fault_and_dies_of_it) {
    const Death death = die_in_child("segv");
    SH_CHECK(death.by_signal && death.signal == SIGSEGV);
    SH_CHECK(death.report.find("[CRASH] test build 1.2.3\n") == 0);
    SH_CHECK(death.report.find("[CRASH] SIGSEGV (signal 11), fault address 0x0000000000000000\n") != std::string::npos);
    SH_CHECK(death.report.find("call chain") != std::string::npos);
    // At least one frame of this program, as module(+offset).
    SH_CHECK(death.report.find("sh_tests") != std::string::npos);
    SH_CHECK(death.report.rfind("[CRASH] end of report\n") == death.report.size() - 22);
}

SH_TEST(crash_handler_reports_abort) {
    const Death death = die_in_child("abort");
    SH_CHECK(death.by_signal && death.signal == SIGABRT);
    SH_CHECK(death.report.find("[CRASH] SIGABRT (signal 6)\n") != std::string::npos);
    SH_CHECK(death.report.find("fault address") == std::string::npos);
}

#endif
