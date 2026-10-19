#include "config_file.hpp"
#include "input_bindings.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace hades::config {
namespace {

// Registered sgg::Field entries, extracted from the 346 field-registering
// `dynamic initializer for 'sgg::ConfigOptions::*'` procedures (string_hash_map
// insert 0x18010d860 / registerField<string> 0x18010d1a0): name, type tag,
// flags, eastl::any default. Generated; see config-file-contract.json.
const std::vector<OptionSpec>& optionTable() {
    static const std::vector<OptionSpec> kOptions = {
    {"AllowAutoLock", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xcd40},
    {"AllowControlHotSwap", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x9fe0},
    {"AllowEditFlippedMaps", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x114b0},
    {"AllowHardQuit", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xe7e0},
    {"AllowOversizeWindows", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xfbb0},
    {"AllowPause", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xe4a0},
    {"AllowPlaySpeech", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xb930},
    {"AllowSaveLoad", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xdaf0},
    {"AllowStrafe", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xce40},
    {"AllowStripRequirements", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10930},
    {"AllowUnitInput", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xbab0},
    {"AllowVoiceBankStreaming", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xd700},
    {"AmbienceDistanceMax", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x44c80000}), "", 0xf3d0},
    {"ApplyDeadZone", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x8a30},
    {"AskAboutCloudSaves", FieldType::Bool, 2u, true, 0, 0.0f, "", 0x132e0},
    {"AssetErrorAsserts", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x10400},
    {"AssetSwatchPreviewSize", FieldType::Int, 1u, false, 64, 0.0f, "", 0xbe50},
    {"AttachDxDiagToBugReports", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x12a70},
    {"AttachmentMode", FieldType::Bool, 3u, false, 0, 0.0f, "", 0xbcb0},
    {"AttemptHighDPI", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x9040},
    {"AudioMaxAudibleVoices", FieldType::Int, 1u, false, 64, 0.0f, "", 0xd570},
    {"AudioMaxVirtualVoices", FieldType::Int, 1u, false, 128, 0.0f, "", 0xd4e0},
    {"AudioStreamBufferSize", FieldType::Int, 1u, false, 65536, 0.0f, "", 0xd450},
    {"AuthBasecampOnStartup", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x123a0},
    {"AuthTwitchOnStartup", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x11630},
    {"AutoDeleteExtraDevSaves", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xded0},
    {"AutoDetectLowResAssets", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x10c10},
    {"AutoDownloadSavePathStr", FieldType::String, 1u, false, 0, 0.0f, "\\\\AQUARIUS\\share\\Minos\\LatestSaves", 0xde50},
    {"AutoHotLoad", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xf770},
    {"AutoPlaySaveQueueDeleteOnLoad", FieldType::Bool, 3u, false, 0, 0.0f, "", 0x9d60},
    {"AutoPlaySaveQueueFolderStr", FieldType::String, 3u, false, 0, 0.0f, "\\\\aquarius\\share\\minos\\LatestSaves", 0x9de0},
    {"AutoPlaySaveQueueLoadInterval", FieldType::Float, 3u, false, 0, std::bit_cast<float>(uint32_t{0x43b40000}), "", 0x9cd0},
    {"AutoReportAsserts", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x128f0},
    {"AutoSubmitBugs", FieldType::Bool, 3u, false, 0, 0.0f, "", 0x129f0},
    {"BackgroundAudioThread", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xcbc0},
    {"BasecampMaxLogSize", FieldType::Int, 1u, false, 4194304, 0.0f, "", 0x12500},
    {"BasecampProject", FieldType::String, 1u, false, 0, 0.0f, "Minos  Bugs & Polish", 0x12320},
    {"BinkStreamCacheSize", FieldType::Int, 1u, false, 587202560, 0.0f, "", 0x11b60},
    {"BinkWeaponStreamCacheSize", FieldType::Int, 1u, false, 157286400, 0.0f, "", 0x11bf0},
    {"BlinkKeyboardAim", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xcdc0},
    {"Brightness", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x3f000000}), "", 0xd7e0},
    {"BugReportSendTo", FieldType::String, 1u, false, 0, 0.0f, "", 0x12790},
    {"BugReportVideoPath", FieldType::String, 1u, false, 0, 0.0f, "C:\\Fraps\\Movies\\", 0x12610},
    {"CameraShake", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xd040},
    {"CatchExceptions", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x11a50},
    {"ClearDebugLogOnDevLoad", FieldType::Bool, 3u, false, 0, 0.0f, "", 0x9e60},
    {"ColorGradingScreenshot", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xf6f0},
    {"ConfigInclude", FieldType::String, 1u, false, 0, 0.0f, "", 0x133e0},
    {"ControllerPropelSpeed", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x43960000}), "", 0xeec0},
    {"ControllerReturnPropelSpeed", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x43fa0000}), "", 0xef50},
    {"ControlsResetRevision", FieldType::Int, 1u, false, 16755, 0.0f, "", 0xb390},
    {"ControlsResetRevisionViewed", FieldType::Int, 0u, false, 0, 0.0f, "", 0xb420},
    {"CopyDragAndDroppedSaves", FieldType::Bool, 3u, true, 0, 0.0f, "", 0x127f0},
    {"CycleSelectWrap", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xa360},
    {"DeadZone", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x3e666666}), "", 0xb8b0},
    {"DebugBinkMask", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x119d0},
    {"DebugCameraZoom", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x9140},
    {"DebugDisplay", FieldType::Other, 0u, false, 1, 0.0f, "", 0x91c0},
    {"DebugDof", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x11950},
    {"DebugDrawAllCursors", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x112b0},
    {"DebugDrawBounds", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xff00},
    {"DebugDrawGridManager", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xfe00},
    {"DebugDrawLiveEditorQuadTree", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xff80},
    {"DebugDrawMouseCoordinates", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10000},
    {"DebugDrawPathfinding", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x99d0},
    {"DebugDrawScissorRects", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10080},
    {"DebugGridManager", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xfe80},
    {"DebugKeyScreenFilter", FieldType::String, 0u, false, 0, 0.0f, "", 0xe300},
    {"DebugLog", FieldType::String, 1u, false, 0, 0.0f, "DebugLog.txt", 0x9450},
    {"DebugLogSounds", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x92d0},
    {"DebugMessages", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x90c0},
    {"DebugOverlayFilterEdit", FieldType::Bool, 3u, false, 0, 0.0f, "", 0xbbb0},
    {"DebugPrintAnimations", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10180},
    {"DebugPrintBinkWarnings", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x10380},
    {"DebugPrintCollisions", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10280},
    {"DebugPrintInteractTarget", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10200},
    {"DebugPrintSpeed", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10100},
    {"DebugPrintWeapons", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10300},
    {"DebugRNGSeed", FieldType::Int, 1u, false, 0, 0.0f, "", 0x10580},
    {"DebugSortGroupName", FieldType::String, 1u, false, 0, 0.0f, "Standing", 0xe420},
    {"DefaultAssignee", FieldType::String, 1u, false, 0, 0.0f, "", 0x124a0},
    {"DefaultControlsChosen", FieldType::Bool, 0u, false, 0, 0.0f, "", 0xa3e0},
    {"DefaultProfileName", FieldType::String, 1u, false, 0, 0.0f, "", 0xaf70},
    {"DefaultTodoList", FieldType::String, 1u, false, 0, 0.0f, "Bugs", 0x12420},
    {"DevSaveCustomName", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xdcf0},
    {"DevSavePathStr", FieldType::String, 1u, false, 0, 0.0f, "", 0xddf0},
    {"DialogueVolume", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x3f800000}), "", 0xd240},
    {"DirectControl", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xa2e0},
    {"DirectLoad", FieldType::String, 0u, false, 0, 0.0f, "RoomOpening", 0xe620},
    {"DirectLoadCheckpoint", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xe700},
    {"DirectLoadSave", FieldType::String, 0u, false, 0, 0.0f, "", 0xe6a0},
    {"DirectLoadShell", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xe5a0},
    {"DisableAnalyticsInDebug", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x111a0},
    {"DisableAudio", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x12020},
    {"DisableManifestLogging", FieldType::Bool, 0u, false, 0, 0.0f, "", 0xca30},
    {"DisablePoolLogging", FieldType::Bool, 0u, false, 0, 0.0f, "", 0xc9b0},
    {"DisablePreDrawThread", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xcab0},
    {"DisableScreenSaver", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x89b0},
    {"DisplayIndex", FieldType::Int, 6u, false, -1, 0.0f, "", 0xfc30},
    {"DrawBlendMax", FieldType::Int, 1u, false, 99, 0.0f, "", 0xf5e0},
    {"DrawBloom", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xf4e0},
    {"DrawBuildNumber", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xf8f0},
    {"DrawColorGrading", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xf670},
    {"DrawDof", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xf560},
    {"DrawExtents", FieldType::Bool, 3u, false, 0, 0.0f, "", 0x98d0},
    {"DrawGround", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xf460},
    {"DrawHealthFX", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xea80},
    {"DrawIds", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xf870},
    {"DrawLights", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xeb00},
    {"DrawLightsWithExtents", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x9950},
    {"DumpRunStatsOnLoad", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x9b50},
    {"EasyMode", FieldType::Bool, 0u, false, 0, 0.0f, "", 0xd860},
    {"EditingMode", FieldType::Bool, 3u, false, 0, 0.0f, "", 0xbb30},
    {"EditorAngleSnapping", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x00000000}), "", 0xbdc0},
    {"EditorAutoOpenRecent", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xc510},
    {"EditorAutoSaveBackups", FieldType::Int, 1u, false, 20, 0.0f, "", 0xc620},
    {"EditorAutoSaveInterval", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x42f00000}), "", 0xc590},
    {"EditorCameraScrollSpeed", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3e800000}), "", 0xc2e0},
    {"EditorDrawGridBeforeGroup", FieldType::String, 0u, false, 0, 0.0f, "", 0xbf60},
    {"EditorGridSize", FieldType::Int, 1u, false, 256, 0.0f, "", 0xbd30},
    {"EditorOutlineThickness", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3f800000}), "", 0xc040},
    {"EditorOutlineThreshold", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3f000000}), "", 0xc0d0},
    {"EditorPreviewNameAndId", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xc160},
    {"EditorSelectionAsHighlight", FieldType::Bool, 0u, false, 0, 0.0f, "", 0xbfc0},
    {"EditorTimeLapseInterval", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x42f00000}), "", 0x10710},
    {"EditorWindowsStayOnTop", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xbee0},
    {"EmptyScriptActionAsserts", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x12870},
    {"EnableAudioDeviceChangeCallBack", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x121a0},
    {"EnableCloudSaves", FieldType::Bool, 2u, true, 0, 0.0f, "", 0x13160},
    {"EnableConsoleMenus", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x12af0},
    {"EnabledDebugDisplays", FieldType::Other, 1u, false, 111, 0.0f, "", 0x9240},
    {"EnableEditorTimeLapse", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10690},
    {"EnableEpicOverlay", FieldType::Bool, 2u, true, 0, 0.0f, "", 0x13360},
    {"EnableHulls", FieldType::Bool, 0u, true, 0, 0.0f, "", 0x10fa0},
    {"EnableLuaMessageHook", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x95d0},
    {"EnableOutOfMemoryAsserts", FieldType::Bool, 3u, true, 0, 0.0f, "", 0x12970},
    {"EnableSimulateCloudConflicts", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x131e0},
    {"EnableStreamingIntegration", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x11530},
    {"EnableTestLoadTask", FieldType::Bool, 7u, false, 0, 0.0f, "", 0x85a0},
    {"EnableTestLogs", FieldType::Bool, 7u, false, 0, 0.0f, "", 0x8520},
    {"EnableTwitch", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x115b0},
    {"EnableWin32DebugHelpLogging", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10610},
    {"ExclusiveInteractGroup", FieldType::String, 1u, false, 0, 0.0f, "", 0xa880},
    {"FastForward", FieldType::Bool, 3u, false, 0, 0.0f, "", 0x9bd0},
    {"FastForwardSpeed", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x40400000}), "", 0x9ad0},
    {"FlipMapThings", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x11330},
    {"ForceFlipMapThings", FieldType::Bool, 3u, false, 0, 0.0f, "", 0x113b0},
    {"ForceVoiceBankStreaming", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xd680},
    {"FreeFormSelecSearchFromId", FieldType::Int, 1u, false, 0, 0.0f, "", 0x12e70},
    {"FreeFormSelectGridLock", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x12cd0},
    {"FreeFormSelectRepeatDelay", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x00000000}), "", 0x12d50},
    {"FreeFormSelectRepeatInterval", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x00000000}), "", 0x12de0},
    {"FreeFormSelectStepDistance", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x41800000}), "", 0xa760},
    {"FreeFormSelectSuccessDistanceStep", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x41000000}), "", 0xa7f0},
    {"FreeFormSelectWrapX", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xa660},
    {"FreeFormSelectWrapY", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xa6e0},
    {"FullScreen", FieldType::Bool, 6u, true, 0, 0.0f, "", 0x8d40},
    {"FullscreenEffectGroup", FieldType::String, 1u, false, 0, 0.0f, "Vignette", 0xcf40},
    {"FxVolume", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x3f800000}), "", 0xd140},
    {"GamepadCursorControl", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xa560},
    {"GamepadCursorFreeFormSelect", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xa5e0},
    {"GamepadCursorInteractScale", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3f733333}), "", 0xab10},
    {"GamepadCursorScaleSpeed", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x40300000}), "", 0xaba0},
    {"GamepadCursorSlowSpeed", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x441c4000}), "", 0xa970},
    {"GamepadCursorSpeed", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x449c4000}), "", 0xa8e0},
    {"GamepadCursorTextOpacity", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3e99999a}), "", 0xaa80},
    {"GamepadLightBrightness", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x3f800000}), "", 0xaa00},
    {"GamepadSlot", FieldType::Int, 0u, false, 1, 0.0f, "", 0xb4a0},
    {"GpuIndex", FieldType::Int, 7u, false, 0, 0.0f, "", 0x8cb0},
    {"HardMode", FieldType::Bool, 0u, false, 0, 0.0f, "", 0xd8e0},
    {"HotLoadScripts", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xf7f0},
    {"IgnoreGamepads", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xa060},
    {"IsometricMovement", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xa460},
    {"KioskMode", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xd960},
    {"KioskModeNumLives", FieldType::Int, 1u, false, 2, 0.0f, "", 0xd9e0},
    {"Language", FieldType::String, 2u, false, 0, 0.0f, "", 0x12f80},
    {"LastManualLoadName", FieldType::String, 0u, false, 0, 0.0f, "", 0xe1e0},
    {"LastPatchNoteRevisionViewed", FieldType::Int, 0u, false, 0, 0.0f, "", 0xb0f0},
    {"LastRoadmapRevisionViewed", FieldType::Int, 0u, false, 0, 0.0f, "", 0xb200},
    {"LastUpdateDate", FieldType::String, 1u, false, 0, 0.0f, "03-12-2020", 0xe860},
    {"Letterbox", FieldType::Bool, 0u, true, 0, 0.0f, "", 0x8f40},
    {"LiveCreateTextures", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x10820},
    {"LiveCreateTexturesRetryAttempts", FieldType::Int, 1u, false, 10, 0.0f, "", 0x108a0},
    {"LoadMapScreenFilter", FieldType::String, 0u, false, 0, 0.0f, "", 0xe240},
    {"LoadMapScreenSortReverse", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xe160},
    {"LoadMapScreenSortType", FieldType::String, 0u, false, 0, 0.0f, "Chronological", 0xe0e0},
    {"LoadSavedControls", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xaef0},
    {"LoadSaveScreenFilter", FieldType::String, 0u, false, 0, 0.0f, "", 0xe2a0},
    {"LoadSaveScreenSortReverse", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xe060},
    {"LoadSaveScreenSortType", FieldType::String, 0u, false, 0, 0.0f, "Chronological", 0xdfe0},
    {"LoadScreenHints", FieldType::String, 1u, false, 0, 0.0f, "", 0x10bb0},
    {"LoadScreenMinTime", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x00000000}), "", 0x10b20},
    {"LoadScreenShowComponentsDelay", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x00000000}), "", 0x10a90},
    {"LockInputToWindow", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xa1e0},
    {"LogCombatMultipliers", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x9650},
    {"LogFrameNumber", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x96d0},
    {"Logging", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x93d0},
    {"LogLuaMemoryDump", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x97d0},
    {"LogPlayTime", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x107a0},
    {"LogRandomDebug", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x9750},
    {"LogTelemetry", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x9850},
    {"LuaBreakpointEdit", FieldType::Bool, 3u, false, 0, 0.0f, "", 0xbc30},
    {"LuaDataBreakpoint", FieldType::String, 1u, false, 0, 0.0f, "", 0xfd20},
    {"LuaGCStepSize", FieldType::Int, 0u, false, 4, 0.0f, "", 0x11020},
    {"LuaIgnoreListFileStr", FieldType::String, 1u, false, 0, 0.0f, "_saveData", 0xfd80},
    {"LuaSpikeWarningMs", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x00000000}), "", 0x11ad0},
    {"LuaWatchListFileStr", FieldType::String, 1u, false, 0, 0.0f, "", 0xfcc0},
    {"MasterVolume", FieldType::Float, 2u, false, 0, std::bit_cast<float>(uint32_t{0x3f800000}), "", 0xd2c0},
    {"MaxDevSaves", FieldType::Int, 1u, false, 2000, 0.0f, "", 0xdf50},
    {"MaxPerCmdDraws", FieldType::Int, 7u, false, 256, 0.0f, "", 0x8920},
    {"MaxWorkerThreads", FieldType::Int, 1u, false, 6, 0.0f, "", 0xcb30},
    {"MissingCue", FieldType::String, 1u, false, 0, 0.0f, "", 0xd780},
    {"MouseDeadZone", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x00000000}), "", 0xed10},
    {"MousePropelSpeed", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x447a0000}), "", 0xee30},
    {"MouseTetherEnabled", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xec00},
    {"MouseTetherWeight", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3f800000}), "", 0xec80},
    {"MouseTetherZone", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x44480000}), "", 0xeda0},
    {"MouseXDistanceMultiplier", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3f800000}), "", 0xf070},
    {"MouseYDistanceMultiplier", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3f266666}), "", 0xefe0},
    {"MultiThreadMeshFromDrawable", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xdb70},
    {"MusicVolume", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x3f800000}), "", 0xd0c0},
    {"NewUpdateSoundRevision", FieldType::Int, 1u, false, 31591, 0.0f, "", 0xb280},
    {"NewUpdateSoundRevisionViewed", FieldType::Int, 0u, false, 0, 0.0f, "", 0xb310},
    {"NextUpdateDate", FieldType::String, 1u, false, 0, 0.0f, "", 0xe8e0},
    {"NoBorder", FieldType::Bool, 6u, false, 0, 0.0f, "", 0x8dc0},
    {"NoDiscard", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10e90},
    {"NoFixedStep", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x8ec0},
    {"NonPS4GamepadRumbleMultiplier", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x3f4ccccd}), "", 0xb830},
    {"OffscreenIndicatorMargin", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x437a0000}), "", 0xc8b0},
    {"OldestAllowedSave", FieldType::String, 1u, false, 0, 0.0f, "", 0xe780},
    {"OnlyFancyText", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10e10},
    {"OutputWindowLogging", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x94d0},
    {"ParseTextMarkup", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xc370},
    {"PatchNoteRevision", FieldType::Int, 1u, false, 36368, 0.0f, "", 0xb060},
    {"PauseSoundsWhenPaused", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x9350},
    {"PerfDash", FieldType::Bool, 3u, false, 0, 0.0f, "", 0x117d0},
    {"PerfDashFpsOnly", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x118d0},
    {"PerfDashWarnings", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x11850},
    {"PermanentVfxCap", FieldType::Int, 1u, false, 80, 0.0f, "", 0xf190},
    {"PlatformAudioAlloc", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xd600},
    {"PlayerInputDebug", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x13440},
    {"PlayerOptInToCloudSaves", FieldType::Bool, 2u, false, 0, 0.0f, "", 0x13260},
    {"ProfileAudio", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xd3d0},
    {"ProfileZoneUsecThreshold", FieldType::Long, 1u, false, 500, 0.0f, "", 0x11220},
    {"PS4Gamepad", FieldType::Bool, 3u, false, 0, 0.0f, "", 0xb520},
    {"PS4GamepadRumbleMultiplier", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x3f800000}), "", 0xb7b0},
    {"RallyPointAnimation", FieldType::String, 1u, false, 0, 0.0f, "MoveHint", 0xae70},
    {"RepeatSaveOnLoad", FieldType::Bool, 3u, false, 0, 0.0f, "", 0x9ee0},
    {"RequireFocusToUpdate", FieldType::Bool, 2u, true, 0, 0.0f, "", 0x9a50},
    {"ResetProgress", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xb9b0},
    {"ResetRoomData", FieldType::Bool, 2u, false, 0, 0.0f, "", 0x11430},
    {"RoadmapRevision", FieldType::Int, 1u, false, 32900, 0.0f, "", 0xb170},
    {"RotatedIsometricMovement", FieldType::Bool, 0u, false, 0, 0.0f, "", 0xa4e0},
    {"Rumble", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xcfc0},
    {"SafeFrameCompliant", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xc930},
    {"SaveCollectionBeginDate", FieldType::String, 1u, false, 0, 0.0f, "", 0xe9c0},
    {"SaveCollectionEndDate", FieldType::String, 1u, false, 0, 0.0f, "", 0xea20},
    {"SaveNotableLuaInfo", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xdd70},
    {"SaveToTempAndMove", FieldType::Bool, 0u, false, 0, 0.0f, "", 0x12b70},
    {"ScreenshotCount", FieldType::Int, 6u, false, 0, 0.0f, "", 0xafd0},
    {"ScriptDebugFilter", FieldType::String, 0u, false, 0, 0.0f, "", 0xe360},
    {"ScriptErrorAsserts", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x10480},
    {"SelectionRelativeEditing", FieldType::Bool, 3u, true, 0, 0.0f, "", 0xc1e0},
    {"SfxFrameCap", FieldType::Int, 1u, false, 16, 0.0f, "", 0xf340},
    {"ShowDamageNumbers", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xc730},
    {"ShowDocumentsPathError", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x122a0},
    {"ShowGameplayTimer", FieldType::Bool, 0u, false, 0, 0.0f, "", 0xc7b0},
    {"ShowLogFileError", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x12220},
    {"ShowMissingSubtitles", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xccc0},
    {"ShowOffscreenIndicators", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xc830},
    {"ShowSaveErrors", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x10500},
    {"ShowSocialLinks", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xda70},
    {"ShowSpeechBubble", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xd1c0},
    {"ShowUIAnimations", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xc6b0},
    {"SingleThread", FieldType::Bool, 7u, false, 0, 0.0f, "", 0x8620},
    {"SkipVideoCardTest", FieldType::Bool, 0u, false, 0, 0.0f, "", 0x10d90},
    {"SteamAccessToken", FieldType::String, 2u, false, 0, 0.0f, "", 0x11770},
    {"SteamGamepadCount", FieldType::Int, 3u, false, 0, 0.0f, "", 0xb6a0},
    {"SteamTrackpadMultiplier", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x41700000}), "", 0xb730},
    {"StepFrameRepeatInterval", FieldType::Int, 0u, false, 0, 0.0f, "", 0x9f60},
    {"SubmitBugsToJira", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x12710},
    {"Subtitles", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xcc40},
    {"SwapInterval", FieldType::Int, 0u, false, 1, 0.0f, "", 0x8e40},
    {"SwitchGamepad", FieldType::Bool, 3u, false, 0, 0.0f, "", 0xb620},
    {"TelemetryServer", FieldType::String, 1u, false, 0, 0.0f, "file", 0x10a10},
    {"TerrainFxGroup", FieldType::String, 1u, false, 0, 0.0f, "Shadows", 0xcec0},
    {"TextDebugShowIds", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x12f00},
    {"TextInputRepeatDelay", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3e99999a}), "", 0xc3f0},
    {"TextInputRepeatInterval", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3d4ccccd}), "", 0xc480},
    {"ThingDebugFilter", FieldType::String, 0u, false, 0, 0.0f, "", 0xe3c0},
    {"TooltipHideDelay", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3f000000}), "", 0xade0},
    {"TooltipShowDelay", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x3f000000}), "", 0xad50},
    {"TooltipsOnPlayerRadius", FieldType::Int, 1u, false, 0, 0.0f, "", 0xac30},
    {"TooltipSwitchBlockFrames", FieldType::Int, 1u, false, 5, 0.0f, "", 0xacc0},
    {"TwitchChatChannel", FieldType::String, 1u, false, 0, 0.0f, "", 0x116b0},
    {"TwitchRefreshToken", FieldType::String, 2u, false, 0, 0.0f, "", 0x11710},
    {"UiLanguage", FieldType::String, 2u, false, 0, 0.0f, "", 0x12fe0},
    {"UpdateWarningDate", FieldType::String, 1u, false, 0, 0.0f, "09-04-2020", 0xe940},
    {"UseAltCursor", FieldType::Bool, 0u, false, 0, 0.0f, "", 0xa260},
    {"UseAnalytics", FieldType::Bool, 0u, true, 0, 0.0f, "", 0x11120},
    {"UseAudioDriver", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xd350},
    {"UseAutoPlaySaveQueue", FieldType::Bool, 3u, false, 0, 0.0f, "", 0x9c50},
    {"UseBc3Assets", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10d10},
    {"UseBugReportVideo", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x12590},
    {"UseCpuBoost", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x8c30},
    {"UseDefaultGpu", FieldType::Bool, 7u, false, 0, 0.0f, "", 0x8720},
    {"UseDeferredResourceCreation", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x8b30},
    {"UseDevSaves", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xdbf0},
    {"UseDx10", FieldType::Bool, 7u, false, 0, 0.0f, "", 0x86a0},
    {"UseDXGI12SwapChain", FieldType::Bool, 7u, true, 0, 0.0f, "", 0x8820},
    {"UseGamepadGlyphs", FieldType::Bool, 0u, false, 0, 0.0f, "", 0xa160},
    {"UseInternalBugReporter", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x12690},
    {"UseLowResAssets", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x10c90},
    {"UseMouse", FieldType::Bool, 0u, false, 0, 0.0f, "", 0xa0e0},
    {"UseNativeAnalytics", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x11e00},
    {"UseNativeAudio", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x11fa0},
    {"UseNativeGaInput", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x120a0},
    {"UseNativeGaInputNoControllers", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x12120},
    {"UseNativeOpenGLRenderer", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x11c80},
    {"UseNativeOS", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x11d00},
    {"UseOcclusion", FieldType::Bool, 1u, true, 0, 0.0f, "", 0xba30},
    {"UseOverlayHardware", FieldType::Bool, 3u, false, 0, 0.0f, "", 0x8bb0},
    {"UseRenderMeshCache", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x8ab0},
    {"UseResourceLoaderSingleThread", FieldType::Bool, 7u, true, 0, 0.0f, "", 0x88a0},
    {"UserName", FieldType::String, 1u, false, 0, 0.0f, "", 0x109b0},
    {"UseSideBarArt", FieldType::Bool, 0u, true, 0, 0.0f, "", 0x8fc0},
    {"UseSwapEffectDiscard", FieldType::Bool, 7u, false, 0, 0.0f, "", 0x87a0},
    {"UseTelemetry", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x110a0},
    {"UseTheForgeRenderer", FieldType::Bool, 1u, false, 0, 0.0f, "", 0x11d80},
    {"ValidateSaves", FieldType::Bool, 0u, false, 0, 0.0f, "", 0x12bf0},
    {"ValidCheckpoint", FieldType::Bool, 0u, true, 0, 0.0f, "", 0xdc70},
    {"ValueRelativeEditing", FieldType::Bool, 3u, true, 0, 0.0f, "", 0xc260},
    {"VerboseScriptLogging", FieldType::Bool, 1u, true, 0, 0.0f, "", 0x9550},
    {"VfxFrameCap", FieldType::Int, 1u, false, 60, 0.0f, "", 0xf220},
    {"VfxManagerCapMultiplier", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x40800000}), "", 0xf2b0},
    {"VfxScalarDamage", FieldType::Float, 1u, false, 0, std::bit_cast<float>(uint32_t{0x43480000}), "", 0xf100},
    {"WindowHeight", FieldType::Int, 2u, false, 0, 0.0f, "", 0x11e80},
    {"WindowWidth", FieldType::Int, 2u, false, 0, 0.0f, "", 0x11f10},
    {"WindowX", FieldType::Int, 6u, false, -1, 0.0f, "", 0xfa90},
    {"WindowY", FieldType::Int, 6u, false, -1, 0.0f, "", 0xfb20},
    {"X", FieldType::Int, 6u, false, 0, 0.0f, "", 0xf970},
    {"XboxOneGamepad", FieldType::Bool, 3u, false, 0, 0.0f, "", 0xb5a0},
    {"Y", FieldType::Int, 6u, false, 0, 0.0f, "", 0xfa00},
    {"ZeroMouseTether", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xeb80},
    {"ZPassCullRadius", FieldType::Float, 0u, false, 0, std::bit_cast<float>(uint32_t{0x42200000}), "", 0x10f10},
    {"ZTesting", FieldType::Bool, 1u, false, 0, 0.0f, "", 0xe520},
    };
    return kOptions;
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

