// Replays fuzz inputs (files or directories of files) through a libFuzzer target without libFuzzer: used to run the
// coverage-guided corpus under GCC's AddressSanitizer (tests/fuzz/run-fuzz.sh).
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(int argc, char **argv) {
    size_t n = 0;
    for (int i = 1; i < argc; i++) {
        std::vector<std::filesystem::path> files;
        if (std::filesystem::is_directory(argv[i])) {
            for (const auto &e: std::filesystem::directory_iterator(argv[i])) {
                files.push_back(e.path());
            }
        } else {
            files.emplace_back(argv[i]);
        }
        for (const auto &f: files) {
            std::ifstream in(f, std::ios::binary);
            std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            LLVMFuzzerTestOneInput((const uint8_t *) bytes.data(), bytes.size());
            n++;
        }
    }
    std::printf("%zu input(s) replayed\n", n);
    return 0;
}
