#include "core/WorldFile.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "core/Rules.hpp"
#include "core/Simulation.hpp"

namespace bitzkey {

namespace {

using json = nlohmann::json;

static_assert(std::endian::native == std::endian::little, "world files hold little-endian numbers");

// Rules that say how this computer runs a world rather than how the world unfolds: loading keeps the user's own.
constexpr std::array<std::string_view, 3> MACHINE_RULES = {"USE_GPU", "HISTORY_TURNS", "HISTORY_MEMORY"};

std::string genome_text(Dna dna) {
    char text[25];
    std::snprintf(text, sizeof text, "%08X%016llX", dna.social, static_cast<unsigned long long>(dna.bits));
    return text;
}

Dna genome_from(const json& value) {
    const std::string text = value.get<std::string>();
    if (text.size() != 24) throw std::runtime_error("a genome isn't 24 hex digits");
    return Dna(std::stoull(text.substr(8), nullptr, 16), static_cast<std::uint32_t>(std::stoul(text.substr(0, 8), nullptr, 16)));
}

// A name read back, kept for the program's life: an animal's last action is a view onto one.
std::string_view interned(const std::string& name) {
    static std::set<std::string, std::less<>> names;
    return *names.insert(name).first;
}

template <class T>
json optional_value(const std::optional<T>& value) {
    return value ? json(*value) : json(nullptr);
}

std::optional<int> optional_int(const json& value) {
    return value.is_null() ? std::nullopt : std::optional<int>(value.get<int>());
}

// The numbers part, as it is written.
struct Numbers {
    std::vector<double> values;

    std::size_t add(const double* first, std::size_t count) {
        const std::size_t at = values.size();
        values.insert(values.end(), first, first + count);
        return at;
    }
    template <class Container>
    std::size_t add(const Container& container) {
        return add(container.data(), container.size());
    }
    void add_layers(const std::vector<Layer>& layers, const Brain::OutputWeights& output_weights,
                    const std::array<double, ACTION_COUNT>& output_biases) {
        for (const Layer& layer : layers) {
            add(layer.weights);
            add(layer.biases);
        }
        for (const auto& row : output_weights) add(row);
        add(output_biases);
    }
    json brain(const std::shared_ptr<Brain>& brain) {
        if (!brain) return nullptr;
        const std::size_t at = values.size();
        add_layers(brain->layers, brain->output_weights, brain->output_biases);
        if (brain->traces) add_layers(brain->traces->layers, brain->traces->output_weights, brain->traces->output_biases);
        return {{"at", at}, {"depth", brain->depth()}, {"traced", brain->traces.has_value()}};
    }
    json senses(const Senses& senses) { return add(senses); }
};

// The numbers part, as it is read back.
struct Reader {
    const double* numbers;
    std::size_t count;

