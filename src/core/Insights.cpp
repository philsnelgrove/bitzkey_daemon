#include "core/Insights.hpp"

#include <algorithm>
#include <cstdio>
#include <execution>
#include <numeric>
#include <map>
#include <tuple>

#include "core/PyFormat.hpp"
#include "core/Simulation.hpp"

namespace bitzkey {

namespace {
constexpr int GROUPS = 1 + INSIGHT_TRAIT_COUNT;  // the whole species, then one per trait
constexpr int LEVELS = 3;
constexpr std::size_t KEYS = 4 * GROUPS * LEVELS * ACTION_COUNT;
}  // namespace

std::string_view insight_trait_name(int trait) {
    return trait < TEMPERAMENT_COUNT ? TEMPERAMENT[trait] : BODY_TRAITS[trait - TEMPERAMENT_COUNT + 1];
}

Level band(double value) {
    const double rounded = py_round(value);
    return rounded <= 2 ? Level::Low : rounded <= 4 ? Level::Mid : Level::High;
}

std::string_view level_name(Level level) {
    static constexpr std::array<std::string_view, 3> names = {"low", "mid", "high"};
    return names[static_cast<int>(level)];
}

std::string group_label(Species species, std::optional<int> trait, Level level) {
    if (!trait) return std::string(species_name(species)) + "s";
    return std::string(level_name(level)) + "-" + std::string(insight_trait_name(*trait)) + " " +
           std::string(species_name(species)) + "s";
}

Insights::Insights() : offered_(KEYS, 0.0), chosen_(KEYS, 0.0) {}

std::vector<Insights::Entry> Insights::entries() const {
    std::vector<Entry> found;
    for (int species = 0; species < 4; ++species) {
        for (int group = 0; group < GROUPS; ++group) {
            for (int level = 0; level < (group ? LEVELS : 1); ++level) {
                for (int action = 0; action < ACTION_COUNT; ++action) {
                    const std::size_t at = key(static_cast<Species>(species), group, level, action);
                    if (offered_[at] == 0.0 && chosen_[at] == 0.0) continue;
                    found.push_back({static_cast<Species>(species), group ? std::optional<int>(group - 1) : std::nullopt,
                                     static_cast<Level>(level), action, offered_[at], chosen_[at]});
                }
            }
        }
    }
    return found;
}

void Insights::set(Species species, std::optional<int> trait, Level level, int action, double offered, double chosen) {
    const std::size_t at = key(species, trait ? 1 + *trait : 0, trait ? static_cast<int>(level) : 0, action);
    offered_[at] = offered;
    chosen_[at] = chosen;
}

std::size_t Insights::key(Species species, int group, int level, int action) {
    return ((static_cast<std::size_t>(species) * GROUPS + group) * LEVELS + level) * ACTION_COUNT + action;
}

void Insights::record(Species species, const Profile& traits, std::span<const int> options, int choice) {
    for (int group = 0; group < GROUPS; ++group) {
        const int level = group == 0 ? 0 : static_cast<int>(band(traits[group - 1]));
        for (const int action : options) offered_[key(species, group, level, action)] += 1;
        chosen_[key(species, group, level, choice)] += 1;
    }
}

void Insights::record_all(std::size_t count, const std::function<std::optional<Choice>(std::size_t)>& chose) {
    std::vector<std::optional<Choice>> choices(count);
    std::vector<std::size_t> indices(count);
    std::iota(indices.begin(), indices.end(), std::size_t{0});
    std::for_each(std::execution::par, indices.begin(), indices.end(), [&](std::size_t index) { choices[index] = chose(index); });
    std::array<int, GROUPS> groups;
    std::iota(groups.begin(), groups.end(), 0);
    std::for_each(std::execution::par, groups.begin(), groups.end(), [&](int group) {
        for (const std::optional<Choice>& made : choices) {
            if (!made) continue;
            const int level = group == 0 ? 0 : static_cast<int>(band(made->traits[group - 1]));
            for (const int action : made->options) offered_[key(made->species, group, level, action)] += 1;
            chosen_[key(made->species, group, level, made->choice)] += 1;
        }
    });
}

void Insights::decay() {
    for (auto* counts : {&offered_, &chosen_}) {
        for (double& count : *counts) {
            if (count == 0.0) continue;
            count *= DECAY;
            if (count < 0.01) count = 0.0;
        }
    }
}

Insights::Rate Insights::rate(int action, Species species, std::optional<int> trait, Level level) const {
    const std::size_t at = trait ? key(species, 1 + *trait, static_cast<int>(level), action) : key(species, 0, 0, action);
    const double offered = offered_[at];
    if (offered < MIN_SAMPLE) return {std::nullopt, offered};
    return {chosen_[at] / offered, offered};
}

std::vector<Insights::Extreme> Insights::extremes(int action, int count, bool lowest) const {
    std::vector<Extreme> found;
    for (int species = 0; species < 4; ++species) {
        for (int trait = 0; trait < INSIGHT_TRAIT_COUNT; ++trait) {
            for (int level = 0; level < LEVELS; ++level) {
                if (offered_[key(static_cast<Species>(species), 1 + trait, level, action)] == 0.0) continue;
                const Rate result = rate(action, static_cast<Species>(species), trait, static_cast<Level>(level));
                if (result.share) {
                    found.push_back({group_label(static_cast<Species>(species), trait, static_cast<Level>(level)),
                                     *result.share, result.offered});
                }
            }
        }
    }
    std::sort(found.begin(), found.end(), [lowest](const Extreme& a, const Extreme& b) {
        const double ka = lowest ? a.share : -a.share, kb = lowest ? b.share : -b.share;
        return std::tie(ka, a.label) < std::tie(kb, b.label);
    });
    if (static_cast<int>(found.size()) > count) found.resize(static_cast<std::size_t>(count));
    return found;
}

namespace {

struct Scenario {
    const char* title;
    std::vector<std::pair<Sense, double>> situation;
    std::vector<Action> actions;
    std::vector<Species> species;
};

const std::vector<Species> ANIMAL_LIST = {Species::Grazer, Species::Omnivore, Species::Predator};
const std::vector<Species> GRAZER_LIST = {Species::Grazer, Species::Omnivore};
const std::vector<Species> GRAZER_ONLY = {Species::Grazer};
const std::vector<Species> HUNTER_LIST = {Species::Omnivore, Species::Predator};

const std::vector<Scenario>& scenarios() {
    static const std::vector<Scenario> list = {
        {"Fed and safe, a willing mate beside them",
         {{SenseHunger, 0.2}, {SenseHealth, 1}, {SenseMateNear, 1}, {SenseMateCloseness, 1}, {SenseReadyToBreed, 1},
          {SenseKinNear, 1.0 / 3}, {SenseAge, 0.2}},
         {Breed, Court, Rest, GoEast, GoWest}, ANIMAL_LIST},
        {"Fed and safe, a willing mate in sight",
         {{SenseHunger, 0.2}, {SenseHealth, 1}, {SenseReadyToBreed, 1}, {SenseMateCloseness, 0.6}, {SenseAge, 0.2}},
         {Court, Rest, GoEast, GoWest}, ANIMAL_LIST},
        {"Hungry, grass underfoot, a willing mate beside them",
         {{SenseHunger, 0.6}, {SenseHealth, 1}, {SenseGrassHere, 1}, {SenseMateNear, 1}, {SenseMateCloseness, 1},
          {SenseReadyToBreed, 1}, {SenseKinNear, 1.0 / 3}, {SenseAge, 0.2}},
         {Eat, Breed, Court, Rest, GoEast, GoWest}, GRAZER_LIST},
        {"Hungry, grass in sight",
         {{SenseHunger, 0.6}, {SenseHealth, 1}, {SenseGrassSeen, 1}, {SenseGrassCloseness, 0.6}, {SenseAge, 0.2}},
         {Forage, Rest, GoEast, GoWest}, GRAZER_LIST},
        {"Hungry, a plant in sight",
         {{SenseHunger, 0.6}, {SenseHealth, 1}, {SensePlantCloseness, 0.6}, {SenseAge, 0.2}},
         {Browse, Rest, GoEast, GoWest}, GRAZER_LIST},
        {"Hungry, carrion in sight",
         {{SenseHunger, 0.6}, {SenseHealth, 1}, {SenseCarrionCloseness, 0.6}, {SenseAge, 0.2}},
         {Scavenge, Rest, GoEast, GoWest}, HUNTER_LIST},
        {"Hungry, a fresh kill in sight",
         {{SenseHunger, 0.6}, {SenseHealth, 1}, {SenseKillCloseness, 0.6}, {SenseAge, 0.2}},
         {Feast, Rest, GoEast, GoWest}, HUNTER_LIST},
        {"Peckish, prey in sight",
         {{SenseHunger, 0.5}, {SenseHealth, 1}, {SensePreyCloseness, 0.6}, {SenseAge, 0.2}},
         {Hunt, Rest, GoEast, GoWest}, HUNTER_LIST},
        {"A hunter in sight",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseThreatCloseness, 0.6}, {SenseAge, 0.2}},
         {Flee, Rest, GoEast, GoWest}, ANIMAL_LIST},
        {"Wounded, a dangerous hunter beside them",
         {{SenseHunger, 0.3}, {SenseHealth, 0.4}, {SenseThreatNear, 0.25}, {SenseThreatDanger, 0.4},
          {SenseThreatCloseness, 1}, {SenseAge, 0.2}},
         {Flee, Rest, GoEast, GoWest}, ANIMAL_LIST},
        {"Wounded, the attacker still beside them",
         {{SenseHunger, 0.3}, {SenseHealth, 0.5}, {SenseThreatNear, 0.25}, {SenseThreatDanger, 0.3},
          {SenseThreatCloseness, 1}, {SenseAssailantNear, 1}, {SenseCounterFrailty, 0.3}, {SenseAge, 0.2}},
         {Defend, Flee, Rest, GoEast, GoWest}, ANIMAL_LIST},
        {"Crowded territory, a rival beside them",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseRivalry, 1}, {SensePreyNear, 0.25}, {SensePreyFrailty, 0.3},
          {SensePreyCloseness, 1}, {SenseThreatNear, 0.25}, {SenseThreatDanger, 0.3}, {SenseThreatCloseness, 1},
          {SenseAge, 0.2}},
         {Attack, Flee, Rest, GoEast, GoWest}, HUNTER_LIST},
        {"Alone, its herd in range",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseHerdCloseness, 0.4}, {SenseAge, 0.2}},
         {Herd, Rest, GoEast, GoWest}, ANIMAL_LIST},
        {"A herd-mate struck close by",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseAlarm, 1}, {SenseHerdNear, 1.0 / 3}, {SenseThreatCloseness, 0.6},
          {SenseCounterFrailty, 0.3}, {SenseAge, 0.2}},
         {Defend, Flee, Herd, Rest, GoEast, GoWest}, ANIMAL_LIST},
        {"Peckish, frail prey beside them",
         {{SenseHunger, 0.5}, {SenseHealth, 1}, {SensePreyNear, 0.25}, {SensePreyFrailty, 0.5}, {SensePreyCloseness, 1},
          {SenseAge, 0.2}},
         {Attack, Hunt, Rest, GoEast, GoWest}, HUNTER_LIST},
        // The climate: off home each way, far off, and at home with the weather turning toward it or not,
        // so the report shows whether brains follow the warmth senses or only a habit of one direction.
        {"Too cold, warmer to the south",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseClimateDiscomfort, 0.5}, {SenseComfortCloseness, 0.6},
          {SenseWarmth, -0.5}, {SenseWarmthNorth, -1}, {SenseHomeDirection, 1}, {SenseHomeDistance, 0.3}, {SenseLatitude, -0.3}, {SenseAge, 0.2}},
         {GoNorth, GoSouth, WalkNorth, WalkSouth, GoEast, GoWest, Rest}, ANIMAL_LIST},
        {"Too cold, warmer to the north",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseClimateDiscomfort, 0.5}, {SenseComfortCloseness, 0.6},
          {SenseWarmth, -0.5}, {SenseWarmthSouth, -1}, {SenseHomeDirection, -1}, {SenseHomeDistance, 0.3}, {SenseLatitude, 0.3}, {SenseAge, 0.2}},
         {GoNorth, GoSouth, WalkNorth, WalkSouth, GoEast, GoWest, Rest}, ANIMAL_LIST},
        {"Too hot, cooler to the north",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseClimateDiscomfort, 0.5}, {SenseComfortCloseness, 0.6},
          {SenseWarmth, 0.5}, {SenseWarmthSouth, 0.5}, {SenseHomeDirection, -1}, {SenseHomeDistance, 0.3}, {SenseLatitude, 0.1}, {SenseAge, 0.2}},
         {GoNorth, GoSouth, WalkNorth, WalkSouth, GoEast, GoWest, Rest}, ANIMAL_LIST},
        {"Too hot, cooler to the south",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseClimateDiscomfort, 0.5}, {SenseComfortCloseness, 0.6},
          {SenseWarmth, 0.5}, {SenseWarmthNorth, 0.5}, {SenseHomeDirection, 1}, {SenseHomeDistance, 0.3}, {SenseLatitude, -0.1}, {SenseAge, 0.2}},
         {GoNorth, GoSouth, WalkNorth, WalkSouth, GoEast, GoWest, Rest}, ANIMAL_LIST},
        {"Far too cold, warmer to the south",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseClimateDiscomfort, 1}, {SenseComfortCloseness, 0.3},
          {SenseWarmth, -1}, {SenseWarmthNorth, -1}, {SenseWarmthSouth, -0.5}, {SenseHomeDirection, 1}, {SenseHomeDistance, 0.6}, {SenseLatitude, -0.5}, {SenseAge, 0.2}},
         {GoNorth, GoSouth, WalkNorth, WalkSouth, GoEast, GoWest, Rest}, ANIMAL_LIST},
        {"At home, cold coming from the north",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseComfortCloseness, 1}, {SenseWarmthNorth, -0.5}, {SenseLatitude, -0.3}, {SenseAge, 0.2}},
         {GoNorth, GoSouth, WalkNorth, WalkSouth, GoEast, GoWest, Rest}, ANIMAL_LIST},
        {"At home, cold coming from the south",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseComfortCloseness, 1}, {SenseWarmthSouth, -0.5}, {SenseLatitude, 0.3}, {SenseAge, 0.2}},
         {GoNorth, GoSouth, WalkNorth, WalkSouth, GoEast, GoWest, Rest}, ANIMAL_LIST},
        {"At home, the same all around",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseComfortCloseness, 1}, {SenseLatitude, -0.3}, {SenseAge, 0.2}},
         {GoNorth, GoSouth, WalkNorth, WalkSouth, GoEast, GoWest, Rest}, ANIMAL_LIST},
        {"Starving, food scarce, a plant beside them",
         {{SenseHunger, 0.85}, {SenseHealth, 1}, {SensePlantCloseness, 1}, {SenseAge, 0.2}},
         {Uproot, Browse, Rest, GoEast, GoWest}, GRAZER_ONLY},
        {"Fed and safe, its hungry infants about it",
         {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseYoungNear, 2.0 / 3}, {SenseAge, 0.3}},
         {FeedYoung, Rest, GoEast, GoWest}, ANIMAL_LIST},
        {"Nothing around", {{SenseHunger, 0.3}, {SenseHealth, 1}, {SenseAge, 0.2}}, {Rest, GoEast, GoWest}, ANIMAL_LIST},
    };
    return list;
}

