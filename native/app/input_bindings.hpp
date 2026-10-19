#pragma once
// Original Hades default control bindings, recovered from EngineWin64s.dll
// (sgg::Controls::SetupControlScheme / SetCommonDefaults / SetKeyboardMoveDefaults).
// Evidence: native/analysis/default-bindings-contract.json.
// Pure module: no Android or GL headers. Android constants below mirror the NDK
// <android/keycodes.h> and <android/input.h> values so this also builds on the Mac host.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hades::input {

// --- Android NDK mirrors (values from android/keycodes.h, android/input.h) ---
namespace android {
inline constexpr int32_t KEYCODE_DPAD_UP = 19, KEYCODE_DPAD_DOWN = 20, KEYCODE_DPAD_LEFT = 21, KEYCODE_DPAD_RIGHT = 22;
inline constexpr int32_t KEYCODE_0 = 7, KEYCODE_9 = 16, KEYCODE_A = 29, KEYCODE_Z = 54;
inline constexpr int32_t KEYCODE_SHIFT_LEFT = 59, KEYCODE_SPACE = 62, KEYCODE_ENTER = 66, KEYCODE_DEL = 67;
inline constexpr int32_t KEYCODE_ESCAPE = 111, KEYCODE_F1 = 131, KEYCODE_F12 = 142;
inline constexpr int32_t KEYCODE_BUTTON_A = 96, KEYCODE_BUTTON_B = 97, KEYCODE_BUTTON_X = 99, KEYCODE_BUTTON_Y = 100;
inline constexpr int32_t KEYCODE_BUTTON_L1 = 102, KEYCODE_BUTTON_R1 = 103, KEYCODE_BUTTON_L2 = 104, KEYCODE_BUTTON_R2 = 105;
inline constexpr int32_t KEYCODE_BUTTON_THUMBL = 106, KEYCODE_BUTTON_THUMBR = 107;
inline constexpr int32_t KEYCODE_BUTTON_START = 108, KEYCODE_BUTTON_SELECT = 109;
inline constexpr int32_t AXIS_X = 0, AXIS_Y = 1, AXIS_Z = 11, AXIS_RZ = 14, AXIS_HAT_X = 15, AXIS_HAT_Y = 16;
inline constexpr int32_t AXIS_LTRIGGER = 17, AXIS_RTRIGGER = 18, AXIS_VSCROLL = 9;
inline constexpr int32_t BUTTON_PRIMARY = 1, BUTTON_SECONDARY = 2, BUTTON_TERTIARY = 4, BUTTON_BACK = 8, BUTTON_FORWARD = 16;
}

// --- Original engine enumerators (PDB sgg::GamepadButtonId / KeyboardButtonId / MouseButtons) ---
enum class PadButton : int32_t { Start = 44, Select = 45, Left = 46, Right = 47, Up = 48, Down = 49,
    A = 50, B = 51, X = 52, Y = 53, L1 = 54, R1 = 55, L2 = 56, R2 = 57, L3 = 58, R3 = 59 };
enum class MouseButton : int32_t { Left = 0, Right = 1, Middle = 2, XButton1 = 3, XButton2 = 4, WheelUp = 5, WheelDown = 6 };
// Keyboard keys use the integer sgg::KeyboardButtonId values (e.g. KeySpace=32, KeyA=65,
// KeyLeft=97, KeyBackSpace=125, KeyReturn=127, KeyShiftL=129, KeyEscape=0, KeyF10=10).

struct DefaultBinding {
    std::string_view control;      // sgg::Controls global / Lua control name
    int32_t gamepadButton;         // GamepadButtonId, -1 when unmapped by default
    std::array<int32_t, 2> keys;   // KeyboardButtonId OrKeys, -1 = none
    int32_t mouseButton;           // MouseButtons, -1 = none
};
// All 29 controls in original SetupControlScheme order.
const std::array<DefaultBinding, 29>& defaultBindings();

// Every control bound to a button/key (the original maps one button to several
// controls, e.g. A -> Rush and Select; context decides which one acts).
struct ControlList {
    std::array<std::string_view, 4> names{};
    std::size_t count = 0;
};

// Android gamepad keycode -> engine PadButton (Android Xbox-layout adaptation).
std::optional<PadButton> padButtonFromAndroid(int32_t keycode);
ControlList padButtonControls(PadButton);
ControlList gamepadButtonControls(int32_t keycode);
// First control in original setup order (Rush for A, Attack2 for X, Attack1 for B, MenuLeft for L1...).
// Prefer gamepadButtonControls for wiring; this single answer is a convenience only.
std::optional<std::string_view> gamepadButtonControl(int32_t keycode);

// Android keyboard keycode (keyboard source) -> KeyboardButtonId; DPAD_* are the arrow keys here.
std::optional<int32_t> keyboardKeyFromAndroid(int32_t keycode);
ControlList keyboardControls(int32_t keycode);
std::optional<std::string_view> keyboardControl(int32_t keycode);

// Android AMotionEvent button-state bit -> controls bound to that mouse button.
ControlList mouseButtonControls(int32_t androidButtonBit);
// AXIS_VSCROLL > 0 -> MenuUp (WheelUp), < 0 -> MenuDown (WheelDown).
std::optional<std::string_view> mouseWheelControl(float vscroll);

// Gamepad axes. Recovered: ConfigOptions::DeadZone = 0.225 (0x3e666666), radial,
// x*x+y*y < dz*dz -> zero, otherwise raw unscaled value. Engine stick Y is positive up
// (XInput), so Android AXIS_Y / AXIS_RZ (positive down) are negated.
struct Stick { float x = 0.0f, y = 0.0f; };
struct GamepadAxes {
    static constexpr float kDeadZone = 0.225f;
    static constexpr float kTriggerPressed = 1.0f / 255.0f; // original: trigger byte != 0
    static constexpr float kHatPressed = 0.5f;              // Android adaptation threshold
    float deadZone = kDeadZone;
    Stick applyDeadZone(Stick s) const;
    // Left stick (AXIS_X, AXIS_Y): movement, added to digital Up/Down/Left/Right direction.
    Stick leftStick(float axisX, float axisY) const { return applyDeadZone({axisX, -axisY}); }
    // Right stick (AXIS_Z, AXIS_RZ): only gamepad menu free-form cursor; not used for aiming.
    Stick rightStick(float axisZ, float axisRz) const { return applyDeadZone({axisZ, -axisRz}); }
    static bool triggerPressed(float value) { return value >= kTriggerPressed; } // LTRIGGER->L2, RTRIGGER->R2
    // HAT_X/HAT_Y -> D-pad buttons; returns up to two pressed buttons.
    static std::array<std::optional<PadButton>, 2> hatButtons(float hatX, float hatY);
};

} // namespace hades::input