    const double* take(std::size_t at, std::size_t wanted) const {
        if (at + wanted > count) throw std::runtime_error("The world file's numbers are cut short.");
        return numbers + at;
    }
    void read_layers(std::size_t& at, int depth, std::vector<Layer>& layers, Brain::OutputWeights& output_weights,
                     std::array<double, ACTION_COUNT>& output_biases) const {
        layers.clear();
        for (int index = 0; index < depth; ++index) {
            Layer layer;
            layer.reads = index == 0 ? INPUT_SIZE : HIDDEN_SIZE;
            const std::size_t size = static_cast<std::size_t>(HIDDEN_SIZE) * layer.reads;
            const double* weights = take(at, size);
            layer.weights.assign(weights, weights + size);
            at += size;
            std::memcpy(layer.biases.data(), take(at, HIDDEN_SIZE), sizeof(double) * HIDDEN_SIZE);
            at += HIDDEN_SIZE;
            layers.push_back(std::move(layer));
        }
        for (auto& row : output_weights) {
            std::memcpy(row.data(), take(at, HIDDEN_SIZE), sizeof(double) * HIDDEN_SIZE);
            at += HIDDEN_SIZE;
        }
        std::memcpy(output_biases.data(), take(at, ACTION_COUNT), sizeof(double) * ACTION_COUNT);
        at += ACTION_COUNT;
    }
    std::shared_ptr<Brain> brain(const json& record) const {
        if (record.is_null()) return nullptr;
        auto brain = std::make_shared<Brain>();
        std::size_t at = record.at("at").get<std::size_t>();
        const int depth = record.at("depth").get<int>();
        read_layers(at, depth, brain->layers, brain->output_weights, brain->output_biases);
        if (record.at("traced").get<bool>()) {
            Brain::Traces traces;
            read_layers(at, depth, traces.layers, traces.output_weights, traces.output_biases);
            brain->traces = std::move(traces);
        }
        return brain;
    }
    Senses senses(const json& at) const {
        Senses senses{};
        std::memcpy(senses.data(), take(at.get<std::size_t>(), INPUT_SIZE), sizeof(double) * INPUT_SIZE);
        return senses;
    }
};

json mind_record(const Mind& mind) { return json(std::vector<double>(mind.begin(), mind.end())); }

Mind mind_from(const json& values) {
    Mind mind{};
    for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) mind[trait] = values.at(trait).get<double>();
    return mind;
}

json agent_record(const Bitzkey& agent, Numbers& numbers) {
    json record = {
        {"identifier", agent.identifier}, {"dna", genome_text(agent.dna)}, {"x", agent.x}, {"y", agent.y},
        {"hunger", agent.hunger}, {"health", agent.health}, {"age", agent.age}, {"cooldown", agent.cooldown},
        {"last_action", std::string(agent.last_action)}, {"brain", numbers.brain(agent.brain)},
        {"generation", agent.generation}, {"children", agent.children}, {"died_turn", optional_value(agent.died_turn)},
        {"assailant", optional_value(agent.assailant)}, {"panicked", agent.panicked}, {"primal_fear", agent.primal_fear},
        {"rivalry", agent.rivalry}, {"own_rivalry", agent.own_rivalry}, {"hunts_grazers", agent.hunts_grazers},
        {"hunts_omnivores", agent.hunts_omnivores}, {"hunts_predators", agent.hunts_predators}, {"guarded", agent.guarded},
        {"alarm", optional_value(agent.alarm)},
        {"kill_alert", agent.kill_alert ? json::array({agent.kill_alert->x, agent.kill_alert->y}) : json(nullptr)},
        {"seen_success", agent.seen_success}, {"seen_failure", agent.seen_failure},
        {"herd_assailant", optional_value(agent.herd_assailant)},
        {"food_memory", agent.food_memory ? json::array({agent.food_memory->x, agent.food_memory->y, agent.food_memory->turn})
                                          : json(nullptr)},
        {"danger_memory", agent.danger_memory
                              ? json::array({agent.danger_memory->x, agent.danger_memory->y, agent.danger_memory->turn})
                              : json(nullptr)},
        {"meat", agent.meat}, {"meat_fill", agent.meat_fill}, {"population", agent.population},
        {"cost_share", agent.cost_share}, {"glut", agent.glut}, {"rot", agent.rot}, {"portions", agent.portions},
        {"due_turn", optional_value(agent.due_turn)}, {"mind", mind_record(agent.mind)},
        {"last_senses", agent.has_last_senses ? numbers.senses(agent.last_senses) : json(nullptr)},
        {"last_choice", agent.last_choice}, {"last_wellbeing", agent.last_wellbeing}, {"reward", agent.reward},
        {"last_comfort", agent.last_comfort},
    };
    json trail = json::array();
    for (const auto& [x, y] : agent.trail) trail.push_back({x, y});
    record["trail"] = std::move(trail);
    json memories = json::array();
    for (const Bitzkey::Memory& memory : agent.memories) {
        memories.push_back({numbers.senses(memory.senses), memory.choice, memory.target, memory.weight});
    }
    record["memories"] = std::move(memories);
    json womb = json::array();
    for (const Bitzkey::Young& young : agent.womb) {
        womb.push_back({genome_text(young.dna), mind_record(young.mind), numbers.brain(young.brain), young.energy, young.generation});
    }
    record["womb"] = std::move(womb);
    json odds = json::object();
    for (int action = 0; action < ACTION_COUNT; ++action) {
        if (agent.odds_offered & (1U << action)) odds[std::string(ACTIONS[action])] = agent.odds[action];
    }
    record["odds"] = std::move(odds);
    return record;
}

int action_index(const std::string& name) {
    for (int action = 0; action < ACTION_COUNT; ++action) {
        if (ACTIONS[action] == name) return action;
    }
    throw std::runtime_error("an unknown action, " + name);
}

std::unique_ptr<Bitzkey> agent_from(const json& record, const Reader& numbers) {
    auto agent = std::make_unique<Bitzkey>(record.at("identifier").get<int>(), genome_from(record.at("dna")),
                                           record.at("x").get<int>(), record.at("y").get<int>());
    agent->hunger = record.at("hunger").get<double>();
    agent->health = record.at("health").get<double>();
    agent->age = record.at("age").get<int>();
    agent->cooldown = record.at("cooldown").get<int>();
    agent->last_action = interned(record.at("last_action").get<std::string>());
    agent->brain = numbers.brain(record.at("brain"));
    agent->generation = record.at("generation").get<int>();
    agent->children = record.at("children").get<int>();
    agent->died_turn = optional_int(record.at("died_turn"));
    agent->assailant = optional_int(record.at("assailant"));
    agent->panicked = record.at("panicked").get<bool>();
    agent->primal_fear = record.at("primal_fear").get<bool>();
    agent->rivalry = record.at("rivalry").get<double>();
    agent->own_rivalry = record.at("own_rivalry").get<double>();
    agent->hunts_grazers = record.at("hunts_grazers").get<bool>();
    agent->hunts_omnivores = record.at("hunts_omnivores").get<bool>();
    agent->hunts_predators = record.at("hunts_predators").get<bool>();
    agent->guarded = record.at("guarded").get<bool>();
    agent->alarm = optional_int(record.at("alarm"));
    if (const json& alert = record.at("kill_alert"); !alert.is_null()) agent->kill_alert = Cell{alert.at(0).get<int>(), alert.at(1).get<int>()};
    agent->seen_success = record.at("seen_success").get<double>();
    agent->seen_failure = record.at("seen_failure").get<double>();
    agent->herd_assailant = optional_int(record.at("herd_assailant"));
    for (const auto& [name, place] : {std::pair{"food_memory", &agent->food_memory}, std::pair{"danger_memory", &agent->danger_memory}}) {
        if (const json& value = record.at(name); !value.is_null()) *place = Place{value.at(0).get<int>(), value.at(1).get<int>(), value.at(2).get<int>()};
    }
    agent->meat = record.at("meat").get<double>();
    agent->meat_fill = record.at("meat_fill").get<double>();
    agent->population = record.at("population").get<int>();
    agent->cost_share = record.at("cost_share").get<double>();
    agent->glut = record.at("glut").get<double>();
    agent->rot = record.at("rot").get<double>();
    agent->portions = record.at("portions").get<int>();
    for (const json& cell : record.at("trail")) agent->trail.emplace_back(cell.at(0).get<int>(), cell.at(1).get<int>());
    for (const json& memory : record.at("memories")) {
        agent->memories.push_back({numbers.senses(memory.at(0)), memory.at(1).get<int>(), memory.at(2).get<double>(), memory.at(3).get<double>()});
    }
    for (const json& young : record.at("womb")) {
        agent->womb.push_back({genome_from(young.at(0)), mind_from(young.at(1)), numbers.brain(young.at(2)), young.at(3).get<double>(),
                               young.at(4).get<int>()});
    }
    agent->due_turn = optional_int(record.at("due_turn"));
    agent->mind = mind_from(record.at("mind"));
    if (const json& senses = record.at("last_senses"); !senses.is_null()) {
        agent->has_last_senses = true;
        agent->last_senses = numbers.senses(senses);
    }
    agent->last_choice = record.at("last_choice").get<int>();
    agent->last_wellbeing = record.at("last_wellbeing").get<double>();
    agent->reward = record.at("reward").get<double>();
    agent->last_comfort = record.at("last_comfort").get<double>();
    for (const auto& [name, chance] : record.at("odds").items()) {
        const int action = action_index(name);
        agent->odds[action] = chance.get<double>();
        agent->odds_offered |= 1U << action;
    }
    agent->derive();  // under the rules as they now stand, at its age
    return agent;
}

Species species_named(const std::string& name) {
    const auto species = species_from_name(name);
    if (!species) throw std::runtime_error("an unknown species, " + name);
    return *species;
}

json names(auto&& list) {
    json result = json::array();
    for (const auto& name : list) result.push_back(std::string(name));
    return result;
}

}  // namespace

