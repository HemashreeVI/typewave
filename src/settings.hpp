// Settings file (plain "key = value" lines). Only preferences are stored here,
// never anything about what you type.
#pragma once
#include <filesystem>
#include <string>

namespace tw {

struct Settings {
    bool enabled = true;
    std::string pack = "Thock";
    float volume = 0.6f;  // 0 .. 1
    bool releaseSounds = true;
    bool stereo = true;

    bool load(const std::filesystem::path& file);  // false if missing/unreadable (defaults kept)
    bool save(const std::filesystem::path& file) const;
};

}  // namespace tw