constexpr int COLUMN = 11;  // characters to an action's column in the report: room for the longest name

std::string pad_left(const std::string& text, int width) {
    return static_cast<int>(text.size()) >= width ? text : std::string(static_cast<std::size_t>(width) - text.size(), ' ') + text;
}
std::string pad_right(const std::string& text, int width) {
    return static_cast<int>(text.size()) >= width ? text : text + std::string(static_cast<std::size_t>(width) - text.size(), ' ');
}

using Group = std::tuple<int, int, int>;  // (species, trait or -1, level)
using Sample = std::array<double, ACTION_COUNT>;

double mean(const std::vector<Sample>& samples, int action) {
    std::vector<double> values;
    for (const auto& sample : samples) values.push_back(sample[action]);
    return py_sum(values) / static_cast<double>(samples.size());
}

std::string label_of(const Group& group) {
    const auto [species, trait, level] = group;
    if (trait < 0) return group_label(static_cast<Species>(species));
    return group_label(static_cast<Species>(species), trait, static_cast<Level>(level));
}

// Every situation asks about an animal rested and awake, standing where it stands now: an exhausted or sleeping one
// can only rest, so its brain has learnt nothing else there, and asking it so would show nothing but resting.
// What it values its options at as it chooses: its brain's values and its instincts, its hunger from the situation's and
// its young, if about it, half hungry.
Values as_chosen(const Bitzkey& animal, const Senses& senses, std::span<const int> options) {
    Values valued = animal.brain->values(senses);
    add_instincts(animal, valued, options, senses[SenseHunger] * 100, senses[SenseYoungNear] > 0 ? 50.0 : 0.0);
    return valued;
}

