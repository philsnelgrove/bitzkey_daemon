#pragma once
// What the Bitzkeys have learned: recent choice statistics and brain probing.

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/Brain.hpp"
#include "core/Genome.hpp"

namespace bitzkey {

struct Bitzkey;

constexpr std::array<Species, 3> ANIMALS = {Species::Grazer, Species::Omnivore, Species::Predator};
constexpr double DECAY = 0.99;
constexpr double MIN_SAMPLE = 5.0;
// Insight traits: the mind traits, then the body traits after species.
constexpr int INSIGHT_TRAIT_COUNT = MIND_TRAIT_COUNT + BODY_TRAIT_COUNT - 1;
using Profile = std::array<double, INSIGHT_TRAIT_COUNT>;
std::string_view insight_trait_name(int trait);

enum class Level : int { Low, Mid, High };
// Trait values 0-7 in three bands: low 0-2, mid 3-4, high 5-7.
Level band(double value);
std::string_view level_name(Level level);
std::string group_label(Species species, std::optional<int> trait = std::nullopt, Level level = Level::Low);

// Recent decisions, discounted each turn: how often each kind of animal chose an action when it
// was possible.
class Insights {
public:
    struct Rate {
        std::optional<double> share;  // none until there are MIN_SAMPLE recent chances
        double offered = 0.0;
    };
    struct Extreme {
        std::string label;
        double share = 0.0;
        double offered = 0.0;
    };
    struct State {
        std::vector<double> offered, chosen;
    };

    Insights();
    void record(Species species, const Profile& traits, std::span<const int> options, int choice);
    void decay();
    Rate rate(int action, Species species, std::optional<int> trait = std::nullopt, Level level = Level::Low) const;
    std::vector<Extreme> extremes(int action, int count = 3, bool lowest = true) const;
    State snapshot() const { return {offered_, chosen_}; }
    void restore(const State& state) {
        offered_ = state.offered;
        chosen_ = state.chosen;
    }

private:
    static std::size_t key(Species species, int group, int level, int action);
    std::vector<double> offered_, chosen_;
};

// Probe every living brain with standard situations and summarise who prefers what.
std::string brain_report(int turn, std::span<const Bitzkey* const> animals, const Insights& insights);

}  // namespace bitzkey
