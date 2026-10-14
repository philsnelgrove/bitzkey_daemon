#include "daemon/Views.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <tuple>

#include "core/Exchange.hpp"
#include "core/Insights.hpp"
#include "core/PyFormat.hpp"

namespace bitzkey::daemon {

namespace {

std::string format(const char* pattern, auto... values) {
    char buffer[512];
    std::snprintf(buffer, sizeof buffer, pattern, values...);
    return buffer;
}

Json rgb(const Rgb& colour) { return Json::array({colour.r, colour.g, colour.b}); }

template <typename T>
void put(std::string& out, T value) {
    char bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));  // every platform the daemon runs on is little-endian
    out.append(bytes, sizeof(T));
}

// ---------------------------------------------------------------------------------------------
// Exemplars, as the window app's cards: the living animal that stands out most by a statistic,
// highest, or lowest where `lowest`.

enum class StatKind { Mind, Body, Home, State };
enum StateStat { Age, Children, Generation, Hunger, Health, Lifespan, Burn, Stomach, Depth, Herd, Changed };
struct ExemplarStat {
    const char* title;
    StatKind kind;
    int index;
    bool lowest;
};
constexpr std::array<ExemplarStat, 50> EXEMPLAR_STATS = {{
    {"SMARTEST", StatKind::Mind, Intellect, false}, {"MOST AGGRESSIVE", StatKind::Mind, Aggression, false},
    {"MOST AMOROUS", StatKind::Mind, Libido, false}, {"MOST SOCIABLE", StatKind::Mind, Sociability, false},
    {"MOST CURIOUS", StatKind::Mind, Curiosity, false}, {"MOST FEARFUL", StatKind::Mind, Fear, false},
    {"GREATEST APPETITE", StatKind::Mind, Appetite, false}, {"MOST PATIENT", StatKind::Mind, Patience, false},
    {"MOST EMPATHETIC", StatKind::Mind, Empathy, false}, {"MOST TOLERANT", StatKind::Mind, Tolerance, false},
    {"BEST MEMORY", StatKind::Mind, Memory, false}, {"MOST RESILIENT", StatKind::Mind, Resilience, false},
    {"MOODIEST", StatKind::Mind, Moodiness, false}, {"MOST LOYAL", StatKind::Mind, Loyalty, false},
    {"MOST DOMINANT", StatKind::Mind, Dominance, false}, {"MOST TRUSTING", StatKind::Mind, Trust, false},
    {"MOST CLANNISH", StatKind::Mind, Kinship, false},
    {"SIMPLEST", StatKind::Mind, Intellect, true}, {"GENTLEST", StatKind::Mind, Aggression, true},
    {"BRAVEST", StatKind::Mind, Fear, true}, {"MOST SOLITARY", StatKind::Mind, Sociability, true},
    {"LARGEST", StatKind::Body, Size, false}, {"SMALLEST", StatKind::Body, Size, true},
    {"STRONGEST", StatKind::Body, Strength, false}, {"BEST ARMOURED", StatKind::Body, Armour, false},
    {"BEST ARMED", StatKind::Body, Weapons, false}, {"FASTEST", StatKind::Body, Speed, false},
    {"SLOWEST", StatKind::Body, Speed, true}, {"SHARPEST-EYED", StatKind::Body, Sight, false},
    {"MOST FERTILE", StatKind::Body, Fertility, false}, {"BIGGEST LITTERS", StatKind::Body, Litter, false},
    {"BEST SCAVENGER", StatKind::Body, Carrion, false},
    {"HOTTEST HOME", StatKind::Home, 0, true}, {"COLDEST HOME", StatKind::Home, 0, false},
    {"OLDEST", StatKind::State, Age, false}, {"YOUNGEST", StatKind::State, Age, true},
    {"MOST CHILDREN", StatKind::State, Children, false}, {"LATEST GENERATION", StatKind::State, Generation, false},
    {"BEST FED", StatKind::State, Hunger, true}, {"HUNGRIEST", StatKind::State, Hunger, false},
    {"HEALTHIEST", StatKind::State, Health, false}, {"MOST WOUNDED", StatKind::State, Health, true},
    {"LONGEST LIFESPAN", StatKind::State, Lifespan, false}, {"SHORTEST LIFESPAN", StatKind::State, Lifespan, true},
    {"FASTEST METABOLISM", StatKind::State, Burn, false}, {"SLOWEST METABOLISM", StatKind::State, Burn, true},
    {"BIGGEST STOMACH", StatKind::State, Stomach, false}, {"DEEPEST BRAIN", StatKind::State, Depth, false},
    {"MOST HERD-MINDED", StatKind::State, Herd, false}, {"MOST CHANGED BY EXPERIENCE", StatKind::State, Changed, false},
}};
constexpr std::array<const char*, 4> HOMES = {"hot", "medium", "cold", "very cold"};

