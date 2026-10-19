#pragma once
// Original Hades PC settings persistence, reconstructed from EngineWin64s.dll.
// Evidence and provenance: native/analysis/config-file-contract.json
// (raw disassembly under .local/reports/config-file/).
//
//  * Profile settings: "<ACTIVE_PROFILE>.sjson" (default profile "Profile1"),
//    written by SGD_Serialize<ConfigOptions> 0x18010aa50 from
//    ProfileManager::SaveProfile, read by SGD_Deserialize<ConfigOptions>
//    0x18010d2d0/0x18010adf0 from ProfileManager::Load after
//    ConfigOptions::Defaults(). Text: "{\n" + "  Name = value\n"... + "}\n",
//    only registered fields whose flag bit 0 is clear and whose value differs
//    from the registered default.
//  * Control bindings: "<ACTIVE_PROFILE>.ctrls", an SGB1 BinarySaveData stream
//    without checksum: Controls::SaveControls 0x18018f3b0 /
//    RemappableControl::Save 0x18019a860, read by the LoadControls worker
//    0x180190010 / RemappableControl::Load 0x18019ae00.
//
// Pure module: no Android, GL or Lua dependency.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hades::config {

// --- File names (ProfileManager / Controls string globals) ---
inline constexpr std::string_view kProfileBaseName = "Profile";  // PROFILE_BASE_NAME 0x18068eca0
inline constexpr std::string_view kDefaultProfile = "Profile1";  // getLastLoaded 0x1802165f0
inline constexpr std::string_view kConfigExtension = ".sjson";   // SaveProfile worker 0x1802153cd
inline constexpr std::string_view kControlsExtension = ".ctrls"; // Controls::ControlsExtension init 0x1800174f0
std::string profileConfigFileName(std::string_view profile = kDefaultProfile);   // "Profile1.sjson"
std::string profileControlsFileName(std::string_view profile = kDefaultProfile); // "Profile1.ctrls"

// --- sgg::Field type tags (jump tables 0x18010b320 / 0x180104354 / 0x180103fc8) ---
enum class FieldType : uint8_t { Bool = 0, Int = 1, Long = 2, Float = 3, String = 4, Other = 5 };
// sgg::Field flags (Field+4).
inline constexpr uint32_t kFieldNotSaved = 1;    // SGD_Serialize skips (testb $1 at 0x18010ab06)
inline constexpr uint32_t kFieldMachineWide = 2; // GetOptionsByType keeps across profile reload (0x180105a70)
inline constexpr uint32_t kFieldFlag4 = 4;       // display/window options; use not recovered

struct OptionSpec {
    const char* name;
    FieldType type;
    uint32_t flags;
    bool defaultBool;
    int32_t defaultInt;   // Int, Long, and the raw enum value for Other
    float defaultFloat;
    const char* defaultString;
    uint32_t initializerRva; // `dynamic initializer for 'sgg::ConfigOptions::<name>'`
    bool persisted() const { return (flags & kFieldNotSaved) == 0; }
};
// All 346 fields registered in the ConfigOptions field map (sorted by name).
const std::vector<OptionSpec>& optionSpecs();
// Case-insensitive lookup (map uses HashAsciiIgnoreCase/PredicateAsciiIgnoreCase).
const OptionSpec* findOption(std::string_view name);

struct Value {
    FieldType type = FieldType::Bool;
    bool b = false;
    int32_t i = 0;
    float f = 0.0f;
    std::string s; // String value, or the raw value text for Other
};

// One profile .sjson document: typed values for registered options plus
// unknown keys preserved verbatim. (The original engine ignores unknown keys
// on load and never writes them; preserving them is a port addition.)
class ConfigFile {
public:
    struct Entry {
        std::string key;      // spelling as first seen
        const OptionSpec* spec = nullptr; // null = unknown key
        Value value;          // registered options only
        std::string raw;      // unknown keys / Other: value source text
        bool present = false; // registered option: read from file or set explicitly
    };
    struct WriteOptions {
        // Original behaviour = false: flag-1 ("not saved") options are skipped.
        // true writes non-default flag-1 values too (Android adaptation); the
        // original reader still accepts them because SGD_Deserialize does not
        // test the flag.
        bool includeNotSavedOptions = false;
        bool preserveUnknownKeys = true;
    };

    ConfigFile();
    // ConfigOptions::Defaults (RestoreDefault on every field); unknown keys kept.
    void resetDefaults();
    // ProfileManager::Load order: resetDefaults() then apply the file. Returns
    // false (values stay at defaults) when the text is not a braced SJSON object.
    bool parse(std::string_view text, std::string* error = nullptr);
    std::string serialize(const WriteOptions& options) const;
    std::string serialize() const { return serialize(WriteOptions{}); }

