// Minimal check: the native default-binding table equals default-bindings-contract.json.
#include "input_bindings.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int intAfter(const std::string& s, std::size_t from, std::size_t to, const char* key) {
    const auto p = s.find(key, from);
    if (p == std::string::npos || p >= to) return -2;
    const auto v = p + std::string(key).size();
    return s.compare(v, 4, "null") == 0 ? -1 : std::stoi(s.substr(v));
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "native/analysis/default-bindings-contract.json";
    std::ifstream in(path);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", path); return 2; }
    std::stringstream ss; ss << in.rdbuf(); const std::string j = ss.str();
    int failures = 0;
    for (const auto& b : hades::input::defaultBindings()) {
        const auto start = j.find("\n  \"" + std::string(b.control) + "\": {");
        const auto end = start == std::string::npos ? start : j.find("\"provenance\"", start);
        if (start == std::string::npos || end == std::string::npos) { std::printf("MISSING %s\n", b.control.data()); ++failures; continue; }
        const int pad = intAfter(j, start, end, "\"button\": ");
        const auto kb = j.find("\"keyboard\"", start), ms = j.find("\"mouse\"", start);
        std::vector<int> keys;
        for (auto p = j.find("\"value\": ", kb); p != std::string::npos && p < ms; p = j.find("\"value\": ", p + 1))
            keys.push_back(std::stoi(j.substr(p + 9)));
        const int mouse = j.compare(ms + 9, 4, "null") == 0 ? -1 : intAfter(j, ms, end, "\"value\": ");
        std::vector<int> mine;
        for (int k : b.keys) if (k >= 0) mine.push_back(k);
        if (pad != b.gamepadButton || keys != mine || mouse != b.mouseButton) {
            std::printf("MISMATCH %s pad %d/%d mouse %d/%d\n", b.control.data(), pad, b.gamepadButton, mouse, b.mouseButton);
            ++failures;
        }
    }
    std::printf(failures ? "INPUT_BINDINGS_FAIL %d\n" : "INPUT_BINDINGS_CONTRACT_OK\n", failures);
    return failures ? 1 : 0;
}
