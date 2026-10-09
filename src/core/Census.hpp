#pragma once
// Population census for the colour graph: which Bitzkeys were alive, sampled across the whole run
// at an even spacing that widens as the run grows, so it never outgrows memory.

#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "core/Genome.hpp"

namespace bitzkey {

constexpr std::size_t MAX_SNAPSHOTS = 1000;

// A sort key that stacks similar colours together: greys by lightness, then hues round the wheel.
std::tuple<int, double, double> colour_order(const Rgb& rgb);

class Census {
public:
    struct Snapshot {
        int turn = 0;
        std::vector<int> identifiers;  // the Bitzkeys alive then
    };

    // Snapshots every `stride` turns, plus the latest turn. When there are too many, the stride
    // doubles and every other snapshot goes.
    void record(int turn, std::vector<int> identifiers);
    // Drop snapshots of turns from `turn` on: a future abandoned by rewinding.
    void forget_from(int turn);
    // Who was alive at the latest snapshot on or before `turn`.
    std::span<const int> at(int turn) const;

    const std::vector<Snapshot>& snapshots() const { return snapshots_; }
    int stride() const { return stride_; }

private:
    std::vector<Snapshot> snapshots_;
    int stride_ = 1;
};

}  // namespace bitzkey