Value defaultValue(const OptionSpec& s) {
    Value v; v.type = s.type;
    switch (s.type) {
    case FieldType::Bool: v.b = s.defaultBool; break;
    case FieldType::Int: case FieldType::Long: case FieldType::Other: v.i = s.defaultInt; break;
    case FieldType::Float: v.f = s.defaultFloat; break;
    case FieldType::String: v.s = s.defaultString; break;
    }
    return v;
}

// ---------------- SJSON reader (nflibs nfjp, settings {1,1,0,1,1,1,1,1,1}) ----------------
// Observed in nfjp_parse_with_settings 0x180325ed0 and helpers: braced root
// required (implicit root setting = 0), // and /* */ comments and commas are
// whitespace (skip_whitespace 0x180327700), unquoted keys, '=' or ':' (parse_members
// 0x180326fa2/0x180326fad), JSON escapes incl. \uXXXX, Python """ strings.
enum class Tag { Null, False, True, Number, String, Array, Object };
struct Scalar {
    Tag tag = Tag::Null;
    double number = 0;
    std::string text;               // String
    std::string_view firstMember;   // Object: raw text of its first member value
};

struct Reader {
    std::string_view t; std::size_t p = 0; std::string error;
    bool fail(const char* what) { if (error.empty()) { error = what; error += " at offset " + std::to_string(p); } return false; }
    bool eof() const { return p >= t.size(); }
    void ws() {
        while (p < t.size()) {
            const char c = t[p];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') { ++p; continue; }
            if (c == '/' && p + 1 < t.size() && t[p + 1] == '/') { while (p < t.size() && t[p] != '\n') ++p; continue; }
            if (c == '/' && p + 1 < t.size() && t[p + 1] == '*') {
                p += 2; while (p + 1 < t.size() && !(t[p] == '*' && t[p + 1] == '/')) ++p;
                p = std::min(t.size(), p + 2); continue;
            }
            break;
        }
    }
    static void utf8(std::string& o, uint32_t c) {
        if (c < 0x80) o += char(c);
        else if (c < 0x800) { o += char(0xc0 | (c >> 6)); o += char(0x80 | (c & 0x3f)); }
        else if (c < 0x10000) { o += char(0xe0 | (c >> 12)); o += char(0x80 | ((c >> 6) & 0x3f)); o += char(0x80 | (c & 0x3f)); }
        else { o += char(0xf0 | (c >> 18)); o += char(0x80 | ((c >> 12) & 0x3f)); o += char(0x80 | ((c >> 6) & 0x3f)); o += char(0x80 | (c & 0x3f)); }
    }
    bool string(std::string& out) {
        ++p; // opening quote
        if (t.substr(p, 2) == "\"\"") { // """multiline"""
            p += 2; const std::size_t end = t.find("\"\"\"", p);
            if (end == std::string_view::npos) return fail("unterminated multiline string");
            out.assign(t.substr(p, end - p)); p = end + 3; return true;
        }
        while (p < t.size() && t[p] != '"') {
            char c = t[p++];
            if (c != '\\') { out += c; continue; }
            if (p >= t.size()) break;
            c = t[p++];
            switch (c) {
            case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
            case 'r': out += '\r'; break; case 't': out += '\t'; break;
            case 'u': {
                if (p + 4 > t.size()) return fail("bad \\u escape");
                uint32_t v = 0;
                for (int k = 0; k < 4; ++k) {
                    const char h = t[p++]; v <<= 4;
                    if (h >= '0' && h <= '9') v |= uint32_t(h - '0');
                    else if (lower(h) >= 'a' && lower(h) <= 'f') v |= uint32_t(lower(h) - 'a' + 10);
                    else return fail("bad \\u escape");
                }
                utf8(out, v); break;
            }
            default: out += c; break;
            }
        }
        if (p >= t.size()) return fail("unterminated string");
        ++p; return true;
    }
    bool key(std::string& out) {
        if (eof()) return fail("expected key");
        if (t[p] == '"') return string(out);
        const std::size_t b = p;
        while (p < t.size()) {
            const char c = t[p];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '=' || c == ':' || c == '{' || c == '}' || c == '[' || c == ']' || c == ',' || c == '"') break;
            ++p;
        }
        if (p == b) return fail("expected key");
        out.assign(t.substr(b, p - b)); return true;
    }
    bool skipComposite(char open, char close) {
        // Validate and skip a nested object/array (kept as raw text).
        ++p; ws();
        while (!eof() && t[p] != close) {
            if (open == '{') {
                std::string k; if (!key(k)) return false; ws();
                if (eof() || (t[p] != '=' && t[p] != ':')) return fail("expected '=' or ':'");
                ++p; ws();
            }
            Scalar s; std::string_view raw; if (!value(s, raw)) return false; ws();
        }
        if (eof()) return fail("unterminated object/array");
        ++p; return true;
    }
    bool value(Scalar& s, std::string_view& raw) {
        ws(); if (eof()) return fail("expected value");
        const std::size_t b = p; const char c = t[p];
        if (c == '{') {
            const std::size_t save = p; ++p; ws();
            if (!eof() && t[p] != '}') {
                std::string k; if (!key(k)) return false; ws();
                if (!eof() && (t[p] == '=' || t[p] == ':')) { ++p; ws(); Scalar inner; std::string_view r; if (!value(inner, r)) return false; s.firstMember = r; }
            }
            p = save; if (!skipComposite('{', '}')) return false; s.tag = Tag::Object;
        } else if (c == '[') { if (!skipComposite('[', ']')) return false; s.tag = Tag::Array; }
        else if (c == '"') { if (!string(s.text)) return false; s.tag = Tag::String; }
        else if (t.substr(p, 4) == "true") { p += 4; s.tag = Tag::True; }
        else if (t.substr(p, 5) == "false") { p += 5; s.tag = Tag::False; }
        else if (t.substr(p, 4) == "null") { p += 4; s.tag = Tag::Null; }
        else if (c == '-' || (c >= '0' && c <= '9')) {
            std::size_t e = p + 1;
            while (e < t.size() && t[e] != '\0' && std::strchr("0123456789.eE+-", t[e]) != nullptr) ++e;
            const std::string num(t.substr(p, e - p)); char* endp = nullptr;
            s.number = std::strtod(num.c_str(), &endp);
            if (endp != num.c_str() + num.size()) return fail("bad number");
            p = e; s.tag = Tag::Number;
        } else return fail("unexpected character");
        raw = t.substr(b, p - b); return true;
    }
};

