// Zones, seasons and temperature, ported from the Python tests.

#include <doctest/doctest.h>

#include <algorithm>
#include <numeric>
#include <set>
#include <vector>

#include "core/Simulation.hpp"

using namespace bitzkey;

namespace {

// An empty world at a turn, its grass cleared.
Simulation world_at(int turn, int size = 100) {
    Simulation simulation(Simulation::Settings{size, size, 0, 3});
    for (int step = 0; step < turn; ++step) simulation.take_turn();
    simulation.clear_grass();
    return simulation;
}

// The most a turn's go_ can cost an animal beyond its burn: a quarter of it for each cell past the first.
double most_gone(const Bitzkey& agent) { return agent.burn * GO_EFFORT * std::max(0, agent.steps - 1); }

std::vector<double> rows_on(int turn) {
    std::vector<double> rows;
    for (int y = 0; y < 100; ++y) rows.push_back(temperature_at(y, 100, turn));
    return rows;
}

std::vector<double> bands(std::initializer_list<std::pair<int, int>> runs) {
    std::vector<double> rows;
    for (const auto& [temperature, count] : runs) rows.insert(rows.end(), static_cast<std::size_t>(count), temperature);
    return rows;
}

}  // namespace

// These tests measure exposure's harm at the harsher rate climate began with, a tenth of full health a step.
struct HarshClimate {
    double saved = CLIMATE_DAMAGE;
    HarshClimate() { CLIMATE_DAMAGE = 0.1; }
    ~HarshClimate() { CLIMATE_DAMAGE = saved; }
};

TEST_CASE("zones band the map arctic, temperate, equator, temperate, arctic") {
    std::vector<double> rows;
    for (int y = 0; y < 100; ++y) rows.push_back(zone_temperature(y, 100));
    CHECK(rows == bands({{Cold, 20}, {Medium, 20}, {Hot, 20}, {Medium, 20}, {Cold, 20}}));
}

TEST_CASE("a year is 360 turns in four quarters by default") {
    std::vector<int> seasons;
    for (const int turn : {0, 89, 90, 179, 180, 269, 270, 359, 360}) seasons.push_back(season(turn));
    CHECK(seasons == std::vector<int>{0, 0, 1, 1, 2, 2, 3, 3, 0});
    CHECK((year(0) == 1 && year(359) == 1 && year(360) == 2 && year(800) == 3));
}

TEST_CASE("a world sets the year length for seasons and lifespans") {
    Simulation::Settings settings{20, 20, 0, 1};
    settings.year_length = 100;
    Simulation simulation(settings);
    CHECK((season(25) == 1 && season(50) == 2 && year(100) == 2));
    CHECK(simulation.add_agent(encode_dna(Species::Grazer), 1, 1).lifespan == doctest::Approx(100));  // the smallest live a year
    settings.year_length = 3;
    CHECK_THROWS_AS(Simulation{settings}, std::invalid_argument);
    const Simulation standard(Simulation::Settings{20, 20, 0, 1});
    CHECK(year(360) == 2);  // back to the default
}

TEST_CASE("each hemisphere and the equator are a step colder in its winter") {
    // the second quarter, once the wave has passed, and the fourth
    CHECK(rows_on(150) == bands({{VeryCold, 20}, {Cold, 20}, {Medium, 20}, {Medium, 20}, {Cold, 20}}));
    CHECK(rows_on(330) == bands({{Cold, 20}, {Medium, 20}, {Medium, 20}, {Cold, 20}, {VeryCold, 20}}));
    const std::vector<double> normal = bands({{Cold, 20}, {Medium, 20}, {Hot, 20}, {Medium, 20}, {Cold, 20}});
    for (const int turn : {10, 240, 420}) CHECK(rows_on(turn) == normal);  // the mild quarters
    CHECK(rows_on(510) == rows_on(150));  // the next year repeats
}

