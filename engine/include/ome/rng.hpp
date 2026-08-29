#ifndef ORDER_MATCHING_ENGINE_RNG_HPP
#define ORDER_MATCHING_ENGINE_RNG_HPP

#include <cstdint>
#include <random>

// Deterministic RNG: same seed always produces the same sequence, since
// std::mt19937_64's algorithm is spec-fixed across platforms. Seed always
// comes from the caller (config) -- never seed this from std::random_device
// or other hardware entropy inside core code, or reproducibility breaks.
class Rng {
  public:
    explicit Rng(const uint64_t seed) : engine(seed) {}

    uint64_t next() { return engine(); }

  private:
    std::mt19937_64 engine;
};

#endif // ORDER_MATCHING_ENGINE_RNG_HPP
