#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "core/Brain.hpp"

using namespace bitzkey;

TEST_CASE("learning moves the chosen action's value toward its target") {
    PyRandom rng(7);
    Brain brain = Brain::random(rng);
    Senses senses{};
    senses.fill(0.4);
    const double before = brain.values(senses)[Rest];
    for (int round = 0; round < 50; ++round) brain.learn(senses, Rest, 3.0, 0.05);
    CHECK(std::abs(brain.values(senses)[Rest] - 3.0) < std::abs(before - 3.0));
}

TEST_CASE("a new brain is reluctant to set off in any direction") {
    PyRandom first(7), second(7);
    const double saved = HEADING_RELUCTANCE;
    const Brain reluctant = Brain::random(first);
    HEADING_RELUCTANCE = 0.0;
    const Brain eager = Brain::random(second);
    HEADING_RELUCTANCE = saved;
    for (int action = 0; action < ACTION_COUNT; ++action) {
        const bool heading = action >= GoNorth && action <= WalkWest;
        CHECK(reluctant.output_biases[action] == doctest::Approx(eager.output_biases[action] - (heading ? saved : 0.0)));
    }
}

TEST_CASE("a first traced lesson is an ordinary lesson") {
    PyRandom first(7), second(7);
    Brain traced = Brain::random(first, 3), plain = Brain::random(second, 3);
    Senses senses{};
    senses.fill(0.3);
    traced.learn_traced(senses, 2, 1.5, 0.05, 0.9);
    plain.learn(senses, 2, 1.5, 0.05);
    senses.fill(0.6);
    for (int action = 0; action < ACTION_COUNT; ++action) {
        CHECK(traced.values(senses)[action] == doctest::Approx(plain.values(senses)[action]).epsilon(1e-12));
    }
}

TEST_CASE("a traced lesson reaches back to the choice before") {
    Senses earlier{}, later{};
    earlier.fill(0.2);
    later.fill(0.7);
    const double saved = TRACE_DECAY;
    std::vector<double> changes;
    for (const double decay : {0.0, 0.9}) {
        TRACE_DECAY = decay;
        PyRandom rng(7);
        Brain brain = Brain::random(rng);
        const double before = brain.values(earlier)[1];
        brain.learn_traced(earlier, 1, before, 0.05, 1.0);  // no surprise yet
        brain.learn_traced(later, 4, brain.values(later)[4] - 1, 0.05, 1.0);
        changes.push_back(brain.values(earlier)[1] - before);
    }
    TRACE_DECAY = saved;
    // Without a trace, the earlier choice moves only a little, either way, through the layers both
    // share; with one, it shares the blame.
    CHECK(changes[1] < changes[0] - 0.1);
}

TEST_CASE("a copy keeps its own traces, and an export none") {
    PyRandom rng(7);
    Brain brain = Brain::random(rng);
    Senses senses{};
    senses.fill(0.5);
    brain.learn_traced(senses, 0, 5.0, 0.1, 0.9);
    Brain duplicate = brain;
    brain.learn_traced(senses, 0, 5.0, 0.1, 0.9);
    duplicate.learn_traced(senses, 0, 5.0, 0.1, 0.9);
    CHECK(brain == duplicate);  // the same traces, kept apart
    CHECK_FALSE(Brain::from_record(nlohmann::json::parse(brain.to_record().dump())).traces.has_value());
    CHECK_FALSE(brain.resized(4).traces.has_value());  // traces belong to one life
}

TEST_CASE("a brain is twice as deep as the inborn intellect, and at least two layers") {
    std::vector<int> depths;
    for (int intellect = 0; intellect < 8; ++intellect) depths.push_back(depth_for(intellect));
    CHECK(depths == std::vector<int>{2, 2, 4, 6, 8, 10, 12, 14});
    PyRandom rng(7);
    const Brain brain = Brain::random(rng, 6);
    CHECK((brain.depth() == 6 && brain.layers[0].reads == INPUT_SIZE && brain.layers[5].reads == HIDDEN_SIZE));
    CHECK(Brain::random(rng).depth() == MIN_DEPTH);
}

