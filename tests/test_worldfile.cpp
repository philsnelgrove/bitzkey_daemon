// Saving a world whole and loading it back: core/WorldFile.

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

#include "core/Rules.hpp"
#include "core/Simulation.hpp"
#include "core/WorldFile.hpp"

using namespace bitzkey;

TEST_CASE("a world loaded carries on exactly as it would have") {
    rules::apply(rules::current());  // the tests' own settings, held within the rules' limits as loading holds them
    Simulation world(Simulation::Settings{30, 30, 40, 5});
    for (int turn = 0; turn < 40; ++turn) world.advance();
    const std::string data = WorldFile::bytes(world);
    const auto loaded = WorldFile::from_bytes(data);
    CHECK(WorldFile::bytes(*loaded) == data);  // every Bitzkey, brain, seed and tuft as it was
    CHECK((loaded->turn() == world.turn() && loaded->frontier() == world.turn() && loaded->earliest() == world.turn()));
    for (int turn = 0; turn < 30; ++turn) {
        world.advance();
        loaded->advance();
    }
    CHECK(WorldFile::bytes(*loaded) == WorldFile::bytes(world));
}

TEST_CASE("a world brings its rules, but not how this computer runs it") {
    rules::Values values = {{"CORPSE_TURNS", 60}, {"HISTORY_TURNS", 50}};
    rules::apply(values);
    Simulation world(Simulation::Settings{20, 20, 10, 2});
    const std::string data = WorldFile::bytes(world);
    rules::apply({{"HISTORY_TURNS", 80}});
    WorldFile::from_bytes(data);
    CHECK(rules::current().at("CORPSE_TURNS") == 60);
    CHECK(rules::current().at("HISTORY_TURNS") == 80);
    rules::apply({});
}

TEST_CASE("what isn't a world says so") {
    CHECK_THROWS_WITH_AS(WorldFile::from_bytes("not a world at all"), "This isn't a Bitzkey world file.", std::runtime_error);
    Simulation world(Simulation::Settings{20, 20, 10, 2});
    const std::string data = WorldFile::bytes(world);
    CHECK_THROWS_WITH_AS(WorldFile::from_bytes(std::string_view(data).substr(0, data.size() - 4)), "The world file is cut short.",
                         std::runtime_error);
}
