#pragma once
// Minimal dependency-free test harness (ADR-003: no third-party test framework in
// the engine). KRSG_TEST registers a case; CHECK records failures with location;
// the runner exits non-zero on any failure so ctest reports honestly.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace krsg::test
{

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& Registry()
{
    static std::vector<Case> cases;
    return cases;
}

inline int& FailureCount()
{
    static int failures = 0;
    return failures;
}

inline bool Register(const char* name, void (*fn)())
{
    Registry().push_back(Case{name, fn});
    return true;
}

inline void Check(bool ok, const char* expr, const char* file, int line)
{
    if (!ok) {
        ++FailureCount();
        std::printf("    FAIL %s:%d: %s\n", file, line, expr);
    }
}

inline int RunAll()
{
    int failedCases = 0;
    for (const Case& testCase : Registry()) {
        const int before = FailureCount();
        std::printf("[ RUN  ] %s\n", testCase.name);
        testCase.fn();
        if (FailureCount() > before) {
            ++failedCases;
            std::printf("[ FAIL ] %s\n", testCase.name);
        } else {
            std::printf("[  OK  ] %s\n", testCase.name);
        }
    }
    std::printf("%zu cases, %d failed\n", Registry().size(), failedCases);
    return failedCases == 0 ? 0 : 1;
}

} // namespace krsg::test

#define KRSG_TEST(name)                                                                                                \
    static void krsg_test_##name();                                                                                    \
    static const bool krsg_reg_##name = krsg::test::Register(#name, &krsg_test_##name);                                \
    static void krsg_test_##name()

#define CHECK(expr) krsg::test::Check((expr), #expr, __FILE__, __LINE__)
