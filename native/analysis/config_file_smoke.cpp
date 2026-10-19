// Host smoke for native/app/config_file: original Profile<N>.sjson text and
// Profile<N>.ctrls binary round trips (contract: config-file-contract.json).
#include "config_file.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace hades::config;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
    CHECK(optionSpecs().size() == 346);
    CHECK(profileConfigFileName() == "Profile1.sjson" && profileControlsFileName() == "Profile1.ctrls");

    ConfigFile cfg;
    CHECK(cfg.getFloat("Brightness") == 0.5f && cfg.getFloat("DeadZone") == 0.225f);
    CHECK(cfg.getInt("SwapInterval") == 1 && cfg.getBool("Letterbox") == true && cfg.getBool("EasyMode") == false);
    CHECK(findOption("drawbloom") && !findOption("DrawBloom")->persisted() && findOption("MasterVolume")->flags == kFieldMachineWide);
    CHECK(cfg.serialize() == "{\n}\n");
    CHECK(!cfg.setInt("Letterbox", 1) && !cfg.setBool("NoSuchOption", true));

    cfg.setFloat("MusicVolume", 0.5f); cfg.setBool("Letterbox", false); cfg.setString("Language", "en");
    cfg.setInt("SwapInterval", 0); cfg.setBool("DrawBloom", false);
    const std::string expect = "{\n  Language = \"en\"\n  Letterbox = false\n  MusicVolume = 0.500000\n  SwapInterval = 0\n}\n";
    CHECK(cfg.serialize() == expect);
    ConfigFile::WriteOptions all; all.includeNotSavedOptions = true;
    CHECK(cfg.serialize(all).find("  DrawBloom = false\n") != std::string::npos);

    const char* text =
        "\xEF\xBB\xBF{\n  // comment\n  mastervolume = 0.25,\n  \"Rumble\": false /* c */\n"
        "  SwapInterval = 2.9\n  DeadZone = \"x\"\n  EasyMode = 1\n  Language = \"en\"\n"
        "  ModSection = { A = 1 B = [1 2] }\n}\n";
    ConfigFile in;
    std::string err;
    CHECK(in.parse(text, &err));
    CHECK(in.getFloat("MasterVolume") == 0.25f && in.getBool("Rumble") == false);
    CHECK(in.getInt("SwapInterval") == 2 && in.getFloat("DeadZone") == 0.0f && in.getBool("EasyMode") == false);
    CHECK(in.getString("Language") == "en");
    ConfigFile esc; // JSON escapes, then sgg::Unescape "\\\\" -> "\\"
    CHECK(esc.parse("{ DirectLoad = \"a\\\\\\\\b\" }", &err) && esc.getString("DirectLoad") == "a\\b");
    CHECK(in.unknownKeys().size() == 1 && in.unknownKeys()[0].second == "{ A = 1 B = [1 2] }");
    ConfigFile again; CHECK(again.parse(in.serialize(), &err));
    CHECK(again.serialize() == in.serialize() && again.unknownKeys().size() == 1);
    CHECK(!ConfigFile().parse("MusicVolume = 1", &err)); // braced root required

    CHECK(keyToFile(65) == 65 && keyToFile(129) == 160 && keyToFile(0) == 27 && keyFromFile(27) == 0);
    CHECK(keyToFile(158) == 187 && keyFromFile(187) == 187); // original asymmetry
    CHECK(padToFile(50) == 0x1000 && padToFile(56) == 0x800000 && padToFile(-1) == 0xffffffffu && padFromFile(0xffffffffu) == kPadButtonUnmapped);
    CHECK(mouseToFile(0) == 0x100000 && mouseToFile(-1) == 0 && mouseFromFile(0x5000000) == 6 && mouseFromFile(0) == -1);

    auto scheme = defaultControlRecords();
    CHECK(scheme.size() == 29 && scheme[0].name == "MoveUp");
    auto edited = scheme;
    edited[7].keys = {129, 32}; edited[7].gamepadButton = 51; edited[4].mouseButton = 2; // Rush, Attack1
    auto bytes = writeControlsFile(edited);
    CHECK(bytes.size() > 8 && std::memcmp(bytes.data(), "SGB1", 4) == 0 && bytes[4] == 29);
    // First record: version 1, len 6 "MoveUp", 1 key W(87), pad Up mask 1, mouse none 0, use 1, shift 0.
    const uint8_t first[] = {1,0,0,0, 6,0,0,0, 'M','o','v','e','U','p', 1,0,0,0, 87,0,0,0, 1,0,0,0, 0,0,0,0, 1, 0};
    CHECK(std::memcmp(bytes.data() + 8, first, sizeof first) == 0);
    std::vector<ControlRecord> loaded;
    CHECK(readControlsFile(bytes.data(), bytes.size(), loaded, &err) && loaded.size() == 29);
    auto target = defaultControlRecords();
    CHECK(applyLoadedControls(target, loaded) == 29);
    CHECK(target[7].keys == std::vector<int32_t>({129, 32}) && target[7].gamepadButton == 51 && target[4].mouseButton == 2);
    CHECK(writeControlsFile(target) == bytes);
    CHECK(!readControlsFile(bytes.data(), bytes.size() - 1, loaded, &err));

    ConfigFile gate;
    CHECK(!shouldLoadSavedControls(gate)); // 16755 > 0 on a fresh profile
    markControlsEdited(gate);
    CHECK(shouldLoadSavedControls(gate));
    CHECK(gate.serialize() == "{\n  ControlsResetRevisionViewed = 36401\n  DefaultControlsChosen = true\n}\n");

    std::printf(failures ? "config_file_smoke: %d failure(s)\n" : "config_file_smoke: OK\n", failures);
    return failures ? 1 : 0;
}
