#pragma once
// Synthetic test audio (never real recordings).
#include <cmath>
#include <vector>

#include "mangle/Types.h"

namespace testsig {

inline mangle::SampleData sine(double sr, double seconds, double hz, float amp = 0.5f)
{
    mangle::SampleData d;
    d.sampleRate = sr;
    const size_t n = static_cast<size_t>(seconds * sr);
    d.left.resize(n);
    for (size_t i = 0; i < n; ++i) d.left[i] = amp * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / sr));
    d.right = d.left;
    return d;
}

/** Drum-ish loop: `beats` quarter notes at `bpm`, a kick on every beat and a hat on every off-beat. */
inline mangle::SampleData drumLoop(double sr, double bpm, int beats, bool kickOnly = false)
{
    mangle::SampleData d;
    d.sampleRate = sr;
    const size_t n = static_cast<size_t>(std::llround(beats * 60.0 / bpm * sr));
    d.left.assign(n, 0.f);
    unsigned seed = 12345;
    auto noise = [&]() { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / 8388608.f - 1.f; };
    const double beat = 60.0 / bpm * sr;
    for (int b = 0; b < beats; ++b)
    {
        const size_t k0 = static_cast<size_t>(b * beat);
        for (size_t i = 0; i < static_cast<size_t>(0.18 * sr) && k0 + i < n; ++i)   // kick: decaying sweep
        {
            const double t = static_cast<double>(i) / sr;
            d.left[k0 + i] += 0.9f * static_cast<float>(std::sin(2.0 * M_PI * (50.0 * t + 90.0 * (1.0 - std::exp(-t * 30.0)) / 30.0)) * std::exp(-t * 22.0));
        }
        if (kickOnly) continue;
        const size_t h0 = static_cast<size_t>((b + 0.5) * beat);
        for (size_t i = 0; i < static_cast<size_t>(0.04 * sr) && h0 + i < n; ++i)
            d.left[h0 + i] += 0.35f * noise() * static_cast<float>(std::exp(-static_cast<double>(i) / sr * 90.0));
    }
    d.right = d.left;
    return d;
}

inline double rms(const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0;
    for (size_t i = a; i < b && i < v.size(); ++i) s += static_cast<double>(v[i]) * v[i];
    return b > a ? std::sqrt(s / static_cast<double>(b - a)) : 0.0;
}

/** Dominant frequency by zero crossings (clean tones only). */
inline double zeroCrossHz(const std::vector<float>& v, size_t a, size_t b, double sr)
{
    int c = 0;
    for (size_t i = a + 1; i < b && i < v.size(); ++i) if ((v[i - 1] < 0.f) != (v[i] < 0.f)) ++c;
    return c / 2.0 / (static_cast<double>(b - a) / sr);
}

} // namespace testsig
