// Days and nights: night over half the map, moving round it once a day, and animals resting through their own.

#include <doctest/doctest.h>

#include <algorithm>

#include "core/Brain.hpp"
#include "core/Simulation.hpp"

using namespace bitzkey;

namespace {

// A world of a day's turns, its animals sleeping through their nights.
struct Sleeping {
    Sleeping() { SLEEP = 1; }
    ~Sleeping() { SLEEP = 0; }
};

Dna animal(bool nocturnal, Terrain terrain = Terrain::Terrestrial) {
    Dna dna = with_terrain(encode_dna(Species::Grazer), terrain);
    if (nocturnal) dna.bits |= NOCTURNAL;
    return dna;
}

// A column in the night, and one in the day, on this turn.
std::pair<int, int> dark_and_light(const Simulation& simulation) {
    int dark = -1, light = -1;
    for (int x = 0; x < simulation.width(); ++x) (simulation.night(x) ? dark : light) = x;
    return {dark, light};
}

}  // namespace

TEST_CASE("a new world's year is 360 days of eight turns, and lifespans in years grow with it") {
    CHECK(DAY_LENGTH == 8);
    CHECK(8 * DAYS_IN_YEAR == 2880);
    Simulation::Settings settings{20, 20, 0, 1};
    settings.year_length = 8 * DAYS_IN_YEAR;
    const Simulation world(settings);
    CHECK(YEAR_LENGTH == 2880);
    const Bitzkey smallest(0, encode_dna(Species::Grazer, {{"size", 0}}), 0, 0);
    CHECK(smallest.lifespan == doctest::Approx(LIFESPAN_YEARS[0] * 2880));  // three years, of 360 days
}

TEST_CASE("night covers half the map, top to bottom, and moves west round it once a day") {
    Simulation simulation(Simulation::Settings{40, 20, 0, 1});
    std::vector<int> dark_turns(40, 0);
    int previous = simulation.night_start();
    for (int turn = 0; turn < DAY_LENGTH; ++turn) {
        int dark = 0;
        for (int x = 0; x < 40; ++x) {
            dark += simulation.night(x);
            dark_turns[static_cast<std::size_t>(x)] += simulation.night(x);
        }
        CHECK(dark == 20);  // half the columns
        simulation.take_turn();
        if (turn + 1 < DAY_LENGTH) CHECK(simulation.night_start() == (previous - 40 / DAY_LENGTH + 40) % 40);  // a step west
        previous = simulation.night_start();
    }
    CHECK(std::all_of(dark_turns.begin(), dark_turns.end(), [](int turns) { return turns == DAY_LENGTH / 2; }));  // half of each day
    CHECK(simulation.night_start() == Simulation(Simulation::Settings{40, 20, 0, 1}).night_start());  // round again in a day
}

TEST_CASE("an animal rests through its night, the diurnal in the dark and the nocturnal in the light") {
    Sleeping sleeping;
    Simulation simulation(Simulation::Settings{40, 20, 0, 1});
    simulation.clear_grass();
    const auto [dark, light] = dark_and_light(simulation);
    Bitzkey& day_owl = simulation.add_agent(animal(false), dark, 10);  // diurnal, in the dark: asleep
    Bitzkey& night_owl = simulation.add_agent(animal(true), dark, 12);  // nocturnal, in the dark: awake
    Bitzkey& late = simulation.add_agent(animal(true), light, 10);     // nocturnal, in the light: asleep
    simulation.index_cells();
    CHECK(simulation.resting_time(day_owl));
    CHECK_FALSE(simulation.resting_time(night_owl));
    CHECK(simulation.resting_time(late));
    for (int turn = 0; turn < 3; ++turn) {
        for (Bitzkey* agent : {&day_owl, &night_owl, &late}) agent->stamina = FULL_STAMINA;
        simulation.think_all({&day_owl, &night_owl, &late});
        CHECK(day_owl.last_action == "rested");
        CHECK(late.last_action == "rested");
    }
    CHECK(std::string(night_owl.last_action) != "new");
    const Senses senses = simulation.sense(night_owl).senses;
    CHECK(senses[SenseNight] == (simulation.night(night_owl.x) ? 1.0 : 0.0));
    CHECK(senses[SenseNocturnal] == 1.0);
    CHECK(simulation.sense(day_owl).senses[SenseNocturnal] == 0.0);
}

TEST_CASE("startled awake, a sleeper may flee or strike back, and a flyer over the sea can't sleep") {
    Sleeping sleeping;
    Simulation::Settings settings{40, 40, 0, 3};
    settings.ocean = DEFAULT_OCEAN;
    Simulation simulation(settings);
    simulation.clear_grass();
    int sea_dark = -1, row = -1;
    for (int y = 5; y < 35 && sea_dark < 0; ++y) {
        for (int x = 0; x < 40; ++x) {
            if (simulation.night(x) && simulation.sea(x, y)) {
                sea_dark = x;
                row = y;
                break;
            }
        }
    }
    REQUIRE(sea_dark >= 0);
    Bitzkey& gull = simulation.add_agent(animal(false, Terrain::Avian), sea_dark, row);
    simulation.index_cells();
    CHECK(simulation.resting_time(gull));
    simulation.think_all({&gull});
    CHECK(gull.last_action != std::string_view("rested"));  // it can't rest at sea, so it flies on through the night
    int land_dark = -1, land_row = -1;
    for (int y = 5; y < 35 && land_dark < 0; ++y) {
        for (int x = 0; x < 40; ++x) {
            if (simulation.night(x) && !simulation.sea(x, y) && !simulation.sea(x, y + 1)) {
                land_dark = x;
                land_row = y;
                break;
            }
        }
    }
    REQUIRE(land_dark >= 0);
    Bitzkey& sleeper = simulation.add_agent(with_terrain(encode_dna(Species::Grazer, {{"intellect", 7}}), Terrain::Terrestrial), land_dark,
                                            land_row);  // too clever to freeze in fear
    Bitzkey& hunter = simulation.add_agent(with_terrain(encode_dna(Species::Predator, {{"strength", 7}, {"weapons", 7}}), Terrain::Terrestrial),
                                           land_dark, land_row + 1);
    simulation.index_cells();
    (void)hunter;
    simulation.think_all({&sleeper});
    const std::uint32_t awake = (1U << Rest) | (1U << Flee) | (1U << Defend);
    CHECK((sleeper.odds_offered & ~awake) == 0);  // nothing but rest, flight or a blow back
    CHECK((sleeper.odds_offered & (1U << Flee)) != 0);  // a hunter alongside: it may wake to flee
}

TEST_CASE("an infant beside a parent is sheltered from the climate") {
    FEEBLE_YOUTH = 0.5;  // infants for half their lives, in this test
    Simulation simulation(Simulation::Settings{20, 20, 0, 1});
    Bitzkey& mother = simulation.add_agent(animal(false), 5, 5);
    Bitzkey& near = simulation.add_agent(animal(false), 5, 6);
    Bitzkey& apart = simulation.add_agent(animal(false), 9, 9);
    near.mother = apart.mother = mother.identifier;
    mother.age = static_cast<int>(mother.lifespan * 0.6);  // grown
    for (Bitzkey* agent : {&mother, &near, &apart}) agent->derive();
    CHECK(simulation.sheltered(near));
    CHECK_FALSE(simulation.sheltered(apart));  // too far from its mother
    CHECK_FALSE(simulation.sheltered(mother));  // grown, and an orphan besides
    mother.health = 0;
    CHECK_FALSE(simulation.sheltered(near));  // no shelter from a dead parent
    FEEBLE_YOUTH = 0.0;
}
