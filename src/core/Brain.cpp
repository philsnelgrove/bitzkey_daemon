#include "core/Brain.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "core/PyFormat.hpp"

namespace bitzkey {

int action_index(std::string_view name) {
    for (int index = 0; index < ACTION_COUNT; ++index) {
        if (ACTIONS[index] == name) return index;
    }
    return -1;
}

int sense_index(std::string_view name) {
    for (int index = 0; index < INPUT_SIZE; ++index) {
        if (SENSES[index] == name) return index;
    }
    return -1;
}

namespace {

// Marsaglia and Tsang's ziggurat for the normal distribution, 128 layers: nearly every draw is one
// multiply, with no logarithm, root or sine.
struct Ziggurat {
    std::array<std::uint32_t, 128> k;
    std::array<double, 128> w, f;
    Ziggurat() {
        const double m1 = 2147483648.0, vn = 9.91256303526217e-3;
        double dn = 3.442619855899, tn = dn;
        const double q = vn / std::exp(-0.5 * dn * dn);
        k[0] = static_cast<std::uint32_t>((dn / q) * m1);
        k[1] = 0;
        w[0] = q / m1;
        w[127] = dn / m1;
        f[0] = 1.0;
        f[127] = std::exp(-0.5 * dn * dn);
        for (int i = 126; i >= 1; --i) {
            dn = std::sqrt(-2.0 * std::log(vn / dn + std::exp(-0.5 * dn * dn)));
            k[i + 1] = static_cast<std::uint32_t>((dn / tn) * m1);
            tn = dn;
            f[i] = std::exp(-0.5 * dn * dn);
            w[i] = dn / m1;
        }
    }
};

// A fast stream of random numbers: xoshiro256+, seeded by splitmix64, with normal draws by the ziggurat.
class Stream {
public:
    explicit Stream(std::uint64_t seed) {
        for (std::uint64_t& word : state_) {
            seed += 0x9E3779B97F4A7C15ULL;
            std::uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            word = z ^ (z >> 31);
        }
    }

    double random() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }  // in [0, 1)

    double normal() {  // of mean 0 and spread 1
        static const Ziggurat table;
        constexpr double tail = 3.442620;  // where the base layer's tail begins
        for (;;) {
            const auto hz = static_cast<std::int32_t>(next() >> 32);
            const int iz = hz & 127;
            const double x = hz * table.w[iz];
            if (static_cast<std::uint32_t>(hz < 0 ? -static_cast<std::int64_t>(hz) : hz) < table.k[iz]) return x;
            if (iz == 0) {  // beyond the base: the tail, by Marsaglia's method
                double tx, ty;
                do {
                    tx = -std::log(1.0 - random()) / tail;
                    ty = -std::log(1.0 - random());
                } while (ty + ty < tx * tx);
                return hz > 0 ? tail + tx : -tail - tx;
            }
            if (table.f[iz] + random() * (table.f[iz - 1] - table.f[iz]) < std::exp(-0.5 * x * x)) return x;
        }
    }

private:
    std::uint64_t next() {  // xoshiro256+
        const std::uint64_t result = state_[0] + state_[3];
        const std::uint64_t shifted = state_[1] << 17;
        state_[2] ^= state_[0];
        state_[3] ^= state_[1];
        state_[1] ^= state_[2];
        state_[0] ^= state_[3];
        state_[2] ^= shifted;
        state_[3] = (state_[3] << 45) | (state_[3] >> 19);
        return result;
    }

    std::uint64_t state_[4];
};

// numpy's pairwise summation (the Python version adds up with numpy's sum), so that every sum matches
// the Python version's to the last bit: up to 8 values in order; up to 128 in eight running sums,
// combined in pairs, then the rest in order; more, in two halves split on a multiple of eight.
double pairwise_sum(const double* values, std::size_t count) {
    if (count < 8) {
        double total = -0.0;
        for (std::size_t index = 0; index < count; ++index) total += values[index];
        return total;
    }
    if (count <= 128) {
        double sums[8];
        for (std::size_t lane = 0; lane < 8; ++lane) sums[lane] = values[lane];
        std::size_t index = 8;
        for (; index < count - count % 8; index += 8) {
            for (std::size_t lane = 0; lane < 8; ++lane) sums[lane] += values[index + lane];
        }
        double total = ((sums[0] + sums[1]) + (sums[2] + sums[3])) + ((sums[4] + sums[5]) + (sums[6] + sums[7]));
        for (; index < count; ++index) total += values[index];
        return total;
    }
    std::size_t half = count / 2;
    half -= half % 8;
    return pairwise_sum(values, half) + pairwise_sum(values + half, count - half);
}

// The products of two runs of numbers, added up as numpy's sum would.
double dot(const double* first, const double* second, std::size_t count) {
    std::array<double, std::max(INPUT_SIZE, MAX_WIDTH)> products;  // no layer reads more
    for (std::size_t index = 0; index < count; ++index) products[index] = first[index] * second[index];
    return pairwise_sum(products.data(), count);
}

}  // namespace

