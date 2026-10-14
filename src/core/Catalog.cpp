#include "core/Catalog.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

#include "core/PyFormat.hpp"

namespace bitzkey::catalog {

namespace {

std::string now_text() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M", &local);
    return buffer;
}

std::string name_of(const std::filesystem::path& path) { return path.filename().string(); }

}  // namespace

Entry entry_for(const Bitzkey& agent, int turn) {
    Entry record = bitzkey_record(agent);
    Entry state = Entry::object();
    state["identifier"] = agent.identifier;
    state["turn"] = turn;
    state["added"] = now_text();
    state["hunger"] = py_round(agent.hunger, STATE_DIGITS);
    state["health"] = py_round(agent.health, STATE_DIGITS);
    state["age"] = agent.age;
    state["generation"] = agent.generation;
    state["children"] = agent.children;
    state["last_action"] = std::string(agent.last_action);
    Entry odds = Entry::object();
    for (int action = 0; action < ACTION_COUNT; ++action) {
        if (agent.odds_offered & (1U << action)) {
            odds[std::string(ACTIONS[static_cast<std::size_t>(action)])] = py_round(agent.odds[static_cast<std::size_t>(action)], STATE_DIGITS);
        }
    }
    state["odds"] = std::move(odds);
    record["catalog"] = std::move(state);
    return record;
}

std::vector<Entry> load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::invalid_argument("The catalog " + name_of(path) + " can't be read.");
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Entry value;
    try {
        value = Entry::parse(text);
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument("The catalog " + name_of(path) + " can't be read: " + error.what());
    }
    std::vector<Entry> entries;
    if (value.is_array()) {
        for (auto& entry : value) entries.push_back(std::move(entry));
    } else {
        entries.push_back(std::move(value));
    }
    for (const Entry& entry : entries) {
        if (!entry.is_object() || !entry.contains("ring") || !entry.contains("dot")) {
            throw std::invalid_argument("The catalog " + name_of(path) + " holds something that isn't a Bitzkey.");
        }
    }
    return entries;
}

void save(const std::vector<Entry>& entries, const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("Couldn't write the catalog " + name_of(path) + ".");
    file << (entries.empty() ? std::string("[]") : format_records(entries));
}

int add(const std::vector<Entry>& entries, const std::filesystem::path& path) {
    std::vector<Entry> kept = load(path);
    std::vector<nlohmann::json> known;  // compared regardless of the order of their keys
    for (const Entry& entry : kept) known.emplace_back(nlohmann::json::parse(entry.dump()));
    int added = 0;
    for (const Entry& entry : entries) {
        const nlohmann::json plain = nlohmann::json::parse(entry.dump());
        if (std::find(known.begin(), known.end(), plain) != known.end()) continue;
        kept.push_back(entry);
        known.push_back(plain);
        ++added;
    }
    if (added) save(kept, path);
    return added;
}

void remove(std::size_t index, const std::filesystem::path& path) {
    std::vector<Entry> entries = load(path);
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
    save(entries, path);
}

Imported imported(const Entry& entry) { return import_bitzkeys(entry.dump()).front(); }

Bitzkey inspected(const Entry& entry) {
    const Imported bitzkey = imported(entry);
    const Entry state = entry.contains("catalog") ? entry["catalog"] : Entry::object();
    Bitzkey agent(state.value("identifier", 0), bitzkey.dna, 0, 0);
    if (bitzkey.brain) agent.brain = std::make_shared<Brain>(*bitzkey.brain);
    if (bitzkey.mind) {
        for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) {
            if (bitzkey.mind->traits[static_cast<std::size_t>(trait)]) agent.mind[trait] = *bitzkey.mind->traits[static_cast<std::size_t>(trait)];
        }
    }
    agent.health = state.value("health", agent.max_health);
    agent.hunger = state.value("hunger", 0.0);
    agent.age = state.value("age", 0);
    agent.generation = state.value("generation", 0);
    agent.children = state.value("children", 0);
    agent.last_action = kept_text(state.value("last_action", std::string("catalogued")));
    if (state.contains("odds")) {
        for (const auto& [name, chance] : state["odds"].items()) {
            const int action = action_index(name);
            if (action < 0 || !chance.is_number()) continue;
            agent.odds[static_cast<std::size_t>(action)] = chance.get<double>();
            agent.odds_offered |= 1U << action;
        }
    }
    return agent;
}

std::string describe(const Entry& entry) {
    const Entry state = entry.contains("catalog") ? entry["catalog"] : Entry::object();
    const std::string species(SPECIES[static_cast<std::size_t>(decode_body(imported(entry).dna)[SpeciesTrait])]);
    std::vector<std::string> parts;
    if (state.contains("identifier") && state["identifier"].is_number_integer()) {
        char name[32];
        std::snprintf(name, sizeof name, "#%03d ", state["identifier"].get<int>());
        parts.push_back(name + species);
    } else {
        parts.push_back(species);
    }
    if (state.contains("turn")) parts.push_back("turn " + std::to_string(state["turn"].get<int>()));
    if (state.contains("added")) parts.push_back(state["added"].get<std::string>());
    std::string line;
    for (const std::string& part : parts) line += (line.empty() ? "" : "   ") + part;
    return line;
}

}  // namespace bitzkey::catalog
