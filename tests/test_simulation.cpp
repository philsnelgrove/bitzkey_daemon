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

// The tests were written before the dark dimmed sight: they see as in their own hours unless one says otherwise.
const bool unhindered_sight = (OFF_HOURS_SIGHT = 1.0, true);

Simulation world(int size = 12, int seed = 3) {
    Simulation simulation(Simulation::Settings{size, size, 0, static_cast<std::uint64_t>(seed)});
    simulation.clear_grass();
    return simulation;
}

// A genome with some of its dot and ring bits flipped, and of its social background.
Dna flipped(Dna dna, std::uint64_t bits, std::uint32_t social = 0) { return {dna.bits ^ bits, dna.social ^ social}; }

}  // namespace

// Grazers' infancy and gestation as long as other animals', for tests of the rule for all of them; restored after.
struct GrazersAsOthers {
    double infancy = GRAZER_INFANCY, gestation = GRAZER_GESTATION;
    GrazersAsOthers() { GRAZER_INFANCY = GRAZER_GESTATION = 1.0; }
    ~GrazersAsOthers() {
        GRAZER_INFANCY = infancy;
        GRAZER_GESTATION = gestation;
    }
};

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

TEST_CASE("random populations spawn in groups sharing a genome: grazers by Grazer group, the rest three to five") {
    BIOME_GROUPS = 0;  // the population alone, without the biomes' groups
    for (const int population : {3, 7, 90, 101}) {
        Simulation simulation(Simulation::Settings{50, 50, population, static_cast<std::uint64_t>(population)});
        std::map<Dna, int> groups;
        std::map<Species, int> kinds;
        for (const auto& agent : simulation.agents()) {
            groups[agent->dna] += 1;
            kinds[agent->species] += 1;
        }
        INFO("population " << population << ": plants " << kinds[Species::Plant] << ", grazers " << kinds[Species::Grazer]
                           << ", omnivores " << kinds[Species::Omnivore] << ", predators " << kinds[Species::Predator]);
        CHECK(static_cast<int>(simulation.agents().size()) <= population);  // each where there is room: a large group may run out
        CHECK(static_cast<int>(simulation.agents().size()) >= population - 5);
        for (const auto& [dna, count] : groups) {
            const bool grazers = static_cast<Species>(decode_body(dna)[SpeciesTrait]) == Species::Grazer;
            if (grazers) CHECK(count <= GRAZER_SPAWN_GROUP + 2);  // or the rest, rather than leave a group too small
            else if (decode_body(dna)[SpeciesTrait] != 0) CHECK(count <= 5);  // plants may share a genome between groups
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
    const GrazersAsOthers as_others;  // the rule for all animals, grazers among them
    CHECK(std::vector<int>{gestation(0), gestation(1), gestation(4), gestation(7)} == std::vector<int>{10, 16, 33, 50});
    const double damage = CLIMATE_DAMAGE;
    CLIMATE_DAMAGE = 0;
    const double newborn_food = NEWBORN_FOOD;
    NEWBORN_FOOD = 0;  // the full litter, whatever energy is given
    const double plants_per_grazer = PLANTS_PER_GRAZER;
    PLANTS_PER_GRAZER = 0;  // no plants about: breeding left to the rest
    Simulation::Settings settings{30, 30, 0, 3};
    settings.body_mutation = 0;  // the young the size of their parents
    Simulation simulation(settings);
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
    plant.fruit = 0;  // picked bare: it can't be browsed, but it can be uprooted
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

TEST_CASE("a grazer is glad of fruit picked without uprooting, and the plant remembers its picker") {
    Simulation simulation(Simulation::Settings{30, 30, 0, 3});
    simulation.clear_grass();
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 10, 10);
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant), 11, 10);
    simulation.index_cells();
    grazer.hunger = 50;
    Simulation::Sensed sensed = simulation.sense(grazer);
    REQUIRE(std::find(sensed.options.begin(), sensed.options.end(), Browse) != sensed.options.end());
    const double reward = grazer.reward;
    simulation.act(grazer, Browse, sensed.situation);
    CHECK(grazer.last_action == "browsed");
    CHECK(grazer.reward >= reward + HARVEST_JOY - 1e-9);  // the gladness of harvest, on top of what else it felt
    CHECK(plant.picked_by == grazer.identifier);
}

TEST_CASE("loyalty: a follower leaves its leader for a better only by chance, the less the more loyal") {
    CHECK(swayed(0.0, 0.01, 0.999));        // no loyalty: any better wins it
    CHECK_FALSE(swayed(1.0, 0.45, 0.0));    // full loyalty: not even a wavering short of LOYALTY_MARGIN more
    CHECK(swayed(1.0, 0.9, 0.5));           // far better: four fifths of the way past it
    CHECK_FALSE(swayed(1.0, 0.6, 0.5));     // a little past it: a fifth
    CHECK_FALSE(swayed(0.5, 0.0, 0.0));     // none better, none left
    CHECK(whim(7, 100) == whim(7, 100));
    CHECK(whim(7, 100) != whim(7, 101));
    CHECK((whim(7, 100) >= 0.0 && whim(7, 100) < 1.0));
}

TEST_CASE("convictions: an empathetic grazer won't uproot, an empathetic predator spares the young, a tolerant one strikes no foe") {
    Simulation simulation(Simulation::Settings{30, 30, 0, 3});
    simulation.clear_grass();
    simulation.set_grass_regrowth(0);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 10, 10);
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"size", 0}}), 11, 10);
    simulation.index_cells();
    const auto offered = [](const Simulation::Sensed& found, int action) {
        return std::find(found.options.begin(), found.options.end(), action) != found.options.end();
    };
    grazer.hunger = 85;
    plant.fruit = 0;
    grazer.mind[Empathy] = 6.0;
    CHECK(offered(simulation.sense(grazer), Uproot));
    grazer.mind[Empathy] = 7.0;  // the most there is: it would rather starve
    CHECK_FALSE(offered(simulation.sense(grazer), Uproot));
    grazer.mind[Tolerance] = 7.0;
    CHECK(Simulation::convinced(grazer, Tolerance));
}

TEST_CASE("convictions: an empathetic predator spares the young") {
    const double feeble_youth = FEEBLE_YOUTH;
    FEEBLE_YOUTH = 0.1;  // young are infants for a tenth of their lives, as in a world
    Simulation simulation = world(20);
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 5, 5);
    Bitzkey& young = simulation.add_agent(encode_dna(Species::Predator, {{"size", 0}}), 6, 5);  // newborn, as in the guarding test
    REQUIRE(young.infant());
    predator.mind[Empathy] = 6.0;
    CHECK_FALSE(Simulation::spares(predator, young));
    predator.mind[Empathy] = 7.0;
    CHECK(Simulation::spares(predator, young));  // the young are spared
    young.age = static_cast<int>(std::lround(young.lifespan / 2));
    young.grow();
    REQUIRE_FALSE(young.infant());
    CHECK_FALSE(Simulation::spares(predator, young));  // a grown one, hunted as ever
    FEEBLE_YOUTH = feeble_youth;
}

TEST_CASE("a child hearing its parent call copies the call and takes in what the parent did, a stranger's not") {
    const double feeble_youth = FEEBLE_YOUTH;
    FEEBLE_YOUTH = 0.1;  // young are infants for a tenth of their lives, as in a world
    Simulation simulation = world(20);
    Bitzkey& parent = simulation.add_agent(encode_dna(Species::Grazer), 5, 5);
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Grazer), 4, 5);
    Bitzkey& child = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 0}}), 6, 5);
    REQUIRE(child.infant());
    child.mother = parent.identifier;
    for (Bitzkey* caller : {&parent, &stranger}) {
        caller->has_last_senses = true;
        caller->last_senses = {};
        caller->last_senses[0] = 1.0;
        caller->last_call = FirstCall + 2;
        caller->last_choice = Eat;
    }
    child.has_last_senses = true;
    child.last_senses = {};
    child.last_senses[1] = 1.0;
    const int call = call_output(FirstCall + 2);
    const double call_before = child.brain->values(parent.last_senses)[call], deed_before = child.brain->values(child.last_senses)[Eat];
    child.heard_from = stranger.identifier;
    child.heard_turn = simulation.turn();
    simulation.learn_from_parent(child, 5.0, 5.0, 0.1);
    CHECK(child.brain->values(parent.last_senses)[call] == call_before);  // a stranger's call, not its parent's
    child.heard_from = parent.identifier;
    simulation.learn_from_parent(child, 5.0, 5.0, 0.1);
    CHECK(child.brain->values(parent.last_senses)[call] > call_before);  // copied, where the parent called it
    CHECK(child.brain->values(child.last_senses)[Eat] > deed_before);  // and, hearing it, what the parent did
    FEEBLE_YOUTH = feeble_youth;
}

TEST_CASE("a sharer brings its food to the store nearest its herd's middle, glad as it is generous, the greedy uneasy") {
    Simulation simulation = world(20);
    Bitzkey& sharer = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    Bitzkey& lighter = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 9, 5);
    lighter.weapon = Club;
    ++lighter.carried_wood;  // the second stick, rubbed against its club
    simulation.light_fire(lighter);
    simulation.add_to_pile(3, 5, false, 1);
    simulation.index_cells();
    const std::optional<Cell> store = simulation.herd_store(sharer, Cell{10, 5}, 6);
    REQUIRE(store);
    CHECK((store->x == 9 && store->y == 5));  // the fire, nearer the herd's middle than the pile
    CHECK(simulation.herd_store(sharer, Cell{1, 5}, 6)->x == 3);  // the pile, nearer a middle the other way
    sharer.mind[Sociability] = 7.0;
    sharer.carried_fruit = 2;
    sharer.cooked_meat = 1;
    sharer.reward = 0;
    for (int step = 0; step < 6 && sharer.cooked_meat; ++step) simulation.provision(sharer, *store);
    CHECK(sharer.carried_fruit + sharer.cooked_meat == 0);
    CHECK(std::abs(sharer.x - 9) + std::abs(sharer.y - 5) <= 1);  // beside the fire
    CHECK(simulation.fruit_pile_at(sharer.x, sharer.y) == 2);
    CHECK(simulation.pile_at(sharer.x, sharer.y, CookedMeatPile) == 1);
    CHECK(Simulation::generosity(sharer) > 0);
    CHECK(sharer.reward == doctest::Approx(SHARE_JOY * Simulation::generosity(sharer)));
    for (const int trait : {Patience, Sociability, Empathy}) sharer.mind[trait] = 0.0;
    sharer.mind[Appetite] = 7.0;  // the greedy
    CHECK(Simulation::generosity(sharer) < 0);
    sharer.carried_fruit = 1;
    sharer.reward = 0;
    simulation.provision(sharer, *store);
    CHECK(sharer.reward < 0);  // uneasy to give it up
    Bitzkey& friend_ = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), sharer.x, sharer.y + 1);
    simulation.index_cells();
    sharer.known.push_back(Acquaintance{friend_.identifier, REGARD_STRONG, 0});
    sharer.mind[Kinship] = 7.0;
    sharer.carried_fruit = 1;
    sharer.reward = 0;
    simulation.provision(sharer, *store);
    CHECK(sharer.reward == doctest::Approx(SHARE_JOY * Simulation::generosity(sharer) + KIN_SHARE_JOY * sharer.feeling(Kinship)));
    CHECK(sharer.reward > 0);  // greedy, yet glad to share with a friend
}

TEST_CASE("scruples: the kinder value uprooting less, and the more tolerant striking a foe, as their traits rise") {
    Simulation simulation(Simulation::Settings{30, 30, 0, 3});
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 10, 10);
    Simulation::Situation nothing;
    for (const double empathy : {0.0, 3.5, 7.0}) {
        grazer.mind[Empathy] = empathy;
        Values valued{};
        simulation.add_scruples(grazer, valued, nothing);
        CHECK(valued[Uproot] == doctest::Approx(-EMPATHY_SCRUPLE * grazer.feeling(Empathy)));
        CHECK(valued[Hunt] == 0.0);  // nothing to hunt or strike
    }
}

TEST_CASE("a plant on worked land bears twice the fruit, and twice again where bone was fed, spending it") {
    Simulation simulation(10, 40, 0, 1);
    int temperate = -1;
    for (int y = 0; y < 40; ++y) {
        if (zone_temperature(y, 40) == Medium) temperate = y;
    }
    REQUIRE(temperate >= 0);
    Bitzkey& wild = simulation.add_agent(encode_dna(Species::Plant), 2, temperate);
    Bitzkey& farmer = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, temperate);
    Bitzkey& farmed = simulation.add_agent(encode_dna(Species::Plant), 6, temperate);
    simulation.index_cells();
    CHECK(simulation.morning_fruit(wild) == 1);
    farmer.weapon = Club;
    simulation.dig(farmer);
    REQUIRE(simulation.arable_at(6, temperate));
    CHECK(simulation.morning_fruit(farmed) == ARABLE_FRUIT);
    farmer.carried_bone = 1;
    simulation.fertilize(farmer);
    REQUIRE(simulation.fertilized_at(6, temperate));
    CHECK(simulation.morning_fruit(farmed) == ARABLE_FRUIT * FERTILIZED_FRUIT);
    CHECK_FALSE(simulation.fertilized_at(6, temperate));  // the bone spent
    CHECK(simulation.morning_fruit(farmed) == ARABLE_FRUIT);
}

TEST_CASE("worked land left untended goes wild, its bone with it") {
    Simulation simulation(10, 40, 0, 1);
    int temperate = -1;
    for (int y = 0; y < 40; ++y) {
        if (zone_temperature(y, 40) == Medium) temperate = y;
    }
    REQUIRE(temperate >= 0);
    Bitzkey& farmer = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, temperate);
    simulation.index_cells();
    farmer.weapon = Club;
    simulation.dig(farmer);
    farmer.carried_bone = 1;
    simulation.fertilize(farmer);
    REQUIRE(simulation.fertilized_at(6, temperate));
    CHECK(simulation.tended_at(6, temperate) == simulation.turn());
    simulation.go_fallow();
    CHECK(simulation.arable_at(6, temperate));  // tended just now
    const double years = FALLOW_YEARS;
    FALLOW_YEARS = 0;
    simulation.go_fallow();
    CHECK(simulation.arable_at(6, temperate));  // never, at 0
    simulation.advance();
    FALLOW_YEARS = 1e-9;  // as if a year had passed untended
    simulation.go_fallow();
    FALLOW_YEARS = years;
    CHECK_FALSE(simulation.arable_at(6, temperate));
    CHECK_FALSE(simulation.fertilized_at(6, temperate));
}

TEST_CASE("an axe fells a bare plant too, and the kind are loath to fell a farmed one") {
    Simulation simulation(10, 40, 0, 1);
    int temperate = -1;
    for (int y = 0; y < 40; ++y) {
        if (zone_temperature(y, 40) == Medium) temperate = y;
    }
    REQUIRE(temperate >= 0);
    Bitzkey& feller = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, temperate);
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant), 6, temperate);
    simulation.index_cells();
    const auto offered = [&] {
        const Simulation::Sensed sensed = simulation.sense(feller);
        return std::find(sensed.options.begin(), sensed.options.end(), Cut) != sensed.options.end();
    };
    plant.fruit = 0;  // picked bare
    CHECK_FALSE(offered());  // no axe
    feller.weapon = BoneAxe;
    CHECK(offered());  // bare, and still timber
    feller.mind[Empathy] = 7.0;
    CHECK(offered());  // a wild plant is fair game
    Bitzkey& farmer = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, temperate);
    farmer.weapon = Club;
    simulation.dig(farmer);
    REQUIRE(simulation.arable_at(6, temperate));
    CHECK_FALSE(offered());  // a farmed one, spared by the kindest
    feller.mind[Empathy] = 3.0;
    CHECK(offered());
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
    CHECK(herd_mate.seen_failure > 0);  // a slow death, but a famine: its herd learns from it (Famine lesson)

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
    // Another program's file, named in letters no one code page spells (Cyrillic, Greek, Chinese, an emoji), is passed over.
    const std::filesystem::path foreign = std::filesystem::temp_directory_path() / std::filesystem::path(u8"bitzkey-test-Шя-Ωλ-中文-\U0001F600");
    std::filesystem::create_directories(foreign);
    CHECK_NOTHROW(remove_stale_histories());
    std::filesystem::remove_all(foreign);
}

TEST_CASE("deploying puts five of each around a cell, with their brains and minds, and lets a rewound future go") {
    Simulation simulation(Simulation::Settings{30, 30, 40, 3});
    for (int turn = 0; turn < 5; ++turn) simulation.advance();
    const Bitzkey& animal = **std::find_if(simulation.agents().begin(), simulation.agents().end(),
                                           [](const auto& agent) { return agent->alive() && agent->brain && agent->terrain == Terrain::Terrestrial; });
    const Imported bitzkey = import_bitzkeys(dumps(bitzkey_record(animal))).front();
    for (const auto& agent : simulation.agents()) {  // room about the cell, however crowded with plants
        if (agent->alive() && std::abs(agent->x - 10) + std::abs(agent->y - 1) <= 8) simulation.die(*agent, "eaten");
    }
    simulation.index_cells();
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

TEST_CASE("a new world's grazers start apart from its hunters") {
    const int apart = HUNTERS_APART;
    std::vector<int> near;
    for (const int setting : {15, 0}) {
        HUNTERS_APART = setting;
        const Simulation simulation(Simulation::Settings{120, 80, 200, 5});
        int count = 0;
        for (const auto& grazer : simulation.agents()) {
            if (grazer->species != Species::Grazer) continue;
            count += std::any_of(simulation.agents().begin(), simulation.agents().end(), [&](const auto& hunter) {
                if (hunter->species != Species::Omnivore && hunter->species != Species::Predator) return false;
                const int across = std::abs(grazer->x - hunter->x);
                return std::min(across, 120 - across) + std::abs(grazer->y - hunter->y) <= 8;
            });
        }
        near.push_back(count);
    }
    CHECK(near[0] * 10 < near[1]);  // a hunter within eight squares of far fewer grazers
    HUNTERS_APART = apart;
}

TEST_CASE("a grazer's infancy and gestation are half as long as other animals'") {
    Simulation simulation = world(30);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 5, 5);
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}}), 6, 5);
    CHECK(grazer.infancy() / grazer.lifespan == doctest::Approx(GRAZER_INFANCY * omnivore.infancy() / omnivore.lifespan));
    Bitzkey& mate = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 5, 6);
    simulation.index_cells();
    simulation.breed(grazer, mate);
    CHECK(grazer.due_turn == static_cast<int>(gestation(7) * GRAZER_GESTATION + 0.5));
}

TEST_CASE("a new world's plants grow alone first, uncounted, then its animals spawn") {
    Simulation::Settings settings{60, 60, 40, 7};
    const Simulation bare(settings);
    settings.pregrowth = 200;
    const Simulation grown(settings);
    const auto plants = [](const Simulation& world) {
        return std::count_if(world.agents().begin(), world.agents().end(), [](const auto& agent) { return agent->species == Species::Plant; });
    };
    CHECK(plants(grown) > plants(bare));  // they spread meanwhile
    CHECK(grown.turn() == 0);  // none of it counted
    CHECK(grown.deaths() == 0);
    CHECK(std::any_of(grown.agents().begin(), grown.agents().end(), [](const auto& agent) { return agent->species == Species::Grazer; }));
}

