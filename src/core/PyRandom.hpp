#pragma once
// An exact re-implementation of CPython's random.Random (the Mersenne Twister and the methods
// built on it), so a world seeded here unfolds exactly as the Python version's does.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace bitzkey {

class PyRandom {
public:
    struct State {
        std::array<std::uint32_t, 624> mt{};
        int index = 625;
        std::optional<double> gauss_next;
        bool operator==(const State&) const = default;
    };

    explicit PyRandom(std::uint64_t seed = 0) { this->seed(seed); }

    // Seed as Python's random.Random(seed) does for a non-negative integer.
    void seed(std::uint64_t seed);

    std::uint32_t genrand32();
    double random();                                  // [0, 1), 53 bits
    std::uint64_t getrandbits(int bits);              // bits <= 64
    std::uint64_t randbelow(std::uint64_t n);         // [0, n), n > 0
    std::int64_t randrange(std::int64_t stop) { return static_cast<std::int64_t>(randbelow(stop)); }
    std::int64_t randrange(std::int64_t start, std::int64_t stop) {
        return start + static_cast<std::int64_t>(randbelow(static_cast<std::uint64_t>(stop - start)));
    }
    std::int64_t randint(std::int64_t low, std::int64_t high) { return randrange(low, high + 1); }
    double uniform(double low, double high) { return low + (high - low) * random(); }
    double gauss(double mu, double sigma);

    template <class T>
    const T& choice(std::span<const T> items) { return items[randbelow(items.size())]; }
    template <class T, std::size_t N>
    const T& choice(const std::array<T, N>& items) { return items[randbelow(N)]; }

    template <class T>
    void shuffle(std::vector<T>& items) {
        for (std::size_t i = items.size(); i-- > 1;) {
            std::size_t j = randbelow(i + 1);
            std::swap(items[i], items[j]);
        }
    }

    const State& getstate() const { return state_; }
    void setstate(const State& state) { state_ = state; }

private:
    void init_genrand(std::uint32_t seed);
    void init_by_array(const std::vector<std::uint32_t>& key);

    State state_;
};

}  // namespace bitzkey
