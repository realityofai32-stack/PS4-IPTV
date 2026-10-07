// Minimal test harness for the host-side (Windows/MSVC) unit tests.
#ifndef PS4IPTV_TESTS_CHECK_H
#define PS4IPTV_TESTS_CHECK_H

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace check {
    struct Registry {
        std::vector<std::pair<const char *, std::function<void()>>> tests;
        int checks = 0;
        int failures = 0;

        static Registry &get() {
            static Registry r;
            return r;
        }
    };

    struct Register {
        Register(const char *name, std::function<void()> fn) {
            Registry::get().tests.emplace_back(name, std::move(fn));
        }
    };
}

#define TEST(name) \
    static void name(); \
    static check::Register name##_reg(#name, name); \
    static void name()

#define CHECK(cond) do { check::Registry::get().checks++; if (!(cond)) { check::Registry::get().failures++; \
    std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_EQ(a, b) do { check::Registry::get().checks++; auto _va = (a); auto _vb = (b); if (!(_va == _vb)) { \
    check::Registry::get().failures++; std::printf("  FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); } } while (0)

#endif
