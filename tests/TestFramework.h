#pragma once
// Tiny test framework: just enough to register tests, check conditions and report failures.
// (A real framework like Catch2 or GoogleTest can replace this later; the TEST/CHECK style is the same.)
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testfw {

struct TestCase {
    std::string name;
    void (*run)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures() {
    static int n = 0;
    return n;
}

// Extra text printed with every failure, e.g. which file format a loop is currently testing.
inline std::string& context() {
    static std::string c;
    return c;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void fail(const char* file, int line, const std::string& what) {
    ++failures();
    std::cerr << "    FAIL " << file << ":" << line << "  " << what;
    if (!context().empty()) std::cerr << "   [" << context() << "]";
    std::cerr << "\n";
}

}  // namespace testfw

// Declares a test function and registers it so main() can find it.
#define TEST(name)                                          \
    static void name();                                     \
    static testfw::Registrar registrar_##name(#name, name); \
    static void name()

#define CHECK(cond) \
    do { if (!(cond)) testfw::fail(__FILE__, __LINE__, #cond); } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        const auto& va = (a);                                                           \
        const auto& vb = (b);                                                           \
        if (!(va == vb)) {                                                              \
            std::ostringstream os;                                                      \
            os << #a " == " #b "  (got " << va << " vs " << vb << ")";                  \
            testfw::fail(__FILE__, __LINE__, os.str());                                 \
        }                                                                               \
    } while (0)

#define CHECK_THROWS(expr)                                                  \
    do {                                                                    \
        bool threw = false;                                                 \
        try { (void)(expr); } catch (const std::exception&) { threw = true; } \
        if (!threw) testfw::fail(__FILE__, __LINE__, "expected exception: " #expr); \
    } while (0)
