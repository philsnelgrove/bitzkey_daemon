// The simulation's rules, ported from the Python tests' key cases.

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <numeric>
#include <set>

#include "core/PyFormat.hpp"
#include <nlohmann/json.hpp>

#include "core/Exchange.hpp"
#include "core/Insights.hpp"
#include "core/Simulation.hpp"
#include "core/WorldFile.hpp"

using namespace bitzkey;

namespace {

Simulation world(int size = 12, int seed = 3) {
    Simulation simulation(Simulation::Settings{size, size, 0, static_cast<std::uint64_t>(seed)});
    simulation.clear_grass();
    return simulation;
}

// A genome with some of its dot and ring bits flipped, and of its social background.
Dna flipped(Dna dna, std::uint64_t bits, std::uint32_t social = 0) { return {dna.bits ^ bits, dna.social ^ social}; }

}  // namespace

TEST_CASE("a seeded world replays exactly") {
    Simulation first(Simulation::Settings{40, 40, 60, 11});
    Simulation second(Simulation::Settings{40, 40, 60, 11});
    for (int turn = 0; turn < 40; ++turn) {
        first.advance();
        second.advance();
    }
    REQUIRE(first.agents().size() == second.agents().size());
    for (std::size_t index = 0; index < first.agents().size(); ++index) {
        CHECK(first.agents()[index]->hunger == second.agents()[index]->hunger);
        CHECK(first.agents()[index]->x == second.agents()[index]->x);
    }
}

TEST_CASE("random populations spawn in groups of three to five sharing a genome") {
    BIOME_GROUPS = 0;  // the population alone, without the biomes' groups
    for (const int population : {3, 7, 90, 101}) {
        Simulation simulation(Simulation::Settings{50, 50, population, static_cast<std::uint64_t>(population)});
        std::map<Dna, int> groups;
        for (const auto& agent : simulation.agents()) groups[agent->dna] += 1;
        CHECK(static_cast<int>(simulation.agents().size()) == population);
        for (const auto& [dna, count] : groups) {
            if (population >= 3) CHECK(count >= 3);
            CHECK(count <= 5);
        }
    }
    BIOME_GROUPS = 2;
}

TEST_CASE("bodies set health pools, metabolism, stomachs and lifespans") {
    Simulation simulation = world();
    const Bitzkey& small = simulation.add_agent(encode_dna(Species::Grazer), 1, 1);
    const Bitzkey& large = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 2, 2);
    CHECK(small.max_health == 60);
    CHECK(large.max_health == 150);
    CHECK(small.metabolism == doctest::Approx(1.6));
    CHECK(large.metabolism == doctest::Approx(0.6));
    CHECK(small.stomach == doctest::Approx(0.5));
    CHECK(large.stomach == doctest::Approx(2.0));
    CHECK(small.lifespan == doctest::Approx(3 * 360));    // three years
    CHECK(large.lifespan == doctest::Approx(30 * 360));   // thirty years
    // fifteen years halfway between, rising steadily either side
    CHECK(simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 4, 4).lifespan == doctest::Approx((3 + 12 * 6.0 / 7) * 360));
    CHECK(simulation.add_agent(encode_dna(Species::Grazer, {{"size", 4}}), 5, 5).lifespan == doctest::Approx((15 + 15 * 1.0 / 7) * 360));
    CHECK(small.steps == 2);  // grazers move half as far again, rounded up
    const Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"speed", 7}}), 3, 3);
    CHECK(predator.steps == 4);
}

TEST_CASE("litters: small bodies and a high litter trait make more young, for every species alike") {
    const std::array<std::pair<int, int>, 2> fecund = {std::pair{7, 0}, std::pair{7, 0}};
    const std::array<std::pair<int, int>, 2> sparse = {std::pair{0, 7}, std::pair{0, 7}};
    CHECK(litter_size(fecund) == 12);  // twice the usual, for the smallest
    CHECK(litter_size(sparse) == 1);
    const double small_litters = SMALL_LITTERS;
    SMALL_LITTERS = 1.0;
    CHECK(litter_size(fecund) == 6);
    SMALL_LITTERS = small_litters;
}

TEST_CASE("the mother carries her young for a gestation set by her size, then gives birth around her") {
    CHECK(std::vector<int>{gestation(0), gestation(1), gestation(4), gestation(7)} == std::vector<int>{10, 16, 33, 50});
    const double damage = CLIMATE_DAMAGE;
    CLIMATE_DAMAGE = 0;
    const double newborn_food = NEWBORN_FOOD;
    NEWBORN_FOOD = 0;  // the full litter, whatever energy is given
    const double plants_per_grazer = PLANTS_PER_GRAZER;
    PLANTS_PER_GRAZER = 0;  // no plants about: breeding left to the rest
    Simulation simulation(Simulation::Settings{30, 30, 0, 3});
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"litter", 7}});  // the smallest: ten turns, twelve young
    Bitzkey& first = simulation.add_agent(dna, 15, 15);
    Bitzkey& second = simulation.add_agent(dna, 15, 16);
    simulation.index_cells();
    simulation.breed(first, second);
    CHECK((first.womb.size() == 12 && first.due_turn == 10 && simulation.agents().size() == 2));
    CHECK(second.womb.empty());  // the parent that chose to breed carries them
    CHECK_FALSE(first.can_breed());
    simulation.give_birth(first);
    CHECK((simulation.agents().size() == 14 && first.womb.empty() && !first.due_turn));
    int at_home = 0;
    for (std::size_t index = 2; index < simulation.agents().size(); ++index) {
        const Bitzkey& child = *simulation.agents()[index];
        const int reach = std::max(std::abs(child.x - first.x), std::abs(child.y - first.y));
        CHECK((reach <= BIRTH_REACH && child.generation == 1));
        at_home += reach == 0;
    }
    CHECK(at_home == 9);  // her own square first: nine more of the smallest fit beside her
    simulation.die(first, "killed");
    CLIMATE_DAMAGE = damage;
    NEWBORN_FOOD = newborn_food;
    PLANTS_PER_GRAZER = plants_per_grazer;
}

TEST_CASE("young die with the mother carrying them") {
    Simulation simulation(Simulation::Settings{12, 12, 0, 3});
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"litter", 7}});
    Bitzkey& first = simulation.add_agent(dna, 5, 5);
    Bitzkey& second = simulation.add_agent(dna, 5, 6);
    simulation.index_cells();
    simulation.breed(first, second);
    CHECK_FALSE(first.womb.empty());
    simulation.die(first, "killed");
    CHECK((first.womb.empty() && !first.due_turn && simulation.agents().size() == 2));
}

TEST_CASE("size counts strongly in strength and toughness, and plants' thorns come from weapons") {
    Simulation simulation = world();
    const Bitzkey& target = simulation.add_agent(encode_dna(Species::Grazer), 1, 1);
    const Bitzkey& brute = simulation.add_agent(encode_dna(Species::Predator, {{"strength", 7}}), 2, 2);
    const Bitzkey& giant = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}}), 3, 3);
    CHECK(Simulation::damage(giant, target) == doctest::Approx(Simulation::damage(brute, target)));
    const Bitzkey& thorny = simulation.add_agent(encode_dna(Species::Plant, {{"weapons", 4}, {"strength", 0}}), 5, 5);
    const Bitzkey& strong = simulation.add_agent(encode_dna(Species::Plant, {{"weapons", 4}, {"strength", 7}}), 6, 6);
    CHECK(Simulation::damage(thorny, target) == Simulation::damage(strong, target));
    const Bitzkey& full = simulation.add_agent(encode_dna(Species::Plant, {{"weapons", 7}, {"size", 7}}), 7, 7);
    CHECK(Simulation::damage(full, target) == doctest::Approx(15));
}

TEST_CASE("loyalty draws an animal to its herd") {
    std::array<double, 2> odds{};
    for (const int loyal : {0, 1}) {
        Simulation simulation = world();
        Bitzkey& loner = simulation.add_agent(encode_dna(Species::Grazer, {{"sight", 1}}), 2, 2);
        simulation.add_agent(loner.dna, 6, 2);  // four cells off: far enough from its herd to herd
        simulation.index_cells();
        loner.mind[Loyalty] = 7.0 * loyal;
        simulation.think(loner);
        odds[static_cast<std::size_t>(loyal)] = loner.odds[Herd];
    }
    CHECK(odds[1] > odds[0]);
}

TEST_CASE("the most dominant willing partner is chosen, and crowding troubles the dominant less") {
    Simulation simulation = world();
    for (int x = 0; x < 12; ++x) {
        for (int y = 0; y < 12; ++y) simulation.grass_cell(x, y) = 3;
    }
    Bitzkey& suitor = simulation.add_agent(encode_dna(Species::Grazer), 5, 5);
    Bitzkey& first = simulation.add_agent(suitor.dna, 5, 6);
    Bitzkey& second = simulation.add_agent(suitor.dna, 6, 5);
    simulation.index_cells();
    for (Bitzkey* dominant : {&first, &second}) {
        first.mind[Dominance] = second.mind[Dominance] = 0.0;
        dominant->mind[Dominance] = 7.0;
        CHECK(simulation.sense(suitor).situation.mate == dominant);
    }
    Simulation::Situation crowded;
    crowded.rivalry = 1.0;
    std::array<double, 2> comfort{};
    for (const int dominant : {0, 1}) {
        suitor.mind[Dominance] = 7.0 * dominant;
        comfort[static_cast<std::size_t>(dominant)] = simulation.comfort(suitor, crowded);
    }
    CHECK(comfort[0] == doctest::Approx(3 * comfort[1]));  // 1.5 times the unease, against half
}

TEST_CASE("trust sets how far off an alert is heeded and how much is taken in") {
    Simulation simulation = world();
    Bitzkey& lookout = simulation.add_agent(encode_dna(Species::Grazer, {{"sight", 7}}), 2, 2);
    Bitzkey& near = simulation.add_agent(lookout.dna, 3, 2);
    Bitzkey& far = simulation.add_agent(lookout.dna, 2, 9);  // seven cells off
    const Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator), 9, 9);
    near.mind[Trust] = far.mind[Trust] = 1.0;  // heeds herd-mates up to twice its sight times 1/7 off: 6.6 cells
    simulation.index_cells();
    simulation.alert(lookout, hunter);
    CHECK(near.alarm == hunter.identifier);
    CHECK_FALSE(far.alarm.has_value());

    const Dna bright = encode_dna(Species::Grazer, {{"empathy", 7}, {"intellect", 7}, {"sight", 1}, {"trust", 7}});
    Bitzkey& actor = simulation.add_agent(bright, 8, 2);
    actor.has_last_senses = true;
    Bitzkey& watcher = simulation.add_agent(bright, 8, 4);
    simulation.index_cells();
    simulation.witness(actor, -1.0);
    CHECK(watcher.reward == doctest::Approx(-SOCIAL_MOOD * 1.5));  // half as much again
}

TEST_CASE("kinship shuns herd-mates and partners of another social background") {
    Simulation simulation = world();
    Bitzkey& clannish = simulation.add_agent(encode_dna(Species::Grazer), 5, 5);
    const Bitzkey& stranger = simulation.add_agent(flipped(clannish.dna, 0, 0xFE000000U), 5, 6);  // its social background a quarter apart
    clannish.mind[Kinship] = 7.0;
    CHECK_FALSE(Simulation::willing(clannish, stranger));
    CHECK_FALSE(simulation.herd_mate(clannish, stranger));
    clannish.mind[Tolerance] = 7.0;  // tolerance widens it
    CHECK(Simulation::willing(clannish, stranger));
    clannish.mind[Tolerance] = clannish.mind[Kinship] = 0.0;
    CHECK(Simulation::willing(clannish, stranger));
}

TEST_CASE("memory brings an animal back to where it fed and warns it where it was struck") {
    Simulation simulation = world();
    Bitzkey& eater = simulation.add_agent(encode_dna(Species::Grazer, {{"memory", 7}}), 5, 5);
    simulation.grass_cell(5, 5) = 1;
    simulation.index_cells();
    Simulation::Situation nothing;
    simulation.act(eater, Eat, nothing);
    REQUIRE(eater.food_memory.has_value());
    CHECK((eater.food_memory->x == 5 && eater.food_memory->y == 5 && eater.food_memory->turn == simulation.turn()));

    Bitzkey& grazer = simulation.add_agent(eater.dna, 5, 9);
    grazer.food_memory = Place{5, 5, simulation.turn() - 10};
    simulation.index_cells();
    Simulation::Sensed sensed = simulation.sense(grazer);
    CHECK(sensed.senses[SenseFoodRemembered] == doctest::Approx(1 - 10.0 / MEMORY_TURNS));
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), Revisit) != sensed.options.end());
    simulation.act(grazer, Revisit, sensed.situation);
    CHECK((grazer.x == 5 && grazer.y == 7 && grazer.last_action == "revisited"));
    grazer.mind[Memory] = 0.0;  // no memory: nothing to go back to
    sensed = simulation.sense(grazer);
    CHECK(sensed.senses[SenseFoodRemembered] == 0.0);
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), Revisit) == sensed.options.end());

    grazer.mind[Memory] = 7.0;
    const Bitzkey& striker = simulation.add_agent(encode_dna(Species::Predator), 5, 8);
    simulation.index_cells();
    simulation.struck(grazer, striker);
    CHECK(simulation.sense(grazer).senses[SenseDangerRemembered] == 1.0);
}

TEST_CASE("resilience eases fright and loss, and moodiness sets how far experience moves the mind") {
    Simulation simulation = world();
    Bitzkey& fragile = simulation.add_agent(encode_dna(Species::Grazer, {{"resilience", 0}}), 3, 3);
    Bitzkey& hardy = simulation.add_agent(encode_dna(Species::Grazer, {{"resilience", 7}}), 8, 8);
    for (Bitzkey* animal : {&fragile, &hardy}) {
        simulation.experience(*animal, 3);  // wounded
        animal->seen_failure = 1.0;
    }
    CHECK(fragile.mind[Fear] == doctest::Approx(3 * hardy.mind[Fear]));  // 1.5 times the fright, against half
    simulation.take_turn();
    CHECK(fragile.seen_failure == doctest::Approx(std::pow(SOCIAL_MEMORY, 0.5)));
    CHECK(hardy.seen_failure == doctest::Approx(std::pow(SOCIAL_MEMORY, 1.5)));

    Bitzkey& steady = simulation.add_agent(encode_dna(Species::Predator, {{"moodiness", 0}}), 3, 8);
    Bitzkey& moody = simulation.add_agent(encode_dna(Species::Predator, {{"moodiness", 7}}), 8, 3);
    for (Bitzkey* animal : {&steady, &moody}) simulation.experience(*animal, 0);  // a kill
    CHECK(moody.mind[Aggression] == doctest::Approx(3 * steady.mind[Aggression]));
}

