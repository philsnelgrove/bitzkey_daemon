// The advanced rules, ported from the Python tests.

#include <doctest/doctest.h>

#include <algorithm>

#include "core/Brain.hpp"
#include "core/Rules.hpp"
#include "core/Simulation.hpp"

using namespace bitzkey;

namespace {

// Puts every rule back to its default when a test ends.
struct Restore {
    ~Restore() { rules::apply({}); }
};

int corpses_kept(int turns) {
    Simulation simulation(Simulation::Settings{30, 30, 40, 3, 0.0});
    for (int turn = 0; turn < turns; ++turn) simulation.advance();
    int corpses = 0;
    for (const auto& agent : simulation.agents()) corpses += !agent->alive();
    return corpses;
}

}  // namespace

TEST_CASE("every default is within its rule and of its kind") {
    CHECK(rules::all().size() == 205);
    const rules::Rule& turns = *rules::find("HISTORY_TURNS");
    CHECK((rules::shown(turns, turns.maximum) == "ALL" && rules::shown(turns, 100) == "100"));  // its top keeps every turn
    CHECK(!rules::warning("HISTORY_MEMORY").empty());
    for (const rules::Rule& rule : rules::all()) {
        CAPTURE(rule.key);
        const double value = rules::defaults().at(rule.key);
        CHECK(rule.minimum <= value);
        CHECK(value <= rule.maximum);
        CHECK(rule.clamp(value) == value);
        CHECK(rules::find(rule.key) == &rule);
    }
    CHECK(rules::find("NO_SUCH_RULE") == nullptr);
}

TEST_CASE("applying sets constants, arrays and tables, and resets the rest") {
    Restore restore;
    rules::apply({{"CORPSE_TURNS", 60}, {"LITTER_RANGE.1", 9}, {"TERRITORY.omnivore.most", 7}, {"GRAZING.grazer", 2.0},
                  {"EXPERIENCES.kill.fear", -0.1}, {"MUTATION", 0.5}});
    CHECK(CORPSE_TURNS == 60);
    CHECK(LITTER_RANGE[1] == 9);
    CHECK(TERRITORY[1].most == 7);
    CHECK(GRAZING[1] == 2.0);
    CHECK(EXPERIENCES[0][1].change == -0.1);
    CHECK(MUTATION == 0.5);

    rules::apply({{"LITTER_RANGE.1", 9}});
    CHECK(CORPSE_TURNS == rules::defaults().at("CORPSE_TURNS"));
    CHECK(rules::changed(rules::current()) == rules::Values{{"LITTER_RANGE.1", 9}});
}

TEST_CASE("values are clamped, whole-number rules rounded") {
    Restore restore;
    rules::apply({{"CORPSE_TURNS", 10'000}, {"AGEING", -3.0}, {"SEED_REACH", 2.5}});
    CHECK(CORPSE_TURNS == 500);
    CHECK(AGEING == 0.0);
    CHECK(SEED_REACH == 2);  // half to even, as Python rounds
}

TEST_CASE("problems name pairs out of order or alike") {
    rules::Values values = rules::defaults();
    CHECK(rules::problems(values).empty());
    values["LITTER_RANGE.0"] = 5;
    values["LITTER_RANGE.1"] = 2;
    CHECK(rules::problems(values) == std::vector<std::string>{"Smallest litter must not exceed largest litter"});
    values = rules::defaults();
    values["METABOLISM.0"] = values["METABOLISM.1"] = 1.0;
    CHECK(rules::problems(values) ==
          std::vector<std::string>{"Metabolism, smallest must differ from metabolism, largest"});
    values = rules::defaults();
    values["ARCTIC_SHARE"] = 0.4;
    values["EQUATOR_SHARE"] = 0.3;
    CHECK(rules::problems(values) == std::vector<std::string>{"The two arctic zones and the equator must fit in the map together"});
    values["EQUATOR_SHARE"] = 0.2;
    CHECK(rules::problems(values).empty());
}

TEST_CASE("values show as the Python version shows them") {
    CHECK(rules::shown(*rules::find("CORPSE_TURNS"), 30) == "30");
    CHECK(rules::shown(*rules::find("GRASS_REGROWTH"), 0.004) == "0.004");
    CHECK(rules::shown(*rules::find("GRAZER_SPEED"), 2.0) == "2");
    CHECK(rules::shown(*rules::find("EXPERIENCES.kill.fear"), -0.0005) == "-0.0005");
    CHECK(rules::find("EXPERIENCES.wounded_alone.sociability")->label == "Wounded alone: sociability");
}

TEST_CASE("a world plays by the rules set when it runs") {
    Restore restore;
    const int usual = corpses_kept(40);
    rules::apply({{"CORPSE_TURNS", 1}});
    CHECK(corpses_kept(40) < usual);
}

TEST_CASE("a running world works out afresh what it settled from the rules as it began") {
    Simulation simulation(Simulation::Settings{30, 30, 20, 3, 10});  // 90 tufts to start with
    simulation.advance();
    const Bitzkey& animal = **std::find_if(simulation.agents().begin(), simulation.agents().end(),
                                           [](const auto& agent) { return agent->species != Species::Plant; });
    const double metabolism = animal.metabolism;
    rules::apply({{"GRASS_REGROWTH", 0.05}, {"ARCTIC_SHARE", 0.0}, {"METABOLISM.1", 0.1}});
    simulation.rules_changed();
    CHECK(simulation.grass_regrowth() == doctest::Approx(90 * 0.05));
    CHECK(animal.metabolism < metabolism);  // what its genome makes of it, under the rules now
    CHECK_FALSE(rules::locked("USE_GPU", true).empty());  // where a world thinks is set as it starts
    const int identifier = animal.identifier;
    const double now = animal.metabolism;
    simulation.advance();
    simulation.rewind();  // brought back from a turn recorded before the change, it still plays by the rules now
    simulation.rewind();
    const Bitzkey& again = **std::find_if(simulation.agents().begin(), simulation.agents().end(),
                                          [&](const auto& agent) { return agent->identifier == identifier; });
    CHECK(again.metabolism == now);
    rules::apply({});
}
