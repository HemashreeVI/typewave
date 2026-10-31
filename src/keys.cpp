#include "keys.hpp"

#include <cstring>

namespace tw {

KeyKind classify(unsigned code) {
    switch (code) {
        case vk::Space: return KeyKind::Space;
        case vk::Return: return KeyKind::Enter;
        case vk::Back:
        case vk::Delete: return KeyKind::Backspace;
        case vk::Shift: case vk::Control: case vk::Menu:
        case vk::LShift: case vk::RShift: case vk::LControl: case vk::RControl:
        case vk::LMenu: case vk::RMenu: case vk::LWin: case vk::RWin:
        case vk::Capital: case vk::Tab: case vk::Escape:
            return KeyKind::Modifier;
        default: return KeyKind::Normal;
    }
}

float panFor(unsigned code) {
    // Rough physical layout of a QWERTY keyboard: column -> stereo position.
    static const char* rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    static const float rowOffset[] = {0.0f, 0.3f, 0.55f, 0.85f};
    if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')) {
        for (int r = 0; r < 4; ++r) {
            const char* hit = std::strchr(rows[r], static_cast<int>(code));
            if (hit) {
                float col = static_cast<float>(hit - rows[r]) + rowOffset[r];  // 0 .. ~10
                return -0.8f + 1.6f * (col / 10.0f);
            }
        }
    }
    switch (code) {
        case vk::Escape: case vk::Tab: case vk::Capital: case vk::LShift: case vk::LControl:
        case vk::LWin: return -0.8f;
        case vk::LMenu: return -0.5f;
        case vk::Space: return 0.0f;
        case vk::RMenu: return 0.35f;
        case vk::RControl: case vk::RShift: return 0.65f;
        case vk::Return: case vk::Back: return 0.75f;
        case vk::Left: case vk::Up: case vk::Right: case vk::Down: case vk::Delete: return 0.85f;
        default: break;
    }
    if (code >= vk::Numpad0 && code <= vk::Divide) return 0.95f;
    if (code >= 0xBA && code <= 0xC0) return 0.7f;  // ; = , - . / `
    if (code >= 0xDB && code <= 0xDE) return 0.7f;  // [ \ ] '
    return 0.0f;
}

}  // namespace tw
