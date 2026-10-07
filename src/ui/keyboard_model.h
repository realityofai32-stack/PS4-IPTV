// On-screen keyboard logic (layout, focus movement, editing). No rendering: host-testable.
// The PS4 native IME (SceImeDialog) is deliberately not used (it crashed in an earlier experiment).

#ifndef PS4IPTV_UI_KEYBOARD_MODEL_H
#define PS4IPTV_UI_KEYBOARD_MODEL_H

#include <string>
#include <vector>

enum class KeyAction {
    Char,       // inserts its label (lower/upper by shift state)
    Text,       // inserts a fixed string ("http://", ".com")
    Shift,
    Space,
    Backspace,
    Clear,
    Ok,
    Cancel
};

struct Key {
    KeyAction action = KeyAction::Char;
    std::string lower;
    std::string upper;
    int span = 1;          // width in grid units
};

class KeyboardModel {

public:

    static const int COLUMNS = 12;  // grid units per row

    enum class Layout {
        Full,     // text entry dialogs: URL keys, Cancel / OK
        Search    // the search screen: letters, digits, punctuation found in titles, Space / Clear / Results
    };

    explicit KeyboardModel(std::string initial = "", size_t maxLength = 256, Layout layout = Layout::Full);

    enum class Result {
        None,
        Ok,
        Cancel
    };

    // focus movement; returns false when the focus did not move
    bool move(int dx, int dy);

    Result press();

    // controller shortcuts
    void backspace();

    void space();

    void toggleShift();

    void clear();

    const std::string &text() const { return value; }

    bool shift() const { return shifted; }

    int focusRow() const { return row; }

    int focusCol() const { return col; }

    const std::vector<std::vector<Key>> &rows() const { return layout; }

    const Key &focused() const { return layout[(size_t) row][(size_t) col]; }

    std::string label(const Key &key) const;

    // grid unit where a key starts
    int keyStart(int r, int c) const;

private:

    std::vector<std::vector<Key>> layout;
    std::string value;
    size_t maxLen;
    int row = 1;
    int col = 0;
    bool shifted = false;

    void insert(const std::string &s);
};

#endif // PS4IPTV_UI_KEYBOARD_MODEL_H
