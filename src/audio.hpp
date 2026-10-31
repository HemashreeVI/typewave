// Audio output (miniaudio, low-latency) and custom .wav sound packs.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "engine.hpp"

namespace tw {

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    bool start(Engine& engine);  // opens the default output device
    void stop();
    bool running() const { return running_; }
    const std::string& error() const { return error_; }

private:
    struct Impl;
    Impl* impl_;
    bool running_ = false;
    std::string error_;
};

// Decode an audio file (wav) to mono float at kSampleRate. Empty on failure.
Sound loadSoundFile(const std::filesystem::path& file);

// Load every sub-folder of `dir` as a pack. See README.txt written by writePacksReadme().
// Problems are appended to `log` (one line each).
std::vector<Pack> loadCustomPacks(const std::filesystem::path& dir, std::string* log = nullptr);

void writePacksReadme(const std::filesystem::path& dir);

}  // namespace tw