Held memory_after(const Values& values, const Held& held) {
    Held now{};
    for (int neuron = 0; neuron < MEMORY_NEURONS; ++neuron) {
        const double gate = 1 / (1 + std::exp(-values[MEMORY_GATES + neuron]));
        now[neuron] = (1 - gate) * held[neuron] + gate * std::tanh(values[MEMORY_VALUES + neuron]);
    }
    return now;
}

std::vector<std::pair<int, double>> choice_odds(const Values& values, std::span<const int> options) {
    double best = values[options[0]];
    for (const int option : options) best = std::max(best, values[option]);
    std::vector<double> weights;
    weights.reserve(options.size());
    for (const int option : options) weights.push_back(std::exp((values[option] - best) / TEMPERATURE));
    const double total = py_sum(weights);
    std::vector<std::pair<int, double>> odds;
    odds.reserve(options.size());
    for (std::size_t index = 0; index < options.size(); ++index) odds.emplace_back(options[index], weights[index] / total);
    return odds;
}

void gene_senses(Dna dna, Senses& senses) {
    const Traits inborn = decode_temperament(dna);
    const Body body = decode_body(dna);
    const Climate climate = decode_climate(dna);
    for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) senses[SenseInborn + trait] = share(inborn[trait]);
    senses[SenseBody] = body[SpeciesTrait] / static_cast<double>(SPECIES.size() - 1);
    for (int trait = 1; trait < BODY_TRAIT_COUNT; ++trait) senses[SenseBody + trait] = share(body[trait]);
    senses[SenseHomeTemperature] = climate.temperature / static_cast<double>(TEMPERATURES.size() - 1);
    senses[SenseHeatTolerance] = climate.heat;
    senses[SenseColdTolerance] = climate.cold;
    const Terrain terrain = decode_terrain(dna);
    senses[SenseSwims] = swims(terrain);
    senses[SenseWalks] = walks(terrain);
    senses[SenseFlies] = terrain == Terrain::Avian;
    senses[SenseNocturnal] = decode_nocturnal(dna);
}

Layer Layer::zeros(int neurons, int reads) {
    Layer layer;
    layer.reads = reads;
    layer.weights.assign(static_cast<std::size_t>(neurons) * static_cast<std::size_t>(reads), 0.0);
    layer.biases.assign(static_cast<std::size_t>(neurons), 0.0);
    return layer;
}

Layer Layer::identity(int width) {
    Layer layer = zeros(width, width);
    for (int neuron = 0; neuron < width; ++neuron) layer.row(neuron)[neuron] = 1.0;
    return layer;
}

Layer Layer::fitted(int to_neurons, int to_reads) const {
    Layer layer = zeros(to_neurons, to_reads);
    const int kept = std::min(to_neurons, neurons()), columns = std::min(to_reads, reads);
    for (int neuron = 0; neuron < kept; ++neuron) {
        std::copy(row(neuron), row(neuron) + columns, layer.row(neuron));
        layer.biases[static_cast<std::size_t>(neuron)] = biases[static_cast<std::size_t>(neuron)];
    }
    return layer;
}

namespace {
// An output row cut down or padded with zeros to `width`.
std::vector<double> fitted_row(const std::vector<double>& row, int width) {
    std::vector<double> fitted(row);
    fitted.resize(static_cast<std::size_t>(width), 0.0);
    return fitted;
}
}  // namespace