void awake(const Bitzkey& animal, Senses& senses) {
    senses[SenseStamina] = 1.0;
    senses[SenseNight] = animal.nocturnal ? 1.0 : 0.0;
    senses[SenseSeaHere] = animal.wet;
}

}  // namespace

std::string brain_report(int turn, std::span<const Bitzkey* const> animals, const Insights& insights) {
    std::vector<const Bitzkey*> thinkers;
    for (const Bitzkey* animal : animals) {
        if (animal->brain) thinkers.push_back(animal);
    }
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, "BITZKEY BRAIN REPORT - turn %05d, %zu thinking animals", turn, thinkers.size());
    std::vector<std::string> lines = {
        buffer,
        "Figures are the average chance an animal would pick each action in the situation.",
        "Groups smaller than 5 animals are left out.",
    };
    for (const Scenario& scenario : scenarios()) {
        std::vector<int> options(scenario.actions.begin(), scenario.actions.end());
        const int headline = scenario.actions.front();
        std::map<Group, std::vector<Sample>> per_group;
        std::vector<Group> order;  // groups in the order first seen
        for (const Bitzkey* animal : thinkers) {
            if (std::find(scenario.species.begin(), scenario.species.end(), animal->species) == scenario.species.end()) continue;
            Senses senses{};
            for (const auto& [sense, value] : scenario.situation) senses[sense] = value;
            for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) senses[SenseMind + trait] = animal->feeling(trait);
            gene_senses(animal->dna, senses);
            awake(*animal, senses);
            Sample named{};
            for (const auto& [option, chance] : choice_odds(as_chosen(*animal, senses, options), options)) named[option] = chance;
            const Profile profile = animal->profile();
            const int species = static_cast<int>(animal->species);
            std::vector<Group> groups = {{species, -1, 0}};
            for (int trait = 0; trait < INSIGHT_TRAIT_COUNT; ++trait) groups.emplace_back(species, trait, static_cast<int>(band(profile[trait])));
            for (const Group& group : groups) {
                auto [it, added] = per_group.try_emplace(group);
                if (added) order.push_back(group);
                it->second.push_back(named);
            }
        }

        lines.emplace_back("");
        lines.emplace_back(scenario.title);
        std::string header = "  " + std::string(18, ' ');
        for (const int action : options) header += pad_left(std::string(ACTIONS[action]), COLUMN);
        lines.push_back(header);
        for (const Species species : scenario.species) {
            const auto found = per_group.find({static_cast<int>(species), -1, 0});
            if (found == per_group.end()) {
                lines.push_back("  " + pad_right(std::string(species_name(species)), 10) + " (none)");
                continue;
            }
            std::string averages;
            for (const int action : options) averages += py_percent(mean(found->second, action), COLUMN - 1) + " ";
            lines.push_back("  " + pad_right(std::string(species_name(species)), 10) +
                            pad_left("(n=" + std::to_string(found->second.size()) + ")", 8) + averages);
        }

        std::vector<std::tuple<double, std::string, std::size_t>> ranked;
        for (const Group& group : order) {
            const auto& samples = per_group[group];
            if (std::get<1>(group) >= 0 && static_cast<double>(samples.size()) >= MIN_SAMPLE) {
                ranked.emplace_back(mean(samples, headline), label_of(group), samples.size());
            }
        }
        std::sort(ranked.begin(), ranked.end());
        if (!ranked.empty()) {
            const auto join = [](auto begin, auto end) {
                std::string text;
                for (auto it = begin; it != end; ++it) {
                    if (!text.empty()) text += ", ";
                    text += std::get<1>(*it) + " " + py_percent(std::get<0>(*it), 0) + " (n=" +
                            std::to_string(std::get<2>(*it)) + ")";
                }
                return text;
            };
            const std::size_t shown = std::min<std::size_t>(3, ranked.size());
            lines.push_back("  Least keen to " + std::string(ACTIONS[headline]) + ": " + join(ranked.begin(), ranked.begin() + shown));
            lines.push_back("  Most keen to " + std::string(ACTIONS[headline]) + ":  " + join(ranked.rbegin(), ranked.rbegin() + shown));
        }
    }

    lines.emplace_back("");
    lines.emplace_back("Temperament shaped by experience (average shift from inborn values, 0..7 scale)");
    std::string header = "  " + std::string(10, ' ');
    for (const auto trait : TEMPERAMENT) header += pad_left(std::string(trait.substr(0, 5)), 7);
    lines.push_back(header);
    for (const Species species : ANIMALS) {
        std::vector<const Bitzkey*> members;
        for (const Bitzkey* animal : thinkers) {
            if (animal->species == species) members.push_back(animal);
        }
        if (members.empty()) continue;
        std::string shifts;
        for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) {
            std::vector<double> values;
            for (const Bitzkey* animal : members) values.push_back(animal->mind[trait] - animal->inborn[trait]);
            std::snprintf(buffer, sizeof buffer, "%+7.2f", py_sum(values) / static_cast<double>(members.size()));
            shifts += buffer;
        }
        lines.push_back("  " + pad_right(std::string(species_name(species)), 10) + shifts);
    }

    lines.emplace_back("");
    lines.emplace_back("Observed behaviour, recent turns (share of chances taken)");
    header = "  " + std::string(10, ' ');
    for (const auto action : ACTIONS) header += pad_left(std::string(action), COLUMN);
    lines.push_back(header);
    for (const Species species : ANIMALS) {
        std::string cells;
        for (int action = 0; action < ACTION_COUNT; ++action) {
            const auto result = insights.rate(action, species);
            cells += result.share ? py_percent(*result.share, COLUMN - 1) + " " : pad_left("-", COLUMN);
        }
        lines.push_back("  " + pad_right(std::string(species_name(species)), 10) + cells);
    }
    std::string text;
    for (const auto& line : lines) text += line + "\n";
    return text;
}

