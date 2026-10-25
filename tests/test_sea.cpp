// The sea, and the terrain gene that says who may go into it.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <tuple>

#include "core/Brain.hpp"
#include "core/PyFormat.hpp"
#include "core/Simulation.hpp"
#include "core/WorldFile.hpp"

using namespace bitzkey;

namespace {

Simulation seaside(int seed = 3, int population = 0) {
    Simulation::Settings settings{40, 40, population, static_cast<std::uint64_t>(seed)};
    settings.ocean = DEFAULT_OCEAN;
    Simulation simulation(settings);
    if (!population) simulation.clear_grass();
    return simulation;
}

// A square of land with the sea just east of it, away from the top and bottom.
Cell coast(const Simulation& simulation) {
    for (int y = 5; y < simulation.height() - 5; ++y) {
        for (int x = 1; x < simulation.width() - 2; ++x) {
            if (!simulation.sea(x - 1, y) && !simulation.sea(x, y) && simulation.sea(x + 1, y)) return {x, y};
        }
    }
    FAIL("no coast");
    return {};
}

// A walk from a square onto a neighbour of land, or of sea.
int walk_onto(const Simulation& simulation, Cell from, bool sea) {
    const std::array<std::tuple<int, int, int>, 4> ways = {{{WalkNorth, 0, -1}, {WalkSouth, 0, 1}, {WalkEast, 1, 0}, {WalkWest, -1, 0}}};
    for (const auto& [action, dx, dy] : ways) {
        if (simulation.sea(from.x + dx, from.y + dy) == sea) return action;
    }
    FAIL("no such neighbour");
    return Rest;
}

Dna of(Species species, Terrain terrain, std::initializer_list<std::pair<std::string_view, int>> traits = {}) {
    return with_terrain(encode_dna(species, traits), terrain);
}

}  // namespace

TEST_CASE("the sea covers its share of the map, laid out by the seed") {
    const Simulation first = seaside(3), again = seaside(3), other = seaside(4);
    const auto under = std::count(first.sea_map().begin(), first.sea_map().end(), 1);
    CHECK(static_cast<double>(under) == py_round(40 * 40 * DEFAULT_OCEAN / 100));
    CHECK(first.sea_map() == again.sea_map());
    CHECK(first.sea_map() != other.sea_map());
    CHECK(first.ocean() == DEFAULT_OCEAN);
    // Broad seas and lands, not a scatter: most squares share their neighbour's.
    int alike = 0;
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 39; ++x) alike += first.sea(x, y) == first.sea(x + 1, y);
    }
    CHECK(alike > 0.85 * 40 * 39);
    const Simulation dry(Simulation::Settings{20, 20, 0, 3});
    CHECK_FALSE(dry.has_sea());
    CHECK_FALSE(dry.sea(3, 3));
}

TEST_CASE("the sea leaves the world's own draws as they were") {
    // The same seed, sea or not, draws its starting genomes alike: the sea comes from a stream of its own.
    Simulation::Settings settings{40, 40, 0, 9};
    Simulation dry(settings);
    settings.ocean = 50;
    Simulation wet(settings);
    CHECK(dry.random().getrandbits(64) == wet.random().getrandbits(64));
}

TEST_CASE("each starts where its terrain lets it live, and a world without sea starts all on the land") {
    const Simulation simulation = seaside(5, 300);
    std::array<int, 4> terrains{};
    for (const auto& agent : simulation.agents()) {
        CHECK(agent->can_be(simulation.sea(agent->x, agent->y)));
        CHECK(agent->wet == simulation.sea(agent->x, agent->y));
        ++terrains[static_cast<std::size_t>(agent->terrain)];
    }
    CHECK(std::all_of(terrains.begin(), terrains.end(), [](int count) { return count > 0; }));
    const Simulation dry(Simulation::Settings{40, 40, 300, 5});
    for (const auto& agent : dry.agents()) CHECK(agent->terrain == Terrain::Terrestrial);
}

TEST_CASE("the terrestrial keep to the land and the aquatic to the sea; the others go freely") {
    Simulation simulation = seaside();
    const Cell shore = coast(simulation);
    Bitzkey& walker = simulation.add_agent(of(Species::Grazer, Terrain::Terrestrial), shore.x, shore.y);
    CHECK_FALSE(simulation.step(walker, shore.x + 1, shore.y));
    CHECK(walker.x == shore.x);
    Bitzkey& fish = simulation.add_agent(of(Species::Grazer, Terrain::Aquatic), shore.x + 1, shore.y);
    CHECK(fish.wet);
    CHECK_FALSE(simulation.step(fish, shore.x, shore.y));
    for (const Terrain free : {Terrain::Avian, Terrain::Amphibious}) {
        Bitzkey& rover = simulation.add_agent(of(Species::Grazer, free), shore.x, shore.y);
        CHECK(simulation.step(rover, shore.x + 1, shore.y));
        CHECK(rover.wet);
        CHECK(simulation.step(rover, shore.x, shore.y));
        CHECK_FALSE(rover.wet);
    }
}