bool parseScalar(std::string_view text, Scalar& s) {
    Reader r; r.t = text; std::string_view raw; return r.value(s, raw);
}

int32_t truncToInt(double d) { // cvttsd2si: out of range / NaN -> INT32_MIN
    if (!(d > -2147483649.0 && d < 2147483648.0)) return std::numeric_limits<int32_t>::min();
    return int32_t(d);
}

std::string formatFloat(double d) { // "%f" under setlocale(LC_ALL,"C") (ToString 0x180104019)
    char buf[512]; std::snprintf(buf, sizeof buf, "%f", d); return buf;
}

// sgg::Unescape 0x18010ac70 as called at 0x18010b25c: every "\\\\" -> "\\".
void engineUnescape(std::string& s) {
    std::size_t at = 0;
    while ((at = s.find("\\\\", at)) != std::string::npos) { s.replace(at, 2, "\\"); at += 1; }
}

// SGD_Deserialize<ConfigOptions> 0x18010adf0 per type tag.
void applyValue(const OptionSpec& spec, Value& v, const Scalar& in) {
    switch (spec.type) {
    case FieldType::Bool: // 0x18010af36: false/true literals only
        if (in.tag == Tag::False) v.b = false; else if (in.tag == Tag::True) v.b = true;
        break;
    case FieldType::Int: // 0x18010af98: number truncated, anything else 0
        v.i = in.tag == Tag::Number ? truncToInt(in.number) : 0; break;
    case FieldType::Long: { // SGD_Deserialize<long> 0x18010d4e0
        const Scalar* cur = &in; Scalar member;
        while (cur->tag == Tag::Object && !cur->firstMember.empty() && parseScalar(cur->firstMember, member)) cur = &member;
        if (cur->tag == Tag::Number) v.i = truncToInt(cur->number);
        else if (cur->tag == Tag::False) v.i = 0; else if (cur->tag == Tag::True) v.i = 1;
        break;
    }
    case FieldType::Float: // 0x18010b005: number -> float32, anything else 0
        v.f = in.tag == Tag::Number ? float(in.number) : 0.0f; break;
    case FieldType::String: { // SGD_Deserialize(string) 0x18003f130 then Unescape
        Scalar member; const Scalar* cur = &in;
        while (cur->tag == Tag::Object && !cur->firstMember.empty() && parseScalar(cur->firstMember, member)) cur = &member;
        if (cur->tag == Tag::Null) v.s.clear();
        else if (cur->tag == Tag::Number) v.s = formatFloat(cur->number);
        else if (cur->tag == Tag::String) v.s = cur->text;
        engineUnescape(v.s);
        break;
    }
    case FieldType::Other: break; // raw text kept by caller (FromString not reconstructed)
    }
}

