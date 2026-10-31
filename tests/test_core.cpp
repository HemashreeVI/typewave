// Tests for the platform-independent core (run on Windows, Linux and macOS).
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/engine.hpp"
#include "../src/keys.hpp"
#include "../src/settings.hpp"
#include "../src/synth.hpp"

using namespace tw;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++checks;                                                                \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                        \
    } while (0)

static float peakOf(const std::vector<float>& s) {
    float p = 0;
    for (float v : s) p = std::max(p, std::fabs(v));
    return p;
}
static bool finite(const std::vector<float>& s) {
    for (float v : s) if (!std::isfinite(v)) return false;
    return true;
}

static void testKeys() {
    CHECK(classify(vk::Space) == KeyKind::Space);
    CHECK(classify(vk::Return) == KeyKind::Enter);
    CHECK(classify(vk::Back) == KeyKind::Backspace);
    CHECK(classify(vk::Delete) == KeyKind::Backspace);
    CHECK(classify(vk::LShift) == KeyKind::Modifier);
    CHECK(classify(vk::Tab) == KeyKind::Modifier);
    CHECK(classify('A') == KeyKind::Normal);
    CHECK(classify('7') == KeyKind::Normal);
    CHECK(panFor('Q') < -0.5f);
    CHECK(panFor('P') > 0.5f);
    CHECK(panFor('A') < panFor('L'));
    CHECK(std::fabs(panFor(vk::Space)) < 1e-6f);
    for (unsigned c = 0; c < 256; ++c) CHECK(panFor(c) >= -1.0f && panFor(c) <= 1.0f);
}

static void testSynth() {
    for (int s = 0; s < kStyleCount; ++s) {
        for (int k = 0; k < kKindCount; ++k) {
            for (int rel = 0; rel < 2; ++rel) {
                Recipe r = makeRecipe(static_cast<Style>(s), static_cast<KeyKind>(k), rel == 1, 42);
                std::vector<float> snd = render(r, 42);
                CHECK(!snd.empty());
                CHECK(finite(snd));
                CHECK(peakOf(snd) > 0.2f);    // audible
                CHECK(peakOf(snd) <= 0.99f);  // never clips
                CHECK(snd.size() <= static_cast<size_t>(kSampleRate * 1.25));
                // starts quickly: something audible within the first 15 ms
                float early = 0;
                for (size_t i = 0; i < snd.size() && i < static_cast<size_t>(kSampleRate * 0.015); ++i)
                    early = std::max(early, std::fabs(snd[i]));
                CHECK(early > 0.05f);
                // ends quietly (no click when the sound stops)
                CHECK(std::fabs(snd.back()) < 0.01f);
            }
        }
    }
    // variation: different seeds give different sounds, same seed is repeatable
    auto a = render(makeRecipe(Style::Thock, KeyKind::Normal, false, 1), 1);
    auto b = render(makeRecipe(Style::Thock, KeyKind::Normal, false, 2), 2);
    auto a2 = render(makeRecipe(Style::Thock, KeyKind::Normal, false, 1), 1);
    CHECK(a != b);
    CHECK(a == a2);
    // space bar is longer than a normal key (bigger key, stabiliser rattle)
    auto space = render(makeRecipe(Style::Thock, KeyKind::Space, false, 1), 1);
    CHECK(space.size() > a.size());
}

static void testEngine() {
    Engine e;
    e.addPack(buildPack(Style::Clicky, 3));
    e.addPack(buildPack(Style::Thock, 3));
    CHECK(e.packCount() == 2);
    CHECK(e.findPack("Thock") == 1);
    CHECK(e.findPack("nope") == -1);
    for (int k = 0; k < kKindCount; ++k) CHECK(e.pack(0).pick(static_cast<KeyKind>(k), false) != nullptr);

    std::vector<float> buf(2 * 512);
    // silence when nothing was pressed
    e.render(buf.data(), 512);
    CHECK(peakOf(buf) == 0.0f);

    // fast typing: many overlapping sounds, none cut off
    for (int i = 0; i < 10; ++i) CHECK(e.post({KeyKind::Normal, false, 0}));
    e.render(buf.data(), 512);
    CHECK(e.activeVoices() == 10);
    CHECK(peakOf(buf) > 0.0f);
    CHECK(peakOf(buf) <= 1.0f);  // limiter keeps the sum in range
    CHECK(finite(buf));

    // voices finish on their own
    for (int i = 0; i < 200; ++i) e.render(buf.data(), 512);
    CHECK(e.activeVoices() == 0);

    // more presses than voices: oldest are stolen, never crashes
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 40; ++i) e.post({KeyKind::Space, false, 0.3f});
        e.render(buf.data(), 64);
    }
    CHECK(e.activeVoices() <= Engine::kMaxVoices);

    // muted: presses are ignored
    for (int i = 0; i < 400; ++i) e.render(buf.data(), 512);
    e.enabled = false;
    e.post({KeyKind::Normal, false, 0});
    e.render(buf.data(), 512);
    CHECK(e.activeVoices() == 0);
    CHECK(peakOf(buf) == 0.0f);

    // release sounds can be turned off
    e.enabled = true;
    e.releaseSounds = false;
    e.post({KeyKind::Normal, true, 0});
    e.render(buf.data(), 512);
    CHECK(e.activeVoices() == 0);

    // stereo: a left-hand key is louder on the left
    e.releaseSounds = true;
    e.stereo = true;
    e.post({KeyKind::Normal, false, -1.0f});
    e.render(buf.data(), 512);
    float l = 0, r = 0;
    for (size_t i = 0; i < 512; ++i) { l += std::fabs(buf[2 * i]); r += std::fabs(buf[2 * i + 1]); }
    CHECK(l > r * 1.5f);

    // queue never blocks: a burst bigger than the queue just drops extras
    int accepted = 0;
    for (int i = 0; i < 1000; ++i) accepted += e.post({KeyKind::Normal, false, 0});
    CHECK(accepted == 256);
    e.render(buf.data(), 512);
    CHECK(e.activeVoices() <= Engine::kMaxVoices);
}

static void testSettings() {
    namespace fs = std::filesystem;
    const fs::path file = fs::temp_directory_path() / "typewave-test" / "settings.ini";
    fs::remove_all(file.parent_path());

    Settings missing;
    CHECK(!missing.load(file));
    CHECK(missing.pack == "Thock");

    Settings s;
    s.enabled = false;
    s.pack = "My Keyboard";
    s.volume = 0.25f;
    s.releaseSounds = false;
    s.stereo = false;
    CHECK(s.save(file));

    Settings t;
    CHECK(t.load(file));
    CHECK(!t.enabled);
    CHECK(t.pack == "My Keyboard");
    CHECK(std::fabs(t.volume - 0.25f) < 1e-4f);
    CHECK(!t.releaseSounds);
    CHECK(!t.stereo);

    // junk and out-of-range values don't break anything
    {
        std::ofstream out(file);
        out << "garbage line\nvolume = 7\nenabled = maybe\n# comment\npack =\n";
    }
    Settings u;
    CHECK(u.load(file));
    CHECK(u.volume == 1.0f);
    CHECK(u.enabled);
    CHECK(u.pack == "Thock");
    fs::remove_all(file.parent_path());
}

int main() {
    testKeys();
    testSynth();
    testEngine();
    testSettings();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
