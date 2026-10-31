#include "settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace tw {

namespace {
std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
bool parseBool(const std::string& v, bool fallback) {
    if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "off" || v == "no") return false;
    return fallback;
}
}  // namespace

bool Settings::load(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "enabled") enabled = parseBool(value, enabled);
        else if (key == "pack" && !value.empty()) pack = value;
        else if (key == "volume") {
            char* end = nullptr;
            const float v = std::strtof(value.c_str(), &end);
            if (end != value.c_str()) volume = std::clamp(v, 0.0f, 1.0f);
        }
        else if (key == "release_sounds") releaseSounds = parseBool(value, releaseSounds);
        else if (key == "stereo") stereo = parseBool(value, stereo);
    }
    return true;
}

bool Settings::save(const std::filesystem::path& file) const {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    if (!out) return false;
    out << "# TypeWave settings. Only preferences live here, never what you type.\n"
        << "enabled = " << (enabled ? "true" : "false") << "\n"
        << "pack = " << pack << "\n"
        << "volume = " << volume << "\n"
        << "release_sounds = " << (releaseSounds ? "true" : "false") << "\n"
        << "stereo = " << (stereo ? "true" : "false") << "\n";
    return static_cast<bool>(out);
}

}  // namespace tw