void appendU32(std::vector<uint8_t>& o, uint32_t v) { for (int k = 0; k < 4; ++k) o.push_back(uint8_t(v >> (8 * k))); }
struct In {
    const uint8_t* d; std::size_t n, p = 0;
    bool u32(uint32_t& v) { if (n - p < 4) return false; v = uint32_t(d[p]) | uint32_t(d[p + 1]) << 8 | uint32_t(d[p + 2]) << 16 | uint32_t(d[p + 3]) << 24; p += 4; return true; }
    bool i32(int32_t& v) { uint32_t u; if (!u32(u)) return false; v = int32_t(u); return true; }
    bool u8(uint8_t& v) { if (p >= n) return false; v = d[p++]; return true; }
};

struct KeyPair { int32_t a, b; };
// GetMappedKeyFromGaInputKey 0x18019bf90 (jump tables 0x18019c390/0x18019c208);
// identity for every value not listed.
constexpr KeyPair kKeyToFile[] = {
    {0,27}, {1,112}, {2,113}, {3,114}, {4,115}, {5,116}, {6,117}, {7,118}, {8,119}, {9,120}, {10,121}, {11,122}, {12,123},
    {39,220}, {44,188}, {45,189}, {46,190}, {97,37}, {98,39}, {99,38}, {100,40}, {101,45}, {102,36}, {103,46}, {104,35},
    {105,33}, {106,34}, {109,111}, {110,106}, {111,109}, {112,107}, {114,96}, {115,97}, {116,98}, {117,99}, {118,100},
    {119,101}, {120,102}, {121,103}, {122,104}, {123,105}, {125,8}, {126,9}, {127,13}, {128,20}, {129,160}, {130,162},
    {131,91}, {132,164}, {133,165}, {134,92}, {136,163}, {137,161}, {158,187}, {171,219}, {172,221}, {173,191},
    {174,222}, {175,192}, {176,186}};