TEST_CASE("what a brain values beyond the Value limit is taught back toward it") {
    const double limit = VALUE_LIMIT;
    Simulation simulation = world(20);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"intellect", 7}}), 5, 5);
    simulation.index_cells();
    const Senses senses = simulation.sense(grazer).senses;
    for (int lesson = 0; lesson < 200; ++lesson) grazer.own_brain().learn(senses, Eat, 90.0, 0.5);  // taught far past the limit
    const double before = grazer.brain->values(senses)[Eat];
    REQUIRE(before > 60.0);
    VALUE_LIMIT = 50.0;
    for (int turn = 0; turn < 20; ++turn) simulation.rein_in(grazer, senses, grazer.brain->values(senses));
    CHECK(grazer.brain->values(senses)[Eat] < before);  // pulled back toward the limit
    VALUE_LIMIT = limit;
}

TEST_CASE("an omnivore is an infant half as long, and an infant feels a quarter of a plant's thorns") {
    const double youth = FEEBLE_YOUTH;
    FEEBLE_YOUTH = 0.1;
    Simulation simulation = world(20);
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}}), 5, 5);
    const double half = omnivore.infancy();
    const double share = OMNIVORE_INFANCY;
    OMNIVORE_INFANCY = 1.0;
    CHECK(half == doctest::Approx(share * omnivore.infancy()));
    OMNIVORE_INFANCY = share;
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"weapons", 7}}), 6, 5);
    omnivore.age = 0;
    omnivore.grow();
    REQUIRE(omnivore.infant());
    const double as_infant = Simulation::damage(plant, omnivore, true);
    omnivore.age = static_cast<int>(0.5 * omnivore.lifespan);
    omnivore.grow();
    const double grown = Simulation::damage(plant, omnivore, true);
    CHECK(as_infant < grown);  // a quarter, and an infant's toughness besides
    FEEBLE_YOUTH = youth;
}

TEST_CASE("the first Bitzkeys of a world start grown") {
    const double youth = FEEBLE_YOUTH;
    FEEBLE_YOUTH = 0.1;
    Simulation::Settings settings{30, 30, 40, 3};
    Simulation simulation(settings);
    int animals = 0;
    for (const auto& agent : simulation.agents()) {
        if (agent->species == Species::Plant) continue;
        ++animals;
        CHECK_FALSE(agent->infant());
        CHECK_FALSE(agent->feeble);
        CHECK(agent->health == doctest::Approx(agent->max_health));
    }
    CHECK(animals > 0);
    FEEBLE_YOUTH = youth;
}

TEST_CASE("no frail Bitzkey breeds: neither an infant nor the old") {
    const double youth = FEEBLE_YOUTH;
    FEEBLE_YOUTH = 0.1;
    Simulation simulation = world();
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 5, 5);
    grazer.age = 0;
    grazer.grow();
    CHECK(grazer.feeble);
    CHECK_FALSE(grazer.can_breed());  // an infant
    grazer.age = static_cast<int>(std::lround(0.5 * grazer.lifespan));
    grazer.grow();
    CHECK_FALSE(grazer.feeble);
    CHECK(grazer.can_breed());  // grown
    grazer.age = static_cast<int>(AGEING * grazer.lifespan) + 1;
    grazer.grow();
    CHECK(grazer.feeble);
    CHECK_FALSE(grazer.can_breed());  // old
    FEEBLE_YOUTH = youth;
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

TEST_CASE("a slow death is not blamed on the last choice, nor seen by the herd, but for a famine's lesson") {
    for (const auto& [cause, blamed] : std::vector<std::pair<std::string, bool>>{{"starved", true}, {"froze", false}, {"old age", false}, {"exhausted", true}}) {
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
    const GrazersAsOthers as_others;  // the rule for all animals, grazers among them
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

TEST_CASE("the grass of a grazer's or omnivore's home band counts toward its plants, as well as it digests grass") {
    Simulation simulation = world(30);  // temperate bands: rows 6-11 and 18-23
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 15, 8);
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore), 16, 8);
    simulation.index_cells();
    CHECK(simulation.home_plants(grazer) == 0.0);  // no plants, no grass
    for (const int x : {2, 3, 4, 5}) simulation.grass_cell(x, 7) = 4;  // sixteen tufts in its band
    simulation.index_cells();
    CHECK(simulation.home_plants(grazer) == doctest::Approx(GRAZING[1] * STANDING_GRASS * 16 / PLANT_MEALS));
    CHECK(simulation.home_fed(omnivore) > 0.0);  // half its share from plants: the grass as plants, less well digested
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
    const GrazersAsOthers as_others;  // the rule for all animals, grazers among them
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

    const double restraint = HUNTER_RESTRAINT;
    HUNTER_RESTRAINT = 0;  // remorse alone here; restraint has its own test
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator), 6, 5);
    hunter.reward = 0;
    REQUIRE(!first.womb.empty());
    simulation.die(first, "killed", &hunter, "hunting");
    CHECK(hunter.reward == doctest::Approx(-PREGNANT_REMORSE * (0.5 + hunter.feeling(Empathy))));
    hunter.reward = 0;
    simulation.die(second, "killed", &hunter, "hunting");  // the father carried none
    CHECK(hunter.reward == 0.0);
    HUNTER_RESTRAINT = restraint;
}

TEST_CASE("a large body costs: it is hungrier, starves faster, matures later, and can't hide from a hunter") {
    const GrazersAsOthers as_others;  // the rule for all animals, grazers among them
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
    // The largest wastes away twice as fast; resting through the turn, if either does, heals a little back.
    CHECK(large_lost >= 1.5 * small_lost);

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

TEST_CASE("food is carried up to size + 1 portions, dropped in piles, and taken or eaten from them by any") {
    const double active = ACTIVE_SHARE;
    ACTIVE_SHARE = 1.0;  // none asleep
    Simulation simulation(Simulation::Settings{30, 30, 0, 1});
    Bitzkey& carrier = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 1}}), 10, 10);
    Bitzkey& other = simulation.add_agent(encode_dna(Species::Grazer), 12, 10);
    const auto offered = [](const auto& sensed, int action) { return std::find(sensed.options.begin(), sensed.options.end(), action) != sensed.options.end(); };
    CHECK(carrier.capacity() == carrier.trait(Size) + 1);
    simulation.add_to_pile(10, 10, true, carrier.capacity() + 1);
    simulation.add_to_pile(13, 10, false, 2);
    simulation.index_cells();
    for (int portion = 0; portion < carrier.capacity(); ++portion) {
        auto sensed = simulation.sense(carrier);
        REQUIRE(offered(sensed, TakeMeat));
        simulation.act(carrier, TakeMeat, sensed.situation);
    }
    CHECK(carrier.carried_meat == carrier.capacity());
    CHECK(simulation.meat_pile_at(10, 10) == 1);
    auto full = simulation.sense(carrier);
    CHECK_FALSE(offered(full, TakeMeat));  // no room for more
    CHECK(full.senses[SenseLoad] == doctest::Approx(1.0));
    simulation.act(carrier, DropMeat, full.situation);
    CHECK(simulation.meat_pile_at(10, 10) == 2);
    CHECK(carrier.last_action == "dropped meat");
    other.hunger = 80;  // a grazer too eats from a pile, the one that fills it more
    auto grazer = simulation.sense(other);
    CHECK(grazer.senses[SenseFruitPileCloseness] > 0);
    simulation.act(other, EatPile, grazer.situation);
    CHECK(simulation.meat_pile_at(10, 10) == 1);
    CHECK(simulation.fruit_pile_at(13, 10) == 2);
    CHECK(other.hunger < 80);
    const int carried = carrier.carried_meat;
    carrier.health = 0;
    simulation.die(carrier, "killed");  // what it carried falls where it died
    CHECK(simulation.meat_pile_at(10, 10) == 1 + carried);
    CHECK(carrier.carried_meat == 0);
    ACTIVE_SHARE = active;
}

TEST_CASE("a pile spoils once left long enough") {
    const double lasts = PILE_LASTS;
    Simulation simulation(Simulation::Settings{30, 30, 0, 1});
    simulation.add_to_pile(3, 4, false, 2);
    simulation.spoil_piles();
    CHECK(simulation.fruit_pile_at(3, 4) == 2);
    PILE_LASTS = 0;
    simulation.spoil_piles();
    CHECK(simulation.fruit_pile_at(3, 4) == 0);
    CHECK(simulation.carrying[Simulation::Spoiled].load() == 2);
    PILE_LASTS = lasts;
}

TEST_CASE("an animal may eat what it carries") {
    const double active = ACTIVE_SHARE;
    ACTIVE_SHARE = 1.0;
    Simulation simulation(Simulation::Settings{30, 30, 0, 1});
    Bitzkey& carrier = simulation.add_agent(encode_dna(Species::Omnivore), 10, 10);
    carrier.carried_fruit = 1;
    carrier.hunger = 0;
    auto fed = simulation.sense(carrier);
    CHECK(std::find(fed.options.begin(), fed.options.end(), EatCarried) != fed.options.end());
    carrier.hunger = 90;
    auto hungry = simulation.sense(carrier);
    CHECK(std::find(hungry.options.begin(), hungry.options.end(), EatCarried) != hungry.options.end());
    simulation.act(carrier, EatCarried, hungry.situation);
    CHECK(carrier.carried_fruit == 0);
    CHECK(carrier.hunger < 90);
    ACTIVE_SHARE = active;
}

TEST_CASE("meat is taken from a fresh kill and fruit from a plant, as portions of them") {
    const double active = ACTIVE_SHARE;
    ACTIVE_SHARE = 1.0;
    Simulation simulation(Simulation::Settings{30, 30, 0, 1});
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Omnivore), 10, 10);
    Bitzkey& body = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 11, 10);
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant), 9, 10);
    simulation.index_cells();
    body.health = 0;
    simulation.die(body, "killed");
    const int portions = body.portions;
    auto sensed = simulation.sense(hunter);
    simulation.act(hunter, TakeMeat, sensed.situation);
    CHECK(hunter.carried_meat == 1);
    CHECK(body.portions == portions - 1);
    const double health = plant.health;
    auto again = simulation.sense(hunter);
    simulation.act(hunter, TakeFruit, again.situation);
    CHECK(hunter.carried_fruit == 1);
    CHECK(plant.health == health);  // a fruit picked, the plant unhurt
    CHECK(Simulation::fruit_left(plant) == Simulation::fruit_capacity(plant) - 1);
    ACTIVE_SHARE = active;
}

TEST_CASE("an animal's load and the piles are kept in a saved world") {
    Simulation simulation(Simulation::Settings{30, 30, 0, 1});
    Bitzkey& carrier = simulation.add_agent(encode_dna(Species::Grazer), 10, 10);
    carrier.carried_fruit = 1;
    simulation.add_to_pile(3, 4, true, 2);
    simulation.add_to_pile(3, 4, false, 5);
    const auto loaded = WorldFile::from_bytes(WorldFile::bytes(simulation));
    CHECK(loaded->meat_pile_at(3, 4) == 2);
    CHECK(loaded->fruit_pile_at(3, 4) == 5);
    CHECK(loaded->agents().front()->carried_fruit == 1);
}

TEST_CASE("an elder gives a willing student a copy of its brain, once, and it fits the student") {
    const double active = ACTIVE_SHARE, trusting = STUDENT_TRUST;
    ACTIVE_SHARE = 1.0;  // none asleep
    STUDENT_TRUST = 0.0;
    Simulation simulation(Simulation::Settings{30, 30, 0, 1});
    Bitzkey& elder = simulation.add_agent(encode_dna(Species::Grazer), 10, 10);
    Bitzkey& student = simulation.add_agent(encode_dna(Species::Grazer, {{"intellect", STUDENT_INTELLECT}}), 11, 10);
    elder.age = static_cast<int>(AGEING * elder.lifespan) + 1;
    student.age = 10;
    elder.reward = 0;
    auto sensed = simulation.sense(elder);
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), Teach) != sensed.options.end());
    CHECK(sensed.senses[SenseStudentNear] == doctest::Approx(1.0 / 3));
    CHECK(simulation.sense(student).senses[SenseElderAge] == doctest::Approx(std::min(1.0, static_cast<double>(elder.age) / YEAR_LENGTH)));
    const std::vector<int> studied = {student.brain->depth(), student.brain->width()};
    simulation.act(elder, Teach, sensed.situation);
    simulation.teach_all();
    CHECK(student.taught_by == elder.identifier);
    CHECK(std::vector<int>{student.brain->depth(), student.brain->width()} == studied);
    CHECK(student.brain->output_biases == elder.brain->output_biases);
    CHECK(student.brain != elder.brain);  // a copy, to go its own way
    CHECK(elder.reward > 0);
    CHECK(student.known[0].regard > 0);
    auto again = simulation.sense(elder);
    CHECK(std::find(again.options.begin(), again.options.end(), Teach) == again.options.end());  // taught once in its life
    ACTIVE_SHARE = active;
    STUDENT_TRUST = trusting;
}

TEST_CASE("the young, the old and the distrustful aren't taught") {
    const double active = ACTIVE_SHARE, trusting = STUDENT_TRUST;
    ACTIVE_SHARE = 1.0;
    STUDENT_TRUST = 2.0;  // none so trusting
    Simulation simulation(Simulation::Settings{30, 30, 0, 1});
    Bitzkey& elder = simulation.add_agent(encode_dna(Species::Grazer), 10, 10);
    Bitzkey& student = simulation.add_agent(encode_dna(Species::Grazer, {{"intellect", STUDENT_INTELLECT}}), 11, 10);
    elder.age = static_cast<int>(AGEING * elder.lifespan) + 1;
    student.age = 10;
    const auto offered = [&] {
        const auto options = simulation.sense(elder).options;
        return std::find(options.begin(), options.end(), Teach) != options.end();
    };
    CHECK_FALSE(offered());
    STUDENT_TRUST = 0.0;
    CHECK(offered());
    student.age = elder.age;
    CHECK_FALSE(offered());  // no younger than half its age
    student.age = 10;
    elder.age = static_cast<int>(AGEING * elder.lifespan);
    CHECK_FALSE(offered());  // not yet feeble with age
    elder.age += 1;
    const int bright = STUDENT_INTELLECT;
    STUDENT_INTELLECT = 8;  // none so bright
    CHECK_FALSE(offered());
    STUDENT_INTELLECT = bright;
    ACTIVE_SHARE = active;
    STUDENT_TRUST = trusting;
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

TEST_CASE("an animal's memory is written as it chooses, read back as it senses, and kept with the world") {
    Simulation simulation = world(30);
    for (int index = 0; index < 6; ++index) simulation.add_agent(encode_dna(Species::Grazer), 5 + index, 5);
    simulation.add_agent(encode_dna(Species::Predator), 20, 20);
    for (int turn = 0; turn < 6; ++turn) simulation.take_turn();
    const Bitzkey* grazer = nullptr;
    for (const Bitzkey* agent : simulation.living()) {
        if (agent->species == Species::Grazer && agent->last_wrote) grazer = agent;
    }
    REQUIRE(grazer);
    CHECK(std::any_of(grazer->held.begin(), grazer->held.end(), [](double held) { return held != 0.0; }));
    CHECK(grazer->past.size() <= static_cast<std::size_t>(MEMORY_STEPS));
    Bitzkey& reader = *simulation.find_agent(grazer->identifier);
    const auto sensed = simulation.sense(reader).senses;
    for (int held = 0; held < MEMORY_NEURONS; ++held) CHECK(sensed[SenseMemory + held] == grazer->held[held]);
    const auto loaded = WorldFile::from_bytes(WorldFile::bytes(simulation));
    const Bitzkey* again = loaded->find_agent(grazer->identifier);
    REQUIRE(again);
    CHECK((again->held == grazer->held && again->past == grazer->past && again->last_wrote == grazer->last_wrote));
}

TEST_CASE("an animal's mental map remembers each place by season, points to the best, and forgets the longest ago") {
    Simulation simulation = world(60);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 4, 4);
    simulation.index_cells();
    Senses found{};
    found[SenseGrassHere] = 1.0;  // food here
    found[SenseClimateDiscomfort] = 0.5;
    simulation.note_place(grazer, found);
    REQUIRE(grazer.places.size() == 1);
    const MapPlace& first = grazer.places[0];
    const auto quarter = static_cast<std::size_t>(season(simulation.turn()));
    CHECK((first.column == 0 && first.row == 0 && first.seen[quarter] && first.food[quarter] == 1.0 && first.comfort[quarter] == 0.5));
    found[SenseGrassHere] = 0.0;
    simulation.note_place(grazer, found);  // again, finding none: by Map learning toward it
    CHECK(grazer.places[0].food[quarter] == doctest::Approx(1 - MAP_LEARNING));
    grazer.x = 30;  // a patch with no food, far east of the first
    grazer.y = 4;
    found[SenseThreatCloseness] = 0.8;  // and a hunter close
    simulation.note_place(grazer, found);
    const auto senses = simulation.sense(grazer).senses;
    CHECK(senses[SenseMapFood] == doctest::Approx(1 - MAP_LEARNING));
    CHECK(senses[SenseMapFoodEast] < 0);  // the food lies back west
    CHECK(senses[SenseMapDanger] == doctest::Approx(0.8));
    CHECK(std::abs(senses[SenseMapDangerEast]) < 0.1);  // the danger is here
    CHECK(senses[SenseMapComfort] == 0.0);  // it has known no place next season
    const int places = MAP_PLACES;
    MAP_PLACES = 2;
    grazer.x = 50;
    simulation.note_place(grazer, found);  // a third place: the one longest ago is forgotten
    CHECK(grazer.places.size() == 2);
    CHECK(std::none_of(grazer.places.begin(), grazer.places.end(), [](const MapPlace& place) { return place.column == 0; }));
    MAP_PLACES = places;
    const auto loaded = WorldFile::from_bytes(WorldFile::bytes(simulation));
    CHECK(loaded->find_agent(grazer.identifier)->places == grazer.places);
}