Brain Brain::random(PyRandom& rng, int depth, int width) {
    Brain brain;
    for (int index = 0; index < depth; ++index) {
        Layer layer = Layer::zeros(width, index == 0 ? INPUT_SIZE : width);
        const double bound = std::sqrt(6.0 / (layer.reads + width));
        for (double& weight : layer.weights) weight = rng.uniform(-bound, bound);
        brain.layers.push_back(std::move(layer));
    }
    const double spread = output_spread(width);
    for (auto& row : brain.output_weights) {
        row.resize(static_cast<std::size_t>(width));
        for (double& weight : row) weight = rng.uniform(-spread, spread);
    }
    for (double& bias : brain.output_biases) bias = rng.uniform(-0.2, 0.2);
    for (const int heading : {GoNorth, GoSouth, GoEast, GoWest, WalkNorth, WalkSouth, WalkEast, WalkWest}) {
        brain.output_biases[heading] -= HEADING_RELUCTANCE;
    }
    for (int call = AlertCall; call < CALL_OPTIONS; ++call) brain.output_biases[call_output(call)] -= CALL_RELUCTANCE;
    for (int neuron = 0; neuron < MEMORY_NEURONS; ++neuron) brain.output_biases[MEMORY_GATES + neuron] -= MEMORY_PERSISTENCE;
    brain.scale_values();  // valuing as it would whole
    return brain;
}

Brain Brain::inherit(const Brain& first, const Brain& second, PyRandom& rng, int depth, int width, double mutation) {
    return inherit(first, second, rng.getrandbits(64), depth, width, mutation);
}

Brain Brain::inherit(const Brain& first, const Brain& second, std::uint64_t seed, int depth, int width, double mutation) {
    // Thousands of draws a brain, so they come from a fast stream of the child's own, seeded from the
    // world's: one draw of the world's a child, and children made on many cores at once.
    Stream stream(seed);
    Brain child;
    for (int index = 0; index < depth; ++index) {
        const int reads = index == 0 ? INPUT_SIZE : width;
        const bool in_first = index < first.depth(), in_second = index < second.depth();
        if (in_first && in_second) {
            Layer layer = first.layers[static_cast<std::size_t>(index)].fitted(width, reads);
            const Layer& other = second.layers[static_cast<std::size_t>(index)];
            const int columns = std::min(reads, other.reads);
            for (int neuron = 0; neuron < width; ++neuron) {
                // From the first parent as chosen while it has the neuron, and wherever the second hasn't.
                const bool from_first = stream.random() < 0.5;
                if ((from_first && neuron < first.width()) || neuron >= second.width()) continue;
                std::copy(other.row(neuron), other.row(neuron) + columns, layer.row(neuron));
                std::fill(layer.row(neuron) + columns, layer.row(neuron) + reads, 0.0);
                layer.biases[static_cast<std::size_t>(neuron)] = other.biases[static_cast<std::size_t>(neuron)];
            }
            child.layers.push_back(std::move(layer));
        } else if (in_first || in_second) {
            child.layers.push_back((in_first ? first : second).layers[static_cast<std::size_t>(index)].fitted(width, reads));
        } else {
            child.layers.push_back(Layer::identity(width));
        }
    }
    for (int output = 0; output < OUTPUT_COUNT; ++output) {
        const Brain& donor = stream.random() < 0.5 ? first : second;
        child.output_weights[output] = fitted_row(donor.output_weights[output], width);
        child.output_biases[output] = donor.output_biases[output];
    }
    if (mutation != 0.0) {
        for (Layer& layer : child.layers) {
            for (double& weight : layer.weights) weight += mutation * stream.normal();
        }
        for (auto& row : child.output_weights) {
            for (double& weight : row) weight += mutation * VALUE_SCALE * stream.normal();  // as much as in whole values
        }
        for (Layer& layer : child.layers) {
            for (double& bias : layer.biases) bias += mutation * stream.normal();
        }
        for (double& bias : child.output_biases) bias += mutation * VALUE_SCALE * stream.normal();
    }
    return child;
}

Brain Brain::resized(int depth, int width) const {
    Brain brain;
    for (int index = 0; index < std::min(depth, this->depth()); ++index) {
        brain.layers.push_back(layers[static_cast<std::size_t>(index)].fitted(width, index == 0 ? INPUT_SIZE : width));
    }
    while (brain.depth() < depth) brain.layers.push_back(Layer::identity(width));
    for (int output = 0; output < OUTPUT_COUNT; ++output) brain.output_weights[output] = fitted_row(output_weights[output], width);
    brain.output_biases = output_biases;
    return brain;
}