TEST_CASE("animals shun mates whose ring or dot differ too far, and tolerance widens that") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"size", 2}, {"fear", 3}});
    Bitzkey& animal = simulation.add_agent(dna, 1, 1);
    const Bitzkey& close = simulation.add_agent(flipped(dna, (0x08ULL << 56) ^ 0x10), 2, 2);
    const Bitzkey& ringed = simulation.add_agent(flipped(dna, 0x40ULL << 56), 3, 3);
    Bitzkey& strange = simulation.add_agent(flipped(dna, (0xFFULL << 56) ^ 0xFF), 4, 4);
    CHECK(Simulation::compatible(animal, close));
    CHECK_FALSE(Simulation::compatible(animal, ringed));
    animal.mind[Tolerance] = 7;
    CHECK(Simulation::willing(animal, strange));
    CHECK_FALSE(Simulation::compatible(animal, strange));  // but the stranger shuns it
    strange.mind[Tolerance] = 2;
    CHECK(Simulation::compatible(animal, strange));
}

TEST_CASE("predators hunt other kinds of predator only past their reluctance, and never their own") {
    Simulation simulation = world();
    Bitzkey& gentle = simulation.add_agent(encode_dna(Species::Predator, {{"empathy", 7}, {"tolerance", 7}, {"patience", 7}}), 1, 1);
    const Bitzkey& kin = simulation.add_agent(flipped(gentle.dna, 0xFFULL << 56), 2, 1);       // 25% apart: its kind
    const Bitzkey& stranger = simulation.add_agent(flipped(gentle.dna, 0xFFFFULL << 48), 3, 1);  // 50% apart
    CHECK(Simulation::same_kind(gentle, kin));
    CHECK_FALSE(Simulation::same_kind(gentle, stranger));
    gentle.hunger = 30;
    CHECK_FALSE(Simulation::is_prey(gentle, stranger));
    CHECK_FALSE(Simulation::is_prey(gentle, kin));
    gentle.own_rivalry = 0.8;  // reluctance worn down by RIVALRY_WEAR of 0.8, to 0.6
    CHECK_FALSE(Simulation::is_prey(gentle, stranger));
    gentle.hunger = 70;
    CHECK(Simulation::is_prey(gentle, stranger));
    gentle.hunger = 99;
    CHECK_FALSE(Simulation::is_prey(gentle, kin));  // never its own kind
}

TEST_CASE("predators breed with their kind, and omnivores and predators prey on each other") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Predator, {{"tolerance", 0}});
    const Bitzkey& first = simulation.add_agent(dna, 1, 1);
    const Bitzkey& kin = simulation.add_agent(flipped(dna, 0xFFULL << 56), 2, 2);
    const Bitzkey& stranger = simulation.add_agent(flipped(dna, 0xFFFFULL << 48), 3, 3);
    CHECK(Simulation::willing(first, kin));
    CHECK_FALSE(Simulation::willing(first, stranger));
    const Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore), 4, 4);
    CHECK(Simulation::is_prey(omnivore, first));
    CHECK(Simulation::rival_weight(first, omnivore) == 0.0);
    CHECK(Simulation::rival_weight(omnivore, first) == 0.0);
}

TEST_CASE("a predator's weapons count three times, and the best-armed kill the largest grazer in three") {
    Simulation simulation = world();
    const Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}, {"armour", 7}}), 5, 5);
    const Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}, {"strength", 7}, {"weapons", 7}}), 5, 6);
    const Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}, {"strength", 7}, {"weapons", 7}}), 6, 5);
    CHECK(Simulation::damage(hunter, grazer) > Simulation::damage(omnivore, grazer));
    CHECK(3 * Simulation::damage(hunter, grazer) >= grazer.max_health);
    CHECK(2 * Simulation::damage(hunter, grazer) < grazer.max_health);
}

TEST_CASE("primal fear grows with the disparity, spares predators, and lasts while a hunter is in sight") {
    Simulation simulation = world();
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 0}}), 5, 5);
    const Bitzkey& brute = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}, {"strength", 7}, {"weapons", 7}}), 5, 8);
    const Bitzkey& weakling = simulation.add_agent(encode_dna(Species::Predator, {{"size", 0}}), 9, 9);
    CHECK(Simulation::disparity(grazer, brute) > 0.9);
    CHECK(Simulation::disparity(brute, grazer) == 0.0);
    CHECK(Simulation::disparity(grazer, brute) > Simulation::disparity(grazer, weakling));
    grazer.primal_fear = true;
    CHECK(simulation.primal_fear(grazer, &brute));
    CHECK_FALSE(simulation.primal_fear(grazer, nullptr));
    CHECK_FALSE(grazer.primal_fear);
}

TEST_CASE("predators see prey further, and their young are predators") {
    Simulation simulation = world();
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"sight", 0}}), 1, 1);
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore, {{"sight", 0}}), 1, 8);
    const Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 1, 5);
    simulation.index_cells();
    CHECK(simulation.sense(predator).situation.quarry == &grazer);
    CHECK(simulation.sense(omnivore).situation.quarry == nullptr);
    for (const Species species : {Species::Plant, Species::Grazer, Species::Omnivore, Species::Predator}) {
        const Dna made = predator_dna(encode_dna(species, {{"size", 5}}));
        CHECK(static_cast<Species>(decode_body(made)[SpeciesTrait]) == Species::Predator);
        CHECK(decode_body(made)[Size] == 5);
    }
    CHECK(predator_dna(encode_dna(Species::Predator)) == encode_dna(Species::Predator));
}

TEST_CASE("species evolve along the chain, and plants stay plants") {
    // (before, the species bit flipped, after)
    const std::array<std::tuple<Species, int, Species>, 8> expected = {{
        {Species::Plant, 0, Species::Plant}, {Species::Plant, 1, Species::Plant},
        {Species::Grazer, 0, Species::Omnivore}, {Species::Grazer, 1, Species::Predator},
        {Species::Omnivore, 0, Species::Predator}, {Species::Omnivore, 1, Species::Grazer},
        {Species::Predator, 0, Species::Predator}, {Species::Predator, 1, Species::Omnivore},
    }};
    for (const auto& [before, bit, after] : expected) {
        const Dna parent = encode_dna(before, {{"size", 3}});
        const Dna child = evolved(parent, flipped(parent, std::uint64_t{1} << (32 + bit)));
        CHECK(static_cast<Species>(decode_body(child)[SpeciesTrait]) == after);
        CHECK(decode_body(child)[Size] == 3);
    }
    CHECK(evolved(encode_dna(Species::Grazer), encode_dna(Species::Grazer)) == encode_dna(Species::Grazer));
}

TEST_CASE("a body is eaten a portion at a time, and gone with the last") {
    Simulation simulation = world();
    Bitzkey& corpse = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 1}}), 5, 5);  // two portions
    corpse.health = 0;
    simulation.die(corpse, "killed");
    CHECK(corpse.portions == 2);
    Bitzkey& first = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}}), 5, 6);
    Bitzkey& second = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}}), 4, 5);
    simulation.index_cells();
    first.hunger = second.hunger = 80;
    simulation.feast(first, corpse);
    const double meal = FRESH_KILL_MEALS * (KILL_MEAL + KILL_MEAL_FOR_SIZE * corpse.physique(Size));
    CHECK(first.hunger == doctest::Approx(std::max(0.0, 80 - meal / first.stomach)));  // a whole feast
    CHECK(corpse.portions == 1);
    CHECK(*corpse.died_turn == simulation.turn());
    simulation.feast(second, corpse);
    CHECK(corpse.portions == 0);
    CHECK(corpse.meat == 0.0);
    simulation.feast(first, corpse);
    CHECK(first.last_action == "sought a kill");
}

TEST_CASE("deaths are counted by species and cause, and rewound") {
    Simulation world_now(30, 30, 60, 4);
    for (int turn = 0; turn < 60; ++turn) world_now.advance();
    const auto total = [&] {
        int count = 0;
        for (const auto& causes : world_now.death_causes()) {
            for (const auto& [cause, n] : causes) count += n;
        }
        return count;
    };
    CHECK(total() == world_now.deaths());
    const std::vector<Death> deaths = world_now.deaths_so_far();
    CHECK(static_cast<int>(deaths.size()) == world_now.deaths());  // every death back to the start
    const auto lines = death_report_lines(deaths);
    CHECK(lines.front().rfind("CAUSE", 0) == 0);
    const auto starts = [&](std::string_view section) {
        return std::find_if(lines.begin(), lines.end(), [&](const std::string& line) { return line.rfind(section, 0) == 0; });
    };
    CHECK(starts("ALL ")->ends_with(" " + std::to_string(world_now.deaths())));  // the total of totals
    CHECK(starts("WHO KILLED WHOM") != lines.end());
    CHECK(starts("THE DEADLIEST KILLERS") != lines.end());
    int killings = 0, counted = 0;
    for (const Death& death : deaths) killings += death.killer >= 0;
    for (auto line = starts("WHAT THE KILLERS WERE DOING") + 2; line != lines.end() && !line->empty(); ++line) {
        counted += std::stoi(line->substr(line->find_last_of(' ') + 1));
    }
    CHECK(counted == killings);
    CHECK(total() == world_now.deaths());
    CHECK(static_cast<int>(world_now.deaths_so_far().size()) == world_now.deaths());
    CHECK(death_report_lines(world_now.death_tally()) == lines);  // counted as they happened, the same as from them all
}

TEST_CASE("deaths are counted by age at death, in bands of days, the youngest first") {
    DeathTally tally;
    for (const int days : {0, 5, 9, 45, 400, 2000, -1}) tally.add({"starved", Species::Grazer, 1, -1, Species::Plant, "", days});
    tally.add({"killed", Species::Predator, 1, -1, Species::Plant, "", 100});
    const DeathReport report = death_report(tally);
    std::vector<std::pair<std::string, int>> rows;
    for (const DeathReport::Row& row : report.ages) rows.emplace_back(row.label, row.total);
    CHECK(rows == std::vector<std::pair<std::string, int>>{
                      {"under a day", 1}, {"1-9 days", 2}, {"30-89 days", 1}, {"90-179 days", 1}, {"1-2 years", 1}, {"over 2 years", 1}});
    CHECK(report.ages[3].counts[3] == 1);  // the predator
    const auto lines = death_report_lines(tally);
    CHECK(std::find(lines.begin(), lines.end(), "AGE AT DEATH, by species") != lines.end());
}

TEST_CASE("an animal's age at death is recorded in days") {
    Simulation simulation(Simulation::Settings{20, 20, 0, 1});
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 5, 5);
    grazer.age = 3 * DAY_LENGTH + 1;
    grazer.health = 0;
    simulation.die(grazer, "starved");
    CHECK(simulation.deaths_so_far().back().age_days == 3);
}

TEST_CASE("deaths are kept whole only for the latest turns, and the tally keeps only the deadliest killers gone") {
    DeathTally tally;
    for (int killer = 0; killer < DEADLIEST + 5; ++killer) {
        for (int kill = 0; kill <= killer; ++kill) tally.add({"killed", Species::Grazer, 1, killer, Species::Predator, "hunting"});
    }
    tally.add({"starved", Species::Grazer, 1});
    CHECK((tally.killers == DEADLIEST + 5 && tally.whom[1][3] == (DEADLIEST + 5) * (DEADLIEST + 6) / 2));
    tally.let_go([](int identifier) { return identifier == 0; });  // the least deadly still alive
    CHECK(tally.by_killer.size() == static_cast<std::size_t>(DEADLIEST + 1));
    CHECK((tally.by_killer.contains(0) && !tally.by_killer.contains(1) && tally.by_killer.contains(DEADLIEST + 4)));
    CHECK(tally.killers == DEADLIEST + 5);  // still counted
    CHECK((tally.causes.at("starved")[1] == 1 && tally.activities.at("hunting")[3] == tally.whom[1][3]));

    const double saved = CLIMATE_DAMAGE, starving = STARVING_DAMAGE;
    CLIMATE_DAMAGE = STARVING_DAMAGE = 0;  // a world that lasts, whatever the platform's chance
    Simulation world(Simulation::Settings{20, 20, 40, 3});
    for (int turn = 0; turn < RECENT_DEATH_TURNS + 50 && !world.ended(); ++turn) world.advance();
    CLIMATE_DAMAGE = saved;
    STARVING_DAMAGE = starving;
    REQUIRE(world.turn() > RECENT_DEATH_TURNS);
    for (const Death& death : world.deaths_so_far()) CHECK(death.turn >= world.turn() - RECENT_DEATH_TURNS);
    CHECK(death_report(world.death_tally()).all.total == world.deaths());  // yet every death counted
}

TEST_CASE("each death records its cause, its killer and what the killer was doing") {
    Simulation simulation = world();
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}, {"strength", 7}}), 4, 4);
    Bitzkey& prey = simulation.add_agent(encode_dna(Species::Grazer, {{"weapons", 0}}), 4, 5);
    Bitzkey& starved = simulation.add_agent(encode_dna(Species::Grazer), 9, 9);
    simulation.index_cells();
    prey.health = 0.1;
    simulation.attack(hunter, prey, "hunting");
    const Death* killed = simulation.death_record(prey.identifier);
    REQUIRE(killed);
    CHECK((killed->cause == "killed" && killed->species == Species::Grazer && killed->turn == simulation.turn()));
    CHECK((killed->killer == hunter.identifier && killed->killer_species == Species::Predator && killed->activity == "hunting"));
    simulation.die(starved, "starved");
    CHECK((simulation.death_record(starved.identifier)->killer == -1 && simulation.death_record(starved.identifier)->activity.empty()));

    Bitzkey& spiny = simulation.add_agent(encode_dna(Species::Grazer, {{"weapons", 7}, {"size", 7}}), 7, 2);
    Bitzkey& reckless = simulation.add_agent(encode_dna(Species::Predator), 7, 3);
    Bitzkey& defender = simulation.add_agent(encode_dna(Species::Grazer, {{"weapons", 7}, {"size", 7}, {"strength", 7}}), 2, 8);
    Bitzkey& assailant = simulation.add_agent(encode_dna(Species::Omnivore), 2, 9);
    simulation.index_cells();
    reckless.health = 0.1;
    simulation.attack(reckless, spiny);
    CHECK((simulation.death_record(reckless.identifier)->killer == spiny.identifier &&
           simulation.death_record(reckless.identifier)->activity == "spines"));
    assailant.health = 0.1;
    simulation.defend(defender, assailant);
    CHECK(simulation.death_record(assailant.identifier)->activity == "defending");
}

TEST_CASE("a brain matrix shows each situation its species meets") {
    Simulation simulation = world();
    const Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 5, 5);
    const auto rows = brain_matrix(grazer);
    CHECK(!rows.empty());
    for (const BrainRow& row : rows) {
        double sum = 0;
        for (const auto& [action, chance] : row.chances) sum += chance;
        CHECK(sum == doctest::Approx(1.0));
    }
    const auto lines = brain_matrix_lines(grazer);
    CHECK(lines.size() == rows.size() + 1);
    CHECK(lines.front().find("hunt") == std::string::npos);  // a grazer never meets a chance to hunt
}

