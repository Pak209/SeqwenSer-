#pragma once
// Minimal self-registering test harness (no external dependency).

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace tf {

struct TestCase
{
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

inline int& failures()
{
    static int f = 0;
    return f;
}

inline int& checks()
{
    static int c = 0;
    return c;
}

struct Registrar
{
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({ name, std::move(fn) }); }
};

inline void fail(const char* file, int line, const std::string& msg)
{
    ++failures();
    std::cout << "    FAIL " << file << ":" << line << "  " << msg << "\n";
}

inline int runAll(const char* suite)
{
    int failedTests = 0;
    for (auto& t : registry())
    {
        const int before = failures();
        std::cout << "[ RUN  ] " << t.name << "\n";
        try { t.fn(); }
        catch (const std::exception& e) { fail(__FILE__, __LINE__, std::string("exception: ") + e.what()); }
        const bool ok = failures() == before;
        if (!ok) ++failedTests;
        std::cout << (ok ? "[  OK  ] " : "[FAILED] ") << t.name << "\n";
    }
    std::cout << "\n" << suite << ": " << (registry().size() - static_cast<size_t>(failedTests)) << "/" << registry().size()
              << " tests passed, " << checks() << " checks, " << failures() << " failed checks\n";
    return failedTests == 0 ? 0 : 1;
}

} // namespace tf

#define TF_CAT2(a, b) a##b
#define TF_CAT(a, b) TF_CAT2(a, b)
#define TEST_CASE(name)                                                      \
    static void TF_CAT(tf_test_, __LINE__)();                                \
    static tf::Registrar TF_CAT(tf_reg_, __LINE__)(name, &TF_CAT(tf_test_, __LINE__)); \
    static void TF_CAT(tf_test_, __LINE__)()

#define CHECK(cond)                                                          \
    do { ++tf::checks(); if (!(cond)) tf::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_NEAR(a, b, tol)                                                \
    do {                                                                     \
        ++tf::checks();                                                      \
        const double tf_a = (a), tf_b = (b);                                 \
        if (!(std::abs(tf_a - tf_b) <= (tol))) {                             \
            std::ostringstream tf_os;                                        \
            tf_os << "CHECK_NEAR(" #a ", " #b ", " #tol ") got " << tf_a << " vs " << tf_b; \
            tf::fail(__FILE__, __LINE__, tf_os.str());                       \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        ++tf::checks();                                                      \
        const auto tf_a = (a); const auto tf_b = (b);                        \
        if (!(tf_a == tf_b)) {                                               \
            std::ostringstream tf_os;                                        \
            tf_os << "CHECK_EQ(" #a ", " #b ") got " << tf_a << " vs " << tf_b; \
            tf::fail(__FILE__, __LINE__, tf_os.str());                       \
        }                                                                    \
    } while (0)

#define INFO(x) do { std::cout << "    " << x << "\n"; } while (0)