TEST_CASE("every zone's winter washes in at once in a wave, and ebbs back the way it came") {
    // The north's winter covers the arctic (rows 0-19), the temperate zone (20-39) and the equator
    // (40-59), each 10 bands of two with a wave of its own, all setting off together. A wave takes half
    // a season, 45 turns, and has a front four bands wide. 27 turns in, each front has come 0.6 of 14 bands.
    CHECK((season_progress(90) == 0.0 && season_progress(135) == 1.0 && season_progress(117) == doctest::Approx(0.6)));
    // each zone's wave sets off at once: the arctic, the temperate zone and the equator each its own
    CHECK((wave_band(0, 100, 1) == std::pair{0, 10} && wave_band(37, 100, 1) == std::pair{8, 10} &&
           wave_band(59, 100, 1) == std::pair{9, 10}));
    const double hot = Hot, medium = Medium, cold = Cold, very_cold = VeryCold;
    const std::array<double, 10> turned = {1, 1, 1, 1, 1, 0.85, 0.6, 0.35, 0.1, 0};
    for (const int start : {0, 20, 40}) {  // in every zone alike: winter come behind the front, a staircase, not yet ahead
        for (int band = 0; band < 10; ++band) {
            for (const int row : {start + 2 * band, start + 2 * band + 1}) {
                CHECK(temperature_at(row, 100, 117) ==
                      doctest::Approx(season_temperature(row, 100, 0) + turned[static_cast<std::size_t>(band)]));
            }
        }
    }
    for (int y = 60; y < 100; ++y) CHECK(temperature_at(y, 100, 117) == (y < 80 ? medium : cold));  // the south: no change

    CHECK((temperature_at(59, 100, 207) == hot && temperature_at(0, 100, 207) == very_cold));  // ebbing back from the equator
    CHECK(temperature_at(24, 100, 207) == doctest::Approx(cold - 0.35));  // the temperate zone's eighth band from the equator
    CHECK((temperature_at(99, 100, 297) == very_cold && temperature_at(40, 100, 297) == hot));  // the south's, up from the bottom
    CHECK((temperature_at(99, 100, 387) == very_cold && temperature_at(41, 100, 387) == hot));  // and ebbing back down to it
    CHECK(temperature_at(70, 100, 0) == medium);  // the world's first season is settled
    const double saved = SEASON_TURNING;
    SEASON_TURNING = 0;
    CHECK(temperature_at(50, 100, 90) == medium);  // all at once
    SEASON_TURNING = saved;
}

TEST_CASE("while the weather turns, its effects turn with it") {
    const HarshClimate harsh;
    Simulation simulation = world_at(116);  // at turn 117, next, the temperate zone's front has turned row 32 0.6 of the way to cold
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}}), 50, 32);
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    // 0.6 of a step's harm, with at most a little health given back by resting.
    CHECK((grazer.health >= 0.94 * grazer.max_health - 1e-9 && grazer.health < 0.98 * grazer.max_health));
    CHECK(grazer.hunger >= 1.1 * grazer.burn - 1e-9);  // 0.6 of the extra food, less at most half a turn's burn resting
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Medium}}), 60, 32);
    CHECK(simulation.plant_yield(plant) == doctest::Approx(0.4));
}

TEST_CASE("grass grows and dies back by how medium a turning row is") {
    Simulation simulation = world_at(117);  // rows 32 and 33 are 0.6 of the way to cold; the south is medium
    simulation.sow(20000);
    double turning = 0, south = 0;
    for (int x = 0; x < 100; ++x) {
        for (const int y : {32, 33}) turning += simulation.grass_at(x, y);
        for (const int y : {70, 71}) south += simulation.grass_at(x, y);
    }
    CHECK(turning / south == doctest::Approx(0.4).epsilon(0.2));
    CHECK(simulation.growth(32) == doctest::Approx(0.4));
    Simulation wave = world_at(116);
    const double regrowth = wave.grass_regrowth();
    wave.set_grass_regrowth(0);
    for (int x = 0; x < 100; ++x) wave.grass_cell(x, 32) = wave.grass_cell(x, 70) = 3;
    wave.take_turn();  // turn 117: the cells whose turn it is to die back, x of 3, 23, 43, 63 and 83 in row 32
    wave.set_grass_regrowth(regrowth);
    for (int x = 0; x < 100; ++x) {
        const bool due = (x + 32 * 100 + 117) % GRASS_DIEBACK == 0 && dieback_threshold(x, 32) < 0.6;
        CHECK(wave.grass_at(x, 32) == (due ? 2 : 3));
        CHECK(wave.grass_at(x, 70) == 3);
    }
}

TEST_CASE("hardiness halves an animal's exposure in one direction, and plants have none") {
    const Bitzkey tender(0, encode_dna(Species::Grazer), 1, 1);
    const Bitzkey hardy(1, encode_dna(Species::Grazer, {{"cold_tolerance", 1}}), 1, 1);
    const Bitzkey warm(2, encode_dna(Species::Grazer, {{"heat_tolerance", 1}}), 1, 1);
    const Bitzkey plant(3, encode_dna(Species::Plant, {{"heat_tolerance", 1}, {"cold_tolerance", 1}}), 1, 1);
    for (const Bitzkey* agent : {&tender, &hardy, &warm, &plant}) {  // every step is felt
        std::vector<double> steps;
        for (const int here : {Hot, Medium, Cold, VeryCold}) steps.push_back(agent->climate_steps(here));
        CHECK(steps == std::vector<double>{1, 0, 1, 2});
    }
    const auto shares = [](const Bitzkey& agent) {
        std::vector<double> found;
        for (const int here : {Hot, Medium, Cold, VeryCold}) found.push_back(agent.exposure_share(here));
        return found;
    };
    CHECK(shares(tender) == std::vector<double>{1.0, 1.0, 1.0, 1.0});
    CHECK(shares(hardy) == std::vector<double>{2.0, 1.0, 0.5, 0.5});
    CHECK(shares(warm) == std::vector<double>{0.5, 1.0, 2.0, 2.0});
    const Bitzkey both(4, encode_dna(Species::Grazer, {{"heat_tolerance", 1}, {"cold_tolerance", 1}}), 1, 1);  // the flags cancel out
    CHECK(shares(both) == std::vector<double>{1.0, 1.0, 1.0, 1.0});
    CHECK(shares(plant) == std::vector<double>{1.0, 1.0, 1.0, 1.0});
}