TEST_CASE("predators loom larger in primal fear, and feel half their fear") {
    Simulation simulation = world();
    const auto body = [](Species species, int size, int strength) {
        return encode_dna(species, {{"size", size}, {"strength", strength}, {"weapons", 0}, {"armour", 0}});
    };
    const Bitzkey& grazer = simulation.add_agent(body(Species::Grazer, 3, 3), 5, 5);
    const Bitzkey& omnivore = simulation.add_agent(body(Species::Omnivore, 3, 3), 6, 5);
    const Bitzkey& predator = simulation.add_agent(body(Species::Predator, 3, 3), 7, 5);
    CHECK(Simulation::disparity(grazer, omnivore) == 0.0);
    CHECK(Simulation::disparity(grazer, predator) > 0.0);
    CHECK(Simulation::disparity(predator, omnivore) == 0.0);
    const Bitzkey& bigger = simulation.add_agent(body(Species::Omnivore, 5, 5), 8, 5);
    CHECK(Simulation::disparity(bigger, predator) == doctest::Approx(0.0));
    const Bitzkey& fearful = simulation.add_agent(encode_dna(Species::Predator, {{"fear", 7}}), 9, 9);
    CHECK(fearful.fearfulness() == doctest::Approx(0.5));
}

TEST_CASE("predators think twice as well, hard-won experiences sharpen and hardship dulls") {
    Simulation simulation = world();
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"intellect", 3}}), 5, 5);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"intellect", 3}}), 6, 5);
    CHECK(predator.intelligence() == doctest::Approx(6.0 / 7));
    CHECK(grazer.intelligence() == doctest::Approx(3.0 / 7));
    CHECK(thinking_intellect(3, Species::Predator) == 6);
    CHECK(thinking_intellect(5, Species::Predator) == 7);
    CHECK(thinking_intellect(3, Species::Grazer) == 3);
    CHECK((predator.brain->depth() == 4 && predator.brain->width() == 48));  // its doubled intellect, 6
    double before = grazer.mind[Intellect];
    simulation.experience(grazer, 9);   // discovery
    CHECK(grazer.mind[Intellect] > before);
    before = grazer.mind[Intellect];
    simulation.experience(grazer, 13);  // lesson
    simulation.experience(grazer, 8);   // foraged meal: neither sharpens the mind now
    CHECK(grazer.mind[Intellect] == before);
    for (const int hardship : {7, 3, 12}) {  // starving, wounded, panicked
        simulation.experience(grazer, hardship);
        CHECK(grazer.mind[Intellect] < before);
        before = grazer.mind[Intellect];
    }
}

TEST_CASE("a cleverer brain burns more food, a predator's half as much more, and a plant's none") {
    Simulation simulation = world();
    const auto burn = [&](Species species, int intellect) { return simulation.add_agent(encode_dna(species, {{"intellect", intellect}}), 5, 5).burn; };
    CHECK(burn(Species::Grazer, 7) / burn(Species::Grazer, 0) == doctest::Approx(1 + BRAIN_COST));
    CHECK(burn(Species::Omnivore, 7) / burn(Species::Omnivore, 0) == doctest::Approx(1 + BRAIN_COST));
    CHECK(burn(Species::Predator, 7) / burn(Species::Predator, 0) == doctest::Approx(1 + BRAIN_COST * PREDATOR_BRAIN_COST));
    CHECK(burn(Species::Plant, 7) == burn(Species::Plant, 0));
    CHECK((BRAIN_COST == 0.15 && PREDATOR_BRAIN_COST == 0.5));
}

TEST_CASE("weak grazers cannot strike back") {
    Simulation simulation = world();
    Bitzkey& weak = simulation.add_agent(encode_dna(Species::Grazer, {{"strength", 5}}), 5, 5);
    Bitzkey& strong = simulation.add_agent(encode_dna(Species::Grazer, {{"strength", 6}}), 9, 9);
    const Bitzkey& first = simulation.add_agent(encode_dna(Species::Predator), 5, 6);
    const Bitzkey& second = simulation.add_agent(encode_dna(Species::Predator), 9, 10);
    simulation.index_cells();
    weak.assailant = first.identifier;
    strong.assailant = second.identifier;
    const auto offers = [&](Bitzkey& grazer) {
        const auto options = simulation.sense(grazer).options;
        return std::find(options.begin(), options.end(), Defend) != options.end();
    };
    CHECK_FALSE(offers(weak));
    CHECK(offers(strong));
}

TEST_CASE("omnivores fight their own only in an overcrowded territory") {
    Simulation simulation = world();
    Bitzkey& ruthless = simulation.add_agent(encode_dna(Species::Omnivore), 1, 1);
    const Bitzkey& other = simulation.add_agent(encode_dna(Species::Omnivore), 2, 1);
    ruthless.hunger = 50;
    CHECK_FALSE(Simulation::is_prey(ruthless, other));
    ruthless.own_rivalry = 0.5;
    CHECK(Simulation::is_prey(ruthless, other));
}

TEST_CASE("territory: the less empathetic tolerate fewer rivals, and only their own species crowds them") {
    Simulation simulation = world(30);
    Bitzkey& callous = simulation.add_agent(encode_dna(Species::Predator, {{"empathy", 0}, {"sight", 1}}), 15, 15);  // a territory of five cells
    for (const int x : {11, 12, 13, 17, 18, 19}) simulation.add_agent(encode_dna(Species::Omnivore), x, 15);
    simulation.index_cells();
    CHECK(simulation.territory(callous) == std::pair{0.0, 0.0});  // omnivores are prey, not rivals
    for (const int x : {14, 16, 15, 13, 17, 12}) simulation.add_agent(callous.dna, x, 16);  // its pack
    simulation.index_cells();
    CHECK(simulation.territory(callous) == std::pair{0.0, 0.0});  // is no rival
    for (const int x : {14, 16, 15, 13, 17, 12}) simulation.add_agent(flipped(encode_dna(Species::Predator), 0xFFFFULL << 48), x, 14);  // another kind
    simulation.index_cells();
    CHECK(simulation.territory(callous) == std::pair{1.0, 1.0});
}

TEST_CASE("the herd action moves toward the middle of the herd, fear setting how close it keeps") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"empathy", 7}, {"patience", 7}});
    Bitzkey& fearful = simulation.add_agent(encode_dna(Species::Grazer, {{"empathy", 7}, {"patience", 7}, {"fear", 7}, {"sight", 1}}), 2, 5);  // sees its herd four off
    for (const auto& [x, y] : std::vector<std::pair<int, int>>{{5, 4}, {5, 6}, {6, 5}}) simulation.add_agent(dna, x, y);
    simulation.index_cells();
    auto sensed = simulation.sense(fearful);
    REQUIRE(std::find(sensed.options.begin(), sensed.options.end(), Herd) != sensed.options.end());
    CHECK(sensed.situation.herd_middle == Cell{5, 5});
    simulation.act(fearful, Herd, sensed.situation);
    CHECK(fearful.x == 4);
    CHECK(fearful.last_action == "herded");
}

TEST_CASE("striking a herd-mate alarms the herd and rouses those beside the attacker") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"empathy", 7}, {"patience", 7}, {"sight", 7}});
    Bitzkey& victim = simulation.add_agent(dna, 5, 5);
    const Bitzkey& beside = simulation.add_agent(dna, 6, 6);
    const Bitzkey& near = simulation.add_agent(dna, 3, 5);
    const Bitzkey& far = simulation.add_agent(dna, 9, 9);
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 6, 5);
    simulation.index_cells();
    simulation.attack(predator, victim);
    CHECK(beside.alarm.value_or(-1) == predator.identifier);
    CHECK(near.alarm.value_or(-1) == predator.identifier);
    CHECK_FALSE(far.alarm.has_value());
    CHECK(beside.herd_assailant.value_or(-1) == predator.identifier);
    CHECK_FALSE(near.herd_assailant.has_value());
}

TEST_CASE("young start with their parents' current minds, which their dots show") {
    Simulation simulation = world();
    simulation.set_mutation(0, 50);
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"fear", 2}, {"curiosity", 5}, {"libido", 6}});
    Bitzkey& first = simulation.add_agent(dna, 5, 5);
    Bitzkey& second = simulation.add_agent(dna, 5, 6);
    first.mind[Fear] = 4.0;
    first.mind[Curiosity] = 3.0;
    first.mind[Libido] = 7.0;
    second.mind[Fear] = 3.0;
    second.mind[Curiosity] = 5.0;
    second.mind[Libido] = 7.0;
    simulation.index_cells();
    simulation.breed(first, second);
    simulation.give_birth(first);
    const Bitzkey& child = *simulation.agents().back();
    CHECK(child.mind[Fear] == 3.5);
    CHECK(child.mind[Curiosity] == 4.0);
    CHECK(child.mind[Libido] == 7.0);
    CHECK((child.inborn[Fear] == 3 || child.inborn[Fear] == 4));
    CHECK(child.inborn[Curiosity] == 4);
}

TEST_CASE("each species weighs the food it eats against the mouths sharing it") {
    Simulation simulation = world(30);
    simulation.grass_cell(15, 13) = 4;
    simulation.grass_cell(16, 15) = 2;
    const Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 15, 15);
    const Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore), 14, 15);
    const Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 17, 15);
    simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Hot}}), 15, 17);  // at home on the equator
    simulation.add_agent(encode_dna(Species::Grazer), 15, 18);
    simulation.index_cells();
    simulation.set_grass_regrowth(0);  // judge the standing grass alone
    CHECK(simulation.food_supply(grazer) == doctest::Approx(1.25 * (0.25 * 6 + 2) / (1 + 1 + 0.5)));
    CHECK(simulation.food_supply(omnivore) == doctest::Approx((0.5 * 0.25 * 6 + 0.75 * 2 + 2 * PREY_MEALS) / (1 + 0.5)));
    CHECK(simulation.food_supply(predator) == doctest::Approx(3 * PREY_MEALS));
    simulation.add_agent(flipped(encode_dna(Species::Predator), 0xFFFFULL << 48), 18, 16);  // a rival of another kind, not food
    simulation.index_cells();
    CHECK(simulation.food_supply(predator) == doctest::Approx(3 * PREY_MEALS / 2.0));
    simulation.add_agent(predator.dna, 18, 14);  // a pack-mate shares its kills, so counts as only part of a mouth
    simulation.index_cells();
    CHECK(simulation.food_supply(predator) == doctest::Approx(3 * PREY_MEALS / (2 + PACK_MOUTHS)));
}

TEST_CASE("parents conceive only as many young as they can feed, but always at least one") {
    const double newborn_food = NEWBORN_FOOD, small_litters = SMALL_LITTERS;
    SMALL_LITTERS = 1.0;  // the usual litters, without the small-body boost
    const double plants_per_grazer = PLANTS_PER_GRAZER;
    PLANTS_PER_GRAZER = 0;  // no plants about: breeding left to the rest
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"litter", 7}});
    struct Case {
        double food, hunger;
        int young;
    };
    for (const Case& test : {Case{5.0, 0.0, 6}, Case{5.0, 90.0, 4}, Case{5.0, 98.0, 1}, Case{60.0, 0.0, 1}}) {
        // Six young by their traits: 25% of a stomach each could feed ten at 5% (however little the fertile pay), 10% each
        // left only four, 2% each none, and 50% between them none at 60%; but always at least one.
        NEWBORN_FOOD = test.food;
        Simulation simulation = world();
        Bitzkey& first = simulation.add_agent(dna, 5, 5);
        simulation.add_agent(dna, 5, 6);
        simulation.index_cells();
        first.hunger = simulation.agents()[1]->hunger = test.hunger;
        simulation.breed(first, *simulation.agents()[1]);
        simulation.give_birth(first);
        CHECK(simulation.agents().size() == static_cast<std::size_t>(2 + test.young));
    }
    SMALL_LITTERS = small_litters;
    NEWBORN_FOOD = newborn_food;
    PLANTS_PER_GRAZER = plants_per_grazer;
}

TEST_CASE("a grazer's breeding follows the plants in its home band") {
    const double newborn_food = NEWBORN_FOOD, small_litters = SMALL_LITTERS, plants_per_grazer = PLANTS_PER_GRAZER;
    NEWBORN_FOOD = 0;  // no limit from the energy given
    SMALL_LITTERS = 1.0;
    PLANTS_PER_GRAZER = 0.5;
    Simulation simulation = world(30);  // temperate bands: rows 6-11 and 18-23
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"litter", 7}});  // at home in the temperate bands
    Bitzkey& first = simulation.add_agent(dna, 15, 8);
    Bitzkey& second = simulation.add_agent(dna, 15, 9);
    simulation.add_agent(encode_dna(Species::Plant), 3, 20);  // the southern temperate band: not its home band
    simulation.index_cells();
    auto offers_breed = [&] {
        const auto options = simulation.sense(first).options;
        return std::find(options.begin(), options.end(), Breed) != options.end();
    };
    CHECK(simulation.home_plants(first) == 0.0);
    CHECK_FALSE(offers_breed());
    simulation.add_agent(encode_dna(Species::Plant), 3, 7);
    simulation.index_cells();
    CHECK(simulation.home_plants(first) == 0.5);  // one plant shared by two grazers
    CHECK(offers_breed());
    simulation.breed(first, second);
    simulation.give_birth(first);
    CHECK(simulation.agents().size() == 4 + 3);  // six young, halved: half the plants of a full litter
    PLANTS_PER_GRAZER = plants_per_grazer;
    SMALL_LITTERS = small_litters;
    NEWBORN_FOOD = newborn_food;
}

TEST_CASE("scarce food stops breeding, and plenty allows full litters") {
    const double food_needed = FOOD_NEEDED, newborn_food = NEWBORN_FOOD;
    FOOD_NEEDED = 3.0;  // food limiting breeding, as it may be set to
    NEWBORN_FOOD = 0.0;  // no limit from the energy given
    const double plants_per_grazer = PLANTS_PER_GRAZER;
    PLANTS_PER_GRAZER = 0;  // no plants about: breeding left to the rest
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"litter", 7}});
    Bitzkey& first = simulation.add_agent(dna, 5, 5);
    simulation.add_agent(dna, 5, 6);
    simulation.index_cells();
    simulation.set_grass_regrowth(0);  // a barren world
    auto has_breed = [&] {
        const auto options = simulation.sense(first).options;
        return std::find(options.begin(), options.end(), Breed) != options.end();
    };
    CHECK_FALSE(has_breed());
    for (int x = 0; x < 12; ++x) {
        for (int y = 0; y < 12; ++y) simulation.grass_cell(x, y) = 3;
    }
    CHECK(has_breed());
    const double small_litters = SMALL_LITTERS;
    SMALL_LITTERS = 1.0;  // the usual litters, without the small-body boost
    simulation.breed(first, *simulation.agents()[1]);
    simulation.give_birth(first);
    SMALL_LITTERS = small_litters;
    CHECK(simulation.agents().size() == 2 + 6);
    FOOD_NEEDED = food_needed;
    NEWBORN_FOOD = newborn_food;
    PLANTS_PER_GRAZER = plants_per_grazer;
}

TEST_CASE("a distant hunter is watched but not fled, and a herd flees only hunters closer still") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"empathy", 7}, {"patience", 7}, {"sight", 7}});
    Bitzkey& grazer = simulation.add_agent(dna, 5, 6);
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 5, 2);
    simulation.index_cells();
    const auto offers_flight = [&] {
        const auto sensed = simulation.sense(grazer);
        return std::find(sensed.options.begin(), sensed.options.end(), Flee) != sensed.options.end();
    };
    CHECK(simulation.sense(grazer).situation.hunter == &predator);
    CHECK_FALSE(offers_flight());  // four steps off
    predator.y = 3;
    simulation.index_cells();
    CHECK(offers_flight());  // three steps off
    for (const auto& [x, y] : std::vector<std::pair<int, int>>{{4, 6}, {6, 6}, {5, 7}}) simulation.add_agent(dna, x, y);
    simulation.index_cells();
    CHECK(Simulation::flee_range(grazer, 3) == doctest::Approx(1.5));
    CHECK_FALSE(offers_flight());  // its herd watches with it
}