// GetCorrectlyMappedKey 0x18019bac0 (tables 0x18019beb4/0x18019bd30); identity
// otherwise. Not an exact inverse: 187 (written for KeyPlus 158) reads back as 187.
constexpr KeyPair kKeyFromFile[] = {
    {8,125}, {9,126}, {13,127}, {20,128}, {27,0}, {33,105}, {34,106}, {35,104}, {36,102}, {37,97}, {38,99}, {39,98},
    {40,100}, {45,101}, {46,103}, {91,131}, {92,134}, {96,114}, {97,115}, {98,116}, {99,117}, {100,118}, {101,119},
    {102,120}, {103,121}, {104,122}, {105,123}, {106,110}, {107,112}, {109,111}, {111,109}, {112,1}, {113,2}, {114,3},
    {115,4}, {116,5}, {117,6}, {118,7}, {119,8}, {120,9}, {121,10}, {122,11}, {123,12}, {160,129}, {161,137},
    {162,130}, {163,136}, {164,132}, {165,133}, {186,176}, {188,44}, {189,45}, {190,46}, {191,173}, {192,175},
    {219,171}, {220,39}, {221,172}, {222,174}};
// RemappableControl::Save pad switch (table 0x18019ad90, GamepadButtonId 44..61).
constexpr KeyPair kPadToFile[] = {
    {44,0x10}, {45,0x20}, {46,0x4}, {47,0x8}, {48,0x1}, {49,0x2}, {50,0x1000}, {51,0x2000}, {52,0x4000}, {53,0x8000},
    {54,0x100}, {55,0x200}, {56,0x800000}, {57,0x400000}, {58,0x40}, {59,0x80}, {61,0x800}};

