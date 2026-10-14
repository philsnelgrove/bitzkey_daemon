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
    std::array<double, std::max(INPUT_SIZE, HIDDEN_SIZE)> products;  // no layer reads more
    for (std::size_t index = 0; index < count; ++index) products[index] = first[index] * second[index];
    return pairwise_sum(products.data(), count);
}

}  // namespace

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
}

Layer Layer::identity() {
    Layer layer;
    layer.weights.assign(static_cast<std::size_t>(HIDDEN_SIZE) * HIDDEN_SIZE, 0.0);
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) layer.row(neuron)[neuron] = 1.0;
    return layer;
}

Brain Brain::random(PyRandom& rng, int depth) {
    Brain brain;
    for (int index = 0; index < depth; ++index) {
        Layer layer;
        layer.reads = index == 0 ? INPUT_SIZE : HIDDEN_SIZE;
        const double bound = std::sqrt(6.0 / (layer.reads + HIDDEN_SIZE));
        layer.weights.resize(static_cast<std::size_t>(HIDDEN_SIZE) * layer.reads);
        for (double& weight : layer.weights) weight = rng.uniform(-bound, bound);
        brain.layers.push_back(std::move(layer));
    }
    for (auto& row : brain.output_weights) {
        for (double& weight : row) weight = rng.uniform(-OUTPUT_SPREAD, OUTPUT_SPREAD);
    }
    for (double& bias : brain.output_biases) bias = rng.uniform(-0.2, 0.2);
    for (const int heading : {GoNorth, GoSouth, GoEast, GoWest, WalkNorth, WalkSouth, WalkEast, WalkWest}) {
        brain.output_biases[heading] -= HEADING_RELUCTANCE;
    }
    return brain;
}

Brain Brain::inherit(const Brain& first, const Brain& second, PyRandom& rng, int depth, double mutation) {
    Brain child;
    for (int index = 0; index < depth; ++index) {
        const bool in_first = index < first.depth(), in_second = index < second.depth();
        if (in_first && in_second) {
            std::array<bool, HIDDEN_SIZE> from_first{};
            for (bool& choice : from_first) choice = rng.random() < 0.5;
            Layer layer = first.layers[static_cast<std::size_t>(index)];
            const Layer& other = second.layers[static_cast<std::size_t>(index)];
            for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
                if (from_first[neuron]) continue;
                std::copy(other.row(neuron), other.row(neuron) + other.reads, layer.row(neuron));
                layer.biases[neuron] = other.biases[neuron];
            }
            child.layers.push_back(std::move(layer));
        } else if (in_first || in_second) {
            child.layers.push_back((in_first ? first : second).layers[static_cast<std::size_t>(index)]);
        } else {
            child.layers.push_back(Layer::identity());
        }
    }
    std::array<bool, ACTION_COUNT> from_first{};
    for (bool& choice : from_first) choice = rng.random() < 0.5;
    for (int action = 0; action < ACTION_COUNT; ++action) {
        const Brain& donor = from_first[action] ? first : second;
        child.output_weights[action] = donor.output_weights[action];
        child.output_biases[action] = donor.output_biases[action];
    }
    if (mutation != 0.0) {
        for (Layer& layer : child.layers) {
            for (double& weight : layer.weights) weight += rng.gauss(0, mutation);
        }
        for (auto& row : child.output_weights) {
            for (double& weight : row) weight += rng.gauss(0, mutation);
        }
        for (Layer& layer : child.layers) {
            for (double& bias : layer.biases) bias += rng.gauss(0, mutation);
        }
        for (double& bias : child.output_biases) bias += rng.gauss(0, mutation);
    }
    return child;
}

Brain Brain::resized(int depth) const {
    Brain brain = *this;
    brain.traces.reset();
    if (brain.depth() > depth) brain.layers.resize(static_cast<std::size_t>(depth));
    while (brain.depth() < depth) brain.layers.push_back(Layer::identity());
    return brain;
}