TEST_CASE("grazers are the plant and grass specialists, and omnivores live on meat") {
    Simulation simulation = world();
    const Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 1, 1);
    const Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore), 2, 2);
    const Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 3, 3);
    CHECK(grazer.grazing == 1.25);
    CHECK(omnivore.grazing == 0.5);
    CHECK(omnivore.browsing == 0.75);
    CHECK(grazer.browsing == 1.25);
    CHECK(predator.browsing == 0.0);
}

TEST_CASE("plants seed less the more plants shade them") {
    Simulation simulation = world(30);
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"fertility", 7}, {"size", 7}}), 15, 15);
    for (int x = 16; x <= 18; ++x) simulation.add_agent(encode_dna(Species::Plant, {{"size", 7}}), x, 15);  // three within three cells, living ten years
    const std::size_t before = simulation.agents().size();
    const double damage = CLIMATE_DAMAGE;
    CLIMATE_DAMAGE = 0;  // out of their temperature through the winters, the plants wither but live
    for (int turn = 0; turn < 500; ++turn) {
        plant.hunger = 0;
        simulation.take_turn();
    }
    CLIMATE_DAMAGE = damage;
    // Shaded by three plants, the first never seeds; its neighbours, less shaded, may.
    CHECK(plant.children == 0);
    CHECK(simulation.agents().size() >= before);
}

TEST_CASE("hunters switch away from prey that is scarce nearby") {
    const double switch_share = SWITCH_SHARE;
    SWITCH_SHARE = 0.25;  // the switch measured at a quarter
    Simulation simulation = world(30);
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore, {{"sight", 7}}), 15, 15);
    const Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 15, 16);
    for (int x = 12; x < 16; ++x) simulation.add_agent(encode_dna(Species::Plant), x, 13);  // within six steps
    simulation.index_cells();
    CHECK(simulation.prey_switching(omnivore).first == false);  // a grazer against four plants
    simulation.sense(omnivore);
    CHECK_FALSE(Simulation::is_prey(omnivore, grazer));
    simulation.add_agent(encode_dna(Species::Grazer), 16, 16);
    simulation.index_cells();
    simulation.sense(omnivore);
    CHECK(Simulation::is_prey(omnivore, grazer));  // two in six: common enough

    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 25, 25);
    for (int x = 20; x < 25; ++x) simulation.add_agent(encode_dna(Species::Omnivore), x, 22);
    simulation.index_cells();
    CHECK(simulation.prey_switching(predator) == std::pair{true, true});  // a predator always hunts both
    SWITCH_SHARE = switch_share;
}

TEST_CASE("a blow that exhausts the striker leaves it starving, and no body is left without a recorded death") {
    Simulation simulation = world();
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}, {"strength", 7}, {"weapons", 7}}), 5, 5);
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}}), 5, 6);
    simulation.index_cells();
    grazer.hunger = 99.9;
    simulation.defend(grazer, predator);
    CHECK(grazer.alive());  // past full hunger, but alive: starving wears its health away
    CHECK(grazer.hunger > 100);

    Simulation busy(Simulation::Settings{60, 60, 90, 1});
    for (int turn = 0; turn < 300; ++turn) busy.advance();
    for (const auto& agent : busy.agents()) CHECK((agent->alive() || agent->died_turn.has_value()));
}

TEST_CASE("grazers don't scavenge") {
    Simulation simulation = world();
    Bitzkey& corpse = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 5, 5);
    corpse.health = 0;
    simulation.die(corpse, "wounded");
    corpse.died_turn = -5;  // no longer a fresh kill
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"carrion", 7}, {"sight", 4}}), 5, 6);
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore, {{"sight", 4}}), 5, 4);
    simulation.index_cells();
    Simulation::Sensed sensed = simulation.sense(grazer);
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), Scavenge) == sensed.options.end());
    CHECK(sensed.senses[SenseCarrionCloseness] == 0.0);
    sensed = simulation.sense(omnivore);
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), Scavenge) != sensed.options.end());
}

TEST_CASE("a body is a fresh kill through its death turn and three more, then carrion") {
    for (const bool watched : {true, false}) {  // a hunter watching from the start would eat it
        Simulation simulation = world();
        Bitzkey& corpse = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 5, 5);
        corpse.health = 0;
        corpse.hunger = 50;
        simulation.die(corpse, "wounded");
        if (watched) {
            Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"sight", 4}}), 5, 8);
            simulation.index_cells();
            const Simulation::Sensed sensed = simulation.sense(predator);
            CHECK((sensed.situation.kill == &corpse && sensed.situation.carrion == nullptr));
            CHECK(sensed.senses[SenseKillCloseness] > 0);
            continue;
        }
        for (int turn = 0; turn < 3; ++turn) simulation.take_turn();  // fresh through these; it starts to rot at the third's end
        CHECK(corpse.meat == doctest::Approx(40.0 - 40.0 / 27));
        simulation.take_turn();
        Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"sight", 4}}), 5, 8);
        simulation.index_cells();
        const Simulation::Sensed sensed = simulation.sense(predator);
        CHECK((sensed.situation.kill == nullptr && sensed.situation.carrion == &corpse));
    }
}

TEST_CASE("a hunter eats a fresh kill whole for three times the kill meal") {
    const double kill_meal = KILL_MEAL;
    KILL_MEAL = 25;  // a meal small enough to measure, short of filling it
    Simulation simulation = world();
    Bitzkey& corpse = simulation.add_agent(encode_dna(Species::Grazer), 5, 5);
    corpse.health = 0;
    simulation.die(corpse, "killed");
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}, {"sight", 4}}), 5, 8);
    predator.hunger = 90;
    simulation.index_cells();
    Simulation::Sensed sensed = simulation.sense(predator);
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), Feast) != sensed.options.end());
    while (predator.last_action != "feasted") simulation.act(predator, Feast, sensed.situation);
    CHECK(predator.hunger == doctest::Approx(90 - 3 * 25 / predator.stomach));  // the kill meal of the smallest prey
    CHECK(corpse.meat == 0.0);
    CHECK(simulation.sense(predator).situation.kill == nullptr);
    const int identifier = corpse.identifier;
    simulation.take_turn();
    for (const auto& agent : simulation.agents()) CHECK(agent->identifier != identifier);  // the body is gone
    KILL_MEAL = kill_meal;
}

TEST_CASE("only hunters feast, and only on what they live on") {
    for (const auto& [species, eaten] : {std::pair{Species::Grazer, Species::Grazer}, std::pair{Species::Omnivore, Species::Predator},
                                         std::pair{Species::Predator, Species::Plant}}) {
        Simulation simulation = world();
        Bitzkey& corpse = simulation.add_agent(encode_dna(eaten), 5, 5);
        corpse.health = 0;
        simulation.die(corpse, "killed");
        Bitzkey& eater = simulation.add_agent(encode_dna(species, {{"sight", 4}}), 5, 6);
        simulation.index_cells();
        const Simulation::Sensed sensed = simulation.sense(eater);
        CHECK(std::find(sensed.options.begin(), sensed.options.end(), Feast) == sensed.options.end());
        CHECK(sensed.senses[SenseKillCloseness] == 0.0);
    }
}

TEST_CASE("the top and bottom of the map are barriers, but east and west wrap") {
    Simulation simulation(Simulation::Settings{20, 20, 0, 2});
    Bitzkey& top = simulation.add_agent(encode_dna(Species::Grazer, {{"speed", 7}}), 0, 0);
    Bitzkey& bottom = simulation.add_agent(encode_dna(Species::Predator, {{"speed", 7}}), 19, 19);
    simulation.index_cells();
    Simulation::Situation nothing;
    for (int round = 0; round < 200; ++round) {
        const int heading = std::array{GoNorth, GoWest, GoSouth, GoEast}[static_cast<std::size_t>(round % 4)];
        simulation.act(top, heading, nothing);
        simulation.act(bottom, heading, nothing);
        CHECK((0 <= top.y && top.y < 20 && 0 <= bottom.y && bottom.y < 20 && 0 <= top.x && top.x < 20 && 0 <= bottom.x &&
               bottom.x < 20));
    }
    bottom.x = bottom.y = 19;
    Bitzkey& slow = simulation.add_agent(encode_dna(Species::Predator, {{"speed", 0}}), 0, 0);  // a cell a step
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator), 1, 1);
    simulation.index_cells();
    Simulation::Situation fleeing;
    fleeing.hunter = &hunter;
    simulation.act(slow, Flee, fleeing);  // it flees up and west: stopped at the top, it wraps round the west
    CHECK((slow.x == 19 && slow.y == 0));
    const auto cells = simulation.free_cells_near(bottom);
    for (const Cell& cell : cells) CHECK((0 <= cell.y && cell.y < 20));
    CHECK(std::find(cells.begin(), cells.end(), Cell{0, 19}) != cells.end());  // east wraps round to the west
}

TEST_CASE("grass dies back a tuft at a time where it isn't medium") {
    Simulation simulation(Simulation::Settings{20, 20, 0, 1});
    while (simulation.turn() < 240) simulation.take_turn();  // mid-year, mild: the arctic cold, the temperate zone medium
    simulation.clear_grass();
    for (int x = 0; x < 20; ++x) simulation.grass_cell(x, 1) = simulation.grass_cell(x, 5) = 3;  // arctic, then temperate
    const double regrowth = simulation.grass_regrowth();
    simulation.set_grass_regrowth(0);
    for (int turn = 0; turn < GRASS_DIEBACK; ++turn) simulation.take_turn();
    simulation.set_grass_regrowth(regrowth);
    int arctic = 0, temperate = 0;
    for (int x = 0; x < 20; ++x) {
        arctic += simulation.grass_at(x, 1);
        temperate += simulation.grass_at(x, 5);
    }
    CHECK(arctic == 20 * 2);  // a tuft each
    CHECK(temperate == 20 * 3);
}

TEST_CASE("a dead Bitzkey leaves meat by how nourished it was, and the brain is gone") {
    Simulation simulation = world();
    Bitzkey& fed = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 2, 2);
    Bitzkey& starved = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 4, 4);
    starved.hunger = 100;
    simulation.die(fed, "wounded");
    simulation.die(starved, "starved");
    CHECK(fed.meat == doctest::Approx(80));
    CHECK(starved.meat == 0);
    CHECK(fed.brain == nullptr);
    CHECK_FALSE(fed.alive());
}

TEST_CASE("a world with no animals left has ended") {
    Simulation simulation = world();
    simulation.add_agent(encode_dna(Species::Plant), 2, 2);
    CHECK(simulation.end_reason() == EndReason::Extinct);
    simulation.add_agent(encode_dna(Species::Grazer), 3, 3);
    CHECK(simulation.end_reason() == EndReason::None);
}

TEST_CASE("grass grows twice as fast on and around corpses") {
    Simulation simulation = world();
    const auto& rows = simulation.grass_rows();  // grass grows only where it is medium
    for (const int y : {7, 8, 9}) CHECK(std::find(rows.begin(), rows.end(), y) != rows.end());
    Bitzkey& corpse = simulation.add_agent(encode_dna(Species::Grazer), 0, 8);
    corpse.health = 0;
    simulation.die(corpse, "wounded");
    simulation.clear_grass();
    simulation.sow(20000);
    double fertile = 0;
    int cells = 0;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            fertile += simulation.grass_at(py_mod(dx, 12), 8 + dy);
            ++cells;
        }
    }
    const double elsewhere = (static_cast<double>(simulation.grass_tufts()) - fertile) / (12 * static_cast<int>(rows.size()) - cells);
    CHECK(fertile / cells / elsewhere == doctest::Approx(2).epsilon(0.2));
}

TEST_CASE("every zone starts with plants, and an animal group is always kept") {
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        const Simulation simulation(Simulation::Settings{100, 100, 90, seed});
        for (const auto& rows : simulation.zones()) {  // arctic, temperate, equator, temperate, arctic
            bool planted = false;
            for (const auto& agent : simulation.agents()) {
                planted = planted || (agent->species == Species::Plant && rows.front() - SPAWN_SPREAD <= agent->y &&
                                      agent->y <= rows.back() + SPAWN_SPREAD);
            }
            CHECK(planted);
        }
        CHECK(std::any_of(simulation.agents().begin(), simulation.agents().end(),
                          [](const auto& agent) { return agent->species != Species::Plant; }));
    }
    const Simulation few(Simulation::Settings{100, 100, 8, 2});  // two groups: not enough for every zone
    std::set<bool> plants;
    for (const auto& agent : few.agents()) plants.insert(agent->species == Species::Plant);
    CHECK(plants.size() == 2);  // a plant group and an animal group
}

TEST_CASE("a starving grazer with food scarce may uproot a plant beside it") {
    Simulation simulation(Simulation::Settings{30, 30, 0, 3});
    simulation.clear_grass();
    simulation.set_grass_regrowth(0);  // no grass to come
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 10, 10);  // a big stomach
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"size", 0}}), 11, 10);    // 60 health
    simulation.index_cells();
    const auto offered = [](const Simulation::Sensed& found, int action) {
        return std::find(found.options.begin(), found.options.end(), action) != found.options.end();
    };
    grazer.hunger = 50;
    CHECK_FALSE(offered(simulation.sense(grazer), Uproot));  // not starving
    grazer.hunger = 85;
    plant.cooldown = 5;  // still recovering from a bite: it can't be browsed, but it can be uprooted
    Simulation::Sensed sensed = simulation.sense(grazer);
    CHECK((offered(sensed, Uproot) && !offered(sensed, Browse)));
    const double meal = UPROOT_MEALS * FRESH_KILL_MEALS * KILL_MEAL / grazer.stomach;  // half a feast on a body of its size
    simulation.act(grazer, Uproot, sensed.situation);
    CHECK(grazer.hunger == doctest::Approx(std::max(0.0, 85 - meal)));  // it may fill it
    CHECK((plant.health == doctest::Approx(60 - UPROOT_BITE) && grazer.last_action == "uprooted"));
    grazer.hunger = 85;
    sensed = simulation.sense(grazer);
    simulation.act(grazer, Uproot, sensed.situation);
    CHECK((!plant.alive() && plant.last_action == "uprooted"));  // it destroyed the plant
    for (const int x : {12, 13, 14}) simulation.add_agent(encode_dna(Species::Plant), x, 10);  // food enough about
    simulation.add_agent(encode_dna(Species::Plant), 10, 11);
    simulation.index_cells();
    CHECK_FALSE(offered(simulation.sense(grazer), Uproot));
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore), 10, 9);
    omnivore.hunger = 85;
    simulation.index_cells();
    CHECK_FALSE(offered(simulation.sense(omnivore), Uproot));  // grazers only
}