std::string_view controlName(std::string_view control) { // RemappableControl::ControlName
    if (control == "Up") return "MoveUp";
    if (control == "Down") return "MoveDown";
    if (control == "Left") return "MoveLeft";
    if (control == "Right") return "MoveRight";
    if (control == "Pause") return "Start";
    return control;
}

} // namespace

std::string profileConfigFileName(std::string_view profile) { return std::string(profile) + std::string(kConfigExtension); }
std::string profileControlsFileName(std::string_view profile) { return std::string(profile) + std::string(kControlsExtension); }

const std::vector<OptionSpec>& optionSpecs() { return optionTable(); }
const OptionSpec* findOption(std::string_view name) {
    for (const auto& s : optionTable()) if (iequals(s.name, name)) return &s;
    return nullptr;
}

std::string formatValue(const Value& v) {
    switch (v.type) {
    case FieldType::Bool: return v.b ? "true" : "false";
    case FieldType::Int: case FieldType::Long: return std::to_string(v.i); // "%d"
    case FieldType::Float: return formatFloat(double(v.f));                // "%f"
    case FieldType::String: return v.s.empty() ? std::string() : "\"" + v.s + "\""; // "\"%s\"", no escaping
    case FieldType::Other: return v.s;
    }
    return {};
}

ConfigFile::ConfigFile() {
    entries_.reserve(optionTable().size());
    for (const auto& s : optionTable()) { Entry e; e.key = s.name; e.spec = &s; e.value = defaultValue(s); entries_.push_back(std::move(e)); }
}

