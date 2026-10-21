#include <cstring>

#include "TestFramework.h"

// Usage: diskdive_tests [name-filter]   (runs only tests whose name contains the filter)
int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0, failedTests = 0;

    for (const auto& t : testfw::registry()) {
        if (!filter.empty() && t.name.find(filter) == std::string::npos) continue;
        const int before = testfw::failures();
        testfw::context().clear();
        std::cout << "[ RUN  ] " << t.name << "\n";
        try {
            t.run();
        } catch (const std::exception& e) {
            testfw::fail(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        ++ran;
        if (testfw::failures() > before) {
            ++failedTests;
            std::cout << "[ FAIL ] " << t.name << "\n";
        } else {
            std::cout << "[  OK  ] " << t.name << "\n";
        }
    }

    std::cout << "\n" << ran << " tests run, " << failedTests << " failed.\n";
    return failedTests == 0 ? 0 : 1;
}