namespace {
constexpr std::array<std::string_view, 4> SPECIES_ORDER = {"plant", "grazer", "omnivore", "predator"};
constexpr std::array<std::string_view, ACTION_COUNT> SHORT_ACTIONS = {
    "eat", "forage", "browse", "uproot", "scaven", "feast", "rest", "court", "breed", "hunt", "attack", "defend", "flee", "herd",
    "go N", "go S", "go E", "go W", "walk N", "walk S", "walk E", "walk W", "revisit", "feed"};
constexpr int CELL = 7;        // characters to a column of figures
constexpr int SITUATION = 42;  // characters to the situations' column

std::string upper_case(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}
}  // namespace

std::string_view short_action(int action) { return SHORT_ACTIONS[static_cast<std::size_t>(action)]; }

void DeathTally::add(const Death& death) {
    causes[death.cause][static_cast<std::size_t>(death.species)] += 1;
    if (death.age_days >= 0) {
        std::size_t band = AGE_BANDS.size() - 1;
        while (band > 0 && death.age_days < AGE_BANDS[band]) --band;
        ages[band][static_cast<std::size_t>(death.species)] += 1;
    }
    if (death.killer < 0) return;
    whom[static_cast<std::size_t>(death.species)][static_cast<std::size_t>(death.killer_species)] += 1;
    activities[death.activity][static_cast<std::size_t>(death.killer_species)] += 1;
    const auto [found, added] = by_killer.try_emplace(death.killer, Killer{death.killer_species});
    killers += added;  // a killer let go of can kill no more, so none is counted twice
    found->second.kills += 1;
    found->second.doing[death.activity] += 1;
}

