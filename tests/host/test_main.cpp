#include "check.h"

int main() {
    auto &r = check::Registry::get();
    for (auto &t: r.tests) {
        int before = r.failures;
        t.second();
        std::printf("%s %s\n", r.failures == before ? "ok  " : "FAIL", t.first);
    }
    std::printf("%d checks, %d failures\n", r.checks, r.failures);
    return r.failures == 0 ? 0 : 1;
}
