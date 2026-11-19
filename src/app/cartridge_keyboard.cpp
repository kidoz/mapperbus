#include <array>
#include <span>

#include "app/emulation_session.hpp"

namespace mapperbus::app {
namespace {
// Physical keys use USB HID page 7 in both desktop backends. Matrix indices
// are the row and data bit seen by the emulated keyboard controller.
enum Key : core::Byte {
    Unknown = 0,
    A = 4,
    B = 5,
    C = 6,
    D = 7,
    E = 8,
    F = 9,
    G = 10,
    H = 11,
    I = 12,
    J = 13,
    K = 14,
    L = 15,
    M = 16,
    N = 17,
    O = 18,
    P = 19,
    Q = 20,
    R = 21,
    S = 22,
    T = 23,
    U = 24,
    V = 25,
    W = 26,
    X = 27,
    Y = 28,
    Z = 29,
    Num1 = 30,
    Num2 = 31,
    Num3 = 32,
    Num4 = 33,
    Num5 = 34,
    Num6 = 35,
    Num7 = 36,
    Num8 = 37,
    Num9 = 38,
    Num0 = 39,
    Return = 40,
    Escape = 41,
    Backspace = 42,
    Tab = 43,
    Space = 44,
    Minus = 45,
    Equals = 46,
    LeftBracket = 47,
    RightBracket = 48,
    Backslash = 49,
    Semicolon = 51,
    Apostrophe = 52,
    Grave = 53,
    Comma = 54,
    Period = 55,
    Slash = 56,
    CapsLock = 57,
    F1 = 58,
    F2 = 59,
    F3 = 60,
    F4 = 61,
    F5 = 62,
    F6 = 63,
    F7 = 64,
    F8 = 65,
    F9 = 66,
    F10 = 67,
    F11 = 68,
    F12 = 69,
    PrintScreen = 70,
    ScrollLock = 71,
    Pause = 72,
    Insert = 73,
    Home = 74,
    PageUp = 75,
    Delete = 76,
    End = 77,
    PageDown = 78,
    Right = 79,
    Left = 80,
    Down = 81,
    Up = 82,
    NumLock = 83,
    NumpadDivide = 84,
    NumpadMultiply = 85,
    NumpadMinus = 86,
    NumpadPlus = 87,
    NumpadEnter = 88,
    Numpad1 = 89,
    Numpad2 = 90,
    Numpad3 = 91,
    Numpad4 = 92,
    Numpad5 = 93,
    Numpad6 = 94,
    Numpad7 = 95,
    Numpad8 = 96,
    Numpad9 = 97,
    Numpad0 = 98,
    NumpadPeriod = 99,
    LeftCtrl = 224,
    LeftShift = 225,
    LeftAlt = 226,
    LeftSuper = 227,
    RightCtrl = 228,
    RightShift = 229,
    RightAlt = 230,
    RightSuper = 231,
};
using Row = std::array<Key, 8>;
constexpr Row kSubor[] = {
    {Num4, G, F, C, F2, E, Num5, V},
    {Num2, D, S, End, F1, W, Num3, X},
    {Insert, Backspace, PageDown, Right, F8, PageUp, Delete, Home},
    {Num9, I, L, Comma, F5, O, Num0, Period},
    {RightBracket, Return, Up, Left, F7, LeftBracket, Backslash, Down},
    {Q, CapsLock, Z, Tab, Escape, A, Num1, LeftCtrl},
    {Num7, Y, K, M, F4, U, Num8, J},
    {Minus, Semicolon, Apostrophe, Slash, F6, P, Equals, LeftShift},
    {T, H, N, Space, F3, R, Num6, B},
    {Grave, F10, F11, F12, NumpadPlus, NumpadMinus, NumpadMultiply, NumpadDivide},
    {ScrollLock, Pause, Grave, Tab, Numpad6, Numpad7, Numpad8, Numpad9},
    {CapsLock, Slash, RightShift, LeftAlt, F9, NumpadPlus, NumpadMinus, NumpadMultiply},
    {RightAlt, Unknown, RightCtrl, Numpad1, NumpadDivide, NumpadPeriod, NumLock, NumpadEnter},
};
constexpr Row kPec[] = {
    {Escape, Space, LeftAlt, CapsLock, LeftCtrl, Grave, Tab, LeftShift},
    {F6, F7, F5, F4, F8, F2, F1, F3},
    {Equals, Numpad0, Period, A, Return, Num1, Q, Z},
    {Unknown, Numpad3, Numpad6, S, Numpad9, Num2, W, X},
    {Slash, Numpad2, Numpad5, D, Numpad8, Num3, E, C},
    {Pause, Numpad1, Numpad4, F, Numpad7, Num4, R, V},
    {Backspace, Backslash, NumpadEnter, G, RightBracket, Num5, T, B},
    {Num9, Period, L, K, O, Num8, I, Comma},
    {Num0, Slash, Semicolon, J, P, Num7, U, M},
    {Minus, Minus, Apostrophe, H, LeftBracket, Num6, Y, N},
    {F11, F12, F10, Unknown, Minus, F9, Unknown, Unknown},
    {Up, Right, Down, PageDown, Left, NumpadMultiply, NumpadMinus, NumpadPlus},
    {Insert, Numpad1, PageUp, Home, Delete, End, NumpadDivide, NumLock},
};
constexpr Row kGolden[] = {
    {Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown},
    {Num2, W, S, X, Num3, E, D, C},
    {Num4, R, F, V, Num5, T, G, B},
    {Num6, Y, H, N, Num7, U, J, M},
    {Num8, I, K, Comma, Num9, O, L, Period},
    {Num0, P, Semicolon, Slash, Minus, LeftBracket, Apostrophe, Backslash},
    {Equals, RightBracket, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown},
    {Unknown, Unknown, Unknown, Unknown, Escape, Unknown, Unknown, Unknown},
    {Unknown, Unknown, Space, Unknown, Unknown, Unknown, Unknown, Unknown},
    {F1, F2, F3, F4, F5, F6, F7, F8},
    {Unknown, Unknown, Unknown, Unknown, Unknown, LeftShift, Unknown, Unknown},
};
// PC/AT set 1 make codes indexed by scan code. A release sets the high bit.
constexpr Key kSet1[] = {
    Unknown,    Escape,     Num1,     Num2,    Num3,        Num4,        Num5,
    Num6,       Num7,       Num8,     Num9,    Num0,        Minus,       Equals,
    Backspace,  Tab,        Q,        W,       E,           R,           T,
    Y,          U,          I,        O,       P,           LeftBracket, RightBracket,
    Return,     LeftCtrl,   A,        S,       D,           F,           G,
    H,          J,          K,        L,       Semicolon,   Apostrophe,  Grave,
    LeftShift,  Backslash,  Z,        X,       C,           V,           B,
    N,          M,          Comma,    Period,  Slash,       RightShift,  NumpadMultiply,
    LeftAlt,    Space,      CapsLock, F1,      F2,          F3,          F4,
    F5,         F6,         F7,       F8,      F9,          F10,         NumLock,
    ScrollLock, Numpad7,    Numpad8,  Numpad9, NumpadMinus, Numpad4,     Numpad5,
    Numpad6,    NumpadPlus, Numpad1,  Numpad2, Numpad3,     Numpad0,     NumpadPeriod,
    Unknown,    Unknown,    Unknown,  F11,     F12};
constexpr auto kScanForKey = [] {
    std::array<core::Word, 256> result{};
    for (unsigned code = 1; code < std::size(kSet1); ++code) {
        if (kSet1[code] != Unknown)
            result[kSet1[code]] = code;
    }
    // Enhanced PC/AT keys send an E0 prefix before both make and break.
    result[NumpadEnter] = 0xE01C;
    result[RightCtrl] = 0xE01D;
    result[NumpadDivide] = 0xE035;
    result[RightAlt] = 0xE038;
    result[Home] = 0xE047;
    result[Up] = 0xE048;
    result[PageUp] = 0xE049;
    result[Left] = 0xE04B;
    result[Right] = 0xE04D;
    result[End] = 0xE04F;
    result[Down] = 0xE050;
    result[PageDown] = 0xE051;
    result[Insert] = 0xE052;
    result[Delete] = 0xE053;
    return result;
}();
} // namespace

void EmulationSession::sync_transformer_keyboard() {
    // A frame can contain a complete chord. Preserve every sampled transition
    // until the guest acknowledges its one-byte receiver. Bound the host buffer
    // for software that never services its keyboard IRQ.
    constexpr std::size_t kBufferSize = 64;
    constexpr std::size_t kLongestSequence = 6;
    const auto sample = [&](unsigned key, bool down) {
        if (down == sampled_keys_[key] ||
            pending_scancodes_.size() > kBufferSize - kLongestSequence)
            return;
        sampled_keys_[key] = down;
        if (key == Pause) {
            if (down)
                pending_scancodes_.insert(pending_scancodes_.end(),
                                          {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5});
        } else if (key == PrintScreen) {
            const std::array<core::Byte, 4> bytes =
                down ? std::array<core::Byte, 4>{0xE0, 0x2A, 0xE0, 0x37}
                     : std::array<core::Byte, 4>{0xE0, 0xB7, 0xE0, 0xAA};
            pending_scancodes_.insert(pending_scancodes_.end(), bytes.begin(), bytes.end());
        } else if (const auto code = kScanForKey[key]) {
            if (code >> 8)
                pending_scancodes_.push_back(code >> 8);
            pending_scancodes_.push_back(static_cast<core::Byte>(code | (down ? 0 : 0x80)));
        }
    };
    for (unsigned key = LeftCtrl; key <= RightAlt; ++key) {
        if (input_->keyboard_key_down(key))
            sample(key, true);
    }
    for (unsigned key = A; key <= NumpadPeriod; ++key)
        sample(key, input_->keyboard_key_down(key));
    for (unsigned key = LeftCtrl; key <= RightAlt; ++key) {
        if (!input_->keyboard_key_down(key))
            sample(key, false);
    }
    if (!pending_scancodes_.empty() &&
        emulator_->try_send_keyboard_scancode(pending_scancodes_.front()))
        pending_scancodes_.pop_front();
}

void EmulationSession::configure_cartridge_keyboard() {
    sampled_keys_.fill(false);
    pending_scancodes_.clear();
    switch (emulator_->mapper_number().value_or(0)) {
    case 365:
    case 531:
        cartridge_keyboard_ = CartridgeKeyboard::Subor;
        break;
    case 371:
    case 440:
        cartridge_keyboard_ = CartridgeKeyboard::Pec;
        break;
    case 442:
        cartridge_keyboard_ = CartridgeKeyboard::Golden;
        break;
    case 329:
        cartridge_keyboard_ = CartridgeKeyboard::Transformer;
        break;
    default:
        cartridge_keyboard_ = CartridgeKeyboard::None;
        break;
    }
    input_->set_cartridge_keyboard_active(cartridge_keyboard_ != CartridgeKeyboard::None);
}

void EmulationSession::sync_cartridge_keyboard() {
    std::span<const Row> matrix;
    switch (cartridge_keyboard_) {
    case CartridgeKeyboard::Subor:
        matrix = kSubor;
        break;
    case CartridgeKeyboard::Pec:
        matrix = kPec;
        break;
    case CartridgeKeyboard::Golden:
        matrix = kGolden;
        break;
    case CartridgeKeyboard::Transformer:
        sync_transformer_keyboard();
        return;
    case CartridgeKeyboard::None:
        return;
    }
    for (unsigned row = 0; row < matrix.size(); ++row) {
        core::Byte mask = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto key = matrix[row][bit];
            if (key != Unknown && input_->keyboard_key_down(key))
                mask |= 1U << bit;
        }
        if (cartridge_keyboard_ == CartridgeKeyboard::Pec ||
            cartridge_keyboard_ == CartridgeKeyboard::Golden) {
            if ((row == 7 && input_->keyboard_key_down(RightShift)) ||
                (row == 5 && input_->keyboard_key_down(RightCtrl)))
                mask |= 0x80;
        }
        emulator_->set_keyboard_row(row, mask);
    }
}
} // namespace mapperbus::app