TEST_CASE("no body is left without a recorded death, even when moving, striking and seeding starve") {
    const double go = GO_EFFORT, strike = STRIKE_EFFORT;
    const double seed_cost = SEED_COST, seed_hunger = SEED_HUNGER;
    for (const bool harsh : {false, true}) {
        if (harsh) {
            GO_EFFORT = 2.0;
            STRIKE_EFFORT = 2.0;
            SEED_COST = 100;
            SEED_HUNGER = 100;
        }
        for (const std::uint64_t seed : {1, 2, 3}) {
            Simulation simulation(Simulation::Settings{40, 40, 120, seed});
            for (int turn = 0; turn < 120 && !simulation.ended(); ++turn) {
                simulation.advance();
                for (const auto& agent : simulation.agents()) {
                    CAPTURE(seed);
                    CAPTURE(simulation.turn());
                    CHECK((agent->alive() || agent->died_turn.has_value()));
                }
            }
        }
    }
    GO_EFFORT = go;
    STRIKE_EFFORT = strike;
    SEED_COST = seed_cost;
    SEED_HUNGER = seed_hunger;
}

TEST_CASE("a move leaves a trail of the cells it passed through that lasts the turn") {
    Simulation simulation(Simulation::Settings{30, 30, 0, 3});
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"speed", 7}}), 27, 10);  // six cells a step
    simulation.index_cells();
    Simulation::Sensed sensed = simulation.sense(grazer);
    simulation.act(grazer, GoEast, sensed.situation);  // across the edge, east and west wrapping round
    CHECK(grazer.x == 3);
    CHECK(grazer.trail == std::vector<std::pair<int, int>>{{27, 10}, {28, 10}, {29, 10}, {0, 10}, {1, 10}, {2, 10}});
    sensed = simulation.sense(grazer);
    simulation.act(grazer, WalkSouth, sensed.situation);
    CHECK(grazer.trail.back() == std::pair{3, 10});
    const Bitzkey copied = grazer;
    simulation.advance();  // a new turn forgets the last one's way
    CHECK(std::find(grazer.trail.begin(), grazer.trail.end(), std::pair{27, 10}) == grazer.trail.end());
    CHECK(copied.trail.front() == std::pair{27, 10});  // though a copy, as history holds, keeps its own
}

TEST_CASE("starving is recorded and its body handled however it comes") {
    Simulation simulation(Simulation::Settings{30, 30, 0, 3});
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"intellect", 7}, {"empathy", 7}}), 10, 10);
    Bitzkey& herd_mate = simulation.add_agent(grazer.dna, 11, 10);
    simulation.index_cells();
    simulation.think(grazer);  // a decision to learn from
    herd_mate.x = grazer.x + 1;  // still alongside, wherever that took it
    herd_mate.y = grazer.y;
    simulation.index_cells();
    grazer.hunger = 100.5;      // starving, with a turn of health left
    grazer.health = 0.05 * grazer.max_health;
    const int deaths = simulation.deaths();
    simulation.advance();
    CHECK((grazer.last_action == "starved" && grazer.died_turn == 1 && simulation.deaths() == deaths + 1));
    CHECK(grazer.meat == 0);  // a starved body leaves nothing to eat
    CHECK(grazer.brain == nullptr);
    CHECK(herd_mate.seen_failure == 0);  // a slow death: no choice of its own to see fail

    const double seed_cost = SEED_COST;
    SEED_COST = 100;
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"fertility", 7}}), 20, 20);
    simulation.index_cells();
    for (int attempt = 0; attempt < 400 && plant.alive(); ++attempt) {
        plant.hunger = 30;
        simulation.seed_plant(plant);
    }
    SEED_COST = seed_cost;
    CHECK((plant.alive() && plant.last_action == "seeded"));  // starving, not dead
}

TEST_CASE("a history folder an earlier version left behind is cleared away once stale") {
    const auto made = [](const char* name) {
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / (std::string(HISTORY_PREFIX) + name);
        std::filesystem::create_directories(folder);
        return folder;
    };
    const std::filesystem::path old = made("test-old"), recent = made("test-recent");
    std::filesystem::last_write_time(old, std::filesystem::file_time_type::clock::now() - std::chrono::hours(STALE_HISTORY_HOURS + 1));
    remove_stale_histories();
    CHECK_FALSE(std::filesystem::exists(old));
    CHECK(std::filesystem::exists(recent));
    std::filesystem::remove_all(recent);
}

TEST_CASE("deploying puts five of each around a cell, with their brains and minds, and lets a rewound future go") {
    Simulation simulation(Simulation::Settings{30, 30, 40, 3});
    for (int turn = 0; turn < 5; ++turn) simulation.advance();
    const Bitzkey& animal = **std::find_if(simulation.agents().begin(), simulation.agents().end(),
                                           [](const auto& agent) { return agent->alive() && agent->brain; });
    const Imported bitzkey = import_bitzkeys(dumps(bitzkey_record(animal))).front();
    const int turn = simulation.turn();
    const std::size_t before = simulation.agents().size();
    const std::vector<Bitzkey*> deployed = simulation.deploy({bitzkey, bitzkey}, 10, 1);  // near the top: kept on the map
    CHECK((DEPLOY_GROUP == 5 && deployed.size() == 10 && simulation.agents().size() == before + 10));
    for (const Bitzkey* agent : deployed) {
        CHECK(agent->dna == animal.dna);
        CHECK(std::min(std::abs(agent->x - 10), 30 - std::abs(agent->x - 10)) <= SPAWN_SPREAD + BIRTH_REACH);  // the nearest room to its spot
        CHECK((agent->y >= 0 && agent->y <= 1 + SPAWN_SPREAD + BIRTH_REACH));
        CHECK((agent->brain != deployed.front()->brain || agent == deployed.front()));  // each its own copy
        CHECK(agent->mind[Fear] == doctest::Approx(animal.mind[Fear]).epsilon(1e-3));
    }
    CHECK(simulation.turn() == turn);  // between turns
    simulation.advance();
    CHECK(simulation.turn() == turn + 1);
}

TEST_CASE("breeding is the greatest pleasure, courting into reach the next, and failing costs nothing") {
    struct PreyAside {  // the prey at home aside, put back however the test ends
        double was = PREY_PER_PREDATOR;
        PreyAside() { PREY_PER_PREDATOR = 0.0; }
        ~PreyAside() { PREY_PER_PREDATOR = was; }
    } prey_aside;
    CHECK((choice_learning(Breed) == BREED_LEARNING && choice_learning(Court) == COURT_LEARNING));
    CHECK((BREED_LEARNING == 1.0 && COURT_LEARNING == 1.0 && KILL_FOOD_LEARNING == 2.0));
    CHECK((BREED_JOY == 4.0 && COURT_JOY == 2.5 && LIBIDO_IN_JOY == 0.25));
    Simulation simulation = world();
    Bitzkey& cold = simulation.add_agent(encode_dna(Species::Predator, {{"sight", 7}, {"libido", 0}}), 2, 2);
    CHECK(mating_joy(cold, BREED_JOY) == doctest::Approx(3.0));  // three quarters of it even with no libido
    CHECK(mating_joy(cold, COURT_JOY) == doctest::Approx(1.875));
    CHECK(mating_joy(cold, COURT_JOY) > FEAST_JOY);  // the strongest pleasure besides
    Bitzkey& mate = simulation.add_agent(cold.dna, 2, 8);  // six cells off: a predator closes two a step
    simulation.index_cells();
    Simulation::Sensed sensed = simulation.sense(cold);
    simulation.act(cold, Court, sensed.situation);
    CHECK(cold.reward == 0.0);  // not yet in reach: nothing, and nothing lost
    while (std::abs(cold.y - mate.y) > 2) simulation.act(cold, Court, sensed.situation);
    CHECK(cold.reward == doctest::Approx(1.875));

    cold.reward = mate.reward = 0.0;
    mate.cooldown = 3;  // it bred with another first
    Simulation::Situation spurned;
    spurned.mate = &mate;
    simulation.act(cold, Breed, spurned);
    CHECK((cold.last_action == "was spurned" && cold.reward == 0.0));
    mate.cooldown = 0;
    simulation.breed(cold, mate);
    CHECK(cold.reward == doctest::Approx(3.0));
    CHECK(mate.reward == doctest::Approx(3.0));  // both parents
}

TEST_CASE("a predator's breeding follows the prey in its home band, which it senses") {
    const double newborn_food = NEWBORN_FOOD, small_litters = SMALL_LITTERS, prey_per_predator = PREY_PER_PREDATOR;
    NEWBORN_FOOD = 0;  // no limit from the energy given
    SMALL_LITTERS = 1.0;
    PREY_PER_PREDATOR = 1.5;
    Simulation simulation = world(30);  // temperate bands: rows 6-11 and 18-23
    const Dna dna = encode_dna(Species::Predator, {{"fertility", 7}, {"litter", 7}});  // at home in the temperate bands
    Bitzkey& first = simulation.add_agent(dna, 15, 8);
    Bitzkey& second = simulation.add_agent(dna, 15, 9);
    simulation.add_agent(encode_dna(Species::Grazer), 3, 20);  // the southern temperate band: not its home band
    simulation.add_agent(encode_dna(Species::Plant), 4, 7);   // no prey
    simulation.index_cells();
    auto offers_breed = [&] {
        const auto options = simulation.sense(first).options;
        return std::find(options.begin(), options.end(), Breed) != options.end();
    };
    CHECK(simulation.home_prey(first) == 0.0);
    CHECK_FALSE(offers_breed());  // no prey at home: a hunter, like a grazer, doesn't breed below its need
    CHECK(simulation.sense(first).senses[SenseHomeFood] == 0.0);
    for (const int x : {2, 4, 6}) simulation.add_agent(encode_dna(x == 2 ? Species::Grazer : Species::Omnivore), x, 10);
    simulation.index_cells();
    CHECK(simulation.home_prey(first) == 1.5);  // three prey shared by two predators
    CHECK(offers_breed());
    CHECK(simulation.sense(first).senses[SenseHomeFood] == 0.5);
    Bitzkey& omnivore = *simulation.agents()[5];
    CHECK(simulation.sense(omnivore).senses[SenseHomeFood] == simulation.home_fed(omnivore));  // its own: both foods
    simulation.breed(first, second);
    simulation.give_birth(first);
    CHECK(simulation.agents().size() == 7 + 3);  // six young, halved: half the prey of a full litter
    PREY_PER_PREDATOR = prey_per_predator;
    SMALL_LITTERS = small_litters;
    NEWBORN_FOOD = newborn_food;
}

TEST_CASE("a square holds ten spaces, a full one stops steps and spawning, and young share a square while there is room") {
    Simulation simulation = world();
    Bitzkey& walker = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 5);  // four spaces
    CHECK((CELL_SPACE == 10 && walker.space() == 4));
    simulation.add_agent(encode_dna(Species::Plant, {{"size", 7}}), 6, 5);  // eight of the next square's ten
    simulation.index_cells();
    CHECK(simulation.room_in(Cell{6, 5}) == 2);
    Simulation::Situation nothing;
    simulation.act(walker, WalkEast, nothing);
    CHECK((walker.x == 5 && walker.y == 5));  // stopped short of the full square
    Bitzkey& small = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 0}}), 7, 5);
    simulation.index_cells();
    Simulation::Situation toward;
    toward.herd_middle = Cell{6, 5};
    simulation.act(small, Herd, toward);
    CHECK((small.x == 6 && small.y == 5));  // one space: room enough, sharing the plant's square
    CHECK(simulation.spot_near(Cell{6, 5}, 4) == Cell{6, 4});  // the nearest square with room

    const double newborn_food = NEWBORN_FOOD, small_litters = SMALL_LITTERS, plants_per_grazer = PLANTS_PER_GRAZER;
    NEWBORN_FOOD = 0;
    SMALL_LITTERS = 1.0;
    PLANTS_PER_GRAZER = 0;
    Simulation births = world();
    births.set_mutation(births.mind_mutation(), 0);  // the young as large as their parents
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"litter", 7}, {"size", 1}});  // two spaces each: six young
    Bitzkey& first = births.add_agent(dna, 5, 5);
    Bitzkey& second = births.add_agent(dna, 5, 6);
    births.index_cells();
    births.breed(first, second);
    births.give_birth(first);
    int home = 0, north = 0;
    for (std::size_t index = 2; index < births.agents().size(); ++index) {
        home += births.agents()[index]->x == 5 && births.agents()[index]->y == 5;
        north += births.agents()[index]->x == 5 && births.agents()[index]->y == 4;
    }
    CHECK((births.agents().size() == 8 && home == 4 && north == 2));  // 2 + 4 x 2 fills her square; the rest north of her
    CHECK(births.room_in(Cell{5, 5}) == 0);
    PLANTS_PER_GRAZER = plants_per_grazer;
    SMALL_LITTERS = small_litters;
    NEWBORN_FOOD = newborn_food;
}

TEST_CASE("meat lasts twice as long as plant food, and full hunger wears a tenth of its health away each turn") {
    Simulation simulation = world();
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator), 3, 5);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 8, 5);
    hunter.hunger = grazer.hunger = 50;
    hunter.nourish(40 * hunter.stomach, true);
    grazer.nourish(40 * grazer.stomach);
    CHECK((hunter.hunger == doctest::Approx(10) && hunter.meat_fill == doctest::Approx(40) && grazer.meat_fill == 0.0));
    CHECK(hunter.digest(10) == doctest::Approx(10 * (1 - 40.0 / 90 * (1 - MEAT_DIGESTION))));  // its meat share at a seventh the rate
    CHECK(grazer.digest(10) == doctest::Approx(10));
    hunter.meat_fill = 90;
    hunter.hunger = 10;  // all its fullness meat: a seventh of the hunger a turn, so meat lasts seven times as long
    CHECK(hunter.digest(10) == doctest::Approx(10.0 / 7));

    Simulation starving = world();
    Bitzkey& lean = starving.add_agent(encode_dna(Species::Grazer, {{"temperature", 1}}), 5, 3);  // medium: at home here
    starving.add_agent(encode_dna(Species::Predator), 10, 10);
    lean.hunger = 150;
    starving.take_turn();
    CHECK(lean.alive());
    CHECK(lean.hunger < 101);  // its hunger stops at full, and only what it did after adds a little
    CHECK(lean.health < (1 - STARVING_DAMAGE / 2) * lean.max_health);
}

TEST_CASE("an infant has half its body and health, and grows out of it; plants are never feeble") {
    FEEBLE_YOUTH = 0.1;
    Simulation simulation = world();
    Bitzkey& infant = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}, {"strength", 7}, {"speed", 6}, {"sight", 5}}), 5, 5);
    const double lifespan = infant.lifespan;
    CHECK(infant.feeble);
    CHECK((infant.trait(Size) == 3 && infant.trait(Strength) == 3 && infant.trait(Speed) == 3 && infant.trait(Sight) == 2));
    CHECK((infant.space() == 4 && infant.body[Size] == 7));  // its genes are as they were
    CHECK(infant.max_health == doctest::Approx((60 + 90 * share(3)) * 0.5));
    CHECK(infant.health == infant.max_health);  // born with the health of an infant
    infant.age = static_cast<int>(std::lround(0.1 * lifespan));
    infant.grow();
    CHECK_FALSE(infant.feeble);
    CHECK((infant.trait(Size) == 7 && infant.space() == 8 && infant.lifespan == lifespan));  // lifespan by its genes
    CHECK(infant.max_health == doctest::Approx(150.0));
    Bitzkey& seedling = simulation.add_agent(encode_dna(Species::Plant, {{"size", 6}, {"weapons", 6}}), 7, 7);
    CHECK_FALSE(seedling.feeble);
    CHECK(seedling.trait(Weapons) == 6);
}

