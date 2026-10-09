// The simulation's rules, ported from the Python tests' key cases.

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <numeric>
#include <set>

#include "core/PyFormat.hpp"
#include "core/Simulation.hpp"

using namespace bitzkey;

namespace {

Simulation world(int size = 12, int seed = 3) {
    Simulation simulation(Simulation::Settings{size, size, 0, static_cast<std::uint64_t>(seed)});
    simulation.clear_grass();
    return simulation;
}

}  // namespace

TEST_CASE("a seeded world replays exactly, and seeking repeats the recorded run") {
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
    std::vector<double> hunger_at_25;
    first.seek(25);
    for (const auto& agent : first.agents()) hunger_at_25.push_back(agent->hunger);
    first.seek(40);
    first.seek(25);
    std::vector<double> again;
    for (const auto& agent : first.agents()) again.push_back(agent->hunger);
    CHECK(hunger_at_25 == again);
    first.seek(40);
    CHECK(first.agents().size() == second.agents().size());
    CHECK(first.agents().back()->hunger == second.agents().back()->hunger);
}

TEST_CASE("random populations spawn in groups of three to five sharing a genome") {
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
    CHECK(small.lifespan == doctest::Approx(360));   // a year
    CHECK(large.lifespan == doctest::Approx(3600));  // ten years
    // five years halfway between, rising steadily either side
    CHECK(simulation.add_agent(encode_dna(Species::Grazer, {{"size", 3}}), 4, 4).lifespan == doctest::Approx((1 + 4 * 6.0 / 7) * 360));
    CHECK(simulation.add_agent(encode_dna(Species::Grazer, {{"size", 4}}), 5, 5).lifespan == doctest::Approx((5 + 5 * 1.0 / 7) * 360));
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
    bool further = false;
    for (std::size_t index = 2; index < simulation.agents().size(); ++index) {
        const Bitzkey& child = *simulation.agents()[index];
        const int reach = std::max(std::abs(child.x - first.x), std::abs(child.y - first.y));
        CHECK((reach >= 1 && reach <= BIRTH_REACH && child.generation == 1));
        further = further || reach == 2;
    }
    CHECK(further);  // more than the eight cells beside her
    simulation.die(first, "killed");
    CLIMATE_DAMAGE = damage;
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

TEST_CASE("animals shun mates whose ring or dot differ too far, and tolerance widens that") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"size", 2}, {"fear", 3}});
    Bitzkey& animal = simulation.add_agent(dna, 1, 1);
    const Bitzkey& close = simulation.add_agent(dna ^ (0x08ULL << 56) ^ 0x10, 2, 2);
    const Bitzkey& ringed = simulation.add_agent(dna ^ (0x40ULL << 56), 3, 3);
    Bitzkey& strange = simulation.add_agent(dna ^ (0xFFULL << 56) ^ 0xFF, 4, 4);
    CHECK(Simulation::compatible(animal, close));
    CHECK_FALSE(Simulation::compatible(animal, ringed));
    animal.mind[Tolerance] = 7;
    CHECK(Simulation::willing(animal, strange));
    CHECK_FALSE(Simulation::compatible(animal, strange));  // but the stranger shuns it
    strange.mind[Tolerance] = 2;
    CHECK(Simulation::compatible(animal, strange));
}

TEST_CASE("predators hunt other predators, of any kind, only past their reluctance") {
    Simulation simulation = world();
    Bitzkey& gentle = simulation.add_agent(encode_dna(Species::Predator, {{"empathy", 7}, {"tolerance", 7}, {"patience", 7}}), 1, 1);
    const Bitzkey& kin = simulation.add_agent(gentle.dna ^ (0xFFULL << 56), 2, 1);       // 25% apart: its kind
    const Bitzkey& stranger = simulation.add_agent(gentle.dna ^ (0xFFFFULL << 48), 3, 1);  // 50% apart
    CHECK(Simulation::same_kind(gentle, kin));
    CHECK_FALSE(Simulation::same_kind(gentle, stranger));
    gentle.hunger = 30;
    CHECK_FALSE(Simulation::is_prey(gentle, stranger));
    CHECK_FALSE(Simulation::is_prey(gentle, kin));
    gentle.own_rivalry = 0.8;  // reluctance worn down to a fifth
    CHECK(Simulation::is_prey(gentle, kin));
    CHECK(Simulation::is_prey(gentle, stranger));
}