TEST_CASE("there is an input for each ring, dot and social trait, and none for which actions are open") {
    for (const auto action : ACTIONS) CHECK(sense_index("open_" + std::string(action)) == -1);  // which actions are open is not an input
    for (const auto trait : TEMPERAMENT) CHECK((sense_index("inborn_" + std::string(trait)) >= 0 && sense_index(trait) >= 0));
    for (const auto trait : BODY_TRAITS) CHECK(sense_index(trait) >= 0);
    Senses senses{};
    gene_senses(encode_dna(Species::Predator, {{"temperature", 2}, {"cold_tolerance", 1}, {"size", 7}, {"intellect", 7}, {"trust", 7}}), senses);
    CHECK((senses[SenseBody] == 1.0 && senses[SenseBody + Size] == 1.0 && senses[SenseInborn + Intellect] == 1.0 &&
           senses[SenseInborn + Trust] == 1.0));
    CHECK((senses[SenseHomeTemperature] == doctest::Approx(2.0 / 3) && senses[SenseColdTolerance] == 1.0 &&
           senses[SenseHeatTolerance] == 0.0));
}

TEST_CASE("a child takes each hidden neuron whole from one parent") {
    PyRandom rng(3);
    const Brain mother = Brain::random(rng, 4), father = Brain::random(rng, 4);
    const Brain child = Brain::inherit(mother, father, rng, 4, 0.0);
    for (int index = 0; index < 4; ++index) {
        const Layer& layer = child.layers[static_cast<std::size_t>(index)];
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
            const Layer& from_mother = mother.layers[static_cast<std::size_t>(index)];
            const bool mothers = std::equal(layer.row(neuron), layer.row(neuron) + layer.reads, from_mother.row(neuron));
            const Layer& donor = (mothers ? mother : father).layers[static_cast<std::size_t>(index)];
            CHECK(std::equal(layer.row(neuron), layer.row(neuron) + layer.reads, donor.row(neuron)));
            CHECK(layer.biases[neuron] == donor.biases[neuron]);
        }
    }
    for (int action = 0; action < ACTION_COUNT; ++action) {
        const Brain& donor = child.output_weights[action] == mother.output_weights[action] ? mother : father;
        CHECK(child.output_weights[action] == donor.output_weights[action]);
        CHECK(child.output_biases[action] == donor.output_biases[action]);
    }
}

TEST_CASE("a child deeper than a parent keeps the deeper parent's layers, then passes input on") {
    PyRandom rng(3);
    const Brain shallow = Brain::random(rng, 2), deep = Brain::random(rng, 4);
    const Brain child = Brain::inherit(shallow, deep, rng, 6, 0.0);
    CHECK(child.depth() == 6);
    for (const int index : {2, 3}) CHECK(child.layers[static_cast<std::size_t>(index)] == deep.layers[static_cast<std::size_t>(index)]);
    for (const int index : {4, 5}) CHECK(child.layers[static_cast<std::size_t>(index)] == Layer::identity());
    CHECK(Brain::inherit(deep, deep, rng, 2, 0.0).depth() == 2);
    const Brain deeper = deep.resized(6), shallower = deep.resized(2);
    CHECK((deeper.depth() == 6 && shallower.depth() == 2));
    CHECK((deeper.layers[3] == deep.layers[3] && deeper.layers[5] == Layer::identity() && shallower.layers[1] == deep.layers[1]));
}

TEST_CASE("odds favour the better-valued action and sum to one") {
    Values values{};
    values[Eat] = 1.0;
    values[Rest] = 0.5;
    const std::vector<int> options = {Eat, Rest};
    const auto odds = choice_odds(values, options);
    CHECK(odds[0].second > odds[1].second);
    CHECK(odds[0].second + odds[1].second == doctest::Approx(1.0));
}

