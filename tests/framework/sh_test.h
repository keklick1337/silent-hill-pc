#pragma once

#include <string>
#include <vector>

namespace sh::test {

using TestFn = void (*)();

struct Case {
    const char* name;
    TestFn fn;
};

// Cases are collected during static initialisation, in registration order.
std::vector<Case>& registry();

struct Registrar {
    Registrar(const char* name, TestFn fn);
};

// Thrown by SH_SKIP. The runner reports the case as skipped, not failed.
struct Skip {
    std::string reason;
};

// Called by SH_CHECK. Failures are counted and the case keeps running, so one run shows several problems.
void record_failure(const char* file, int line, const char* expr);

}  // namespace sh::test

#define SH_TEST_CONCAT_INNER(a, b) a##b
#define SH_TEST_CONCAT(a, b) SH_TEST_CONCAT_INNER(a, b)

#define SH_TEST(name)                                                                  \
    static void name();                                                                \
    static const ::sh::test::Registrar SH_TEST_CONCAT(sh_registrar_, name){#name, &name}; \
    static void name()

#define SH_CHECK(expr)                                                                 \
    do {                                                                               \
        if (!(expr)) {                                                                 \
            ::sh::test::record_failure(__FILE__, __LINE__, #expr);                     \
        }                                                                              \
    } while (0)

#define SH_SKIP(reason) throw ::sh::test::Skip{reason}
