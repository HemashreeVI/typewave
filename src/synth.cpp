#include "synth.hpp"

#include <algorithm>
#include <cmath>

namespace tw {

namespace {
constexpr float kPi = 3.14159265358979f;

Partial sine(float delay, float freq, float tau, float amp) {
    Partial p; p.type = Partial::Sine; p.delay = delay; p.freq = freq; p.tau = tau; p.amp = amp; return p;
}
Partial noiseLo(float delay, float cutoff, float tau, float amp) {
    Partial p; p.type = Partial::NoiseLow; p.delay = delay; p.freq = cutoff; p.tau = tau; p.amp = amp; return p;
}
Partial noiseHi(float delay, float cutoff, float tau, float amp) {
    Partial p; p.type = Partial::NoiseHigh; p.delay = delay; p.freq = cutoff; p.tau = tau; p.amp = amp; return p;
}
Partial sweep(float delay, float f1, float f2, float tau, float amp) {
    Partial p; p.type = Partial::Sweep; p.delay = delay; p.freq = f1; p.freq2 = f2; p.tau = tau; p.amp = amp; return p;
}

// --- one function per switch style ------------------------------------------------

Recipe clicky(bool release) {
    Recipe r;
    if (release) {
        r.parts = {noiseHi(0, 3500, 0.0015f, 0.5f), sine(0, 2600, 0.004f, 0.45f), sine(0, 900, 0.006f, 0.15f)};
        r.peak = 0.5f;
    } else {
        // the click jacket snaps, then the keycap bottoms out ~9 ms later
        r.parts = {noiseHi(0, 4000, 0.0018f, 1.0f), sine(0, 3200, 0.003f, 0.6f),
                   sine(0.009f, 330, 0.018f, 0.5f), sine(0.009f, 1150, 0.006f, 0.3f),
                   noiseLo(0.009f, 2500, 0.003f, 0.45f)};
    }
    return r;
}

Recipe tactile(bool release) {
    Recipe r;
    if (release) {
        r.parts = {sine(0, 1400, 0.004f, 0.3f), noiseLo(0, 3000, 0.002f, 0.25f), sine(0, 520, 0.006f, 0.12f)};
        r.peak = 0.42f;
    } else {
        r.parts = {noiseLo(0, 1800, 0.002f, 0.35f),
                   sine(0.007f, 260, 0.022f, 0.6f), sine(0.007f, 900, 0.008f, 0.3f),
                   noiseLo(0.007f, 2200, 0.0035f, 0.5f)};
        r.peak = 0.85f;
    }
    return r;
}

Recipe thock(bool release) {
    Recipe r;
    if (release) {
        r.parts = {sine(0, 600, 0.006f, 0.25f), noiseLo(0, 1800, 0.0025f, 0.2f)};
        r.peak = 0.38f;
    } else {
        r.parts = {sine(0, 170, 0.030f, 0.8f), sine(0, 420, 0.014f, 0.4f), sine(0, 1250, 0.004f, 0.12f),
                   noiseLo(0, 1600, 0.003f, 0.45f)};
        r.peak = 0.9f;
    }
    return r;
}

Recipe typewriter(KeyKind kind, bool release) {
    Recipe r;
    if (release) {
        r.parts = {sine(0, 1800, 0.004f, 0.2f), noiseHi(0, 3000, 0.001f, 0.15f)};
        r.peak = 0.3f;
        return r;
    }
    // type bar strikes the platen: bright metallic ring + a low thud
    r.parts = {noiseHi(0, 3000, 0.004f, 1.0f), sine(0, 2300, 0.025f, 0.35f), sine(0, 3700, 0.015f, 0.25f),
               sine(0, 5100, 0.010f, 0.15f), sine(0.002f, 120, 0.020f, 0.5f)};
    if (kind == KeyKind::Enter) {
        // carriage-return bell and ratchet
        r.parts.push_back(sine(0.06f, 2093, 0.40f, 0.55f));
        r.parts.push_back(sine(0.06f, 4186, 0.20f, 0.15f));
        for (int i = 0; i < 4; ++i) r.parts.push_back(noiseHi(0.20f + 0.035f * i, 2500, 0.004f, 0.35f));
    } else if (kind == KeyKind::Space) {
        for (int i = 0; i < 3; ++i) r.parts.push_back(noiseHi(0.012f * (i + 1), 2200, 0.003f, 0.3f));
    }
    return r;
}

Recipe bubble(KeyKind kind, bool release) {
    Recipe r;
    if (release) {
        r.parts = {sweep(0, 1500, 2200, 0.012f, 0.3f)};
        r.peak = 0.3f;
        return r;
    }
    if (kind == KeyKind::Enter) {
        r.parts = {sweep(0, 420, 900, 0.035f, 0.9f), sweep(0.07f, 600, 1300, 0.035f, 0.8f)};
    } else {
        r.parts = {sweep(0, 500, 1100, 0.03f, 1.0f)};
    }
    r.peak = 0.75f;
    return r;
}
}  // namespace

const char* styleName(Style s) {
    switch (s) {
        case Style::Clicky: return "Clicky";
        case Style::Tactile: return "Tactile";
        case Style::Thock: return "Thock";
        case Style::Typewriter: return "Typewriter";
        case Style::Bubble: return "Bubble";
        default: return "?";
    }
}

const char* styleBlurb(Style s) {
    switch (s) {
        case Style::Clicky: return "Sharp click, then bottom-out (like blue switches)";
        case Style::Tactile: return "Soft bump and a muted clack (like brown switches)";
        case Style::Thock: return "Deep, smooth thock (like lubed linear switches)";
        case Style::Typewriter: return "Metal type bars, with a bell on Enter";
        case Style::Bubble: return "Playful bubble pops";
        default: return "";
    }
}

Recipe makeRecipe(Style style, KeyKind kind, bool release, uint32_t seed) {
    Recipe r;
    switch (style) {
        case Style::Clicky: r = clicky(release); break;
        case Style::Tactile: r = tactile(release); break;
        case Style::Thock: r = thock(release); break;
        case Style::Typewriter: r = typewriter(kind, release); break;
        case Style::Bubble: r = bubble(kind, release); break;
        default: r = thock(release); break;
    }

    // Bigger keys sound lower, longer and louder; space bars rattle (stabilisers).
    float fMul = 1, tMul = 1, aMul = 1;
    bool keepPitch = style == Style::Typewriter;  // its special keys are already distinct
    switch (kind) {
        case KeyKind::Space: fMul = 0.74f; tMul = 1.45f; aMul = 1.1f; break;
        case KeyKind::Enter: fMul = 0.86f; tMul = 1.25f; aMul = 1.08f; break;
        case KeyKind::Backspace: fMul = 0.92f; tMul = 1.12f; aMul = 1.02f; break;
        case KeyKind::Modifier: fMul = 0.95f; tMul = 1.05f; aMul = 0.85f; break;
        default: break;
    }
    if (keepPitch) fMul = 1;
    if (kind == KeyKind::Space && style != Style::Bubble && style != Style::Typewriter && !release) {
        Partial rattle = noiseLo(0.004f, 2500, 0.035f, 0.14f);
        rattle.am = 55;
        r.parts.push_back(rattle);
    }

    // Seeded variation: no two presses are exactly alike.
    Rng rng(seed * 2654435761u + static_cast<uint32_t>(style) * 97u + static_cast<uint32_t>(kind) * 13u +
            (release ? 7u : 0u));
    float delayJitter = rng.range(-0.001f, 0.001f);
    for (Partial& p : r.parts) {
        float v = rng.range(-0.06f, 0.06f);
        p.freq *= fMul * (1 + v);
        p.freq2 *= fMul * (1 + v);
        p.tau *= tMul * (1 + rng.range(-0.1f, 0.1f));
        p.amp *= 1 + rng.range(-0.12f, 0.12f);
        if (p.delay > 0) p.delay = std::max(0.0f, p.delay + delayJitter);
    }
    r.peak = std::min(0.98f, r.peak * aMul);
    return r;
}

std::vector<float> render(const Recipe& r, uint32_t seed) {
    float end = 0.02f;
    for (const Partial& p : r.parts) end = std::max(end, p.delay + p.tau * 7.0f);
    end = std::min(end + 0.005f, 1.2f);
    const int n = static_cast<int>(end * kSampleRate);
    std::vector<float> out(static_cast<size_t>(n), 0.0f);

    Rng rng(seed ^ 0xA5A5A5A5u);
    const float dt = 1.0f / kSampleRate;
    const float attack = 0.0003f;  // 0.3 ms: avoids a digital click at the onset
    for (const Partial& p : r.parts) {
        const int start = static_cast<int>(p.delay * kSampleRate);
        const float lpA = 1.0f - std::exp(-2.0f * kPi * std::max(50.0f, p.freq) * dt);
        float lp = 0, phase = rng.range(0, 2 * kPi);
        for (int i = start; i < n; ++i) {
            const float t = (i - start) * dt;
            float env = std::exp(-t / p.tau);
            if (env < 0.0005f) break;
            if (t < attack) env *= t / attack;
            if (p.am > 0) env *= 0.55f + 0.45f * std::sin(2 * kPi * p.am * t);
            float s = 0;
            switch (p.type) {
                case Partial::Sine:
                    s = std::sin(phase + 2 * kPi * p.freq * t);
                    break;
                case Partial::Sweep: {
                    // exponential glide from freq to freq2 over ~2 tau
                    const float k = std::min(1.0f, t / (2 * p.tau));
                    const float f = p.freq * std::pow(p.freq2 / p.freq, k);
                    phase += 2 * kPi * f * dt;
                    s = std::sin(phase);
                    break;
                }
                case Partial::NoiseLow:
                case Partial::NoiseHigh: {
                    const float w = rng.range(-1, 1);
                    lp += lpA * (w - lp);
                    s = p.type == Partial::NoiseLow ? lp : (w - lp);
                    break;
                }
            }
            out[static_cast<size_t>(i)] += p.amp * env * s;
        }
    }

    float peak = 0;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    if (peak > 1e-6f) {
        const float g = r.peak / peak;
        for (float& v : out) v *= g;
    }
    // short fade at the very end
    const int fade = std::min(n, kSampleRate / 500);
    for (int i = 0; i < fade; ++i) out[static_cast<size_t>(n - 1 - i)] *= static_cast<float>(i) / fade;
    return out;
}

}  // namespace tw