void Brain::centre_inputs() {
    // A sense x read as 2x - 1: each weight on it halved, and the half it gave at 0 given by the neuron's bias.
    if (layers.empty()) return;
    Layer& first = layers.front();
    for (int neuron = 0; neuron < first.neurons(); ++neuron) {
        double* weights = first.row(neuron);
        for (int sense = 0; sense < INPUT_SIZE; ++sense) {
            if (signed_sense(sense)) continue;
            weights[sense] /= 2;
            first.biases[static_cast<std::size_t>(neuron)] += weights[sense];
        }
    }
    traces.reset();
}

void Brain::scale_values() {
    for (auto& row : output_weights) {
        for (double& weight : row) weight *= VALUE_SCALE;
    }
    for (double& bias : output_biases) bias *= VALUE_SCALE;
    traces.reset();
}

std::vector<std::vector<double>> Brain::activations(const Senses& inputs) const {
    std::vector<std::vector<double>> found;
    found.reserve(layers.size() + 1);
    found.emplace_back(inputs.begin(), inputs.end());
    for (int sense = 0; sense < INPUT_SIZE; ++sense) found.back()[static_cast<std::size_t>(sense)] = read_sense(sense, inputs[sense]);
    for (const Layer& layer : layers) {
        const std::vector<double>& below = found.back();
        std::vector<double> output(static_cast<std::size_t>(layer.neurons()));
        for (int neuron = 0; neuron < layer.neurons(); ++neuron) {
            const double sum = dot(layer.row(neuron), below.data(), static_cast<std::size_t>(layer.reads));
            output[static_cast<std::size_t>(neuron)] = std::tanh(sum + layer.biases[static_cast<std::size_t>(neuron)]);
        }
        found.push_back(std::move(output));
    }
    return found;
}

Values Brain::values(const Senses& inputs) const {
    const std::vector<double> top = activations(inputs).back();
    Values values{};
    for (int output = 0; output < OUTPUT_COUNT; ++output) {
        const double sum = dot(output_weights[output].data(), top.data(), top.size());
        values[output] = (sum + output_biases[output]) / VALUE_SCALE;
    }
    return values;
}

void Brain::learn(const Senses& inputs, int action, double target, double learning_rate) {
    if (learning_rate <= 0) return;
    const std::vector<std::vector<double>> found = activations(inputs);
    const std::vector<double>& top = found.back();
    const int width = this->width();
    const double sum = dot(output_weights[action].data(), top.data(), top.size());
    const double error = std::max(-LESSON_LIMIT, std::min(LESSON_LIMIT, (sum + output_biases[action]) / VALUE_SCALE - target));
    std::vector<double> gradient(static_cast<std::size_t>(width));
    for (int neuron = 0; neuron < width; ++neuron) gradient[static_cast<std::size_t>(neuron)] = error * output_weights[action][static_cast<std::size_t>(neuron)];
    const double output_step = learning_rate * error * VALUE_SCALE;  // the output layer moves the whole value as it would
    for (int neuron = 0; neuron < width; ++neuron) {
        output_weights[action][static_cast<std::size_t>(neuron)] -= output_step * top[static_cast<std::size_t>(neuron)];
    }
    for (int index = depth() - 1; index >= 0; --index) {
        Layer& layer = layers[static_cast<std::size_t>(index)];
        const std::vector<double>& output = found[static_cast<std::size_t>(index) + 1];
        const std::vector<double>& below = found[static_cast<std::size_t>(index)];
        std::array<double, MAX_WIDTH> delta{};
        for (int neuron = 0; neuron < width; ++neuron) {
            const double value = output[static_cast<std::size_t>(neuron)];
            delta[neuron] = gradient[static_cast<std::size_t>(neuron)] * (1 - value * value);
        }
        if (index) {
            for (int input = 0; input < layer.reads; ++input) {
                double back = 0.0;
                for (int neuron = 0; neuron < width; ++neuron) back += layer.row(neuron)[input] * delta[neuron];
                gradient[static_cast<std::size_t>(input)] = back;
            }
        }
        for (int neuron = 0; neuron < width; ++neuron) layer.biases[static_cast<std::size_t>(neuron)] -= learning_rate * delta[neuron];
        for (int neuron = 0; neuron < width; ++neuron) {
            double* weights = layer.row(neuron);
            for (int input = 0; input < layer.reads; ++input) {
                weights[input] -= learning_rate * (delta[neuron] * below[static_cast<std::size_t>(input)]);
            }
        }
    }
    output_biases[action] -= output_step;
}