TEST_CASE("a hunter reaches only what stands where it may go, and a flyer only while it rests") {
    Simulation simulation = seaside();
    const Cell shore = coast(simulation);
    Bitzkey& shark = simulation.add_agent(of(Species::Predator, Terrain::Aquatic), shore.x + 1, shore.y);
    Bitzkey& wolf = simulation.add_agent(of(Species::Predator, Terrain::Terrestrial), shore.x, shore.y);
    Bitzkey& deer = simulation.add_agent(of(Species::Grazer, Terrain::Terrestrial), shore.x, shore.y);
    CHECK_FALSE(Simulation::is_prey(shark, deer));  // on the land, out of the shark's reach
    CHECK(Simulation::is_prey(wolf, deer));
    Bitzkey& gull = simulation.add_agent(of(Species::Grazer, Terrain::Avian), shore.x, shore.y);
    CHECK_FALSE(Simulation::is_prey(wolf, gull));  // aloft
    gull.last_action = "rested";
    CHECK(Simulation::is_prey(wolf, gull));  // resting, it can be caught
    gull.last_action = "ate";
    Bitzkey& hawk = simulation.add_agent(of(Species::Predator, Terrain::Avian), shore.x, shore.y);
    Bitzkey& frog = simulation.add_agent(of(Species::Predator, Terrain::Amphibious), shore.x + 1, shore.y);
    CHECK(Simulation::is_prey(hawk, gull));
    CHECK(Simulation::is_prey(frog, gull));  // only the terrestrial and aquatic are kept from a flyer
    CHECK(Simulation::is_prey(hawk, deer));
}

TEST_CASE("a flyer is light and hungry, and the amphibious slow") {
    const auto grown = [](Terrain terrain, Species species = Species::Grazer) {
        Bitzkey animal(0, of(species, terrain, {{"size", 6}, {"weapons", 7}, {"armour", 4}, {"strength", 5}, {"speed", 7}}), 0, 0);
        animal.age = static_cast<int>(animal.lifespan / 2);  // neither infant nor old
        animal.derive();
        return animal;
    };
    const Bitzkey walker = grown(Terrain::Terrestrial);
    Bitzkey flyer = grown(Terrain::Avian);
    CHECK((flyer.trait(Size) == 3 && flyer.trait(Weapons) == 3 && flyer.trait(Armour) == 2 && flyer.trait(Strength) == 5));
    CHECK((walker.trait(Size) == 6 && walker.trait(Weapons) == 7));
    CHECK(flyer.space() == 4);
    CHECK(flyer.max_health < walker.max_health);
    const double hungry = flyer.burn;
    FLIGHT_HUNGER = 0.0;
    flyer.derive();
    CHECK(hungry == doctest::Approx(flyer.burn * 1.25));
    FLIGHT_HUNGER = 0.25;
    CHECK(grown(Terrain::Amphibious).steps == static_cast<int>(walker.steps * AMPHIBIOUS_SPEED + 0.5));
    CHECK(grown(Terrain::Avian, Species::Plant).trait(Size) == 6);  // a plant's terrain is where it grows, not wings
}

TEST_CASE("young are born where they can live, or lost") {
    Simulation simulation = seaside();
    const Cell shore = coast(simulation);
    Bitzkey& mother = simulation.add_agent(of(Species::Grazer, Terrain::Terrestrial), shore.x, shore.y);
    mother.womb.push_back({of(Species::Grazer, Terrain::Aquatic), mother.mind, std::make_shared<Brain>(*mother.brain), 50, 1, -1});
    mother.womb.push_back({of(Species::Grazer, Terrain::Terrestrial), mother.mind, std::make_shared<Brain>(*mother.brain), 50, 1, -1});
    simulation.give_birth(mother);
    const auto& born = simulation.agents();
    REQUIRE(born.size() == 3);
    CHECK((born[1]->terrain == Terrain::Aquatic && born[1]->wet));
    CHECK((born[2]->terrain == Terrain::Terrestrial && !born[2]->wet));
}

TEST_CASE("plants grow only where their terrain lets them, and a body's seed suits where it lay") {
    Simulation simulation = seaside();
    const Cell shore = coast(simulation);
    Bitzkey& body = simulation.add_agent(of(Species::Grazer, Terrain::Avian), shore.x + 1, shore.y);
    body.health = 0;
    simulation.seed_corpse(body);
    simulation.seed_corpse(body);
    REQUIRE(simulation.seeds().size() == 2);
    for (Simulation::Seed& seed : simulation.seeds()) {
        CHECK(decode_terrain(seed.dna) == Terrain::Aquatic);
        seed.sown = -PLANT_GESTATION;
    }
    simulation.seeds().back().x = shore.x;  // one carried onto the land, where it never sprouts
    simulation.germinate();
    REQUIRE(simulation.seeds().size() == 1);
    CHECK(simulation.seeds().back().x == shore.x);
    const Bitzkey& sprouted = *simulation.agents().back();
    CHECK((sprouted.species == Species::Plant && sprouted.terrain == Terrain::Aquatic && sprouted.wet));
}