TEST_CASE("a hardy animal suffers half the extra hunger and damage") {
    const HarshClimate harsh;
    Simulation simulation = world_at(239);  // mid-year, mild, the weather settled
    Bitzkey& tender = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}}), 20, 10);  // the arctic: two steps too cold
    Bitzkey& hardy = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}, {"cold_tolerance", 1}}), 40, 10);
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    // With nothing to eat, resting or a walk gives back at most half a turn's burn and a little health;
    // going far costs up to a little more.
    CHECK((tender.hunger >= 1.5 * tender.burn - 1e-9 && tender.hunger <= 2 * tender.burn + most_gone(tender) + 1e-9));
    CHECK((hardy.hunger >= 1.0 * hardy.burn - 1e-9 && hardy.hunger <= 1.5 * hardy.burn + most_gone(hardy) + 1e-9));
    CHECK((tender.health < 0.83 * tender.max_health && hardy.health >= 0.9 * hardy.max_health - 1e-9 &&
           hardy.health < 0.93 * hardy.max_health));
}

TEST_CASE("an animal tolerant of heat suffers twice the extra hunger and damage in the cold") {
    const HarshClimate harsh;
    Simulation simulation = world_at(239);  // mid-year, mild, the weather settled
    Bitzkey& frail = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}, {"heat_tolerance", 1}}), 40, 10);  // the arctic
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    CHECK((frail.hunger >= 2.5 * frail.burn - 1e-9 && frail.hunger <= 3 * frail.burn + most_gone(frail) + 1e-9));
    CHECK((frail.health >= 0.6 * frail.max_health - 1e-9 && frail.health < 0.63 * frail.max_health));
}

TEST_CASE("a step from home doubles an animal's food, and two steps also hurt") {
    const HarshClimate harsh;
    Simulation simulation = world_at(239);  // the next turn is in the second quarter: no winter
    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}}), 10, 30);
    Bitzkey& one_away = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Cold}}), 30, 30);
    Bitzkey& two_away = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", VeryCold}}), 50, 30);
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    // With nothing to eat, resting gives back at most half a turn's burn.
    CHECK(at_home.hunger <= at_home.burn + most_gone(at_home) + 1e-9);
    CHECK(one_away.hunger >= 1.5 * one_away.burn - 1e-9);
    CHECK(two_away.hunger >= 1.5 * two_away.burn - 1e-9);
    CHECK(at_home.health == at_home.max_health);  // a tenth of its health a step from home, resting at most a little back
    CHECK(one_away.health < 0.93 * one_away.max_health);
    CHECK(two_away.health < 0.83 * two_away.max_health);
}

TEST_CASE("starving out of its temperature is dying of exposure") {
    Simulation simulation = world_at(240);
    Bitzkey& away = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Cold}}), 20, 50);  // on the equator: too hot
    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}}), 40, 50);
    away.hunger = at_home.hunger = 99.9;
    away.health = 0.05 * away.max_health;  // wasting, each has a turn left
    at_home.health = 0.05 * at_home.max_health;
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    CHECK((away.last_action == "overheated" && at_home.last_action == "starved"));
}

TEST_CASE("tolerance is rare in the starting genomes") {
    const Simulation simulation(Simulation::Settings{100, 100, 600, 5});
    int flags = 0, count = 0;
    for (const auto& agent : simulation.agents()) {
        flags += agent->climate.heat + agent->climate.cold;
        count += 2;
    }
    CHECK(static_cast<double>(flags) / count == doctest::Approx(0.1).epsilon(0.5));
}

TEST_CASE("winter turns a home zone hostile, and the cold can kill") {
    const HarshClimate harsh;
    Simulation simulation = world_at(149);  // by turn 150, next, the north's winter has set in
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}}), 50, 10);
    grazer.health = 5;
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    CHECK(!grazer.alive());
    CHECK(grazer.last_action == "froze");
}

