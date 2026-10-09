#include "core/PyRandom.hpp"

#include <bit>
#include <cmath>
#include <numbers>

namespace bitzkey {

namespace {
constexpr int N = 624;
constexpr int M = 397;
constexpr std::uint32_t MATRIX_A = 0x9908b0dfU;
constexpr std::uint32_t UPPER_MASK = 0x80000000U;
constexpr std::uint32_t LOWER_MASK = 0x7fffffffU;
}  // namespace

void PyRandom::init_genrand(std::uint32_t seed) {
    auto& mt = state_.mt;
    mt[0] = seed;
    for (int i = 1; i < N; ++i) {
        mt[i] = 1812433253U * (mt[i - 1] ^ (mt[i - 1] >> 30)) + static_cast<std::uint32_t>(i);
    }
    state_.index = N;
}

void PyRandom::init_by_array(const std::vector<std::uint32_t>& key) {
    auto& mt = state_.mt;
    init_genrand(19650218U);
    std::size_t i = 1, j = 0;
    const std::size_t length = key.size();
    for (std::size_t k = (N > length ? N : length); k; --k) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525U)) + key[j] + static_cast<std::uint32_t>(j);
        ++i;
        ++j;
        if (i >= N) {
            mt[0] = mt[N - 1];
            i = 1;
        }
        if (j >= length) j = 0;
    }
    for (std::size_t k = N - 1; k; --k) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941U)) - static_cast<std::uint32_t>(i);
        ++i;
        if (i >= N) {
            mt[0] = mt[N - 1];
            i = 1;
        }
    }
    mt[0] = 0x80000000U;
    state_.index = N;
}

void PyRandom::seed(std::uint64_t seed) {
    // Python splits the seed's absolute value into 32-bit words, least significant first.
    std::vector<std::uint32_t> key{static_cast<std::uint32_t>(seed & 0xFFFFFFFFU)};
    if (seed >> 32) key.push_back(static_cast<std::uint32_t>(seed >> 32));
    init_by_array(key);
    state_.gauss_next.reset();
}

std::uint32_t PyRandom::genrand32() {
    static constexpr std::uint32_t mag01[2] = {0x0U, MATRIX_A};
    auto& mt = state_.mt;
    std::uint32_t y;
    if (state_.index >= N) {
        int kk = 0;
        for (; kk < N - M; ++kk) {
            y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK);
            mt[kk] = mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        for (; kk < N - 1; ++kk) {
            y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK);
            mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        y = (mt[N - 1] & UPPER_MASK) | (mt[0] & LOWER_MASK);
        mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
        state_.index = 0;
    }
    y = mt[state_.index++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    y ^= (y >> 18);
    return y;
}

double PyRandom::random() {
    const std::uint32_t a = genrand32() >> 5;
    const std::uint32_t b = genrand32() >> 6;
    return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}

std::uint64_t PyRandom::getrandbits(int bits) {
    if (bits == 0) return 0;
    if (bits <= 32) return genrand32() >> (32 - bits);
    // Least significant word first, the last one holding what is left over.
    std::uint64_t low = genrand32();
    std::uint64_t high = genrand32() >> (64 - bits);
    return low | (high << 32);
}

std::uint64_t PyRandom::randbelow(std::uint64_t n) {
    const int bits = static_cast<int>(std::bit_width(n));
    std::uint64_t value = getrandbits(bits);
    while (value >= n) value = getrandbits(bits);
    return value;
}

double PyRandom::gauss(double mu, double sigma) {
    double z;
    if (state_.gauss_next) {
        z = *state_.gauss_next;
        state_.gauss_next.reset();
    } else {
        const double x2pi = random() * (2.0 * std::numbers::pi);
        const double g2rad = std::sqrt(-2.0 * std::log(1.0 - random()));
        z = std::cos(x2pi) * g2rad;
        state_.gauss_next = std::sin(x2pi) * g2rad;
    }
    return mu + z * sigma;
}

}  // namespace bitzkey
