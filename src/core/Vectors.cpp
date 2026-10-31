#include "core/Vectors.hpp"

#include <array>

#if defined(__x86_64__) || defined(_M_X64)
#define BITZKEY_WIDE 1
#include <immintrin.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define BITZKEY_AVX2
#else
#define BITZKEY_AVX2 __attribute__((target("avx2")))  // not fma: multiplying and adding at once rounds differently
#endif
#endif

namespace bitzkey::vectors {

namespace {

// numpy's pairwise summation (the Python version adds up with numpy's sum), so that every sum matches
// the Python version's to the last bit: up to 8 values in order; up to 128 in eight running sums,
// combined in pairs, then the rest in order; more, in two halves split on a multiple of eight.
double pairwise_sum(const double* values, std::size_t count) {
    if (count < 8) {
        double total = -0.0;
        for (std::size_t index = 0; index < count; ++index) total += values[index];
        return total;
    }
    if (count <= 128) {
        double sums[8];
        for (std::size_t lane = 0; lane < 8; ++lane) sums[lane] = values[lane];
        std::size_t index = 8;
        for (; index < count - count % 8; index += 8) {
            for (std::size_t lane = 0; lane < 8; ++lane) sums[lane] += values[index + lane];
        }
        double total = ((sums[0] + sums[1]) + (sums[2] + sums[3])) + ((sums[4] + sums[5]) + (sums[6] + sums[7]));
        for (; index < count; ++index) total += values[index];
        return total;
    }
    std::size_t half = count / 2;
    half -= half % 8;
    return pairwise_sum(values, half) + pairwise_sum(values + half, count - half);
}

#ifdef BITZKEY_WIDE

bool can_go_wide() {
#if defined(_MSC_VER) && !defined(__clang__)
    int found[4];
    __cpuid(found, 0);
    if (found[0] < 7) return false;
    __cpuid(found, 1);
    if (!(found[2] & (1 << 27)) || !(found[2] & (1 << 28))) return false;  // the system saves these registers, and AVX
    if ((_xgetbv(0) & 6) != 6) return false;
    __cpuidex(found, 7, 0);
    return (found[1] & (1 << 5)) != 0;
#else
    return __builtin_cpu_supports("avx2");
#endif
}

BITZKEY_AVX2 double pairwise_sum_wide(const double* values, std::size_t count) {
    if (count < 8) return pairwise_sum(values, count);
    if (count <= 128) {
        __m256d low = _mm256_loadu_pd(values);
        __m256d high = _mm256_loadu_pd(values + 4);
        std::size_t index = 8;
        for (; index < count - count % 8; index += 8) {
            low = _mm256_add_pd(low, _mm256_loadu_pd(values + index));
            high = _mm256_add_pd(high, _mm256_loadu_pd(values + index + 4));
        }
        double sums[8];
        _mm256_storeu_pd(sums, low);
        _mm256_storeu_pd(sums + 4, high);
        double total = ((sums[0] + sums[1]) + (sums[2] + sums[3])) + ((sums[4] + sums[5]) + (sums[6] + sums[7]));
        for (; index < count; ++index) total += values[index];
        return total;
    }
    std::size_t half = count / 2;
    half -= half % 8;
    return pairwise_sum_wide(values, half) + pairwise_sum_wide(values + half, count - half);
}

BITZKEY_AVX2 double dot_wide(const double* first, const double* second, std::size_t count) {
    std::array<double, MOST> products;
    std::size_t index = 0;
    for (; index + 4 <= count; index += 4) {
        _mm256_storeu_pd(products.data() + index, _mm256_mul_pd(_mm256_loadu_pd(first + index), _mm256_loadu_pd(second + index)));
    }
    for (; index < count; ++index) products[index] = first[index] * second[index];
    return pairwise_sum_wide(products.data(), count);
}

BITZKEY_AVX2 void scale_wide(double* values, std::size_t count, double by) {
    const __m256d factor = _mm256_set1_pd(by);
    std::size_t index = 0;
    for (; index + 4 <= count; index += 4) _mm256_storeu_pd(values + index, _mm256_mul_pd(_mm256_loadu_pd(values + index), factor));
    for (; index < count; ++index) values[index] *= by;
}

BITZKEY_AVX2 void add_scaled_wide(double* to, const double* from, std::size_t count, double by) {
    const __m256d factor = _mm256_set1_pd(by);
    std::size_t index = 0;
    for (; index + 4 <= count; index += 4) {
        _mm256_storeu_pd(to + index, _mm256_add_pd(_mm256_loadu_pd(to + index), _mm256_mul_pd(factor, _mm256_loadu_pd(from + index))));
    }
    for (; index < count; ++index) to[index] += by * from[index];
}

BITZKEY_AVX2 void add_scaled_twice_wide(double* to, const double* from, std::size_t count, double outer, double inner) {
    const __m256d first = _mm256_set1_pd(outer);
    const __m256d second = _mm256_set1_pd(inner);
    std::size_t index = 0;
    for (; index + 4 <= count; index += 4) {
        const __m256d product = _mm256_mul_pd(first, _mm256_mul_pd(second, _mm256_loadu_pd(from + index)));
        _mm256_storeu_pd(to + index, _mm256_add_pd(_mm256_loadu_pd(to + index), product));
    }
    for (; index < count; ++index) to[index] += outer * (inner * from[index]);
}

const bool able = can_go_wide();

#else

const bool able = false;

#endif

bool going_wide = able;

}  // namespace

bool wide() { return going_wide; }

bool use_wide(bool on) {
    const bool was = going_wide;
    going_wide = on && able;
    return was;
}

double dot(const double* first, const double* second, std::size_t count) {
#ifdef BITZKEY_WIDE
    if (going_wide) return dot_wide(first, second, count);
#endif
    std::array<double, MOST> products;
    for (std::size_t index = 0; index < count; ++index) products[index] = first[index] * second[index];
    return pairwise_sum(products.data(), count);
}

void scale(double* values, std::size_t count, double by) {
#ifdef BITZKEY_WIDE
    if (going_wide) return scale_wide(values, count, by);
#endif
    for (std::size_t index = 0; index < count; ++index) values[index] *= by;
}

void add_scaled(double* to, const double* from, std::size_t count, double by) {
#ifdef BITZKEY_WIDE
    if (going_wide) return add_scaled_wide(to, from, count, by);
#endif
    for (std::size_t index = 0; index < count; ++index) to[index] += by * from[index];
}

void add_scaled_twice(double* to, const double* from, std::size_t count, double outer, double inner) {
#ifdef BITZKEY_WIDE
    if (going_wide) return add_scaled_twice_wide(to, from, count, outer, inner);
#endif
    for (std::size_t index = 0; index < count; ++index) to[index] += outer * (inner * from[index]);
}

}  // namespace bitzkey::vectors
