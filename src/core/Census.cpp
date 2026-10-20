#include "core/Census.hpp"

#include <algorithm>
#include <cmath>

namespace bitzkey {

std::tuple<int, double, double> colour_order(const Rgb& rgb) {
    // colorsys.rgb_to_hsv on channels scaled to 0..1.
    const double r = rgb.r / 255.0, g = rgb.g / 255.0, b = rgb.b / 255.0;
    const double maxc = std::max({r, g, b}), minc = std::min({r, g, b});
    const double value = maxc;
    double hue = 0.0, saturation = 0.0;
    if (minc != maxc) {
        const double range = maxc - minc;
        saturation = range / maxc;
        const double rc = (maxc - r) / range, gc = (maxc - g) / range, bc = (maxc - b) / range;
        if (r == maxc) {
            hue = bc - gc;
        } else if (g == maxc) {
            hue = 2.0 + rc - bc;
        } else {
            hue = 4.0 + gc - rc;
        }
        hue = hue / 6.0;
        hue -= std::floor(hue);  // Python's (h / 6.0) % 1.0
    }
    if (saturation < 0.2 || value < 0.2) return {0, 0.0, value};
    return {1, hue, value};
}

bool Census::record(int turn, std::vector<int> identifiers) {
    Snapshot entry{turn, std::move(identifiers)};
    if (!snapshots_.empty() && snapshots_.back().turn % stride_) {
        snapshots_.back() = std::move(entry);  // the last one only stood in for "latest"
    } else {
        snapshots_.push_back(std::move(entry));
    }
    if (snapshots_.size() > MAX_SNAPSHOTS) {
        stride_ *= 2;
        Snapshot latest = std::move(snapshots_.back());
        snapshots_.pop_back();
        std::erase_if(snapshots_, [this](const Snapshot& snapshot) { return snapshot.turn % stride_ != 0; });
        snapshots_.push_back(std::move(latest));
    }
    std::size_t old = 0;
    while (old + 1 < snapshots_.size() && snapshots_[old].turn < turn - GRAPH_TURNS) ++old;
    snapshots_.erase(snapshots_.begin(), snapshots_.begin() + static_cast<std::ptrdiff_t>(old));
    return old > 0;
}

void Census::forget_from(int turn) {
    while (!snapshots_.empty() && snapshots_.back().turn >= turn) snapshots_.pop_back();
}

std::span<const int> Census::at(int turn) const {
    const auto after = std::upper_bound(snapshots_.begin(), snapshots_.end(), turn,
                                        [](int value, const Snapshot& snapshot) { return value < snapshot.turn; });
    if (after == snapshots_.begin()) return {};
    return std::prev(after)->identifiers;
}

}  // namespace bitzkey