TEST_CASE("a plant out of its temperature withers without growing or seeding, and two steps hurt") {
    const HarshClimate harsh;
    Simulation simulation = world_at(239);
    Bitzkey& one_away = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Hot}, {"fertility", 7}}), 50, 30);
    Bitzkey& two_away = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", VeryCold}, {"fertility", 7}}), 20, 30);
    for (Bitzkey* plant : {&one_away, &two_away}) {
        plant->health = plant->max_health / 2;
        plant->hunger = 70;  // too hungry to heal
    }
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    for (int turn = 0; turn < 2; ++turn) simulation.take_turn();
    CHECK((one_away.last_action == "withering" && two_away.last_action == "withering"));
    CHECK(one_away.health == doctest::Approx(0.5 * one_away.max_health));  // a step away doesn't hurt
    CHECK(two_away.health == doctest::Approx(0.1 * two_away.max_health));  // two steps, a fifth a turn
    CHECK((simulation.agents().size() == 3 && simulation.seeds().empty()));
}

TEST_CASE("a plant out of its temperature gives half the food") {
    Simulation simulation = world_at(240);
    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Medium}}), 50, 30);
    Bitzkey& away = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Cold}}), 60, 30);
    CHECK((simulation.plant_yield(at_home) == 1.0 && simulation.plant_yield(away) == 0.0));
    for (Bitzkey* plant : {&at_home, &away}) {
        Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), plant->x, plant->y + 1);  // a big stomach
        grazer.hunger = 90;
        simulation.browse(grazer, *plant);
        const double meal = PLANT_MEAL * grazer.browsing * simulation.plant_yield(*plant) / grazer.stomach;
        CHECK(grazer.hunger == doctest::Approx(90 - meal));
    }
}

TEST_CASE("spawned plants are at home in their zone, and imports keep theirs") {
    Simulation simulation(Simulation::Settings{60, 60, 300, 4});
    std::set<int> temperatures;
    for (const auto& agent : simulation.agents()) {
        if (agent->species != Species::Plant) continue;
        std::set<int> near;  // the group's centre is within two rows
        for (int dy = -2; dy <= 2; ++dy) {
            if (0 <= agent->y + dy && agent->y + dy < 60) near.insert(zone_temperature(agent->y + dy, 60));
        }
        CHECK(near.contains(agent->climate.temperature));
        temperatures.insert(agent->climate.temperature);
    }
    CHECK(temperatures.size() == 3);  // hot, medium and cold zones
    Imported saved;
    saved.dna = encode_dna(Species::Plant, {{"temperature", VeryCold}});
    Simulation::Settings settings{30, 30, 0, 4};
    settings.imported.push_back(saved);
    const Simulation imported(settings);
    for (const auto& agent : imported.agents()) CHECK(agent->climate.temperature == VeryCold);
}

TEST_CASE("an animal senses how far it is from home and how close home weather is") {
    Simulation simulation = world_at(240);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Cold}, {"sight", 2}}), 50, 23);  // sees 8 rows
    const auto comfort = simulation.comfort_row(grazer, grazer.sight);
    REQUIRE(comfort.has_value());
    CHECK((comfort->x == 50 && comfort->y == 19));  // the arctic, four rows north
    Simulation::Sensed sensed = simulation.sense(grazer);
    CHECK(sensed.senses[SenseClimateDiscomfort] == 0.5);  // temperate: a step too warm
    CHECK(sensed.senses[SenseComfortCloseness] == doctest::Approx(1 - 3.0 / 8));

    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}}), 50, 30);
    sensed = simulation.sense(at_home);
    CHECK((sensed.senses[SenseClimateDiscomfort] == 0.0 && sensed.senses[SenseComfortCloseness] == 1.0));

    Bitzkey& lost = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", VeryCold}, {"sight", 0}}), 50, 50);  // none in sight
    sensed = simulation.sense(lost);
    CHECK((sensed.senses[SenseClimateDiscomfort] == 1.0 && sensed.senses[SenseComfortCloseness] == 0.0));
    CHECK(action_index("migrate") == -1);  // moving toward home is left for brains to learn
}

TEST_CASE("an animal senses which way home lies and how far, beyond its sight, and minds the distance") {
    Simulation simulation = world_at(240);  // mild: arctic rows 0 to 19 and 80 to 99, the equator 40 to 59
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Cold}, {"sight", 0}}), 50, 35);
    Simulation::Sensed sensed = simulation.sense(grazer);
    CHECK((sensed.senses[SenseComfortCloseness] == 0.0 && sensed.senses[SenseHomeDirection] == -1.0));  // out of sight, to the north
    CHECK(sensed.senses[SenseHomeDistance] == doctest::Approx(16.0 / 50));
    const double far = simulation.comfort(grazer, sensed.situation);

    Bitzkey& nearer = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Cold}, {"sight", 0}}), 50, 70);
    sensed = simulation.sense(nearer);
    CHECK((sensed.senses[SenseHomeDirection] == 1.0 && sensed.senses[SenseHomeDistance] == doctest::Approx(10.0 / 50)));
    CHECK(simulation.comfort(nearer, sensed.situation) - far == doctest::Approx(HOMEWARD_UNEASE * 6.0 / 50));  // each row home eases it

    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}}), 50, 30);
    sensed = simulation.sense(at_home);
    CHECK((sensed.senses[SenseHomeDirection] == 0.0 && sensed.senses[SenseHomeDistance] == 0.0));

    Bitzkey& lost = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", VeryCold}}), 50, 50);  // very cold nowhere this season
    sensed = simulation.sense(lost);
    CHECK((sensed.senses[SenseHomeDirection] == 0.0 && sensed.senses[SenseHomeDistance] == 1.0));
}

