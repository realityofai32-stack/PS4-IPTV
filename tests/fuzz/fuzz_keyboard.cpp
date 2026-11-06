// libFuzzer target: the console's keyboard state buffer (reverse-engineered layout) and the key decoder, fed a
// sequence of arbitrary snapshots (any size, any key codes / modifiers / locks), plus typing into a text field.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/platform/input_actions.h"
#include "../../src/platform/keyboard_logic.h"
#include "../../src/ui/keyboard_model.h"

#define REQUIRE(c) do { if (!(c)) { __builtin_trap(); } } while (0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    kbd::Decoder d;
    d.setLayout(size % 2 ? kbd::Layout::TurkishQ : kbd::Layout::Us);
    KeyboardModel field("", 64);
    double t = 0;
    size_t pos = 0;
    while (pos < size) {
        size_t chunk = 1 + data[pos] % 128;   // snapshot sizes below, at and above the structure
        chunk = chunk > size - pos ? size - pos : chunk;
        kbd::State st;
        bool ok = kbd::parseState(data + pos, chunk, st);
        REQUIRE(ok == (chunk >= kbd::STATE_SIZE));
        REQUIRE(st.count >= 0 && st.count <= (int) kbd::STATE_MAX_KEYS);
        for (const auto &e: d.update(st.keys, st.count, st.mods, st.locks, t)) {
            if (e.key == kbd::Key::Char) {
                REQUIRE(!e.text.empty() && e.text.size() <= 4);
            }
            field.applyKey(e);
            // routing on a screen declaring an arbitrary set of actions: always a real button, or nothing
            std::vector<actions::Action> declared;
            for (size_t i = 0; i < 4 && pos + i < size; i++) {
                declared.push_back((actions::Action) (data[pos + i] % (int) actions::Action::Count));
            }
            InputEvent out;
            if (actions::route(e, declared, out)) {
                REQUIRE((int) out.button >= 0 && (int) out.button < (int) PadButton::Count);
                REQUIRE(out.repeat == e.repeat);
            }
        }
        // the same bytes as raw key codes / modifiers (never parsed as a structure)
        uint16_t keys[8];
        size_t n = chunk / 2 > 8 ? 8 : chunk / 2;
        std::memcpy(keys, data + pos, n * 2);
        uint32_t mods = chunk >= 1 ? data[pos] : 0;
        d.update(keys, (int) n, mods, mods >> 1, t + 0.01);
        t += (data[pos] % 40) * 0.016;
        pos += chunk;
    }
    REQUIRE(field.text().size() <= 64 * 4);
    return 0;
}
