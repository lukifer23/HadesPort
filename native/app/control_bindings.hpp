#pragma once
// Live, user-editable control bindings (PC KeyMappingScreen/RemappableControl
// semantics) seeded from the recovered original defaults in input_bindings.
// Engine enum ids: sgg::GamepadButtonId, sgg::KeyboardButtonId, sgg::MouseButtons.
#include "input_bindings.hpp"
#include "config_file.hpp"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hades::input {

struct ControlBinding {
    std::string control;           // original control name
    int32_t gamepadButton = -1;    // GamepadButtonId, -1 unmapped
    std::array<int32_t, 2> keys{-1, -1}; // KeyboardButtonId, -1 unmapped
    int32_t mouseButton = -1;      // MouseButtons, -1 unmapped
};

// Rows of the native Controls screen (Android adaptation of the PC menu):
// player-facing controls in PC Controls order; menu controls keep defaults.
struct RemapRow { const char* control; const char* label; bool gamepad, keyboard; };
const std::vector<RemapRow>& remapRows();

class ControlBindings {
    std::vector<ControlBinding> bindings_;
public:
    ControlBindings() { resetDefaults(); }
    void resetDefaults();
    const std::vector<ControlBinding>& all() const { return bindings_; }
    ControlBinding* find(std::string_view control);
    const ControlBinding* find(std::string_view control) const;
    ControlList padControls(PadButton button) const;
    ControlList keyControls(int32_t engineKey) const;
    // Assign with swap among remappable rows so no two player controls share
    // an input (the previous owner receives the target's old binding).
    void assignPad(std::string_view control, int32_t button);
    void assignKey(std::string_view control, int32_t engineKey);
    std::string serialize() const;           // "control pad key0 key1 mouse;..."
    bool deserialize(const std::string& text);
};

// Original Profile1.ctrls records (RemappableControl::Save names: MoveUp..,
// Start) <-> live bindings. Records start from the original defaults so the
// UseGamepadButton/RequiresShift flags stay the engine's.
std::vector<config::ControlRecord> toControlRecords(const ControlBindings&);
void applyControlRecords(ControlBindings&, const std::vector<config::ControlRecord>&);

// Display names (PC-style glyph labels) and extended Android key mapping.
std::string padButtonName(int32_t gamepadButton);
std::string keyName(int32_t engineKey);
std::optional<int32_t> engineKeyFromAndroid(int32_t androidKeycode);

} // namespace hades::input
