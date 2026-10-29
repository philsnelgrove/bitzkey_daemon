#include "daemon/Graph.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <span>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "core/Census.hpp"
#include "core/PyFormat.hpp"

namespace bitzkey::daemon {

namespace {
constexpr double DIM = 0.75;         // how far unselected Bitzkeys fade toward the background
constexpr double FUTURE_DIM = 0.55;  // how far turns after the present fade, once rewound
constexpr Rgb BACKGROUND{30, 40, 40};
constexpr Rgb GUIDE{54, 68, 62};
constexpr std::array<std::string_view, 3> CLIMATE_SORTS = {"temperature", "heat tolerance", "cold tolerance"};
constexpr int SORT_COUNT = 1 + TEMPERAMENT_COUNT + BODY_TRAIT_COUNT - 1 + 3;
constexpr int FIRST_CLIMATE_SORT = SORT_COUNT - static_cast<int>(CLIMATE_SORTS.size());

// The genome's word for a part: 0 the ring, 1 the dot, 2 the social background.
// 3 the species' own colours, a colour-blind safe set (Okabe and Ito's), as words: plant, grazer, omnivore, predator.
constexpr std::array<std::uint32_t, 4> SPECIES_WORDS = {(255u - 0) << 24 | (255u - 158) << 16 | (255u - 115) << 8,
                                                        (255u - 86) << 24 | (255u - 180) << 16 | (255u - 233) << 8,
                                                        (255u - 230) << 24 | (255u - 159) << 16 | (255u - 0) << 8,
                                                        (255u - 213) << 24 | (255u - 94) << 16 | (255u - 0) << 8};

std::uint32_t word_of(Dna dna, int part) {
    if (part == 3) return SPECIES_WORDS[static_cast<std::size_t>(decode_body(dna)[SpeciesTrait] % 4)];
    return part == 0 ? body_word(dna) : part == 1 ? mind_word(dna) : social_word(dna);
}

Rgb faded(Rgb colour, double amount) {
    const auto channel = [amount](int value, int back) { return static_cast<int>(py_round(value + (back - value) * amount)); };
    return {channel(colour.r, BACKGROUND.r), channel(colour.g, BACKGROUND.g), channel(colour.b, BACKGROUND.b)};
}

int sort_value(Dna dna, int sort) {
    if (sort == 0) return 0;
    if (sort >= FIRST_CLIMATE_SORT) {
        const Climate climate = decode_climate(dna);
        return sort == FIRST_CLIMATE_SORT ? climate.temperature : sort == FIRST_CLIMATE_SORT + 1 ? climate.heat : climate.cold;
    }
    if (sort <= TEMPERAMENT_COUNT) return decode_temperament(dna)[static_cast<std::size_t>(sort - 1)];
    return decode_body(dna)[static_cast<std::size_t>(sort - TEMPERAMENT_COUNT)];
}

template <typename T>
void put(std::string& out, T value) {
    char bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    out.append(bytes, sizeof(T));
}
}  // namespace

double nice_ceiling(double value) {
    double scale = 1;
    while (true) {
        for (const int step : {1, 2, 5}) {
            if (step * scale >= value) return step * scale;
        }
        scale *= 10;
    }
}

int sort_count() { return SORT_COUNT; }

std::string sort_label(int sort) {
    std::string name;
    if (sort == 0) return "Colour value";
    if (sort >= FIRST_CLIMATE_SORT) name = CLIMATE_SORTS[static_cast<std::size_t>(sort - FIRST_CLIMATE_SORT)];
    else if (sort <= TEMPERAMENT_COUNT) name = TEMPERAMENT[static_cast<std::size_t>(sort - 1)];
    else name = BODY_TRAITS[static_cast<std::size_t>(sort - TEMPERAMENT_COUNT)];
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

void Graph::build(const Simulation& simulation, const GraphView& view) {
    // One bar per stretch of turns, each stacking the Bitzkeys alive at its end; the bar holding the
    // present shows it exactly. Stacked by species, plants at the bottom; within each, by the sort's
    // trait if there is one, then similar colours together. A trait sorts only the living, the dead
    // being remembered by their colour alone, beneath them.
    const std::vector<long long> key = {simulation.turn(), static_cast<long long>(simulation.census().snapshots().size()),
                                        static_cast<long long>(simulation.agents().size()), simulation.deaths(), simulation.next_identifier(),
                                        view.part, view.sort, view.first, view.last, view.bars,
                                        static_cast<long long>(reinterpret_cast<std::uintptr_t>(&simulation))};
    if (key == key_) return;
    key_ = key;
    const int turns = view.last - view.first + 1;
    const int count = std::max(1, std::min(view.bars, turns));
    const int now = simulation.turn();
    std::vector<int> present;
    for (const Bitzkey* agent : simulation.living()) present.push_back(agent->identifier);
    alive_ = std::set<int>(present.begin(), present.end());

    using Key = std::tuple<int, int, int, double, double, std::uint32_t, int>;
    std::unordered_map<int, Key> order;
    const auto key_of = [&](int identifier) -> const Key& {
        auto found = order.find(identifier);
        if (found == order.end()) {
            const Dna dna = simulation.lineage(identifier);
            const std::uint32_t word = word_of(dna, view.part);
            const auto [group, hue, value] = colour_order(color_from_word(word));
            const int trait = view.sort == 0 ? 0 : alive_.contains(identifier) ? sort_value(dna, view.sort) : -1;
            found = order.emplace(identifier, Key{decode_body(dna)[SpeciesTrait], trait, group, hue, value, word, identifier}).first;
        }
        return found->second;
    };
    bars_.clear();
    std::size_t most = 0;
    for (int index = 0; index < count; ++index) {
        const int start = view.first + static_cast<int>(static_cast<long long>(index) * turns / count);
        const int end = view.first + static_cast<int>(static_cast<long long>(index + 1) * turns / count) - 1;
        const std::span<const int> members = (start <= now && now <= end) ? std::span<const int>(present) : simulation.census().at(end);
        Bar bar{end, start > now, std::vector<int>(members.begin(), members.end())};
        std::sort(bar.members.begin(), bar.members.end(), [&](int a, int b) { return key_of(a) < key_of(b); });
        most = std::max(most, bar.members.size());
        bars_.push_back(std::move(bar));
    }
    peak_ = nice_ceiling(static_cast<double>(most));
}

std::pair<double, double> Graph::scale(const GraphView& view) const {
    const int height = std::max(1, view.height);
    return {height * view.zoom / peak_, height + view.floor * view.zoom * height};
}

std::string Graph::image(const Simulation& simulation, const GraphView& view, const std::set<int>& selection) {
    build(simulation, view);
    const int height = std::max(1, view.height);
    const auto [unit, base] = scale(view);
    std::string out;
    out += "BZG1";
    put<std::uint32_t>(out, static_cast<std::uint32_t>(bars_.size()));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(height));
    put<float>(out, static_cast<float>(peak_));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(view.first));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(view.last));
    for (const Bar& bar : bars_) {
        put<std::uint32_t>(out, static_cast<std::uint32_t>(bar.turn));
        out.push_back(bar.future ? 1 : 0);
    }
    const bool choosing = !selection.empty();
    std::unordered_map<int, Rgb> colours;
    std::vector<Rgb> column(static_cast<std::size_t>(height));
    for (const Bar& bar : bars_) {
        std::fill(column.begin(), column.end(), BACKGROUND);
        for (const double fraction : {0.5, 1.0}) {  // guides at half and full
            const int y = static_cast<int>(py_round(height - fraction * height));
            if (y >= 0 && y < height) column[static_cast<std::size_t>(y)] = GUIDE;
        }
        column[static_cast<std::size_t>(height - 1)] = GUIDE;
        const auto fill = [&](Rgb colour, double top, double bottom) {
            const int y0 = std::max(0, static_cast<int>(py_round(top))), y1 = std::min(height, static_cast<int>(py_round(bottom)));
            for (int y = y0; y < y1; ++y) column[static_cast<std::size_t>(y)] = colour;
        };
        std::optional<Rgb> run_colour;
        double run_top = 0, run_bottom = 0, bottom = base;
        for (const int identifier : bar.members) {
            auto found = colours.find(identifier);
            if (found == colours.end()) {
                const Dna dna = simulation.lineage(identifier);
                found = colours.emplace(identifier, color_from_word(word_of(dna, view.part))).first;
            }
            Rgb colour = found->second;
            if (choosing && !selection.contains(identifier)) colour = faded(colour, DIM);
            if (bar.future) colour = faded(colour, FUTURE_DIM);
            const double top = bottom - unit;
            if (!run_colour || !(colour == *run_colour)) {
                if (run_colour) fill(*run_colour, run_top, run_bottom);
                run_colour = colour;
                run_bottom = bottom;
            }
            run_top = top;
            bottom = top;
        }
        if (run_colour) fill(*run_colour, run_top, run_bottom);
        for (const Rgb& pixel : column) {
            out.push_back(static_cast<char>(pixel.r));
            out.push_back(static_cast<char>(pixel.g));
            out.push_back(static_cast<char>(pixel.b));
        }
    }
    std::map<int, std::pair<double, double>> bands;  // species -> (top, bottom) in the last bar
    if (!bars_.empty()) {
        double bottom = base;
        for (const int identifier : bars_.back().members) {
            const int species = decode_body(simulation.lineage(identifier))[SpeciesTrait];
            const double top = bottom - unit;
            const auto found = bands.find(species);
            if (found == bands.end()) {
                bands.emplace(species, std::pair{top, bottom});
            } else {
                found->second.first = std::min(found->second.first, top);
                found->second.second = std::max(found->second.second, bottom);
            }
            bottom = top;
        }
    }
    put<std::uint32_t>(out, static_cast<std::uint32_t>(bands.size()));
    for (const auto& [species, band] : bands) {
        out.push_back(static_cast<char>(species));
        put<float>(out, static_cast<float>(band.first));
        put<float>(out, static_cast<float>(band.second));
    }
    return out;
}

