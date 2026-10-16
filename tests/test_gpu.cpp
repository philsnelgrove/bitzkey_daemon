// The GPU brains against the CPU's: the same values and the same learning, near enough, since the
// GPU works in floats. Skipped where there is no GPU.

#include <doctest/doctest.h>

#include <cmath>

#include "core/GpuBrains.hpp"
#include "core/PyRandom.hpp"
#include "core/Rules.hpp"
#include "core/Simulation.hpp"

#include <stdexcept>

using namespace bitzkey;

namespace {

Senses some_senses(PyRandom& rng) {
    Senses senses{};
    for (double& sense : senses) sense = rng.uniform(-1.0, 1.0);
    return senses;
}

double furthest(const Brain& a, const Brain& b) {  // the largest difference between two brains' weights
    double most = 0.0;
    for (std::size_t layer = 0; layer < a.layers.size(); ++layer) {
        for (std::size_t index = 0; index < a.layers[layer].weights.size(); ++index) {
            most = std::max(most, std::abs(a.layers[layer].weights[index] - b.layers[layer].weights[index]));
        }
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) most = std::max(most, std::abs(a.layers[layer].biases[neuron] - b.layers[layer].biases[neuron]));
    }
    for (int action = 0; action < ACTION_COUNT; ++action) {
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
            most = std::max(most, std::abs(a.output_weights[action][neuron] - b.output_weights[action][neuron]));
        }
        most = std::max(most, std::abs(a.output_biases[action] - b.output_biases[action]));
    }
    return most;
}

}  // namespace

TEST_CASE("brains on the GPU value and learn as on the CPU, near enough") {
    std::string why;
    const auto engine = gpu::BrainEngine::create(&why);
    if (!engine) {
        MESSAGE("no GPU: " << why);
        return;
    }
    for (const gpu::DeviceInfo& device : engine->devices()) MESSAGE("GPU: " << device.name);
    PyRandom rng(7);
    std::vector<Brain> brains;
    for (const int depth : {2, 3, 6, 14, 2}) brains.push_back(Brain::random(rng, depth));
    std::vector<gpu::BrainEngine::Handle> handles;
    for (const Brain& brain : brains) handles.push_back(engine->attach(brain));
    CHECK(engine->attached() == brains.size());

    std::vector<Senses> senses;
    for (std::size_t index = 0; index < brains.size(); ++index) senses.push_back(some_senses(rng));
    std::vector<Values> values(brains.size());
    engine->values(handles, senses, values);
    for (std::size_t index = 0; index < brains.size(); ++index) {
        const Values expected = brains[index].values(senses[index]);
        for (int action = 0; action < ACTION_COUNT; ++action) CHECK(values[index][action] == doctest::Approx(expected[action]).epsilon(1e-4));
    }

    // Each brain learns a traced lesson, a plain one, then another traced one, as on the CPU.
    std::vector<Senses> lesson_senses;
    for (std::size_t index = 0; index < 3 * brains.size(); ++index) lesson_senses.push_back(some_senses(rng));
    std::vector<gpu::BrainEngine::Lesson> lessons;
    const double lambda = TRACE_DECAY;
    TRACE_DECAY = 0.85;  // the CPU's traced lessons decay by discount (1) times lambda, as the GPU's are told to
    for (int round = 0; round < 3; ++round) {
        for (std::size_t index = 0; index < brains.size(); ++index) {
            const Senses& seen = lesson_senses[static_cast<std::size_t>(round) * brains.size() + index];
            const int action = static_cast<int>((index * 5 + static_cast<std::size_t>(round) * 3) % ACTION_COUNT);
            const double target = round == 1 ? -0.4 : 0.7;
            const bool traced = round != 1;
            lessons.push_back({handles[index], &seen, action, target, 0.05, traced, 0.85});
            if (traced) {
                brains[index].learn_traced(seen, action, target, 0.05, 1.0);
            } else {
                brains[index].learn(seen, action, target, 0.05);
            }
        }
    }
    TRACE_DECAY = lambda;
    engine->learn(lessons);
    std::vector<Brain> back = brains;  // the right shapes, overwritten from the GPU
    std::vector<Brain*> into;
    for (Brain& brain : back) into.push_back(&brain);
    engine->download(handles, into);
    for (std::size_t index = 0; index < brains.size(); ++index) {
        CAPTURE(index);
        CHECK(furthest(back[index], brains[index]) < 1e-4);
    }
    for (const auto handle : handles) engine->detach(handle);
    CHECK(engine->attached() == 0);
}

TEST_CASE("a world thinks on the GPUs, keeps only the present, and brings brains back to read") {
    if (!gpu::BrainEngine::create()) return;  // no GPU: nothing to try
    const int use = USE_GPU;
    USE_GPU = 1;
    Simulation simulation(Simulation::Settings{30, 30, 30, 1});
    USE_GPU = use;
    REQUIRE(simulation.on_gpu());
    for (int turn = 0; turn < 20; ++turn) simulation.advance();
    CHECK(simulation.history_turns().size() == 1);  // with brains only on the GPUs, no rewinding
    Bitzkey* thinker = nullptr;
    for (Bitzkey* agent : simulation.living()) {
        if (agent->brain) thinker = agent;
    }
    REQUIRE(thinker);
    const Brain before = *thinker->brain;
    simulation.sync_brains();
    CHECK_FALSE(*thinker->brain == before);  // it learnt on the GPU, and that has come back
}

TEST_CASE("think on the GPU is locked, greyed out, only where there is no GPU") {
    std::string why;
    const bool found = gpu::available(&why);
    CHECK(rules::locked("USE_GPU").empty() == found);
    if (!found) CHECK_FALSE(why.empty());
    CHECK(rules::locked("TEMPERATURE").empty());  // every other rule can be changed
}

TEST_CASE("a world whose GPU fails thinks on the CPU from there, rather than stop") {
    if (!gpu::available()) return;
    const int was = USE_GPU;
    USE_GPU = 1;
    Simulation world(Simulation::Settings{30, 30, 40, 5});
    USE_GPU = was;
    REQUIRE(world.on_gpu());
    for (int turn = 0; turn < 10; ++turn) world.advance();
    world.leave_gpu(std::runtime_error("Vulkan: allocating GPU memory failed (-2)"));
    CHECK_FALSE(world.on_gpu());
    CHECK(world.gpu_status() == "thinking on the CPU: the GPU failed (Vulkan: allocating GPU memory failed (-2))");
    for (int turn = 0; turn < 10; ++turn) world.advance();
    CHECK(world.turn() == 20);
    CHECK(world.rewind());  // it keeps a history again
    CHECK(world.turn() == 19);
}