TEST_CASE("old age brings feebleness, and its health shrinks to match") {
    FEEBLE_YOUTH = 0.1;
    const double ageing = AGEING;
    Simulation simulation = world();
    Bitzkey& old = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}, {"armour", 5}}), 5, 5);
    old.age = static_cast<int>(AGEING * old.lifespan) + 1;
    old.grow();
    CHECK(old.feeble);
    CHECK(old.trait(Armour) == 2);
    CHECK(old.max_health == doctest::Approx((60 + 90 * 3.0 / 7) * 0.5));
    AGEING = 0.95;  // not yet old, by these rules
    old.grow();
    CHECK_FALSE(old.feeble);
    old.health = old.max_health;
    AGEING = ageing;
    simulation.add_agent(encode_dna(Species::Predator), 10, 10);
    simulation.take_turn();
    CHECK(old.feeble);
    CHECK(old.health <= old.max_health);
}

TEST_CASE("a heading pleases the curious only if it moved") {
    Simulation simulation = world();
    Bitzkey& walker = simulation.add_agent(encode_dna(Species::Grazer, {{"curiosity", 7}}), 5, 0);
    simulation.index_cells();
    Simulation::Situation nothing;
    simulation.act(walker, WalkNorth, nothing);  // the map's edge
    CHECK((walker.y == 0 && walker.reward == 0.0));
    simulation.act(walker, WalkSouth, nothing);
    CHECK((walker.y == 1 && walker.reward == doctest::Approx(CURIOSITY_JOY)));
}

TEST_CASE("an alert pleases only when a herd-mate takes it in") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"trust", 7}, {"empathy", 7}, {"patience", 7}, {"sight", 3}});
    Bitzkey& lookout = simulation.add_agent(dna, 5, 5);
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator), 9, 9);
    simulation.index_cells();
    simulation.alert(lookout, hunter);  // no one to tell
    CHECK(lookout.reward == 0.0);
    Bitzkey& mate = simulation.add_agent(dna, 5, 6);
    simulation.index_cells();
    simulation.alert(lookout, hunter);
    CHECK((lookout.reward == ALERT_JOY && mate.alarm == hunter.identifier));
}

TEST_CASE("learning weights make success memorable, not failure") {
    CHECK((lesson_weight(Hunt, 0.3) == KILL_FOOD_LEARNING && lesson_weight(Hunt, -0.3) == 1.0));
    CHECK((lesson_weight(Eat, 0.3) == PLANT_FOOD_LEARNING && lesson_weight(Eat, -0.3) == 1.0));
    CHECK(lesson_weight(Rest, 0.3) == 1.0);
}

TEST_CASE("a slow death is not blamed on the last choice, nor seen by the herd") {
    for (const auto& [cause, blamed] : std::vector<std::pair<std::string, bool>>{{"starved", false}, {"froze", false}, {"old age", false}, {"exhausted", true}}) {
        Simulation simulation = world();
        const Dna dna = encode_dna(Species::Grazer, {{"intellect", 7}, {"empathy", 7}, {"sight", 7}});  // seen wherever it went
        Bitzkey& dying = simulation.add_agent(dna, 5, 5);
        Bitzkey& herd_mate = simulation.add_agent(dna, 6, 5);
        simulation.index_cells();
        simulation.think(dying);
        dying.reward = 0.0;
        dying.last_wellbeing = simulation.wellbeing(dying);
        simulation.die(dying, cause);
        CHECK((herd_mate.seen_failure > 0) == blamed);
    }
}

TEST_CASE("growing out of infancy or into old age is no lesson") {
    FEEBLE_YOUTH = 0.1;
    for (const bool infant : {true, false}) {
        Simulation simulation = world();
        Bitzkey& animal = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 5, 5);
        simulation.add_agent(encode_dna(Species::Predator), 10, 10);
        animal.age = infant ? static_cast<int>(std::lround(0.1 * animal.lifespan)) - 1 : static_cast<int>(AGEING * animal.lifespan);
        animal.grow();
        const bool feeble = animal.feeble;
        animal.health = 0.8 * animal.max_health;
        animal.last_wellbeing = simulation.wellbeing(animal);
        simulation.take_turn();
        CHECK(animal.feeble != feeble);
        // Only the turn's own hunger and healing move its wellbeing; its health's share jumping with its full health does not.
        CHECK(std::abs(simulation.wellbeing(animal) - animal.last_wellbeing) < 0.05);
    }
}

TEST_CASE("every way of moving pays for the distance") {
    Simulation simulation = world();
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator, {{"speed", 7}}), 1, 5);
    Bitzkey& prey = simulation.add_agent(encode_dna(Species::Grazer), 9, 5);
    simulation.index_cells();
    hunter.hunger = 10;
    Simulation::Situation toward;
    toward.herd_middle = Cell{prey.x, prey.y};
    simulation.act(hunter, Herd, toward);  // four cells, at its speed
    CHECK(hunter.x == 5);
    CHECK(hunter.hunger == doctest::Approx(10 + hunter.burn * GO_EFFORT * 3));  // every cell past the first
    prey.hunger = 10;
    Simulation::Situation away;
    away.hunter = &hunter;
    simulation.act(prey, Flee, away);
    CHECK(prey.hunger == doctest::Approx(10 + prey.burn * GO_EFFORT * std::max(0, prey.steps - 1)));
}

TEST_CASE("defending is damped by tolerance, as attacking is") {
    Simulation simulation = world();
    std::vector<double> joys;
    for (const int tolerance : {0, 7}) {
        const int y = 2 + 3 * tolerance / 7;
        Bitzkey& defender = simulation.add_agent(encode_dna(Species::Omnivore, {{"aggression", 7}, {"tolerance", tolerance}, {"size", 7}}), 2, y);
        Bitzkey& assailant = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 3, y);
        simulation.index_cells();
        simulation.defend(defender, assailant);
        joys.push_back(defender.reward);
    }
    CHECK(joys[1] == doctest::Approx(joys[0] * 0.25));
}

TEST_CASE("omnivores live alone, in neither herd nor pack, and miss none") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Omnivore, {{"empathy", 7}, {"patience", 7}});
    Bitzkey& first = simulation.add_agent(dna, 5, 5);
    Bitzkey& second = simulation.add_agent(dna, 6, 5);
    const Dna grazer = encode_dna(Species::Grazer, {{"empathy", 7}, {"patience", 7}});
    Bitzkey& one = simulation.add_agent(grazer, 2, 9);
    Bitzkey& other = simulation.add_agent(grazer, 3, 9);
    simulation.index_cells();
    CHECK_FALSE(simulation.herd_mate(first, second));
    CHECK_FALSE(simulation.society(first, second));
    CHECK(simulation.herd_mate(one, other));  // grazers still herd
    Simulation::Situation lonely = simulation.sense(first).situation;
    lonely.herd = false;
    lonely.herd_near = 0;
    const double loneliness = HERD_LONELINESS;
    HERD_LONELINESS = 1.0;
    const double alone = simulation.comfort(first, lonely);
    SOLITARY_OMNIVORES = 0;
    CHECK(simulation.herd_mate(first, second));  // the rule off: they herd again
    CHECK(simulation.comfort(first, lonely) < alone);  // and miss their herd
    SOLITARY_OMNIVORES = 1;
    HERD_LONELINESS = loneliness;
}

TEST_CASE("courting is open, and pays, only when breeding could follow") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"sight", 2}});
    Bitzkey& agent = simulation.add_agent(dna, 2, 5);
    simulation.add_agent(dna, 6, 5);
    simulation.index_cells();
    const double plants_per_grazer = PLANTS_PER_GRAZER;
    const auto offers_court = [&] {
        const auto options = simulation.sense(agent).options;
        return std::find(options.begin(), options.end(), Court) != options.end();
    };
    PLANTS_PER_GRAZER = 1.0;  // no plants at home: breeding couldn't follow
    CHECK_FALSE(offers_court());
    PLANTS_PER_GRAZER = 0.0;
    CHECK(offers_court());
    Simulation::Sensed sensed = simulation.sense(agent);
    simulation.act(agent, Court, sensed.situation);
    CHECK(agent.reward > 0.5 * COURT_JOY);  // courted into reach, to breed
    PLANTS_PER_GRAZER = plants_per_grazer;
}

TEST_CASE("a pack courts and hunts only toward its home temperature, as a herd herds") {
    struct PreyAside {  // the prey at home aside, put back however the test ends
        double was = PREY_PER_PREDATOR;
        PreyAside() { PREY_PER_PREDATOR = 0.0; }
        ~PreyAside() { PREY_PER_PREDATOR = was; }
    } prey_aside;
    Simulation simulation(100, 100, 0, 3);  // cold rows 0-19, medium 20-39, hot 40-59
    const Dna dna = encode_dna(Species::Predator, {{"sight", 7}, {"fertility", 7}, {"temperature", Hot}});
    Bitzkey& predator = simulation.add_agent(dna, 50, 30);  // a step from home
    Bitzkey& prey = simulation.add_agent(encode_dna(Species::Grazer), 50, 42);
    Bitzkey& partner = simulation.add_agent(dna, 52, 42);
    simulation.index_cells();
    const auto offers = [&](int action) {
        const auto options = simulation.sense(predator).options;
        return std::find(options.begin(), options.end(), action) != options.end();
    };
    CHECK((simulation.sense(predator).situation.quarry == &prey && simulation.sense(predator).situation.suitor == &partner));
    CHECK(offers(Hunt));  // toward home: open
    CHECK(offers(Court));
    prey.y = partner.y = 18;  // off toward the cold: closed
    simulation.index_cells();
    CHECK((simulation.sense(predator).situation.quarry == &prey && simulation.sense(predator).situation.suitor == &partner));
    CHECK_FALSE(offers(Hunt));
    CHECK_FALSE(offers(Court));
}

TEST_CASE("an omnivore struck always breaks, and a predator is never overcome by primal fear") {
    Simulation simulation = world();
    Bitzkey& bold = simulation.add_agent(encode_dna(Species::Omnivore, {{"fear", 0}}), 5, 5);  // fearless, and unhurt
    Bitzkey& striker = simulation.add_agent(encode_dna(Species::Predator), 5, 6);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"fear", 0}}), 8, 8);
    simulation.index_cells();
    simulation.struck(bold, striker);
    simulation.struck(grazer, striker);
    CHECK(bold.panicked);  // a coward, whatever its fear
    CHECK_FALSE(grazer.panicked);  // the fearless otherwise never break
    COWARDLY_OMNIVORES = 0;
    bold.panicked = false;
    simulation.struck(bold, striker);
    CHECK_FALSE(bold.panicked);
    COWARDLY_OMNIVORES = 1;
    Bitzkey& small = simulation.add_agent(encode_dna(Species::Predator, {{"size", 0}}), 2, 2);
    Bitzkey& brute = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}, {"strength", 7}, {"weapons", 7}}), 2, 4);
    CHECK(Simulation::disparity(small, brute) > 0.5);
    for (int roll = 0; roll < 50; ++roll) CHECK_FALSE(simulation.primal_fear(small, &brute));  // never overcome
}

TEST_CASE("an omnivore's litters follow the plants and prey at home, half each") {
    const double small_litters = SMALL_LITTERS, newborn_food = NEWBORN_FOOD, plants = PLANTS_PER_GRAZER, prey = PREY_PER_PREDATOR;
    SMALL_LITTERS = 1.0;
    NEWBORN_FOOD = 0.0;
    PLANTS_PER_GRAZER = 1.0;
    PREY_PER_PREDATOR = 1.0;
    Simulation simulation = world(30);  // temperate bands: rows 6-11 and 18-23
    const Dna dna = encode_dna(Species::Omnivore, {{"fertility", 7}, {"litter", 7}});  // at home in the temperate bands
    Bitzkey& first = simulation.add_agent(dna, 15, 8);
    Bitzkey& second = simulation.add_agent(dna, 15, 9);
    simulation.index_cells();
    CHECK(simulation.home_fed(first) == 0.0);  // nothing to eat at home
    for (const int x : {2, 4, 6, 8}) simulation.add_agent(encode_dna(Species::Plant), x, 7);  // two plants each: half from plants alone
    simulation.index_cells();
    CHECK(simulation.home_fed(first) == 0.5);
    for (const int x : {10, 12, 14, 16}) simulation.add_agent(encode_dna(x == 16 ? Species::Predator : Species::Grazer), x, 11);
    simulation.index_cells();
    CHECK(simulation.home_fed(first) == 1.0);
    for (const auto& agent : simulation.agents()) if (agent->species == Species::Plant) agent->health = 0;
    simulation.index_cells();
    CHECK(simulation.home_fed(first) == 0.5);  // prey alone: half
    simulation.breed(first, second);
    CHECK(first.womb.size() == 3);  // six by their traits, halved
    SMALL_LITTERS = small_litters;
    NEWBORN_FOOD = newborn_food;
    PLANTS_PER_GRAZER = plants;
    PREY_PER_PREDATOR = prey;
}

TEST_CASE("an omnivore has no spines, but an armed grazer does") {
    Simulation simulation = world();
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore, {{"weapons", 7}, {"size", 7}}), 2, 2);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"weapons", 7}, {"size", 7}, {"strength", 7}}), 8, 8);
    Bitzkey& first = simulation.add_agent(encode_dna(Species::Predator), 2, 3);
    Bitzkey& second = simulation.add_agent(encode_dna(Species::Predator), 8, 9);
    simulation.index_cells();
    simulation.attack(first, omnivore);
    simulation.attack(second, grazer);
    CHECK(first.health == first.max_health);  // struck an omnivore: unstung
    CHECK(second.health < second.max_health);  // struck a well-armed grazer: stung
}

TEST_CASE("a body rotted away leaves the seed of a plant, at home where it lay") {
    const double corpse_plant = CORPSE_PLANT;
    CORPSE_PLANT = 1.0;  // every body that rots away
    Simulation simulation = world(30);
    Bitzkey& rotted = simulation.add_agent(encode_dna(Species::Grazer), 5, 8);
    Bitzkey& eaten = simulation.add_agent(encode_dna(Species::Grazer), 9, 8);
    simulation.add_agent(encode_dna(Species::Predator), 25, 25);
    simulation.index_cells();
    simulation.die(rotted, "starved");
    simulation.die(eaten, "killed");
    rotted.died_turn = eaten.died_turn = simulation.turn() + 1 - CORPSE_TURNS;  // both gone by the coming turn's end
    eaten.portions = 0;  // eaten to the last portion: nothing left to rot
    simulation.take_turn();
    std::vector<Simulation::Seed> seeds;
    for (const Simulation::Seed& seed : simulation.seeds()) {
        if ((seed.x == 5 || seed.x == 9) && seed.y == 8) seeds.push_back(seed);
    }
    REQUIRE(seeds.size() == 1);
    CHECK(seeds[0].x == 5);
    CHECK(decode_body(seeds[0].dna)[SpeciesTrait] == static_cast<int>(Species::Plant));
    CHECK(decode_climate(seeds[0].dna).temperature == zone_temperature(8, 30));  // at home where it lay
    CORPSE_PLANT = corpse_plant;
}

