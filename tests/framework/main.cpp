#include "sh_test.h"

#include <cstdio>
#include <cstring>
#include <exception>

namespace sh::test {

namespace {

std::vector<std::string> g_failures;

int run(int argc, char** argv) {
    if (argc > 2) {
        std::printf("usage: %s [name-substring]\n", argv[0]);
        return 2;
    }
    const char* filter = argc == 2 ? argv[1] : nullptr;

    int selected = 0;
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    for (const Case& c : registry()) {
        if (filter && !std::strstr(c.name, filter)) {
            continue;
        }
        ++selected;
        // Printed and flushed before the case runs, so a crash in CI names its case: piped stdout is fully buffered.
        std::printf("RUN  %s\n", c.name);
        std::fflush(stdout);

        g_failures.clear();
        bool skip = false;
        std::string skip_reason;
        std::string error;
        try {
            c.fn();
        } catch (const Skip& s) {
            skip = true;
            skip_reason = s.reason;
        } catch (const std::exception& e) {
            error = e.what();
        } catch (...) {
            error = "unknown exception";
        }

        if (!g_failures.empty() || !error.empty()) {
            ++failed;
            std::printf("FAIL %s\n", c.name);
            for (const std::string& failure : g_failures) {
                std::printf("    %s\n", failure.c_str());
            }
            if (!error.empty()) {
                std::printf("    exception: %s\n", error.c_str());
            }
        } else if (skip) {
            ++skipped;
            std::printf("SKIP %s (%s)\n", c.name, skip_reason.c_str());
        } else {
            ++passed;
            std::printf("PASS %s\n", c.name);
        }
        std::fflush(stdout);
        std::fflush(stderr);
    }

    if (filter && selected == 0) {
        std::printf("no test matches \"%s\"\n", filter);
        return 2;
    }
    std::printf("\n%d passed, %d failed, %d skipped\n", passed, failed, skipped);
    return failed > 0 ? 1 : 0;
}

}  // namespace

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

Registrar::Registrar(const char* name, TestFn fn) {
    registry().push_back(Case{name, fn});
}

void record_failure(const char* file, int line, const char* expr) {
    g_failures.push_back(std::string(file) + ":" + std::to_string(line) + ": check failed: " + expr);
}

}  // namespace sh::test

int main(int argc, char** argv) {
    return sh::test::run(argc, argv);
}
