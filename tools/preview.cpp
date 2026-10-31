// Writes a short typing demo for every built-in pack to WAV files, so you can
// hear the packs without running the app:  typewave_preview [output-folder]
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/engine.hpp"

using namespace tw;

static void writeWav(const std::filesystem::path& file, const std::vector<float>& stereo) {
    std::ofstream out(file, std::ios::binary);
    const uint32_t frames = static_cast<uint32_t>(stereo.size() / 2), dataBytes = frames * 4;
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4); u32(36 + dataBytes); out.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(2); u32(kSampleRate); u32(kSampleRate * 4); u16(4); u16(16);
    out.write("data", 4); u32(dataBytes);
    for (float v : stereo) {
        const float c = v > 1 ? 1 : (v < -1 ? -1 : v);
        u16(static_cast<uint16_t>(static_cast<int16_t>(c * 32767.0f)));
    }
}

int main(int argc, char** argv) {
    const std::filesystem::path dir = argc > 1 ? argv[1] : "previews";
    std::filesystem::create_directories(dir);

    // "hello world" + Enter, typed at a natural, slightly uneven pace
    struct Step { float at; unsigned vk; float hold; };
    std::vector<Step> steps;
    const std::string text = "HELLO WORLD";
    float t = 0.15f;
    Rng rng(7);
    for (char c : text) {
        steps.push_back({t, c == ' ' ? vk::Space : static_cast<unsigned>(c), rng.range(0.06f, 0.1f)});
        t += rng.range(0.1f, 0.17f);
    }
    steps.push_back({t + 0.12f, vk::Back, 0.08f});
    steps.push_back({t + 0.4f, 'D', 0.08f});
    steps.push_back({t + 0.75f, vk::Return, 0.1f});
    const float total = t + 2.0f;

    for (int s = 0; s < kStyleCount; ++s) {
        Engine e;
        e.addPack(buildPack(static_cast<Style>(s)));
        e.volume = 0.8f;
        struct Ev { float at; KeyEvent e; };
        std::vector<Ev> evs;
        for (const Step& st : steps) {
            evs.push_back({st.at, {classify(st.vk), false, panFor(st.vk)}});
            evs.push_back({st.at + st.hold, {classify(st.vk), true, panFor(st.vk)}});
        }
        std::vector<float> out;
        const uint32_t block = 64;
        std::vector<float> buf(block * 2);
        for (uint32_t f = 0; f < static_cast<uint32_t>(total * kSampleRate); f += block) {
            const float now = static_cast<float>(f) / kSampleRate;
            for (const Ev& ev : evs)
                if (ev.at >= now && ev.at < now + static_cast<float>(block) / kSampleRate) e.post(ev.e);
            e.render(buf.data(), block);
            out.insert(out.end(), buf.begin(), buf.end());
        }
        std::string name = styleName(static_cast<Style>(s));
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        writeWav(dir / (name + ".wav"), out);
        std::printf("wrote %s\n", (dir / (name + ".wav")).string().c_str());
    }
    return 0;
}