TEST_CASE("an animal remembers others by what they did to it, senses the nearest, and knows a caller") {
    Simulation simulation = world(40);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"sight", 7}}), 10, 10);
    Bitzkey& friendly = simulation.add_agent(encode_dna(Species::Grazer), 12, 10);
    Bitzkey& brute = simulation.add_agent(encode_dna(Species::Predator), 10, 12);  // within earshot
    simulation.index_cells();
    simulation.note_deed(grazer, friendly, ALERT_DEED);  // it heeded an alert
    simulation.struck(grazer, brute);                    // and was struck
    REQUIRE(grazer.known.size() == 2);
    CHECK((grazer.known[0].identifier == friendly.identifier && grazer.known[0].regard == ALERT_DEED));
    CHECK(grazer.known[1].regard == BLOW_DEED);
    simulation.note_deed(grazer, friendly, FEEDING_DEED);  // a later deed moves its regard by Regard learning
    CHECK(grazer.known[0].regard == doctest::Approx(ALERT_DEED + REGARD_LEARNING * (FEEDING_DEED - ALERT_DEED)));
    auto senses = simulation.sense(grazer).senses;
    CHECK(senses[SenseKnownRegard] == doctest::Approx(grazer.known[0].regard));  // the nearest it knows: the friend, two east
    CHECK(senses[SenseKnownEast] > 0);
    CHECK(senses[SenseKnownCloseness] > 0);
    CHECK(senses[SenseCallerKnown] == 0.0);
    brute.call = FirstCall;  // the brute calls
    brute.call_turn = simulation.turn() - 1;
    senses = simulation.sense(grazer).senses;
    CHECK(senses[SenseCallerKnown] == 1.0);
    CHECK(senses[SenseCallerRegard] == doctest::Approx(BLOW_DEED));
    const int acquaintances = ACQUAINTANCES;
    ACQUAINTANCES = 2;
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Grazer), 20, 20);
    simulation.note_deed(grazer, stranger, MATING_DEED);  // a third: the one regarded least, either way, is forgotten
    CHECK(grazer.known.size() == 2);
    CHECK(std::none_of(grazer.known.begin(), grazer.known.end(), [&](const Acquaintance& one) { return one.identifier == friendly.identifier; }));
    ACQUAINTANCES = acquaintances;
    const auto loaded = WorldFile::from_bytes(WorldFile::bytes(simulation));
    CHECK(loaded->find_agent(grazer.identifier)->known == grazer.known);
}

TEST_CASE("stones lie scattered once, and bone and wood pile up where bodies fall") {
    Simulation simulation = world();
    int stones = 0;
    for (int y = 0; y < simulation.height(); ++y) {
        for (int x = 0; x < simulation.width(); ++x) stones += simulation.pile_at(x, y, StonePile);
    }
    CHECK(stones > 0);
    CHECK(stones <= simulation.width() * simulation.height() / STONE_SCATTER_PER);

    Bitzkey& carrier = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 3, 3);
    carrier.carried_stone = 1;
    carrier.carried_wood = 2;
    CHECK(carrier.load() == 3);

    simulation.add_to_pile(5, 5, WoodPile, 2);
    simulation.index_cells();
    simulation.take_turn();
    CHECK(simulation.pile_at(5, 5, WoodPile) == 2);  // not rotted yet
    const double wood_lasts = WOOD_LASTS;
    WOOD_LASTS = 0;
    simulation.index_cells();
    simulation.take_turn();
    WOOD_LASTS = wood_lasts;
    CHECK(simulation.pile_at(5, 5, WoodPile) == 0);
}

TEST_CASE("a deed seen counts for a share, a blow only against one's own kind, and lighting a fire pays") {
    Simulation simulation(40, 40, 0, 1);
    Bitzkey& doer = simulation.add_agent(encode_dna(Species::Omnivore, {{"sight", 7}}), 10, 10);
    Bitzkey& watcher = simulation.add_agent(encode_dna(Species::Grazer, {{"sight", 7}}), 12, 10);
    Bitzkey& kin = simulation.add_agent(encode_dna(Species::Grazer, {{"sight", 7}}), 11, 10);
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Predator, {{"sight", 7}}), 10, 11);
    simulation.index_cells();
    simulation.deed_seen(doer, BLOW_DEED, &kin, HarmDeed);
    REQUIRE(watcher.known.size() == 1);
    CHECK(watcher.known[0].regard == doctest::Approx(BLOW_DEED * WITNESS_SHARE));
    CHECK(kin.known.empty());  // the victim's own regard is the blow's, not a witness's
    CHECK(watcher.known[0].danger == doctest::Approx(WITNESS_SHARE));
    const double regard_before = watcher.known[0].regard, danger_before = watcher.known[0].danger;
    simulation.deed_seen(doer, BLOW_DEED, &stranger, HarmDeed);
    CHECK(watcher.known[0].regard == regard_before);  // no wrong to it, not its own kind struck
    CHECK(watcher.known[0].danger > danger_before);   // yet it has seen what the doer can do
    doer.weapon = NoWeapon;
    const double skill_before = watcher.known[0].skill;
    simulation.note_deed(watcher, doer, CRAFT_DEED, 1.0, SkillDeed, false);
    CHECK(watcher.known[0].skill > skill_before);
    CHECK(simulation.sense(watcher).senses[SenseKnownSkill] == doctest::Approx(watcher.known[0].skill));
    CHECK(simulation.sense(watcher).senses[SenseKnownDanger] == doctest::Approx(watcher.known[0].danger));
    watcher.known[0].regard = regard_before;
    const double reward = doer.reward;
    doer.weapon = Club;
    ++doer.carried_wood;  // the second stick, rubbed against its club
    simulation.light_fire(doer);
    CHECK(doer.reward == doctest::Approx(reward + FIRE_JOY));
    CHECK(watcher.known[0].regard == doctest::Approx(regard_before + REGARD_LEARNING * WITNESS_SHARE * (FIRE_DEED - regard_before)));
    Bitzkey& crafter = simulation.add_agent(doer.dna, 14, 10);
    simulation.index_cells();
    simulation.deed_seen(crafter, CRAFT_DEED, nullptr, SkillDeed);
    simulation.deed_seen(crafter, CRAFT_DEED, nullptr, SkillDeed);
    const auto seen = std::find_if(watcher.known.begin(), watcher.known.end(), [&](const Acquaintance& one) { return one.identifier == crafter.identifier; });
    REQUIRE(seen != watcher.known.end());
    CHECK(seen->skill >= SKILL_HELD);  // two crafts seen are enough to be held skilled
    CHECK(seen->regard < REGARD_STRONG);  // though no friend for it
}

TEST_CASE("an enemy is no pleasure to see but a pleasure to strike, and a friend the reverse") {
    Simulation simulation(40, 40, 0, 1);
    Bitzkey& agent = simulation.add_agent(encode_dna(Species::Grazer, {{"sight", 7}}), 10, 10);
    Simulation::Situation near;
    const double plain = simulation.comfort(agent, near);
    near.regard_near = 0.8;
    CHECK(simulation.comfort(agent, near) == doctest::Approx(plain + REGARD_COMFORT * 0.8 * (0.5 + agent.feeling(Trust))));
    near.regard_near = -0.8;
    CHECK(simulation.comfort(agent, near) == doctest::Approx(plain - REGARD_COMFORT * 0.8 * (0.5 + agent.fearfulness())));
    near.regard_near = 0.3;
    CHECK(simulation.comfort(agent, near) == doctest::Approx(plain));  // neither friend nor enemy
    CHECK(simulation.harm_felt(agent, -1.0) == doctest::Approx(GRUDGE_JOY * (0.5 + agent.feeling(Aggression))));
    CHECK(simulation.harm_felt(agent, 1.0) == doctest::Approx(-GRUDGE_JOY * (0.5 + agent.feeling(Empathy))));
    CHECK(simulation.harm_felt(agent, 0.2) == 0.0);
    CHECK(simulation.help_felt(agent, 1.0) == doctest::Approx(FRIEND_JOY * (0.5 + agent.feeling(Empathy))));
    CHECK(simulation.help_felt(agent, -1.0) == 0.0);
}

TEST_CASE("one held skilled comforts and is copied, a teacher anchors its student, and one to follow spares dread") {
    Simulation simulation(40, 40, 0, 1);
    Bitzkey& agent = simulation.add_agent(encode_dna(Species::Omnivore, {{"sight", 7}}), 10, 10);
    Simulation::Situation near;
    const double plain = simulation.comfort(agent, near);
    near.skill_near = 0.8;
    CHECK(simulation.comfort(agent, near) == doctest::Approx(plain + SKILL_COMFORT * 0.8 * (0.5 + agent.feeling(Curiosity))));
    near.skill_near = 0.3;
    CHECK(simulation.comfort(agent, near) == doctest::Approx(plain));  // not held skilled
    near.teacher_steps = ELDER_REACH;
    CHECK(simulation.comfort(agent, near) == doctest::Approx(plain + ELDER_COMFORT * (0.5 + agent.feeling(Trust))));
    near.teacher_steps = ELDER_REACH + 1;
    CHECK(simulation.comfort(agent, near) == doctest::Approx(plain));  // too far from it
    Simulation::Situation danger;
    danger.threats = 2;
    agent.mind[Fear] = 1.0;
    danger.regard_near = 0.8;
    const double alone = simulation.comfort(agent, danger);
    danger.known_steps = FOLLOW_REACH;
    CHECK(simulation.comfort(agent, danger) > alone);  // a friend beside it with danger in sight
    danger.threats = 0;
    const double calm = simulation.comfort(agent, danger);
    danger.known_steps = -1;
    CHECK(simulation.comfort(agent, danger) == doctest::Approx(calm));  // no danger, nothing spared

    Bitzkey& doer = simulation.add_agent(agent.dna, 12, 10);
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Grazer, {{"sight", 7}}), 11, 11);
    simulation.index_cells();
    const double watching = WATCH_LEARNING;
    WATCH_LEARNING = 1.0;
    agent.known.push_back(Acquaintance{doer.identifier, -1.0, 0, 0.0, 0.0});
    stranger.known.push_back(Acquaintance{doer.identifier, 0.0, 0, 0.0, 0.9});
    simulation.deed_seen(doer, CRAFT_DEED, nullptr, SkillDeed, 1);
    CHECK((agent.crafts & 2) == 0);  // held a foe: nothing learnt from it
    CHECK((stranger.crafts & 2) == 0);  // not of its kind
    agent.known[0].regard = 0.0;  // no foe, skilled or not
    simulation.deed_seen(doer, CRAFT_DEED, nullptr, SkillDeed, 1);
    CHECK((agent.crafts & 2) != 0);
    CHECK(agent.craft_source[1] == 4);
    WATCH_LEARNING = watching;
}

TEST_CASE("an enemy's alert is refused, and a friend's is heeded from further") {
    Simulation simulation(40, 40, 0, 1);
    Bitzkey& caller = simulation.add_agent(encode_dna(Species::Grazer, {{"sight", 7}}), 10, 10);
    Bitzkey& listener = simulation.add_agent(caller.dna, 11, 10);
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator), 20, 20);
    simulation.index_cells();
    REQUIRE(simulation.herd_mate(caller, listener));
    listener.known.push_back(Acquaintance{caller.identifier, -0.9, 0});
    simulation.alert(caller, hunter);
    CHECK_FALSE(listener.alarm);
    CHECK(simulation.regard_counts[Simulation::AlertsRefused].load() == 1);
    listener.known[0].regard = 0.9;
    listener.mind[Trust] = 0;  // no trust of its own: only a friend's word reaches it
    simulation.alert(caller, hunter);
    CHECK(simulation.regard_counts[Simulation::AlertsFromFriends].load() == 1);
}

TEST_CASE("a deed colours the view of the doer's whole faction, and a stranger is taken at its colour's worth") {
    Simulation simulation(40, 40, 0, 1);
    Bitzkey& agent = simulation.add_agent(encode_dna(Species::Grazer, {{"sight", 7}}), 10, 10);
    Bitzkey& brute = simulation.add_agent(encode_dna(Species::Predator), 11, 10);
    Bitzkey& same = simulation.add_agent(brute.dna, 13, 10);
    Bitzkey& unlike = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}, {"speed", 1}}), 10, 13);
    simulation.index_cells();
    simulation.note_deed(agent, brute, BLOW_DEED);
    REQUIRE(agent.factions.size() == 1);
    CHECK(simulation.regard_for(agent, brute) == doctest::Approx(BLOW_DEED));  // its own deed counts in full
    CHECK(simulation.regard_for(agent, same) == doctest::Approx(BLOW_DEED * FACTION_SHARE));
    if (ring_difference(unlike.dna, brute.dna) > FACTION_WIDTH) CHECK(simulation.regard_for(agent, unlike) == 0.0);
    agent.known.clear();
    Senses senses = simulation.sense(agent).senses;
    CHECK(senses[SenseKnownRegard] == doctest::Approx(BLOW_DEED * FACTION_SHARE));  // sensed as one it knows would be
    CHECK(senses[SenseKnownCloseness] > 0);
    const auto loaded = WorldFile::from_bytes(WorldFile::bytes(simulation));
    REQUIRE(loaded->find_agent(agent.identifier)->factions.size() == 1);
}

TEST_CASE("hot land near water grows grass as temperate land does, and can be tilled") {
    Simulation::Settings settings{80, 80, 0, 5};
    settings.ocean = 30;
    Simulation simulation(settings);
    const auto near_water = [&](int x, int y) {
        for (int dy = -WATERED_REACH; dy <= WATERED_REACH; ++dy) {
            for (int dx = std::abs(dy) - WATERED_REACH; dx <= WATERED_REACH - std::abs(dy); ++dx) {
                if (simulation.sea(py_mod(x + dx, 80), y + dy)) return true;
            }
        }
        return false;
    };
    Cell shore{-1, -1};
    for (int y = 0; y < 80; ++y) {
        int count = 0;
        for (int x = 0; x < 80; ++x) {
            const bool expected = zone_temperature(y, 80) == Hot && !simulation.sea(x, y) && near_water(x, y);
            CHECK(simulation.watered(x, y) == expected);
            count += expected;
            if (expected) shore = {x, y};
        }
        CHECK(simulation.watered_in_row(y) == count);
    }
    REQUIRE(shore.x >= 0);
    CHECK(simulation.growth_at(shore.x, shore.y) == 1.0);
    CHECK(std::find(simulation.grass_rows().begin(), simulation.grass_rows().end(), shore.y) != simulation.grass_rows().end());
    Bitzkey& farmer = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), shore.x, shore.y);
    farmer.weapon = Club;
    simulation.dig(farmer);
    CHECK(simulation.arable_at(shore.x, shore.y));

    const int reach = WATERED_REACH;
    WATERED_REACH = 0;
    simulation.take_turn();
    CHECK_FALSE(simulation.watered(shore.x, shore.y));  // none, by the rule
    WATERED_REACH = reach;
}

TEST_CASE("fruit leaves a seed, a tool works temperate land, and a seed is planted there or eaten") {
    Simulation simulation(40, 40, 0, 1);
    int temperate = -1, hot = -1;
    for (int y = 0; y < 40; ++y) {
        if (zone_temperature(y, 40) == Medium) temperate = y;
        if (zone_temperature(y, 40) == Hot) hot = y;
    }
    REQUIRE((temperate >= 0 && hot >= 0));
    Bitzkey& farmer = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, temperate);
    Bitzkey& nomad = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, hot);
    farmer.weapon = nomad.weapon = Club;
    simulation.dig(farmer);
    simulation.dig(nomad);
    CHECK(simulation.arable_at(5, temperate));
    CHECK_FALSE(simulation.arable_at(5, hot));
    farmer.carried_fruit = 1;
    simulation.eat_carried(farmer);
    CHECK(farmer.carried_seed == 1);
    farmer.cooked_fruit = 1;
    simulation.eat_carried(farmer);
    CHECK(farmer.carried_seed == 1);  // cooking ends a seed
    const double reward = farmer.reward;
    const std::size_t seeds = simulation.seeds().size();
    simulation.plant_seed(farmer);
    CHECK(simulation.seeds().size() == seeds + 1);
    CHECK(farmer.carried_seed == 0);
    CHECK(farmer.reward == doctest::Approx(reward + PLANTING_JOY * (0.5 + farmer.feeling(Curiosity))));
    farmer.carried_seed = 1;
    farmer.hunger = 50;
    simulation.eat_seed(farmer);
    CHECK(farmer.carried_seed == 0);
    CHECK(farmer.hunger < 50);
}

TEST_CASE("possessions are demanded, fought over, stolen and given; foes, victory and cowardice move regard") {
    Simulation simulation = world();
    Bitzkey& taker = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    Bitzkey& owner = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, 5);
    Bitzkey& onlooker = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 6);
    simulation.index_cells();
    for (Bitzkey* one : {&taker, &owner, &onlooker}) {
        for (int trait : {Aggression, Dominance, Empathy, Sociability}) one->mind[trait] = 0.0;
    }
    CHECK(Simulation::foe_level(onlooker) == doctest::Approx(0.5 * FOE_LEVEL));
    onlooker.known.push_back(Acquaintance{owner.identifier, -0.9, 0});
    CHECK(simulation.is_foe(onlooker, owner));
    onlooker.known.back().regard = 0.0;

    owner.hide = static_cast<int>(Species::Predator);
    owner.weapon = StoneAxe;
    CHECK(Simulation::better_possession(taker, owner) == HidePossession);  // the hide is most better
    simulation.demand(taker, owner, HidePossession);  // asked, a meek owner yields
    CHECK(taker.hide == static_cast<int>(Species::Predator));
    CHECK(owner.hide == 0);
    CHECK(simulation.fights().empty());

    owner.mind[Aggression] = 7.0;  // a fierce owner fights
    simulation.demand(taker, owner, WeaponPossession);
    REQUIRE(simulation.fights().size() == 1);
    CHECK(simulation.regard_for(onlooker, taker) == -1.0);  // a foe while it fights to take
    taker.mind[Aggression] = 7.0;
    Simulation::Sensed sensed = simulation.sense(taker);
    simulation.act(taker, Rest, sensed.situation);  // in a fight, it fights
    CHECK(taker.last_action == "fought");
    CHECK(owner.health < owner.max_health);
    owner.health = owner.max_health / 2;  // beaten
    simulation.settle_fights();
    CHECK(simulation.fights().empty());
    CHECK(taker.weapon == StoneAxe);
    CHECK(owner.weapon == NoWeapon);
    CHECK(simulation.regard_for(onlooker, taker) > -1.0);  // the fight over

    owner.health = owner.max_health;
    owner.armour = WoodArmour;
    const double before = simulation.regard_for(onlooker, taker);
    simulation.steal(taker, owner, ArmourPossession);  // a theft seen
    CHECK(taker.armour == WoodArmour);
    const auto held = [&](const Bitzkey& by) {  // as remembered, the fight aside
        for (const Acquaintance& one : by.known) {
            if (one.identifier == taker.identifier) return one.regard;
        }
        return 0.0;
    };
    CHECK(held(onlooker) < before);
    CHECK(held(owner) < held(onlooker));  // twice the wrong to its victim
    REQUIRE(simulation.fights().size() == 1);  // and it fights to get it back
    taker.last_action = "fled";  // the thief runs
    simulation.settle_fights();
    CHECK(owner.armour == WoodArmour);  // and gives it up

    const auto seen = std::find_if(onlooker.known.begin(), onlooker.known.end(), [&](const Acquaintance& one) { return one.identifier == taker.identifier; });
    REQUIRE(seen != onlooker.known.end());
    seen->regard = REGARD_STRONG;  // a friend
    simulation.deed_seen(taker, 0.0, nullptr, CowardDeed);  // a friend's cowardice
    CHECK(seen->regard < REGARD_STRONG);

    onlooker.mind[Empathy] = 7.0;  // a kind one gives a friend what it lacks
    owner.known.push_back(Acquaintance{onlooker.identifier, 0.0, 0});
    onlooker.known.push_back(Acquaintance{owner.identifier, REGARD_STRONG, 0});
    onlooker.hide = static_cast<int>(Species::Grazer);
    simulation.give(onlooker, owner, HidePossession);
    CHECK(owner.hide == static_cast<int>(Species::Grazer));
    CHECK(onlooker.hide == 0);
    CHECK(simulation.regard_for(owner, onlooker) > 0);
}

