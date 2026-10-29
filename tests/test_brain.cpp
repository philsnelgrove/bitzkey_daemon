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
        CHECK(reluctant.output_biases[action] == doctest::Approx(eager.output_biases[action] - (heading ? saved * VALUE_SCALE : 0.0)));  // held in hundredths
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
    CHECK_FALSE(brain.resized(4, BASE_WIDTH).traces.has_value());  // traces belong to one life
}

TEST_CASE("intellect deepens a brain to four layers, then widens it") {
    std::vector<int> depths, widths;
    for (int intellect = 0; intellect < 8; ++intellect) {
        depths.push_back(depth_for(intellect));
        widths.push_back(width_for(intellect));
    }
    CHECK(depths == std::vector<int>{1, 1, 2, 3, 4, 4, 4, 4});
    CHECK(widths == std::vector<int>{38, 38, 38, 38, 38, 43, 48, 53});
    CHECK((BASE_WIDTH == 38 && MAX_WIDTH == 53));
    PyRandom rng(7);
    const Brain brain = Brain::random(rng, 4, 53);
    CHECK((brain.depth() == 4 && brain.width() == 53 && brain.layers[0].reads == INPUT_SIZE && brain.layers[3].reads == 53 &&
           brain.output_weights[0].size() == 53));
    const Brain plain = Brain::random(rng);
    CHECK((plain.depth() == 1 && plain.width() == BASE_WIDTH));
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
    const Brain child = Brain::inherit(mother, father, rng, 4, BASE_WIDTH, 0.0);
    for (int index = 0; index < 4; ++index) {
        const Layer& layer = child.layers[static_cast<std::size_t>(index)];
        for (int neuron = 0; neuron < BASE_WIDTH; ++neuron) {
            const Layer& from_mother = mother.layers[static_cast<std::size_t>(index)];
            const bool mothers = std::equal(layer.row(neuron), layer.row(neuron) + layer.reads, from_mother.row(neuron));
            const Layer& donor = (mothers ? mother : father).layers[static_cast<std::size_t>(index)];
            CHECK(std::equal(layer.row(neuron), layer.row(neuron) + layer.reads, donor.row(neuron)));
            CHECK(layer.biases[static_cast<std::size_t>(neuron)] == donor.biases[static_cast<std::size_t>(neuron)]);
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
    const Brain child = Brain::inherit(shallow, deep, rng, 6, BASE_WIDTH, 0.0);
    CHECK(child.depth() == 6);
    for (const int index : {2, 3}) CHECK(child.layers[static_cast<std::size_t>(index)] == deep.layers[static_cast<std::size_t>(index)]);
    for (const int index : {4, 5}) CHECK(child.layers[static_cast<std::size_t>(index)] == Layer::identity(BASE_WIDTH));
    CHECK(Brain::inherit(deep, deep, rng, 2, BASE_WIDTH, 0.0).depth() == 2);
    const Brain deeper = deep.resized(6, BASE_WIDTH), shallower = deep.resized(2, BASE_WIDTH);
    CHECK((deeper.depth() == 6 && shallower.depth() == 2));
    CHECK((deeper.layers[3] == deep.layers[3] && deeper.layers[5] == Layer::identity(BASE_WIDTH) && shallower.layers[1] == deep.layers[1]));
}

TEST_CASE("a child wider than a parent takes the neurons only the wider parent has from it") {
    PyRandom rng(3);
    const Brain narrow = Brain::random(rng, 2, 38), wide = Brain::random(rng, 2, 48);
    const Brain child = Brain::inherit(narrow, wide, rng, 2, 53, 0.0);
    CHECK((child.depth() == 2 && child.width() == 53 && child.layers[1].reads == 53));
    for (int neuron = 38; neuron < 48; ++neuron) {  // only the wider parent has these
        CHECK(std::equal(child.layers[0].row(neuron), child.layers[0].row(neuron) + INPUT_SIZE, wide.layers[0].row(neuron)));
        CHECK(std::equal(child.layers[1].row(neuron), child.layers[1].row(neuron) + 48, wide.layers[1].row(neuron)));
    }
    for (int neuron = 48; neuron < 53; ++neuron) {  // neither has these: idle
        CHECK(std::all_of(child.layers[1].row(neuron), child.layers[1].row(neuron) + 53, [](double weight) { return weight == 0.0; }));
    }
    for (const auto& row : child.output_weights) CHECK(std::all_of(row.begin() + 48, row.end(), [](double weight) { return weight == 0.0; }));
    const Brain narrower = Brain::inherit(wide, wide, rng, 1, 38, 0.0);
    CHECK((narrower.output_weights[0].size() == 38 && narrower.layers[0] == wide.layers[0].fitted(38, INPUT_SIZE)));
}

TEST_CASE("resizing wider adds idle neurons that change nothing, and narrower drops the last") {
    PyRandom rng(7);
    const Brain brain = Brain::random(rng, 3);
    const Brain wider = brain.resized(3, 53);
    CHECK((wider.width() == 53 && wider.layers[2].reads == 53));
    Senses senses{};
    senses.fill(0.4);
    for (int action = 0; action < ACTION_COUNT; ++action) CHECK(wider.values(senses)[action] == doctest::Approx(brain.values(senses)[action]).epsilon(1e-12));
    const Brain narrower = brain.resized(3, 30);
    CHECK((narrower.width() == 30 && narrower.layers[1].row(29)[29] == brain.layers[1].row(29)[29]));
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
    for (int neuron = 0; neuron < BASE_WIDTH; ++neuron) CHECK(partial.layers[0].row(neuron)[SenseHunger] == 0.0);
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

TEST_CASE("there are 105 senses, and a narrower saved brain loads as saved, valuing alike") {
    CHECK(INPUT_SIZE == 105);
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
    nlohmann::json record = {{"senses", SENSES}, {"centred_senses", true}, {"actions", ACTIONS}, {"output_weights", output}, {"output_biases", output_biases}};
    record["layers"] = nlohmann::json::array();
    for (int index = 0; index < 2; ++index) record["layers"].push_back({{"weights", weights[index]}, {"biases", biases[index]}});
    const Brain loaded = Brain::from_record(record);
    CHECK((loaded.layers[0].reads == INPUT_SIZE && loaded.layers[1].reads == narrow && loaded.width() == narrow));
    Senses senses{};
    for (double& sense : senses) sense = uniform(engine) + 0.5;
    std::vector<double> activation(senses.begin(), senses.end());
    for (int sense = 0; sense < INPUT_SIZE; ++sense) activation[static_cast<std::size_t>(sense)] = read_sense(sense, senses[sense]);  // as a brain reads them
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

TEST_CASE("a child's brain is its parents' nudged by normal mutations of the mutation's size") {
    PyRandom rng(5);
    const Brain parent = Brain::random(rng, 4, MAX_WIDTH);
    std::vector<double> nudges;
    for (int child = 0; child < 20; ++child) {
        const Brain made = Brain::inherit(parent, parent, rng, 4, MAX_WIDTH, 0.05);
        for (std::size_t layer = 0; layer < made.layers.size(); ++layer) {
            for (std::size_t index = 0; index < made.layers[layer].weights.size(); ++index) {
                nudges.push_back(made.layers[layer].weights[index] - parent.layers[layer].weights[index]);
            }
        }
    }
    double sum = 0, squares = 0;
    int wide = 0;
    for (const double nudge : nudges) {
        sum += nudge;
        squares += nudge * nudge;
        wide += std::abs(nudge) > 3 * 0.05;
    }
    const double mean = sum / nudges.size(), spread = std::sqrt(squares / nudges.size() - mean * mean);
    CHECK(std::abs(mean) < 0.0005);
    CHECK(spread == doctest::Approx(0.05).epsilon(0.01));
    CHECK(static_cast<double>(wide) / nudges.size() == doctest::Approx(0.0027).epsilon(0.15));  // a normal's tails beyond 3 sigma
    const Brain again = Brain::inherit(parent, parent, rng, 4, MAX_WIDTH, 0.05);
    CHECK(again.layers[0].weights != Brain::inherit(parent, parent, rng, 4, MAX_WIDTH, 0.05).layers[0].weights);  // each child its own
}


TEST_CASE("a child's brain is made exactly as the Python version makes it from the same seed") {
    PyRandom rng(7);
    const Brain first = Brain::random(rng, 2, 40), second = Brain::random(rng, 3, 45);
    const Brain child = Brain::inherit(first, second, rng, 3, 45, 0.05);
    const std::vector<double> found = {child.layers[0].row(3)[11], child.layers[0].row(44)[89], child.layers[1].biases[20],
                                       child.layers[2].row(5)[7], child.output_weights[23][44], child.output_biases[3]};
    // As the Python version's Brain.inherit makes them, to the last bit (its test pins the same numbers).
    CHECK(found == std::vector<double>{-0.15983579101927251, 0.092250751175896234, -0.097734976568545115, -0.097151955572367998,
                                       -0.00011420547423109583, -0.00019905365281007838});  // the output layer held in hundredths
    CHECK(rng.random() == 0.20169522729508749);  // one draw of the world's for the child
}

TEST_CASE("a brain that read its senses uncentred, made to read them centred, values everything just as before") {
    PyRandom rng(3);
    Brain brain = Brain::random(rng, 2, BASE_WIDTH);
    Senses senses{};
    for (int sense = 0; sense < INPUT_SIZE; ++sense) senses[sense] = signed_sense(sense) ? rng.uniform(-1, 1) : rng.random();
    // Its values as it read them before: the senses as they are, through its layers.
    std::vector<double> activation(senses.begin(), senses.end());
    for (const Layer& layer : brain.layers) {
        std::vector<double> next(static_cast<std::size_t>(layer.neurons()));
        for (int neuron = 0; neuron < layer.neurons(); ++neuron) {
            double sum = layer.biases[static_cast<std::size_t>(neuron)];
            for (int input = 0; input < layer.reads; ++input) sum += layer.row(neuron)[input] * activation[static_cast<std::size_t>(input)];
            next[static_cast<std::size_t>(neuron)] = std::tanh(sum);
        }
        activation = next;
    }
    brain.centre_inputs();
    const Values values = brain.values(senses);
    for (int action = 0; action < ACTION_COUNT; ++action) {
        double before = brain.output_biases[action];
        for (std::size_t neuron = 0; neuron < activation.size(); ++neuron) before += brain.output_weights[action][neuron] * activation[neuron];
        CHECK(values[action] == doctest::Approx(before / VALUE_SCALE).epsilon(1e-12));  // held in hundredths, given whole
    }
    CHECK(read_sense(SenseHunger, 0.0) == -1.0);
    CHECK(read_sense(SenseHunger, 1.0) == 1.0);
    CHECK(read_sense(SenseLatitude, -0.4) == -0.4);  // already -1..1
}

TEST_CASE("a brain made to hold its values in hundredths values everything as before, and exports them whole") {
    PyRandom rng(4);
    Brain brain = Brain::random(rng, 2, BASE_WIDTH);
    Senses senses{};
    for (int sense = 0; sense < INPUT_SIZE; ++sense) senses[sense] = rng.random();
    const Values before = brain.values(senses);
    // As a brain saved before held them: whole.
    for (auto& row : brain.output_weights) {
        for (double& weight : row) weight /= VALUE_SCALE;
    }
    for (double& bias : brain.output_biases) bias /= VALUE_SCALE;
    brain.scale_values();
    const Values after = brain.values(senses), imported = Brain::from_record(brain.to_record()).values(senses);
    for (int action = 0; action < ACTION_COUNT; ++action) {
        CHECK(after[action] == doctest::Approx(before[action]).epsilon(1e-12));
        CHECK(imported[action] == doctest::Approx(before[action]).epsilon(1e-4));  // rounded for export
    }
    CHECK(std::abs(brain.output_biases[Eat]) < 0.01);  // held small
}