void Brain::learn_traced(const Senses& inputs, int action, double target, double learning_rate, double discount,
                         double sharpness) {
    learn_traced_by(inputs, action, target, learning_rate, discount * (TRACE_DECAY + (SMART_TRACE_DECAY - TRACE_DECAY) * sharpness));
}

void Brain::learn_traced_by(const Senses& inputs, int action, double target, double learning_rate, double decay) {
    if (learning_rate <= 0) return;
    const int width = this->width();
    if (!traces) {
        traces.emplace();
        for (const Layer& layer : layers) traces->layers.push_back(Layer::zeros(layer.neurons(), layer.reads));
        for (auto& row : traces->output_weights) row.assign(static_cast<std::size_t>(width), 0.0);
    }
    const std::vector<std::vector<double>> found = activations(inputs);
    const std::vector<double>& top = found.back();
    const double sum = dot(output_weights[action].data(), top.data(), top.size());
    const double error = std::max(-LESSON_LIMIT, std::min(LESSON_LIMIT, (sum + output_biases[action]) / VALUE_SCALE - target));
    for (auto& row : traces->output_weights) {
        for (double& trace : row) trace *= decay;
    }
    // The output layer's traces gather in hundredths, so it moves the whole value as it would.
    for (int neuron = 0; neuron < width; ++neuron) traces->output_weights[action][static_cast<std::size_t>(neuron)] += VALUE_SCALE * top[static_cast<std::size_t>(neuron)];
    for (double& trace : traces->output_biases) trace *= decay;
    traces->output_biases[action] += VALUE_SCALE;
    std::vector<double> gradient(output_weights[action].begin(), output_weights[action].end());
    for (int index = depth() - 1; index >= 0; --index) {
        const Layer& layer = layers[static_cast<std::size_t>(index)];
        Layer& trace = traces->layers[static_cast<std::size_t>(index)];
        const std::vector<double>& output = found[static_cast<std::size_t>(index) + 1];
        const std::vector<double>& below = found[static_cast<std::size_t>(index)];
        std::array<double, MAX_WIDTH> delta{};
        for (int neuron = 0; neuron < width; ++neuron) {
            const double value = output[static_cast<std::size_t>(neuron)];
            delta[neuron] = gradient[static_cast<std::size_t>(neuron)] * (1 - value * value);
        }
        if (index) {
            for (int input = 0; input < layer.reads; ++input) {
                double back = 0.0;
                for (int neuron = 0; neuron < width; ++neuron) back += layer.row(neuron)[input] * delta[neuron];
                gradient[static_cast<std::size_t>(input)] = back;
            }
        }
        for (double& weight : trace.weights) weight *= decay;
        for (int neuron = 0; neuron < width; ++neuron) {
            double* weights = trace.row(neuron);
            for (int input = 0; input < layer.reads; ++input) weights[input] += delta[neuron] * below[static_cast<std::size_t>(input)];
        }
        for (double& bias : trace.biases) bias *= decay;
        for (int neuron = 0; neuron < width; ++neuron) trace.biases[static_cast<std::size_t>(neuron)] += delta[neuron];
    }
    const double step = learning_rate * error;
    for (std::size_t index = 0; index < layers.size(); ++index) {
        Layer& layer = layers[index];
        const Layer& trace = traces->layers[index];
        for (std::size_t weight = 0; weight < layer.weights.size(); ++weight) layer.weights[weight] -= step * trace.weights[weight];
        for (std::size_t neuron = 0; neuron < layer.biases.size(); ++neuron) layer.biases[neuron] -= step * trace.biases[neuron];
    }
    for (int row = 0; row < OUTPUT_COUNT; ++row) {
        for (int neuron = 0; neuron < width; ++neuron) {
            output_weights[row][static_cast<std::size_t>(neuron)] -= step * traces->output_weights[row][static_cast<std::size_t>(neuron)];
        }
    }
    for (int row = 0; row < OUTPUT_COUNT; ++row) output_biases[row] -= step * traces->output_biases[row];
}