std::string WorldFile::bytes(Simulation& simulation) {
    simulation.sync_brains();
    Numbers numbers;
    const PyRandom::State& random = simulation.random_.getstate();
    json grass = json::array();
    for (const int tufts : simulation.grass_) grass.push_back(tufts);
    json seeds = json::array();
    for (const Simulation::Seed& seed : simulation.seeds_) seeds.push_back({seed.x, seed.y, genome_text(seed.dna), seed.generation, seed.sown});
    json death_causes = json::object();
    for (std::size_t species = 0; species < simulation.death_causes_.size(); ++species) {
        if (simulation.death_causes_[species].empty()) continue;
        json causes = json::object();
        for (const auto& [cause, count] : simulation.death_causes_[species]) causes[cause] = count;
        death_causes[std::string(SPECIES[species])] = std::move(causes);
    }
    json insights = json::array();
    for (const Insights::Entry& entry : simulation.insights_.entries()) {
        insights.push_back({std::string(species_name(entry.species)),
                            entry.trait ? json(std::string(insight_trait_name(*entry.trait))) : json(nullptr),
                            entry.trait ? json(std::string(level_name(entry.level))) : json(nullptr),
                            std::string(ACTIONS[entry.action]), entry.offered, entry.chosen});
    }
    json snapshots = json::array();
    for (const Census::Snapshot& snapshot : simulation.census_.snapshots()) snapshots.push_back({snapshot.turn, snapshot.identifiers});
    json lineage = json::array();
    for (const Dna& dna : simulation.lineage_) lineage.push_back(genome_text(dna));
    json death_records = json::array();
    std::vector<std::pair<int, const Death*>> deaths;  // by identifier, the same however the records are kept
    for (const auto& [identifier, death] : simulation.death_records_) deaths.emplace_back(identifier, &death);
    std::sort(deaths.begin(), deaths.end());
    for (const auto& [identifier, held] : deaths) {
        const Death& death = *held;
        const bool killed = death.killer >= 0;
        death_records.push_back({identifier, death.cause, std::string(species_name(death.species)), death.turn,
                                 killed ? json(death.killer) : json(nullptr),
                                 killed ? json(std::string(species_name(death.killer_species))) : json(nullptr),
                                 death.activity.empty() ? json(nullptr) : json(death.activity)});
    }
    json agents = json::array();
    for (const auto& agent : simulation.agents_) agents.push_back(agent_record(*agent, numbers));
    json rules = json::object();
    for (const auto& [key, value] : rules::current()) {
        const rules::Rule* rule = rules::find(key);
        rules[key] = rule && rule->integer ? json(static_cast<long long>(value)) : json(value);
    }
    const json record = {
        {"format", WORLD_FORMAT}, {"senses", names(SENSES)}, {"actions", names(ACTIONS)}, {"temperament", names(TEMPERAMENT)},
        {"settings", {{"width", simulation.width_}, {"height", simulation.height_}, {"population_cap", optional_value(simulation.population_cap_)},
                      {"mind_mutation", simulation.mind_mutation_}, {"body_mutation", simulation.body_mutation_},
                      {"year_length", YEAR_LENGTH}, {"initial_grass", simulation.initial_grass_}}},
        {"rules", std::move(rules)},
        {"turn", simulation.turn_}, {"deaths", simulation.deaths_}, {"next_identifier", simulation.next_identifier_},
        {"death_causes", std::move(death_causes)},
        {"random", {{"mt", random.mt}, {"index", random.index}, {"gauss_next", optional_value(random.gauss_next)}}},
        {"grass", std::move(grass)}, {"seeds", std::move(seeds)}, {"insights", std::move(insights)},
        {"census", {{"stride", simulation.census_.stride()}, {"snapshots", std::move(snapshots)}}},
        {"lineage", std::move(lineage)}, {"death_records", std::move(death_records)}, {"agents", std::move(agents)},
    };
    const std::string text = record.dump();
    std::string data(WORLD_MAGIC);
    const std::uint64_t length = text.size();
    data.append(reinterpret_cast<const char*>(&length), sizeof length);
    data += text;
    data.append(reinterpret_cast<const char*>(numbers.values.data()), numbers.values.size() * sizeof(double));
    return data;
}

