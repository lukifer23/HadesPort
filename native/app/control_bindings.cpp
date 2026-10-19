#include "control_bindings.hpp"
#include <algorithm>
#include <cstdio>
#include <sstream>

namespace hades::input {
namespace {
#include "engine_input_enums.inc"
bool remappable(std::string_view control) {
    for (const auto& row : remapRows()) if (control == row.control) return true;
    return false;
}
}

const std::vector<RemapRow>& remapRows() {
    static const std::vector<RemapRow> rows{
        {"Attack2", "ATTACK", true, true}, {"Attack3", "SPECIAL", true, true}, {"Attack1", "CAST", true, true},
        {"Rush", "DASH", true, true}, {"Use", "INTERACT", true, true}, {"Shout", "CALL", true, true},
        {"Assist", "SUMMON", true, true}, {"Reload", "RELOAD", true, true}, {"AutoLock", "LOCK-ON", true, true},
        {"Codex", "CODEX", true, true}, {"Gift", "GIFT", true, true}, {"Pause", "PAUSE", true, true},
        {"Up", "MOVE UP", false, true}, {"Down", "MOVE DOWN", false, true},
        {"Left", "MOVE LEFT", false, true}, {"Right", "MOVE RIGHT", false, true}};
    return rows;
}

void ControlBindings::resetDefaults() {
    bindings_.clear();
    for (const auto& d : defaultBindings())
        bindings_.push_back({std::string(d.control), d.gamepadButton, d.keys, d.mouseButton});
}
ControlBinding* ControlBindings::find(std::string_view control) {
    for (auto& b : bindings_) if (b.control == control) return &b;
    return nullptr;
}
const ControlBinding* ControlBindings::find(std::string_view control) const {
    for (const auto& b : bindings_) if (b.control == control) return &b;
    return nullptr;
}
ControlList ControlBindings::padControls(PadButton button) const {
    ControlList out;
    for (const auto& b : bindings_)
        if (b.gamepadButton == int32_t(button) && out.count < out.names.size()) out.names[out.count++] = b.control;
    return out;
}
ControlList ControlBindings::keyControls(int32_t key) const {
    ControlList out;
    for (const auto& b : bindings_)
        if ((b.keys[0] == key || b.keys[1] == key) && out.count < out.names.size()) out.names[out.count++] = b.control;
    return out;
}
void ControlBindings::assignPad(std::string_view control, int32_t button) {
    auto* target = find(control);
    if (!target) return;
    for (auto& b : bindings_)
        if (&b != target && b.gamepadButton == button && remappable(b.control)) b.gamepadButton = target->gamepadButton;
    target->gamepadButton = button;
}
void ControlBindings::assignKey(std::string_view control, int32_t key) {
    auto* target = find(control);
    if (!target) return;
    for (auto& b : bindings_)
        if (&b != target && remappable(b.control))
            for (auto& k : b.keys) if (k == key) k = target->keys[0];
    target->keys = {key, target->keys[1] == key ? -1 : target->keys[1]};
}
std::string ControlBindings::serialize() const {
    std::ostringstream out;
    for (const auto& b : bindings_)
        out << b.control << ' ' << b.gamepadButton << ' ' << b.keys[0] << ' ' << b.keys[1] << ' ' << b.mouseButton << ';';
    return out.str();
}
bool ControlBindings::deserialize(const std::string& text) {
    ControlBindings parsed;
    std::istringstream in(text);
    std::string record;
    while (std::getline(in, record, ';')) {
        if (record.empty()) continue;
        std::istringstream fields(record);
        ControlBinding b;
        if (!(fields >> b.control >> b.gamepadButton >> b.keys[0] >> b.keys[1] >> b.mouseButton)) return false;
        auto* slot = parsed.find(b.control);
        if (!slot) continue; // unknown control: keep defaults (forward compatible)
        *slot = b;
    }
    *this = std::move(parsed);
    return true;
}

namespace {
std::string_view fileName(std::string_view control) {
    if (control == "Up") return "MoveUp";
    if (control == "Down") return "MoveDown";
    if (control == "Left") return "MoveLeft";
    if (control == "Right") return "MoveRight";
    if (control == "Pause") return "Start";
    return control;
}
}
std::vector<config::ControlRecord> toControlRecords(const ControlBindings& bindings) {
    auto records = config::defaultControlRecords();
    for (auto& r : records)
        for (const auto& b : bindings.all()) {
            if (fileName(b.control) != r.name) continue;
            r.keys.clear();
            for (auto k : b.keys) if (k >= 0) r.keys.push_back(k);
            r.gamepadButton = b.gamepadButton >= 0 ? b.gamepadButton : config::kPadButtonUnmapped;
            r.mouseButton = b.mouseButton >= 0 ? b.mouseButton : config::kMouseButtonNone;
        }
    return records;
}
void applyControlRecords(ControlBindings& bindings, const std::vector<config::ControlRecord>& records) {
    auto scheme = toControlRecords(bindings);
    config::applyLoadedControls(scheme, records);
    for (const auto& r : scheme) {
        for (const auto& b : bindings.all()) {
            if (fileName(b.control) != r.name) continue;
            auto* live = bindings.find(b.control);
            live->keys = {r.keys.size() > 0 ? r.keys[0] : -1, r.keys.size() > 1 ? r.keys[1] : -1};
            live->gamepadButton = r.gamepadButton == config::kPadButtonUnmapped ? -1 : r.gamepadButton;
            live->mouseButton = r.mouseButton == config::kMouseButtonNone ? -1 : r.mouseButton;
            break;
        }
    }
}

std::string padButtonName(int32_t id) {
    switch (id) {
    case 44: return "START"; case 45: return "SELECT";
    case 46: return "D-LEFT"; case 47: return "D-RIGHT"; case 48: return "D-UP"; case 49: return "D-DOWN";
    case 50: return "A"; case 51: return "B"; case 52: return "X"; case 53: return "Y";
    case 54: return "LB"; case 55: return "RB"; case 56: return "LT"; case 57: return "RT";
    case 58: return "L-STICK"; case 59: return "R-STICK";
    default: return "-";
    }
}
std::string keyName(int32_t id) {
    if (id < 0) return "-";
    for (const auto& k : kEngineKeys) {
        if (k.id != id) continue;
        std::string name = k.name + 3; // strip "Key"
        if (name == "Return") return "ENTER";
        if (name == "BackSpace") return "BACKSPACE";
        if (name == "Escape") return "ESC";
        if (name == "ShiftL") return "L-SHIFT";
        if (name == "ShiftR") return "R-SHIFT";
        if (name == "CtrlL") return "L-CTRL";
        if (name == "CtrlR") return "R-CTRL";
        if (name == "AltL") return "L-ALT";
        if (name == "AltR") return "R-ALT";
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        return name;
    }
    return "-";
}
std::optional<int32_t> engineKeyFromAndroid(int32_t k) {
    if (auto basic = keyboardKeyFromAndroid(k)) return basic;
    // Android NDK keycodes (android/keycodes.h) -> sgg::KeyboardButtonId.
    switch (k) {
    case 61: return 126;  // TAB -> KeyTab
    case 113: return 130; // CTRL_LEFT -> KeyCtrlL
    case 114: return 136; // CTRL_RIGHT -> KeyCtrlR
    case 57: return 132;  // ALT_LEFT -> KeyAltL
    case 58: return 133;  // ALT_RIGHT -> KeyAltR
    case 60: return 137;  // SHIFT_RIGHT -> KeyShiftR
    case 55: return 44;   // COMMA
    case 56: return 46;   // PERIOD
    case 69: return 45;   // MINUS
    case 70: return 61;   // EQUALS -> KeyEqual
    case 71: return 91;   // LEFT_BRACKET
    case 72: return 93;   // RIGHT_BRACKET
    case 73: return 92;   // BACKSLASH
    case 74: return 59;   // SEMICOLON
    case 75: return 39;   // APOSTROPHE
    case 76: return 47;   // SLASH
    case 68: return 96;   // GRAVE
    case 92: return 105;  // PAGE_UP
    case 93: return 106;  // PAGE_DOWN
    case 122: return 102; // MOVE_HOME
    case 123: return 104; // MOVE_END
    case 124: return 101; // INSERT
    case 112: return 103; // FORWARD_DEL -> KeyDelete
    case 115: return 128; // CAPS_LOCK
    case 160: return 113; // NUMPAD_ENTER -> KeyKpEnter
    case 154: return 109; // NUMPAD_DIVIDE
    case 155: return 110; // NUMPAD_MULTIPLY
    case 156: return 111; // NUMPAD_SUBTRACT
    case 157: return 112; // NUMPAD_ADD
    default:
        // NUMPAD_0..9 (144..153): gainput reports keypad digits as the
        // navigation keypad ids (KpInsert, KpEnd, KpDown, ... KpPageUp).
        if (k >= 144 && k <= 153) {
            static constexpr int32_t keypad[10]{114, 115, 116, 117, 118, 119, 120, 121, 122, 123};
            return keypad[k - 144];
        }
        return std::nullopt;
    }
}

} // namespace hades::input