TEST_CASE("predators breed with their kind, and omnivores and predators prey on each other") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Predator, {{"tolerance", 0}});
    const Bitzkey& first = simulation.add_agent(dna, 1, 1);
    const Bitzkey& kin = simulation.add_agent(dna ^ (0xFFULL << 56), 2, 2);
    const Bitzkey& stranger = simulation.add_agent(dna ^ (0xFFFFULL << 48), 3, 3);
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
    Bitzkey& callous = simulation.add_agent(encode_dna(Species::Predator, {{"empathy", 0}}), 15, 15);
    for (const int x : {11, 12, 13, 17, 18, 19}) simulation.add_agent(encode_dna(Species::Omnivore), x, 15);
    simulation.index_cells();
    CHECK(simulation.territory(callous) == std::pair{0.0, 0.0});  // omnivores are prey, not rivals
    for (const int x : {14, 16, 15, 13, 17, 12}) simulation.add_agent(encode_dna(Species::Predator), x, 14);
    simulation.index_cells();
    CHECK(simulation.territory(callous) == std::pair{1.0, 1.0});
}

TEST_CASE("the herd action moves toward the middle of the herd, fear setting how close it keeps") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"empathy", 7}, {"patience", 7}});
    Bitzkey& fearful = simulation.add_agent(encode_dna(Species::Grazer, {{"empathy", 7}, {"patience", 7}, {"fear", 7}}), 2, 5);
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
    CHECK(simulation.food_supply(omnivore) == doctest::Approx((0.5 * 0.25 * 6 + 0.75 * 2 + 2 * 2) / (1 + 0.5)));
    CHECK(simulation.food_supply(predator) == doctest::Approx(3 * 2));
    simulation.add_agent(encode_dna(Species::Predator) ^ (0xFFULL << 56), 18, 16);  // a rival, not food
    simulation.index_cells();
    CHECK(simulation.food_supply(predator) == doctest::Approx(3 * 2 / 2.0));
}

TEST_CASE("scarce food stops breeding, and plenty allows full litters") {
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
    CHECK(simulation.prey_switching(predator) == std::pair{false, true});  // omnivores near, and no grazers
}

TEST_CASE("a blow that exhausts the striker kills it, and no body is left without a recorded death") {
    Simulation simulation = world();
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}, {"strength", 7}, {"weapons", 7}}), 5, 5);
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}}), 5, 6);
    simulation.index_cells();
    grazer.hunger = 99.9;
    simulation.defend(grazer, predator);
    CHECK_FALSE(grazer.alive());
    CHECK(grazer.died_turn == simulation.turn());
    CHECK(grazer.last_action == "exhausted");

    Simulation busy(Simulation::Settings{60, 60, 90, 1});
    for (int turn = 0; turn < 300; ++turn) busy.advance();
    for (const auto& agent : busy.agents()) CHECK((agent->alive() || agent->died_turn.has_value()));
}

TEST_CASE("grazers don't scavenge") {
    Simulation simulation = world();
    Bitzkey& corpse = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 5, 5);
    corpse.health = 0;
    simulation.die(corpse, "wounded");
    corpse.died_turn = -2;  // no longer a fresh kill
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"carrion", 7}, {"sight", 4}}), 5, 6);
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore, {{"sight", 4}}), 5, 4);
    simulation.index_cells();
    Simulation::Sensed sensed = simulation.sense(grazer);
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), Scavenge) == sensed.options.end());
    CHECK(sensed.senses[SenseCarrionCloseness] == 0.0);
    sensed = simulation.sense(omnivore);
    CHECK(std::find(sensed.options.begin(), sensed.options.end(), Scavenge) != sensed.options.end());
}

TEST_CASE("a body is a fresh kill through its death turn and the next, then carrion") {
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
        simulation.take_turn();  // fresh through this turn too; it starts to rot at its end
        CHECK(corpse.meat == doctest::Approx(40.0 - 40.0 / 29));
        simulation.take_turn();
        Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"sight", 4}}), 5, 8);
        simulation.index_cells();
        const Simulation::Sensed sensed = simulation.sense(predator);
        CHECK((sensed.situation.kill == nullptr && sensed.situation.carrion == &corpse));
    }
}