void DeathTally::let_go(const std::function<bool(int)>& alive) {
    std::vector<std::pair<int, int>> gone;  // (kills, identifier)
    for (const auto& [identifier, killer] : by_killer) {
        if (!alive(identifier)) gone.emplace_back(killer.kills, identifier);
    }
    if (gone.size() <= static_cast<std::size_t>(DEADLIEST)) return;
    std::stable_sort(gone.begin(), gone.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t index = DEADLIEST; index < gone.size(); ++index) by_killer.erase(gone[index].second);
}

DeathReport death_report(const DeathTally& tally) {
    DeathReport report;
    // Rows by a label, the most common first, each counting its deaths by species.
    const auto ranked = [](const std::map<std::string, DeathTally::Counts, std::less<>>& rows) {
        std::vector<DeathReport::Row> result;
        for (const auto& [label, counts] : rows) {
            DeathReport::Row row{label, counts};
            for (const int count : counts) row.total += count;
            if (row.total > 0) result.push_back(std::move(row));
        }
        std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.total > b.total; });
        return result;
    };
    report.causes = ranked(tally.causes);
    for (const DeathReport::Row& row : report.causes) {
        for (std::size_t species = 0; species < row.counts.size(); ++species) report.all.counts[species] += row.counts[species];
        report.all.total += row.total;
    }
    for (std::size_t species = 0; species < tally.whom.size(); ++species) {
        DeathReport::Row row{std::string(SPECIES_ORDER[species]), tally.whom[species]};
        for (const int count : row.counts) row.total += count;
        report.kills += row.total;
        if (row.total > 0) report.whom.push_back(std::move(row));
    }
    report.activities = ranked(tally.activities);
    for (std::size_t band = 0; band < tally.ages.size(); ++band) {
        DeathReport::Row row{AGE_BAND_NAMES[band], tally.ages[band]};
        for (const int count : row.counts) row.total += count;
        if (row.total > 0) report.ages.push_back(std::move(row));
    }
    report.killers = tally.killers;
    for (const auto& [identifier, killer] : tally.by_killer) {
        DeathReport::Killer entry{identifier, killer.species, killer.kills, {killer.doing.begin(), killer.doing.end()}};
        std::stable_sort(entry.doing.begin(), entry.doing.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        report.deadliest.push_back(std::move(entry));
    }
    std::stable_sort(report.deadliest.begin(), report.deadliest.end(), [](const auto& a, const auto& b) { return a.kills > b.kills; });
    if (report.deadliest.size() > static_cast<std::size_t>(DEADLIEST)) report.deadliest.resize(DEADLIEST);
    return report;
}