std::set<int> Graph::living_in(const Simulation& simulation, const GraphView& view, int bar_from, int bar_to, double top, double bottom) {
    build(simulation, view);
    const auto [unit, base] = scale(view);
    std::set<int> found;
    const int last = static_cast<int>(bars_.size()) - 1;
    for (int index = std::max(0, std::min(bar_from, bar_to)); index <= std::min(last, std::max(bar_from, bar_to)); ++index) {
        double slice_bottom = base;
        for (const int identifier : bars_[static_cast<std::size_t>(index)].members) {
            const double slice_top = slice_bottom - unit;
            if (slice_bottom > top && slice_top < bottom && alive_.contains(identifier)) found.insert(identifier);
            slice_bottom = slice_top;
        }
    }
    return found;
}

std::optional<std::pair<int, bool>> Graph::at(const Simulation& simulation, const GraphView& view, int bar, double y) {
    build(simulation, view);
    if (bar < 0 || bar >= static_cast<int>(bars_.size())) return std::nullopt;
    const auto [unit, base] = scale(view);
    const auto& members = bars_[static_cast<std::size_t>(bar)].members;
    const double from_bottom = base - y;
    if (from_bottom < 0) return std::nullopt;
    const auto slot = static_cast<std::size_t>(from_bottom / unit);
    if (slot >= members.size()) return std::nullopt;
    const int identifier = members[slot];
    return std::pair{identifier, alive_.contains(identifier)};
}

}  // namespace bitzkey::daemon