std::vector<std::vector<double>> Brain::activations(const Senses& inputs) const {
    std::vector<std::vector<double>> found;
    found.reserve(layers.size() + 1);
    found.emplace_back(inputs.begin(), inputs.end());
    for (const Layer& layer : layers) {
        const std::vector<double>& below = found.back();
        std::vector<double> output(HIDDEN_SIZE);
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
            const double sum = dot(layer.row(neuron), below.data(), static_cast<std::size_t>(layer.reads));
            output[static_cast<std::size_t>(neuron)] = std::tanh(sum + layer.biases[neuron]);
        }
        found.push_back(std::move(output));
    }
    return found;
}

Values Brain::values(const Senses& inputs) const {
    const std::vector<double> top = activations(inputs).back();
    Values values{};
    for (int action = 0; action < ACTION_COUNT; ++action) {
        const double sum = dot(output_weights[action].data(), top.data(), HIDDEN_SIZE);
        values[action] = sum + output_biases[action];
    }
    return values;
}

void Brain::learn(const Senses& inputs, int action, double target, double learning_rate) {
    if (learning_rate <= 0) return;
    const std::vector<std::vector<double>> found = activations(inputs);
    const std::vector<double>& top = found.back();
    const double sum = dot(output_weights[action].data(), top.data(), HIDDEN_SIZE);
    const double error = std::max(-1.0, std::min(1.0, sum + output_biases[action] - target));
    std::vector<double> gradient(HIDDEN_SIZE);
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) gradient[static_cast<std::size_t>(neuron)] = error * output_weights[action][neuron];
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
        output_weights[action][neuron] -= learning_rate * error * top[static_cast<std::size_t>(neuron)];
    }
    for (int index = depth() - 1; index >= 0; --index) {
        Layer& layer = layers[static_cast<std::size_t>(index)];
        const std::vector<double>& output = found[static_cast<std::size_t>(index) + 1];
        const std::vector<double>& below = found[static_cast<std::size_t>(index)];
        std::array<double, HIDDEN_SIZE> delta{};
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
            const double value = output[static_cast<std::size_t>(neuron)];
            delta[neuron] = gradient[static_cast<std::size_t>(neuron)] * (1 - value * value);
        }
        if (index) {
            for (int input = 0; input < layer.reads; ++input) {
                double back = 0.0;
                for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) back += layer.row(neuron)[input] * delta[neuron];
                gradient[static_cast<std::size_t>(input)] = back;
            }
        }
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) layer.biases[neuron] -= learning_rate * delta[neuron];
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
            double* weights = layer.row(neuron);
            for (int input = 0; input < layer.reads; ++input) {
                weights[input] -= learning_rate * (delta[neuron] * below[static_cast<std::size_t>(input)]);
            }
        }
    }
    output_biases[action] -= learning_rate * error;
}

