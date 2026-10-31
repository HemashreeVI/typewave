#ifndef NOMINMAX
#define NOMINMAX  // windows.h (pulled in by miniaudio) must not define min/max macros
#endif
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio/miniaudio.h"

#include "audio.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>

namespace tw {

struct AudioOutput::Impl {
    ma_device device{};
    bool initialised = false;
};

namespace {
void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* engine = static_cast<Engine*>(device->pUserData);
    engine->render(static_cast<float*>(output), frames);
}
}  // namespace

AudioOutput::AudioOutput() : impl_(new Impl) {}
AudioOutput::~AudioOutput() {
    stop();
    delete impl_;
}

bool AudioOutput::start(Engine& engine) {
    stop();
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = kSampleRate;
    cfg.dataCallback = dataCallback;
    cfg.pUserData = &engine;
    cfg.performanceProfile = ma_performance_profile_low_latency;
    cfg.periodSizeInMilliseconds = 10;  // key-to-sound delay stays around 10-20 ms
    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
        error_ = "Couldn't open the speakers/headphones (no audio output device?).";
        return false;
    }
    impl_->initialised = true;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        error_ = "Couldn't start audio playback.";
        stop();
        return false;
    }
    running_ = true;
    return true;
}

void AudioOutput::stop() {
    if (impl_->initialised) {
        ma_device_uninit(&impl_->device);
        impl_->initialised = false;
    }
    running_ = false;
}

Sound loadSoundFile(const std::filesystem::path& file) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, kSampleRate);
    ma_decoder dec;
#ifdef _WIN32
    if (ma_decoder_init_file_w(file.wstring().c_str(), &cfg, &dec) != MA_SUCCESS) return {};
#else
    if (ma_decoder_init_file(file.string().c_str(), &cfg, &dec) != MA_SUCCESS) return {};
#endif
    Sound s;
    float chunk[4096];
    const size_t maxFrames = static_cast<size_t>(kSampleRate) * 2;  // 2 s is plenty for a key
    for (;;) {
        ma_uint64 got = 0;
        if (ma_decoder_read_pcm_frames(&dec, chunk, 4096, &got) != MA_SUCCESS || got == 0) break;
        s.insert(s.end(), chunk, chunk + got);
        if (s.size() >= maxFrames) { s.resize(maxFrames); break; }
    }
    ma_decoder_uninit(&dec);

    // trim leading silence so the sound starts right on the key press
    size_t first = 0;
    while (first < s.size() && std::fabs(s[first]) < 0.01f) ++first;
    if (first > 0 && first < s.size()) s.erase(s.begin(), s.begin() + static_cast<long>(first));
    float peak = 0;
    for (float v : s) peak = std::max(peak, std::fabs(v));
    if (peak > 0.95f) for (float& v : s) v *= 0.9f / peak;
    return s;
}

std::vector<Pack> loadCustomPacks(const std::filesystem::path& dir, std::string* log) {
    namespace fs = std::filesystem;
    std::vector<Pack> packs;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return packs;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_directory()) continue;
        Pack p;
        p.name = entry.path().filename().u8string().c_str();
        p.blurb = "Your sound pack";
        p.custom = true;
        for (const auto& f : fs::directory_iterator(entry.path(), ec)) {
            if (!f.is_regular_file()) continue;
            std::string stem = f.path().stem().u8string().c_str();
            std::string ext = f.path().extension().u8string().c_str();
            std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return std::tolower(c); });
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            if (ext != ".wav") continue;
            // name: <kind>[_up][number], e.g. key.wav, key2.wav, space_up.wav
            while (!stem.empty() && std::isdigit(static_cast<unsigned char>(stem.back()))) stem.pop_back();
            bool release = false;
            if (stem.size() > 3 && stem.compare(stem.size() - 3, 3, "_up") == 0) {
                release = true;
                stem.resize(stem.size() - 3);
            }
            int kind = -1;
            for (int k = 0; k < kKindCount; ++k)
                if (stem == kindName(static_cast<KeyKind>(k))) kind = k;
            if (kind < 0) {
                if (log) *log += "Skipped " + f.path().filename().u8string() + " in " + p.name + " (unknown name)\n";
                continue;
            }
            Sound s = loadSoundFile(f.path());
            if (s.size() < 2) {
                if (log) *log += "Couldn't read " + f.path().filename().u8string() + " in " + p.name + "\n";
                continue;
            }
            p.variants[static_cast<size_t>(kind)][release ? 1 : 0].push_back(std::move(s));
        }
        if (p.empty()) {
            if (log) *log += "Pack \"" + p.name + "\" has no key.wav, so it was skipped\n";
            continue;
        }
        packs.push_back(std::move(p));
    }
    std::sort(packs.begin(), packs.end(), [](const Pack& a, const Pack& b) { return a.name < b.name; });
    return packs;
}

void writePacksReadme(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / "README.txt";
    if (std::filesystem::exists(file, ec)) return;
    std::ofstream out(file);
    out << "TypeWave custom sound packs\r\n"
           "===========================\r\n\r\n"
           "Make one folder per pack inside this folder. The folder name is the pack name.\r\n"
           "Put .wav files in it with these names:\r\n\r\n"
           "  key.wav        any normal key (required)\r\n"
           "  space.wav      space bar\r\n"
           "  enter.wav      Enter\r\n"
           "  backspace.wav  Backspace and Delete\r\n"
           "  modifier.wav   Shift, Ctrl, Alt, Tab, Caps Lock, Esc, Windows key\r\n\r\n"
           "Add _up for the sound when the key is released (key_up.wav, space_up.wav, ...).\r\n"
           "Add numbers for variations TypeWave picks between at random (key1.wav, key2.wav, ...).\r\n"
           "Missing sounds fall back to key.wav. Then choose \"Reload sound packs\" in the tray menu.\r\n\r\n"
           "Example:\r\n"
           "  packs\\My Keyboard\\key1.wav\r\n"
           "  packs\\My Keyboard\\key2.wav\r\n"
           "  packs\\My Keyboard\\key_up.wav\r\n"
           "  packs\\My Keyboard\\space.wav\r\n\r\n"
           "Tip: record your own keyboard, or use sounds you have the rights to (for example CC0).\r\n";
}

}  // namespace tw