TEST_CASE("grazers breed at half the cost, and predators take spines half as hard") {
    Simulation simulation = world();
    CHECK((GRAZER_BREEDING_COST == 0.5 && PREDATOR_STING == 0.5));
    CHECK(simulation.add_agent(encode_dna(Species::Grazer, {{"fertility", 0}}), 2, 2).breeding_cost == 25 * GRAZER_BREEDING_COST);
    CHECK(simulation.add_agent(encode_dna(Species::Predator, {{"fertility", 0}}), 4, 4).breeding_cost == 25 * HUNTER_BREEDING_COST);
    const double sting = PREDATOR_STING;
    std::vector<double> stung;
    for (const auto& [share, x] : {std::pair{1.0, 2}, std::pair{sting, 8}}) {  // the same blow, its spines taken in full, then half as hard
        Bitzkey& spiny = simulation.add_agent(encode_dna(Species::Grazer, {{"weapons", 7}, {"size", 7}, {"strength", 7}}), x, 8);
        Bitzkey& striker = simulation.add_agent(encode_dna(Species::Predator), x, 9);
        simulation.index_cells();
        PREDATOR_STING = share;
        simulation.attack(striker, spiny);
        stung.push_back(striker.max_health - striker.health);
    }
    PREDATOR_STING = sting;
    CHECK(stung[0] > 0);
    CHECK(stung[1] == doctest::Approx(stung[0] * 0.5));
}

TEST_CASE("a carrion-eater scales into a coward, and away from hunting") {
    Simulation simulation = world();
    Bitzkey& scavenger = simulation.add_agent(encode_dna(Species::Grazer, {{"carrion", 7}, {"fear", 0}}), 2, 2);  // fearless, but a carrion-eater
    Bitzkey& middling = simulation.add_agent(encode_dna(Species::Grazer, {{"carrion", 3}, {"fear", 0}}), 8, 8);
    Bitzkey& striker = simulation.add_agent(encode_dna(Species::Predator), 2, 3);
    simulation.index_cells();
    CHECK((scavenger.scavenger() == 1.0 && middling.scavenger() == 0.0));
    simulation.struck(scavenger, striker);
    simulation.struck(middling, striker);
    CHECK(scavenger.panicked);  // always breaks, like an omnivore
    CHECK_FALSE(middling.panicked);
    const double aversion = CARRION_AVERSION;
    std::vector<double> odds;
    for (const double level : {0.0, 1.0}) {  // the same world twice, without and with the aversion
        CARRION_AVERSION = level;
        Simulation again = world();
        Bitzkey& hunter = again.add_agent(encode_dna(Species::Predator, {{"carrion", 7}, {"sight", 3}}), 5, 5);
        again.add_agent(encode_dna(Species::Grazer), 5, 8);
        again.index_cells();
        const auto sensed = again.sense(hunter);
        REQUIRE(std::find(sensed.options.begin(), sensed.options.end(), Hunt) != sensed.options.end());
        again.think(hunter);
        odds.push_back(hunter.odds[Hunt]);
    }
    CARRION_AVERSION = aversion;
    CHECK(odds[1] < odds[0]);  // it naturally shies from hunting
}

TEST_CASE("a scarce species breeds more cheaply, down to a third when rare, and grazers to a ninth") {
    RARE_BREEDING_COST = 1.0 / 3;
    GRAZER_RARE_BREEDING_COST = 1.0 / 9;
    CHECK((scarcity_share(100) == 1.0 && scarcity_share(250) == 1.0));
    CHECK((scarcity_share(10) == 1.0 / 3 && scarcity_share(3) == 1.0 / 3));
    CHECK(scarcity_share(55) == doctest::Approx(2.0 / 3));  // halfway
    CHECK((scarcity_share(300, Species::Grazer) == 1.0 && scarcity_share(10, Species::Grazer) == 1.0 / 9));  // grazers' from 300
    CHECK(scarcity_share(155, Species::Grazer) == doctest::Approx(1.0 / 9 + (8.0 / 9) * 145 / 290));
    Simulation simulation = world();
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore, {{"fertility", 0}}), 2, 2);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"fertility", 0}}), 8, 8);
    const double usual = omnivore.breeding_price(), usual_grazer = grazer.breeding_price();
    simulation.index_cells();  // one of each: rare
    CHECK(omnivore.breeding_price() == doctest::Approx(usual / 3));
    CHECK(grazer.breeding_price() == doctest::Approx(usual_grazer / 9));
    for (int x = 0; x < 10; ++x) {
        for (int y = 1; y <= 10; ++y) {
            if (!(x == 2 && y == 2)) simulation.add_agent(encode_dna(Species::Omnivore), x, y);
        }
    }
    simulation.index_cells();  // a hundred omnivores: common again
    CHECK(omnivore.breeding_price() == usual);
}

TEST_CASE("a predator is an infant half as long") {
    FEEBLE_YOUTH = 0.1;
    Simulation simulation = world();
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}}), 2, 2);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 8, 8);
    for (Bitzkey* animal : {&predator, &grazer}) {
        animal->age = static_cast<int>(0.07 * animal->lifespan);  // past a predator's 5%, within the usual 10%
        animal->grow();
    }
    CHECK_FALSE(predator.feeble);
    CHECK(grazer.feeble);
    predator.age = static_cast<int>(0.04 * predator.lifespan);
    predator.grow();
    CHECK(predator.feeble);
}

TEST_CASE("hunters need their food, full litters need four times it, and the last prey are spared") {
    const double full = FULL_LITTER_FOOD, prey_need = PREY_PER_PREDATOR;
    const int rare_prey = RARE_PREY;
    FULL_LITTER_FOOD = 4.0;
    PREDATOR_FULL_LITTER_FOOD = 8.0;
    RARE_PREY = 200;
    PREY_PER_PREDATOR = 1.0;
    Simulation simulation = world(30);  // temperate bands: rows 6-11 and 18-23
    const Dna dna = encode_dna(Species::Predator, {{"fertility", 7}, {"litter", 7}, {"sight", 3}});  // at home in the temperate bands
    Bitzkey& first = simulation.add_agent(dna, 15, 8);
    simulation.add_agent(dna, 15, 9);
    std::vector<Bitzkey*> far;
    for (int x = 0; x < 12; ++x) far.push_back(&simulation.add_agent(encode_dna(Species::Grazer), x, 26));  // twelve prey alive, none at home
    simulation.index_cells();
    const auto offers_breed = [&] {
        const auto options = simulation.sense(first).options;
        return std::find(options.begin(), options.end(), Breed) != options.end();
    };
    CHECK_FALSE(offers_breed());  // below its need at home: no breeding, as for a grazer
    for (const int x : {2, 4}) simulation.add_agent(encode_dna(Species::Grazer), x, 10);  // two prey at home for two predators: just its need
    simulation.index_cells();
    CHECK(simulation.home_fed(first) == 0.125);  // an eighth of a predator's full litter at the need
    CHECK(offers_breed());
    for (Bitzkey* grazer : far) grazer->health = 0;  // the prey dying out: only the two at home left
    simulation.index_cells();
    CHECK(first.population == 2);  // it knows its own numbers
    CHECK_FALSE(offers_breed());  // prey under twice its own numbers: it spares them
    FULL_LITTER_FOOD = full;
    RARE_PREY = rare_prey;
    PREY_PER_PREDATOR = prey_need;
}

TEST_CASE("a glut of predators turns them on other kinds, then their own") {
    const int glut = PREDATOR_GLUT;
    PREDATOR_GLUT = 3;  // a glut of three predators, for a small world
    Simulation simulation = world();
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator, {{"empathy", 7}, {"tolerance", 7}, {"patience", 7}}), 5, 5);  // loath to
    Bitzkey& kin = simulation.add_agent(hunter.dna, 5, 6);
    Bitzkey& other = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}, {"strength", 7}, {"armour", 7}, {"weapons", 7}, {"speed", 7}, {"sight", 7}}),
                                          6, 5);  // another kind: its ring far apart
    CHECK_FALSE(Simulation::same_kind(hunter, other));
    hunter.hunger = 50;
    struct Step {
        int count;
        bool other_prey, kin_prey;
    };
    for (const Step& step : {Step{3, false, false}, Step{6, true, false}, Step{8, true, true}}) {  // at it, twice it, past it
        for (const auto& extra : simulation.agents()) if (extra->identifier > 2) extra->health = 0;
        for (int x = 0; x < step.count - 3; ++x) simulation.add_agent(encode_dna(Species::Predator), x, 10);
        simulation.index_cells();
        CHECK(Simulation::is_prey(hunter, other) == step.other_prey);
        CHECK(Simulation::is_prey(hunter, kin) == step.kin_prey);
    }
    PREDATOR_GLUT = glut;
}

TEST_CASE("predators turn on other kinds, then their own, as their prey die out") {
    const int rare_prey = RARE_PREY;
    RARE_PREY = 500;  // three predators: rare under 15 prey, famine from 30
    Simulation simulation = world(30);
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator, {{"empathy", 7}, {"tolerance", 7}, {"patience", 7}}), 5, 5);  // loath to
    Bitzkey& kin = simulation.add_agent(hunter.dna, 5, 6);
    Bitzkey& other = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}, {"strength", 7}, {"armour", 7}, {"weapons", 7}, {"speed", 7}, {"sight", 7}}),
                                          6, 5);
    std::vector<Bitzkey*> prey;
    for (int x = 0; x < 30; ++x) prey.push_back(&simulation.add_agent(encode_dna(Species::Grazer), x % 25, 20 + x / 25));
    hunter.hunger = 50;
    struct Step {
        int alive;
        bool other_prey, kin_prey;
    };
    for (const Step& step : {Step{30, false, false}, Step{15, true, false}, Step{0, true, true}}) {  // twice Rare prey, it, none
        for (std::size_t index = static_cast<std::size_t>(step.alive); index < prey.size(); ++index) prey[index]->health = 0;
        simulation.index_cells();
        CHECK(Simulation::is_prey(hunter, other) == step.other_prey);
        CHECK(Simulation::is_prey(hunter, kin) == step.kin_prey);
    }
    RARE_PREY = rare_prey;
}

TEST_CASE("parents pay at least what their young need") {
    FEEBLE_YOUTH = 0.1;
    const double small_litters = SMALL_LITTERS, plants = PLANTS_PER_GRAZER;
    SMALL_LITTERS = 1.0;
    PLANTS_PER_GRAZER = 0.0;
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"litter", 7}, {"size", 4}});
    Bitzkey& first = simulation.add_agent(dna, 5, 5);
    Bitzkey& second = simulation.add_agent(dna, 5, 6);
    for (Bitzkey* parent : {&first, &second}) {
        parent->age = static_cast<int>(std::lround(parent->lifespan / 2));  // grown
        parent->grow();
    }
    simulation.index_cells();
    const double cheap = first.breeding_price();
    simulation.breed(first, second);
    const auto litter = static_cast<double>(first.womb.size());
    CHECK(litter * NEWBORN_FOOD / 2 > cheap);  // a litter needs more than breeding costs them
    CHECK(first.hunger == doctest::Approx(litter * NEWBORN_FOOD / 2));  // so each paid its half of the young's food
    for (const Bitzkey::Young& young : first.womb) CHECK(young.energy / first.stomach == doctest::Approx(NEWBORN_FOOD));
    SMALL_LITTERS = small_litters;
    PLANTS_PER_GRAZER = plants;
}

TEST_CASE("a grown animal feeds the hungriest infant of its kind close by") {
    FEEBLE_YOUTH = 0.1;
    Simulation simulation = world();
    Bitzkey& adult = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 4}}), 5, 5);
    adult.age = static_cast<int>(std::lround(adult.lifespan / 2));
    adult.grow();
    Bitzkey& hungry = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 4}}), 6, 5);
    Bitzkey& fed = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 4}}), 5, 6);
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 4}}), 4, 5);  // another species: not its to feed
    Bitzkey& far = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 4}}), 10, 10);
    simulation.index_cells();
    CHECK((hungry.infant() && fed.infant() && stranger.infant() && far.infant() && !adult.infant()));
    hungry.hunger = 60;
    fed.hunger = 20;
    stranger.hunger = 90;
    far.hunger = 99;
    adult.hunger = 10;
    simulation.nurse(adult);
    CHECK(hungry.hunger == doctest::Approx(60 - hungry.burn));  // the hungriest close by, a turn's burn
    CHECK((fed.hunger == 20 && stranger.hunger == 90 && far.hunger == 99));
    CHECK(adult.hunger == doctest::Approx(10 + hungry.burn * hungry.stomach / adult.stomach));  // from its own stomach
    adult.hunger = NURSING_LIMIT - 0.01;  // too hungry itself
    simulation.nurse(adult);
    CHECK(hungry.hunger == doctest::Approx(60 - hungry.burn));
    simulation.nurse(hungry);  // an infant feeds no one
    CHECK(fed.hunger == 20);
}

TEST_CASE("an infant keeps close to its kind, even a loner's") {
    FEEBLE_YOUTH = 0.1;
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Omnivore, {{"empathy", 7}, {"patience", 7}, {"sight", 3}});
    Bitzkey& infant = simulation.add_agent(dna, 2, 5);
    std::vector<Bitzkey*> grown;
    for (const int x : {4, 5}) {
        Bitzkey& adult = simulation.add_agent(dna, x, 5);
        adult.age = static_cast<int>(std::lround(adult.lifespan / 2));
        adult.grow();
        grown.push_back(&adult);
    }
    simulation.index_cells();
    CHECK(simulation.herd_mate(infant, *grown[0]));  // a solitary omnivore's infant herds with its kind
    CHECK_FALSE(simulation.herd_mate(*grown[0], *grown[1]));  // though grown omnivores still don't
    const auto options = simulation.sense(infant).options;
    CHECK(std::find(options.begin(), options.end(), Herd) != options.end());  // two cells off their middle: it herds
}

TEST_CASE("an omnivore hunts predators only with no other food near") {
    FEEBLE_YOUTH = 0.1;
    Simulation simulation = world(20);
    simulation.clear_grass();
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}, {"strength", 7}, {"weapons", 7}}), 10, 10);
    hunter.age = static_cast<int>(std::lround(hunter.lifespan / 2));
    hunter.grow();
    Bitzkey& target = simulation.add_agent(encode_dna(Species::Predator, {{"size", 0}}), 11, 10);
    target.age = static_cast<int>(std::lround(target.lifespan / 2));  // grown: no infant to guard
    target.grow();
    simulation.index_cells();
    simulation.survey(hunter);
    CHECK(Simulation::is_prey(hunter, target));  // nothing else to eat: a predator will do
    simulation.grass_cell(13, 10) = 1;  // a grassy cell near
    simulation.survey(hunter);
    CHECK_FALSE(Simulation::is_prey(hunter, target));
    simulation.grass_cell(13, 10) = 0;
    simulation.add_agent(encode_dna(Species::Plant), 10, 14);  // or a plant
    simulation.index_cells();
    simulation.survey(hunter);
    CHECK_FALSE(Simulation::is_prey(hunter, target));
}