TEST_CASE("an animal sees the wave coming as a staircase of warmth") {
    Simulation simulation = world_at(117);  // the temperate zone's winter front at rows 30 to 37
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}, {"sight", 2}}), 50, 32);  // sees 8 rows
    const Simulation::Sensed sensed = simulation.sense(grazer);
    CHECK(sensed.senses[SenseWarmthNorth] == doctest::Approx(-0.5));  // cold behind it,
    CHECK(sensed.senses[SenseWarmth] == doctest::Approx(-0.3));       // part way here,
    CHECK(sensed.senses[SenseWarmthSouth] == doctest::Approx(0.0));   // the equator's turned mild
    CHECK(sensed.senses[SenseClimateDiscomfort] == doctest::Approx(0.3));
}

TEST_CASE("an animal senses where it is and how warm it is here, to the north and to the south") {
    Simulation simulation = world_at(0);  // mild: arctic rows 0-19, temperate 20-39, equator 40-59
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}, {"sight", 2}}), 50, 35);  // sees 8 rows
    Simulation::Sensed sensed = simulation.sense(grazer);
    CHECK(sensed.senses[SenseLatitude] == 2 * 35.5 / 100 - 1);
    CHECK((sensed.senses[SenseWarmth] == 0.0 && sensed.senses[SenseWarmthNorth] == 0.0 &&
           sensed.senses[SenseWarmthSouth] == 0.5));  // row 27 temperate, row 43 the equator
    Bitzkey& cold = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}, {"sight", 2}}), 50, 3);
    sensed = simulation.sense(cold);
    CHECK((sensed.senses[SenseWarmth] == -1.0 && sensed.senses[SenseWarmthNorth] == -1.0 &&
           sensed.senses[SenseWarmthSouth] == -1.0));  // two steps too cold, north to the map's edge too
}

TEST_CASE("an animal can head north or south, but not off the map") {
    Simulation simulation = world_at(0);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"speed", 0}}), 50, 50);  // two rows a step
    Simulation::Sensed sensed = simulation.sense(grazer);
    const auto offered = [](const Simulation::Sensed& found, int action) {
        return std::find(found.options.begin(), found.options.end(), action) != found.options.end();
    };
    CHECK((offered(sensed, GoNorth) && offered(sensed, GoSouth)));
    simulation.act(grazer, GoNorth, sensed.situation);
    CHECK((grazer.x == 50 && grazer.y == 48 && grazer.last_action == "went north"));
    simulation.act(grazer, GoSouth, sensed.situation);
    CHECK((grazer.x == 50 && grazer.y == 50 && grazer.last_action == "went south"));
    Bitzkey& top = simulation.add_agent(encode_dna(Species::Grazer), 50, 0);
    Bitzkey& bottom = simulation.add_agent(encode_dna(Species::Grazer, {{"speed", 7}}), 50, 98);
    CHECK_FALSE(offered(simulation.sense(top), GoNorth));
    sensed = simulation.sense(bottom);
    CHECK(offered(sensed, GoSouth));
    simulation.act(bottom, GoSouth, sensed.situation);
    CHECK(bottom.y == 99);  // stopped by the edge
    CHECK_FALSE(offered(simulation.sense(bottom), GoSouth));
}

TEST_CASE("a walk moves one cell for half a turn's food, and going further costs more") {
    Simulation simulation = world_at(0);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"speed", 7}, {"curiosity", 7}}), 50, 50);  // six cells a step
    Simulation::Sensed sensed = simulation.sense(grazer);
    for (const int walk : {WalkNorth, WalkSouth, WalkEast, WalkWest}) {
        CHECK(std::find(sensed.options.begin(), sensed.options.end(), walk) != sensed.options.end());
    }
    grazer.hunger = 50;
    simulation.act(grazer, WalkNorth, sensed.situation);
    CHECK((grazer.x == 50 && grazer.y == 49 && grazer.last_action == "walked north"));
    CHECK(grazer.hunger == doctest::Approx(50 - grazer.burn * WALK_REST));  // saves half the turn's burn, as resting does
    CHECK(grazer.reward == doctest::Approx(CURIOSITY_JOY));  // walking explores too, but eases no hunger
    grazer.hunger = 50;
    simulation.act(grazer, GoSouth, sensed.situation);
    CHECK(grazer.y == 55);
    CHECK(grazer.hunger == doctest::Approx(50 + grazer.burn * GO_EFFORT * 5));  // a quarter of a turn's burn for each cell past one
    simulation.act(grazer, WalkWest, sensed.situation);
    CHECK((grazer.x == 49 && grazer.last_action == "walked west"));
    Bitzkey& edge = simulation.add_agent(encode_dna(Species::Grazer, {{"speed", 7}}), 50, 97);
    edge.hunger = 50;
    sensed = simulation.sense(edge);
    simulation.act(edge, GoSouth, sensed.situation);
    CHECK(edge.y == 99);
    CHECK(edge.hunger == doctest::Approx(50 + edge.burn * GO_EFFORT * 1));  // only the two cells it could go
    sensed = simulation.sense(edge);
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), WalkSouth) == sensed.options.end());
}

