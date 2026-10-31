// Key classification. Only the *kind* of key (and a stereo position) is ever
// derived from a keystroke: TypeWave never stores or sends which key you pressed.
#pragma once
#include <cstdint>

namespace tw {

enum class KeyKind : uint8_t { Normal = 0, Space, Enter, Backspace, Modifier, Count };

constexpr int kKindCount = static_cast<int>(KeyKind::Count);

inline const char* kindName(KeyKind k) {
    switch (k) {
        case KeyKind::Normal: return "key";
        case KeyKind::Space: return "space";
        case KeyKind::Enter: return "enter";
        case KeyKind::Backspace: return "backspace";
        case KeyKind::Modifier: return "modifier";
        default: return "key";
    }
}

// Windows virtual-key codes (spelled out so this header builds on any platform).
namespace vk {
constexpr unsigned Back = 0x08, Tab = 0x09, Return = 0x0D, Shift = 0x10, Control = 0x11,
                   Menu = 0x12, Capital = 0x14, Escape = 0x1B, Space = 0x20, Left = 0x25,
                   Up = 0x26, Right = 0x27, Down = 0x28, Delete = 0x2E, LWin = 0x5B, RWin = 0x5C,
                   Numpad0 = 0x60, Divide = 0x6F, LShift = 0xA0, RShift = 0xA1,
                   LControl = 0xA2, RControl = 0xA3, LMenu = 0xA4, RMenu = 0xA5;
}

KeyKind classify(unsigned vkCode);

// -1 = far left of the keyboard, +1 = far right.
float panFor(unsigned vkCode);

}  // namespace tw
