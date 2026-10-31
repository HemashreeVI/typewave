// The sound engine: sound packs, a lock-free event queue from the keyboard
// hook to the audio thread, and a polyphonic mixer so fast typing never cuts
// sounds off (the classic PlaySound() problem).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "keys.hpp"
#include "synth.hpp"

namespace tw {

using Sound = std::vector<float>;  // mono, kSampleRate

struct Pack {
    std::string name;
    std::string blurb;
    bool custom = false;
    // variants[kind][0 = press, 1 = release]
    std::array<std::array<std::vector<Sound>, 2>, kKindCount> variants;

    // The variants to use for a kind, falling back to the normal key's sounds.
    const std::vector<Sound>* pick(KeyKind kind, bool release) const;
    bool empty() const;
};

Pack buildPack(Style style, int variantsPerSound = 6);

struct KeyEvent {
    KeyKind kind = KeyKind::Normal;
    bool release = false;
    float pan = 0;  // -1 .. 1
};

// Single-producer / single-consumer ring buffer. The producer is the UI thread
// (keyboard hook + menu), the consumer is the audio callback. No locks, no allocation.
template <typename T, size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

public:
    bool push(const T& v) {
        const size_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= N) return false;  // full: drop
        buf_[h & (N - 1)] = v;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& out) {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;
        out = buf_[t & (N - 1)];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, N> buf_{};
    std::atomic<size_t> head_{0}, tail_{0};
};

class Engine {
public:
    static constexpr int kMaxVoices = 48;

    Engine();

    // Packs must be added before audio starts (or while it is stopped).
    void addPack(Pack p);
    void removeCustomPacks();
    size_t packCount() const { return packs_.size(); }
    const Pack& pack(size_t i) const { return packs_[i]; }
    int findPack(const std::string& name) const;  // -1 if missing

    // Settings, safe to change at any time from the UI thread.
    std::atomic<int> current{0};
    std::atomic<float> volume{0.6f};
    std::atomic<bool> enabled{true};
    std::atomic<bool> releaseSounds{true};
    std::atomic<bool> stereo{true};

    // UI thread: queue a key sound. Returns false if the queue is full.
    bool post(const KeyEvent& e) { return queue_.push(e); }

    // Audio thread: mix `frames` stereo frames (interleaved L R) into `out`.
    void render(float* out, uint32_t frames);

    int activeVoices() const;  // for tests

private:
    struct Voice {
        const Sound* sound = nullptr;
        double pos = 0;
        double rate = 1;
        float gl = 0, gr = 0;
        uint64_t started = 0;
    };
    void trigger(const KeyEvent& e);

    std::vector<Pack> packs_;
    SpscQueue<KeyEvent, 256> queue_;
    std::array<Voice, kMaxVoices> voices_{};
    uint64_t counter_ = 0;
    Rng rng_{0xC0FFEEu};
};

}  // namespace tw