void Brain::learn_traced(const Senses& inputs, int action, double target, double learning_rate, double discount,
                         double sharpness) {
    if (learning_rate <= 0) return;
    const double decay = discount * (TRACE_DECAY + (SMART_TRACE_DECAY - TRACE_DECAY) * sharpness);
    if (!traces) {
        traces.emplace();
        for (const Layer& layer : layers) {
            Layer trace;
            trace.reads = layer.reads;
            trace.weights.assign(layer.weights.size(), 0.0);
            traces->layers.push_back(std::move(trace));
        }
    }
    const std::vector<std::vector<double>> found = activations(inputs);
    const std::vector<double>& top = found.back();
    const double sum = dot(output_weights[action].data(), top.data(), HIDDEN_SIZE);
    const double error = std::max(-1.0, std::min(1.0, sum + output_biases[action] - target));
    for (auto& row : traces->output_weights) {
        for (double& trace : row) trace *= decay;
    }
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) traces->output_weights[action][neuron] += top[static_cast<std::size_t>(neuron)];
    for (double& trace : traces->output_biases) trace *= decay;
    traces->output_biases[action] += 1.0;
    std::vector<double> gradient(output_weights[action].begin(), output_weights[action].end());
    for (int index = depth() - 1; index >= 0; --index) {
        const Layer& layer = layers[static_cast<std::size_t>(index)];
        Layer& trace = traces->layers[static_cast<std::size_t>(index)];
        const std::vector<double>& output = found[static_cast<std::size_t>(index) + 1];
        const std::vector<double>& below = found[static_cast<std::size_t>(index)];
        std::array<double, HIDDEN_SIZE> delta{};
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
            const double value = output[static_cast<std::size_t>(neuron)];
            delta[neuron] = gradient[static_cast<std::size_t>(neuron)] * (1 - value * value);
        }
        if (index) {
            for (int input = 0; input < layer.reads; ++input) {
                double back = 0.0;
                for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) back += layer.row(neuron)[input] * delta[neuron];
                gradient[static_cast<std::size_t>(input)] = back;
            }
        }
        for (double& weight : trace.weights) weight *= decay;
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
            double* weights = trace.row(neuron);
            for (int input = 0; input < layer.reads; ++input) weights[input] += delta[neuron] * below[static_cast<std::size_t>(input)];
        }
        for (double& bias : trace.biases) bias *= decay;
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) trace.biases[neuron] += delta[neuron];
    }
    const double step = learning_rate * error;
    for (std::size_t index = 0; index < layers.size(); ++index) {
        Layer& layer = layers[index];
        const Layer& trace = traces->layers[index];
        for (std::size_t weight = 0; weight < layer.weights.size(); ++weight) layer.weights[weight] -= step * trace.weights[weight];
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) layer.biases[neuron] -= step * trace.biases[neuron];
    }
    for (int row = 0; row < ACTION_COUNT; ++row) {
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) output_weights[row][neuron] -= step * traces->output_weights[row][neuron];
    }
    for (int row = 0; row < ACTION_COUNT; ++row) output_biases[row] -= step * traces->output_biases[row];
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
    record["actions"] = json::array();
    for (const auto name : ACTIONS) record["actions"].push_back(std::string(name));
    record["layers"] = json::array();
    for (const Layer& layer : layers) {
        json entry = json::object();
        entry["weights"] = json::array();
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
            entry["weights"].push_back(rounded(layer.row(neuron), layer.row(neuron) + layer.reads));
        }
        entry["biases"] = rounded(layer.biases.begin(), layer.biases.end());
        record["layers"].push_back(std::move(entry));
    }
    record["output_weights"] = json::array();
    for (const auto& row : output_weights) record["output_weights"].push_back(rounded(row.begin(), row.end()));
    record["output_biases"] = rounded(output_biases.begin(), output_biases.end());
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
        if (rows.size() != width || biases.size() != width || width == 0 || width > HIDDEN_SIZE) {
            throw std::invalid_argument("The brain has " + std::to_string(rows.size()) + " neurons in a layer; this version uses up to " +
                                        std::to_string(HIDDEN_SIZE) + ", the same in every layer.");
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
        Layer layer;
        layer.reads = index == 0 ? INPUT_SIZE : HIDDEN_SIZE;
        layer.weights.assign(static_cast<std::size_t>(HIDDEN_SIZE) * layer.reads, 0.0);  // neurons past `width` stay idle
        for (std::size_t neuron = 0; neuron < width; ++neuron) {
            const auto& row = rows[neuron];
            for (int input = 0; input < layer.reads; ++input) {
                if (index == 0) {
                    const auto found = sense_at.find(std::string(SENSES[input]));
                    layer.row(static_cast<int>(neuron))[input] = found == sense_at.end() ? 0.0 : row[found->second];
                } else if (static_cast<std::size_t>(input) < width) {
                    layer.row(static_cast<int>(neuron))[input] = row[static_cast<std::size_t>(input)];
                }
            }
            layer.biases[neuron] = biases[neuron];
        }
        brain.layers.push_back(std::move(layer));
    }
    for (int action = 0; action < ACTION_COUNT; ++action) {
        const auto found = action_at.find(std::string(ACTIONS[action]));
        for (std::size_t neuron = 0; neuron < width; ++neuron) {
            brain.output_weights[action][neuron] = found == action_at.end() ? 0.0 : output_rows[found->second][neuron];
        }
        brain.output_biases[action] = found == action_at.end() ? 0.0 : output_bias[found->second];
    }
    return brain;
}

}  // namespace bitzkey