void Brain::learn_memory(const Senses& inputs, std::span<const Senses* const> past, int action, double target, double learning_rate) {
    if (learning_rate <= 0 || past.empty()) return;
    const int width = this->width();
    const auto clip = [](double error) { return std::max(-LESSON_LIMIT, std::min(LESSON_LIMIT, error)); };
    const auto valued = [&](int output, const std::vector<double>& top) {
        return (dot(output_weights[output].data(), top.data(), top.size()) + output_biases[output]) / VALUE_SCALE;
    };
    // How the action's value, with its error, moves with what each memory neuron held: the error carried down through
    // every layer as a lesson's, nothing moving, in whole values.
    Held carried{};
    {
        const std::vector<std::vector<double>> found = activations(inputs);
        const double error = clip(valued(action, found.back()) - target);
        std::vector<double> gradient(static_cast<std::size_t>(width));
        for (int neuron = 0; neuron < width; ++neuron) gradient[static_cast<std::size_t>(neuron)] = error * output_weights[action][static_cast<std::size_t>(neuron)];
        for (int index = depth() - 1; index >= 0; --index) {
            const Layer& layer = layers[static_cast<std::size_t>(index)];
            const std::vector<double>& output = found[static_cast<std::size_t>(index) + 1];
            std::array<double, MAX_WIDTH> delta{};
            for (int neuron = 0; neuron < width; ++neuron) {
                const double value = output[static_cast<std::size_t>(neuron)];
                delta[neuron] = gradient[static_cast<std::size_t>(neuron)] * (1 - value * value);
            }
            if (index) {
                for (int input = 0; input < layer.reads; ++input) {
                    double back = 0.0;
                    for (int neuron = 0; neuron < width; ++neuron) back += layer.row(neuron)[input] * delta[neuron];
                    gradient[static_cast<std::size_t>(input)] = back;
                }
            } else {
                for (int held = 0; held < MEMORY_NEURONS; ++held) {
                    double back = 0.0;
                    for (int neuron = 0; neuron < width; ++neuron) back += layer.row(neuron)[SenseMemory + held] * delta[neuron];
                    carried[held] = back / VALUE_SCALE;
                }
            }
        }
    }
    for (std::size_t step = 0; step < past.size(); ++step) {
        // The choice before: each gate's and value's error, as an output's in a lesson, from what the neuron came to
        // hold; and what of the error reaches what it held before, through what it kept of it.
        const Senses& before = *past[step];
        const std::vector<std::vector<double>> found = activations(before);
        const std::vector<double>& top = found.back();
        std::array<double, 2 * MEMORY_NEURONS> errors{};
        Held kept{};
        for (int held = 0; held < MEMORY_NEURONS; ++held) {
            const double gate = 1 / (1 + std::exp(-valued(MEMORY_GATES + held, top)));
            const double write = std::tanh(valued(MEMORY_VALUES + held, top));
            const double was = before[SenseMemory + held];
            errors[static_cast<std::size_t>(held)] = clip(carried[held] * (write - was) * gate * (1 - gate));
            errors[static_cast<std::size_t>(MEMORY_NEURONS + held)] = clip(carried[held] * gate * (1 - write * write));
            kept[held] = carried[held] * (1 - gate);
        }
        std::vector<double> gradient(static_cast<std::size_t>(width), 0.0);
        for (int neuron = 0; neuron < width; ++neuron) {
            for (int output = 0; output < 2 * MEMORY_NEURONS; ++output) {
                gradient[static_cast<std::size_t>(neuron)] += errors[static_cast<std::size_t>(output)] * output_weights[MEMORY_GATES + output][static_cast<std::size_t>(neuron)];
            }
        }
        for (int output = 0; output < 2 * MEMORY_NEURONS; ++output) {
            const double output_step = learning_rate * errors[static_cast<std::size_t>(output)] * VALUE_SCALE;
            for (int neuron = 0; neuron < width; ++neuron) {
                output_weights[MEMORY_GATES + output][static_cast<std::size_t>(neuron)] -= output_step * top[static_cast<std::size_t>(neuron)];
            }
        }
        const bool further = step + 1 < past.size();
        Held reached{};
        for (int index = depth() - 1; index >= 0; --index) {
            Layer& layer = layers[static_cast<std::size_t>(index)];
            const std::vector<double>& output = found[static_cast<std::size_t>(index) + 1];
            const std::vector<double>& below = found[static_cast<std::size_t>(index)];
            std::array<double, MAX_WIDTH> delta{};
            for (int neuron = 0; neuron < width; ++neuron) {
                const double value = output[static_cast<std::size_t>(neuron)];
                delta[neuron] = gradient[static_cast<std::size_t>(neuron)] * (1 - value * value);
            }
            if (index) {
                for (int input = 0; input < layer.reads; ++input) {
                    double back = 0.0;
                    for (int neuron = 0; neuron < width; ++neuron) back += layer.row(neuron)[input] * delta[neuron];
                    gradient[static_cast<std::size_t>(input)] = back;
                }
            } else if (further) {
                for (int held = 0; held < MEMORY_NEURONS; ++held) {
                    double back = 0.0;
                    for (int neuron = 0; neuron < width; ++neuron) back += layer.row(neuron)[SenseMemory + held] * delta[neuron];
                    reached[held] = back / VALUE_SCALE;
                }
            }
            for (int neuron = 0; neuron < width; ++neuron) layer.biases[static_cast<std::size_t>(neuron)] -= learning_rate * delta[neuron];
            for (int neuron = 0; neuron < width; ++neuron) {
                double* weights = layer.row(neuron);
                for (int input = 0; input < layer.reads; ++input) {
                    weights[input] -= learning_rate * (delta[neuron] * below[static_cast<std::size_t>(input)]);
                }
            }
        }
        for (int output = 0; output < 2 * MEMORY_NEURONS; ++output) {
            output_biases[MEMORY_GATES + output] -= learning_rate * errors[static_cast<std::size_t>(output)] * VALUE_SCALE;
        }
        for (int held = 0; held < MEMORY_NEURONS; ++held) carried[held] = kept[held] + reached[held];
    }
}