ConfigFile::Entry* ConfigFile::find(std::string_view name) {
    for (auto& e : entries_) if (iequals(e.key, name)) return &e;
    return nullptr;
}
const ConfigFile::Entry* ConfigFile::find(std::string_view name) const {
    for (const auto& e : entries_) if (iequals(e.key, name)) return &e;
    return nullptr;
}

void ConfigFile::resetDefaults() {
    for (auto& e : entries_) if (e.spec) { e.value = defaultValue(*e.spec); e.raw.clear(); e.present = false; }
}

bool ConfigFile::parse(std::string_view text, std::string* error) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.spec == nullptr; }), entries_.end());
    resetDefaults();
    // Byte-order marks stripped as in SGD_Deserialize<ConfigOptions> 0x18010d35d.
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    else if (text.substr(0, 4) == std::string_view("\xFF\xFE\0\0", 4) || text.substr(0, 4) == std::string_view("\0\0\xFE\xFF", 4)) text.remove_prefix(4);
    else if (text.substr(0, 2) == "\xFF\xFE" || text.substr(0, 2) == "\xFE\xFF") text.remove_prefix(2);
    Reader r; r.t = text; r.ws();
    if (r.eof() || text[r.p] != '{') { if (error) *error = "root is not a braced object"; return false; }
    ++r.p;
    std::vector<std::pair<std::string, std::pair<Scalar, std::string_view>>> members;
    for (r.ws(); !r.eof() && text[r.p] != '}'; r.ws()) {
        std::string k; if (!r.key(k)) break;
        r.ws();
        if (r.eof() || (text[r.p] != '=' && text[r.p] != ':')) { r.fail("expected '=' or ':'"); break; }
        ++r.p;
        Scalar s; std::string_view raw; if (!r.value(s, raw)) break;
        members.push_back({std::move(k), {std::move(s), raw}});
    }
    if (r.error.empty() && r.eof()) r.fail("unterminated root object");
    if (!r.error.empty()) { if (error) *error = r.error; return false; } // nfjp error: nothing applied
    for (auto& [k, sv] : members) {
        Entry* e = find(k);
        if (!e) { Entry u; u.key = k; u.raw = std::string(sv.second); u.present = true; entries_.push_back(std::move(u)); continue; }
        if (!e->spec) { e->raw = std::string(sv.second); continue; }
        applyValue(*e->spec, e->value, sv.first);
        if (e->spec->type == FieldType::Other) e->value.s = std::string(sv.second);
        e->present = true;
    }
    return true;
}

bool ConfigFile::isDefault(std::string_view name) const {
    const Entry* e = find(name);
    if (!e || !e->spec) return false;
    const OptionSpec& s = *e->spec;
    switch (s.type) {
    case FieldType::Bool: return e->value.b == s.defaultBool;
    case FieldType::Int: case FieldType::Long: return e->value.i == s.defaultInt;
    case FieldType::Float: {
        const float a = e->value.f, b = s.defaultFloat, d = std::fabs(a - b);
        if (d <= std::bit_cast<float>(uint32_t{8})) return true;
        return std::max(std::fabs(a), std::fabs(b)) * std::bit_cast<float>(uint32_t{0x38d1b717}) >= d;
    }
    case FieldType::String: return e->value.s == s.defaultString;
    case FieldType::Other: return !e->present; // raw only; FromString/IsDefault for enums not reconstructed
    }
    return false;
}

std::string ConfigFile::serialize(const WriteOptions& o) const {
    std::string out = "{\n";
    for (const auto& e : entries_) {
        if (!e.spec) {
            if (o.preserveUnknownKeys) out += "  " + e.key + " = " + e.raw + "\n";
            continue;
        }
        if (!e.spec->persisted() && !o.includeNotSavedOptions) continue;
        if (isDefault(e.key)) continue;
        out += "  "; out += e.spec->name; out += " = "; out += formatValue(e.value); out += "\n";
    }
    out += "}\n";
    return out;
}

const Value* ConfigFile::value(std::string_view name) const { const Entry* e = find(name); return (e && e->spec) ? &e->value : nullptr; }
std::optional<bool> ConfigFile::getBool(std::string_view n) const { const Value* v = value(n); if (!v || v->type != FieldType::Bool) return std::nullopt; return v->b; }
std::optional<int32_t> ConfigFile::getInt(std::string_view n) const { const Value* v = value(n); if (!v || (v->type != FieldType::Int && v->type != FieldType::Long)) return std::nullopt; return v->i; }
std::optional<float> ConfigFile::getFloat(std::string_view n) const { const Value* v = value(n); if (!v || v->type != FieldType::Float) return std::nullopt; return v->f; }
std::optional<std::string> ConfigFile::getString(std::string_view n) const { const Value* v = value(n); if (!v || v->type != FieldType::String) return std::nullopt; return v->s; }
bool ConfigFile::setBool(std::string_view n, bool x) { Entry* e = find(n); if (!e || !e->spec || e->spec->type != FieldType::Bool) return false; e->value.b = x; e->present = true; return true; }
bool ConfigFile::setInt(std::string_view n, int32_t x) { Entry* e = find(n); if (!e || !e->spec || (e->spec->type != FieldType::Int && e->spec->type != FieldType::Long)) return false; e->value.i = x; e->present = true; return true; }
bool ConfigFile::setFloat(std::string_view n, float x) { Entry* e = find(n); if (!e || !e->spec || e->spec->type != FieldType::Float || !std::isfinite(x)) return false; e->value.f = x; e->present = true; return true; }
bool ConfigFile::setString(std::string_view n, std::string x) { Entry* e = find(n); if (!e || !e->spec || e->spec->type != FieldType::String) return false; e->value.s = std::move(x); e->present = true; return true; }