TEST_CASE("an animal can always head east or west round the map, and enjoys setting off") {
    Simulation simulation = world_at(0);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"speed", 0}, {"curiosity", 7}}), 1, 50);  // two cells a step
    Simulation::Sensed sensed = simulation.sense(grazer);
    for (const int action : {GoEast, GoWest, Rest}) {
        CHECK(std::find(sensed.options.begin(), sensed.options.end(), action) != sensed.options.end());
    }
    CHECK(action_index("explore") == -1);
    simulation.act(grazer, GoWest, sensed.situation);
    CHECK((grazer.x == 99 && grazer.y == 50 && grazer.last_action == "went west" && grazer.reward == CURIOSITY_JOY));
    simulation.act(grazer, GoEast, sensed.situation);
    CHECK((grazer.x == 1 && grazer.y == 50 && grazer.last_action == "went east"));
}

TEST_CASE("being out of its temperature is uncomfortable, less so for the hardy") {
    Simulation simulation = world_at(0);
    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}}), 50, 30);
    Bitzkey& one_step = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}}), 52, 10);
    Bitzkey& two_steps = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}}), 54, 10);
    Bitzkey& hardy = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}, {"cold_tolerance", 1}}), 56, 10);
    std::vector<double> exposure;
    for (Bitzkey* agent : {&at_home, &one_step, &two_steps, &hardy}) exposure.push_back(simulation.sense(*agent).situation.exposure);
    CHECK(exposure == std::vector<double>{0.0, 0.5, 1.0, 0.5});
    const double saved = CLIMATE_UNEASE;
    CLIMATE_UNEASE = 0.4;
    Simulation::Situation situation = simulation.sense(two_steps).situation;
    const double cold = simulation.comfort(two_steps, situation);
    situation.exposure = 0.0;
    CHECK(simulation.comfort(two_steps, situation) - cold == doctest::Approx(0.4));
    CLIMATE_UNEASE = saved;
}

TEST_CASE("foraging and herding never lead further from home") {
    Simulation simulation = world_at(0);  // row 40 is the hot equator, rows 20-39 temperate
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}, {"sight", 2}}), 50, 38);
    simulation.grass_cell(50, 41) = 1;
    simulation.grass_cell(50, 33) = 1;
    Simulation::Situation situation = simulation.sense(grazer).situation;
    REQUIRE(situation.grass.has_value());
    CHECK((situation.grass->x == 50 && situation.grass->y == 33));  // past the nearer, hot one
    Bitzkey& hot = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}, {"sight", 2}}), 50, 42);
    situation = simulation.sense(hot).situation;
    REQUIRE(situation.grass.has_value());
    CHECK((situation.grass->x == 50 && situation.grass->y == 41));  // it is at home there

    Bitzkey& herder = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}, {"empathy", 7}, {"patience", 7}, {"sight", 1}}), 70, 36);  // sees 5 cells
    std::vector<Bitzkey*> mates;
    for (const int dx : {-1, 0, 1}) mates.push_back(&simulation.add_agent(herder.dna, 70 + dx, 40));  // on the equator
    simulation.index_cells();
    Simulation::Sensed sensed = simulation.sense(herder);
    REQUIRE(sensed.situation.herd_middle.has_value());
    CHECK((sensed.situation.herd_middle->x == 70 && sensed.situation.herd_middle->y == 40));
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), Herd) == sensed.options.end());
    for (Bitzkey* mate : mates) mate->y = 32;  // into the temperate zone
    simulation.index_cells();
    sensed = simulation.sense(herder);
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), Herd) != sensed.options.end());
}

TEST_CASE("a plant too hot needs twice the food, and too cold doesn't") {
    Simulation simulation = world_at(240);
    Bitzkey& too_hot = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Cold}}), 20, 30);  // temperate: a step too warm
    Bitzkey& too_cold = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Hot}}), 40, 30);
    too_hot.hunger = too_cold.hunger = 50;
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    CHECK(too_hot.hunger == doctest::Approx(50 + 2 * too_hot.burn - 1.5));
    CHECK(too_cold.hunger == doctest::Approx(50 + too_cold.burn - 1.5));
}