nlohmann::ordered_json Brain::to_record() const {
    using json = nlohmann::ordered_json;
    const auto rounded = [](auto begin, auto end) {
        json list = json::array();
        for (auto it = begin; it != end; ++it) list.push_back(py_round(*it, RECORD_DIGITS));
        return list;
    };
    json record = json::object();
    record["senses"] = json::array();
    for (const auto name : SENSES) record["senses"].push_back(std::string(name));
    record["centred_senses"] = true;
    record["actions"] = json::array();
    for (const auto name : OUTPUTS) record["actions"].push_back(std::string(name));  // its outputs: actions, then calls
    record["layers"] = json::array();
    for (const Layer& layer : layers) {
        json entry = json::object();
        entry["weights"] = json::array();
        for (int neuron = 0; neuron < layer.neurons(); ++neuron) {
            entry["weights"].push_back(rounded(layer.row(neuron), layer.row(neuron) + layer.reads));
        }
        entry["biases"] = rounded(layer.biases.begin(), layer.biases.end());
        record["layers"].push_back(std::move(entry));
    }
    // The output layer exported whole, as it values (it is held in hundredths), lest rounding lose it.
    const auto whole = [](const auto& numbers) {
        std::vector<double> scaled(numbers.begin(), numbers.end());
        for (double& number : scaled) number /= VALUE_SCALE;
        return scaled;
    };
    record["output_weights"] = json::array();
    for (const auto& row : output_weights) {
        const std::vector<double> scaled = whole(row);
        record["output_weights"].push_back(rounded(scaled.begin(), scaled.end()));
    }
    const std::vector<double> biases = whole(output_biases);
    record["output_biases"] = rounded(biases.begin(), biases.end());
    return record;
}

namespace {
std::vector<double> numbers(const nlohmann::json& list) {
    if (!list.is_array()) throw std::invalid_argument("expected a list of numbers");
    std::vector<double> values;
    for (const auto& item : list) {
        if (!item.is_number()) throw std::invalid_argument("expected a number, found " + item.dump());
        values.push_back(item.get<double>());
    }
    return values;
}
std::vector<std::string> names(const nlohmann::json& list) {
    if (!list.is_array()) throw std::invalid_argument("expected a list of names");
    std::vector<std::string> values;
    for (const auto& item : list) values.push_back(item.is_string() ? item.get<std::string>() : item.dump());
    return values;
}
}  // namespace

