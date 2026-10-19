#include "input_bindings.hpp"

namespace hades::input {
namespace {
// Final defaults after SetupControlScheme -> SetCommonDefaults -> SetKeyboardMoveDefaults.
// Instruction provenance per entry is in native/analysis/default-bindings-contract.json.
// ReportFeedback/TakeScreenshot are given R3/Select by SetupScheme but then marked
// UseGamepadButton=0, DefaultGamepadButtonUnmapped=1 (0x18018d600..0x18018d69c), so -1 here.
constexpr std::array<DefaultBinding, 29> kBindings{{
    {"Up", 48, {87, -1}, -1},              // MoveUp: D-pad Up, W
    {"Down", 49, {83, -1}, -1},            // MoveDown: D-pad Down, S
    {"Left", 46, {65, -1}, -1},            // MoveLeft: D-pad Left, A
    {"Right", 47, {68, -1}, -1},           // MoveRight: D-pad Right, D
    {"Attack1", 51, {-1, -1}, 1},          // Cast: B, right mouse
    {"Attack2", 52, {-1, -1}, 0},          // Attack: X, left mouse
    {"Attack3", 53, {81, -1}, -1},         // Special: Y, Q
    {"Rush", 50, {32, -1}, -1},            // Dash: A, Space
    {"AutoLock", 56, {129, -1}, -1},       // L2, Left Shift
    {"Select", 50, {32, -1}, 0},           // A, Space, left mouse
    {"Confirm", 52, {127, -1}, -1},        // X, Return
    {"ContinueText", 52, {32, -1}, -1},    // X, Space
    {"Cancel", 51, {125, 0}, 3},           // B, BackSpace, Escape, mouse XButton1
    {"MenuLeft", 54, {97, -1}, -1},        // L1, Left arrow
    {"MenuRight", 55, {98, -1}, -1},       // R1, Right arrow
    {"MenuUp", -1, {-1, -1}, 5},           // mouse wheel up
    {"MenuDown", -1, {-1, -1}, 6},         // mouse wheel down
    {"MenuInfo", 45, {66, -1}, -1},        // Select/Back, B
    {"Shout", 57, {70, -1}, -1},           // Call: R2, F
    {"Assist", 56, {49, -1}, -1},          // L2, 1
    {"AdvancedTooltip", 45, {66, -1}, -1}, // Select/Back, B
    {"Use", 55, {69, -1}, -1},             // Interact: R1, E
    {"Gift", 57, {71, -1}, -1},            // R2 (AllowGamepadDoubleMapping), G
    {"Reload", 59, {82, -1}, -1},          // R3, R
    {"Emote", 58, {84, -1}, -1},           // L3, T
    {"Pause", 44, {0, -1}, -1},            // ControlName "Start": Start, Escape
    {"Codex", 54, {67, -1}, -1},           // L1, C
    {"ReportFeedback", -1, {10, -1}, -1},  // F10
    {"TakeScreenshot", -1, {11, -1}, -1},  // F11
}};

template <typename Pred> ControlList collect(Pred pred) {
    ControlList out;
    for (const auto& b : kBindings)
        if (pred(b) && out.count < out.names.size()) out.names[out.count++] = b.control;
    return out;
}
std::optional<std::string_view> first(const ControlList& l) {
    if (l.count == 0) return std::nullopt;
    return l.names[0];
}
} // namespace

const std::array<DefaultBinding, 29>& defaultBindings() { return kBindings; }

std::optional<PadButton> padButtonFromAndroid(int32_t k) {
    using namespace android;
    switch (k) {
    case KEYCODE_BUTTON_A: return PadButton::A;
    case KEYCODE_BUTTON_B: return PadButton::B;
    case KEYCODE_BUTTON_X: return PadButton::X;
    case KEYCODE_BUTTON_Y: return PadButton::Y;
    case KEYCODE_BUTTON_L1: return PadButton::L1;
    case KEYCODE_BUTTON_R1: return PadButton::R1;
    case KEYCODE_BUTTON_L2: return PadButton::L2;
    case KEYCODE_BUTTON_R2: return PadButton::R2;
    case KEYCODE_BUTTON_THUMBL: return PadButton::L3;
    case KEYCODE_BUTTON_THUMBR: return PadButton::R3;
    case KEYCODE_BUTTON_START: return PadButton::Start;
    case KEYCODE_BUTTON_SELECT: return PadButton::Select;
    case KEYCODE_DPAD_UP: return PadButton::Up;
    case KEYCODE_DPAD_DOWN: return PadButton::Down;
    case KEYCODE_DPAD_LEFT: return PadButton::Left;
    case KEYCODE_DPAD_RIGHT: return PadButton::Right;
    default: return std::nullopt;
    }
}

ControlList padButtonControls(PadButton p) {
    const auto id = static_cast<int32_t>(p);
    return collect([id](const DefaultBinding& b) { return b.gamepadButton == id; });
}
ControlList gamepadButtonControls(int32_t keycode) {
    const auto p = padButtonFromAndroid(keycode);
    return p ? padButtonControls(*p) : ControlList{};
}
std::optional<std::string_view> gamepadButtonControl(int32_t keycode) { return first(gamepadButtonControls(keycode)); }

std::optional<int32_t> keyboardKeyFromAndroid(int32_t k) {
    using namespace android;
    if (k >= KEYCODE_A && k <= KEYCODE_Z) return 65 + (k - KEYCODE_A);  // KeyA..KeyZ
    if (k >= KEYCODE_0 && k <= KEYCODE_9) return 48 + (k - KEYCODE_0);  // Key0..Key9
    if (k >= KEYCODE_F1 && k <= KEYCODE_F12) return 1 + (k - KEYCODE_F1); // KeyF1..KeyF12
    switch (k) {
    case KEYCODE_SPACE: return 32;
    case KEYCODE_ENTER: return 127;      // KeyReturn
    case KEYCODE_DEL: return 125;        // KeyBackSpace
    case KEYCODE_ESCAPE: return 0;       // KeyEscape
    case KEYCODE_SHIFT_LEFT: return 129; // KeyShiftL
    case KEYCODE_DPAD_LEFT: return 97;   // KeyLeft (arrow keys arrive as DPAD_* from keyboards)
    case KEYCODE_DPAD_RIGHT: return 98;
    case KEYCODE_DPAD_UP: return 99;
    case KEYCODE_DPAD_DOWN: return 100;
    default: return std::nullopt;
    }
}
ControlList keyboardControls(int32_t keycode) {
    const auto key = keyboardKeyFromAndroid(keycode);
    if (!key) return {};
    return collect([k = *key](const DefaultBinding& b) { return b.keys[0] == k || b.keys[1] == k; });
}
std::optional<std::string_view> keyboardControl(int32_t keycode) { return first(keyboardControls(keycode)); }

ControlList mouseButtonControls(int32_t bit) {
    using namespace android;
    int32_t m = -1;
    switch (bit) {
    case BUTTON_PRIMARY: m = 0; break;
    case BUTTON_SECONDARY: m = 1; break;
    case BUTTON_TERTIARY: m = 2; break;
    case BUTTON_BACK: m = 3; break;   // XButton1 (adaptation assumption)
    case BUTTON_FORWARD: m = 4; break;
    default: return {};
    }
    return collect([m](const DefaultBinding& b) { return b.mouseButton == m; });
}
std::optional<std::string_view> mouseWheelControl(float v) {
    const int32_t m = v > 0.0f ? 5 : v < 0.0f ? 6 : -1;
    if (m < 0) return std::nullopt;
    return first(collect([m](const DefaultBinding& b) { return b.mouseButton == m; }));
}

Stick GamepadAxes::applyDeadZone(Stick s) const {
    if (s.x * s.x + s.y * s.y < deadZone * deadZone) return {};
    return s;
}
std::array<std::optional<PadButton>, 2> GamepadAxes::hatButtons(float hx, float hy) {
    std::array<std::optional<PadButton>, 2> out{};
    if (hx <= -kHatPressed) out[0] = PadButton::Left;
    else if (hx >= kHatPressed) out[0] = PadButton::Right;
    if (hy <= -kHatPressed) out[1] = PadButton::Up;
    else if (hy >= kHatPressed) out[1] = PadButton::Down;
    return out;
}

} // namespace hades::input