TEST_CASE("an animal senses the sea it stands in and the shores it can't cross") {
    Simulation simulation = seaside();
    const Cell shore = coast(simulation);
    Bitzkey& walker = simulation.add_agent(of(Species::Grazer, Terrain::Terrestrial), shore.x, shore.y);
    const Senses senses = simulation.sense(walker).senses;
    CHECK(senses[SenseSeaHere] == 0.0);
    CHECK(senses[SenseShoreEast] == 1.0);  // the very next square east
    CHECK(senses[SenseWalks] == 1.0);
    CHECK(senses[SenseSwims] == 0.0);
    Bitzkey& gull = simulation.add_agent(of(Species::Grazer, Terrain::Avian), shore.x + 1, shore.y);
    const Senses flying = simulation.sense(gull).senses;
    CHECK(flying[SenseSeaHere] == 1.0);
    CHECK(flying[SenseShoreWest] == 0.0);  // no shore stops a flyer
    CHECK(flying[SenseFlies] == 1.0);
}

TEST_CASE("the sea is saved with the world, and a world saved before terrain is all terrestrial") {
    Simulation simulation = seaside(6, 120);
    for (int turn = 0; turn < 5; ++turn) simulation.advance();
    std::string bytes = WorldFile::bytes(simulation);
    const auto loaded = WorldFile::from_bytes(bytes);
    CHECK(loaded->sea_map() == simulation.sea_map());
    CHECK(loaded->ocean() == simulation.ocean());
    REQUIRE(loaded->agents().size() == simulation.agents().size());
    for (std::size_t index = 0; index < loaded->agents().size(); ++index) {
        CHECK(loaded->agents()[index]->terrain == simulation.agents()[index]->terrain);
        CHECK(loaded->agents()[index]->wet == simulation.agents()[index]->wet);
    }
    const auto version = bytes.find("\"genome_version\":4");
    REQUIRE(version != std::string::npos);
    bytes[version + std::string("\"genome_version\":").size()] = '3';
    const auto old = WorldFile::from_bytes(bytes);
    for (const auto& agent : old->agents()) CHECK(agent->terrain == Terrain::Terrestrial);
}

TEST_CASE("acting tires an animal, flying the more, and resting restores it") {
    Simulation simulation = seaside();
    const Cell shore = coast(simulation);
    Bitzkey& walker = simulation.add_agent(of(Species::Grazer, Terrain::Terrestrial, {{"speed", 7}}), shore.x, shore.y);
    Bitzkey& gull = simulation.add_agent(of(Species::Grazer, Terrain::Avian, {{"speed", 7}}), shore.x, shore.y);
    walker.age = gull.age = static_cast<int>(walker.lifespan / 2);
    walker.derive();
    gull.derive();
    Simulation::Situation nothing;
    simulation.act(walker, walk_onto(simulation, shore, false), nothing);  // a square: the action and the step
    CHECK(walker.stamina == doctest::Approx(FULL_STAMINA - ACTION_FATIGUE - STEP_FATIGUE));
    simulation.act(gull, walk_onto(simulation, shore, false), nothing);
    CHECK(gull.stamina == doctest::Approx(FULL_STAMINA - ACTION_FATIGUE - STEP_FATIGUE * FLIGHT_FATIGUE));
    Bitzkey& frog = simulation.add_agent(of(Species::Grazer, Terrain::Amphibious), shore.x, shore.y);
    simulation.act(frog, walk_onto(simulation, shore, false), nothing);  // twice the walker's
    CHECK(frog.stamina == doctest::Approx(FULL_STAMINA - 2 * (ACTION_FATIGUE + STEP_FATIGUE)));
    frog.stamina = 30;
    simulation.act(frog, Rest, nothing);  // but rests back as fast
    CHECK(frog.stamina == doctest::Approx(30 + REST_RECOVERY));
    walker.stamina = 30;
    simulation.act(walker, Rest, nothing);
    CHECK(walker.stamina == doctest::Approx(30 + REST_RECOVERY));
    walker.stamina = 95;
    simulation.act(walker, Rest, nothing);
    CHECK(walker.stamina == FULL_STAMINA);
}

TEST_CASE("an exhausted animal can only rest, and a flyer can't rest at sea, where exhaustion drowns it") {
    Simulation simulation = seaside();
    const Cell shore = coast(simulation);
    Bitzkey& walker = simulation.add_agent(of(Species::Grazer, Terrain::Terrestrial), shore.x, shore.y);
    walker.stamina = 0;
    simulation.think(walker);
    CHECK(walker.last_action == "rested");
    CHECK(walker.stamina == REST_RECOVERY);
    Bitzkey& gull = simulation.add_agent(of(Species::Grazer, Terrain::Avian), shore.x + 1, shore.y);
    const auto options = simulation.sense(gull).options;
    CHECK(std::find(options.begin(), options.end(), Rest) == options.end());
    gull.stamina = 1;
    Simulation::Situation nothing;
    simulation.act(gull, walk_onto(simulation, {shore.x + 1, shore.y}, true), nothing);
    CHECK_FALSE(gull.alive());
    CHECK(simulation.death_record(gull.identifier)->cause == "drowned");
}