double exemplar_value(const Bitzkey& agent, const ExemplarStat& stat) {
    switch (stat.kind) {
        case StatKind::Mind: return agent.inborn[static_cast<std::size_t>(stat.index)];
        case StatKind::Body: return agent.body[static_cast<std::size_t>(stat.index)];
        case StatKind::Home: return decode_climate(agent.dna).temperature;
        case StatKind::State: break;
    }
    switch (stat.index) {
        case Age: return agent.age;
        case Children: return agent.children;
        case Generation: return agent.generation;
        case Hunger: return agent.hunger;
        case Health: return agent.health / agent.max_health;
        case Lifespan: return agent.lifespan;
        case Burn: return agent.burn;
        case Stomach: return agent.stomach;
        case Depth: return agent.brain ? agent.brain->depth() : 0;
        case Herd: return agent.herd_instinct();
        default: {  // how far experience has moved its temperament from what it was born with
            std::vector<double> shifts;
            for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) {
                shifts.push_back(std::abs(agent.mind[static_cast<std::size_t>(trait)] - agent.inborn[static_cast<std::size_t>(trait)]));
            }
            return py_sum(shifts);
        }
    }
}

std::string exemplar_text(const Bitzkey& agent, const ExemplarStat& stat) {
    const double value = exemplar_value(agent, stat);
    const int whole = static_cast<int>(value);
    switch (stat.kind) {
        case StatKind::Mind: return std::string(TEMPERAMENT[static_cast<std::size_t>(stat.index)]) + " " + std::to_string(whole);
        case StatKind::Body: return std::string(BODY_TRAITS[static_cast<std::size_t>(stat.index)]) + " " + std::to_string(whole);
        case StatKind::Home: return std::string("home ") + HOMES[static_cast<std::size_t>(whole)];
        case StatKind::State: break;
    }
    switch (stat.index) {
        case Age: return "age " + std::to_string(whole);
        case Children: return std::to_string(whole) + " children";
        case Generation: return "generation " + std::to_string(whole);
        case Hunger: return "hunger " + std::to_string(whole) + "%";
        case Health: return "health " + std::to_string(static_cast<int>(value * 100)) + "%";
        case Lifespan: return "lifespan " + std::to_string(whole) + " turns";
        case Burn: return format("burn %.2f", value);
        case Stomach: return format("stomach %.2f", value);
        case Depth: return "brain " + std::to_string(whole) + " deep";
        case Herd: return format("herd instinct %.2f", value);
        default: return format("changed %.1f", value);
    }
}

double corpse_opacity(const Bitzkey& agent, int turn) {
    if (!agent.died_turn) return 1.0;
    return std::max(0.0, 1 - static_cast<double>(turn - *agent.died_turn) / CORPSE_TURNS);
}

// ---------------------------------------------------------------------------------------------
// The setup panel's settings, as the window app's.