    // Typed access. Setters fail on unknown names or type mismatch; Float
    // setters store float32, Int setters int32 (Long shares the int path).
    std::optional<bool> getBool(std::string_view name) const;
    std::optional<int32_t> getInt(std::string_view name) const;
    std::optional<float> getFloat(std::string_view name) const;
    std::optional<std::string> getString(std::string_view name) const;
    bool setBool(std::string_view name, bool v);
    bool setInt(std::string_view name, int32_t v);
    bool setFloat(std::string_view name, float v);
    bool setString(std::string_view name, std::string v);
    const Value* value(std::string_view name) const;
    // sgg::IsDefaultValue 0x180103e00 (float: |a-b| <= 2^-146 or <= 1e-4*max(|a|,|b|)).
    bool isDefault(std::string_view name) const;

    const std::vector<Entry>& entries() const { return entries_; }
    std::vector<std::pair<std::string, std::string>> unknownKeys() const;
    void setUnknownRaw(std::string key, std::string rawValueText);

private:
    std::vector<Entry> entries_;
    Entry* find(std::string_view name);
    const Entry* find(std::string_view name) const;
};

// ToString 0x180103fe0 / value conversions of SGD_Deserialize<ConfigOptions>.
std::string formatValue(const Value& v);

// --- Control bindings (.ctrls) ---
inline constexpr int32_t kControlRecordVersion = 1;   // RemappableControl::Save 0x18019a899
inline constexpr int32_t kPadButtonUnmapped = 76;     // PadButtonMax_, Load default 0x18019ae52
inline constexpr int32_t kMouseButtonNone = -1;
inline constexpr uint32_t kSgb1Magic = 0x31424753;    // 'SGB1', PrepareForWrite 0x1802043da
// KeyMappingScreen: ctor stores atoi("36401") in ControlsResetRevisionViewed
// (0x1801b1656); OnExit sets DefaultControlsChosen, SaveProfile, SaveControls.
inline constexpr int32_t kKeyMappingScreenRevision = 36401;

struct ControlRecord {
    std::string name;            // RemappableControl::ControlName (+0x30)
    std::vector<int32_t> keys;   // OrKeys, sgg::KeyboardButtonId (+0x78)
    int32_t gamepadButton = kPadButtonUnmapped; // sgg::GamepadButtonId (+0xa8)
    int32_t mouseButton = kMouseButtonNone;     // sgg::MouseButtons (+0xb0)
    bool useGamepadButton = false; // +0x12
    bool requiresShift = false;    // +0x11
};

// Per-field encodings used inside .ctrls.
int32_t keyToFile(int32_t keyboardButtonId);   // GetMappedKeyFromGaInputKey 0x18019bf90 (Windows VK)
int32_t keyFromFile(int32_t fileKey);          // GetCorrectlyMappedKey 0x18019bac0
uint32_t padToFile(int32_t gamepadButtonId);   // XInput-style mask, 0xffffffff unmapped (0x18019ab18)
int32_t padFromFile(uint32_t fileMask);        // kPadButtonUnmapped when unknown (0x18019b05f)
int32_t mouseToFile(int32_t mouseButton);      // 0x18019acb2
int32_t mouseFromFile(int32_t fileValue);      // 0x18019b1a5

// File = 'SGB1' + i32 count + count * record. Record = i32 version(1),
// u32 len + name bytes, i32 nkeys, nkeys * i32 key, i32 pad, i32 mouse,
// u8 useGamepadButton, u8 requiresShift. Little-endian.
std::vector<uint8_t> writeControlsFile(const std::vector<ControlRecord>& controls);
// Records whose version is not 1 are dropped (Load returns a disabled control
// after consuming only the version, as the original does).
bool readControlsFile(const uint8_t* data, std::size_t size, std::vector<ControlRecord>& out,
                      std::string* error = nullptr);
// LoadControls worker: exact-name match into the scheme; copies keys,
// gamepad, mouse, useGamepadButton and requiresShift. Unknown names ignored.
// Returns the number applied.
std::size_t applyLoadedControls(std::vector<ControlRecord>& scheme, const std::vector<ControlRecord>& loaded);
// Gate before loading .ctrls (0x18019028f..0x1801902a8): LoadSavedControls &&
// ControlsResetRevision <= ControlsResetRevisionViewed.
bool shouldLoadSavedControls(const ConfigFile& config);
// What KeyMappingScreen does when the player edits bindings.
void markControlsEdited(ConfigFile& config);
// Original defaults for the 29 scheme controls (input_bindings defaults;
// useGamepadButton per SetupScheme(name,pad)).
std::vector<ControlRecord> defaultControlRecords();

} // namespace hades::config