DeathReport death_report(const std::vector<Death>& deaths) {
    DeathTally tally;
    for (const Death& death : deaths) tally.add(death);
    return death_report(tally);
}

std::vector<std::string> death_report_lines(const std::vector<Death>& deaths) {
    DeathTally tally;
    for (const Death& death : deaths) tally.add(death);
    return death_report_lines(tally);
}

std::vector<std::string> death_report_lines(const DeathTally& tally) {
    const DeathReport report = death_report(tally);
    const auto table = [](const std::string& title, const std::vector<DeathReport::Row>& rows, std::vector<std::string>& lines) {
        std::string heading = pad_right(title, 18);
        for (const auto species : SPECIES_ORDER) heading += pad_left(upper_case(species), 10);
        lines.push_back(heading + pad_left("ALL", 10));
        for (const DeathReport::Row& row : rows) {
            std::string line = pad_right(row.label, 18);
            for (const int count : row.counts) line += pad_left(std::to_string(count), 10);
            lines.push_back(line + pad_left(std::to_string(row.total), 10));
        }
    };
    std::vector<std::string> lines;
    table("CAUSE", report.causes, lines);
    table("", {report.all}, lines);
    lines.erase(lines.end() - 2);  // the totals beneath the causes, under the one heading
    if (!report.ages.empty()) {
        lines.insert(lines.end(), {"", "AGE AT DEATH, by species"});
        table("AGE", report.ages, lines);
    }
    if (report.kills == 0) {
        lines.insert(lines.end(), {"", "Nothing has killed anything yet."});
        return lines;
    }
    lines.insert(lines.end(), {"", "WHO KILLED WHOM: " + std::to_string(report.kills) +
                                       " killed, by the species of the dead (rows) and of their killers (columns)"});
    table("THE DEAD", report.whom, lines);
    lines.insert(lines.end(), {"", "WHAT THE KILLERS WERE DOING, by the killers' species"});
    table("ACTIVITY", report.activities, lines);
    lines.insert(lines.end(), {"", "THE DEADLIEST KILLERS, " + std::to_string(report.deadliest.size()) + " of " +
                                       std::to_string(report.killers)});
    for (const DeathReport::Killer& killer : report.deadliest) {
        std::string what;
        for (const auto& [name, count] : killer.doing) what += (what.empty() ? "" : ", ") + name + " " + std::to_string(count);
        char number[16];
        std::snprintf(number, sizeof number, "#%03d ", killer.identifier);
        lines.push_back(number + pad_right(std::string(species_name(killer.species)), 10) + pad_left(std::to_string(killer.kills), 4) +
                        " killed   " + what);
    }
    return lines;
}