struct Setting {
    const char* key;
    const char* label;
    const char* hint;
    int minimum, maximum, step;
    bool optional = false;
    const char* blank = "";
};
const std::array<Setting, 9> SETTINGS = {{
    {"width", "Grid width", "cells across the world", 10, MAX_GRID, 10},
    {"height", "Grid height", "cells down the world", 10, MAX_GRID, 10},
    {"population", "Bitzkeys", "spawned at the start, a random mix of species", 0, MAX_POPULATION, 10},
    {"grass_density", "Grass density", "percent of cells with grass at the start; regrowth scales too", 0, 100, 1},
    {"seed", "Random seed", "the same seed replays the same world; blank for a new one", 0, 999'999'999, 1, true, "random"},
    {"population_cap", "Population cap", "most animals alive at once; blank lets crowding alone decide", 10, 99'999, 100, true,
     "no limit"},
    {"mind_mutation", "Mind mutation", "mind mutations a birth carries: 0 none, 50 about 1.5, 100 three", 0, 100, 5},
    {"body_mutation", "Body mutation", "percent chance a birth flips one body gene: 0 stable, 100 always", 0, 100, 5},
    {"year_length", "Year length", "turns in a year, of four seasons; lifespans are counted in years", 20, 3600, 20},
}};

Json setting_value(const Simulation::Settings& settings, std::string_view key) {
    if (key == "width") return settings.width;
    if (key == "height") return settings.height;
    if (key == "population") return settings.population;
    if (key == "grass_density") return static_cast<int>(settings.grass_density);
    if (key == "seed") return settings.seed ? Json(*settings.seed) : Json(nullptr);
    if (key == "population_cap") return settings.population_cap ? Json(*settings.population_cap) : Json(nullptr);
    if (key == "mind_mutation") return static_cast<int>(settings.mind_mutation);
    if (key == "body_mutation") return static_cast<int>(settings.body_mutation);
    return settings.year_length;
}

