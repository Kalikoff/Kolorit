#pragma once

#include <cstdint>

namespace kolorit {

/// Fast mixing of a 32-bit number (lowbias32). Deterministic and stateless, so it is
/// safe to call from any thread.
inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/// Combine several values into one hash (seed + row number + pass number)
inline uint32_t hashCombine(uint32_t a, uint32_t b)
{
    return hash32(a * 0x9E3779B1U ^ hash32(b + 0x7F4A7C15U));
}

inline uint32_t hashCombine(uint32_t a, uint32_t b, uint32_t c)
{
    return hashCombine(hashCombine(a, b), c);
}

/// A lightweight xorshift32 generator with local state.
/// Every image row creates its own instance with seed = hash(seed, y), so the result
/// does not depend on the number of threads or the order they run in.
class Rng {
public:
    explicit Rng(uint32_t seed)
        : m_state(seed ? seed : 0xA341316CU)
    {
    }

    uint32_t next()
    {
        uint32_t s = m_state;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        m_state = s;
        return s;
    }

    /// Uniform number in [0, 1)
    float unit() { return float(next() >> 8) * (1.f / 16777216.f); }

    /// Uniform number in [-1, 1)
    float signedUnit() { return unit() * 2.f - 1.f; }

    /// Integer in [lo, hi]
    int range(int lo, int hi)
    {
        if (hi <= lo)
            return lo;
        return lo + int(next() % uint32_t(hi - lo + 1));
    }

private:
    uint32_t m_state;
};

} // namespace kolorit