std::vector<std::pair<std::string, std::string>> ConfigFile::unknownKeys() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& e : entries_) if (!e.spec) out.emplace_back(e.key, e.raw);
    return out;
}
void ConfigFile::setUnknownRaw(std::string key, std::string raw) {
    if (Entry* e = find(key)) { if (!e->spec) e->raw = std::move(raw); return; }
    Entry u; u.key = std::move(key); u.raw = std::move(raw); u.present = true; entries_.push_back(std::move(u));
}

// ---------------- .ctrls ----------------
int32_t keyToFile(int32_t k) { for (const auto& m : kKeyToFile) if (m.a == k) return m.b; return k; }
int32_t keyFromFile(int32_t k) { for (const auto& m : kKeyFromFile) if (m.a == k) return m.b; return k; }
uint32_t padToFile(int32_t b) { for (const auto& m : kPadToFile) if (m.a == b) return uint32_t(m.b); return 0xffffffffu; }
int32_t padFromFile(uint32_t mask) { for (const auto& m : kPadToFile) if (uint32_t(m.b) == mask) return m.a; return kPadButtonUnmapped; }
int32_t mouseToFile(int32_t b) {
    switch (b) {
    case -1: return 0;
    case 0: return 0x100000; case 1: return 0x200000; case 2: return 0x400000; case 3: return 0x800000;
    case 4: return 0x1000000; case 5: return 0x3000000; case 6: return 0x5000000;
    default: return b; // out of switch range: raw value
    }
}
int32_t mouseFromFile(int32_t v) {
    switch (v) {
    case -1: case 0: return kMouseButtonNone;
    case 0x100000: return 0; case 0x200000: return 1; case 0x400000: return 2; case 0x800000: return 3;
    case 0x1000000: return 4; case 0x3000000: return 5; case 0x5000000: return 6;
    default: return v;
    }
}

std::vector<uint8_t> writeControlsFile(const std::vector<ControlRecord>& controls) {
    std::vector<uint8_t> o;
    appendU32(o, kSgb1Magic);                 // BinarySaveData(0x800, checksum=false): 4-byte header
    appendU32(o, uint32_t(controls.size()));  // Controls::Scheme element count (0x18018f5e4)
    for (const auto& c : controls) {
        appendU32(o, uint32_t(kControlRecordVersion));
        appendU32(o, uint32_t(c.name.size())); o.insert(o.end(), c.name.begin(), c.name.end());
        appendU32(o, uint32_t(c.keys.size()));
        for (int32_t k : c.keys) appendU32(o, uint32_t(keyToFile(k)));
        appendU32(o, padToFile(c.gamepadButton));
        appendU32(o, uint32_t(mouseToFile(c.mouseButton)));
        o.push_back(c.useGamepadButton ? 1 : 0);
        o.push_back(c.requiresShift ? 1 : 0);
    }
    return o;
}

bool readControlsFile(const uint8_t* data, std::size_t size, std::vector<ControlRecord>& out, std::string* error) {
    out.clear();
    auto bad = [&](const char* m) { if (error) *error = m; out.clear(); return false; };
    In in{data, size};
    uint32_t magic = 0;
    if (!in.u32(magic) || magic != kSgb1Magic) return bad("missing SGB1 header (legacy pre-cppCompat controls file not supported)");
    int32_t count = 0;
    if (!in.i32(count)) return bad("truncated count");
    for (int32_t i = 0; i < count; ++i) {
        int32_t version = 0;
        if (!in.i32(version)) return bad("truncated record");
        if (version != kControlRecordVersion) continue; // disabled control, nothing more consumed
        ControlRecord c; uint32_t len = 0;
        if (!in.u32(len) || in.n - in.p < len) return bad("truncated name");
        c.name.assign(reinterpret_cast<const char*>(in.d + in.p), len); in.p += len;
        int32_t nkeys = 0;
        if (!in.i32(nkeys) || nkeys < 0 || std::size_t(nkeys) > (in.n - in.p) / 4) return bad("bad key count");
        for (int32_t k = 0; k < nkeys; ++k) { int32_t v; in.i32(v); c.keys.push_back(keyFromFile(v)); }
        uint32_t pad = 0; int32_t mouse = 0; uint8_t use = 0, shift = 0;
        if (!in.u32(pad) || !in.i32(mouse) || !in.u8(use) || !in.u8(shift)) return bad("truncated record tail");
        c.gamepadButton = padFromFile(pad); c.mouseButton = mouseFromFile(mouse);
        c.useGamepadButton = use != 0; c.requiresShift = shift != 0;
        out.push_back(std::move(c));
    }
    return true;
}

std::size_t applyLoadedControls(std::vector<ControlRecord>& scheme, const std::vector<ControlRecord>& loaded) {
    std::size_t n = 0;
    for (const auto& l : loaded) {
        for (auto& s : scheme) {
            if (s.name != l.name) continue; // memcmp after FNV-1a bucket lookup (0x180190338..0x1801903fa)
            s.keys = l.keys; s.gamepadButton = l.gamepadButton; s.mouseButton = l.mouseButton;
            s.useGamepadButton = l.useGamepadButton; s.requiresShift = l.requiresShift;
            ++n; break;
        }
    }
    return n;
}

bool shouldLoadSavedControls(const ConfigFile& c) {
    return c.getBool("LoadSavedControls").value_or(true) &&
           c.getInt("ControlsResetRevision").value_or(0) <= c.getInt("ControlsResetRevisionViewed").value_or(0);
}

void markControlsEdited(ConfigFile& c) {
    c.setInt("ControlsResetRevisionViewed", kKeyMappingScreenRevision);
    c.setBool("DefaultControlsChosen", true);
}

std::vector<ControlRecord> defaultControlRecords() {
    std::vector<ControlRecord> out;
    for (const auto& d : hades::input::defaultBindings()) {
        ControlRecord c; c.name = std::string(controlName(d.control));
        for (int32_t k : d.keys) if (k >= 0) c.keys.push_back(k);
        c.mouseButton = d.mouseButton;
        if (d.gamepadButton >= 0) { c.gamepadButton = d.gamepadButton; c.useGamepadButton = true; }
        // SetupScheme gives these R3/Select, then UseGamepadButton=0 and
        // DefaultGamepadButtonUnmapped=1 (0x18018d600..0x18018d69c).
        if (d.control == "ReportFeedback") c.gamepadButton = 59;
        if (d.control == "TakeScreenshot") c.gamepadButton = 45;
        out.push_back(std::move(c));
    }
    return out;
}

} // namespace hades::config