TEST_CASE("the year starts mild") {
    std::vector<int> seasons;
    for (const int turn : {0, 90, 180, 270}) seasons.push_back(season(turn));
    CHECK(seasons == std::vector<int>{0, 1, 2, 3});
    std::vector<double> start, zones;
    for (int y = 0; y < 100; ++y) {
        start.push_back(temperature_at(y, 100, 0));
        zones.push_back(zone_temperature(y, 100));
    }
    CHECK(start == zones);
}

TEST_CASE("grass grows only where it is medium now") {
    Simulation simulation = world_at(150);  // the north's winter: its temperate zone is cold, the equator medium
    simulation.sow(3000);
    std::set<int> rows;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            if (simulation.grass_at(x, y)) rows.insert(y);
        }
    }
    CHECK(rows.size() == 40);
    CHECK((*rows.begin() == 40 && *rows.rbegin() == 79));
}

TEST_CASE("a seed that falls where it can't grow waits for its temperature") {
    const double regrowth = GRASS_REGROWTH;
    GRASS_REGROWTH = 0.004;  // the world this test was laid out in: the seed falls where it waits long enough
    Simulation simulation = world_at(240);
    GRASS_REGROWTH = regrowth;
    simulation.set_mutation(0.5, 0.0);
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Cold}, {"fertility", 7}}), 50, 30);
    simulation.index_cells();
    for (int attempt = 0; attempt < 400 && simulation.seeds().empty(); ++attempt) {
        plant.hunger = 0;
        simulation.seed_plant(plant);
    }
    REQUIRE(simulation.seeds().size() == 1);
    CHECK(simulation.agents().size() == 1);
    const Simulation::Seed seed = simulation.seeds().front();
    CHECK((seed.generation == 1 && seed.sown == 240 && plant.children == 1));

    while (simulation.turn() < 495) simulation.take_turn();  // next year's northern winter makes its zone cold
    CHECK(simulation.seeds().empty());
    const auto& agents = simulation.agents();
    const auto sprouted = std::find_if(agents.begin(), agents.end(), [&](const auto& agent) { return agent->dna == seed.dna && agent.get() != &plant; });
    REQUIRE(sprouted != agents.end());
    CHECK(((*sprouted)->x == seed.x && (*sprouted)->y == seed.y && (*sprouted)->generation == 1));
}

TEST_CASE("a seed stays viable for three quarters of a year") {
    Simulation simulation = world_at(0);
    simulation.seeds().push_back({50, 30, encode_dna(Species::Plant, {{"temperature", VeryCold}}), 1, 0});  // never very cold there
    while (simulation.turn() < 269) simulation.take_turn();
    CHECK(simulation.seeds().size() == 1);
    simulation.take_turn();
    CHECK(simulation.seeds().empty());
}

TEST_CASE("animals sense the time of year as two clock hands a quarter apart") {
    Simulation simulation = world_at(0);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 50, 50);
    std::vector<std::pair<double, double>> hands;
    for (int turn = 0; turn <= 360; ++turn) {
        if (turn % 90 == 0) {
            const Simulation::Sensed sensed = simulation.sense(grazer);
            hands.emplace_back(sensed.senses[SenseTimeOfYearA], sensed.senses[SenseTimeOfYearB]);
        }
        grazer.hunger = 0;  // kept fed and young the year round: it must outlive the test that holds it
        grazer.age = 0;
        if (turn < 360) simulation.take_turn();
        REQUIRE(grazer.alive());
    }
    CHECK(hands == std::vector<std::pair<double, double>>{{1.0, 0.5}, {0.5, 0.0}, {0.0, 0.5}, {0.5, 1.0}, {1.0, 0.5}});
}

