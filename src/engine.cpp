#include "engine.hpp"

#include <algorithm>
#include <cmath>

namespace tw {

const std::vector<Sound>* Pack::pick(KeyKind kind, bool release) const {
    const auto& own = variants[static_cast<int>(kind)][release ? 1 : 0];
    if (!own.empty()) return &own;
    const auto& normal = variants[static_cast<int>(KeyKind::Normal)][release ? 1 : 0];
    if (!normal.empty()) return &normal;
    return nullptr;
}

bool Pack::empty() const { return pick(KeyKind::Normal, false) == nullptr; }

Pack buildPack(Style style, int variantsPerSound) {
    Pack p;
    p.name = styleName(style);
    p.blurb = styleBlurb(style);
    for (int k = 0; k < kKindCount; ++k) {
        for (int rel = 0; rel < 2; ++rel) {
            auto& list = p.variants[static_cast<size_t>(k)][static_cast<size_t>(rel)];
            for (int v = 0; v < variantsPerSound; ++v) {
                const uint32_t seed = 1000u + static_cast<uint32_t>(v) * 7919u;
                Recipe r = makeRecipe(style, static_cast<KeyKind>(k), rel == 1, seed);
                list.push_back(render(r, seed + static_cast<uint32_t>(k) * 31u + static_cast<uint32_t>(rel)));
            }
        }
    }
    return p;
}

Engine::Engine() = default;

void Engine::addPack(Pack p) { packs_.push_back(std::move(p)); }

void Engine::removeCustomPacks() {
    for (auto& v : voices_) v.sound = nullptr;  // never point into a freed pack
    packs_.erase(std::remove_if(packs_.begin(), packs_.end(), [](const Pack& p) { return p.custom; }),
                 packs_.end());
    if (current.load() >= static_cast<int>(packs_.size())) current = 0;
}

int Engine::findPack(const std::string& name) const {
    for (size_t i = 0; i < packs_.size(); ++i)
        if (packs_[i].name == name) return static_cast<int>(i);
    return -1;
}

void Engine::trigger(const KeyEvent& e) {
    const int idx = current.load(std::memory_order_relaxed);
    if (idx < 0 || idx >= static_cast<int>(packs_.size())) return;
    const std::vector<Sound>* list = packs_[static_cast<size_t>(idx)].pick(e.kind, e.release);
    if (!list || list->empty()) return;
    const Sound& s = (*list)[rng_.next() % list->size()];
    if (s.size() < 2) return;

    // free voice, or steal the oldest one
    Voice* slot = &voices_[0];
    for (Voice& v : voices_) {
        if (!v.sound) { slot = &v; break; }
        if (v.started < slot->started) slot = &v;
    }
    const float gain = volume.load(std::memory_order_relaxed) * rng_.range(0.85f, 1.0f);
    const float pan = stereo.load(std::memory_order_relaxed) ? std::clamp(e.pan, -1.0f, 1.0f) * 0.7f : 0.0f;
    const float angle = (pan + 1.0f) * 0.25f * 3.14159265f;  // equal-power pan
    slot->sound = &s;
    slot->pos = 0;
    slot->rate = rng_.range(0.97f, 1.03f);
    slot->gl = gain * std::cos(angle) * 1.41421356f;
    slot->gr = gain * std::sin(angle) * 1.41421356f;
    slot->started = ++counter_;
}

void Engine::render(float* out, uint32_t frames) {
    std::fill(out, out + static_cast<size_t>(frames) * 2, 0.0f);

    KeyEvent e;
    const bool on = enabled.load(std::memory_order_relaxed);
    const bool releases = releaseSounds.load(std::memory_order_relaxed);
    while (queue_.pop(e)) {
        if (on && (!e.release || releases)) trigger(e);
    }

    for (Voice& v : voices_) {
        if (!v.sound) continue;
        const Sound& s = *v.sound;
        const double last = static_cast<double>(s.size() - 1);
        for (uint32_t f = 0; f < frames; ++f) {
            if (v.pos >= last) { v.sound = nullptr; break; }
            const size_t i = static_cast<size_t>(v.pos);
            const float frac = static_cast<float>(v.pos - static_cast<double>(i));
            const float x = s[i] + (s[i + 1] - s[i]) * frac;
            out[2 * f] += x * v.gl;
            out[2 * f + 1] += x * v.gr;
            v.pos += v.rate;
        }
    }

    // soft limiter: transparent below 0.8, smoothly saturates above
    for (size_t i = 0; i < static_cast<size_t>(frames) * 2; ++i) {
        const float x = out[i], a = std::fabs(x);
        if (a > 0.8f) out[i] = std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), x);
    }
}

int Engine::activeVoices() const {
    int n = 0;
    for (const Voice& v : voices_) n += v.sound != nullptr;
    return n;
}

}  // namespace tw
