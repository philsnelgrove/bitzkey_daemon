#pragma once
// What the Bitzkeys have learned: recent choice statistics and brain probing.

#include <array>
#include <functional>
#include <map>
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
constexpr int INSIGHT_TRAIT_COUNT = TEMPERAMENT_COUNT + BODY_TRAIT_COUNT - 1;
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
    struct Entry {  // one group's counts for an action: the whole species (no trait), or a trait's level
        Species species;
        std::optional<int> trait;
        Level level;
        int action;
        double offered, chosen;
    };
    std::vector<Entry> entries() const;  // every group and action counted, for saving a world
    void set(Species species, std::optional<int> trait, Level level, int action, double offered, double chosen);
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

// A death: its cause, the dead Bitzkey's species and the turn, and, when something killed it, the killer's
// identifier and species and what the killer was doing (hunting, attacking, defending, browsing, uprooting, or
// spines: struck, its spines killed its attacker).
struct Death {
    std::string cause;
    Species species = Species::Plant;
    int turn = 0;
    int killer = -1;
    Species killer_species = Species::Plant;
    std::string activity;
};
constexpr int DEADLIEST = 10;  // the killers the death report names

// Every death since the world began, counted as it happens rather than kept one by one: by cause and the species of
// the dead; who killed whom; what the killers were doing; and each killer's kills, for the living and for the DEADLIEST
// of those gone (the rest let go, see let_go).
struct DeathTally {
    using Counts = std::array<int, 4>;  // by species, in SPECIES order
    struct Killer {
        Species species = Species::Plant;
        int kills = 0;
        std::map<std::string, int, std::less<>> doing;  // its kills by what it was doing
    };
    std::map<std::string, Counts, std::less<>> causes;      // by cause, by the species of the dead
    std::array<Counts, 4> whom{};                           // by the species of the dead, by their killers'
    std::map<std::string, Counts, std::less<>> activities;  // by what the killers were doing, by the killers' species
    int killers = 0;                                        // how many Bitzkeys have ever killed
    std::map<int, Killer> by_killer;                        // by identifier

    void add(const Death& death);
    // Forget the killers no longer `alive` but the DEADLIEST of them, so the tally doesn't grow with the run.
    void let_go(const std::function<bool(int)>& alive);
};

// The death report's figures, for drawing as tables. Each row counts deaths by species, in SPECIES order.
struct DeathReport {
    struct Row {
        std::string label;
        std::array<int, 4> counts{};
        int total = 0;
    };
    struct Killer {
        int identifier = -1;
        Species species = Species::Plant;
        int kills = 0;
        std::vector<std::pair<std::string, int>> doing;  // its kills by what it was doing, the most first
    };
    std::vector<Row> causes;         // by the species of the dead, the most common cause first
    Row all{"ALL"};                  // every death, by species
    int kills = 0;                   // the deaths something killed
    std::vector<Row> whom;           // the dead, by species (rows), by their killers' species (counts)
    std::vector<Row> activities;     // what the killers were doing, the most common first, by the killers' species
    int killers = 0;                 // how many Bitzkeys have killed
    std::vector<Killer> deadliest;   // the DEADLIEST with the most kills
};
DeathReport death_report(const DeathTally& tally);
DeathReport death_report(const std::vector<Death>& deaths);  // these deaths alone, tallied

// The two reports a viewer can open, each laid out as lines of text in a fixed-width font. Every death so far,
// back to the world's start: a table of causes by the species of the dead, the most common first; who killed
// whom, by the species of the dead and of their killers; what the killers were doing, by the killers' species;
// and the deadliest killers, each with its kills by what it was doing. And one animal's brain probed with the
// standard situations its species can meet, its chance of choosing each action open to it, with its temperament
// now, the likeliest marked * and an action not open shown as a dot.
std::vector<std::string> death_report_lines(const DeathTally& tally);
std::vector<std::string> death_report_lines(const std::vector<Death>& deaths);
struct BrainRow {
    std::string situation;
    std::vector<std::pair<int, double>> chances;  // (action, chance), for the actions open in it
};
std::vector<BrainRow> brain_matrix(const Bitzkey& animal);
std::vector<std::string> brain_matrix_lines(const Bitzkey& animal);
std::string_view short_action(int action);  // an action's name in a column of the matrix

}  // namespace bitzkey