std::unique_ptr<Simulation> WorldFile::from_bytes(std::string_view data) {
    if (!data.starts_with(WORLD_MAGIC)) throw std::runtime_error("This isn't a Bitzkey world file.");
    const std::size_t start = WORLD_MAGIC.size() + sizeof(std::uint64_t);
    if (data.size() < start) throw std::runtime_error("The world file is cut short.");
    std::uint64_t length = 0;
    std::memcpy(&length, data.data() + WORLD_MAGIC.size(), sizeof length);
    if (data.size() < start + length || (data.size() - start - length) % sizeof(double)) {
        throw std::runtime_error("The world file is cut short.");
    }
    json record;
    try {
        record = json::parse(data.substr(start, length));
    } catch (const json::exception& error) {
        throw std::runtime_error(std::string("The world file is damaged (") + error.what() + ").");
    }
    std::vector<double> numbers((data.size() - start - length) / sizeof(double));
    std::memcpy(numbers.data(), data.data() + start + length, numbers.size() * sizeof(double));
    const Reader reader{numbers.data(), numbers.size()};
    try {
        if (record.value("format", 0) != WORLD_FORMAT) {
            throw std::runtime_error("This world file is format " + record.value("format", json(nullptr)).dump() + "; this version reads format " +
                                     std::to_string(WORLD_FORMAT) + ".");
        }
        if (record.at("senses") != names(SENSES) || record.at("actions") != names(ACTIONS) || record.at("temperament") != names(TEMPERAMENT)) {
            throw std::runtime_error("This world was saved by a version of Bitzkey whose brains sense or act differently.");
        }
        rules::Values values;
        for (const auto& [key, value] : record.at("rules").items()) values[key] = value.get<double>();
        const rules::Values now = rules::current();
        for (const std::string_view key : MACHINE_RULES) values[std::string(key)] = now.find(key)->second;
        rules::apply(values);
        const json& settings = record.at("settings");
        Simulation::Settings made;
        made.width = settings.at("width").get<int>();
        made.height = settings.at("height").get<int>();
        made.population = 0;
        made.seed = 0;
        made.grass_density = 0;
        made.population_cap = optional_int(settings.at("population_cap"));
        made.mind_mutation = 100 * settings.at("mind_mutation").get<double>();
        made.body_mutation = 100 * settings.at("body_mutation").get<double>();
        made.year_length = settings.at("year_length").get<int>();
        auto simulation = std::make_unique<Simulation>(made);
        Simulation& world = *simulation;
        world.mind_mutation_ = settings.at("mind_mutation").get<double>();
        world.body_mutation_ = settings.at("body_mutation").get<double>();
        world.initial_grass_ = settings.at("initial_grass").get<int>();
        world.grass_regrowth_ = world.initial_grass_ * GRASS_REGROWTH;
        world.turn_ = record.at("turn").get<int>();
        world.deaths_ = record.at("deaths").get<int>();
        world.next_identifier_ = record.at("next_identifier").get<int>();
        world.death_causes_ = {};
        for (const auto& [species, causes] : record.at("death_causes").items()) {
            for (const auto& [cause, count] : causes.items()) {
                world.death_causes_[static_cast<std::size_t>(species_named(species))][cause] = count.get<int>();
            }
        }
        PyRandom::State random;
        const json& state = record.at("random");
        for (std::size_t index = 0; index < random.mt.size(); ++index) random.mt[index] = state.at("mt").at(index).get<std::uint32_t>();
        random.index = state.at("index").get<int>();
        if (!state.at("gauss_next").is_null()) random.gauss_next = state.at("gauss_next").get<double>();
        world.random_.setstate(random);
        const json& grass = record.at("grass");
        if (grass.size() != world.grass_.size()) throw std::runtime_error("The world file's grass doesn't fit its map.");
        for (std::size_t index = 0; index < grass.size(); ++index) world.grass_[index] = grass[index].get<int>();
        world.seeds_.clear();
        for (const json& seed : record.at("seeds")) {
            world.seeds_.push_back({seed.at(0).get<int>(), seed.at(1).get<int>(), genome_from(seed.at(2)),
                                    seed.at(3).get<int>(), seed.at(4).get<int>()});
        }
        world.insights_ = Insights();
        for (const json& entry : record.at("insights")) {
            std::optional<int> trait;
            Level level = Level::Low;
            if (!entry.at(1).is_null()) {
                const std::string name = entry.at(1).get<std::string>();
                for (int index = 0; index < INSIGHT_TRAIT_COUNT; ++index) {
                    if (insight_trait_name(index) == name) trait = index;
                }
                if (!trait) throw std::runtime_error("an unknown trait, " + name);
                const std::string band = entry.at(2).get<std::string>();
                level = band == "low" ? Level::Low : band == "mid" ? Level::Mid : Level::High;
            }
            world.insights_.set(species_named(entry.at(0).get<std::string>()), trait, level, action_index(entry.at(3).get<std::string>()),
                                entry.at(4).get<double>(), entry.at(5).get<double>());
        }
        std::vector<Census::Snapshot> snapshots;
        for (const json& snapshot : record.at("census").at("snapshots")) {
            snapshots.push_back({snapshot.at(0).get<int>(), snapshot.at(1).get<std::vector<int>>()});
        }
        world.census_.restore(record.at("census").at("stride").get<int>(), std::move(snapshots));
        world.lineage_.clear();
        for (const json& dna : record.at("lineage")) world.lineage_.push_back(genome_from(dna));
        world.death_records_.clear();
        for (const json& death : record.at("death_records")) {
            Death made_death{death.at(1).get<std::string>(), species_named(death.at(2).get<std::string>()), death.at(3).get<int>()};
            if (!death.at(4).is_null()) {
                made_death.killer = death.at(4).get<int>();
                made_death.killer_species = species_named(death.at(5).get<std::string>());
            }
            if (!death.at(6).is_null()) made_death.activity = death.at(6).get<std::string>();
            world.death_records_[death.at(0).get<int>()] = std::move(made_death);
        }
        world.agents_.clear();
        for (const json& agent : record.at("agents")) {
            world.agents_.push_back(agent_from(agent, reader));
            Bitzkey& made_agent = *world.agents_.back();
            if (world.gpu_ && made_agent.brain) made_agent.gpu = world.gpu_->attach(*made_agent.brain);
        }
        world.update_climate();
        world.index_cells();
        // What each knew as of its turn's start, which indexing the cells afresh would count again as things now stand.
        for (std::size_t index = 0; index < world.agents_.size(); ++index) {
            const json& agent = record.at("agents").at(index);
            world.agents_[index]->population = agent.at("population").get<int>();
            world.agents_[index]->cost_share = agent.at("cost_share").get<double>();
            world.agents_[index]->glut = agent.at("glut").get<double>();
            world.agents_[index]->guarded = agent.at("guarded").get<bool>();
        }
        world.history_.clear();
        world.history_bytes_ = 0;
        world.record();
        return simulation;
    } catch (const json::exception& error) {
        throw std::runtime_error(std::string("The world file is incomplete (") + error.what() + ").");
    }
}

void WorldFile::save(Simulation& simulation, const std::filesystem::path& path) {
    const std::string data = bytes(simulation);
    std::ofstream file(path, std::ios::binary);
    if (!file || !file.write(data.data(), static_cast<std::streamsize>(data.size()))) {
        throw std::runtime_error("couldn't write it");
    }
}

std::unique_ptr<Simulation> WorldFile::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("can't open it");
    std::stringstream data;
    data << file.rdbuf();
    return from_bytes(data.str());
}

}  // namespace bitzkey