std::string import_summary(const std::vector<Imported>& bitzkeys) {
    const std::size_t kept = std::min<std::size_t>(bitzkeys.size(), MAX_IMPORTED);
    std::map<std::string, int> species;
    int brains = 0, minds = 0;
    for (std::size_t index = 0; index < kept; ++index) {
        species[std::string(SPECIES[static_cast<std::size_t>(decode_body(bitzkeys[index].dna)[SpeciesTrait])])] += 1;
        brains += bitzkeys[index].brain != nullptr;
        minds += bitzkeys[index].mind.has_value();
    }
    std::string parts;
    for (const auto& [name, count] : species) {
        if (!parts.empty()) parts += ", ";
        parts += std::to_string(count) + " " + name + (count != 1 ? "s" : "");
    }
    std::string summary = std::to_string(kept) + " Bitzkey" + (kept != 1 ? "s" : "") + " ready to import (" + parts + "; " +
                          std::to_string(brains) + " with saved brains, " + std::to_string(minds) +
                          " with saved temperaments), each spawning as a group of " + std::to_string(SPAWN_GROUP[0]) + "-" +
                          std::to_string(SPAWN_GROUP[1]);
    if (bitzkeys.size() > static_cast<std::size_t>(MAX_IMPORTED)) {
        summary += ". Only the first " + std::to_string(MAX_IMPORTED) + " of " + std::to_string(bitzkeys.size()) + " are used";
    }
    return summary;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// The header and sidebar

std::string end_note(const Simulation& simulation) {
    const EndReason reason = simulation.end_reason();
    if (!simulation.ended()) return "";
    const std::string cause = reason == EndReason::Extinct ? "every animal has died" : "the world is full, with no cell left free";
    return format("The simulation ended at turn %d: %s.", simulation.turn(), cause.c_str());
}

Json state(World& world) {
    Simulation& simulation = world.simulation();
    const auto alive = simulation.living();
    std::array<int, 4> counts{};
    for (const Bitzkey* agent : alive) counts[static_cast<std::size_t>(agent->species)] += 1;
    const int turn = simulation.turn();
    Json result = {
        {"version", world.version()},
        {"world", world.world_number()},
        {"width", simulation.width()},
        {"height", simulation.height()},
        {"turn", turn},
        {"frontier", simulation.frontier()},
        {"earliest", simulation.earliest()},
        {"year", year(turn)},
        {"year_length", YEAR_LENGTH},
        {"season", std::string(SEASONS[static_cast<std::size_t>(season(turn))])},
        {"winter", season(turn) % 2 == 1},
        {"alive", alive.size()},
        {"lost", simulation.deaths()},
        {"species", counts},
        {"playing", world.playing()},
        {"ended", simulation.ended()},
        {"end_note", end_note(simulation)},
        {"speed", world.speed()},
        {"speeds", SPEED_LABELS},
        {"gpu_status", simulation.gpu_status()},
        {"on_gpu", simulation.on_gpu()},
        {"rules_changed", world.rules().size()},
    };
    return result;
}

std::string frame(const Simulation& simulation) {
    const int width = simulation.width(), height = simulation.height();
    const auto& agents = simulation.agents();
    std::size_t trail_cells = 0;
    for (const auto& agent : agents) {
        for (const auto& [x, y] : agent->trail) trail_cells += !(x == agent->x && y == agent->y);
    }
    std::string out;
    out.reserve(24 + 4 * static_cast<std::size_t>(height) + static_cast<std::size_t>(width) * height + agents.size() * 33 + trail_cells * 8);
    out += "BZF1";
    put<std::uint32_t>(out, static_cast<std::uint32_t>(width));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(height));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(simulation.turn()));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(agents.size()));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(trail_cells));
    for (int y = 0; y < height; ++y) put<float>(out, static_cast<float>(simulation.temperature(y)));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) out.push_back(simulation.grass_at(x, y) > 0 ? 1 : 0);
    }
    for (const auto& agent : agents) {
        const Rgb dot = color_from_word(mind_word(agent->dna)), ring = color_from_word(body_word(agent->dna)),
                  social = color_from_word(social_word(agent->dna));
        put<std::uint32_t>(out, static_cast<std::uint32_t>(agent->identifier));
        put<std::uint16_t>(out, static_cast<std::uint16_t>(agent->x));
        put<std::uint16_t>(out, static_cast<std::uint16_t>(agent->y));
        for (const int channel : {dot.r, dot.g, dot.b, ring.r, ring.g, ring.b, social.r, social.g, social.b}) {
            out.push_back(static_cast<char>(channel));
        }
        out.push_back(static_cast<char>(agent->species));
        out.push_back(static_cast<char>(agent->body[Size]));
        out.push_back(static_cast<char>(agent->alive() ? 1 : 0));
        out.push_back(static_cast<char>(static_cast<int>(py_round(255 * corpse_opacity(*agent, simulation.turn())))));
        put<std::uint32_t>(out, mind_word(agent->dna));
        put<std::uint32_t>(out, body_word(agent->dna));
        put<std::uint32_t>(out, social_word(agent->dna));
    }
    for (std::size_t index = 0; index < agents.size(); ++index) {
        const Bitzkey& agent = *agents[index];
        for (const auto& [x, y] : agent.trail) {
            if (x == agent.x && y == agent.y) continue;
            put<std::uint32_t>(out, static_cast<std::uint32_t>(index));
            put<std::uint16_t>(out, static_cast<std::uint16_t>(x));
            put<std::uint16_t>(out, static_cast<std::uint16_t>(y));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// The inspector

Json inspect(const Bitzkey& agent, bool known, std::optional<double> here) {
    Json result = {
        {"identifier", agent.identifier},
        {"species", std::string(species_name(agent.species))},
        {"species_index", static_cast<int>(agent.species)},
        {"dot", hex_word(mind_word(agent.dna))},
        {"ring", hex_word(body_word(agent.dna))},
        {"social", hex_word(social_word(agent.dna))},
        {"dot_rgb", rgb(color_from_word(mind_word(agent.dna)))},
        {"ring_rgb", rgb(color_from_word(body_word(agent.dna)))},
        {"social_rgb", rgb(color_from_word(social_word(agent.dna)))},
        {"neurodivergence", decode_neurodivergence(agent.dna)},
        {"inborn", agent.inborn},
        {"body", agent.body},
        {"burn", agent.burn},
        {"home", agent.climate.temperature},
        {"heat", agent.climate.heat},
        {"cold", agent.climate.cold},
        {"lifespan", agent.lifespan},
        {"known", known},
    };
    if (!known) return result;
    result["mind"] = agent.mind;
    if (here) {
        result["here"] = *here;
        result["climate_steps"] = agent.climate_steps(*here);
    }
    result["alive"] = agent.alive();
    result["hunger"] = agent.hunger;
    result["health"] = agent.health;
    result["max_health"] = agent.max_health;
    result["age"] = agent.age;
    result["generation"] = agent.generation;
    result["children"] = agent.children;
    result["last_action"] = std::string(agent.last_action);
    result["meat"] = agent.meat;
    result["brain_depth"] = agent.brain ? agent.brain->depth() : 0;
    Json chances = Json::array();
    if (agent.odds_offered) {
        std::vector<std::pair<int, double>> odds;
        for (int action = 0; action < ACTION_COUNT; ++action) {
            if (agent.odds_offered & (1U << action)) odds.emplace_back(action, agent.odds[static_cast<std::size_t>(action)]);
        }
        std::stable_sort(odds.begin(), odds.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (std::size_t index = 0; index < std::min<std::size_t>(3, odds.size()); ++index) {
            chances.push_back({std::string(ACTIONS[static_cast<std::size_t>(odds[index].first)]), py_percent(odds[index].second, 0)});
        }
    }
    result["chances"] = chances;
    return result;
}

Json design(Dna dna) {
    const Bitzkey sample(0, dna, 0, 0);
    Json result = inspect(sample, false);
    std::vector<std::string> facts;
    if (sample.species == Species::Plant) {
        facts = {"A plant: it photosynthesises and never moves,", "thinks, breeds or eats.", format("Lives about %.0f turns.", sample.lifespan)};
    } else {
        const double fertility = sample.physique(Fertility);
        const std::array<std::pair<int, int>, 2> parents = {std::pair{sample.body[Litter], sample.body[Size]},
                                                            std::pair{sample.body[Litter], sample.body[Size]}};
        const int litter = litter_size(parents);
        const std::string species(species_name(sample.species));
        facts = {
            format("A %s.  Health %.0f.  Metabolism %.2f.", species.c_str(), sample.max_health, sample.metabolism),
            format("Stomach %.2f.  Burns %.2f hunger a turn.", sample.stomach, sample.burn),
            format("Moves %d cell%s a step.  Sees %d cells.", sample.steps, sample.steps != 1 ? "s" : "", sample.sight),
            format("Breeding costs each parent %.0f hunger, then", 25 - 15 * fertility),
            format("%d turns to recover.  With a like partner, litters of %d.", static_cast<int>(py_round(20 - 15 * fertility)), litter),
            "Carrion gives it " + py_percent(sample.digestion, 0) + format(" of its meat.  Lives about %.0f turns.", sample.lifespan),
        };
    }
    const std::string temperature(TEMPERATURES[static_cast<std::size_t>(sample.climate.temperature)]);
    if (sample.species == Species::Plant) {
        facts.push_back("At home where it is " + temperature + "; elsewhere it withers.");
    } else {
        const int hardiness = (sample.climate.heat ? 1 : 0) + (sample.climate.cold ? 2 : 0);
        constexpr std::array<const char*, 4> hardy = {"", "  Tolerates heat.", "  Tolerates cold.", "  Tolerates heat and cold."};
        facts.push_back("At home where it is " + temperature + "." + hardy[static_cast<std::size_t>(hardiness)]);
    }
    result["facts"] = facts;
    result["record"] = Json{{"version", GENOME_VERSION}, {"ring", hex_word(body_word(dna))}, {"dot", hex_word(mind_word(dna))},
                            {"social", hex_word(social_word(dna))}};
    return result;
}

// ---------------------------------------------------------------------------------------------
// Exemplars

Json exemplar_stats() {
    Json titles = Json::array();
    for (const ExemplarStat& stat : EXEMPLAR_STATS) titles.push_back(stat.title);
    return {{"stats", titles}, {"defaults", DEFAULT_EXEMPLARS}};
}

Json exemplars(const Simulation& simulation, const std::vector<int>& choices) {
    std::vector<const Bitzkey*> animals;
    for (const Bitzkey* agent : simulation.living()) {
        if (agent->species != Species::Plant) animals.push_back(agent);
    }
    Json cards = Json::array();
    for (const int raw : choices) {
        const int choice = std::max(0, std::min(static_cast<int>(EXEMPLAR_STATS.size()) - 1, raw));
        const ExemplarStat& stat = EXEMPLAR_STATS[static_cast<std::size_t>(choice)];
        const double sign = stat.lowest ? -1 : 1;
        const Bitzkey* best = nullptr;
        for (const Bitzkey* agent : animals) {  // ties go to the oldest, then the first born
            if (!best || std::tuple{sign * exemplar_value(*agent, stat), agent->age, -agent->identifier} >
                             std::tuple{sign * exemplar_value(*best, stat), best->age, -best->identifier}) {
                best = agent;
            }
        }
        Json card = {{"stat", choice}, {"title", stat.title}};
        if (best) {
            card["identifier"] = best->identifier;
            card["species"] = std::string(species_name(best->species));
            card["species_index"] = static_cast<int>(best->species);
            card["size"] = best->body[Size];
            card["dot_rgb"] = rgb(color_from_word(mind_word(best->dna)));
            card["ring_rgb"] = rgb(color_from_word(body_word(best->dna)));
            card["social_rgb"] = rgb(color_from_word(social_word(best->dna)));
            card["said"] = exemplar_text(*best, stat);
        } else {
            card["said"] = "none alive";
        }
        cards.push_back(std::move(card));
    }
    return cards;
}

// ---------------------------------------------------------------------------------------------
// Insights

Json insights(const Simulation& simulation) {
    const Insights& insights = simulation.insights();
    Json result;
    Json behaviour = Json::array();
    for (int action = 0; action < ACTION_COUNT; ++action) {
        Json row = {{"action", std::string(ACTIONS[static_cast<std::size_t>(action)])}};
        Json shares = Json::array();
        for (const Species species : ANIMALS) {
            const auto rate = insights.rate(action, species);
            shares.push_back(rate.share ? Json(py_percent(*rate.share, 4)) : Json("  -"));
        }
        row["shares"] = shares;
        behaviour.push_back(std::move(row));
    }
    result["behaviour"] = behaviour;

    const auto extremes = [](const std::vector<Insights::Extreme>& groups) {
        Json list = Json::array();
        for (const auto& group : groups) list.push_back({{"label", group.label}, {"share", py_percent(group.share, 4)}});
        return list;
    };
    result["blocks"] = Json::array({
        {{"title", "LEAST KEEN TO BREED"}, {"groups", extremes(insights.extremes(Breed, 3, true))}},
        {{"title", "KEENEST TO BREED"}, {"groups", extremes(insights.extremes(Breed, 2, false))}},
        {{"title", "LEAST KEEN TO FIGHT"}, {"groups", extremes(insights.extremes(Attack, 2, true))}},
    });

    std::array<std::vector<const Bitzkey*>, 3> members;
    for (const Bitzkey* agent : simulation.living()) {
        for (std::size_t index = 0; index < ANIMALS.size(); ++index) {
            if (agent->species == ANIMALS[index]) members[index].push_back(agent);
        }
    }
    const auto average = [&](std::size_t index, auto value) -> std::optional<double> {
        if (members[index].empty()) return std::nullopt;
        double total = 0;
        for (const Bitzkey* agent : members[index]) total += value(*agent);
        return total / static_cast<double>(members[index].size());
    };
    const auto shown = [](std::optional<double> value, const char* pattern) { return value ? format(pattern, *value) : std::string("-"); };
    const auto percent = [](std::optional<double> value) { return value ? py_percent(*value, 0) : std::string("-"); };
    Json life = Json::array();
    const auto row = [&](const char* label, auto cell) {
        Json values = Json::array();
        for (std::size_t index = 0; index < ANIMALS.size(); ++index) values.push_back(cell(index));
        life.push_back({{"label", label}, {"values", values}});
    };
    row("ALIVE", [&](std::size_t i) { return std::to_string(members[i].size()); });
    row("SIZE", [&](std::size_t i) { return shown(average(i, [](const Bitzkey& a) { return static_cast<double>(a.body[Size]); }), "%.1f"); });
    row("AGE", [&](std::size_t i) { return shown(average(i, [](const Bitzkey& a) { return static_cast<double>(a.age); }), "%.0f"); });
    row("GENERATION", [&](std::size_t i) { return shown(average(i, [](const Bitzkey& a) { return static_cast<double>(a.generation); }), "%.1f"); });
    row("HUNGER", [&](std::size_t i) { return percent(average(i, [](const Bitzkey& a) { return std::min(100.0, a.hunger) / 100; })); });
    row("HEALTH", [&](std::size_t i) { return percent(average(i, [](const Bitzkey& a) { return std::max(0.0, a.health) / a.max_health; })); });
    row("CHILDREN", [&](std::size_t i) { return shown(average(i, [](const Bitzkey& a) { return static_cast<double>(a.children); }), "%.1f"); });
    row("HERDING", [&](std::size_t i) { return percent(average(i, [](const Bitzkey& a) { return a.herd_instinct(); })); });
    row("RIVALRY", [&](std::size_t i) {
        return ANIMALS[i] == Species::Grazer ? std::string("-") : percent(average(i, [](const Bitzkey& a) { return a.rivalry; }));
    });
    result["life"] = life;

    Json temperament = Json::array();
    for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) {
        Json values = Json::array();
        for (std::size_t index = 0; index < ANIMALS.size(); ++index) {
            const auto now = average(index, [trait](const Bitzkey& a) { return a.mind[static_cast<std::size_t>(trait)]; });
            if (!now) {
                values.push_back(nullptr);
                continue;
            }
            const double shift = *now - *average(index, [trait](const Bitzkey& a) {
                return static_cast<double>(a.inborn[static_cast<std::size_t>(trait)]);
            });
            values.push_back({{"value", format("%4.1f", *now)}, {"shift", std::abs(shift) < 0.25 ? 0 : shift > 0 ? 1 : -1}});
        }
        temperament.push_back({{"trait", std::string(TEMPERAMENT[static_cast<std::size_t>(trait)])}, {"values", values}});
    }
    result["temperament"] = temperament;

    Json keenest = Json::array();
    for (int action = 0; action < ACTION_COUNT; ++action) {
        const auto groups = insights.extremes(action, 1, false);
        if (groups.empty()) continue;
        keenest.push_back({{"action", std::string(ACTIONS[static_cast<std::size_t>(action)])},
                           {"label", groups[0].label},
                           {"share", py_percent(groups[0].share, 4)}});
    }
    result["keenest"] = keenest;
    Json species = Json::array();
    for (const Species animal : ANIMALS) species.push_back(std::string(species_name(animal)));
    result["species"] = species;
    return result;
}

// ---------------------------------------------------------------------------------------------
// Setup and the advanced rules

Json settings_form(const Simulation::Settings& current) {
    Json list = Json::array();
    for (const Setting& setting : SETTINGS) {
        list.push_back({{"key", setting.key},
                        {"label", setting.label},
                        {"hint", setting.hint},
                        {"minimum", setting.minimum},
                        {"maximum", setting.maximum},
                        {"step", setting.step},
                        {"optional", setting.optional},
                        {"blank", setting.blank},
                        {"value", setting_value(current, setting.key)}});
    }
    return {{"settings", list}, {"max_imported", MAX_IMPORTED}};
}

Json rules_form(const rules::Values& current) {
    Json list = Json::array();
    for (const rules::Rule& rule : rules::all()) {
        const auto found = current.find(rule.key);
        const double value = found != current.end() ? found->second : rules::defaults().at(rule.key);
        list.push_back({{"key", rule.key},
                        {"group", rule.group},
                        {"label", rule.label},
                        {"hint", rule.hint},
                        {"minimum", rule.minimum},
                        {"maximum", rule.maximum},
                        {"step", rule.step},
                        {"integer", rule.integer},
                        {"value", value},
                        {"default", rules::defaults().at(rule.key)},
                        {"shown", rules::shown(rule, value)},
                        {"warning", std::string(rules::warning(rule.key))},
                        {"locked", rules::locked(rule.key)}});
    }
    return {{"rules", list}};
}

Simulation::Settings read_settings(const Json& form, std::string* import_error) {
    Simulation::Settings settings;
    const auto read = [&](const Setting& setting) -> std::optional<int> {
        if (!form.contains(setting.key) || form[setting.key].is_null()) {
            if (setting.optional) return std::nullopt;
            throw std::invalid_argument(std::string(setting.label) + " is needed");
        }
        const Json& raw = form[setting.key];
        long long value = 0;
        if (raw.is_number()) value = raw.get<long long>();
        else if (raw.is_string() && !raw.get<std::string>().empty()) value = std::stoll(raw.get<std::string>());
        else if (setting.optional) return std::nullopt;
        else throw std::invalid_argument(std::string(setting.label) + " is needed");
        return static_cast<int>(std::max<long long>(setting.minimum, std::min<long long>(setting.maximum, value)));
    };
    for (const Setting& setting : SETTINGS) {
        const auto value = read(setting);
        const std::string_view key = setting.key;
        if (key == "width") settings.width = *value;
        else if (key == "height") settings.height = *value;
        else if (key == "population") settings.population = *value;
        else if (key == "grass_density") settings.grass_density = *value;
        else if (key == "seed") { if (value) settings.seed = static_cast<std::uint64_t>(*value); }
        else if (key == "population_cap") settings.population_cap = value;
        else if (key == "mind_mutation") settings.mind_mutation = *value;
        else if (key == "body_mutation") settings.body_mutation = *value;
        else if (key == "year_length") settings.year_length = *value;
    }
    const std::string text = form.value("import", std::string());
    try {
        auto found = import_bitzkeys(text);
        if (found.size() > static_cast<std::size_t>(MAX_IMPORTED)) found.resize(MAX_IMPORTED);
        settings.imported = std::move(found);
    } catch (const std::exception& error) {
        if (import_error) *import_error = error.what();
        throw std::invalid_argument(std::string("Can't import: ") + error.what());
    }
    return settings;
}

Json import_check(const std::string& text) {
    try {
        const auto found = import_bitzkeys(text);
        if (found.empty()) return {{"ok", true}, {"count", 0}, {"summary", "Nothing to import."}};
        return {{"ok", true}, {"count", found.size()}, {"summary", import_summary(found)}};
    } catch (const std::exception& error) {
        return {{"ok", false}, {"count", 0}, {"summary", std::string("Can't import: ") + error.what()}};
    }
}

}  // namespace bitzkey::daemon