std::vector<BrainRow> brain_matrix(const Bitzkey& animal) {
    std::vector<BrainRow> rows;
    for (const Scenario& scenario : scenarios()) {
        if (std::find(scenario.species.begin(), scenario.species.end(), animal.species) == scenario.species.end()) continue;
        Senses senses{};
        for (const auto& [sense, value] : scenario.situation) senses[sense] = value;
        for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) senses[SenseMind + trait] = animal.feeling(trait);
        gene_senses(animal.dna, senses);
        awake(animal, senses);
        const std::vector<int> options(scenario.actions.begin(), scenario.actions.end());
        rows.push_back({scenario.title, choice_odds(as_chosen(animal, senses, options), options)});
    }
    return rows;
}

std::vector<std::string> brain_matrix_lines(const Bitzkey& animal) {
    const std::vector<BrainRow> rows = brain_matrix(animal);
    std::vector<int> used;
    for (int action = 0; action < ACTION_COUNT; ++action) {
        for (const BrainRow& row : rows) {
            if (std::any_of(row.chances.begin(), row.chances.end(), [action](const auto& entry) { return entry.first == action; })) {
                used.push_back(action);
                break;
            }
        }
    }
    std::string heading = pad_right("SITUATION", SITUATION);
    for (const int action : used) heading += pad_left(std::string(short_action(action)), CELL);
    std::vector<std::string> lines = {heading};
    for (const BrainRow& row : rows) {
        int best = row.chances.front().first;
        double most = row.chances.front().second;
        for (const auto& [action, chance] : row.chances) {  // the first of the likeliest, as Python's max
            if (chance > most) {
                best = action;
                most = chance;
            }
        }
        std::string line = pad_right(row.situation.substr(0, SITUATION - 1), SITUATION);
        for (const int action : used) {
            const auto found = std::find_if(row.chances.begin(), row.chances.end(), [action](const auto& entry) { return entry.first == action; });
            line += found == row.chances.end() ? pad_left("· ", CELL + 1)  // the dot is two bytes in UTF-8
                                               : pad_left(py_percent(found->second, 0) + (action == best ? "*" : " "), CELL);
        }
        lines.push_back(line);
    }
    return lines;
}

}  // namespace bitzkey
