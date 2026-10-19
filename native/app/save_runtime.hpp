#pragma once
// Original Hades save container (sgg::BinarySaveData / BinaryLoadData,
// ProgressManager::GatherSaveData / Restore / LoadLua). Recovered from
// EngineWin64s.dll; evidence and addresses: native/analysis/save-contract.json.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hades::save {

inline constexpr uint32_t kMagic=0x31424753;            // "SGB1" little-endian (PrepareForWrite)
inline constexpr int32_t kVersion=16;                   // GatherSaveData writes 0x10
inline constexpr int32_t kOldestVersion=1;              // Load accepts 1..16
inline constexpr int32_t kRevision=0x8e31;              // GatherLuaState: _G.Revision = 36401
inline constexpr size_t kLuaStateLimit=0x8f4000;        // COMPRESS_BUFFER capacity
inline constexpr size_t kCompressedLuaLimit=0x2fc000;   // GatherLuaState compressed bound
inline constexpr size_t kGatherHeaderLimit=0x4000;      // Lua state gathered only if header <= this
inline constexpr const char* kSaveExtension=".sav";            // SAVE_EXTENSION
inline constexpr const char* kValidTempExtension=".v.sav";     // VALID_TEMP_SAVE_EXTENSION
inline constexpr const char* kBackupSuffix=".bak";             // PCSaveDevice::BackupPath appends ".bak"

// ProfileManager::SaveInfo fields written by GatherSaveData in this order.
struct SaveInfo {
    uint64_t timestamp=0;                 // _time64(nullptr), low 32 bits zero-extended
    std::string location;                 // SaveInfo+0x98 (GameState.LocationName)
    int32_t completedRuns=0;              // SaveInfo+0x74 (int)GameState.CompletedRunsCache
    int32_t accumulatedMetaPoints=0;      // SaveInfo+0x78 (int)GameState.AccumulatedMetaPointsCache
    int32_t spentShrinePoints=0;          // SaveInfo+0x7c (int)GameState.SpentShrinePointsCache
    bool easyMode=false;                  // SaveInfo+0x70 ConfigOptions::EasyMode
    bool hardMode=false;                  // SaveInfo+0x71 GameState.Flags.HardMode
    std::vector<std::string> notable;     // SaveInfo+0xb0 (SaveNotableLuaInfo list)
};

struct SaveFile {
    int32_t version=0;
    SaveInfo info;
    std::string currentMap;   // World map name at save time
    std::string nextMap;      // Save(..., startNextMap) argument
    std::string luaBlock;     // compressed (version > 14) or raw luabins bytes
    // Restore: next map is nextMap when non-empty, else currentMap.
    const std::string& restoreMap()const{return nextMap.empty()?currentMap:nextMap;}
};

// sgg::adler32(data, offset, length): standard Adler-32, initial a=1 b=0.
uint32_t adler32(const uint8_t* data,size_t length);

// Original layout: "SGB1", adler32(payload), payload. payload =
// i32 version, u64 timestamp, str location, i32 x3, u8 x2, u32 n + n str,
// str currentMap, str nextMap, then (header <= 0x4000) u32 n + LZ4 block.
bool encodeSave(const SaveInfo&,const std::string& currentMap,const std::string& nextMap,const std::string& luabinsState,std::vector<uint8_t>& out,std::string& error);
bool decodeSave(const std::vector<uint8_t>& bytes,bool verifyChecksum,SaveFile& out,std::string& error);
// ProgressManager::LoadLua: LZ4_decompress_safe into the 0x8f4000 buffer for
// version > 14; earlier versions stored the luabins string directly.
bool luabinsState(const SaveFile&,std::string& out,std::string& error);

// SaveValidCheckpoint: BinarySaveData(8, no checksum) => "SGB1", i32 16, u8 valid.
std::vector<uint8_t> encodeValidCheckpoint(bool valid);
bool decodeValidCheckpoint(const std::vector<uint8_t>&,bool& valid,std::string& error);

// PCSaveDevice::Save default path (SaveToTempAndMove=false): copy an existing
// file to "<name>.bak", then write the stream directly.
bool writeSaveFile(const std::string& path,const std::vector<uint8_t>& bytes,bool backup,std::string& error);
bool readSaveFile(const std::string& path,std::vector<uint8_t>& bytes,std::string& error);
bool fileExists(const std::string& path);

}
