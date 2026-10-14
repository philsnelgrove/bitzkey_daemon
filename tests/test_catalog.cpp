// The catalog of favourite Bitzkeys, ported from the Python tests.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "core/Catalog.hpp"

using namespace bitzkey;

namespace {

struct TemporaryCatalog {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "bitzkey-test-catalog.json";
    TemporaryCatalog() { std::filesystem::remove(path); }
    ~TemporaryCatalog() { std::filesystem::remove(path); }
};

std::vector<const Bitzkey*> thinkers(Simulation& simulation) {
    std::vector<const Bitzkey*> found;
    for (const Bitzkey* agent : simulation.living()) {
        if (agent->brain) found.push_back(agent);
    }
    return found;
}

}  // namespace

TEST_CASE("a catalog entry keeps the brain, the temperament and what the inspector shows") {
    TemporaryCatalog file;
    Simulation simulation(Simulation::Settings{30, 30, 40, 3});
    for (int turn = 0; turn < 5; ++turn) simulation.advance();
    const Bitzkey& animal = *thinkers(simulation).front();
    CHECK(catalog::add({catalog::entry_for(animal, simulation.turn())}, file.path) == 1);
    const auto entries = catalog::load(file.path);
    REQUIRE(entries.size() == 1);
    const Bitzkey rebuilt = catalog::inspected(entries.front());
    CHECK(rebuilt.dna == animal.dna);
    REQUIRE(rebuilt.brain);
    Senses senses{};
    senses.fill(0.3);
    for (int action = 0; action < ACTION_COUNT; ++action) {
        CHECK(rebuilt.brain->values(senses)[action] == doctest::Approx(animal.brain->values(senses)[action]).epsilon(1e-5));
    }
    for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) CHECK(rebuilt.mind[trait] == doctest::Approx(animal.mind[trait]).epsilon(1e-3));
    CHECK((rebuilt.identifier == animal.identifier && rebuilt.age == animal.age && rebuilt.generation == animal.generation &&
           rebuilt.last_action == animal.last_action && rebuilt.odds_offered == animal.odds_offered));
    CHECK(rebuilt.health == doctest::Approx(animal.health).epsilon(1e-3));
    char name[16];
    std::snprintf(name, sizeof name, "#%03d", animal.identifier);
    CHECK(catalog::describe(entries.front()).starts_with(name));
    std::ifstream saved(file.path);
    const std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    CHECK(import_bitzkeys(text).front().brain);  // the catalog is itself an export
}

TEST_CASE("the same Bitzkey is catalogued once, and entries can be removed") {
    TemporaryCatalog file;
    Simulation simulation(Simulation::Settings{30, 30, 40, 3});
    for (int turn = 0; turn < 5; ++turn) simulation.advance();
    const auto animals = thinkers(simulation);
    const auto entry = catalog::entry_for(*animals[0], simulation.turn());
    CHECK(catalog::add({entry, entry}, file.path) == 1);
    CHECK(catalog::add({entry, catalog::entry_for(*animals[1], simulation.turn())}, file.path) == 1);
    CHECK(catalog::load(file.path).size() == 2);
    catalog::remove(0, file.path);
    CHECK(catalog::inspected(catalog::load(file.path).front()).identifier == animals[1]->identifier);
    CHECK(catalog::load(std::filesystem::temp_directory_path() / "bitzkey-no-such-catalog.json").empty());
    std::ofstream(file.path) << "{not json";
    CHECK_THROWS_AS(catalog::load(file.path), std::invalid_argument);
}