TEST_CASE("herd-mates in sight learn from a death and feel it, by empathy and intellect") {
    const double trust_heed = TRUST_HEED;
    TRUST_HEED = 0.0;  // trust aside
    Simulation simulation = world_at(240);
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);  // far off
    const Dna bright = encode_dna(Species::Grazer, {{"empathy", 7}, {"intellect", 7}, {"sight", 1}});  // sees 5 cells
    Bitzkey& dying = simulation.add_agent(bright, 50, 50);
    Bitzkey& witness = simulation.add_agent(bright, 50, 53);  // three cells off: in sight
    Bitzkey& dull = simulation.add_agent(encode_dna(Species::Grazer, {{"empathy", 7}, {"intellect", 0}, {"sight", 1}}), 51, 50);
    Bitzkey& far = simulation.add_agent(bright, 50, 59);  // out of sight
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Omnivore, {{"empathy", 7}, {"intellect", 7}, {"sight", 1}}), 49, 50);
    simulation.index_cells();
    const Simulation::Sensed sensed = simulation.sense(dying);
    dying.last_senses = sensed.senses;
    dying.last_choice = sensed.options.front();
    dying.has_last_senses = true;
    const Brain before = *witness.brain;
    dying.health = 0;
    simulation.die(dying, "exhausted");  // a sudden death, its last choice's doing
    CHECK(*witness.brain == before);  // what the herd saw is learnt at the turn's end, with the rest
    simulation.learn_witnessed();
    Brain expected = before;
    expected.learn(dying.last_senses, dying.last_choice, -DEATH_PENALTY, LEARNING_RATE);
    CHECK(*witness.brain == expected);
    CHECK((witness.reward == SOCIAL_MOOD * -DEATH_PENALTY && witness.seen_failure == 1.0));
    CHECK(dull.reward == 0.0);  // with no intellect, it sees it but takes nothing in
    for (const Bitzkey* unmoved : {&far, &stranger}) CHECK((unmoved->reward == 0.0 && unmoved->seen_failure == 0.0));
    TRUST_HEED = trust_heed;
}

TEST_CASE("a pack learns from a kill, and what it saw is an input that fades") {
    Simulation simulation = world_at(240);
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    const Dna pack = encode_dna(Species::Predator, {{"empathy", 7}, {"intellect", 7}, {"sight", 3}, {"strength", 7}, {"weapons", 7}});
    Bitzkey& hunter = simulation.add_agent(pack, 50, 50);
    Bitzkey& mate = simulation.add_agent(pack, 52, 50);
    Bitzkey& prey = simulation.add_agent(encode_dna(Species::Grazer), 50, 51);
    prey.health = 0.1;
    simulation.index_cells();
    const Simulation::Sensed sensed = simulation.sense(hunter);
    hunter.last_senses = sensed.senses;
    hunter.last_choice = sensed.options.front();
    hunter.has_last_senses = true;
    simulation.attack(hunter, prey);
    CHECK(!prey.alive());
    CHECK(mate.seen_success == 1.0);
    const Simulation::Sensed seen = simulation.sense(mate);
    CHECK(seen.senses[SenseHerdSuccess] == 1.0);
    simulation.take_turn();
    CHECK(mate.seen_success == doctest::Approx(SOCIAL_MEMORY));
}

TEST_CASE("waiting seeds rewind and replay with the world") {
    Simulation simulation(Simulation::Settings{40, 40, 60, 4});
    while (simulation.seeds().empty() && simulation.turn() < 300 && !simulation.ended()) simulation.advance();  // until some seeds wait
    const std::vector<Simulation::Seed> seeds = simulation.seeds();
    const int turn = simulation.turn();
    CHECK(!seeds.empty());
    simulation.seek(turn / 2);
    simulation.seek(turn);
    CHECK(simulation.seeds() == seeds);
}

TEST_CASE("groups spawn where their home temperature is usual") {
    for (const std::uint64_t seed : {1, 2, 3}) {
        const Simulation simulation(Simulation::Settings{100, 100, 90, seed});
        for (const auto& agent : simulation.agents()) {  // each within the spread of a centre at home
            const int home = std::min(decode_climate(agent->dna).temperature, 2);  // very cold homes take the arctic
            bool found = false;
            for (int y = agent->y - SPAWN_SPREAD; y <= agent->y + SPAWN_SPREAD; ++y) found = found || zone_temperature(y, 100) == home;
            CHECK(found);
        }
    }
    const Simulation empty(Simulation::Settings{100, 100, 0, 1});
    std::vector<int> equator(20);
    std::iota(equator.begin(), equator.end(), 40);
    CHECK(empty.home_rows(Hot) == equator);
    CHECK(empty.home_rows(VeryCold).size() == 36);  // the arctic, north and south
}

TEST_CASE("an action finds the world as those before it left it, and a stale plan fails softly") {
    Simulation simulation = world_at(240);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 50, 50);
    Bitzkey& prey = simulation.add_agent(encode_dna(Species::Grazer), 50, 51);
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 51, 50);
    simulation.index_cells();
    Simulation::Situation situation;
    simulation.act(grazer, Eat, situation);  // its grass was eaten before it got to it
    CHECK(grazer.last_action == "found it eaten");
    prey.health = 0;
    simulation.die(prey, "killed");
    situation.prey = &prey;
    simulation.act(predator, Attack, situation);  // it fell to another first
    CHECK(predator.last_action == "missed");
    Bitzkey& mate = simulation.add_agent(encode_dna(Species::Grazer), 49, 50);
    simulation.index_cells();
    mate.cooldown = 5;  // it bred with another first
    situation.mate = &mate;
    simulation.act(grazer, Breed, situation);
    CHECK((grazer.last_action == "was spurned" && grazer.womb.empty()));
}
