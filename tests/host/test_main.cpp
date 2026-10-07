#include "check.h"

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a crash still shows the test it happened in
    auto &r = check::Registry::get();
    for (auto &t: r.tests) {
        int before = r.failures;
        t.second();
        std::printf("%s %s\n", r.failures == before ? "ok  " : "FAIL", t.first);
    }
    std::printf("%d checks, %d failures\n", r.checks, r.failures);
    return r.failures == 0 ? 0 : 1;
}