TEST_CASE("fights are settled after every turn, however it is acted, one fight at a time for each") {
    Simulation simulation = world();
    Bitzkey& taker = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    Bitzkey& owner = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, 5);
    Bitzkey& other = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 7, 5);
    simulation.index_cells();
    simulation.begin_fight({taker.identifier, owner.identifier, WeaponPossession, false});
    simulation.begin_fight({taker.identifier, other.identifier, WeaponPossession, false});  // a second, for the same one
    simulation.begin_fight({other.identifier, owner.identifier, HidePossession, false});
    simulation.settle_fights();
    CHECK(simulation.fights().size() == 1);  // the rest dropped
    owner.health = owner.max_health / 2;
    simulation.take_turn();
    CHECK(simulation.fights().empty());  // won, and settled with the turn
}

TEST_CASE("a hide skinned from a fresh body warms, armours and draws regard; fleeing in it is a lie, and the envious want it") {
    Simulation simulation = world();
    Bitzkey& skinner = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 5);
    Bitzkey& seer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 6, 5);
    Bitzkey& body = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}, {"armour", 6}}), 5, 6);
    simulation.index_cells();
    simulation.die(body, "killed");
    skinner.weapon = StoneAxe;
    const double bare = Simulation::gear_toughness(skinner);
    simulation.skin(skinner, body);
    CHECK(skinner.hide == static_cast<int>(Species::Predator));
    CHECK(skinner.hide_armour == doctest::Approx(HIDE_ARMOUR_SHARE * body.physique(Armour)));
    CHECK(body.skinned);
    CHECK(Simulation::gear_toughness(skinner) == doctest::Approx(bare + (1 - SIZE_IN_MIGHT) * skinner.hide_armour));
    const double home = skinner.climate.temperature;
    CHECK(skinner.climate_steps(home + 1) == 0);  // a step colder, kept out
    CHECK(skinner.climate_steps(home + 2) == doctest::Approx(1.0));
    CHECK(skinner.climate_steps(home - 1) == doctest::Approx(1.0));  // but not heat
    simulation.skin(seer, body);
    CHECK(seer.hide == 0);  // already skinned
    CHECK(seer.last_action == "sought a body");

    REQUIRE(seer.known.size() == 1);  // it saw the skinning, a craft
    const double skilled = seer.known[0].regard;
    for (int turn = 0; turn < 5; ++turn) simulation.deed_seen(skinner, PRESTIGE_GARB, nullptr, GarbDeed);
    const double won = seer.known[0].regard - skilled;
    CHECK(won > 0);
    CHECK(seer.known[0].garb == doctest::Approx(won));
    simulation.index_cells();
    CHECK(skinner.regarded == doctest::Approx(skilled + won));  // as it is held, by the one who knows it
    simulation.deed_seen(skinner, 0.0, nullptr, LieDeed);  // seen fleeing a fight in it
    CHECK(seer.known[0].regard == doctest::Approx(skilled - LIE_DISREGARD * won));
    CHECK(seer.known[0].liar);
    simulation.deed_seen(skinner, PRESTIGE_GARB, nullptr, GarbDeed);
    CHECK(seer.known[0].regard == doctest::Approx(skilled - LIE_DISREGARD * won));  // a liar's garb wins nothing

    seer.mind[Dominance] = 7.0;
    seer.mind[Moodiness] = 0.0;
    skinner.mind[Dominance] = skinner.mind[Moodiness] = 0.0;
    simulation.index_cells();  // envy goes by rank within its kind: the moodier or more dominant of the two
    CHECK(simulation.envy_of(seer) == doctest::Approx(1.0));
    CHECK(simulation.envy_of(skinner) == 0);
    Simulation::Sensed sensed = simulation.sense(seer);
    CHECK(sensed.situation.envy == doctest::Approx(Simulation::possessions(skinner)));  // a hide and an axe it lacks
    Simulation::Situation calm = sensed.situation;
    calm.envy = 0.0;
    CHECK(simulation.comfort(seer, calm) - simulation.comfort(seer, sensed.situation) == doctest::Approx(ENVY_UNEASE * sensed.situation.envy));
    seer.mind[Dominance] = 0.0;
    skinner.mind[Dominance] = 7.0;
    simulation.index_cells();
    CHECK(simulation.envy_of(seer) == 0);
    CHECK(simulation.envy_of(skinner) == doctest::Approx(1.0));
}

TEST_CASE("a plant's fruit is picked unhurt while it has any, and grows back one a morning until full") {
    Simulation simulation(30, 40, 0, 1);
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"size", 7}}), 10, 20);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 11, 20);
    simulation.index_cells();
    const int full = Simulation::fruit_capacity(plant);
    CHECK(full == static_cast<int>(plant.max_health / PLANT_BITE));
    const double health = plant.health;
    for (int bite = 0; bite < full; ++bite) {
        grazer.hunger = 90;
        simulation.browse(grazer, plant);
        CHECK(grazer.last_action == "browsed");
    }
    CHECK(plant.health == health);  // unhurt
    CHECK(Simulation::fruit_left(plant) == 0);
    simulation.browse(grazer, plant);
    CHECK(grazer.last_action == "sought plants");  // none left to pick
    int mornings = 0;
    for (int turn = 0; turn < 2 * DAY_LENGTH; ++turn) {
        simulation.advance();
        if (!plant.alive()) break;
        mornings = Simulation::fruit_left(plant);
    }
    if (plant.alive() && plant.last_action == "photosynthesized") CHECK(mornings >= 1);
    CHECK(Simulation::fruit_left(plant) <= 2);  // one a morning
}

TEST_CASE("each species esteems its own culture seen among its kind, twice from family or a predator's pack") {
    Simulation simulation(30, 40, 0, 1);
    Bitzkey& sower = simulation.add_agent(encode_dna(Species::Grazer), 10, 20);
    Bitzkey& seer = simulation.add_agent(encode_dna(Species::Grazer), 11, 20);
    Bitzkey& sibling = simulation.add_agent(encode_dna(Species::Grazer), 12, 20);
    sower.mother = sibling.mother = 999;
    simulation.index_cells();
    simulation.culture_seen(seer, sower, SowingCulture);
    CHECK(simulation.regard_for(seer, sower) == doctest::Approx(CULTURE_REGARD));  // a first deed sets it
    simulation.culture_seen(seer, sower, CraftCulture);  // not a grazer's
    CHECK(simulation.regard_for(seer, sower) == doctest::Approx(CULTURE_REGARD));
    simulation.culture_seen(sibling, sower, SowingCulture);
    CHECK(simulation.regard_for(sibling, sower) == doctest::Approx(2 * CULTURE_REGARD));  // family, twice
    CHECK(Simulation::family(sower, sibling));
    CHECK(!Simulation::family(sower, seer));
}

TEST_CASE("a hunter senses its pack beside its prey, and striking alongside one gladdens it") {
    Simulation simulation(30, 40, 0, 1);
    const Dna hunter_dna = encode_dna(Species::Predator, {{"empathy", 7}, {"patience", 7}});
    Bitzkey& hunter = simulation.add_agent(hunter_dna, 10, 20);
    Bitzkey& prey = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 11, 20);
    simulation.index_cells();
    REQUIRE(simulation.sense(hunter).situation.prey == &prey);
    CHECK(simulation.sense(hunter).senses[SensePreyFlanked] == 0);
    hunter.reward = 0;
    simulation.attack(hunter, prey, "hunting");
    const double alone = hunter.reward;
    Bitzkey& mate = simulation.add_agent(hunter_dna, 12, 20);  // the pack-mate on the far side
    simulation.index_cells();
    REQUIRE(simulation.herd_mate(hunter, mate));
    CHECK(simulation.sense(hunter).senses[SensePreyFlanked] == doctest::Approx(0.5));
    prey.health = prey.max_health;
    hunter.reward = 0;
    simulation.attack(hunter, prey, "hunting");
    CHECK(hunter.reward - alone == doctest::Approx(JOINED_HUNT_JOY * (0.5 + hunter.herd_instinct())).epsilon(0.05));

    Bitzkey& rival = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}}), 15, 25);  // a rival, in a fight with it
    Bitzkey& flanker = simulation.add_agent(hunter_dna, 16, 25);
    hunter.x = 14;
    hunter.y = 25;
    simulation.die(prey, "killed");  // no prey left: only the rival
    simulation.index_cells();
    simulation.begin_fight({hunter.identifier, rival.identifier, WeaponPossession, false});
    const Simulation::Sensed sensed = simulation.sense(hunter);
    REQUIRE(sensed.situation.prey == nullptr);
    REQUIRE(sensed.situation.quarry == nullptr);
    REQUIRE(simulation.herd_mate(hunter, flanker));
    CHECK(sensed.senses[SensePreyFlanked] == doctest::Approx(0.5));  // its pack beside its opponent
}

TEST_CASE("a child is held at half how its parents are held, regard and reputation, until judged by its own deeds") {
    Simulation simulation(30, 40, 0, 1);
    const Dna dna = encode_dna(Species::Grazer);
    Bitzkey& seer = simulation.add_agent(dna, 5, 20);
    Bitzkey& mother = simulation.add_agent(dna, 6, 20);
    Bitzkey& father = simulation.add_agent(dna, 7, 20);
    Bitzkey& child = simulation.add_agent(dna, 8, 20);
    Bitzkey& stranger = simulation.add_agent(dna, 9, 20);
    child.mother = mother.identifier;
    child.father = father.identifier;
    simulation.index_cells();
    seer.known = {Acquaintance{mother.identifier, 0.8, 0, 0.2, 0.6}, Acquaintance{father.identifier, 0.4, 0, 0.0, 0.2}};
    CHECK(simulation.regard_for(seer, child) == doctest::Approx(INHERITED_REGARD * 0.6));  // half the mean of the two
    CHECK(simulation.skill_held(seer, child) == doctest::Approx(INHERITED_REGARD * 0.4));
    CHECK(simulation.regard_for(seer, stranger) == 0);
    simulation.note_deed(seer, child, 1.0, 1.0, PlainDeed);  // its own deed, from where its parents left it
    const double start = INHERITED_REGARD * 0.6;
    CHECK(simulation.regard_for(seer, child) == doctest::Approx(start + REGARD_LEARNING * (1.0 - start)));
}

TEST_CASE("lessons of restraint: a kill where prey are few, the last bite in sight, a sown seed come up, a famine seen, and a sense of plenty") {
    Simulation simulation(30, 40, 0, 1);
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator, {{"sight", 0}}), 10, 20);
    Bitzkey& victim = simulation.add_agent(encode_dna(Species::Grazer), 11, 20);
    Bitzkey& other = simulation.add_agent(encode_dna(Species::Grazer), 12, 20);
    simulation.index_cells();
    hunter.reward = 0;
    simulation.die(victim, "killed", &hunter, "hunting");  // one other left of four
    CHECK(hunter.reward == doctest::Approx(-HUNTER_RESTRAINT * 0.75));
    hunter.reward = 0;
    simulation.die(other, "killed", &hunter, "hunting");  // none left
    CHECK(hunter.reward == doctest::Approx(-HUNTER_RESTRAINT));

    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 20, 20);
    simulation.index_cells();
    grazer.reward = 0;
    simulation.last_bite(grazer);
    CHECK(grazer.reward == doctest::Approx(simulation.food_in_sight(grazer) ? 0.0 : -LAST_BITE));

    simulation.survey(grazer);
    CHECK(grazer.plenty_known == doctest::Approx(grazer.plenty));  // its first reading is its memory
    CHECK(simulation.sense(grazer).senses[SenseFoodPlenty] == doctest::Approx(grazer.plenty > 0 ? 0.5 : 0.5));
    grazer.plenty_known = 2 * grazer.plenty + 2;  // it remembers more than it finds
    const double before = grazer.plenty_known;
    const Simulation::Sensed sensed = simulation.sense(grazer);
    CHECK(sensed.senses[SenseFoodPlenty] < 0.5);
    CHECK(grazer.plenty_known < before);  // the memory follows the food

    grazer.carried_seed = 1;
    simulation.plant_seed(grazer);
    REQUIRE(simulation.seeds().size() == 1);
    CHECK(simulation.seeds()[0].sower == grazer.identifier);
}

TEST_CASE("cooked food is a growing delight, raw a letdown once it is known, cooking a pleasure, and cooking smelt afar") {
    Simulation simulation(30, 40, 0, 1);
    Bitzkey& eater = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}, {"sight", 0}}), 5, 20);
    simulation.index_cells();
    eater.hunger = 90;
    eater.reward = 0;
    const double tasty = TASTE_JOY * eater.feeling(Appetite);
    simulation.taste(eater, false);  // raw, knowing nothing better: only untasty
    CHECK(eater.reward == doctest::Approx(-tasty));
    eater.cooked_meat = 1;
    simulation.eat_carried(eater);  // a cooked portion eaten tastes
    CHECK(eater.cooked_known == doctest::Approx(COOKED_TASTE));
    eater.reward = 0;
    simulation.taste(eater, true);
    CHECK(eater.reward == doctest::Approx(tasty + COOKED_JOY * (0.5 + 2 * COOKED_TASTE)));  // tasty, and growing with the taste
    eater.reward = 0;
    simulation.taste(eater, false);
    CHECK(eater.reward == doctest::Approx(-tasty - RAW_DISTASTE * 2 * COOKED_TASTE));  // raw, untasty and now a letdown

    simulation.add_to_pile(6, 20, true);  // raw meat and cooked meat beside it: the cooked first
    simulation.add_to_pile(6, 20, CookedMeatPile);
    simulation.index_cells();
    Simulation::Situation s = simulation.sense(eater).situation;
    REQUIRE(s.eat_meat);
    simulation.eat_pile(eater, s);
    CHECK(simulation.pile_at(6, 20, CookedMeatPile) == 0);
    CHECK(simulation.pile_at(6, 20, MeatPile) == 1);
    CHECK(simulation.sense(eater).situation.cooking_smell == 0);

    const int sight = eater.sight;
    simulation.add_to_pile(5 + sight + 2, 20, CookedMeatPile);  // beyond sight, within smell
    simulation.index_cells();
    s = simulation.sense(eater).situation;
    CHECK(s.cooking_smell > 0);
    CHECK(s.cooking_smell < 1);
    REQUIRE(s.eat_meat);
    CHECK(s.eat_meat->x == 6);  // the raw in sight before cooked only smelt

    eater.carried_meat = 1;
    eater.reward = 0;
    simulation.cook(eater, Cell{5, 21});
    CHECK(eater.reward == doctest::Approx(COOK_JOY * (0.5 + 3 * COOKED_TASTE)));  // three cooked portions tasted
    CHECK(eater.cooked_meat == 1);
}

TEST_CASE("a grazer eating marrow edges toward meat until it gives in and turns omnivore, in its genes too") {
    Simulation simulation(10, 40, 0, 1);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}, {"carrion", 7}}), 2, 20);
    simulation.index_cells();
    REQUIRE(grazer.species == Species::Grazer);
    grazer.carried_stone = 1;
    const int meals = static_cast<int>(std::lround(1.0 / MARROW_LEAN));
    for (int meal = 1; meal < meals; ++meal) {
        grazer.carried_bone = 1;
        simulation.eat_marrow(grazer);
    }
    CHECK(grazer.species == Species::Grazer);
    CHECK(grazer.meat_lean == doctest::Approx((meals - 1) * MARROW_LEAN));
    grazer.carried_bone = 1;
    simulation.eat_marrow(grazer);
    CHECK(grazer.species == Species::Omnivore);
    CHECK(static_cast<Species>(decode_body(grazer.dna)[SpeciesTrait]) == Species::Omnivore);  // its young will be too
    CHECK(grazer.meat_lean == 0);
    grazer.carried_bone = 1;
    simulation.eat_marrow(grazer);
    CHECK(grazer.meat_lean == 0);  // only grazers lean
}

TEST_CASE("bone crushed with a stone, club or stone axe gives a scavenger marrow, or fertilizes worked land") {
    Simulation simulation(10, 40, 0, 1);
    int temperate = -1;
    for (int y = 0; y < 40; ++y) {
        if (zone_temperature(y, 40) == Medium) temperate = y;
    }
    REQUIRE(temperate >= 0);
    Bitzkey& scavenger = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}, {"carrion", 7}}), 2, temperate);
    Bitzkey& farmer = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}, {"carrion", 0}}), 6, temperate);
    simulation.index_cells();
    REQUIRE(scavenger.scavenger() > 0);
    REQUIRE(farmer.scavenger() == 0);
    scavenger.carried_bone = 1;
    CHECK_FALSE(simulation.can_crush(scavenger));  // nothing to crush it with
    scavenger.carried_stone = 1;
    CHECK(simulation.can_crush(scavenger));
    scavenger.hunger = 90;
    simulation.eat_marrow(scavenger);
    CHECK(scavenger.carried_bone == 0);
    CHECK(scavenger.carried_stone == 1);  // the stone kept
    CHECK(scavenger.hunger < 90);
    CHECK(scavenger.last_action == "ate marrow");
    simulation.add_to_pile(2, temperate, BonePile, 1);  // or a bone from the pile it stands on
    CHECK(simulation.can_crush(scavenger));
    simulation.eat_marrow(scavenger);
    CHECK(simulation.pile_at(2, temperate, BonePile) == 0);
    const double fed = scavenger.hunger;
    simulation.eat_marrow(scavenger);  // as when another took the last bone first this turn
    CHECK(scavenger.last_action == "found no bone");
    CHECK(scavenger.hunger == fed);
    CHECK(simulation.pile_at(2, temperate, BonePile) == 0);

    farmer.weapon = Club;
    simulation.dig(farmer);
    REQUIRE(simulation.arable_at(6, temperate));
    farmer.carried_bone = 1;
    farmer.reward = 0;
    simulation.fertilize(farmer);
    CHECK(simulation.fertilized_at(6, temperate));
    CHECK(farmer.carried_bone == 0);
    CHECK(farmer.reward == doctest::Approx(FERTILIZING_JOY * (0.5 + farmer.feeling(Curiosity))));
    farmer.carried_seed = 1;
    simulation.plant_seed(farmer);  // fed with bone
    simulation.dig(scavenger);
    scavenger.carried_seed = 1;
    simulation.plant_seed(scavenger);  // not
    const auto sown = [&](int x) {
        return std::count_if(simulation.seeds().begin(), simulation.seeds().end(), [&](const auto& seed) { return seed.x == x && seed.y == temperate; });
    };
    REQUIRE(sown(6) == 1);
    REQUIRE(sown(2) == 1);
    scavenger.health = farmer.health = 0;  // out of the way
    simulation.index_cells();
    for (int turn = 0; turn <= PLANT_GESTATION / 2; ++turn) simulation.take_turn();
    CHECK(sown(6) == 0);  // sprouted in half the time
    CHECK_FALSE(simulation.fertilized_at(6, temperate));  // the bone spent on it
    CHECK(sown(2) == 1);
}