TEST_CASE("a grown predator guards the infants of its kind") {
    FEEBLE_YOUTH = 0.1;
    Simulation simulation = world(20);
    simulation.clear_grass();
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}, {"strength", 7}, {"weapons", 7}}), 10, 10);
    hunter.age = static_cast<int>(std::lround(hunter.lifespan / 2));
    hunter.grow();
    Bitzkey& infant = simulation.add_agent(encode_dna(Species::Predator, {{"size", 0}}), 11, 10);
    simulation.index_cells();
    simulation.survey(hunter);
    CHECK((infant.infant() && !infant.guarded && Simulation::is_prey(hunter, infant)));  // alone: prey
    Bitzkey& parent = simulation.add_agent(infant.dna, 12, 11);
    parent.age = static_cast<int>(std::lround(parent.lifespan / 2));
    parent.grow();
    simulation.index_cells();
    CHECK((infant.guarded && !parent.guarded));
    CHECK_FALSE(Simulation::is_prey(hunter, infant));  // guarded: prey to no one
}

TEST_CASE("a parent is glad of its infants close by, grieves for one that dies, and its young know their parents") {
    const double newborn_food = NEWBORN_FOOD, plants_per_grazer = PLANTS_PER_GRAZER, feeble_youth = FEEBLE_YOUTH;
    NEWBORN_FOOD = 0;
    PLANTS_PER_GRAZER = 0;
    FEEBLE_YOUTH = 0.1;  // young are infants for a tenth of their lives, as in a world
    Simulation simulation = world(20);
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"litter", 7}, {"sight", 2}});
    Bitzkey& mother = simulation.add_agent(dna, 5, 5);
    Bitzkey& father = simulation.add_agent(dna, 5, 6);
    simulation.index_cells();
    CHECK(simulation.sense(mother).senses[SenseYoungNear] == 0.0);
    simulation.breed(mother, father);
    simulation.give_birth(mother);
    REQUIRE(simulation.agents().size() > 3);
    Bitzkey& infant = *simulation.agents()[2];
    CHECK((infant.mother == mother.identifier && infant.father == father.identifier && infant.infant()));
    CHECK((simulation.find_agent(infant.identifier) == &infant && simulation.find_agent(9999) == nullptr));

    simulation.index_cells();
    const Simulation::Sensed sensed = simulation.sense(mother);
    const int young = std::min(3, sensed.situation.young_near);
    CHECK(young >= 1);
    CHECK(sensed.senses[SenseYoungNear] == doctest::Approx(young / 3.0));
    Simulation::Situation without = sensed.situation;
    without.young_near = 0;
    CHECK(simulation.comfort(mother, sensed.situation) - simulation.comfort(mother, without) ==
          doctest::Approx(PARENT_JOY * (0.5 + mother.feeling(Empathy)) * young / 3));

    mother.reward = father.reward = 0;
    simulation.die(infant, "killed");
    CHECK(mother.reward == doctest::Approx(-PARENT_GRIEF * (0.5 + mother.feeling(Empathy))));
    CHECK(father.reward == doctest::Approx(-PARENT_GRIEF * (0.5 + father.feeling(Empathy))));

    Bitzkey& grown = *simulation.agents()[3];
    grown.age = static_cast<int>(grown.lifespan / 2);  // no longer an infant
    mother.reward = 0;
    simulation.die(grown, "killed");
    CHECK(mother.reward == 0.0);  // a parent grieves for its infants only
    NEWBORN_FOOD = newborn_food;
    PLANTS_PER_GRAZER = plants_per_grazer;
    FEEBLE_YOUTH = feeble_youth;
}

TEST_CASE("grazers rest after breeding, and a hunter regrets killing a mother carrying young") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 0}});
    Bitzkey& first = simulation.add_agent(dna, 5, 5);
    Bitzkey& second = simulation.add_agent(dna, 5, 6);
    simulation.index_cells();
    simulation.breed(first, second);
    CHECK((first.cooldown == static_cast<int>(60 * BREED_COOLDOWN) && second.cooldown == first.cooldown));  // the least fertile rest longest
    CHECK(!first.can_breed());

    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator), 6, 5);
    hunter.reward = 0;
    REQUIRE(!first.womb.empty());
    simulation.die(first, "killed", &hunter, "hunting");
    CHECK(hunter.reward == doctest::Approx(-PREGNANT_REMORSE * (0.5 + hunter.feeling(Empathy))));
    hunter.reward = 0;
    simulation.die(second, "killed", &hunter, "hunting");  // the father carried none
    CHECK(hunter.reward == 0.0);
}

TEST_CASE("a large body costs: it is hungrier, starves faster, matures later, and can't hide from a hunter") {
    const Dna small_dna = encode_dna(Species::Grazer, {{"size", 0}}), large_dna = encode_dna(Species::Grazer, {{"size", 7}});
    SIZE_HUNGER = 0.0;
    const Bitzkey small_plain(1, small_dna, 0, 0), large_plain(2, large_dna, 0, 0);
    SIZE_HUNGER = 1.0;
    const Bitzkey small(3, small_dna, 0, 0), large(4, large_dna, 0, 0);
    CHECK(small.burn == doctest::Approx(small_plain.burn));  // size 0: no more than it was
    CHECK(large.burn == doctest::Approx(2 * large_plain.burn));  // size 7: twice as hungry

    FEEBLE_YOUTH = 0.1;
    SMALL_MATURITY = 0.5;
    const Bitzkey young_small(5, small_dna, 0, 0), young_large(6, large_dna, 0, 0);
    CHECK(young_small.infancy() == doctest::Approx(0.1 * 0.5 * young_small.lifespan));  // half as long as its lifespan makes it
    CHECK(young_large.infancy() == doctest::Approx(0.1 * young_large.lifespan));        // the largest, the whole of it
    FEEBLE_YOUTH = 0.0;

    Simulation simulation = world();
    Bitzkey& starving_small = simulation.add_agent(small_dna, 3, 3);
    Bitzkey& starving_large = simulation.add_agent(large_dna, 9, 9);
    simulation.index_cells();
    for (Bitzkey* agent : {&starving_small, &starving_large}) agent->hunger = 100;
    const double small_before = starving_small.health / starving_small.max_health, large_before = starving_large.health / starving_large.max_health;
    const double climate_damage = CLIMATE_DAMAGE;
    CLIMATE_DAMAGE = 0;  // starving alone
    simulation.take_turn();
    CLIMATE_DAMAGE = climate_damage;
    const double small_lost = small_before - starving_small.health / starving_small.max_health;
    const double large_lost = large_before - starving_large.health / starving_large.max_health;
    // The largest wastes away twice as fast; resting through the turn, if it does, heals each a little back, the small more.
    CHECK(large_lost >= 2 * small_lost - 1e-9);

    SMALL_HIDING = 1.0;  // at the smallest, a blow always misses; at the largest, never
    Simulation hunt = world();
    Bitzkey& hunter = hunt.add_agent(encode_dna(Species::Predator, {{"strength", 7}, {"weapons", 7}}), 5, 5);
    Bitzkey& tiny = hunt.add_agent(small_dna, 5, 6);
    Bitzkey& huge = hunt.add_agent(large_dna, 6, 5);
    hunt.index_cells();
    const double tiny_health = tiny.health, huge_health = huge.health;
    hunt.attack(hunter, tiny);
    CHECK((tiny.health == tiny_health && hunter.last_action == "missed"));
    hunt.attack(hunter, huge);
    CHECK(huge.health < huge_health);
    SMALL_HIDING = 0.0;
}

TEST_CASE("ties between targets equally near favour no side: east and west, north and south, each half the time") {
    Simulation simulation = world();
    simulation.grass_cell(4, 5) = simulation.grass_cell(6, 5) = 1;  // west and east, one step each
    std::set<int> columns, sides;
    for (int index = 0; index < 4; ++index) {
        Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 5, 5);
        columns.insert(simulation.sense(grazer).situation.grass->x);
        sides.insert(simulation.tie_side(grazer).x * 2 + simulation.tie_side(grazer).y);
    }
    CHECK(columns == std::set<int>{4, 6});
    CHECK(sides.size() == 4);  // four animals in a row: every pairing of east or west with north or south
    simulation.clear_grass();
    simulation.grass_cell(5, 4) = simulation.grass_cell(5, 6) = 1;  // north and south
    std::set<int> rows;
    for (int index = 0; index < 4; ++index) rows.insert(simulation.sense(simulation.add_agent(encode_dna(Species::Grazer), 5, 5)).situation.grass->y);
    CHECK(rows == std::set<int>{4, 6});
}

TEST_CASE("a parent feeds its hungry infants within its sight full, at no cost to it, and is glad of it") {
    FEEBLE_YOUTH = 1.0 / 30;  // infants again
    Simulation simulation(Simulation::Settings{30, 30, 0, 1});
    Bitzkey& parent = simulation.add_agent(encode_dna(Species::Grazer), 10, 10);
    Bitzkey& hungry = simulation.add_agent(encode_dna(Species::Grazer), 11, 10);
    Bitzkey& peckish = simulation.add_agent(encode_dna(Species::Grazer), 10, 11);
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Grazer), 9, 10);
    parent.age = static_cast<int>(parent.lifespan / 2);
    parent.children = 2;
    for (Bitzkey* child : {&hungry, &peckish}) child->mother = parent.identifier;
    for (Bitzkey* young : {&hungry, &peckish, &stranger}) young->age = 0;
    hungry.hunger = 80;
    peckish.hunger = 10;
    stranger.hunger = 90;
    parent.hunger = 40;
    parent.reward = 0;
    auto sensed = simulation.sense(parent);
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), FeedYoung) != sensed.options.end());
    simulation.act(parent, FeedYoung, sensed.situation);
    CHECK((hungry.hunger == 0 && peckish.hunger == 0));  // its own, all fed full
    CHECK(stranger.hunger == 90);                        // not another's
    CHECK(parent.hunger == 40);                          // at no cost
    CHECK(parent.reward == doctest::Approx(FEEDING_JOY * (0.5 + parent.feeling(Empathy)) * 0.9));
    CHECK(parent.last_action == "fed its young");
    auto again = simulation.sense(parent);
    CHECK(std::find(again.options.begin(), again.options.end(), FeedYoung) == again.options.end());  // none hungry now
}

TEST_CASE("a call is heard by those within its reach, the louder the nearer, and a larger caller carries further") {
    Simulation simulation = world(40);
    Bitzkey& small = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 0}}), 10, 10);
    Bitzkey& large = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 30, 30);
    CHECK(call_reach(small) < call_reach(large));
    CHECK(call_reach(small) >= 1);
    Bitzkey& near = simulation.add_agent(encode_dna(Species::Predator), 11, 10);  // any kind hears
    Bitzkey& far = simulation.add_agent(encode_dna(Species::Grazer), 10 + call_reach(small) + 1, 10);
    simulation.index_cells();
    small.call = FirstCall + 2;
    small.call_turn = simulation.turn() - 1;  // called last turn
    const int call = FirstCall + 2;
    const auto heard = simulation.sense(near).senses;
    CHECK(heard[heard_sense(call, HeardLoudness)] == doctest::Approx(1 - 1.0 / (call_reach(small) + 1)));
    CHECK(heard[heard_sense(FirstCall, HeardLoudness)] == 0.0);
    CHECK(heard[heard_sense(call, HeardEast)] < 0);  // the caller lies to its west
    CHECK(heard[heard_sense(call, HeardSouth)] == 0.0);
    CHECK(heard[heard_sense(call, HeardKin)] == 0.0);  // a predator hearing a grazer
    CHECK(simulation.sense(far).senses[heard_sense(call, HeardLoudness)] == 0.0);  // beyond its reach
    Bitzkey& herd_mate = simulation.add_agent(encode_dna(Species::Grazer), 10, 11);
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Predator), 10, 9);  // the same call, from either side
    simulation.index_cells();
    stranger.call = call;
    stranger.call_turn = simulation.turn() - 1;
    CHECK(simulation.sense(herd_mate).senses[heard_sense(call, HeardKin)] == 1.0);  // its own kind, nearest
    CHECK(simulation.sense(herd_mate).senses[heard_sense(call, HeardSouth)] < 0);  // and from the north
    small.call_turn = stranger.call_turn = simulation.turn() - 2;  // an old call is no longer heard
    CHECK(simulation.sense(near).senses[heard_sense(call, HeardLoudness)] == 0.0);
}

TEST_CASE("an animal calls alongside its action: silence and the five calls always open, the alert when it has news") {
    CHECK(std::find(ACTIONS.begin(), ACTIONS.end(), "alert") == ACTIONS.end());  // no longer an action
    CHECK(OUTPUTS[call_output(AlertCall)] == "alert");
    Simulation simulation = world(30);
    const Dna dna = encode_dna(Species::Grazer, {{"sight", 7}});
    Bitzkey& grazer = simulation.add_agent(dna, 10, 10);
    simulation.add_agent(dna, 11, 10);  // its herd
    simulation.index_cells();
    auto calm = simulation.sense(grazer);
    CHECK(calm.calls == std::vector<int>{Silent, FirstCall, FirstCall + 1, FirstCall + 2, FirstCall + 3, FirstCall + 4});
    simulation.add_agent(encode_dna(Species::Predator), 10, 8);
    simulation.index_cells();
    auto alarmed = simulation.sense(grazer);
    REQUIRE(alarmed.situation.hunter != nullptr);
    CHECK(std::find(alarmed.calls.begin(), alarmed.calls.end(), AlertCall) != alarmed.calls.end());
    for (int turn = 0; turn < 20; ++turn) simulation.take_turn();  // and every animal that thinks calls something, or keeps silent
    for (const Bitzkey* agent : simulation.living()) CHECK((agent->call >= Silent && agent->call < CALL_OPTIONS));
}

TEST_CASE("a world keeps its calls when saved and loaded") {
    Simulation simulation = world(20);
    Bitzkey& caller = simulation.add_agent(encode_dna(Species::Grazer), 5, 5);
    caller.call = FirstCall + 4;
    caller.call_turn = 7;
    caller.last_call = FirstCall + 4;
    const auto loaded = WorldFile::from_bytes(WorldFile::bytes(simulation));
    const Bitzkey* again = loaded->find_agent(caller.identifier);
    REQUIRE(again);
    CHECK((again->call == FirstCall + 4 && again->call_turn == 7 && again->last_call == FirstCall + 4));
}

TEST_CASE("a brain saved before calls loads with its actions as they were and its calls unweighted") {
    PyRandom rng(4);
    const Brain brain = Brain::random(rng, 1, 20);
    nlohmann::ordered_json record = brain.to_record();
    auto& actions = record["actions"];
    auto& rows = record["output_weights"];
    auto& biases = record["output_biases"];
    actions.erase(actions.begin() + ACTION_COUNT + FirstCall, actions.end());  // its outputs to the alert, as they were
    rows.erase(rows.begin() + ACTION_COUNT + FirstCall, rows.end());
    biases.erase(biases.begin() + ACTION_COUNT + FirstCall, biases.end());
    const Brain old = Brain::from_record(nlohmann::json::parse(record.dump()));
    CHECK(old.output_weights[FeedYoung][3] == doctest::Approx(brain.output_weights[FeedYoung][3]));
    CHECK(old.output_biases[call_output(AlertCall)] == doctest::Approx(brain.output_biases[call_output(AlertCall)]));
    CHECK(old.output_biases[call_output(FirstCall)] == 0.0);
}
