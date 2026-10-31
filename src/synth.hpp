// Procedural key-switch sounds. Every sound is synthesised at start-up from a
// small "recipe" of damped resonances and filtered noise bursts, so TypeWave
// ships with no audio files and every press can get its own variation.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "keys.hpp"

namespace tw {

constexpr int kSampleRate = 48000;

enum class Style : uint8_t { Clicky = 0, Tactile, Thock, Typewriter, Bubble, Count };
constexpr int kStyleCount = static_cast<int>(Style::Count);

const char* styleName(Style s);         // "Clicky", ...
const char* styleBlurb(Style s);        // one line for menus / README

// One building block of a sound.
struct Partial {
    enum Type : uint8_t { Sine, NoiseLow, NoiseHigh, Sweep } type = Sine;
    float delay = 0;   // seconds after the key event
    float freq = 0;    // Hz (Sine/Sweep start) or filter cutoff (noise)
    float freq2 = 0;   // Sweep end frequency
    float tau = 0.01f; // decay time constant in seconds
    float amp = 0;     // linear amplitude before normalisation
    float am = 0;      // amplitude modulation rate in Hz (rattle), 0 = none
};

struct Recipe {
    std::vector<Partial> parts;
    float peak = 0.9f;  // target peak after normalisation
};

// Recipe for a style, key kind and press/release, with seeded variation.
Recipe makeRecipe(Style style, KeyKind kind, bool release, uint32_t seed);

// Render a recipe to mono float samples at kSampleRate.
std::vector<float> render(const Recipe& r, uint32_t seed);

// Small deterministic PRNG shared by the synth and the mixer.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() { return static_cast<float>(next() >> 8) / 16777216.0f; }   // [0,1)
    float range(float a, float b) { return a + (b - a) * uniform(); }
};

}  // namespace tw
