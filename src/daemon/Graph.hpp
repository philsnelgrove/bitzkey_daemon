#pragma once
// The colour graph, built on the server: every Bitzkey alive over time, stacked in its own colour,
// as in the window app. A run can hold millions of slices, so the browser is sent the bars already
// drawn, a pixel column per bar, and asks the server which Bitzkeys lie under a click or a box.

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/Simulation.hpp"

namespace bitzkey::daemon {

// The smallest 1, 2 or 5 times a power of ten at or above the value.
double nice_ceiling(double value);

// What a species' Bitzkeys are sorted by: their colour (0), then each mind trait, then each body
// trait but species, then each climate gene.
int sort_count();
std::string sort_label(int sort);

struct GraphView {
    int first = 0, last = 0;  // the turns shown
    int bars = 1;             // how many bars they are drawn as
    int height = 1;           // the chart's height in pixels
    bool dot = false;         // the dot's (mind's) colour, else the ring's
    int sort = 0;
    // Zoomed into the height, the chart shows 1 / zoom of it, from floor (a share of the top) up;
    // slices beyond it lie outside the chart.
    double zoom = 1.0, floor = 0.0;
};

class Graph {
public:
    // The bars drawn, little-endian:
    //   "BZG1", u32 bars, u32 height, f32 peak, u32 first, u32 last;
    //   per bar: u32 turn, u8 future (after the present, when rewound);
    //   bars x height RGB bytes, bar by bar, top to bottom;
    //   u32 band count, per band: u8 species, f32 top, f32 bottom (pixels): each species' band in the last bar.
    // The selected are drawn bright, the rest faded toward the background.
    std::string image(const Simulation& simulation, const GraphView& view, const std::set<int>& selection);
    // The living Bitzkeys whose slices lie in a box of bars and pixel rows, both ends included.
    std::set<int> living_in(const Simulation& simulation, const GraphView& view, int bar_from, int bar_to, double top, double bottom);
    // The Bitzkey under a point, if any, and whether it is alive.
    std::optional<std::pair<int, bool>> at(const Simulation& simulation, const GraphView& view, int bar, double y);

private:
    struct Bar {
        int turn = 0;
        bool future = false;
        std::vector<int> members;  // bottom to top
    };
    void build(const Simulation& simulation, const GraphView& view);
    // Pixels a Bitzkey's slice stands, and where none would stand, at the view's height and zoom.
    std::pair<double, double> scale(const GraphView& view) const;

    std::vector<long long> key_;
    std::vector<Bar> bars_;
    std::set<int> alive_;
    double peak_ = 1.0;
};

}  // namespace bitzkey::daemon