TEST_CASE("bone rots like wood") {
    Simulation simulation(10, 10, 0, 1);
    simulation.add_to_pile(5, 5, BonePile, 2);
    simulation.index_cells();
    simulation.take_turn();
    CHECK(simulation.pile_at(5, 5, BonePile) == 2);  // not rotted yet
    const double wood_lasts = WOOD_LASTS;
    WOOD_LASTS = 0;
    simulation.index_cells();
    simulation.take_turn();
    WOOD_LASTS = wood_lasts;
    CHECK(simulation.pile_at(5, 5, BonePile) == 0);
}

TEST_CASE("stones fall where the Python version's do") {
    Simulation simulation(30, 30, 0, 1);
    int stones = 0;
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 30; ++x) stones += simulation.pile_at(x, y, StonePile);
    }
    CHECK(stones == 9);
    for (const auto& [x, y] : {std::pair{0, 17}, {2, 14}, {5, 24}, {9, 22}, {11, 0}, {20, 1}, {21, 27}, {23, 12}, {26, 26}}) CHECK(simulation.pile_at(x, y, StonePile) == 1);
}

TEST_CASE("gear doubles a blow and armour, and bone and wood break after three uses") {
    Bitzkey hitter(1, encode_dna(Species::Predator, {}), 0, 0), target(2, encode_dna(Species::Grazer, {{"armour", 0}}), 1, 0);
    const double plain = Simulation::damage(hitter, target);
    hitter.carried_stone = 1;
    CHECK(Simulation::damage(hitter, target) == doctest::Approx(1.5 * plain));
    hitter.weapon = StoneAxe;
    CHECK(Simulation::damage(hitter, target) == doctest::Approx(2 * plain));
    hitter.weapon = NoWeapon;
    hitter.carried_stone = 0;
    target.armour = WoodArmour;
    CHECK(Simulation::damage(hitter, target) < plain);

    hitter.weapon = Club;
    for (int use = 0; use < GEAR_USES - 1; ++use) Simulation::wear_weapon(hitter);
    CHECK(hitter.weapon == Club);
    Simulation::wear_weapon(hitter);
    CHECK(hitter.weapon == NoWeapon);
    hitter.weapon = StoneAxe;
    for (int use = 0; use < 10; ++use) Simulation::wear_weapon(hitter);
    CHECK(hitter.weapon == StoneAxe);
    for (int use = 0; use < GEAR_USES; ++use) Simulation::wear_armour(target);
    CHECK(target.armour == NoArmour);
}

TEST_CASE("an axe's cut fells a plant for its wood, and digging finds a stone") {
    Bitzkey digger(1, encode_dna(Species::Predator, {}), 0, 0);
    digger.weapon = Club;
    Simulation simulation = world();
    const double chance = DIG_CHANCE;
    DIG_CHANCE = 1;
    simulation.dig(digger);
    DIG_CHANCE = chance;
    CHECK(digger.carried_stone == 1);
    CHECK(digger.weapon_uses == 1);
}

TEST_CASE("two stones strike a fire and are kept; a club and a wood rubbed together light one, the wood spent") {
    Simulation simulation = world();
    Bitzkey& striker = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    simulation.index_cells();
    const auto can_light = [&] {
        const std::vector<int> options = simulation.sense(striker).options;
        return std::find(options.begin(), options.end(), static_cast<int>(LightFire)) != options.end();
    };
    striker.carried_stone = 1;
    CHECK_FALSE(can_light());
    striker.carried_stone = FIRE_STONES;
    striker.weapon = Club;
    CHECK(can_light());
    simulation.light_fire(striker);
    CHECK(simulation.fire_at(5, 5) == simulation.turn() + FIRE_TURNS);
    CHECK(striker.carried_stone == FIRE_STONES);  // kept
    CHECK(striker.weapon == Club);  // kept too: the stones struck it
}

TEST_CASE("a fire lasts a day, fed for longer, and cooks food that never spoils") {
    Simulation simulation = world();
    Bitzkey& lighter = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    lighter.weapon = Club;
    ++lighter.carried_wood;  // the second stick, rubbed against its club
    simulation.light_fire(lighter);
    CHECK(lighter.weapon == Club);  // kept, the wood spent
    CHECK(simulation.fire_at(5, 5) == simulation.turn() + FIRE_TURNS);
    lighter.carried_wood = 1;
    simulation.feed_fire(lighter, Cell{5, 5});
    CHECK(lighter.carried_wood == 0);
    CHECK(simulation.fire_at(5, 5) == simulation.turn() + 2 * FIRE_TURNS);
    lighter.carried_meat = 1;
    simulation.cook(lighter, Cell{5, 5});
    CHECK(lighter.carried_meat == 0);
    CHECK(lighter.cooked_meat == 1);
    CHECK(simulation.warmed_by_fire(lighter, lighter.climate.temperature + 1));
    CHECK_FALSE(simulation.warmed_by_fire(lighter, lighter.climate.temperature));
}

TEST_CASE("beside a fire, an animal cooks food at hand without carrying it there") {
    Simulation simulation = world();
    Bitzkey& cook = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    cook.weapon = Club;
    ++cook.carried_wood;  // the second stick, rubbed against its club
    simulation.light_fire(cook);
    simulation.add_to_pile(6, 5, false, 2);
    simulation.index_cells();
    Simulation::Sensed sensed = simulation.sense(cook);
    REQUIRE(simulation.food_to_cook(cook, sensed.situation) == Simulation::HandFood::FruitPile);
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), Cook) != sensed.options.end());
    simulation.cook(cook, sensed.situation);
    CHECK(simulation.fruit_pile_at(6, 5) == 1);
    CHECK(cook.carried_fruit == 0);
    CHECK(cook.cooked_fruit == 1);
}

TEST_CASE("food gone between choosing and acting is never taken below nothing") {
    Simulation simulation = world();
    Bitzkey& eater = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    simulation.eat_carried(eater);
    simulation.cook(eater, Cell{5, 5});
    Bitzkey& other = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, 5);
    simulation.pass_good(eater, other, MeatGood);
    CHECK(eater.carried_meat == 0);
    CHECK(eater.carried_fruit == 0);
    CHECK(eater.cooked_meat == 0);
    CHECK(other.carried_meat == 0);
}

TEST_CASE("a fire of its own or its herd's comforts whoever sees it, as a stranger's does not") {
    Simulation simulation = world();
    Bitzkey& lighter = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 20, 20);
    lighter.weapon = Club;
    ++lighter.carried_wood;  // the second stick, rubbed against its club
    simulation.light_fire(lighter);
    Simulation::Situation without, seen;
    seen.fire = Cell{5, 5};
    CHECK(simulation.comfort(lighter, seen) - simulation.comfort(lighter, without) == doctest::Approx(FIRE_COMFORT));
    CHECK(simulation.comfort(stranger, seen) == doctest::Approx(simulation.comfort(stranger, without)));
}

TEST_CASE("feeding a fire pays, a friend's fire welcomes, and a fire shared bonds") {
    Simulation simulation = world();
    Bitzkey& lighter = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    Bitzkey& friend_ = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, 5);
    simulation.index_cells();
    lighter.weapon = Club;
    lighter.carried_wood = 1;
    ++lighter.carried_wood;  // the second stick, rubbed against its club
    simulation.light_fire(lighter);
    lighter.reward = 0;
    simulation.feed_fire(lighter, Cell{5, 5});
    CHECK(lighter.reward == doctest::Approx(FIRE_FEED_JOY));
    CHECK_FALSE(simulation.fire_friendly(friend_, Cell{5, 5}));  // omnivores live alone: a stranger's fire
    for (int deed = 0; deed < 20; ++deed) simulation.note_deed(friend_, lighter, 1.0);
    CHECK(simulation.fire_friendly(friend_, Cell{5, 5}));  // a friend's welcomes
    const double before = simulation.regard_for(lighter, friend_);
    std::vector<Simulation::Sensed> plans(2);
    plans[0].situation.fire = plans[1].situation.fire = Cell{5, 5};
    simulation.share_hearths({&lighter, &friend_}, plans);
    CHECK(simulation.regard_for(lighter, friend_) > before);
}

TEST_CASE("an option never taken is hoped for as well as the average of those taken, until tried") {
    Simulation simulation = world();
    Bitzkey& animal = simulation.add_agent(encode_dna(Species::Omnivore), 5, 5);
    Values valued{};
    valued[Eat] = 20;
    valued[Rest] = 10;
    valued[FeedFire] = -3;
    const std::vector<int> options = {Eat, Rest, FeedFire};
    Values untouched = valued;
    hope_for_untried(animal, untouched, options);
    CHECK(untouched[FeedFire] == -3);  // nothing tried yet: nothing to hope by
    animal.last_tried[Eat] = animal.last_tried[Rest] = 1;
    hope_for_untried(animal, valued, options);
    CHECK(valued[FeedFire] == doctest::Approx(15));
    CHECK(valued[Eat] == 20);
    animal.last_tried[FeedFire] = 2;  // tried: its brain alone, from now on
    Values again{};
    again[Eat] = 20, again[Rest] = 10, again[FeedFire] = -3;
    hope_for_untried(animal, again, options);
    CHECK(again[FeedFire] == -3);
}

TEST_CASE("defending a herd-mate bonds the defended most, and the defender too") {
    Simulation simulation = world();
    Bitzkey& victim = simulation.add_agent(encode_dna(Species::Grazer), 5, 10);
    Bitzkey& defender = simulation.add_agent(encode_dna(Species::Grazer), 6, 10);
    Bitzkey& attacker = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}}), 7, 10);
    simulation.index_cells();
    victim.assailant = attacker.identifier;
    simulation.defend(defender, attacker, true);
    const double grateful = simulation.regard_for(victim, defender), bonded = simulation.regard_for(defender, victim);
    CHECK(bonded > 0);
    CHECK(grateful > bonded);
}

TEST_CASE("seven units make a shelter, which doubles a square and keeps wood from rotting") {
    Simulation simulation = world();
    Bitzkey& builder = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}}), 5, 5);
    simulation.index_cells();
    const int open = simulation.room_in(Cell{5, 5});
    builder.carried_stone = 4;
    builder.carried_wood = 3;
    builder.weapon = StoneAxe;
    builder.mind[Curiosity] = 7.0;
    const double thrill = BUILD_THRILL * builder.feeling(Curiosity);
    double last = 0;
    for (int unit = 0; unit < SHELTER_UNITS - 1; ++unit) {
        builder.reward = 0;
        simulation.construct(builder);
        CHECK(builder.reward == doctest::Approx(thrill * (unit + 1) / SHELTER_UNITS));  // each stage thrills more
        CHECK(builder.reward > last);
        last = builder.reward;
    }
    CHECK(simulation.room_in(Cell{5, 5}) == open - (SHELTER_UNITS - 1));
    CHECK_FALSE(simulation.shelter_at(5, 5));
    builder.reward = 0;
    simulation.construct(builder);
    CHECK(builder.reward == doctest::Approx(2 * thrill));  // finishing thrills twice the whole
    CHECK(builder.reward > last);
    CHECK(simulation.shelter_at(5, 5));
    CHECK(builder.load() == 0);
    CHECK(simulation.room_in(Cell{5, 5}) == open + CELL_SPACE);

    simulation.add_to_pile(5, 5, BonePile, 2);  // nor does a shelter let bone or wood rot
    simulation.add_to_pile(5, 5, WoodPile, 2);
    const double wood_lasts = WOOD_LASTS;
    WOOD_LASTS = 0;
    simulation.spoil_piles();  // alone: its builder might otherwise take from the pile
    WOOD_LASTS = wood_lasts;
    CHECK(simulation.pile_at(5, 5, BonePile) == 2);
    CHECK(simulation.pile_at(5, 5, WoodPile) == 2);
}

TEST_CASE("a shelter keeps the weather off, is a comfort to rest in, and one who has rested in it longs to build") {
    Simulation simulation = world();
    Bitzkey& rester = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}, {"temperature", Hot}}), 5, 3);  // out of its temperature
    Bitzkey& outside = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}, {"temperature", Hot}}), 9, 3);
    simulation.index_cells();
    rester.carried_stone = SHELTER_UNITS;
    rester.weapon = StoneAxe;
    for (int unit = 0; unit < SHELTER_UNITS; ++unit) simulation.construct(rester);
    REQUIRE(simulation.shelter_at(5, 3));
    rester.last_choice = Rest;
    Simulation::Sensed in = simulation.sense(rester);
    const Simulation::Sensed out = simulation.sense(outside);
    CHECK(out.situation.exposure > 0);
    CHECK(in.situation.exposure == 0);  // no weather felt inside
    CHECK(in.situation.spared == doctest::Approx(out.situation.exposure));  // but the relief of being spared it
    CHECK(in.situation.inside == 2);
    Simulation::Situation sheltered, bare;
    sheltered.spared = 0.5;
    sheltered.inside = 2;
    CHECK(simulation.comfort(rester, sheltered) - simulation.comfort(rester, bare) == doctest::Approx(0.5 * PROTECTION_JOY + SHELTER_COMFORT));
    simulation.act(rester, Rest, in.situation);
    CHECK(rester.shelter_known == doctest::Approx(1.0 / DAY_LENGTH));  // a turn inside, remembered

    outside.carried_stone = 1;  // one who knows a shelter is thrilled to build, whatever its curiosity
    outside.carried_wood = 1;
    outside.weapon = StoneAxe;
    outside.shelter_known = 1.0;
    outside.reward = 0;
    simulation.construct(outside);
    CHECK(outside.reward == doctest::Approx(BUILD_THRILL * (outside.feeling(Curiosity) + SHELTER_LONGING) / SHELTER_UNITS));
    simulation.construct(outside);
    const int site_turns = SITE_TURNS;
    SITE_TURNS = 0;
    simulation.spoil_piles();  // left unfinished, it falls in, and its stone and wood lie in piles
    SITE_TURNS = site_turns;
    CHECK(simulation.site_at(9, 3) == 0);
    CHECK(simulation.pile_at(9, 3, StonePile) == 1);
    CHECK(simulation.pile_at(9, 3, WoodPile) == 1);
}

TEST_CASE("a road takes three turns with a stone axe, clears its square, joins shelters, and is easy going") {
    Simulation simulation = world();
    Bitzkey& builder = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}}), 3, 5);
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {}), 5, 5);
    simulation.index_cells();
    const auto go = [&](Bitzkey& animal, int x, int y) {
        animal.x = x;
        animal.y = y;
        simulation.index_cells();
    };
    builder.weapon = StoneAxe;
    builder.carried_stone = 2 * SHELTER_UNITS;
    for (int unit = 0; unit < SHELTER_UNITS; ++unit) simulation.construct(builder);
    go(builder, 7, 5);
    for (int unit = 0; unit < SHELTER_UNITS; ++unit) simulation.construct(builder);
    REQUIRE(simulation.shelter_at(3, 5));
    REQUIRE(simulation.shelter_at(7, 5));
    builder.mind[Curiosity] = 7.0;
    const double inspiration = ROAD_THRILL * builder.feeling(Curiosity);
    const auto lay = [&](int x) {  // a whole section at x, 5; the reward of its last turn
        go(builder, x, 5);
        for (int turn = 1; turn < ROAD_TURNS; ++turn) {
            builder.reward = 0;
            simulation.build_road(builder);
            CHECK(builder.reward == doctest::Approx(inspiration * turn / ROAD_TURNS));
            CHECK_FALSE(simulation.road_at(x, 5));
        }
        builder.reward = 0;
        const int stone = builder.carried_stone;
        simulation.build_road(builder);
        CHECK(simulation.road_at(x, 5));
        CHECK(builder.carried_stone == stone - ROAD_STONES);  // laid into it
        return builder.reward;
    };
    builder.carried_stone = 3 * ROAD_STONES;
    simulation.grass_cell(5, 5) = 3;
    CHECK(lay(4) == doctest::Approx(2 * inspiration));  // beside one shelter only
    CHECK(lay(5) == doctest::Approx(2 * inspiration));
    CHECK(simulation.grass_at(5, 5) == 0);  // cleared
    CHECK_FALSE(plant.alive());
    CHECK(lay(6) == doctest::Approx(2 * inspiration + ROAD_LINK_JOY * (0.5 + builder.feeling(Curiosity))));  // the two joined
    CHECK(builder.last_action == "joined shelters by road");

    Bitzkey& walker = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}}), 4, 5);
    Bitzkey& rambler = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}}), 4, 8);
    simulation.index_cells();
    Simulation::Sensed on = simulation.sense(walker), off = simulation.sense(rambler);
    simulation.act(walker, WalkEast, on.situation);
    simulation.act(rambler, WalkEast, off.situation);
    CHECK(walker.x == 5);
    CHECK(FULL_STAMINA - walker.stamina < FULL_STAMINA - rambler.stamina);  // half a step's cost by road
    CHECK(walker.road_known == doctest::Approx(1.0 / DAY_LENGTH));  // remembered
    CHECK(rambler.road_known == 0);
    walker.road_known = 1.0;  // one who knows roads longs to lay them, whatever its curiosity
    walker.weapon = StoneAxe;
    walker.mind[Curiosity] = 0.0;
    go(walker, 9, 9);
    walker.reward = 0;
    simulation.build_road(walker);
    CHECK(walker.reward == doctest::Approx(ROAD_THRILL * (walker.feeling(Curiosity) + ROAD_LONGING) / ROAD_TURNS));
}

TEST_CASE("a craft is discovered, then mastered by trying, the tenth try sure, and a failure thrills the curious") {
    Simulation simulation = world();
    Bitzkey& crafter = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}, {"intellect", 7}}), 5, 5);
    crafter.carried_wood = 2;
    crafter.crafts = 1 << (MakeClub - MakeBoneAxe);
    crafter.craft_tries[MakeClub - MakeBoneAxe] = CRAFT_TRIES - 1;
    simulation.make_gear(crafter, MakeClub);
    CHECK(crafter.weapon == Club);
    CHECK(crafter.carried_wood == 1);
    const double chance = DISCOVERY_CHANCE;
    DISCOVERY_CHANCE = 1000;
    crafter.carried_stone = 1;
    crafter.mind[Curiosity] = 7.0;
    const double before = crafter.reward, curiosity = crafter.feeling(Curiosity);
    simulation.discover(crafter);
    DISCOVERY_CHANCE = chance;
    CHECK((crafter.crafts & (1 << (MakeStoneAxe - MakeBoneAxe))) != 0);
    CHECK(crafter.reward - before >= DISCOVERY_JOY * (0.5 + curiosity) - 1e-9);  // and finding it was a joy
}