TEST_CASE("a hunter eats a fresh kill whole for three times the kill meal") {
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
    const double meal = PLANT_MEAL * grazer.browsing * UPROOT_MEALS / grazer.stomach;
    simulation.act(grazer, Uproot, sensed.situation);
    CHECK(grazer.hunger == doctest::Approx(85 - meal));
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

TEST_CASE("the latest turns are kept in memory and reached at once exactly") {
    const double saved = CLIMATE_DAMAGE;
    CLIMATE_DAMAGE = 0;  // a world that lasts
    Simulation simulation(Simulation::Settings{20, 20, 30, 4});
    const auto state = [&simulation] {
        std::string text;
        for (const auto& agent : simulation.agents()) {
            char line[160];
            std::snprintf(line, sizeof line, "%d %d %d %.17g %.17g %s %zu|", agent->identifier, agent->x, agent->y, agent->hunger,
                          agent->health, std::string(agent->last_action).c_str(), agent->trail.size());
            text += line;
            if (agent->brain) {
                Senses senses{};
                senses.fill(0.4);
                char value[32];
                std::snprintf(value, sizeof value, "%.17g|", agent->brain->values(senses)[3]);
                text += value;
            }
        }
        return text + std::to_string(simulation.grass_tufts());
    };
    std::vector<std::string> recorded = {state()};
    for (int turn = 1; turn <= 60; ++turn) {
        simulation.advance();
        recorded.push_back(state());
    }
    std::vector<int> every(61);
    std::iota(every.begin(), every.end(), 0);
    CHECK(simulation.history_turns() == every);
    for (const int target : {37, 3, 0, 59, 60, 17, 18, 60}) {  // every one at once, with no replaying
        simulation.seek(target);
        CHECK(simulation.turn() == target);
        CHECK(state() == recorded[static_cast<std::size_t>(target)]);
    }
    simulation.seek(57);
    simulation.advance();  // stepping on through the recorded run brings back each turn
    CHECK(state() == recorded[58]);
    simulation.seek(60);
    simulation.advance();  // and from the latest, the world carries on
    CHECK(simulation.frontier() == 61);
    simulation.close();
    CHECK(simulation.history_turns().empty());
    CLIMATE_DAMAGE = saved;
}

TEST_CASE("the history keeps only the latest turns and fits its memory") {
    const double saved = CLIMATE_DAMAGE;
    const int turns = HISTORY_TURNS;
    const double memory = HISTORY_MEMORY;
    CLIMATE_DAMAGE = 0;  // a world that lasts
    HISTORY_TURNS = 10;
    Simulation simulation(Simulation::Settings{20, 20, 30, 4});
    for (int turn = 0; turn < 40; ++turn) simulation.advance();
    std::vector<int> latest(10);
    std::iota(latest.begin(), latest.end(), 31);
    CHECK(simulation.history_turns() == latest);
    simulation.seek(0);  // as far back as it reaches
    CHECK(simulation.turn() == 31);
    CHECK_FALSE(simulation.rewind());
    simulation.seek(40);
    HISTORY_TURNS = HISTORY_ALL;  // "all": every turn from here on
    for (int turn = 0; turn < 20; ++turn) simulation.advance();
    CHECK(simulation.history_turns().size() == 30);
    simulation.seek(60);
    HISTORY_MEMORY = 1e-9;  // far too little: only the newest is kept
    simulation.advance();
    CHECK(simulation.history_turns() == std::vector<int>{61});
    std::uintmax_t brains = 0;
    for (const auto& agent : simulation.agents()) brains += AGENT_BYTES + agent->brain_bytes();
    CHECK(simulation.history_bytes() == brains);
    HISTORY_TURNS = turns;
    HISTORY_MEMORY = memory;
    CLIMATE_DAMAGE = saved;
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
    grazer.hunger = 100.5;      // starved by something that recorded nothing
    const int deaths = simulation.deaths();
    simulation.advance();
    CHECK((grazer.last_action == "starved" && grazer.died_turn == 1 && simulation.deaths() == deaths + 1));
    CHECK(grazer.meat == 0);  // a starved body leaves nothing to eat
    CHECK(grazer.brain == nullptr);
    CHECK(herd_mate.seen_failure > 0);  // its herd saw it fail

    const double seed_cost = SEED_COST;
    SEED_COST = 100;
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"fertility", 7}}), 20, 20);
    simulation.index_cells();
    for (int attempt = 0; attempt < 400 && plant.alive(); ++attempt) {
        plant.hunger = 30;
        simulation.seed_plant(plant);
    }
    SEED_COST = seed_cost;
    CHECK((plant.last_action == "seeded to death" && plant.died_turn == simulation.turn()));
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
