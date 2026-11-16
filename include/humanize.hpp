#pragma once
#include <random>

namespace human {
    inline std::mt19937 rng{ std::random_device{}() };

    template <typename T> T between(T lo, T hi) {
        return std::uniform_int_distribution<T>(lo, hi)(rng);
    }
    inline void sleep_range(int lo, int hi) { Sleep(between(lo, hi)); }
    inline float step_variance(float base) { return base * std::uniform_real_distribution<float>(0.7f, 1.3f)(rng); }
}