TEST_CASE("a craft that works, a weapon's harm and armour's saving are each a joy") {
    Simulation simulation = world();
    Bitzkey& crafter = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    crafter.mind[Curiosity] = 7.0;
    crafter.carried_wood = 1;
    crafter.carried_bone = 1;
    crafter.craft_tries[MakeClub - MakeBoneAxe] = CRAFT_TRIES - 1;
    crafter.reward = 0;
    simulation.make_gear(crafter, MakeClub);
    CHECK(crafter.reward == doctest::Approx(CRAFT_JOY * 1.5 + DON_JOY * TOOL_JOY));  // half plus a full curiosity, and glad of a weapon
    crafter.craft_tries[MakeBoneArmour - MakeBoneAxe] = CRAFT_TRIES - 1;
    crafter.reward = 0;
    simulation.make_gear(crafter, MakeBoneArmour);
    CHECK(crafter.reward == doctest::Approx(CRAFT_JOY * 0.75 + DON_JOY * ARMOUR_JOY));  // armour, half, and glad of it

    Bitzkey& striker = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 8, 8);
    Bitzkey& victim = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 8, 9);
    striker.reward = victim.reward = 0;
    Simulation::gear_pays(striker, victim, 3.0);
    CHECK(striker.reward == 0);  // bare-handed
    CHECK(victim.reward == 0);
    striker.weapon = Club;
    Simulation::gear_pays(striker, victim, 3.0);
    CHECK(striker.reward == doctest::Approx(TOOL_JOY * 3.0 * (1 - 1 / WEAPON_FORCE) / victim.max_health));
    CHECK(victim.reward == 0);
    victim.armour = BoneArmour;
    Simulation::gear_pays(striker, victim, 3.0);
    CHECK(victim.reward > 0);
    const double saved = victim.reward;
    Simulation::gear_pays(striker, victim, 6.0);
    CHECK(victim.reward == doctest::Approx(3 * saved));  // the bigger the blow, the more it saved
}

TEST_CASE("leaders who follow one another hold no rank over each other; the first king of a group is crowned its emperor") {
    Simulation simulation = world(60);
    const auto leader_at = [&](int x, int y, int followers) {
        Bitzkey& one = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), x, y);
        one.age = static_cast<decltype(one.age)>(one.infancy() + 1);
        for (int index = 0; index < followers; ++index) simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), x, y + 1).standout = one.identifier;
        return &one;
    };
    Bitzkey& a = *leader_at(2, 2, LEADER_FOLLOWERS - 1);
    Bitzkey& b = *leader_at(4, 2, LEADER_FOLLOWERS - 1);
    a.standout = b.identifier;
    b.standout = a.identifier;
    simulation.recognise_leaders();
    CHECK(a.rank == 1);  // a loop: neither above the other
    CHECK(b.rank == 1);
    std::vector<Bitzkey*> chain;
    for (int link = 0; link < KING_RANK; ++link) chain.push_back(leader_at(10 + 3 * link, 20, link == 0 ? LEADER_FOLLOWERS : LEADER_FOLLOWERS - 1));
    for (int link = 0; link + 1 < KING_RANK; ++link) chain[static_cast<std::size_t>(link)]->standout = chain[static_cast<std::size_t>(link + 1)]->identifier;
    simulation.recognise_leaders();
    CHECK(chain.back()->emperor);  // reached king, crowned
    CHECK(chain.back()->rank == EMPEROR_RANK);
    CHECK(TITLES[EMPEROR_RANK] == "emperor");
    CHECK(chain[KING_RANK - 2]->rank == 5);  // a duke beneath it
    chain.back()->standout = a.identifier;  // an emperor follows none
    simulation.recognise_leaders();
    CHECK(chain.back()->follows == -1);
    CHECK(chain.back()->rank == EMPEROR_RANK);
}

TEST_CASE("a leader followed by leaders ranks above them, and its bonuses grow with its rank") {
    Simulation simulation = world(30);
    std::vector<Bitzkey*> made;
    for (int one = 0; one < 3; ++one) made.push_back(&simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5 + one, 5));
    Bitzkey& king = *made[0];
    Bitzkey& lord = *made[1];
    Bitzkey& chief = *made[2];
    for (Bitzkey* one : made) one->age = static_cast<decltype(one->age)>(one->infancy() + 1);  // grown
    for (int one = 0; one < LEADER_FOLLOWERS; ++one) simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 10 + one, 10).standout = chief.identifier;
    for (int one = 0; one < LEADER_FOLLOWERS - 1; ++one) simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 10 + one, 12).standout = lord.identifier;
    for (int one = 0; one < LEADER_FOLLOWERS - 1; ++one) simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 10 + one, 14).standout = king.identifier;
    chief.standout = lord.identifier;
    lord.standout = king.identifier;
    simulation.recognise_leaders();  // each a leader, with its followers
    CHECK(chief.rank == 1);
    simulation.recognise_leaders();  // the chief following the lord, and the lord the king
    CHECK(chief.rank == 1);
    CHECK(lord.rank == 2);
    CHECK(king.rank == 3);
    CHECK(TITLES[static_cast<std::size_t>(king.rank)] == "baron");
    CHECK(king.rank_factor() == doctest::Approx(1 + 2 * RANK_BONUS));
    CHECK(chief.rank_factor() == 1.0);
}

TEST_CASE("seeing a friend treat another well or ill colours the watcher's view of that other; a foe's deeds do not") {
    Simulation simulation = world(20);
    Bitzkey& watcher = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 5);
    Bitzkey& friend_ = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 6, 5);
    Bitzkey& foe = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 6);
    Bitzkey& other = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 6, 6);
    simulation.note_deed(watcher, friend_, 1.0, 0.8, PlainDeed, true);
    simulation.note_deed(watcher, foe, -1.0, 1.0, PlainDeed, true);
    simulation.note_deed(watcher, other, 0.0, 1.0, PlainDeed, true);  // known, held neither way
    const double before = simulation.regard_for(watcher, other);
    simulation.deed_seen(foe, 1.0, &other, PlainDeed);
    CHECK(simulation.regard_for(watcher, other) == before);  // one it holds no friend teaches it nothing
    simulation.deed_seen(friend_, 1.0, &other, PlainDeed);  // a friend's kindness to it
    const double kind = simulation.regard_for(watcher, other);
    CHECK(kind > before);
    simulation.deed_seen(friend_, -1.0, &other, HarmDeed);  // and a friend's blow
    CHECK(simulation.regard_for(watcher, other) < kind);
}

TEST_CASE("a standout is remembered out of sight until one in sight is esteemed more, and its call is known") {
    Simulation simulation = world(60);
    Bitzkey& follower = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 5);
    Bitzkey& leader = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 6, 5);
    simulation.note_deed(follower, leader, 1.0, 0.8, PlainDeed, true);
    simulation.sense(follower, true);
    REQUIRE(follower.standout == leader.identifier);
    leader.x = 40;  // far out of sight
    leader.call = FirstCall;
    leader.call_turn = simulation.turn() - 1;
    const Simulation::Sensed far = simulation.sense(follower, true);
    CHECK(follower.standout == leader.identifier);  // remembered
    CHECK(far.senses[SenseStandoutCloseness] == 0.0);
    CHECK(far.situation.standout == 0.0);  // no ease out of its presence
    Bitzkey& rival = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 6);
    simulation.note_deed(follower, rival, 1.0, 0.3, PlainDeed, true);
    simulation.sense(follower, true);
    CHECK(follower.standout == leader.identifier);  // esteemed less: no better
    follower.follows = leader.identifier;  // as if counted its leader
    for (int deed = 0; deed < 40; ++deed) simulation.note_deed(follower, rival, 1.0, 1.0, PlainDeed, true);  // many good turns
    simulation.sense(follower, true);
    CHECK(follower.standout == rival.identifier);  // proved the better
    CHECK(follower.left_leader == 3);  // its leader outshone
}

TEST_CASE("a wagon is made of two stones and three wood with an axe; hitched it follows, holds more and shelters, unhitched it keeps what it held") {
    Simulation simulation = world();
    Bitzkey& maker = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    maker.weapon = BoneAxe;
    maker.carried_stone = WAGON_STONE;
    maker.carried_wood = WAGON_WOOD;
    maker.crafts = 1 << (MakeWagon - MakeBoneAxe);
    maker.craft_tries[MakeWagon - MakeBoneAxe] = CRAFT_TRIES;
    simulation.make_gear(maker, MakeWagon);
    REQUIRE(simulation.wagons().size() == 1);
    CHECK(maker.carried_stone + maker.carried_wood == 0);
    CHECK(maker.weapon == BoneAxe);  // the axe kept
    CHECK(maker.wagon == -1);  // left for any to hitch
    const int alone = maker.capacity();
    simulation.hitch(maker, 0);
    CHECK(maker.wagon == 0);
    CHECK(maker.capacity() == alone + SHELTER_UNITS);
    CHECK(simulation.under_cover(maker));
    maker.carried_stone = alone + 2;  // more than it can carry alone
    simulation.unhitch(maker);
    CHECK(maker.wagon == -1);
    CHECK(maker.load() <= alone);
    CHECK(simulation.wagons()[0].goods[0] == 2);  // the stones it could not carry, left in the wagon
    CHECK(simulation.under_cover(maker));  // standing by it
    CHECK(Simulation::value_of(maker, WagonGood) == 0);
    simulation.hitch(maker, 0);
    CHECK(maker.carried_stone == alone + 2);  // and taken up again
    CHECK(Simulation::value_of(maker, WagonGood) == 1);
}

TEST_CASE("a whole fruit may be planted, and feeds the ground as if fertilized; a seed is planted first") {
    Simulation simulation = world(30);
    Bitzkey& farmer = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    farmer.carried_fruit = 1;
    farmer.carried_seed = 1;
    simulation.plant_seed(farmer);
    CHECK(farmer.carried_seed == 0);
    CHECK(farmer.carried_fruit == 1);  // the seed first
    CHECK_FALSE(simulation.fertilized_at(5, 5));
    farmer.x = 7;
    simulation.plant_seed(farmer);
    CHECK(farmer.carried_fruit == 0);
    CHECK(farmer.last_action == "planted a fruit");
    CHECK(simulation.fertilized_at(7, 5));
    simulation.plant_seed(farmer);
    CHECK(farmer.last_action == "had nothing to plant");
}

TEST_CASE("felling a plant gives a seed for each fruit it would bear, as many as its feller can carry") {
    Simulation simulation = world(30);
    Bitzkey& feller = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 7}}), 5, 5);
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant), 6, 5);
    feller.weapon = StoneAxe;
    simulation.index_cells();
    const int fruit = Simulation::fruit_capacity(plant);
    simulation.cut(feller, plant);
    CHECK_FALSE(plant.alive());
    CHECK(feller.carried_seed == std::min(fruit, feller.capacity()));
}

TEST_CASE("arrows: a wood at hand restores a bow to its full uses, its own or another's beside it, empty or not, a craft toward mastering the bow") {
    Simulation simulation = world(30);
    Bitzkey& archer = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 5, 5);
    archer.crafts = 1 << (MakeBow - MakeBoneAxe);
    archer.bow = true;
    archer.bow_uses = 3;
    archer.carried_wood = 1;
    simulation.index_cells();
    simulation.craft_arrows(archer, archer);
    CHECK(archer.bow_uses == BOW_USES);
    CHECK(archer.carried_wood == 0);
    CHECK(archer.craft_tries[MakeBow - MakeBoneAxe] == 1);  // a step toward mastery
    CHECK(archer.reward > 0);                               // a craft's joy
    CHECK(archer.last_action == "crafted arrows");
    Bitzkey& fletcher = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 10, 10);
    Bitzkey& friend_ = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 11, 10);
    fletcher.crafts = 1 << (MakeBow - MakeBoneAxe);
    simulation.add_to_pile(10, 11, WoodPile, 1);  // a wood at hand, not carried
    friend_.bow = true;
    friend_.bow_uses = 2;
    simulation.index_cells();
    const double regard = simulation.regard_for(friend_, fletcher);
    simulation.craft_arrows(fletcher, friend_);
    CHECK(friend_.bow_uses == BOW_USES);
    CHECK(simulation.pile_at(10, 11, WoodPile) == 0);
    CHECK(simulation.regard_for(friend_, fletcher) > regard);  // a gift
    friend_.bow_uses = 0;  // its arrows spent, the bow kept, empty
    simulation.add_to_pile(10, 11, WoodPile, 1);
    simulation.craft_arrows(fletcher, friend_);
    CHECK(friend_.bow_uses == BOW_USES);
    friend_.bow = false;  // no bow at all: nothing to make arrows for
    friend_.bow_uses = 0;
    simulation.add_to_pile(10, 11, WoodPile, 1);
    simulation.craft_arrows(fletcher, friend_);
    CHECK(friend_.bow_uses == 0);
    CHECK(fletcher.last_action == "had nothing to craft");
}

TEST_CASE("each pack-mate alongside the prey adds to the blow, up to three; each sees less out of its own hours") {
    struct Restore {
        double was = OFF_HOURS_SIGHT;
        ~Restore() { OFF_HOURS_SIGHT = was; }
    } restore;
    OFF_HOURS_SIGHT = 0.5;  // the rule as it stands, here alone
    const auto blow_with = [](int mates) {
        Simulation simulation = world(30);
        const Dna dna = encode_dna(Species::Predator, {{"empathy", 7}, {"patience", 7}, {"sight", 1}});
        Bitzkey& hunter = simulation.add_agent(dna, 10, 10);
        Bitzkey& prey = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 10, 11);
        const std::array<std::pair<int, int>, 4> beside{{{11, 11}, {9, 11}, {10, 12}, {11, 12}}};
        for (int mate = 0; mate < mates; ++mate) simulation.add_agent(dna, beside[static_cast<std::size_t>(mate)].first, beside[static_cast<std::size_t>(mate)].second);
        simulation.index_cells();
        const double health = prey.health;
        simulation.attack(hunter, prey, "hunting");
        return health - prey.health;
    };
    const double alone = blow_with(0);
    REQUIRE(alone > 0);
    CHECK(blow_with(1) == doctest::Approx(alone * (1 + PACK_BONUS)));
    CHECK(blow_with(2) == doctest::Approx(alone * (1 + 2 * PACK_BONUS)));
    CHECK(blow_with(4) == doctest::Approx(alone * (1 + PACK_MATES * PACK_BONUS)));  // three at most
    Simulation simulation = world(30);
    int dark = 0;
    while (!simulation.night(dark)) ++dark;
    int light = 0;
    while (simulation.night(light)) ++light;
    Bitzkey& day = simulation.add_agent(encode_dna(Species::Grazer, {{"sight", 4}}), dark, 5);
    REQUIRE_FALSE(day.nocturnal);
    CHECK(simulation.sight_now(day) == std::max(1, static_cast<int>(py_round(day.sight * OFF_HOURS_SIGHT))));
    CHECK(simulation.sight_now(day) < day.sight);
    day.x = light;
    CHECK(simulation.sight_now(day) == day.sight);
    day.nocturnal = true;
    CHECK(simulation.sight_now(day) < day.sight);  // the nocturnal, dazzled by day
    day.x = dark;
    CHECK(simulation.sight_now(day) == day.sight);  // and at home in the dark
}

TEST_CASE("gluttony rises with appetite and falls with kinship; a meal eaten less than half hungry is an unease by what it lacks of it, the more the fuller") {
    Simulation simulation = world();
    Bitzkey& agent = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 5);
    agent.mind[Appetite] = 7.0;
    agent.mind[Kinship] = 0.0;
    CHECK(Simulation::gluttony(agent) == doctest::Approx(1.0));
    CHECK(Simulation::gluttony_unease(agent, 0.0) == doctest::Approx(0.0));  // the wholly gluttonous feel none
    agent.mind[Appetite] = 0.0;
    agent.mind[Kinship] = 7.0;
    CHECK(Simulation::gluttony(agent) == doctest::Approx(0.0));
    CHECK(Simulation::gluttony_unease(agent, 0.0) == doctest::Approx(GLUTTONY_UNEASE));
    CHECK(Simulation::gluttony_unease(agent, GLUTTONY_HUNGER / 2) == doctest::Approx(GLUTTONY_UNEASE / 2));  // fading as it hungers
    CHECK(Simulation::gluttony_unease(agent, GLUTTONY_HUNGER) == doctest::Approx(0.0));  // hungry enough, none
    agent.mind[Appetite] = 3.5;
    agent.mind[Kinship] = 3.5;
    CHECK(Simulation::gluttony_unease(agent, 0.0) == doctest::Approx(0.5 * GLUTTONY_UNEASE));
}

TEST_CASE("crafting takes from piles at hand, work begun on the square and friends' packs; a bow or wagon can be worked on in parts") {
    Simulation simulation = world(30);
    const auto knows = [](Bitzkey& agent, int action) {
        agent.crafts |= 1 << (action - MakeBoneAxe);
        agent.craft_tries[action - MakeBoneAxe] = CRAFT_TRIES;
    };
    // From a pile beside it.
    Bitzkey& archer = simulation.add_agent(encode_dna(Species::Predator, {{"size", 1}}), 5, 5);
    archer.weapon = StoneAxe;
    archer.hide = static_cast<int>(Species::Grazer);
    knows(archer, MakeBow);
    simulation.add_to_pile(6, 5, WoodPile, BOW_WOOD);
    simulation.index_cells();
    CHECK(simulation.can_finish(archer, MakeBow));
    simulation.make_gear(archer, MakeBow);
    CHECK(archer.bow_uses == BOW_USES);
    CHECK(simulation.pile_at(6, 5, WoodPile) == 0);
    // Worked on in parts: two without the tool put in wood and a hide; one with a stone axe and nothing else finishes it.
    Bitzkey& first = simulation.add_agent(encode_dna(Species::Predator, {{"size", 1}}), 10, 10);
    Bitzkey& second = simulation.add_agent(encode_dna(Species::Predator, {{"size", 1}}), 10, 10);
    Bitzkey& finisher = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 10, 10);
    for (Bitzkey* one : {&first, &second, &finisher}) knows(*one, MakeBow);
    first.carried_wood = 1;
    second.carried_wood = 3;
    second.spare_hides.push_back({static_cast<int>(Species::Grazer), 0.1});
    finisher.weapon = StoneAxe;
    simulation.index_cells();
    CHECK_FALSE(simulation.can_finish(first, MakeBow));
    CHECK(simulation.can_add(first, MakeBow - MakeBoneAxe));
    simulation.make_gear(first, MakeBow);
    CHECK(first.carried_wood == 0);
    CHECK(first.last_action == "worked on a craft");
    CHECK(simulation.work_at(10, 10).craft == MakeBow - MakeBoneAxe);
    simulation.make_gear(second, MakeBow);
    CHECK(second.carried_wood == 2);  // only what the work lacked
    CHECK(second.spare_hides.empty());
    CHECK(simulation.work_at(10, 10).wood == BOW_WOOD);
    CHECK(simulation.work_at(10, 10).hide == 1);
    CHECK_FALSE(simulation.can_add(second, MakeBow - MakeBoneAxe));  // nothing more it needs
    second.carried_wood = 0;
    CHECK(simulation.can_finish(finisher, MakeBow));
    simulation.make_gear(finisher, MakeBow);
    CHECK(finisher.bow_uses == BOW_USES);
    CHECK(simulation.work_at(10, 10).craft == -1);  // finished
    // From a friend's pack beside it, who shares in the making.
    Bitzkey& wright = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 15, 15);
    Bitzkey& friend_ = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 16, 15);
    wright.weapon = BoneAxe;
    knows(wright, MakeWagon);
    friend_.crafts = 1 << (MakeWagon - MakeBoneAxe);
    friend_.carried_stone = WAGON_STONE;
    friend_.carried_wood = WAGON_WOOD;
    simulation.index_cells();
    CHECK_FALSE(simulation.can_finish(wright, MakeWagon));  // a stranger's pack is not at hand
    wright.known.push_back(Acquaintance{friend_.identifier, REGARD_STRONG, 0});
    CHECK(simulation.can_finish(wright, MakeWagon));
    simulation.make_gear(wright, MakeWagon);
    CHECK(simulation.wagons().size() == 1);
    CHECK(friend_.carried_stone + friend_.carried_wood == 0);
    CHECK(friend_.craft_tries[MakeWagon - MakeBoneAxe] == 1);  // a hand in it
    CHECK(friend_.reward == doctest::Approx(SHARE_JOY * Simulation::generosity(friend_)));
    // Left untouched, work falls apart into piles.
    Bitzkey& idler = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 20, 20);
    knows(idler, MakeWagon);
    idler.carried_stone = 1;
    simulation.make_gear(idler, MakeWagon);
    CHECK(simulation.work_at(20, 20).stone == 1);
    const int site_turns = SITE_TURNS;
    SITE_TURNS = 0;
    simulation.rot_works();
    SITE_TURNS = site_turns;
    CHECK(simulation.work_at(20, 20).craft == -1);
    CHECK(simulation.pile_at(20, 20, StonePile) == 1);
}

