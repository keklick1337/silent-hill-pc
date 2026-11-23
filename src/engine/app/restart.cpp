#include "engine/app/restart.h"

#include "engine/log/sh_log.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <csignal>
#endif

namespace sh::app {

namespace {

std::vector<std::string>& saved_args() {
    static std::vector<std::string> args;
    return args;
}

bool g_restart = false;
bool g_hook_installed = false;

#ifdef _WIN32
// Set for the new game while the old one waits for it: the new one attaches to the console it leaves behind.
constexpr const wchar_t* kConsoleHandover = L"SH_RESTART_TAKE_CONSOLE";

void relaunch() {
    std::wstring exe(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (n == 0 || n >= exe.size()) {
        std::fprintf(stderr, "[RESTART] cannot find the executable (error %lu)\n", GetLastError());
        return;
    }
    exe.resize(n);
    // A console (show_console) is handed over: this process stays as its owner and waits, so the window and its
    // scrollback live on; without one the new game starts on its own.
    const bool console = GetConsoleWindow() != nullptr;
    SetEnvironmentVariableW(kConsoleHandover, console ? L"1" : nullptr);
    // The original command line as Windows keeps it: no quoting rules to get wrong. No handles are inherited:
    // the new game opens its own files, and the console is shared by attaching, not by handles.
    std::wstring cmd = GetCommandLineW();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::fprintf(stderr, "[RESTART] cannot start the game again (error %lu)\n", GetLastError());
        return;
    }
    CloseHandle(pi.hThread);
    if (!console) {
        CloseHandle(pi.hProcess);
        return;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    if (!GetExitCodeProcess(pi.hProcess, &code)) code = 1;
    CloseHandle(pi.hProcess);
    // The run ends with the new game's exit code; nothing is left to tear down.
    ExitProcess(code);
}
#else
// The standard streams as the game was started with them: the game points stdout/stderr at the log file or the
// null device later, and the new image must get the terminal (or pipe) back, not the redirection.
int g_saved_stdio[3] = {-1, -1, -1};

void keep_stdio() {
    for (int fd = 0; fd < 3; ++fd) {
        if (g_saved_stdio[fd] < 0) {
            g_saved_stdio[fd] = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        }
    }
}

void restore_stdio() {
    for (int fd = 0; fd < 3; ++fd) {
        if (g_saved_stdio[fd] >= 0) {
            (void)dup2(g_saved_stdio[fd], fd);
        }
    }
}

// Every descriptor past the standard three closes in the new image: the log file, sockets and devices opened
// without O_CLOEXEC (and the file locks on them) must not outlive the old game. Marked rather than closed, so
// a failed exec still reports through stderr.
void close_on_exec_all() {
    DIR* dir = opendir("/proc/self/fd");
    if (dir == nullptr) {
        dir = opendir("/dev/fd");
    }
    if (dir != nullptr) {
        const int own = dirfd(dir);
        while (const dirent* e = readdir(dir)) {
            char* end = nullptr;
            const long fd = std::strtol(e->d_name, &end, 10);
            if (end == e->d_name || *end != '\0' || fd < 3 || fd == own) continue;
            const int flags = fcntl(static_cast<int>(fd), F_GETFD);
            if (flags >= 0) (void)fcntl(static_cast<int>(fd), F_SETFD, flags | FD_CLOEXEC);
        }
        closedir(dir);
        return;
    }
    const long max = sysconf(_SC_OPEN_MAX);
    for (int fd = 3; fd < (max > 0 && max < 65536 ? max : 65536); ++fd) {
        const int flags = fcntl(fd, F_GETFD);
        if (flags >= 0) (void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
}

void relaunch() {
    restore_stdio();
    close_on_exec_all();
    // exec keeps the signal mask and ignored signals: a thread that blocked SIGTERM (audio, SDL) would leave the
    // new game deaf to Ctrl+C and to kill/SIGTERM.
    sigset_t none;
    sigemptyset(&none);
    pthread_sigmask(SIG_SETMASK, &none, nullptr);
    for (const int sig : {SIGINT, SIGTERM, SIGHUP}) {
        std::signal(sig, SIG_DFL);
    }
    std::vector<char*> argv;
    for (std::string& a : saved_args()) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    // The same process: the shell keeps waiting for it, and the run's exit code is the new game's.
    execv("/proc/self/exe", argv.data());
    // No /proc (macOS, BSD): the path the game was started with.
    execvp(argv[0], argv.data());
    std::perror("[RESTART] cannot start the game again");
}
#endif

void relaunch_at_exit() {
    if (!g_restart) {
        return;
    }
    // Everything registered after remember_command_line() has torn down by now; stdio is flushed only after the
    // atexit hooks, and exec would drop what is still buffered.
    std::fflush(nullptr);
    relaunch();
}

}  // namespace

void remember_command_line(int argc, char** argv) {
    std::vector<std::string>& args = saved_args();
    args.clear();
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i] != nullptr ? argv[i] : "");
    }
#ifndef _WIN32
    keep_stdio();
#endif
    if (!g_hook_installed) {
        g_hook_installed = std::atexit(relaunch_at_exit) == 0;
    }
}

bool take_over_console() {
#ifdef _WIN32
    wchar_t flag[4] = {};
    const DWORD n = GetEnvironmentVariableW(kConsoleHandover, flag, 4);
    if (n == 0 || n >= 4) {
        return false;
    }
    // Not passed on: a later restart sets it again only when this game still has the console.
    SetEnvironmentVariableW(kConsoleHandover, nullptr);
    return AttachConsole(ATTACH_PARENT_PROCESS) != 0;
#else
    return false;
#endif
}

Result<void> restart() {
    if (saved_args().empty() || !g_hook_installed) {
        return Result<void>::fail("the command line was not kept, the game cannot restart itself");
    }
    SH_LOG("[RESTART] restarting the game with the same command line");
    ShLog_Flush();
    g_restart = true;
    std::exit(0);
}

}  // namespace sh::app