Brain Brain::from_record(const nlohmann::json& record) {
    std::vector<std::string> senses, actions;
    std::vector<std::pair<std::vector<std::vector<double>>, std::vector<double>>> saved;
    std::vector<std::vector<double>> output_rows;
    std::vector<double> output_bias;
    try {
        if (!record.is_object()) throw std::invalid_argument("not an object");
        senses = names(record.at("senses"));
        actions = names(record.at("actions"));
        if (record.contains("layers")) {
            for (const auto& layer : record.at("layers")) {
                std::vector<std::vector<double>> rows;
                for (const auto& row : layer.at("weights")) rows.push_back(numbers(row));
                saved.emplace_back(std::move(rows), numbers(layer.at("biases")));
            }
        } else {
            std::vector<std::vector<double>> rows;
            for (const auto& row : record.at("hidden_weights")) rows.push_back(numbers(row));
            saved.emplace_back(std::move(rows), numbers(record.at("hidden_biases")));
        }
        for (const auto& row : record.at("output_weights")) output_rows.push_back(numbers(row));
        output_bias = numbers(record.at("output_biases"));
    } catch (const std::exception& error) {
        throw std::invalid_argument(std::string("The brain is incomplete or not numeric (") + error.what() + ").");
    }
    if (saved.empty()) throw std::invalid_argument("The brain has no hidden layers.");
    const std::size_t width = saved[0].first.size();
    for (std::size_t index = 0; index < saved.size(); ++index) {
        const auto& [rows, biases] = saved[index];
        if (rows.size() != width || biases.size() != width || width == 0 || width > MAX_WIDTH) {
            throw std::invalid_argument("The brain has " + std::to_string(rows.size()) + " neurons in a layer; this version uses up to " +
                                        std::to_string(MAX_WIDTH) + ", the same in every layer.");
        }
        const std::size_t reads = index == 0 ? senses.size() : width;
        for (const auto& row : rows) {
            if (row.size() != reads) throw std::invalid_argument("The brain's weights don't match its listed senses and actions.");
        }
    }
    bool mismatched = output_rows.size() != actions.size() || output_bias.size() != actions.size();
    for (const auto& row : output_rows) mismatched = mismatched || row.size() != width;
    if (mismatched) throw std::invalid_argument("The brain's weights don't match its listed senses and actions.");

    std::unordered_map<std::string, std::size_t> sense_at, action_at;
    // As in a Python dict built from the lists, a repeated name keeps its last position.
    for (std::size_t index = 0; index < senses.size(); ++index) sense_at[senses[index]] = index;
    for (std::size_t index = 0; index < actions.size(); ++index) action_at[actions[index]] = index;
    Brain brain;
    for (std::size_t index = 0; index < saved.size(); ++index) {
        const auto& [rows, biases] = saved[index];
        Layer layer = Layer::zeros(static_cast<int>(width), index == 0 ? INPUT_SIZE : static_cast<int>(width));
        for (std::size_t neuron = 0; neuron < width; ++neuron) {
            const auto& row = rows[neuron];
            for (int input = 0; input < layer.reads; ++input) {
                if (index == 0) {
                    const auto found = sense_at.find(std::string(SENSES[input]));
                    layer.row(static_cast<int>(neuron))[input] = found == sense_at.end() ? 0.0 : row[found->second];
                } else {
                    layer.row(static_cast<int>(neuron))[input] = row[static_cast<std::size_t>(input)];
                }
            }
            layer.biases[neuron] = biases[neuron];
        }
        brain.layers.push_back(std::move(layer));
    }
    for (int output = 0; output < OUTPUT_COUNT; ++output) {
        const auto found = action_at.find(std::string(OUTPUTS[output]));
        brain.output_weights[output].resize(width);
        for (std::size_t neuron = 0; neuron < width; ++neuron) {
            brain.output_weights[output][neuron] = found == action_at.end() ? 0.0 : output_rows[found->second][neuron];
        }
        brain.output_biases[output] = found == action_at.end() ? 0.0 : output_bias[found->second];
    }
    if (!record.value("centred_senses", false)) brain.centre_inputs();  // exported before brains read their senses centred
    brain.scale_values();  // its output layer exported whole, held in hundredths
    return brain;
}

}  // namespace bitzkey