TEST_CASE("tools and hides are carried beside those in hand and worn; the best is used, and any serves for crafting") {
    Simulation simulation = world();
    Bitzkey& agent = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 5, 5);
    agent.weapon = BoneAxe;
    agent.hide = static_cast<int>(Species::Predator);
    agent.spare_tools.push_back({StoneAxe, 0});
    agent.spare_hides.push_back({static_cast<int>(Species::Grazer), 0.1});
    simulation.arrange(agent);
    CHECK(agent.weapon == StoneAxe);  // the better to hand
    REQUIRE(agent.spare_tools.size() == 1);
    CHECK(agent.spare_tools[0].kind == BoneAxe);
    CHECK(agent.hide == static_cast<int>(Species::Predator));  // the finer worn
    CHECK(agent.load() == 2);
    agent.armour = BoneArmour;
    agent.weapon = NoWeapon;
    agent.spare_tools = {{BoneAxe, 0}};
    simulation.arrange(agent);
    CHECK(agent.weapon == NoWeapon);  // no bone axe in hand with armour on, but carried
    CHECK(agent.has_tool(BoneAxe));
    agent.armour = NoArmour;
    agent.spare_tools = {{StoneAxe, 0}};
    agent.weapon = Club;
    agent.carried_wood = BOW_WOOD;
    agent.crafts = 1 << (MakeBow - MakeBoneAxe);
    agent.craft_tries[MakeBow - MakeBoneAxe] = CRAFT_TRIES;
    simulation.arrange(agent);
    simulation.make_gear(agent, MakeBow);  // the stone axe carried serves, and the poorer hide is spent
    CHECK(agent.bow_uses == BOW_USES);
    CHECK(agent.spare_hides.empty());
    CHECK(agent.hide == static_cast<int>(Species::Predator));
    for (int many = 0; many < 20; ++many) agent.spare_hides.push_back({static_cast<int>(Species::Grazer), 0.1});
    simulation.arrange(agent);
    CHECK(agent.load() <= agent.capacity());  // what it cannot carry falls
}

TEST_CASE("holding all a known thing needs is a joy by its parts; only what it knows counts") {
    Simulation simulation = world();
    Bitzkey& crafter = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 5, 5);
    crafter.carried_wood = 2;
    crafter.carried_stone = 2;
    crafter.hide = 1;
    crafter.weapon = StoneAxe;
    CHECK(Simulation::ready_sets(crafter) == 0);  // it knows nothing to make of them
    crafter.crafts = 1 << (MakeClub - MakeBoneAxe) | 1 << (MakeBow - MakeBoneAxe);
    const int sets = Simulation::ready_sets(crafter);
    CHECK(sets == (1 << (MakeClub - MakeBoneAxe) | 1 << (MakeBow - MakeBoneAxe)));
    const double club = Simulation::ready_worth(crafter, 1 << (MakeClub - MakeBoneAxe));
    CHECK(club > 0);
    CHECK(Simulation::ready_worth(crafter, 1 << (MakeBow - MakeBoneAxe)) == doctest::Approx(4 * club));  // a bow, four parts
    crafter.road_known = 0.5;
    CHECK((Simulation::ready_sets(crafter) >> (CRAFT_COUNT + 1) & 1) == 1);  // a road section's stones, with a stone axe
    CHECK((Simulation::ready_sets(crafter) >> CRAFT_COUNT & 1) == 0);  // not a shelter's worth, nor known
}

TEST_CASE("a call answered by a deed good for both is a joy to each, of its own gain; one ill for either is answered unrewarded") {
    Simulation simulation = world();
    Bitzkey& caller = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    Bitzkey& listener = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 6, 5);
    listener.answer_to = caller.identifier;
    listener.answer_call = FirstCall + 1;
    caller.reward = listener.reward = 0;
    simulation.answered(listener, caller, 1.0, 0.0);
    CHECK(listener.answer_to == caller.identifier);  // the caller untouched: still to answer
    CHECK(listener.reward == 0);
    simulation.answered(listener, caller, 1.0, 2.0);
    CHECK(listener.reward == doctest::Approx(COMMUNICATION_JOY * 1.0));
    CHECK(caller.reward == doctest::Approx(COMMUNICATION_JOY * 2.0));
    CHECK(listener.answer_to == -1);  // answered
    CHECK(simulation.call_payoffs()[static_cast<std::size_t>(Species::Omnivore)][1][2] == 1);
    listener.answer_to = caller.identifier;
    caller.reward = listener.reward = 0;
    simulation.answered(listener, caller, 1.0, -2.0);
    CHECK(listener.reward == 0);  // ill for the caller
    CHECK(caller.reward == 0);
    CHECK(listener.answer_to == -1);
    listener.answer_to = caller.identifier;
    simulation.answered(listener, caller, -1.0, 2.0);
    CHECK(caller.reward == 0);  // ill for the listener
}

TEST_CASE("seeing gear pay off stirs a desire for it, friend or foe, and desire weighs in envy") {
    Simulation simulation = world();
    Bitzkey& striker = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    Bitzkey& struck = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 6);
    Bitzkey& watcher = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 6, 5);
    striker.weapon = StoneAxe;
    struck.armour = BoneArmour;
    simulation.gear_witnessed(striker, struck);
    CHECK(watcher.desire[WeaponDesire] == doctest::Approx(GEAR_DESIRE));
    CHECK(watcher.desire[ArmourDesire] == doctest::Approx(GEAR_DESIRE));
    CHECK(watcher.desire[BowDesire] == 0);
    CHECK(striker.desire[WeaponDesire] == 0);  // not stirred by its own blow
    CHECK(Simulation::possessions(striker, watcher) == doctest::Approx(Simulation::possessions(striker) + GEAR_DESIRE));
    for (int sight = 0; sight < 100; ++sight) simulation.gear_witnessed(striker, struck);
    CHECK(watcher.desire[WeaponDesire] == doctest::Approx(1.0));  // up to 1
}

TEST_CASE("a parent teaches its infant once, crafts and all, and an elder may teach it again") {
    Simulation simulation = world();
    Bitzkey& parent = simulation.add_agent(encode_dna(Species::Omnivore, {{"intellect", 4}}), 5, 5);
    Bitzkey& child = simulation.add_agent(encode_dna(Species::Omnivore, {{"intellect", 4}}), 5, 6);
    child.mother = parent.identifier;
    parent.crafts = 1 << 2;
    parent.craft_tries[2] = CRAFT_TRIES;
    child.age = 0;
    const double youth = FEEBLE_YOUTH;
    FEEBLE_YOUTH = 0.5;
    child.derive();
    REQUIRE(child.infant());
    CHECK(simulation.willing_student(parent, child));
    simulation.teach(parent, child);
    simulation.teach_all();
    CHECK(child.infant_taught);
    CHECK(child.taught_by == -1);  // its second chance still to come
    CHECK(child.crafts == parent.crafts);
    CHECK(child.craft_tries[2] == CRAFT_TRIES);
    CHECK(child.craft_source[2] == 2);  // from its parent, and nothing else
    CHECK(child.craft_source[0] == 0);
    CHECK(child.craft_chain[2] == 1);
    CHECK(std::any_of(simulation.diary().begin(), simulation.diary().end(), [](const Simulation::Milestone& entry) { return entry.event == "taught_by_parent" && entry.craft == "make_club"; }));
    const std::size_t written = simulation.diary().size();
    simulation.note_first("taught_by_parent", child.species, child.identifier, "make_club");
    CHECK(simulation.diary().size() == written);  // once only
    CHECK_FALSE(simulation.willing_student(parent, child));
    FEEBLE_YOUTH = youth;
}

TEST_CASE("a defeat earns a mark of the victor, kept for life, esteemed by those who see it and dreaded by its foes") {
    Simulation simulation = world();
    Bitzkey& victor = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 5, 5);
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, 5);
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 7, 5);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 7);
    Bitzkey& friend_ = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 4, 5);
    Bitzkey& foe = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 4);
    simulation.index_cells();
    CHECK(victor.mark == NoMark);
    omnivore.health = 0;
    simulation.die(omnivore, "killed", &victor);
    CHECK(victor.mark == WarriorMark);  // the warrior's, for an omnivore
    predator.health = 0;
    simulation.die(predator, "killed", &victor);
    CHECK(victor.mark == PrestigeMark);  // prestige, for a predator
    grazer.health = 0;
    simulation.die(grazer, "killed", &victor);
    CHECK(victor.mark == PrestigeMark);  // kept: a lesser defeat takes nothing away
    CHECK(Simulation::mark_worth(victor.mark) == PRESTIGE_MARK);

    foe.known.push_back(Acquaintance{victor.identifier, -0.9, 0});
    REQUIRE(simulation.is_foe(foe, victor));
    simulation.deed_seen(victor, Simulation::mark_worth(victor.mark), nullptr, MarkDeed);
    CHECK(simulation.regard_for(friend_, victor) > 0.0);  // esteemed
    const auto held = [&](const Bitzkey& by) {
        return *std::find_if(by.known.begin(), by.known.end(), [&](const Acquaintance& one) { return one.identifier == victor.identifier; });
    };
    CHECK(held(foe).regard == doctest::Approx(-0.9));  // a foe esteems it no more
    CHECK(held(foe).danger > 0.0);  // but holds it the more dangerous
    CHECK(foe.reward < 0.0);  // and dreads it
    simulation.deed_seen(victor, 0.0, nullptr, CowardDeed);
    CHECK(victor.mark == PrestigeMark);  // never lost
}

// Lands rules for a test: small groups claim, and the old values come back after.
struct SmallLands {
    int size = LAND_SIZE, hold = LAND_HOLD, patch = LAND_PATCH, kills = WAR_KILLS;
    double fade = WAR_FADE;
    SmallLands() {
        LAND_SIZE = 3;
        LAND_HOLD = 2;
        LAND_PATCH = 8;
        WAR_KILLS = 2;
    }
    ~SmallLands() {
        LAND_SIZE = size;
        LAND_HOLD = hold;
        LAND_PATCH = patch;
        WAR_KILLS = kills;
        WAR_FADE = fade;
    }
};

TEST_CASE("a colour group large enough claims the land it stands in, defends it, and goes to war over contested land") {
    SmallLands lands;
    Simulation simulation = world(24);
    const Dna red = encode_dna(Species::Omnivore, {{"size", 3}});
    const Dna blue = flipped(red, std::uint64_t{0xFFFF0000} << 32);
    REQUIRE(colour_difference(body_word(red), body_word(blue)) > FACTION_WIDTH);
    std::vector<Bitzkey*> reds, blues;
    for (const auto& [x, y] : {std::pair{2, 2}, std::pair{3, 2}, std::pair{2, 3}}) reds.push_back(&simulation.add_agent(red, x, y));
    Bitzkey& outsider = simulation.add_agent(blue, 3, 3);
    simulation.index_cells();
    simulation.count_lands();
    const int realm = reds[0]->realm;
    CHECK(realm >= 0);
    CHECK(reds[1]->realm == realm);
    CHECK(outsider.realm != realm);  // another colour, another group
    CHECK(simulation.realms().at(realm).members == 3);
    REQUIRE(simulation.claims_at(2, 2) != nullptr);
    CHECK(Simulation::claims(*simulation.claims_at(7, 7), realm));  // the whole square, to its edge
    CHECK_FALSE(Simulation::claims(*simulation.claims_at(8, 8), realm));
    CHECK(simulation.realms().at(realm).patches.size() == 1);
    CHECK(simulation.in_own_land(*reds[0]));
    CHECK(simulation.trespassing(outsider));

    // In its land: harder to harm, steadier, its herd the more, and at ease
    CHECK(simulation.land_guard(*reds[0], outsider) == doctest::Approx(1 - LAND_DEFENCE));
    CHECK(simulation.land_guard(*reds[0], *reds[1]) == 1.0);  // its own group are no outsiders
    CHECK(simulation.nerve(*reds[0], &outsider) == doctest::Approx(1 - LAND_RALLY));
    CHECK(simulation.land_herd(*reds[0]) == doctest::Approx(1 + LAND_HERD));
    CHECK(simulation.nerve(outsider, reds[0]) == 1.0);
    Simulation::Sensed sensed = simulation.sense(*reds[0]);
    CHECK(sensed.senses[SenseOwnLand] == 1.0);
    CHECK(sensed.senses[SenseTrespasser] > 0.0);
    CHECK(sensed.situation.land == 1.0);
    Simulation::Sensed away = simulation.sense(outsider);
    CHECK(away.senses[SenseForeignLand] == 1.0);

    // A trespasser is thought the worse of each turn it stays, the more each turn
    simulation.trespass(outsider);
    const double first = simulation.regard_for(*reds[0], outsider);
    CHECK(first < 0.0);
    CHECK(outsider.trespass == 1);
    for (int turn = 0; turn < 20; ++turn) simulation.trespass(outsider);
    CHECK(simulation.regard_for(*reds[0], outsider) < first);
    CHECK(simulation.is_foe(*reds[0], outsider));  // until it is a foe
    CHECK(simulation.drives_out(*reds[0], outsider));  // to be struck down in the land
    CHECK_FALSE(simulation.drives_out(*reds[0], *reds[1]));

    // A second group standing there in strength contests it: war
    for (const auto& [x, y] : {std::pair{4, 3}, std::pair{3, 4}}) blues.push_back(&simulation.add_agent(blue, x, y));
    simulation.index_cells();
    simulation.count_lands();
    const int enemy = outsider.realm;
    REQUIRE(simulation.realms().at(enemy).members == 3);
    const LandClaims& contested = *simulation.claims_at(2, 2);
    CHECK(Simulation::claims(contested, realm));
    CHECK(Simulation::claims(contested, enemy));
    REQUIRE(simulation.wars().size() == 1);
    CHECK(simulation.war_standing(realm) == 0);  // too few deaths yet to tell
    blues[0]->health = 0;
    simulation.die(*blues[0], "killed", reds[0]);
    blues[1]->health = 0;
    simulation.die(*blues[1], "killed", reds[1]);
    CHECK(simulation.war_standing(realm) == 1);  // clearly winning
    CHECK(simulation.war_standing(enemy) == -1);  // clearly losing
    CHECK(simulation.nerve(outsider, &outsider) > 1.0);  // and shaken by it, where it can't rally
    CHECK(reds[0]->mark == WarriorMark);  // each a victor too
    // The loser gives up its claim, fading over the counts, though one of it stands there still
    for (int count = 0; count < LAND_MEMORY; ++count) simulation.count_lands();
    CHECK_FALSE(Simulation::claims(*simulation.claims_at(2, 2), enemy));
    CHECK(simulation.wars().empty());  // and the war is over
    CHECK(Simulation::claims(*simulation.claims_at(2, 2), realm));
}

TEST_CASE("a beaten group may not take back the winner's land until the war's kills have faded, and then it is over") {
    SmallLands lands;
    WAR_FADE = 0.0;
    Simulation simulation = world(24);
    const Dna red = encode_dna(Species::Omnivore, {{"size", 3}});
    const Dna blue = flipped(red, std::uint64_t{0xFFFF0000} << 32);
    std::vector<Bitzkey*> reds, blues;
    for (const auto& [x, y] : {std::pair{2, 2}, std::pair{3, 2}, std::pair{2, 3}}) reds.push_back(&simulation.add_agent(red, x, y));
    for (const auto& [x, y] : {std::pair{4, 3}, std::pair{3, 4}, std::pair{4, 4}, std::pair{5, 4}, std::pair{4, 5}}) blues.push_back(&simulation.add_agent(blue, x, y));
    simulation.index_cells();
    simulation.count_lands();
    simulation.count_lands();  // the one claims, then the other contests it
    const int realm = reds[0]->realm, enemy = blues[0]->realm;
    REQUIRE(simulation.wars().size() == 1);
    for (int fallen = 0; fallen < 2; ++fallen) {
        blues[static_cast<std::size_t>(fallen)]->health = 0;
        simulation.die(*blues[static_cast<std::size_t>(fallen)], "killed", reds[0]);
    }
    REQUIRE(simulation.war_standing(enemy) == -1);
    for (int count = 0; count < 2 * LAND_MEMORY; ++count) simulation.count_lands();
    CHECK(Simulation::claims(*simulation.claims_at(2, 2), realm));
    CHECK_FALSE(Simulation::claims(*simulation.claims_at(2, 2), enemy));  // still there in strength, but beaten
    CHECK(simulation.wars().size() == 1);  // and the war remembered
    WAR_FADE = 1.0;
    simulation.count_lands();
    CHECK(simulation.wars().empty());  // its kills forgotten, the war is over
    simulation.count_lands();
    CHECK(Simulation::claims(*simulation.claims_at(2, 2), enemy));  // and the land may be fought for again
}

