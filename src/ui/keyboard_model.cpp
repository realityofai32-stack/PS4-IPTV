#include <cctype>

#include "keyboard_model.h"
#include "../core/utf8.h"
#include "../i18n/i18n.h"

namespace {
    Key ch(char c) {
        Key k;
        k.action = KeyAction::Char;
        k.lower = std::string(1, c);
        k.upper = std::string(1, (char) std::toupper((unsigned char) c));
        return k;
    }

    Key special(KeyAction a, const char *label, int span) {
        Key k;
        k.action = a;
        k.lower = label;
        k.upper = label;
        k.span = span;
        return k;
    }

    Key textKey(const char *t, int span) {
        Key k;
        k.action = KeyAction::Text;
        k.lower = t;
        k.upper = t;
        k.span = span;
        return k;
    }

    std::vector<Key> chars(const char *s) {
        std::vector<Key> keys;
        for (const char *p = s; *p; p++) {
            keys.push_back(ch(*p));
        }
        return keys;
    }
}

KeyboardModel::KeyboardModel(std::string initial, size_t maxLength, Layout kind)
        : value(std::move(initial)), maxLen(maxLength) {
    // every row is COLUMNS units wide
    if (kind == Layout::Search) {
        std::vector<Key> s0 = chars("1234567890");
        s0.push_back(special(KeyAction::Backspace, "keyboard.backspace", 2));
        std::vector<Key> s1 = chars("qwertyuiop'-");
        std::vector<Key> s2 = chars("asdfghjkl:&.");
        std::vector<Key> s3 = chars("zxcvbnm,()!?");
        std::vector<Key> s4;
        s4.push_back(special(KeyAction::Space, "keyboard.space", 6));
        s4.push_back(special(KeyAction::Clear, "keyboard.clear", 3));
        s4.push_back(special(KeyAction::Ok, "keyboard.results", 3));
        layout = {s0, s1, s2, s3, s4};
        return;
    }
    std::vector<Key> r0 = chars("1234567890");
    r0.push_back(special(KeyAction::Backspace, "keyboard.backspace", 2));
    std::vector<Key> r1 = chars("qwertyuiop-_");
    std::vector<Key> r2 = chars("asdfghjkl@.:");
    std::vector<Key> r3;
    r3.push_back(special(KeyAction::Shift, "keyboard.shift", 2));
    for (const auto &k: chars("zxcvbnm/?=")) {
        r3.push_back(k);
    }
    std::vector<Key> r4 = chars("&%#+!");
    r4.push_back(textKey("http://", 2));
    r4.push_back(textKey(".com", 2));
    r4.push_back(special(KeyAction::Space, "keyboard.space", 3));
    std::vector<Key> r5;
    r5.push_back(special(KeyAction::Clear, "keyboard.clear", 3));
    r5.push_back(special(KeyAction::Cancel, "keyboard.cancel", 3));
    r5.push_back(special(KeyAction::Ok, "keyboard.ok", 6));
    layout = {r0, r1, r2, r3, r4, r5};
}

int KeyboardModel::keyStart(int r, int c) const {
    int x = 0;
    for (int i = 0; i < c; i++) {
        x += layout[(size_t) r][(size_t) i].span;
    }
    return x;
}

bool KeyboardModel::move(int dx, int dy) {
    int rows = (int) layout.size();
    if (dx != 0) {
        int n = (int) layout[(size_t) row].size();
        col = (col + dx + n) % n;  // wrap within the row
        return true;
    }
    if (dy != 0) {
        // keep the horizontal position: pick the key under the current key's centre
        float center = (float) keyStart(row, col) + (float) layout[(size_t) row][(size_t) col].span / 2.0f;
        int newRow = (row + dy + rows) % rows;
        const auto &r = layout[(size_t) newRow];
        int x = 0;
        int pick = (int) r.size() - 1;
        for (int i = 0; i < (int) r.size(); i++) {
            if (center < (float) (x + r[(size_t) i].span)) {
                pick = i;
                break;
            }
            x += r[(size_t) i].span;
        }
        row = newRow;
        col = pick;
        return true;
    }
    return false;
}

std::string KeyboardModel::label(const Key &key) const {
    if (key.action != KeyAction::Char && key.action != KeyAction::Text) {
        return i18n::tr(key.lower.c_str());   // special keys hold a localization key ("keyboard.space")
    }
    return shifted ? key.upper : key.lower;
}

void KeyboardModel::insert(const std::string &s) {
    if (utf8::length(value) + utf8::length(s) <= maxLen) {
        value += s;
    }
}

void KeyboardModel::backspace() {
    utf8::popBack(value);
}

void KeyboardModel::space() {
    insert(" ");
}

void KeyboardModel::toggleShift() {
    shifted = !shifted;
}

void KeyboardModel::clear() {
    value.clear();
}

KeyboardModel::Result KeyboardModel::press() {
    const Key &k = focused();
    switch (k.action) {
        case KeyAction::Char:
            insert(label(k));
            break;
        case KeyAction::Text:
            insert(k.lower);
            break;
        case KeyAction::Shift:
            toggleShift();
            break;
        case KeyAction::Space:
            space();
            break;
        case KeyAction::Backspace:
            backspace();
            break;
        case KeyAction::Clear:
            clear();
            break;
        case KeyAction::Ok:
            return Result::Ok;
        case KeyAction::Cancel:
            return Result::Cancel;
    }
    return Result::None;
}