TEST_CASE("an exported brain loads back, matching senses and actions by name") {
    PyRandom rng(7);
    const Brain brain = Brain::random(rng);
    const nlohmann::json record = nlohmann::json::parse(brain.to_record().dump());
    const Brain loaded = Brain::from_record(record);
    Senses senses{};
    senses.fill(0.4);
    for (int action = 0; action < ACTION_COUNT; ++action) {
        CHECK(loaded.values(senses)[action] == doctest::Approx(brain.values(senses)[action]).epsilon(1e-4));
    }
    nlohmann::json renamed = record;
    renamed["senses"][0] = "smell";
    for (auto& row : renamed["layers"][0]["weights"]) row[0] = 9.0;
    const Brain partial = Brain::from_record(renamed);
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) CHECK(partial.layers[0].row(neuron)[SenseHunger] == 0.0);
    nlohmann::json broken = record;
    broken["layers"][0]["biases"] = "lots";
    CHECK_THROWS_AS(Brain::from_record(broken), std::invalid_argument);
    broken = record;
    broken["layers"] = nlohmann::json::array();
    CHECK_THROWS_AS(Brain::from_record(broken), std::invalid_argument);

    nlohmann::json single = record;  // saved before brains grew deeper: one hidden layer
    single["hidden_weights"] = record["layers"][0]["weights"];
    single["hidden_biases"] = record["layers"][0]["biases"];
    single.erase("layers");
    const Brain old = Brain::from_record(single);
    CHECK(old.depth() == 1);
    CHECK(old.layers[0].weights[5] == doctest::Approx(brain.layers[0].weights[5]).epsilon(1e-5));
}

TEST_CASE("hidden layers are 34 neurons wide, and a narrower saved brain loads widened, valuing alike") {
    CHECK((HIDDEN_SIZE == 34 && INPUT_SIZE == 87));
    std::mt19937 engine(5);
    std::uniform_real_distribution<double> uniform(-0.5, 0.5);
    constexpr int narrow = 12;
    std::vector<std::vector<std::vector<double>>> weights(2);
    std::vector<std::vector<double>> biases(2);
    for (int index = 0; index < 2; ++index) {
        weights[index].assign(narrow, std::vector<double>(index == 0 ? INPUT_SIZE : narrow));
        for (auto& row : weights[index]) for (double& weight : row) weight = uniform(engine);
        biases[index].resize(narrow);
        for (double& bias : biases[index]) bias = uniform(engine);
    }
    std::vector<std::vector<double>> output(ACTION_COUNT, std::vector<double>(narrow));
    for (auto& row : output) for (double& weight : row) weight = uniform(engine);
    std::vector<double> output_biases(ACTION_COUNT);
    for (double& bias : output_biases) bias = uniform(engine);
    nlohmann::json record = {{"senses", SENSES}, {"actions", ACTIONS}, {"output_weights", output}, {"output_biases", output_biases}};
    record["layers"] = nlohmann::json::array();
    for (int index = 0; index < 2; ++index) record["layers"].push_back({{"weights", weights[index]}, {"biases", biases[index]}});
    const Brain loaded = Brain::from_record(record);
    CHECK((loaded.layers[0].reads == INPUT_SIZE && loaded.layers[1].reads == HIDDEN_SIZE));
    Senses senses{};
    for (double& sense : senses) sense = uniform(engine) + 0.5;
    std::vector<double> activation(senses.begin(), senses.end());
    for (int index = 0; index < 2; ++index) {
        std::vector<double> next(narrow);
        for (int neuron = 0; neuron < narrow; ++neuron) {
            double sum = 0.0;
            for (std::size_t input = 0; input < activation.size(); ++input) sum += weights[index][neuron][input] * activation[input];
            next[neuron] = std::tanh(sum + biases[index][neuron]);
        }
        activation = next;
    }
    const Values values = loaded.values(senses);
    for (int action = 0; action < ACTION_COUNT; ++action) {
        double expected = output_biases[action];
        for (int neuron = 0; neuron < narrow; ++neuron) expected += output[action][neuron] * activation[neuron];
        CHECK(values[action] == doctest::Approx(expected).epsilon(1e-12));
    }
    record["layers"][1]["biases"] = std::vector<double>(5, 0.0);
    CHECK_THROWS_AS(Brain::from_record(record), std::invalid_argument);
}