TEST_CASE("a grazer may attack a foe within reach, at half the base force, and makes no meal of it") {
    Simulation simulation = world();
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 5);
    Bitzkey& foe = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 6, 5);
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 6);
    Bitzkey& hunter = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 4, 5);
    simulation.index_cells();
    CHECK_FALSE(simulation.preys_on(grazer, foe));
    grazer.known.push_back(Acquaintance{foe.identifier, -0.9, 0});
    REQUIRE(simulation.is_foe(grazer, foe));
    CHECK(simulation.preys_on(grazer, foe));  // a foe, it may strike
    CHECK_FALSE(simulation.preys_on(grazer, stranger));  // not one it holds nothing against
    CHECK(simulation.sense(grazer).situation.prey == &foe);
    // Weak: half the base force of a hunter its like
    Bitzkey twin = grazer;
    twin.species = Species::Omnivore;
    const double weapons = 1 + 0.75 * Simulation::armed(grazer), alike = 1 + 0.75 * Simulation::armed(twin);
    CHECK(Simulation::damage(grazer, foe) / weapons == doctest::Approx(GRAZER_FORCE * Simulation::damage(twin, foe) / alike));
    CHECK(Simulation::damage(grazer, hunter, true) > Simulation::damage(grazer, hunter));  // but not in defence
    const double hunger = grazer.hunger, health = foe.health;
    simulation.attack(grazer, foe);
    CHECK(foe.health < health);
    CHECK(grazer.hunger >= hunger);  // no meal of it
}

TEST_CASE("lands, groups and wars are saved with the world and loaded back") {
    SmallLands lands;
    Simulation simulation = world(24);
    const Dna red = encode_dna(Species::Omnivore, {{"size", 3}});
    for (const auto& [x, y] : {std::pair{2, 2}, std::pair{3, 2}, std::pair{2, 3}}) simulation.add_agent(red, x, y);
    simulation.index_cells();
    simulation.count_lands();
    simulation.living().front()->mark = PrestigeMark;
    const auto loaded = WorldFile::from_bytes(WorldFile::bytes(simulation));
    CHECK(loaded->realms().size() == simulation.realms().size());
    CHECK(loaded->land_patch() == simulation.land_patch());
    CHECK(loaded->wars() == simulation.wars());
    CHECK(Simulation::claims(*loaded->claims_at(2, 2), loaded->living().front()->realm));
    CHECK(loaded->realms().begin()->second.patches == simulation.realms().begin()->second.patches);
    CHECK(loaded->living().front()->mark == PrestigeMark);
}

TEST_CASE("an animal senses its standout, is at ease near it, and is glad to be held a friend") {
    Simulation simulation = world();
    Bitzkey& follower = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    Bitzkey& esteemed = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, 6);
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 4, 5);
    simulation.index_cells();
    Simulation::Sensed alone = simulation.sense(follower);
    CHECK(alone.senses[SenseStandoutEsteem] == 0.0);  // none esteemed above none
    follower.known.push_back(Acquaintance{esteemed.identifier, 0.8, 0});
    follower.known.push_back(Acquaintance{stranger.identifier, 0.2, 0});
    esteemed.known.push_back(Acquaintance{follower.identifier, 0.9, 0});
    Simulation::Sensed sensed = simulation.sense(follower);
    CHECK(sensed.senses[SenseStandoutEsteem] == doctest::Approx(0.8));  // the one it esteems most
    CHECK(sensed.senses[SenseStandoutCloseness] > 0.0);
    CHECK(sensed.senses[SenseStandoutEast] > 0.0);
    CHECK(sensed.senses[SenseStandoutSouth] > 0.0);
    CHECK(sensed.situation.admired == doctest::Approx(1.0 / 3));  // one about it holds it a friend
    CHECK(simulation.comfort(follower, sensed.situation) > simulation.comfort(follower, alone.situation));
}

TEST_CASE("an offer is weighed by both: a deal that seems a gain to each is made, one that doesn't refused") {
    Simulation simulation = world();
    Bitzkey& trader = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    Bitzkey& owner = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, 5);
    simulation.index_cells();
    for (Bitzkey* one : {&trader, &owner}) {
        for (int trait : {Aggression, Dominance, Empathy, Sociability}) one->mind[trait] = 0.0;
        one->mind[Intellect] = 7.0;  // the sharpest misjudge nothing
    }
    trader.weapon = StoneAxe;     // worth 0.5 to the meek trader
    owner.armour = WoodArmour;    // worth 1 to either
    owner.mind[Aggression] = 7.0; // the axe worth 1.5 to the fierce owner
    const Simulation::Sensed sensed = simulation.sense(trader);
    CHECK(sensed.situation.trade_with == &owner);
    CHECK(sensed.situation.wanted == ArmourPossession);
    CHECK(sensed.situation.offered == WeaponPossession);
    simulation.offer(trader, owner, ArmourPossession, WeaponPossession);
    CHECK(trader.last_action == "traded");
    CHECK((trader.armour == WoodArmour && trader.weapon == NoWeapon));
    CHECK((owner.weapon == StoneAxe && owner.armour == NoArmour));
    CHECK(simulation.regard_for(owner, trader) > 0.0);  // a kindness both ways
    CHECK(simulation.regard_for(trader, owner) > 0.0);
    CHECK((trader.reward >= TRADE_JOY && owner.reward >= TRADE_JOY));  // trade joy, to both

    owner.mind[Aggression] = 0.0;  // the axe now worth 0.5 to the owner, less than the armour it would give up
    owner.armour = WoodArmour;
    owner.weapon = NoWeapon;
    trader.weapon = StoneAxe;
    trader.armour = NoArmour;
    simulation.offer(trader, owner, ArmourPossession, WeaponPossession);
    CHECK(trader.last_action == "was refused");
    CHECK((trader.weapon == StoneAxe && owner.armour == WoodArmour));

    owner.hunger = 100.0;  // food: a hungry owner takes a portion for its armour
    trader.carried_meat = 1;
    simulation.offer(trader, owner, ArmourPossession, MeatGood);
    CHECK(trader.last_action == "traded");
    CHECK((owner.carried_meat == 1 && trader.carried_meat == 0 && trader.armour == WoodArmour));

    // A dull owner may be talked into a deal that truly costs it: no joy, and it thinks the worse of the trader.
    CHECK(Simulation::trade_bias(7, 2) == Simulation::trade_bias(7, 2));  // its misjudgement, the same each time
    CHECK(Simulation::trade_bias(7, 2) != Simulation::trade_bias(7, 3));
    CHECK(Simulation::trade_bias(7, 2) == doctest::Approx(0.9222179767494598).epsilon(1e-15));  // as the Python version's
    owner.mind[Intellect] = 0.0;
    owner.hunger = 0.0;
    owner.carried_meat = 0;
    owner.armour = WoodArmour;
    trader.armour = NoArmour;
    trader.carried_fruit = 1;  // a little fruit, worth less than armour to a sated owner
    const double regard = simulation.regard_for(owner, trader), joy = owner.reward;
    const double misjudge = TRADE_MISJUDGE;
    TRADE_MISJUDGE = 2.0;  // badly, so it is soon talked into it
    bool cheated = false;
    for (int tries = 0; tries < 200 && !cheated; ++tries) {  // until its misjudgement lets it through
        simulation.offer(trader, owner, ArmourPossession, FruitGood);
        cheated = trader.last_action == "traded";
    }
    TRADE_MISJUDGE = misjudge;
    REQUIRE(cheated);
    CHECK(owner.reward == joy);
    CHECK(simulation.regard_for(owner, trader) < regard);
}

TEST_CASE("a leader is recognised by those who hold it their standout, and its followers defer, rally and defend it") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Omnivore, {{"size", 3}});
    Bitzkey& head = simulation.add_agent(dna, 10, 10);
    std::vector<Bitzkey*> band;
    for (int index = 0; index < LEADER_FOLLOWERS; ++index) band.push_back(&simulation.add_agent(dna, 11 + index % 3, 9 + index / 3));
    simulation.index_cells();
    for (Bitzkey* one : band) one->standout = head.identifier;
    simulation.recognise_leaders();
    CHECK(head.leader);
    CHECK(head.followers == LEADER_FOLLOWERS);
    for (const Bitzkey* one : band) CHECK(Simulation::follows(*one, head));
    CHECK(simulation.leader_in_sight(*band[0]));
    CHECK(simulation.nerve(*band[0], nullptr) == doctest::Approx(1 - LEADER_RALLY));

    Bitzkey& owner = *band[0];
    owner.mind[Aggression] = owner.mind[Dominance] = 7.0;  // fierce, it would fight anyone else
    owner.weapon = StoneAxe;
    simulation.demand(head, owner, WeaponPossession);
    CHECK(head.last_action == "was yielded");
    CHECK(head.weapon == StoneAxe);

    for (int index = 0; index + 2 < LEADER_FOLLOWERS; ++index) band[static_cast<std::size_t>(index)]->standout = -1;
    simulation.recognise_leaders();  // fewer than half as many: no longer
    CHECK(head.followers == 2);
    CHECK_FALSE(head.leader);
    for (Bitzkey* one : band) one->standout = head.identifier;
    simulation.recognise_leaders();
    CHECK(head.leader);

    Bitzkey& foe = simulation.add_agent(encode_dna(Species::Predator, {{"size", 3}}), 10, 9);
    simulation.index_cells();
    simulation.struck(head, foe);  // its followers close by come to its defence
    CHECK(band[0]->herd_assailant == std::optional<int>(foe.identifier));
}

TEST_CASE("a guest is welcome in a friend's land and contests none of it; in a foe's land an animal is drawn to stay") {
    SmallLands lands;
    Simulation simulation = world(24);
    const Dna red = encode_dna(Species::Omnivore, {{"size", 3}});
    const Dna blue = flipped(red, std::uint64_t{0xFFFF0000} << 32);
    std::vector<Bitzkey*> hosts, guests;
    for (const auto& [x, y] : {std::pair{2, 2}, std::pair{3, 2}, std::pair{2, 3}}) hosts.push_back(&simulation.add_agent(red, x, y));
    for (const auto& [x, y] : {std::pair{18, 18}, std::pair{19, 18}, std::pair{18, 19}}) guests.push_back(&simulation.add_agent(blue, x, y));
    simulation.index_cells();
    simulation.count_lands();  // each group claims where it stands
    const int host = hosts[0]->realm, guest = guests[0]->realm;
    REQUIRE((host >= 0 && guest >= 0 && host != guest));
    for (std::size_t index = 0; index < guests.size(); ++index) {  // all of them into the host's land
        guests[index]->x = 4 + static_cast<int>(index);
        guests[index]->y = 4;
    }
    simulation.index_cells();
    CHECK(simulation.trespassing(*guests[0]));
    for (Bitzkey* one : hosts) one->factions.push_back(Faction{body_word(blue), 0.9, 0});  // the hosts think well of the guests' colour
    simulation.count_welcomes();
    CHECK(simulation.welcomed(host, guest));
    CHECK_FALSE(simulation.trespassing(*guests[0]));
    simulation.count_lands();
    const LandClaims* land = simulation.claims_at(2, 2);
    REQUIRE(land);
    CHECK(Simulation::claims(*land, host));
    CHECK_FALSE(Simulation::claims(*land, guest));  // guests contest none of their host's land

    guests[0]->factions.push_back(Faction{body_word(red), -0.8, 0});  // one dislikes its hosts, and uninvited it would stay
    for (Bitzkey* one : hosts) one->factions.back().regard = 0.0;
    simulation.count_welcomes();
    CHECK_FALSE(simulation.welcomed(host, guest));
    const Simulation::Sensed sensed = simulation.sense(*guests[0]);
    CHECK(sensed.senses[SenseLandOwnerRegard] == doctest::Approx(-0.8));
    CHECK(sensed.situation.ambition == doctest::Approx(Simulation::ambition(*guests[0], -0.8, false)));  // by its temperament
    CHECK(sensed.situation.ambition != 0.0);
    simulation.count_lands();
    CHECK(Simulation::claims(*simulation.claims_at(2, 2), guest));  // unwelcome, they contest it
}

TEST_CASE("a call that helps pays its caller: joy for one of its kind that got away or, hungry, ate, and regard from it") {
    const double payoff = CALL_PAYOFF, heeded = HEEDED_REGARD;  // none by default
    CALL_PAYOFF = 0.2;
    HEEDED_REGARD = 0.05;
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"size", 1}});
    Bitzkey& caller = simulation.add_agent(dna, 5, 5);
    Bitzkey& listener = simulation.add_agent(dna, 6, 5);
    simulation.index_cells();
    simulation.advance();  // a turn begun
    const int turn = simulation.turn();
    listener.heard_from = caller.identifier;  // it heard the caller's second call last time, a hunter in sight
    listener.heard_call = FirstCall + 1;
    listener.heard_turn = turn - 1;
    listener.heard_danger = true;
    listener.struck_turn = -1;  // and was not struck since
    const double joy = caller.reward;
    std::map<int, int> paid;
    simulation.pay_callers(listener, Simulation::Situation{}, paid);
    CHECK(caller.reward > joy);
    CHECK(simulation.regard_for(listener, caller) > 0.0);
    CHECK(simulation.call_payoffs()[static_cast<std::size_t>(Species::Grazer)][1][0] == 1);
    CHECK(listener.heard_from == -1);  // it heard nothing this time

    listener.heard_from = caller.identifier;  // struck after hearing: no payoff
    listener.heard_turn = turn - 1;
    listener.heard_danger = true;
    listener.struck_turn = turn;
    const double before = caller.reward;
    simulation.pay_callers(listener, Simulation::Situation{}, paid);
    CHECK(caller.reward == before);
    CALL_PAYOFF = payoff;
    HEEDED_REGARD = heeded;
}

TEST_CASE("the hard would impose themselves on a foe's land, the soft are at ease as a friend's guests") {
    Simulation simulation = world();
    const Bitzkey& hard = simulation.add_agent(encode_dna(Species::Grazer, {{"aggression", 7}, {"dominance", 7}, {"fear", 0}, {"empathy", 0}, {"sociability", 0}, {"tolerance", 0}}), 1, 1);
    const Bitzkey& soft = simulation.add_agent(encode_dna(Species::Grazer, {{"aggression", 0}, {"dominance", 0}, {"fear", 7}, {"empathy", 7}, {"sociability", 7}, {"tolerance", 7}}), 3, 3);
    CHECK(Simulation::ambition(hard, -1.0, false) == doctest::Approx(2.0));  // a foe's land: drawn to it
    CHECK(Simulation::ambition(soft, -1.0, false) == doctest::Approx(-1.0));  // uneasy there
    CHECK(Simulation::ambition(soft, 1.0, true) == doctest::Approx(2.0));  // at ease as a friend's guest
    CHECK(Simulation::ambition(hard, 1.0, true) == doctest::Approx(0.0));
    CHECK(Simulation::ambition(soft, 1.0, false) == doctest::Approx(-1.0));  // uninvited, uneasy all the same
}

TEST_CASE("a bow is made of two wood and a worn hide, the stone axe kept; it shoots its uses, and hits teach, please and are respected") {
    Simulation simulation = world();
    Bitzkey& archer = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}, {"patience", 7}, {"intellect", 7}, {"fear", 0}}), 5, 5);
    archer.weapon = StoneAxe;
    archer.carried_wood = 3;
    archer.hide = 2;
    archer.crafts = 1 << (MakeBow - MakeBoneAxe);
    archer.craft_tries[MakeBow - MakeBoneAxe] = CRAFT_TRIES - 1;
    simulation.make_gear(archer, MakeBow);
    CHECK(archer.bow_uses == BOW_USES);
    CHECK(archer.carried_wood == 1);  // two spent
    CHECK(archer.hide == 0);          // and the hide
    CHECK(archer.weapon == StoneAxe); // the axe kept
    CHECK(Simulation::hit_chance(archer, 1) > Simulation::hit_chance(archer, archer.sight));  // harder at the edge of sight

    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"size", 3}}), 7, 5);
    const Bitzkey& onlooker = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, 6);
    simulation.index_cells();
    const double health = plant.health, skill = simulation.skill_held(onlooker, archer);
    for (int shot = 0; shot < BOW_USES; ++shot) simulation.shoot(archer, plant, true);
    CHECK(archer.bow_uses == 0);  // spent
    CHECK(archer.archery == doctest::Approx(1 - std::pow(1 - ARCHERY_LEARNING, BOW_USES)));  // every shot taught
    CHECK(plant.health == health);  // practice harms nothing
    CHECK(simulation.skill_held(onlooker, archer) > skill);  // its hits were seen

    Bitzkey& prey = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 1}}), 5, 7);
    simulation.index_cells();
    archer.archery = 1.0;
    const double before = prey.health;
    for (int shot = 0; shot < 20 && prey.health == before; ++shot) {
        archer.bow_uses = 1;
        simulation.shoot(archer, prey, false);
    }
    CHECK(prey.health < before);  // struck from two steps off
}

TEST_CASE("a tutor gives one of its kind beside it a lesson: a recipe it lacks, then archery, then a try nearer mastery") {
    Simulation simulation = world();
    Bitzkey& tutor = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 5, 5);
    Bitzkey& pupil = simulation.add_agent(encode_dna(Species::Omnivore, {{"size", 3}}), 6, 5);
    simulation.index_cells();
    const int club = MakeClub - MakeBoneAxe;
    const auto lesson = [&](const Bitzkey& to) { return Simulation::lesson_for(tutor, to, simulation.turn()); };
    CHECK(lesson(pupil) == -1);  // nothing worth teaching
    tutor.crafts = 1 << club;
    tutor.archery = 0.5;
    CHECK(lesson(pupil) == CRAFT_COUNT);  // a club it hasn't mastered isn't worth teaching; archery is
    tutor.craft_tries[static_cast<std::size_t>(club)] = CRAFT_TRIES;  // mastered
    CHECK(lesson(pupil) == club);
    const double hunger = tutor.hunger, joy = tutor.reward;
    const double practice = TUTOR_PRACTICE;
    TUTOR_PRACTICE = 1.0;  // a lesson as good as a try, to count
    simulation.tutor(tutor, pupil, club);
    CHECK((pupil.crafts >> club & 1) != 0);  // the recipe, now known
    CHECK(pupil.craft_source[static_cast<std::size_t>(club)] == 5);
    CHECK(pupil.craft_tries[static_cast<std::size_t>(club)] == 1);
    CHECK(tutor.hunger == doctest::Approx(hunger + CALL_COST));  // as dear as a call
    CHECK(tutor.reward > joy);
    CHECK(simulation.regard_for(pupil, tutor) > 0.0);
    CHECK(lesson(pupil) == -1);  // a lesson a day at most
    pupil.tutored_turn = simulation.turn() - DAY_LENGTH;
    CHECK(lesson(pupil) == CRAFT_COUNT);  // a day on, archery, as it shoots the better
    TUTOR_PRACTICE = 0.5;
    simulation.tutor(tutor, pupil, CRAFT_COUNT);
    CHECK(pupil.archery == doctest::Approx(0.5 * ARCHERY_LEARNING));  // half a shot's lesson
    TUTOR_PRACTICE = 0.0;
    pupil.archery = 0.5;
    pupil.tutored_turn = -1;
    CHECK(lesson(pupil) == club);  // then a try nearer mastery, which a worthless lesson never gives
    simulation.tutor(tutor, pupil, club);
    CHECK(pupil.craft_tries[static_cast<std::size_t>(club)] == 1);
    TUTOR_PRACTICE = practice;
    Bitzkey& stranger = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 5, 6);
    CHECK(lesson(stranger) == -1);  // not of its kind
}

