#include "net/Probe.hpp"

#include <cstdio>
#include <unordered_map>
#include <algorithm>
#include <array>
#include <set>
#include <unordered_set>
#include <cmath>
#include <numbers>
#include <numeric>
#include <optional>
#include <random>
#include <regex>
#include <stdexcept>
#include <tuple>

#include <httplib.h>

#include "core/Insights.hpp"
#include "core/Rules.hpp"

namespace bitzkey::net {

namespace {

using Json = Probe::Json;

constexpr std::size_t MOST_KEPT = 100000;  // turns of a watched track kept at most
constexpr int FAMILY_DEPTH = 4;            // generations up and down a family is followed
constexpr std::array<const char*, 4> WEAPON_NAMES{"none", "bone_axe", "club", "stone_axe"};
constexpr std::array<const char*, 3> ARMOUR_NAMES{"none", "bone_armour", "wood_armour"};

std::optional<Species> species_named(const std::string& name) {
    for (std::size_t index = 0; index < SPECIES.size(); ++index) {
        if (SPECIES[index] == name) return static_cast<Species>(index);
    }
    return std::nullopt;
}

Json brief(const Bitzkey& agent) {
    return {{"id", agent.identifier},
            {"species", std::string(species_name(agent.species))},
            {"terrain", std::string(terrain_name(agent.terrain))},
            {"nocturnal", agent.nocturnal},
            {"wet", agent.wet},
            {"x", agent.x},
            {"y", agent.y},
            {"age", agent.age},
            {"health", agent.max_health > 0 ? std::max(0.0, agent.health) / agent.max_health : 0.0},
            {"hunger", agent.hunger},
            {"stamina", agent.stamina},
            {"action", std::string(agent.last_action)}};
}

// Across the map, east and west wrapping round: the nearer way from one column to another.
int across(int from, int to, int width) {
    int dx = (to - from) % width;
    if (dx > width / 2) dx -= width;
    if (dx < -width / 2) dx += width;
    return dx;
}

// The mean of columns on a map that wraps east to west.
double mean_column(const std::vector<int>& columns, int width) {
    double sine = 0, cosine = 0;
    for (const int x : columns) {
        const double angle = 2 * std::numbers::pi * x / width;
        sine += std::sin(angle);
        cosine += std::cos(angle);
    }
    double angle = std::atan2(sine, cosine);
    if (angle < 0) angle += 2 * std::numbers::pi;
    return angle * width / (2 * std::numbers::pi);
}

int number_in(const std::smatch& matches) { return std::stoi(matches[1].str()); }

std::vector<int> numbers_in(const std::string& text) {
    std::vector<int> found;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t comma = text.find(',', at);
        const std::string part = text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (!part.empty()) found.push_back(std::stoi(part));
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return found;
}

void reply(httplib::Response& response, const Json& value, int status = 200) {
    response.status = status;
    response.set_content(value.dump(), "application/json; charset=utf-8");
}

// A route's answer, its errors answered as JSON: an unknown Bitzkey 404, a bad request 400, no world 503.
template <class Handler>
httplib::Server::Handler answered(Handler handler) {
    return [handler](const httplib::Request& request, httplib::Response& response) {
        try {
            handler(request, response);
        } catch (const std::out_of_range& error) {
            reply(response, {{"error", error.what()}}, 404);
        } catch (const std::invalid_argument& error) {
            reply(response, {{"error", error.what()}}, 400);
        } catch (const std::exception& error) {
            reply(response, {{"error", error.what()}}, 503);
        }
    };
}

}  // namespace

Probe::Probe(Run run) : run_(std::move(run)) {}

Json Probe::state(Simulation& world) const {
    std::array<int, 4> counts{};
    std::array<std::array<int, 4>, 4> terrains{};  // by species, then terrain
    std::array<int, 4> infants{}, sheltered{};     // the infants, and those of them beside a living parent
    for (const Bitzkey* agent : world.living()) {
        counts[static_cast<std::size_t>(agent->species)] += 1;
        if (agent->infant()) {
            infants[static_cast<std::size_t>(agent->species)] += 1;
            if (world.sheltered(*agent)) sheltered[static_cast<std::size_t>(agent->species)] += 1;
        }
        terrains[static_cast<std::size_t>(agent->species)][static_cast<std::size_t>(agent->terrain)] += 1;
    }
    Json alive = Json::object(), by_terrain = Json::object(), young = Json::object();
    for (std::size_t index = 0; index < SPECIES.size(); ++index) {
        alive[std::string(SPECIES[index])] = counts[index];
        young[std::string(SPECIES[index])] = {{"infants", infants[index]}, {"sheltered", sheltered[index]}};
        Json kinds = Json::object();
        for (std::size_t terrain = 0; terrain < TERRAINS.size(); ++terrain) kinds[std::string(TERRAINS[terrain])] = terrains[index][terrain];
        by_terrain[std::string(SPECIES[index])] = std::move(kinds);
    }
    return {{"turn", world.turn()},
            {"year", year(world.turn())},
            {"season", std::string(SEASONS[static_cast<std::size_t>(season(world.turn()))])},
            {"year_length", YEAR_LENGTH},
            {"width", world.width()},
            {"height", world.height()},
            {"alive", alive},
            {"by_terrain", by_terrain},
            {"infants", young},
            {"day_length", DAY_LENGTH},
            {"night_start", world.night_start()},  // the night runs east half the map's width from this column
            {"night_width", world.width() / 2}};
}

Json Probe::animals(Simulation& world, const std::string& species, int x, int y, int radius, std::size_t limit) const {
    const std::optional<Species> kind = species.empty() ? std::nullopt : species_named(species);
    if (!species.empty() && !kind) throw std::invalid_argument("no species " + species);
    Json found = Json::array();
    std::size_t matched = 0;
    for (const Bitzkey* agent : world.living()) {
        if (kind ? agent->species != *kind : agent->species == Species::Plant) continue;
        if (radius >= 0 && std::abs(across(x, agent->x, world.width())) + std::abs(agent->y - y) > radius) continue;
        ++matched;
        if (found.size() < limit) found.push_back(brief(*agent));
    }
    return {{"turn", world.turn()}, {"matched", matched}, {"animals", found}};
}

Json Probe::animal(Simulation& world, int identifier) const {
    const Bitzkey* agent = world.find_agent(identifier);
    if (!agent) throw std::out_of_range("no Bitzkey " + std::to_string(identifier) + " in the world now");
    Json result = brief(*agent);
    result["turn"] = world.turn();
    result["alive"] = agent->alive();
    if (agent->died_turn) result["died_turn"] = *agent->died_turn;
    result["lifespan"] = agent->lifespan;
    result["infant"] = agent->infant();
    result["feeble"] = agent->feeble;
    result["generation"] = agent->generation;
    result["taught_by"] = agent->taught_by;
    result["carried_meat"] = agent->carried_meat;
    result["carried_fruit"] = agent->carried_fruit;
    result["capacity"] = agent->capacity();
    result["mother"] = agent->mother;
    result["mark"] = agent->mark == PrestigeMark ? "prestige" : agent->mark == WarriorMark ? "warrior" : "none";
    result["leader"] = agent->leader;
    result["followers"] = agent->followers;
    result["follows"] = agent->follows;
    result["standout"] = agent->standout;
    result["realm"] = agent->realm;
    result["in_own_land"] = world.in_own_land(*agent);
    result["trespassing_turns"] = agent->trespass;
    result["war_standing"] = world.war_standing(agent->realm);
    result["father"] = agent->father;
    result["children"] = agent->children;
    result["max_health"] = agent->max_health;
    result["cooldown"] = agent->cooldown;
    result["in_heat"] = agent->in_heat;
    result["can_breed"] = agent->can_breed();
    result["carrying"] = agent->womb.size();
    if (agent->due_turn) result["due_turn"] = *agent->due_turn;
    Json body = Json::object();
    for (int trait = 0; trait < BODY_TRAIT_COUNT; ++trait) body[std::string(BODY_TRAITS[static_cast<std::size_t>(trait)])] = agent->trait(trait);
    result["body"] = body;
    result["memories"] = agent->memories.size();
    if (agent->food_memory) result["food_memory"] = {agent->food_memory->x, agent->food_memory->y, agent->food_memory->turn};
    if (agent->danger_memory) result["danger_memory"] = {agent->danger_memory->x, agent->danger_memory->y, agent->danger_memory->turn};
    result["rivalry"] = agent->rivalry;
    result["panicked"] = agent->panicked;
    result["primal_fear"] = agent->primal_fear;
    result["last_choice"] = std::string(ACTIONS[static_cast<std::size_t>(agent->last_choice)]);
    result["reward"] = agent->reward;
    result["last_comfort"] = agent->last_comfort;
    result["last_wellbeing"] = agent->last_wellbeing;
    result["home"] = std::string(TEMPERATURES[static_cast<std::size_t>(agent->climate.temperature)]);
    result["heat_tolerant"] = agent->climate.heat;
    result["cold_tolerant"] = agent->climate.cold;
    if (agent->y >= 0 && agent->y < world.height()) {
        const double here = world.temperature(agent->y);
        result["temperature_here"] = here;
        result["steps_from_home"] = agent->climate_steps(here);
    }
    if (agent->species == Species::Plant) return result;
    result["infant_taught"] = agent->infant_taught;  // given its brain by a parent, its first chance
    static constexpr std::array<std::string_view, 6> CRAFT_SOURCES = {"unrecorded", "discovered", "parent", "elder", "watched", "tutored"};  // how it came to know it
    Json crafts = Json::object();  // each craft: whether it has discovered it, how often it has tried, and from whom it came
    for (int craft = 0; craft < CRAFT_COUNT; ++craft) {
        crafts[std::string(ACTIONS[static_cast<std::size_t>(MakeBoneAxe + craft)])] = {{"known", (agent->crafts >> craft & 1) != 0},
                                                                                      {"tries", agent->craft_tries[static_cast<std::size_t>(craft)]},
                                                                                      {"source", CRAFT_SOURCES[static_cast<std::size_t>(agent->craft_source[static_cast<std::size_t>(craft)])]}};
    }
    result["crafts"] = crafts;
    result["boredom"] = agent->boredom;
    result["shelter_known"] = agent->shelter_known;
    result["road_known"] = agent->road_known;
    result["hide"] = agent->hide ? std::string(SPECIES[static_cast<std::size_t>(agent->hide)]) : std::string("none");
    result["regarded"] = agent->regarded;
    result["meat_lean"] = agent->meat_lean;
    result["cooked_known"] = agent->cooked_known;
    result["plenty"] = agent->plenty;
    result["plenty_known"] = agent->plenty_known;
    result["surprise"] = agent->surprise;
    result["gear"] = {{"weapon", WEAPON_NAMES[static_cast<std::size_t>(agent->weapon)]}, {"weapon_uses", agent->weapon_uses},
                      {"bow_uses", agent->bow_uses}, {"archery", agent->archery},
                      {"desire", {{"weapon", agent->desire[WeaponDesire]}, {"armour", agent->desire[ArmourDesire]}, {"bow", agent->desire[BowDesire]}}},
                      {"armour", ARMOUR_NAMES[static_cast<std::size_t>(agent->armour)]}, {"armour_uses", agent->armour_uses},
                      {"seeds", agent->carried_seed}, {"stone", agent->carried_stone}, {"bone", agent->carried_bone}, {"wood", agent->carried_wood},
                      {"cooked_meat", agent->cooked_meat}, {"cooked_fruit", agent->cooked_fruit}};
    Json mind = Json::object();
    for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) mind[std::string(TEMPERAMENT[static_cast<std::size_t>(trait)])] = agent->feeling(trait);
    result["mind"] = mind;
    Json odds = Json::object();  // the chance of each action open to it at its last choice
    for (int action = 0; action < ACTION_COUNT; ++action) {
        if (agent->odds_offered & (1U << action)) odds[std::string(ACTIONS[static_cast<std::size_t>(action)])] = agent->odds[static_cast<std::size_t>(action)];
    }
    result["odds"] = odds;
    if (agent->has_last_senses) {  // what it sensed as it chose
        Json senses = Json::object();
        for (int sense = 0; sense < INPUT_SIZE; ++sense) senses[std::string(SENSES[static_cast<std::size_t>(sense)])] = agent->last_senses[static_cast<std::size_t>(sense)];
        result["senses"] = senses;
    }
    if (agent->alive()) {
        const Simulation::HerdView view = world.herd(*agent);
        result["herd"] = {{"any", view.any}, {"close", view.close}};
        if (view.middle) result["herd"]["middle"] = {view.middle->x, view.middle->y};
    }
    return result;
}

Json Probe::family(Simulation& world, int identifier) const {
    if (!world.find_agent(identifier)) throw std::out_of_range("no Bitzkey " + std::to_string(identifier) + " in the world now");
    std::map<int, std::vector<int>> young;  // by parent: the living young
    for (const Bitzkey* agent : world.living()) {
        for (const int parent : {agent->mother, agent->father}) {
            if (parent >= 0) young[parent].push_back(agent->identifier);
        }
    }
    const auto node = [&](int id) -> Json {
        const Bitzkey* agent = world.find_agent(id);
        if (!agent) return {{"id", id}, {"held", false}};  // gone from the world: its body rotted away
        return {{"id", id}, {"held", true}, {"species", std::string(species_name(agent->species))}, {"alive", agent->alive()},
                {"age", agent->age}, {"x", agent->x}, {"y", agent->y}};
    };
    std::function<Json(int, int)> up = [&](int id, int depth) -> Json {
        Json entry = node(id);
        const Bitzkey* agent = world.find_agent(id);
        if (agent && depth > 0) {
            entry["mother"] = agent->mother >= 0 ? up(agent->mother, depth - 1) : Json(nullptr);
            entry["father"] = agent->father >= 0 ? up(agent->father, depth - 1) : Json(nullptr);
        }
        return entry;
    };
    std::function<Json(int, int)> down = [&](int id, int depth) -> Json {
        Json entry = node(id);
        if (depth > 0) {
            Json children = Json::array();
            if (const auto found = young.find(id); found != young.end()) {
                for (const int child : found->second) children.push_back(down(child, depth - 1));
            }
            entry["young"] = children;
        }
        return entry;
    };
    return {{"turn", world.turn()}, {"ancestors", up(identifier, FAMILY_DEPTH)}, {"descendants", down(identifier, FAMILY_DEPTH)}};
}

Json Probe::herds(Simulation& world, const std::string& species, bool members, int reach) {
    const std::optional<Species> kind = species_named(species.empty() ? "grazer" : species);
    if (!kind || *kind == Species::Plant) throw std::invalid_argument("no herds of " + species);
    if (reach < 1) throw std::invalid_argument("a herd's reach is at least 1 step");
    // Herd-mates within `reach` steps of one another are one herd, and so are their herd-mates: close enough to hold
    // a herd together, not just to see one another, or a crowded map chains into one herd from end to end.
    std::vector<const Bitzkey*> animals;
    for (const Bitzkey* agent : world.living()) {
        if (agent->species == *kind) animals.push_back(agent);
    }
    std::map<int, std::size_t> index;
    for (std::size_t at = 0; at < animals.size(); ++at) index[animals[at]->identifier] = at;
    std::vector<std::size_t> root(animals.size());
    std::iota(root.begin(), root.end(), std::size_t{0});
    std::function<std::size_t(std::size_t)> find = [&](std::size_t at) { return root[at] == at ? at : root[at] = find(root[at]); };
    for (const Bitzkey* agent : animals) {
        for (const Bitzkey* other : world.nearby(*agent, std::min(reach, agent->sight))) {
            const auto found = index.find(other->identifier);
            if (found == index.end() || !(world.herd_mate(*agent, *other) || world.herd_mate(*other, *agent))) continue;
            root[find(index[agent->identifier])] = find(found->second);
        }
    }
    std::map<std::size_t, std::vector<const Bitzkey*>> groups;
    for (std::size_t at = 0; at < animals.size(); ++at) groups[find(at)].push_back(animals[at]);

    std::lock_guard lock(herd_mutex_);
    std::vector<Herd>& last = herds_[std::string(species_name(*kind))];
    std::vector<Herd> now;
    int alone = 0;
    for (const auto& [_, group] : groups) {
        if (group.size() < 2) {
            ++alone;
            continue;
        }
        Herd herd;
        herd.since = world.turn();
        std::vector<int> columns;
        double rows = 0;
        for (const Bitzkey* agent : group) {
            herd.members.insert(agent->identifier);
            columns.push_back(agent->x);
            rows += agent->y;
        }
        herd.x = mean_column(columns, world.width());
        herd.y = rows / static_cast<double>(group.size());
        now.push_back(std::move(herd));
    }
    // Each herd keeps the number of the herd it shares most members with, if no larger share claimed it first;
    // the rest take new numbers, and say which herd they split from, and which herds merged into each.
    std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> shares;  // members shared, now, last
    for (std::size_t a = 0; a < now.size(); ++a) {
        for (std::size_t b = 0; b < last.size(); ++b) {
            std::size_t shared = 0;
            for (const int member : now[a].members) shared += last[b].members.count(member);
            if (shared) shares.emplace_back(shared, a, b);
        }
    }
    std::sort(shares.begin(), shares.end(), [](const auto& p, const auto& q) { return std::get<0>(p) > std::get<0>(q); });
    std::vector<std::optional<std::size_t>> heir(now.size());  // the last herd each continues
    std::vector<bool> claimed(last.size(), false);
    for (const auto& [shared, a, b] : shares) {
        if (heir[a] || claimed[b]) continue;
        heir[a] = b;
        claimed[b] = true;
    }
    Json listed = Json::array();
    int kin_in_herds = 0, herded = 0;
    for (std::size_t a = 0; a < now.size(); ++a) {
        Herd& herd = now[a];
        Json entry;
        if (heir[a]) {
            const Herd& before = last[*heir[a]];
            herd.id = before.id;
            herd.since = before.since;
            entry["drift"] = {across(static_cast<int>(std::lround(before.x)), static_cast<int>(std::lround(herd.x)), world.width()),
                              herd.y - before.y};
        } else {
            herd.id = next_herd_++;
            entry["drift"] = nullptr;
        }
        Json split_from = Json::array(), merged_from = Json::array();
        for (const auto& [shared, here, b] : shares) {
            if (here != a || (heir[a] && *heir[a] == b)) continue;
            // Another herd's members joined it (a merge), or it came from a herd another kept the number of (a split).
            if (!heir[a]) split_from.push_back(last[b].id);
            else merged_from.push_back(last[b].id);
        }
        double spread = 0, home = 0;
        std::vector<int> ids(herd.members.begin(), herd.members.end());
        // Its families: members with a parent, a child or a sibling (sharing a parent) among the herd too.
        int with_kin = 0;
        for (const int id : ids) {
            const Bitzkey* agent = animals[index[id]];
            const bool kin = herd.members.count(agent->mother) || herd.members.count(agent->father) ||
                             std::any_of(ids.begin(), ids.end(), [&](int other_id) {
                                 if (other_id == id) return false;
                                 const Bitzkey* other = animals[index[other_id]];
                                 return other->mother == id || other->father == id ||
                                        (agent->mother >= 0 && other->mother == agent->mother) ||
                                        (agent->father >= 0 && other->father == agent->father);
                             });
            with_kin += kin;
        }
        kin_in_herds += with_kin;
        herded += static_cast<int>(ids.size());
        for (const int id : ids) {
            const Bitzkey* agent = animals[index[id]];
            spread += std::abs(across(static_cast<int>(std::lround(herd.x)), agent->x, world.width())) + std::abs(agent->y - herd.y);
            home += agent->climate.temperature;
        }
        const double size = static_cast<double>(ids.size());
        entry["id"] = herd.id;
        entry["size"] = ids.size();
        entry["x"] = herd.x;
        entry["y"] = herd.y;
        entry["spread"] = spread / size;  // the mean distance of its members from its middle
        entry["home"] = home / size;      // the mean home temperature, 0 hot to 3 very cold
        entry["kin"] = with_kin / size;   // the share of its members with family among it
        entry["since"] = herd.since;      // the turn it was first seen
        if (!split_from.empty()) entry["split_from"] = split_from;
        if (!merged_from.empty()) entry["merged_from"] = merged_from;
        if (members) entry["members"] = ids;
        listed.push_back(std::move(entry));
    }
    std::sort(listed.begin(), listed.end(), [](const Json& p, const Json& q) { return p["size"].get<std::size_t>() > q["size"].get<std::size_t>(); });
    last = std::move(now);
    // And for comparison, of those in herds, the share with any family alive anywhere: the most that could be with it.
    std::set<int> living;
    for (const Bitzkey* agent : animals) living.insert(agent->identifier);
    std::map<int, int> children_of;
    for (const Bitzkey* agent : animals) {
        for (const int parent : {agent->mother, agent->father}) {
            if (parent >= 0) children_of[parent] += 1;
        }
    }
    int with_family = 0;
    for (const Herd& herd : last) {
        for (const int id : herd.members) {
            const Bitzkey* agent = animals[index[id]];
            const bool parent = living.count(agent->mother) || living.count(agent->father);
            const bool child = children_of.count(id) > 0;
            const bool sibling = (agent->mother >= 0 && children_of[agent->mother] > 1) || (agent->father >= 0 && children_of[agent->father] > 1);
            with_family += parent || child || sibling;
        }
    }
    const double count = std::max(1, herded);
    return {{"turn", world.turn()}, {"species", std::string(species_name(*kind))}, {"herds", listed}, {"alone", alone},
            {"kin_in_herd", kin_in_herds / count}, {"family_alive", with_family / count}};
}

void Probe::watch(const std::vector<int>& identifiers, std::size_t keep) {
    std::lock_guard lock(watch_mutex_);
    if (keep) keep_ = std::min(keep, MOST_KEPT);
    for (const int identifier : identifiers) watched_.try_emplace(identifier);
}

void Probe::unwatch(const std::vector<int>& identifiers) {
    std::lock_guard lock(watch_mutex_);
    if (identifiers.empty()) watched_.clear();
    for (const int identifier : identifiers) watched_.erase(identifier);
}

Json Probe::tracks() const {
    std::lock_guard lock(watch_mutex_);
    Json tracks = Json::object();
    for (const auto& [identifier, samples] : watched_) tracks[std::to_string(identifier)] = Json(std::vector<Json>(samples.begin(), samples.end()));
    return {{"watching", watched_.size()}, {"keep", keep_}, {"tracks", tracks}};
}

void Probe::after_turn(const Simulation& world) {
    std::lock_guard lock(watch_mutex_);
    for (auto& [identifier, samples] : watched_) {
        const Bitzkey* agent = world.find_agent(identifier);
        if (!agent || !agent->alive()) {  // gone: said once
            if (samples.empty() || !samples.back().contains("gone")) {
                Json gone = {{"turn", world.turn()}, {"gone", true}};
                if (agent) gone["cause"] = std::string(agent->last_action);
                samples.push_back(std::move(gone));
            }
            continue;
        }
        Json sample = {{"turn", world.turn()}, {"x", agent->x}, {"y", agent->y}, {"action", std::string(agent->last_action)},
                       {"health", agent->max_health > 0 ? std::max(0.0, agent->health) / agent->max_health : 0.0},
                       {"hunger", agent->hunger}};
        if (agent->y >= 0 && agent->y < world.height()) {
            const double here = world.temperature(agent->y);
            sample["temperature"] = here;
            sample["steps_from_home"] = agent->climate_steps(here);
        }
        samples.push_back(std::move(sample));
        while (samples.size() > keep_) samples.pop_front();
    }
}

Json Probe::rules() const {
    Json all = Json::object();
    for (const rules::Rule& rule : rules::all()) {
        all[rule.key] = {{"value", rule.get()}, {"group", rule.group}, {"label", rule.label}, {"hint", rule.hint},
                         {"minimum", rule.minimum}, {"maximum", rule.maximum}, {"step", rule.step}};
    }
    return {{"rules", all}};
}

Json Probe::change_rules(Simulation& world, const Json& asked) {
    if (!asked.is_object()) throw std::invalid_argument("expected {\"rules\": {KEY: value, ...}}");
    rules::Values values = rules::current();
    for (const auto& [key, value] : asked.items()) {
        const rules::Rule* rule = rules::find(key);
        if (!rule) throw std::invalid_argument("no rule " + key);
        if (key == "USE_GPU") throw std::invalid_argument("USE_GPU holds for as long as the world runs");
        if (!value.is_number()) throw std::invalid_argument(key + " needs a number");
        values[key] = rule->clamp(value.get<double>());
    }
    if (const auto problems = rules::problems(values); !problems.empty()) return {{"problems", problems}};
    const rules::Values changed = rules::changed(values);
    rules::apply(changed);
    world.rules_changed();
    if (on_rules) on_rules();
    Json now = Json::object();
    for (const auto& [key, value] : asked.items()) now[key] = values.at(key);  // as clamped
    return {{"problems", Json::array()}, {"set", now}, {"changed_from_defaults", changed.size()}, {"turn", world.turn()}};
}

Json Probe::change_settings(Simulation& world, const Json& asked) const {
    if (!asked.is_object()) throw std::invalid_argument("expected an object of settings");
    const auto percent = [](const Json& value, const std::string& key) {
        if (!value.is_number() || value.get<double>() < 0 || value.get<double>() > 100) throw std::invalid_argument(key + " needs a percent, 0 to 100");
        return value.get<double>();
    };
    for (const auto& [key, value] : asked.items()) {  // all checked before any is changed
        if (key == "mind_mutation" || key == "body_mutation") percent(value, key);
        else if (key == "population_cap") {
            if (!value.is_null() && (!value.is_number_integer() || value.get<int>() < 1)) throw std::invalid_argument("population_cap needs a whole number from 1, or null for none");
        } else if (key == "width" || key == "height" || key == "ocean" || key == "year_length") {
            throw std::invalid_argument(key + " is fixed once a world is made");
        } else {
            throw std::invalid_argument("no setting " + key + " can be changed");
        }
    }
    if (asked.contains("mind_mutation")) world.set_mind_mutation(asked["mind_mutation"].get<double>());
    if (asked.contains("body_mutation")) world.set_body_mutation(asked["body_mutation"].get<double>());
    if (asked.contains("population_cap")) {
        const Json& cap = asked["population_cap"];
        world.set_population_cap(cap.is_null() ? std::nullopt : std::optional<int>(cap.get<int>()));
    }
    return settings(world);
}

Json Probe::settings(Simulation& world) const {
    return {{"turn", world.turn()},
            {"width", world.width()},
            {"height", world.height()},
            {"year_length", YEAR_LENGTH},
            {"population_cap", world.population_cap() ? Json(*world.population_cap()) : Json(nullptr)},
            {"mind_mutation", world.mind_mutation()},
            {"body_mutation", world.body_mutation()},
            {"ended", world.ended()},
            {"deaths", world.deaths()},
            {"next_identifier", world.next_identifier()},
            {"on_gpu", world.on_gpu()},
            {"gpu_status", world.gpu_status()},
            {"grass_regrowth", world.grass_regrowth()},
            {"ocean", world.ocean()},
            {"sea_squares", std::count(world.sea_map().begin(), world.sea_map().end(), 1)}};
}

Json Probe::climate(Simulation& world) const {
    // Each row's animals: how many, how many in a shelter and resting there, how many out of their home temperature
    // (colder or hotter), and the mean steps from home they feel; how many stand on a road and have gone by road, and
    // their mean road memory. Road memory by species too: how many have gone by road, and how many to the full.
    struct Dwellers { int animals = 0, sheltered = 0, resting_inside = 0, colder = 0, hotter = 0, on_road = 0, road_known = 0; double steps = 0.0, road_memory = 0.0; };
    std::map<std::string, std::array<int, 3>> road_love;  // animals, gone by road, full memory
    std::vector<Dwellers> dwellers(static_cast<std::size_t>(world.height()));
    for (const auto& agent : world.agents()) {
        if (!agent->alive() || agent->species == Species::Plant || agent->y < 0 || agent->y >= world.height()) continue;
        Dwellers& row = dwellers[static_cast<std::size_t>(agent->y)];
        const double here = world.temperature(agent->y);
        ++row.animals;
        row.sheltered += world.shelter_at(agent->x, agent->y);
        row.resting_inside += world.safe_inside(*agent);
        row.colder += here > agent->climate.temperature;
        row.hotter += here < agent->climate.temperature;
        row.steps += agent->climate_steps(here);
        row.on_road += world.road_at(agent->x, agent->y);
        auto& love = road_love[std::string(species_name(agent->species))];
        ++love[0];
        if (agent->road_known > 0) {
            ++row.road_known;
            row.road_memory += agent->road_known;
            ++love[1];
            love[2] += agent->road_known >= 1.0;
        }
    }
    Json rows = Json::array();
    for (int y = 0; y < world.height(); ++y) {
        int grass = 0, sea = 0, shelters = 0, roads = 0, roads_begun = 0;
        for (int x = 0; x < world.width(); ++x) {
            grass += world.grass_at(x, y);
            sea += world.sea(x, y);
            shelters += world.shelter_at(x, y);
            roads += world.road_at(x, y);
            roads_begun += world.road_work_at(x, y) > 0;
        }
        const Dwellers& row = dwellers[static_cast<std::size_t>(y)];
        rows.push_back({{"y", y}, {"temperature", world.temperature(y)}, {"zone", zone_temperature(y, world.height())},
                        {"growth", world.growth(y)}, {"grass", grass}, {"sea", sea}, {"watered", world.watered_in_row(y)},
                        {"shelters", shelters}, {"animals", row.animals}, {"sheltered", row.sheltered}, {"resting_inside", row.resting_inside},
                        {"colder_than_home", row.colder}, {"hotter_than_home", row.hotter},
                        {"mean_steps_from_home", row.animals ? Json(row.steps / row.animals) : Json(nullptr)},
                        {"roads", roads}, {"roads_begun", roads_begun}, {"on_road", row.on_road}, {"gone_by_road", row.road_known},
                        {"mean_road_memory", row.road_known ? Json(row.road_memory / row.road_known) : Json(nullptr)}});
    }
    Json love = Json::object();
    for (const auto& [species, counts] : road_love) love[species] = {{"animals", counts[0]}, {"gone_by_road", counts[1]}, {"full_road_memory", counts[2]}};
    return {{"turn", world.turn()}, {"season", std::string(SEASONS[static_cast<std::size_t>(season(world.turn()))])}, {"road_love", love}, {"rows", rows}};
}

Json Probe::grass(Simulation& world, bool map) const {
    Json rows = Json::array(), cells = Json::array(), sea = Json::array();
    for (int y = 0; y < world.height(); ++y) {
        int total = 0;
        Json row = Json::array();
        std::string shore;  // the row's sea, a character a square: 1 sea, 0 land
        for (int x = 0; x < world.width(); ++x) {
            const int tufts = world.grass_at(x, y);
            total += tufts;
            if (map) row.push_back(tufts);
            if (map && world.has_sea()) shore += world.sea(x, y) ? '1' : '0';
        }
        rows.push_back(total);
        if (map) cells.push_back(std::move(row));
        if (map && world.has_sea()) sea.push_back(std::move(shore));
    }
    Json result = {{"turn", world.turn()}, {"tufts", world.grass_tufts()}, {"by_row", rows}};
    if (map) result["map"] = cells;
    if (map && world.has_sea()) result["sea"] = sea;
    return result;
}

Json Probe::cell(Simulation& world, int x, int y) const {
    if (x < 0 || x >= world.width() || y < 0 || y >= world.height()) throw std::invalid_argument("no such cell");
    Json living = Json::array(), dead = Json::array(), seeds = Json::array();
    for (const auto& agent : world.agents()) {
        if (agent->x != x || agent->y != y) continue;
        (agent->alive() ? living : dead).push_back(brief(*agent));
    }
    for (const Simulation::Seed& seed : world.seeds()) {
        if (seed.x == x && seed.y == y) seeds.push_back({{"generation", seed.generation}, {"sown", seed.sown}});
    }
    return {{"turn", world.turn()}, {"x", x}, {"y", y}, {"sea", world.sea(x, y)}, {"grass", world.grass_at(x, y)}, {"temperature", world.temperature(y)},
            {"living", living}, {"dead", dead}, {"seeds", seeds}};
}

Json Probe::bodies(Simulation& world, std::size_t limit) const {
    Json found = Json::array();
    std::size_t count = 0;
    for (const auto& agent : world.agents()) {
        if (agent->alive() || agent->species == Species::Plant) continue;
        ++count;
        if (found.size() >= limit) continue;
        Json body = brief(*agent);
        body["died_turn"] = agent->died_turn ? Json(*agent->died_turn) : Json(nullptr);
        body["meat"] = agent->meat;
        body["portions"] = agent->portions;
        body["fresh"] = world.fresh(*agent);
        found.push_back(std::move(body));
    }
    return {{"turn", world.turn()}, {"count", count}, {"bodies", found}};
}

Json Probe::seeds(Simulation& world, std::size_t limit) const {
    Json found = Json::array();
    for (const Simulation::Seed& seed : world.seeds()) {
        if (found.size() >= limit) break;
        found.push_back({{"x", seed.x}, {"y", seed.y}, {"generation", seed.generation}, {"sown", seed.sown}});
    }
    return {{"turn", world.turn()}, {"count", world.seeds().size()}, {"seeds", found}};
}

Json Probe::deaths(Simulation& world, int since, const std::string& species, const std::string& cause, std::size_t limit) const {
    const std::optional<Species> kind = species.empty() ? std::nullopt : species_named(species);
    if (!species.empty() && !kind) throw std::invalid_argument("no species " + species);
    Json found = Json::array();
    std::size_t matched = 0;
    const std::vector<Death> all = world.deaths_so_far();
    for (auto death = all.rbegin(); death != all.rend(); ++death) {  // the latest first
        if (death->turn < since || (kind && death->species != *kind) || (!cause.empty() && death->cause != cause)) continue;
        ++matched;
        if (found.size() >= limit) continue;
        Json entry = {{"turn", death->turn}, {"species", std::string(species_name(death->species))}, {"cause", death->cause}};
        if (death->killer >= 0) {
            entry["killer"] = death->killer;
            entry["killer_species"] = std::string(species_name(death->killer_species));
            entry["activity"] = death->activity;
        }
        found.push_back(std::move(entry));
    }
    Json totals = Json::object();  // every death since the world began, by species and cause
    for (std::size_t index = 0; index < SPECIES.size(); ++index) {
        Json causes = Json::object();
        for (const auto& [name, count] : world.death_causes()[index]) causes[name] = count;
        totals[std::string(SPECIES[index])] = causes;
    }
    return {{"turn", world.turn()}, {"matched", matched}, {"deaths", found}, {"since_the_start", totals}};
}

Json Probe::history(Simulation& world, std::size_t points) const {
    const auto& snapshots = world.census().snapshots();
    const std::size_t stride = std::max<std::size_t>(1, (snapshots.size() + std::max<std::size_t>(points, 1) - 1) / std::max<std::size_t>(points, 1));
    Json series = Json::array();
    for (std::size_t at = 0; at < snapshots.size(); at += stride) {
        std::array<int, 4> counts{};
        for (const int identifier : snapshots[at].identifiers) {
            if (const Bitzkey* agent = world.find_agent(identifier)) {
                counts[static_cast<std::size_t>(agent->species)] += 1;
            } else if (world.remembered(identifier)) {
                counts[static_cast<std::size_t>(decode_body(world.lineage(identifier))[SpeciesTrait])] += 1;
            }
        }
        Json alive = Json::object();
        for (std::size_t index = 0; index < SPECIES.size(); ++index) alive[std::string(SPECIES[index])] = counts[index];
        series.push_back({{"turn", snapshots[at].turn}, {"alive", alive}});
    }
    return {{"turn", world.turn()}, {"points", series.size()}, {"history", series}};
}

Json Probe::insights(Simulation& world) const {
    Json result = Json::object();
    for (const Species species : {Species::Grazer, Species::Omnivore, Species::Predator}) {
        Json actions = Json::object();
        for (int action = 0; action < ACTION_COUNT; ++action) {
            const Insights::Rate rate = world.insights().rate(action, species);
            if (rate.offered <= 0) continue;
            actions[std::string(ACTIONS[static_cast<std::size_t>(action)])] = {{"offered", rate.offered},
                                                                                {"taken", rate.share ? Json(*rate.share) : Json(nullptr)}};
        }
        result[std::string(species_name(species))] = actions;
    }
    return {{"turn", world.turn()}, {"insights", result}};
}

Json Probe::calls(Simulation& world) const {
    // Each animal's last choice, as it sensed the world then: the call it made, whether a hunter was in sight, what it
    // heard, and what it did.
    const auto moved = [](int action) { return action >= GoNorth && action <= WalkWest; };
    Json result = Json::object();
    for (const Species species : {Species::Grazer, Species::Omnivore, Species::Predator}) {
        struct Count {
            long long with = 0, without = 0;  // made with a hunter in sight, and without
        };
        std::array<Count, 2 + CALL_COUNT> made{};  // by call: silent, the alert, then the five
        long long threatened = 0, calm = 0;
        struct Heard {
            long long heard = 0, acted = 0;
        };
        std::array<Heard, CALL_COUNT> threat_heard{}, threat_heard_kin{}, calm_heard{};
        Heard threat_none, calm_none;
        // What a call says, with no meaning given it: how much the free call made tells of the caller's situation (a
        // hunter in sight, hungry, food in sight, kin alongside, goods it could trade: 32 in all), and how much the loudest call heard tells of
        // what the listener did (fled, moved, rested, other), in bits.
        std::array<std::array<long long, 32>, CALL_COUNT> said{};
        std::array<std::array<long long, 4>, CALL_COUNT> answered{}, answered_kin{}, answered_others{};  // and by the caller: its own kind or another
        Heard fled_kin, fled_others;  // with a hunter in sight, having heard its own kind loudest, or another kind
        for (const Bitzkey* agent : world.living()) {
            if (agent->species != species || !agent->has_last_senses) continue;
            const Senses& senses = agent->last_senses;
            const bool hunter = senses[SenseThreatCloseness] > 0;
            (hunter ? threatened : calm) += 1;
            if (agent->last_call >= 0 && agent->last_call < static_cast<int>(made.size())) {
                (hunter ? made[static_cast<std::size_t>(agent->last_call)].with : made[static_cast<std::size_t>(agent->last_call)].without) += 1;
            }
            const bool acted = hunter ? agent->last_choice == Flee : moved(agent->last_choice);
            if (agent->last_call >= FirstCall && agent->last_call < FirstCall + CALL_COUNT) {
                const bool food = senses[SenseGrassSeen] > 0 || senses[SensePlantCloseness] > 0 || senses[SensePreyCloseness] > 0 || senses[SenseCarrionCloseness] > 0;
                const int situation = (hunter ? 1 : 0) | (senses[SenseHunger] >= 0.5 ? 2 : 0) | (food ? 4 : 0) | (senses[SenseKinNear] > 0 ? 8 : 0);
                bool wares = false;  // something it could offer in trade, as a vendor might cry its wares
                for (int good = 0; good < GOODS && !wares; ++good) wares = Simulation::holds(*agent, good);
                const int offered = situation | (wares ? 16 : 0);
                ++said[static_cast<std::size_t>(agent->last_call - FirstCall)][static_cast<std::size_t>(offered)];
            }
            int loudest = -1;
            for (int call = FirstCall; call < FirstCall + CALL_COUNT; ++call) {
                if (senses[heard_sense(call, HeardLoudness)] > (loudest < 0 ? 0.0 : senses[heard_sense(loudest, HeardLoudness)])) loudest = call;
            }
            if (loudest >= 0) {
                const int answer = agent->last_choice == Flee ? 0 : moved(agent->last_choice) ? 1 : agent->last_choice == Rest ? 2 : 3;
                ++answered[static_cast<std::size_t>(loudest - FirstCall)][static_cast<std::size_t>(answer)];
                const bool kin = senses[heard_sense(loudest, HeardKin)] > 0;
                ++(kin ? answered_kin : answered_others)[static_cast<std::size_t>(loudest - FirstCall)][static_cast<std::size_t>(answer)];
                if (hunter) {
                    Heard& fled = kin ? fled_kin : fled_others;
                    fled.heard += 1;
                    fled.acted += agent->last_choice == Flee;
                }
            }
            bool any = false;
            for (int call = FirstCall; call < FirstCall + CALL_COUNT; ++call) {
                if (senses[heard_sense(call, HeardLoudness)] <= 0) continue;
                any = true;
                const auto at = static_cast<std::size_t>(call - FirstCall);
                Heard& heard = hunter ? threat_heard[at] : calm_heard[at];
                heard.heard += 1;
                heard.acted += acted;
                if (hunter && senses[heard_sense(call, HeardKin)] > 0) {
                    threat_heard_kin[at].heard += 1;
                    threat_heard_kin[at].acted += acted;
                }
            }
            if (!any) {
                Heard& none = hunter ? threat_none : calm_none;
                none.heard += 1;
                none.acted += acted;
            }
        }
        const auto share = [](long long part, long long whole) { return whole > 0 ? Json(static_cast<double>(part) / whole) : Json(nullptr); };
        Json calls = Json::object();
        for (int call = AlertCall; call < FirstCall + CALL_COUNT; ++call) {
            const Count& count = made[static_cast<std::size_t>(call)];
            const std::string name = call == AlertCall ? "alert" : "call_" + std::to_string(call - FirstCall + 1);
            Json entry = {{"made_with_hunter", share(count.with, threatened)}, {"made_without", share(count.without, calm)}};
            if (call >= FirstCall) {
                const auto at = static_cast<std::size_t>(call - FirstCall);
                entry["hunter_in_sight"] = {{"heard", threat_heard[at].heard}, {"fled_having_heard", share(threat_heard[at].acted, threat_heard[at].heard)},
                                            {"fled_having_heard_kin", share(threat_heard_kin[at].acted, threat_heard_kin[at].heard)}};
                entry["no_hunter"] = {{"heard", calm_heard[at].heard}, {"moved_having_heard", share(calm_heard[at].acted, calm_heard[at].heard)}};
                const auto& helped = world.call_payoffs()[static_cast<std::size_t>(species)][at];  // since the world began, or was loaded
                entry["helped"] = {{"escapes", helped[0]}, {"meals", helped[1]}, {"understood", helped[2]}};
            }
            calls[name] = entry;
        }
        const auto information = [](const auto& table) {  // the bits shared, the samples, and the bits chance alone gives so few
            long long total = 0;
            std::vector<double> rows, columns(table[0].size(), 0.0);
            for (const auto& row : table) {
                double sum = 0;
                for (std::size_t at = 0; at < row.size(); ++at) { sum += static_cast<double>(row[at]); columns[at] += static_cast<double>(row[at]); }
                rows.push_back(sum);
                total += static_cast<long long>(sum);
            }
            double bits = 0;
            for (std::size_t r = 0; r < table.size(); ++r) {
                for (std::size_t c = 0; c < table[r].size(); ++c) {
                    const double n = static_cast<double>(table[r][c]);
                    if (n > 0) bits += n / total * std::log2(n * total / (rows[r] * columns[c]));
                }
            }
            const auto used = [](const std::vector<double>& sums) { return static_cast<double>(std::count_if(sums.begin(), sums.end(), [](double s) { return s > 0; })); };
            const double chance = total > 0 ? std::max(0.0, used(rows) - 1) * std::max(0.0, used(columns) - 1) / (2.0 * total * std::log(2.0)) : 0.0;
            return Json{{"bits", total > 0 ? Json(bits) : Json(nullptr)}, {"by_chance", chance}, {"samples", total}};
        };
        result[std::string(species_name(species))] = {
            {"call_tells_situation", information(said)}, {"heard_call_tells_action", information(answered)},
            {"heard_own_kind", {{"tells_action", information(answered_kin)}, {"hunter_in_sight", fled_kin.heard}, {"fled", share(fled_kin.acted, fled_kin.heard)}}},
            {"heard_other_kinds", {{"tells_action", information(answered_others)}, {"hunter_in_sight", fled_others.heard}, {"fled", share(fled_others.acted, fled_others.heard)}}},
            {"choices", threatened + calm}, {"with_hunter", threatened}, {"calls", calls},
            {"fled_hearing_nothing", share(threat_none.acted, threat_none.heard)}, {"moved_hearing_nothing", share(calm_none.acted, calm_none.heard)}};
    }
    return {{"turn", world.turn()}, {"calls", result}};
}

Json Probe::language(Simulation& world) const {
    // Meaning is never assigned, only looked for, each against what chance gives:
    // - each free call's profile: how much likelier each thing is when it is made than over all choices (lift 1 is none);
    // - what listeners do after each call, loudest heard, against those who heard nothing;
    // - conventions: whether callers within a call's reach of each other, and parents and their young, make the same call;
    // - dialects: whether colour groups' calls differ more than the same callers shuffled among the groups.
    constexpr std::array<const char*, 6> FEATURES = {"hunter", "hungry", "food", "kin", "goods", "night"};
    constexpr std::array<const char*, 4> ANSWERS = {"fled", "moved", "rested", "other"};
    const auto moved = [](int action) { return action >= GoNorth && action <= WalkWest; };
    const int width = world.width();
    const auto apart = [width](const Bitzkey& a, const Bitzkey& b) {
        const int dx = std::abs(a.x - b.x);
        return std::min(dx, width - dx) + std::abs(a.y - b.y);
    };
    Json result = Json::object();
    for (const Species species : {Species::Grazer, Species::Omnivore, Species::Predator}) {
        long long choices = 0, quiet_listeners = 0;
        std::array<long long, FEATURES.size()> base{};
        std::array<long long, CALL_COUNT> uses{};
        std::array<std::array<long long, FEATURES.size()>, CALL_COUNT> with{};
        std::array<std::array<long long, ANSWERS.size()>, CALL_COUNT> answers{};
        std::array<long long, ANSWERS.size()> quiet{};
        std::vector<std::pair<const Bitzkey*, int>> callers;
        for (const Bitzkey* agent : world.living()) {
            if (agent->species != species || !agent->has_last_senses) continue;
            const Senses& senses = agent->last_senses;
            bool goods = false;
            for (int good = 0; good < GOODS && !goods; ++good) goods = Simulation::holds(*agent, good);
            const std::array<bool, FEATURES.size()> feature = {
                senses[SenseThreatCloseness] > 0, senses[SenseHunger] >= 0.5,
                senses[SenseGrassSeen] > 0 || senses[SensePlantCloseness] > 0 || senses[SensePreyCloseness] > 0 || senses[SenseCarrionCloseness] > 0,
                senses[SenseKinNear] > 0, goods, senses[SenseNight] > 0};
            ++choices;
            for (std::size_t at = 0; at < FEATURES.size(); ++at) base[at] += feature[at];
            if (agent->last_call >= FirstCall && agent->last_call < FirstCall + CALL_COUNT) {
                const auto call = static_cast<std::size_t>(agent->last_call - FirstCall);
                ++uses[call];
                for (std::size_t at = 0; at < FEATURES.size(); ++at) with[call][at] += feature[at];
                callers.emplace_back(agent, static_cast<int>(call));
            }
            int loudest = -1;
            for (int call = FirstCall; call < FirstCall + CALL_COUNT; ++call) {
                if (senses[heard_sense(call, HeardLoudness)] > (loudest < 0 ? 0.0 : senses[heard_sense(loudest, HeardLoudness)])) loudest = call;
            }
            const std::size_t answer = agent->last_choice == Flee ? 0 : moved(agent->last_choice) ? 1 : agent->last_choice == Rest ? 2 : 3;
            if (loudest >= 0) {
                ++answers[static_cast<std::size_t>(loudest - FirstCall)][answer];
            } else {
                ++quiet[answer];
                ++quiet_listeners;
            }
        }
        const auto ratio = [](double part, double whole) { return whole > 0 ? Json(part / whole) : Json(nullptr); };
        Json calls = Json::object();
        std::array<double, FEATURES.size()> least{}, most{};
        least.fill(1e9);
        int profiled = 0;
        for (std::size_t call = 0; call < CALL_COUNT; ++call) {
            Json lift = Json::object();
            for (std::size_t at = 0; at < FEATURES.size(); ++at) {
                const double everywhere = choices > 0 ? static_cast<double>(base[at]) / choices : 0.0;
                if (uses[call] == 0 || everywhere <= 0) {
                    lift[FEATURES[at]] = nullptr;
                    continue;
                }
                const double value = static_cast<double>(with[call][at]) / static_cast<double>(uses[call]) / everywhere;
                lift[FEATURES[at]] = value;
                if (uses[call] >= 10) {
                    least[at] = std::min(least[at], value);
                    most[at] = std::max(most[at], value);
                }
            }
            profiled += uses[call] >= 10;
            long long heard = 0;
            for (const long long count : answers[call]) heard += count;
            Json response = Json::object();
            for (std::size_t at = 0; at < ANSWERS.size(); ++at) {
                const double after = heard > 0 ? static_cast<double>(answers[call][at]) / static_cast<double>(heard) : 0.0;
                const double otherwise = quiet_listeners > 0 ? static_cast<double>(quiet[at]) / static_cast<double>(quiet_listeners) : 0.0;
                response[ANSWERS[at]] = heard > 0 && otherwise > 0 ? Json(after / otherwise) : Json(nullptr);
            }
            calls["call_" + std::to_string(call + 1)] = {{"made", uses[call]}, {"lift", lift}, {"heard_by", heard}, {"listeners_lift", response}};
        }
        Json spread = Json::object();  // per thing, the most a call leans to it less the least, among calls made 10 times or more
        for (std::size_t at = 0; at < FEATURES.size(); ++at) spread[FEATURES[at]] = profiled >= 2 ? Json(most[at] - least[at]) : Json(nullptr);
        // Conventions: two callers make the same call by chance as often as the sum of each call's share squared.
        long long made = 0;
        for (const long long count : uses) made += count;
        double same_by_chance = 0;
        for (const long long count : uses) same_by_chance += made > 0 ? std::pow(static_cast<double>(count) / static_cast<double>(made), 2) : 0.0;
        long long pairs = 0, same = 0, kin_pairs = 0, kin_same = 0;
        std::unordered_map<int, int> call_of;
        for (const auto& [agent, call] : callers) call_of[agent->identifier] = call;
        for (std::size_t a = 0; a < callers.size(); ++a) {
            for (std::size_t b = a + 1; b < callers.size(); ++b) {
                if (apart(*callers[a].first, *callers[b].first) > CALL_REACH) continue;
                ++pairs;
                same += callers[a].second == callers[b].second;
            }
            for (const int parent : {callers[a].first->mother, callers[a].first->father}) {
                const auto found = call_of.find(parent);
                if (found == call_of.end()) continue;
                ++kin_pairs;
                kin_same += found->second == callers[a].second;
            }
        }
        // Dialects: how far each colour group's calls stray from its species', weighted by callers, against shuffles.
        const auto dialect = [&](const std::vector<int>& groups) -> std::optional<double> {
            std::map<int, std::array<double, CALL_COUNT>> by_group;
            std::map<int, double> sizes;
            for (std::size_t at = 0; at < callers.size(); ++at) {
                if (groups[at] < 0) continue;
                by_group[groups[at]][static_cast<std::size_t>(callers[at].second)] += 1;
                sizes[groups[at]] += 1;
            }
            double total = 0, weight = 0;
            for (const auto& [group, counts] : by_group) {
                if (sizes[group] < 8) continue;
                double distance = 0;
                for (std::size_t call = 0; call < CALL_COUNT; ++call) distance += std::abs(counts[call] / sizes[group] - static_cast<double>(uses[call]) / static_cast<double>(made));
                total += sizes[group] * distance / 2;
                weight += sizes[group];
            }
            if (weight <= 0) return std::nullopt;
            return total / weight;
        };
        std::vector<int> groups;
        for (const auto& [agent, call] : callers) groups.push_back(agent->realm);
        const std::optional<double> spoken = dialect(groups);
        double shuffled = 0;
        std::mt19937 shuffle(static_cast<unsigned>(world.turn()));  // the probe's own, never the world's
        for (int round = 0; round < 20 && spoken; ++round) {
            std::vector<int> mixed = groups;
            std::shuffle(mixed.begin(), mixed.end(), shuffle);
            shuffled += dialect(mixed).value_or(0.0) / 20;
        }
        result[std::string(species_name(species))] = {
            {"choices", choices}, {"calling_share", ratio(static_cast<double>(made), static_cast<double>(choices))}, {"calls", calls}, {"lift_spread", spread},
            {"conventions", {{"neighbour_pairs", pairs}, {"neighbours_same", ratio(static_cast<double>(same), static_cast<double>(pairs))},
                             {"parent_young_pairs", kin_pairs}, {"parent_young_same", ratio(static_cast<double>(kin_same), static_cast<double>(kin_pairs))},
                             {"same_by_chance", made > 0 ? Json(same_by_chance) : Json(nullptr)}}},
            {"dialects", {{"groups_differ", spoken ? Json(*spoken) : Json(nullptr)}, {"by_chance", spoken ? Json(shuffled) : Json(nullptr)}}}};
    }
    return {{"turn", world.turn()}, {"species", result}};
}

Json Probe::towns(Simulation& world) const {
    // Steps from the sea (four ways, wrapping east and west) of every land square, then how the works, the animals and
    // the plants spread over those steps: 1 is the shore.
    const int width = world.width(), height = world.height();
    std::vector<int> steps(static_cast<std::size_t>(width) * height, -1);
    std::vector<std::pair<int, int>> frontier;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (world.sea(x, y)) { steps[static_cast<std::size_t>(y) * width + x] = 0; frontier.emplace_back(x, y); }
        }
    }
    for (std::size_t at = 0; at < frontier.size(); ++at) {
        const auto [x, y] = frontier[at];
        const int here = steps[static_cast<std::size_t>(y) * width + x];
        for (const auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            const int nx = (x + dx + width) % width, ny = y + dy;
            if (ny < 0 || ny >= height) continue;
            int& there = steps[static_cast<std::size_t>(ny) * width + nx];
            if (there < 0) { there = here + 1; frontier.emplace_back(nx, ny); }
        }
    }
    const auto step_at = [&](int x, int y) { return steps[static_cast<std::size_t>(y) * width + x]; };
    struct Spread {
        long long count = 0, at_sea = 0, shore = 0, closer = 0, inland = 0, distant = 0;  // 0, 1, 2-3, 4-8, more
        double sum = 0;
        void add(int step) {
            ++count;
            if (step < 0) step = 99;  // no sea anywhere
            if (step == 0) ++at_sea; else if (step == 1) ++shore; else if (step <= 3) ++closer; else if (step <= 8) ++inland; else ++distant;
            if (step > 0) sum += step;
        }
        Json json() const {
            const auto share = [&](long long part) { return count > 0 ? Json(static_cast<double>(part) / count) : Json(nullptr); };
            const long long ashore = count - at_sea;
            return {{"count", count}, {"at_sea", share(at_sea)}, {"shore", share(shore)}, {"two_three", share(closer)},
                    {"four_eight", share(inland)}, {"further", share(distant)}, {"mean_steps_ashore", ashore > 0 ? Json(sum / ashore) : Json(nullptr)}};
        }
    };
    Spread land, shelters, sites, roads, sheltered, builders, plants;
    std::map<std::string, Spread> by_species, by_terrain;
    std::map<std::string, Spread> shelters_by_band;  // north edge, north, middle, south, south edge
    const auto band = [&](int y) { const double at = static_cast<double>(y) / height;
        return at < 0.1 ? "north_edge" : at < 0.4 ? "north" : at < 0.6 ? "middle" : at < 0.9 ? "south" : "south_edge"; };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int step = step_at(x, y);
            if (step != 0) land.add(step);
            if (world.shelter_at(x, y)) { shelters.add(step); shelters_by_band[band(y)].add(step); }
            if (world.site_at(x, y) > 0) sites.add(step);
            if (world.road_at(x, y)) roads.add(step);
        }
    }
    for (const Bitzkey* agent : world.living()) {
        const int step = step_at(agent->x, agent->y);
        if (agent->species == Species::Plant) { plants.add(step); continue; }
        by_species[std::string(species_name(agent->species))].add(step);
        by_terrain[std::string(terrain_name(agent->terrain))].add(step);
        if (world.safe_inside(*agent)) sheltered.add(step);
        if (agent->last_action == "built" || agent->last_action == "finished a shelter") builders.add(step);
    }
    const auto all = [](const std::map<std::string, Spread>& spreads) { Json out = Json::object(); for (const auto& [name, spread] : spreads) out[name] = spread.json(); return out; };
    return {{"turn", world.turn()}, {"land", land.json()}, {"shelters", shelters.json()}, {"shelters_by_band", all(shelters_by_band)},
            {"sites", sites.json()}, {"roads", roads.json()}, {"sheltered_animals", sheltered.json()}, {"building_now", builders.json()},
            {"plants", plants.json()}, {"animals_by_species", all(by_species)}, {"animals_by_terrain", all(by_terrain)}};
}

Json Probe::brain(Simulation& world, int identifier, bool weights) const {
    Bitzkey* agent = world.find_agent(identifier);
    if (!agent) throw std::out_of_range("no Bitzkey " + std::to_string(identifier) + " in the world now");
    if (!agent->brain) throw std::out_of_range("Bitzkey " + std::to_string(identifier) + " has no brain");
    world.sync_brain(*agent);  // as it now is, from the GPUs if it thinks there
    Json result = {{"id", identifier}, {"depth", agent->brain->depth()}, {"width", agent->brain->width()}};
    if (agent->has_last_senses) {
        const Values values = agent->brain->values(agent->last_senses);
        Json valued = Json::object();
        for (int action = 0; action < ACTION_COUNT; ++action) valued[std::string(ACTIONS[static_cast<std::size_t>(action)])] = values[static_cast<std::size_t>(action)];
        result["values"] = valued;  // what it makes of what it last sensed, every action, open or not
    }
    if (weights) result["weights"] = Json::parse(agent->brain->to_record().dump());
    return result;
}

Json Probe::skills(Simulation& world) const {
    Json result = Json::object();
    std::unordered_set<int> liars;  // held a liar by any living one who knows it
    for (const Bitzkey* agent : world.living()) {
        for (const Acquaintance& known : agent->known) {
            if (known.liar) liars.insert(known.identifier);
        }
    }
    for (const Species species : {Species::Grazer, Species::Omnivore, Species::Predator}) {
        // Possessions and what they stir: hides worn, how well each kind of wearer is regarded, the envious, fighters,
        // thieves and the thefts they've done, and those held liars.
        std::array<std::size_t, 4> hides{};
        std::array<double, 4> regarded{};
        std::array<double, 3> desired{};
        std::size_t counted = 0;
        std::size_t envious = 0, fighting = 0, thieves = 0, thefts = 0, held_liars = 0, leaning = 0, tasted = 0, loving = 0;
        std::size_t living = 0, infants_taught = 0, elders_taught = 0, elders = 0, archers = 0, shot = 0;
        double skill = 0.0;
        std::array<std::size_t, CRAFT_COUNT> known{}, tried{}, mastered{};
        std::array<std::array<std::size_t, 6>, CRAFT_COUNT> sources{};  // of those who know it, how many came to it each way
        std::array<int, CRAFT_COUNT> chain{};
        std::array<std::size_t, WEAPON_NAMES.size()> wielding{};
        std::array<std::size_t, ARMOUR_NAMES.size()> wearing{};
        double oldest = 0;
        for (const Bitzkey* agent : world.living()) {
            if (agent->species != species) continue;
            ++living;
            infants_taught += agent->infant_taught;
            elders_taught += agent->taught_by >= 0;
            elders += agent->age > AGEING * agent->lifespan;  // old enough to teach
            if (agent->lifespan > 0) oldest = std::max(oldest, agent->age / agent->lifespan);
            for (std::size_t craft = 0; craft < CRAFT_COUNT; ++craft) {
                known[craft] += (agent->crafts >> craft & 1) != 0;
                tried[craft] += agent->craft_tries[craft] > 0;
                if (!(agent->crafts >> craft & 1)) continue;
                ++sources[craft][static_cast<std::size_t>(agent->craft_source[craft])];
                mastered[craft] += agent->craft_tries[craft] >= CRAFT_TRIES;
                chain[craft] = std::max(chain[craft], agent->craft_chain[craft]);
            }
            ++wielding[static_cast<std::size_t>(agent->weapon)];
            ++wearing[static_cast<std::size_t>(agent->armour)];
            archers += agent->bow_uses > 0;
            if (agent->archery > 0) {  // any who have ever shot
                ++shot;
                skill += agent->archery;
            }
            const auto hide = static_cast<std::size_t>(std::clamp(agent->hide, 0, 3));
            ++hides[hide];
            regarded[hide] += agent->regarded;
            envious += world.envy_of(*agent) > 0;
            for (std::size_t kind = 0; kind < desired.size(); ++kind) desired[kind] += agent->desire[kind];
            ++counted;
            fighting += world.fight_of(agent->identifier) != nullptr;
            thieves += agent->thefts > 0;
            thefts += static_cast<std::size_t>(agent->thefts);
            held_liars += liars.contains(agent->identifier);
            leaning += agent->meat_lean > 0;
            tasted += agent->cooked_known > 0;
            loving += agent->cooked_known >= 1.0;
        }
        Json worn = Json::object(), esteem = Json::object();
        for (std::size_t hide = 0; hide < hides.size(); ++hide) {
            const std::string name = hide ? std::string(SPECIES[hide]) : std::string("none");
            worn[name] = hides[hide];
            esteem[name] = hides[hide] ? Json(regarded[hide] / static_cast<double>(hides[hide])) : Json(nullptr);
        }
        const Json possessions = {{"hides", worn}, {"regarded_by_hide", esteem}, {"envious", envious}, {"in_a_fight", fighting},
                                  {"thieves", thieves}, {"thefts", thefts}, {"held_liars", held_liars}, {"leaning_to_meat", leaning},
                                  {"tasted_cooked", tasted}, {"love_cooked", loving},
                                  {"mean_desire", {{"weapon", counted ? desired[WeaponDesire] / counted : 0.0},
                                                   {"armour", counted ? desired[ArmourDesire] / counted : 0.0},
                                                   {"bow", counted ? desired[BowDesire] / counted : 0.0}}}};
        Json crafts = Json::object(), weapons = Json::object(), armours = Json::object();
        for (std::size_t craft = 0; craft < CRAFT_COUNT; ++craft) {
            crafts[std::string(ACTIONS[MakeBoneAxe + craft])] = {{"known", known[craft]}, {"tried", tried[craft]}, {"mastered", mastered[craft]}, {"unrecorded", sources[craft][0]},
                                                                 {"discovered", sources[craft][1]}, {"by_parent", sources[craft][2]}, {"by_elder", sources[craft][3]}, {"watched", sources[craft][4]}, {"tutored", sources[craft][5]}, {"longest_chain", chain[craft]}};
        }
        for (std::size_t weapon = 0; weapon < WEAPON_NAMES.size(); ++weapon) weapons[WEAPON_NAMES[weapon]] = wielding[weapon];
        for (std::size_t armour = 0; armour < ARMOUR_NAMES.size(); ++armour) armours[ARMOUR_NAMES[armour]] = wearing[armour];
        result[std::string(species_name(species))] = {{"living", living}, {"infants_taught", infants_taught}, {"taught_by_elder", elders_taught},
                                                      {"elders", elders}, {"oldest_share", oldest}, {"crafts", crafts},
                                                      {"wielding", weapons}, {"wearing", armours}, {"possessions", possessions},
                                                      {"archery", {{"bows", archers}, {"have_shot", shot}, {"mean_skill_of_those", shot > 0 ? Json(skill / static_cast<double>(shot)) : Json(nullptr)}}}};
    }
    int fires = 0, shelters = 0, sites = 0, arable = 0, roads = 0, road_works = 0, fertilized = 0;
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            roads += world.road_at(x, y);
            road_works += world.road_work_at(x, y) > 0;
            fertilized += world.fertilized_at(x, y);
            fires += world.fire_at(x, y) > world.turn();
            shelters += world.shelter_at(x, y);
            sites += world.site_at(x, y) > 0;
            arable += world.arable_at(x, y);
        }
    }
    return {{"turn", world.turn()}, {"skills", result}, {"fights", world.fights().size()}, {"works", {{"arable", arable}, {"fertilized", fertilized}, {"fires", fires}, {"shelters", shelters}, {"sites", sites}, {"roads", roads}, {"roads_begun", road_works}}}};
}

Json Probe::regard(Simulation& world) const {
    // What regard is doing: how close animals stay to those they know, and, of each animal's last choice with a hunter
    // in sight, how often it fled having heard a friend, an enemy, or a caller it doesn't know. The counts are since
    // this run began, and blows at its own kind can be read against chance, the share of each among those in sight.
    const auto share = [](long long part, long long whole) { return whole > 0 ? Json(static_cast<double>(part) / whole) : Json(nullptr); };
    const auto mean = [](double sum, long long count) { return count > 0 ? Json(sum / count) : Json(nullptr); };
    Json species_results = Json::object();
    for (const Species species : {Species::Grazer, Species::Omnivore, Species::Predator}) {
        struct Tie {
            long long known = 0, in_reach = 0;
            double steps = 0;
        };
        std::array<Tie, 3> ties{};  // enemies, the rest, friends
        struct Heard {
            long long heard = 0, fled = 0;
        };
        std::array<Heard, 4> callers{};  // enemies, the rest it knows, friends, unknown
        long long hostile = 0, friendly = 0, held = 0;  // factions
        Tie dangerous, skilled;  // those it holds at half or more of either
        for (const Bitzkey* agent : world.living()) {
            if (agent->species != species) continue;
            for (const Acquaintance& known : agent->known) {
                const std::size_t kind = known.regard <= -REGARD_STRONG ? 0 : known.regard >= REGARD_STRONG ? 2 : 1;
                ties[kind].known += 1;
                const Bitzkey* other = world.find_agent(known.identifier);
                if (!other || !other->alive()) continue;
                ties[kind].in_reach += 1;
                ties[kind].steps += std::abs(other->x - agent->x) + std::abs(other->y - agent->y);
                for (Tie* held_as : {known.danger >= 0.5 ? &dangerous : nullptr, known.skill >= 0.5 ? &skilled : nullptr}) {
                    if (!held_as) continue;
                    held_as->in_reach += 1;
                    held_as->steps += std::abs(other->x - agent->x) + std::abs(other->y - agent->y);
                }
            }
            for (const Faction& faction : agent->factions) {
                held += 1;
                hostile += faction.regard <= -REGARD_STRONG;
                friendly += faction.regard >= REGARD_STRONG;
            }
            if (!agent->has_last_senses || agent->last_senses[SenseThreatCloseness] <= 0) continue;
            const Senses& senses = agent->last_senses;
            bool heard_any = false;
            for (int call = FirstCall; call < FirstCall + CALL_COUNT; ++call) heard_any = heard_any || senses[heard_sense(call, HeardLoudness)] > 0;
            if (!heard_any) continue;
            const double caller = senses[SenseCallerRegard];
            Heard& heard = callers[senses[SenseCallerKnown] <= 0 ? 3 : caller <= -REGARD_STRONG ? 0 : caller >= REGARD_STRONG ? 2 : 1];
            heard.heard += 1;
            heard.fled += agent->last_choice == Flee;
        }
        const auto tie = [&](std::size_t kind) {
            return Json{{"remembered", ties[kind].known}, {"alive", ties[kind].in_reach}, {"mean_steps_away", mean(ties[kind].steps, ties[kind].in_reach)}};
        };
        const auto caller = [&](std::size_t kind) { return Json{{"heard", callers[kind].heard}, {"fled", share(callers[kind].fled, callers[kind].heard)}}; };
        species_results[std::string(species_name(species))] = {
            {"enemies", tie(0)}, {"others", tie(1)}, {"friends", tie(2)},
            {"dangerous", {{"alive", dangerous.in_reach}, {"mean_steps_away", mean(dangerous.steps, dangerous.in_reach)}}},
            {"skilled", {{"alive", skilled.in_reach}, {"mean_steps_away", mean(skilled.steps, skilled.in_reach)}}},
            {"factions", {{"held", held}, {"hostile", hostile}, {"friendly", friendly}}},
            {"with_hunter_in_sight", {{"heard_enemy", caller(0)}, {"heard_other_known", caller(1)}, {"heard_friend", caller(2)}, {"heard_unknown", caller(3)}}}};
    }
    const auto count = [&](Simulation::RegardCount which) { return world.regard_counts[which].load(); };
    return {{"turn", world.turn()}, {"species", species_results},
            {"blows_since_start", {{"at_enemies", count(Simulation::BlowsEnemy)}, {"at_friends", count(Simulation::BlowsFriend)},
                                   {"at_own_kind", count(Simulation::KinBlows)},
                                   {"own_kind_share_at_enemies", share(count(Simulation::KinBlowsEnemy), count(Simulation::KinBlows))},
                                   {"own_kind_share_at_friends", share(count(Simulation::KinBlowsFriend), count(Simulation::KinBlows))},
                                   {"chance_of_enemy", share(count(Simulation::EnemiesInSight), count(Simulation::KinInSight))},
                                   {"chance_of_friend", share(count(Simulation::FriendsInSight), count(Simulation::KinInSight))}}},
            {"alerts_since_start", {{"heeded_from_friends", count(Simulation::AlertsFromFriends)}, {"heeded_from_others", count(Simulation::AlertsFromOthers)},
                                    {"refused_from_enemies", count(Simulation::AlertsRefused)}}}};
}

Json Probe::society(Simulation& world) const {
    // Whether groups and leaders are forming, by species. Each closeness is in steps, to be read against `strangers`, the
    // mean distance of unrelated animals of the species: a group that means anything stands nearer than that.
    //   families: those living descended from one living mother, and how near mother and child stay.
    //   tribes: those joined by friendship both ways, directly or through others.
    //   factions: animals within Faction width of one colour, and whether they think better of their own than of others,
    //   keep near their own colour (territory) and hold as foes, or fight, those of their colour or of others.
    //   leaders: those the most hold as friends, or as skilled, and whether their followers stay near them.
    const auto steps = [](const Bitzkey& a, const Bitzkey& b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); };
    const auto mean = [](double sum, long long count) { return count > 0 ? Json(sum / count) : Json(nullptr); };
    Json result = Json::object();
    for (const Species species : {Species::Grazer, Species::Omnivore, Species::Predator}) {
        std::vector<const Bitzkey*> animals;
        std::unordered_map<int, std::size_t> index;
        for (const Bitzkey* agent : world.living()) {
            if (agent->species != species) continue;
            index[agent->identifier] = animals.size();
            animals.push_back(agent);
        }
        const std::size_t count = animals.size();
        double apart = 0;
        for (std::size_t at = 0; at < count; ++at) apart += steps(*animals[at], *animals[(at + count / 2) % count]);

        // Unions of the living: `joined` is its own for families and tribes in turn.
        std::vector<std::size_t> joined(count);
        const auto root = [&](std::size_t at) {
            while (joined[at] != at) at = joined[at] = joined[joined[at]];
            return at;
        };
        const auto groups = [&](double spread_sum, long long spread_count) {
            std::unordered_map<std::size_t, long long> sizes;
            for (std::size_t at = 0; at < count; ++at) sizes[root(at)] += 1;
            long long largest = 0, of_three = 0, within = 0;
            for (const auto& [group, size] : sizes) {
                largest = std::max(largest, size);
                if (size >= 3) {
                    of_three += 1;
                    within += size;
                }
            }
            return Json{{"groups_of_three_or_more", of_three}, {"largest", largest}, {"share_in_a_group", count ? Json(static_cast<double>(within) / count) : Json(nullptr)},
                        {"mean_steps_apart", mean(spread_sum, spread_count)}};
        };

        std::iota(joined.begin(), joined.end(), std::size_t{0});
        double family_steps = 0;
        long long family_ties = 0;
        for (std::size_t at = 0; at < count; ++at) {
            const auto mother = index.find(animals[at]->mother);
            if (mother == index.end()) continue;
            joined[root(at)] = root(mother->second);
            family_steps += steps(*animals[at], *animals[mother->second]);
            family_ties += 1;
        }
        const Json families = groups(family_steps, family_ties);

        std::iota(joined.begin(), joined.end(), std::size_t{0});
        struct Follow {
            long long friends = 0, skilled = 0, dangerous = 0, hostile = 0, friends_of_its_colour = 0;
            double friend_steps = 0;
        };
        std::vector<Follow> follows(count);
        double tribe_steps = 0;
        long long tribe_ties = 0;
        for (std::size_t at = 0; at < count; ++at) {
            for (const Acquaintance& known : animals[at]->known) {
                const auto other = index.find(known.identifier);
                if (other == index.end()) continue;
                Follow& follow = follows[other->second];
                follow.skilled += known.skill >= 0.5;
                follow.dangerous += known.danger >= 0.5;
                follow.hostile += known.regard <= -REGARD_STRONG;
                if (known.regard < REGARD_STRONG) continue;
                follow.friends += 1;
                follow.friend_steps += steps(*animals[at], *animals[other->second]);
                follow.friends_of_its_colour += colour_difference(body_word(animals[at]->dna), body_word(animals[other->second]->dna)) <= FACTION_WIDTH;
                const auto& theirs = animals[other->second]->known;
                const bool mutual = std::any_of(theirs.begin(), theirs.end(), [&](const Acquaintance& one) {
                    return one.identifier == animals[at]->identifier && one.regard >= REGARD_STRONG;
                });
                if (!mutual) continue;
                joined[root(at)] = root(other->second);
                tribe_steps += steps(*animals[at], *animals[other->second]);
                tribe_ties += 1;
            }
        }
        const Json tribes = groups(tribe_steps, tribe_ties);
        std::unordered_map<std::size_t, long long> tribe_sizes;
        for (std::size_t at = 0; at < count; ++at) tribe_sizes[root(at)] += 1;

        // Colours, each the first animal met of it, in turn: too many to compare all against all, so the largest are kept.
        struct Colour {
            std::uint32_t ring = 0;
            long long members = 0, hostile = 0, friendly = 0;  // of all animals' views of it
        };
        std::vector<Colour> colours;
        for (const Bitzkey* agent : animals) {
            const std::uint32_t ring = body_word(agent->dna);
            const auto found = std::find_if(colours.begin(), colours.end(), [&](const Colour& one) { return colour_difference(one.ring, ring) <= FACTION_WIDTH; });
            if (found != colours.end()) found->members += 1;
            else if (colours.size() < 200) colours.push_back({ring, 1});
        }
        double own_regard = 0, other_regard = 0;
        long long own_views = 0, other_views = 0;
        for (const Bitzkey* agent : animals) {
            const std::uint32_t ring = body_word(agent->dna);
            for (const Faction& view : agent->factions) {
                const bool own = colour_difference(view.ring, ring) <= FACTION_WIDTH;
                (own ? own_regard : other_regard) += view.regard;
                (own ? own_views : other_views) += 1;
                for (Colour& colour : colours) {
                    if (colour_difference(colour.ring, view.ring) > FACTION_WIDTH) continue;
                    colour.hostile += view.regard <= -REGARD_STRONG;
                    colour.friendly += view.regard >= REGARD_STRONG;
                    break;
                }
            }
        }
        // Territory and enmity of colours: neighbours within TERRITORY steps against pairs drawn across the world, and foes
        // and fights within a colour or across. Kin (parent, child or sibling) share a colour and stay near, so the unrelated
        // are counted apart: colour that gathers strangers is chosen ground, not only family.
        constexpr int TERRITORY = 5;
        const auto alike = [](const Bitzkey& one, const Bitzkey& other) { return colour_difference(body_word(one.dna), body_word(other.dna)) <= FACTION_WIDTH; };
        long long near_pairs = 0, near_alike = 0, near_kin = 0, unrelated_alike = 0, far_pairs = 0, far_alike = 0, foes = 0, foes_alike = 0, unrelated_foes = 0,
                  unrelated_foes_alike = 0, fights_within = 0, fights_across = 0;
        for (std::size_t at = 0; at < count; ++at) {
            const Bitzkey& agent = *animals[at];
            for (const Bitzkey* other : world.around(agent, TERRITORY)) {
                if (!other->alive() || other->species != species) continue;
                near_pairs += 1;
                near_alike += alike(agent, *other);
                if (Simulation::family(agent, *other)) near_kin += 1;
                else unrelated_alike += alike(agent, *other);
            }
            for (const std::size_t step : {count / 2, count / 3, count / 5}) {
                if (step == 0) continue;
                far_pairs += 1;
                far_alike += alike(agent, *animals[(at + step) % count]);
            }
            for (const Acquaintance& known : agent.known) {
                if (known.regard > -REGARD_STRONG) continue;
                const auto other = index.find(known.identifier);
                if (other == index.end()) continue;
                foes += 1;
                foes_alike += alike(agent, *animals[other->second]);
                if (Simulation::family(agent, *animals[other->second])) continue;
                unrelated_foes += 1;
                unrelated_foes_alike += alike(agent, *animals[other->second]);
            }
        }
        for (const Simulation::Fight& fight : world.fights()) {
            const auto thief = index.find(fight.thief), victim = index.find(fight.victim);
            if (thief == index.end() || victim == index.end()) continue;
            (alike(*animals[thief->second], *animals[victim->second]) ? fights_within : fights_across) += 1;
        }
        const auto part = [](long long some, long long of) { return of > 0 ? Json(static_cast<double>(some) / of) : Json(nullptr); };
        const Json territory = {{"neighbours_of_their_colour", part(near_alike, near_pairs)}, {"by_chance_of_their_colour", part(far_alike, far_pairs)},
                                {"neighbours_kin", part(near_kin, near_pairs)}, {"unrelated_neighbours_of_their_colour", part(unrelated_alike, near_pairs - near_kin)},
                                {"foes", foes}, {"foes_of_their_colour", part(foes_alike, foes)},
                                {"unrelated_foes", unrelated_foes}, {"unrelated_foes_of_their_colour", part(unrelated_foes_alike, unrelated_foes)},
                                {"fights_within_a_colour", fights_within}, {"fights_across_colours", fights_across}};

        std::sort(colours.begin(), colours.end(), [](const Colour& a, const Colour& b) { return a.members > b.members; });
        Json largest_colours = Json::array();
        for (std::size_t at = 0; at < std::min<std::size_t>(colours.size(), 5); ++at) {
            char ring[9];
            std::snprintf(ring, sizeof ring, "%08x", colours[at].ring);
            largest_colours.push_back({{"ring", ring}, {"members", colours[at].members}, {"held_hostile_by", colours[at].hostile}, {"held_friendly_by", colours[at].friendly}});
        }

        std::vector<std::size_t> ranked(count);
        std::iota(ranked.begin(), ranked.end(), std::size_t{0});
        const auto standing = [&](std::size_t at) { return follows[at].friends + follows[at].skilled; };
        std::sort(ranked.begin(), ranked.end(), [&](std::size_t a, std::size_t b) { return standing(a) != standing(b) ? standing(a) > standing(b) : animals[a]->identifier < animals[b]->identifier; });
        Json leaders = Json::array();
        long long followed_by_three = 0, followers = 0, top_followers = 0;
        // Lineages: how a leader's living parents are regarded, and the top twenty's family ties to each other.
        const auto parents_regarded = [&](const Bitzkey& one) {
            Json held = Json::array();
            for (const int parent : {one.mother, one.father}) {
                const Bitzkey* found = parent >= 0 ? world.find_agent(parent) : nullptr;
                if (found && found->alive()) held.push_back(found->regarded);
            }
            return held;
        };
        std::vector<const Bitzkey*> top_twenty;
        for (std::size_t at = 0; at < std::min<std::size_t>(count, 20); ++at) {
            if (standing(ranked[at]) > 0) top_twenty.push_back(animals[ranked[at]]);
        }
        const auto kin_in_top = [&](const Bitzkey& one) {
            return std::count_if(top_twenty.begin(), top_twenty.end(), [&](const Bitzkey* other) { return other != &one && Simulation::family(one, *other); });
        };
        // Factions about leaders: whether the top twenty's friends share their colour more than friends do at large, and lead tribes.
        long long with_kin = 0, with_regarded_parent = 0, top_friends = 0, top_friends_of_colour = 0, all_friends = 0, all_friends_of_colour = 0, top_in_a_tribe = 0;
        for (const Follow& follow : follows) {
            all_friends += follow.friends;
            all_friends_of_colour += follow.friends_of_its_colour;
        }
        for (std::size_t at = 0; at < top_twenty.size(); ++at) {
            top_friends += follows[ranked[at]].friends;
            top_friends_of_colour += follows[ranked[at]].friends_of_its_colour;
            top_in_a_tribe += tribe_sizes[root(ranked[at])] >= 3;
        }
        for (const Bitzkey* one : top_twenty) {
            with_kin += kin_in_top(*one) > 0;
            const Json held = parents_regarded(*one);
            with_regarded_parent += std::any_of(held.begin(), held.end(), [](const Json& value) { return value.get<double>() >= 0.5; });
        }
        for (std::size_t at = 0; at < count; ++at) {
            const long long held = standing(ranked[at]);
            followers += held;
            followed_by_three += follows[ranked[at]].friends >= 3;
            if (at < 5) top_followers += held;
            if (at >= 5 || held == 0) continue;
            const Follow& follow = follows[ranked[at]];
            const Bitzkey& leader = *animals[ranked[at]];
            leaders.push_back({{"id", leader.identifier}, {"friends", follow.friends}, {"held_skilled_by", follow.skilled}, {"held_dangerous_by", follow.dangerous},
                               {"enemies", follow.hostile}, {"friends_mean_steps", mean(follow.friend_steps, follow.friends)},
                               {"age_share", static_cast<double>(leader.age) / std::max(1.0, static_cast<double>(leader.lifespan))}, {"elder", leader.age > AGEING * leader.lifespan},
                               {"regarded", leader.regarded}, {"mother", leader.mother}, {"father", leader.father},
                               {"parents_regarded", parents_regarded(leader)}, {"kin_in_top_twenty", kin_in_top(leader)},
                               {"friends_of_its_colour", follow.friends_of_its_colour}, {"its_tribe", tribe_sizes[root(ranked[at])]}});
        }
        Json recognised = Json::array();  // the leaders recognised at the last count, the most followed first
        long long following = 0;
        std::vector<const Bitzkey*> heads;
        for (const Bitzkey* one : animals) {
            following += one->follows >= 0;
            if (one->leader) heads.push_back(one);
        }
        std::sort(heads.begin(), heads.end(), [](const Bitzkey* a, const Bitzkey* b) { return a->followers > b->followers; });
        // Chains of leaders: a leader may follow another. Followed up from each, until one follows no leader (the top) or
        // the way comes round again (a loop); depth counts the leaders above it.
        std::unordered_map<int, const Bitzkey*> headAt;
        for (const Bitzkey* head : heads) headAt[head->identifier] = head;
        long long followingALeader = 0, mutual = 0, inLoops = 0, deepest = 0;
        std::map<long long, long long> byDepth;  // leaders at each depth below a top leader, 0 a top leader itself
        for (const Bitzkey* head : heads) {
            const auto above = headAt.find(head->follows);
            if (above != headAt.end()) {
                ++followingALeader;
                mutual += above->second->follows == head->identifier && head->identifier < above->second->identifier;
            }
            std::unordered_set<int> seen{head->identifier};
            long long depth = 0;
            bool loop = false;
            for (auto at = headAt.find(head->follows); at != headAt.end(); at = headAt.find(at->second->follows)) {
                if (!seen.insert(at->first).second) {
                    loop = true;
                    break;
                }
                ++depth;
            }
            if (loop) ++inLoops;
            else {
                ++byDepth[depth];
                deepest = std::max(deepest, depth);
            }
        }
        Json depths = Json::object();
        for (const auto& [depth, count] : byDepth) depths[std::to_string(depth)] = count;
        const Json chains = {{"leaders_following_a_leader", followingALeader}, {"mutual_pairs", mutual}, {"in_loops", inLoops},
                             {"deepest", deepest}, {"leaders_at_depth", depths}};
        std::array<long long, 4> kinFollowers{};  // all leaders' followers: their young, their parents, siblings, unrelated
        long long onlyYoung = 0;  // leaders followed by none but their own young
        for (const Bitzkey* head : heads) {
            int nearby = 0, young = 0, elders = 0, siblings = 0;
            double apartSum = 0.0;
            for (const Bitzkey* one : animals) {
                if (one->follows != head->identifier) continue;
                ++nearby;
                apartSum += steps(*one, *head);
                if (one->mother == head->identifier || one->father == head->identifier) ++young;  // its own young
                else if (head->mother == one->identifier || head->father == one->identifier) ++elders;  // its parent
                else if ((one->mother >= 0 && one->mother == head->mother) || (one->father >= 0 && one->father == head->father)) ++siblings;
            }
            kinFollowers[0] += young;
            kinFollowers[1] += elders;
            kinFollowers[2] += siblings;
            kinFollowers[3] += nearby - young - elders - siblings;
            onlyYoung += nearby > 0 && young == nearby;
            if (recognised.size() >= 20) continue;
            recognised.push_back({{"id", head->identifier}, {"followers", head->followers}, {"followers_now", nearby},
                                  {"followers_kin", {{"its_young", young}, {"its_parents", elders}, {"its_siblings", siblings}, {"unrelated", nearby - young - elders - siblings}}},
                                  {"followers_mean_steps", nearby ? Json(apartSum / nearby) : Json(nullptr)},
                                  {"age_share", static_cast<double>(head->age) / std::max(1.0, static_cast<double>(head->lifespan))},
                                  {"elder", head->age > AGEING * head->lifespan}, {"regarded", head->regarded}, {"realm", head->realm}, {"follows", head->follows},
                                  {"mark", head->mark == PrestigeMark ? "prestige" : head->mark == WarriorMark ? "warrior" : "none"}});
        }
        result[std::string(species_name(species))] = {
            {"living", count}, {"strangers_mean_steps", mean(apart, static_cast<long long>(count))}, {"families", families}, {"tribes", tribes},
            {"factions", {{"colours", colours.size()}, {"largest", largest_colours}, {"regard_for_own_colour", mean(own_regard, own_views)}, {"regard_for_other_colours", mean(other_regard, other_views)}, {"territory", territory}}},
            {"leaders", {{"recognised", heads.size()}, {"following_a_leader", following}, {"chains", chains}, {"followers_kin", {{"their_young", kinFollowers[0]}, {"their_parents", kinFollowers[1]}, {"their_siblings", kinFollowers[2]}, {"unrelated", kinFollowers[3]}}}, {"followed_only_by_their_young", onlyYoung}, {"recognised_leaders", recognised}, {"followed_by_three_or_more", followed_by_three}, {"top_five_share_of_standing", followers > 0 ? Json(static_cast<double>(top_followers) / followers) : Json(nullptr)}, {"top", leaders},
                         {"lineage", {{"top_twenty", top_twenty.size()}, {"with_kin_in_top_twenty", with_kin}, {"with_a_well_regarded_living_parent", with_regarded_parent}}},
                         {"factions", {{"top_twenty_friends_of_their_colour", top_friends ? Json(static_cast<double>(top_friends_of_colour) / top_friends) : Json(nullptr)},
                                       {"all_friends_of_their_colour", all_friends ? Json(static_cast<double>(all_friends_of_colour) / all_friends) : Json(nullptr)},
                                       {"top_twenty_in_a_tribe_of_three_or_more", top_in_a_tribe}}}}}};
    }
    return {{"turn", world.turn()}, {"species", result}};
}

Json Probe::diary(Simulation& world) const {
    Json entries = Json::array();
    for (const Simulation::Milestone& entry : world.diary()) {
        entries.push_back({{"turn", entry.turn}, {"event", entry.event}, {"species", entry.species}, {"craft", entry.craft}, {"who", entry.who}});
    }
    return {{"turn", world.turn()}, {"since", world.diary_since()}, {"entries", entries}};
}

// Trade would show as gifts returned: one that gives to another is given something back by it later, the more so than
// chance, and the more so in another kind of possession (an exchange). Each species is read apart.
Json Probe::gifts(Simulation& world) const {
    static constexpr std::array<const char*, 3> KINDS = {"hide", "weapon", "armour"};
    const std::vector<Simulation::Gift> all = world.gifts_so_far();
    Json bySpecies = Json::object();
    for (std::size_t index = 0; index < SPECIES.size(); ++index) {
        std::vector<const Simulation::Gift*> list;
        for (const auto& gift : all) {
            if (static_cast<std::size_t>(gift.species) == index) list.push_back(&gift);
        }
        if (list.empty()) continue;
        std::map<std::pair<int, int>, std::vector<const Simulation::Gift*>> byPair;  // (giver, receiver), in order
        std::unordered_map<int, std::vector<const Simulation::Gift*>> byGiver, byReceiver;
        for (const auto* gift : list) {
            byPair[{gift->giver, gift->receiver}].push_back(gift);
            byGiver[gift->giver].push_back(gift);
            byReceiver[gift->receiver].push_back(gift);
        }
        long long kin = 0, sameColour = 0, returned = 0, exchanged = 0, receiverGaveLater = 0, passedOn = 0, giverGotLater = 0;
        long long lagTotal = 0;
        std::array<long long, 3> kinds{};
        for (const auto* gift : list) {
            kin += gift->kin;
            sameColour += gift->giver_realm >= 0 && gift->giver_realm == gift->receiver_realm;
            ++kinds[static_cast<std::size_t>(std::clamp(gift->possession, 0, 2))];
            const auto back = byPair.find({gift->receiver, gift->giver});
            const Simulation::Gift* firstBack = nullptr;
            if (back != byPair.end()) {
                for (const auto* other : back->second) {
                    if (other->turn > gift->turn || (other->turn == gift->turn && other != gift)) { firstBack = other; break; }
                }
            }
            if (firstBack) {
                ++returned;
                lagTotal += firstBack->turn - gift->turn;
                exchanged += firstBack->possession != gift->possession;
            }
            const auto later = byGiver.find(gift->receiver);
            if (later != byGiver.end()) {
                bool any = false, onward = false;
                for (const auto* other : later->second) {
                    if (other->turn < gift->turn) continue;
                    any = true;
                    onward |= other->receiver != gift->giver;
                }
                receiverGaveLater += any;
                passedOn += onward;
            }
            const auto got = byReceiver.find(gift->giver);
            if (got != byReceiver.end()) {
                giverGotLater += std::any_of(got->second.begin(), got->second.end(), [&](const auto* other) { return other->turn > gift->turn; });
            }
        }
        long long pairsRepeated = 0, mutualPairs = 0;
        for (const auto& [pair, gifts] : byPair) {
            pairsRepeated += gifts.size() > 1;
            mutualPairs += pair.first < pair.second && byPair.contains({pair.second, pair.first});
        }
        const double n = static_cast<double>(list.size());
        Json possessions = Json::object();
        for (std::size_t kind = 0; kind < KINDS.size(); ++kind) possessions[KINDS[kind]] = kinds[kind];
        bySpecies[std::string(SPECIES[index])] = {
            {"gifts", list.size()},
            {"givers", byGiver.size()},
            {"receivers", byReceiver.size()},
            {"possessions", possessions},
            {"to_kin", kin / n},
            {"to_own_colour", sameColour / n},
            {"pairs", byPair.size()},
            {"pairs_giving_more_than_once", pairsRepeated},
            {"pairs_giving_both_ways", mutualPairs},
            {"returned", returned / n},
            {"returned_in_another_kind", exchanged / n},
            {"mean_turns_until_returned", returned ? static_cast<double>(lagTotal) / returned : 0.0},
            {"receiver_gave_anything_later", receiverGaveLater / n},
            {"receiver_passed_a_gift_on", passedOn / n},
            {"giver_received_anything_later", giverGotLater / n}};
    }
    // Offers: how often accepted, and who comes off better, the sharper or the duller of the two.
    static constexpr std::array<const char*, GOODS> GOOD_NAMES = {"hide", "weapon", "armour", "bow", "meat", "fruit"};
    const std::vector<Simulation::Trade> trades = world.trades_so_far();
    Json offers = Json::object();
    for (std::size_t index = 0; index < SPECIES.size(); ++index) {
        long long made = 0, withdrawn = 0, accepted = 0, fair = 0, sharperGained = 0, sharperCounted = 0;
        double offererGain = 0.0, ownerGain = 0.0;
        std::array<long long, GOODS> wanted{}, given{};
        for (const auto& trade : trades) {
            if (static_cast<std::size_t>(trade.species) != index) continue;
            ++made;
            withdrawn += trade.withdrawn;
            if (!trade.accepted) continue;
            ++accepted;
            fair += trade.offerer_gain > 0 && trade.owner_gain > 0;
            offererGain += trade.offerer_gain;
            ownerGain += trade.owner_gain;
            ++wanted[static_cast<std::size_t>(trade.wanted)];
            ++given[static_cast<std::size_t>(trade.offered)];
            if (trade.offerer_wits != trade.owner_wits) {
                ++sharperCounted;
                const bool offererSharper = trade.offerer_wits > trade.owner_wits;
                sharperGained += (offererSharper ? trade.offerer_gain - trade.owner_gain : trade.owner_gain - trade.offerer_gain) > 0;
            }
        }
        if (!made) continue;
        Json taken = Json::object(), paid = Json::object();
        for (std::size_t good = 0; good < GOODS; ++good) {
            taken[GOOD_NAMES[good]] = wanted[good];
            paid[GOOD_NAMES[good]] = given[good];
        }
        const double deals = std::max<long long>(1, accepted);
        offers[std::string(SPECIES[index])] = {
            {"offers", made},
            {"withdrawn", withdrawn},
            {"accepted", accepted},
            {"accepted_share_of_those_put", made > withdrawn ? static_cast<double>(accepted) / (made - withdrawn) : 0.0},
            {"both_truly_gained", fair / deals},
            {"mean_gain_of_offerer", offererGain / deals},
            {"mean_gain_of_owner", ownerGain / deals},
            {"sharper_came_off_better", sharperCounted ? static_cast<double>(sharperGained) / sharperCounted : 0.0},
            {"goods_wanted", taken},
            {"goods_offered", paid}};
    }
    return {{"turn", world.turn()},
            {"logged", all.size()},
            {"offers", offers},
            {"since", all.empty() ? world.turn() : all.front().turn},
            {"species", bySpecies}};
}

// Opinion and land. Each animal's regard for a colour group is its regard for that colour (its faction memory), none if it
// holds no view. Read two ways:
//   animals: for every animal and every land-holding group not its own, how often it stands in that group's land, by
//   what it thinks of the group: hostile (-0.5 or below), cool, warm, friendly (0.5 or above), or no view.
//   groups: for every two land-holding groups, what the first's members think of the second, on average, and whether
//   they are at war (contesting land) or at peace.
Json Probe::opinions(Simulation& world) const {
    const auto view_of = [](const Bitzkey& agent, std::uint32_t ring) -> std::optional<double> {
        for (const Faction& faction : agent.factions) {
            if (colour_difference(faction.ring, ring) <= FACTION_WIDTH) return faction.regard;
        }
        return std::nullopt;
    };
    std::map<int, const Realm*> holders;  // the groups holding land
    for (const auto& [identifier, realm] : world.realms()) {
        if (!realm.patches.empty()) holders[identifier] = &realm;
    }
    static constexpr std::array<const char*, 5> BUCKETS = {"hostile", "cool", "warm", "friendly", "no_view"};
    const auto bucket = [](std::optional<double> regard) -> std::size_t {
        if (!regard) return 4;
        return *regard <= -0.5 ? 0 : *regard < 0 ? 1 : *regard < 0.5 ? 2 : 3;
    };
    std::array<long long, 5> pairs{}, inside{};
    std::map<std::pair<int, int>, std::pair<double, long long>> groupView;  // (of, about): regard summed, views held
    std::map<std::pair<int, int>, long long> groupInside;  // (of, about): members standing in the other's land
    std::map<int, long long> membersAlive;
    double trespasserViewOfOwner = 0.0, trespasserViewOfOthers = 0.0;
    long long trespasserOwnerViews = 0, trespasserOtherViews = 0;
    for (const Bitzkey* agent : world.living()) {
        if (agent->species == Species::Plant || agent->realm < 0) continue;
        ++membersAlive[agent->realm];
        std::set<int> owners;  // the groups whose land it stands in
        if (const LandClaims* land = world.claims_at(agent->x, agent->y)) {
            for (const Claim& claim : *land) {
                if (claim.realm >= 0 && claim.realm != agent->realm) owners.insert(claim.realm);
            }
        }
        for (const auto& [identifier, realm] : holders) {
            if (identifier == agent->realm) continue;
            const std::optional<double> view = view_of(*agent, realm->ring);
            const std::size_t at = bucket(view);
            const bool in = owners.contains(identifier);
            ++pairs[at];
            inside[at] += in;
            groupInside[{agent->realm, identifier}] += in;
            if (view) {
                auto& [sum, count] = groupView[{agent->realm, identifier}];
                sum += *view;
                ++count;
                if (!owners.empty()) {
                    if (in) {
                        trespasserViewOfOwner += *view;
                        ++trespasserOwnerViews;
                    } else {
                        trespasserViewOfOthers += *view;
                        ++trespasserOtherViews;
                    }
                }
            }
        }
    }
    Json animals = Json::object();
    for (std::size_t at = 0; at < BUCKETS.size(); ++at) {
        animals[BUCKETS[at]] = {{"pairs", pairs[at]}, {"standing_in_their_land", inside[at]},
                                {"share_in_their_land", pairs[at] ? Json(static_cast<double>(inside[at]) / pairs[at]) : Json(nullptr)}};
    }
    const auto mean = [](double sum, long long count) { return count ? Json(sum / count) : Json(nullptr); };
    // Groups: at war or at peace, and what each side thinks of the other.
    std::set<std::pair<int, int>> atWar;
    for (const War& war : world.wars()) {
        atWar.insert({war.first, war.second});
        atWar.insert({war.second, war.first});
    }
    double warSum = 0.0, peaceSum = 0.0;
    long long warPairs = 0, peacePairs = 0;
    Json wars = Json::array();
    for (const auto& [key, held] : groupView) {
        const auto& [sum, count] = held;
        if (count < 5) continue;  // too few views to speak for the group
        const double regard = sum / count;
        if (atWar.contains(key)) {
            warSum += regard;
            ++warPairs;
            const long long members = membersAlive[key.first];
            wars.push_back({{"group", key.first}, {"about", key.second}, {"mean_regard", regard}, {"views", count},
                            {"members_in_their_land", groupInside[key]}, {"members", members}});
        } else {
            peaceSum += regard;
            ++peacePairs;
        }
    }
    return {{"turn", world.turn()},
            {"land_holding_groups", holders.size()},
            {"animals", animals},
            {"trespassers", {{"regard_for_the_land_they_stand_in", mean(trespasserViewOfOwner, trespasserOwnerViews)},
                             {"regard_for_other_land_holders", mean(trespasserViewOfOthers, trespasserOtherViews)}}},
            {"groups", {{"at_war", {{"pairs", warPairs}, {"mean_regard", mean(warSum, warPairs)}}},
                        {"at_peace", {{"pairs", peacePairs}, {"mean_regard", mean(peaceSum, peacePairs)}}},
                        {"wars", wars}}}};
}

Json Probe::lands(Simulation& world) const {
    // Colour groups and their lands: per species, how many groups, how many hold land, the living in their own land (and
    // of those, on land another group contests), trespassing (on land only other groups claim), and the marks of the
    // victor borne; the groups holding land, largest first; and the wars.
    const auto colour = [](std::uint32_t ring) {
        const Rgb rgb = color_from_word(ring);
        char text[8];
        std::snprintf(text, sizeof text, "#%02x%02x%02x", rgb.r, rgb.g, rgb.b);
        return std::string(text);
    };
    Json species_rows = Json::object();
    for (const Species species : {Species::Grazer, Species::Omnivore, Species::Predator}) {
        long long groups = 0, holding = 0, large = 0, squares = 0, living = 0, home = 0, contested = 0, trespassing = 0, warriors = 0, prestige = 0;
        for (const auto& [identifier, realm] : world.realms()) {
            if (realm.species != species) continue;
            groups += 1;
            large += realm.members >= LAND_SIZE;
            holding += !realm.patches.empty();
            squares += static_cast<long long>(realm.patches.size());
        }
        for (const Bitzkey* agent : world.living()) {
            if (agent->species != species) continue;
            living += 1;
            const bool own = world.in_own_land(*agent);
            home += own;
            contested += own && world.trespassing(*agent);  // its own land, claimed by another too
            trespassing += !own && world.trespassing(*agent);
            warriors += agent->mark == WarriorMark;
            prestige += agent->mark == PrestigeMark;
        }
        species_rows[std::string(species_name(species))] = {
            {"groups", groups}, {"groups_large_enough", large}, {"groups_holding_land", holding}, {"squares_held", squares},
            {"living", living}, {"in_own_land", home}, {"on_contested_own_land", contested}, {"trespassing", trespassing},
            {"marks", {{"warrior", warriors}, {"prestige", prestige}}}};
    }
    std::vector<const Realm*> holders;
    for (const auto& [identifier, realm] : world.realms()) {
        if (!realm.patches.empty()) holders.push_back(&realm);
    }
    std::sort(holders.begin(), holders.end(), [](const Realm* a, const Realm* b) { return a->patches.size() > b->patches.size(); });
    Json groups = Json::array();
    for (const Realm* realm : holders) {
        if (groups.size() >= 50) break;
        long long contested = 0;
        for (const int patch : realm->patches) {
            const LandClaims* land = world.square_claims(patch);
            contested += land && (*land)[0].realm >= 0 && (*land)[1].realm >= 0;
        }
        groups.push_back({{"id", realm->identifier}, {"species", std::string(species_name(realm->species))}, {"colour", colour(realm->ring)},
                          {"members", realm->members}, {"squares", realm->patches.size()}, {"contested", contested},
                          {"war_standing", world.war_standing(realm->identifier)}});
    }
    Json wars = Json::array();
    for (const War& war : world.wars()) {
        const auto first = world.realms().find(war.first), second = world.realms().find(war.second);
        wars.push_back({{"first", war.first}, {"second", war.second},
                        {"species", first != world.realms().end() ? std::string(species_name(first->second.species)) : ""},
                        {"against", second != world.realms().end() ? std::string(species_name(second->second.species)) : ""},
                        {"first_kills", war.first_kills}, {"second_kills", war.second_kills}, {"since", war.since},
                        {"first_standing", world.war_standing(war.first, war.second)}});
    }
    return {{"turn", world.turn()}, {"square", world.land_patch()}, {"counted_every", LAND_TURNS}, {"size_to_claim", LAND_SIZE},
            {"species", species_rows}, {"groups_holding_land", groups}, {"wars", wars}};
}

Json Probe::minds(Simulation& world) const {
    world.sync_brains();  // every brain as it now is
    Json result = Json::object();
    for (const Species species : {Species::Grazer, Species::Omnivore, Species::Predator}) {
        std::vector<double> reaches;  // each brain's farthest value from nothing, for what it last sensed
        std::size_t thinking = 0, runaway = 0, neurons = 0, jammed = 0;
        double depth = 0, width = 0;
        std::array<double, 3> surprise{}, surprised{};  // by the third of its lifespan it is in: foresight learnt falls with age
        for (const Bitzkey* agent : world.living()) {
            if (agent->species != species || !agent->brain) continue;
            ++thinking;
            depth += agent->brain->depth();
            width += agent->brain->width();
            if (!agent->has_last_senses) continue;
            const auto third = static_cast<std::size_t>(std::clamp(3.0 * agent->age / agent->lifespan, 0.0, 2.0));
            surprise[third] += agent->surprise;
            surprised[third] += 1;
            const Values values = agent->brain->values(agent->last_senses);
            double reach = 0;
            for (const double value : values) reach = std::max(reach, std::abs(value));
            reaches.push_back(reach);
            runaway += reach > RUNAWAY_VALUE;
            const std::vector<std::vector<double>> found = agent->brain->activations(agent->last_senses);
            for (std::size_t layer = 1; layer < found.size(); ++layer) {
                for (const double output : found[layer]) {
                    ++neurons;
                    jammed += std::abs(output) > JAMMED;
                }
            }
        }
        std::sort(reaches.begin(), reaches.end());
        const auto at = [&](double share) { return reaches.empty() ? 0.0 : reaches[static_cast<std::size_t>(share * static_cast<double>(reaches.size() - 1))]; };
        const double count = std::max<double>(1, static_cast<double>(thinking));
        result[std::string(species_name(species))] = {{"thinking", thinking}, {"valued", reaches.size()}, {"depth", depth / count},
                                                      {"width", width / count}, {"value_median", at(0.5)}, {"value_p90", at(0.9)},
                                                      {"value_max", at(1.0)}, {"runaway", runaway}, {"neurons", neurons}, {"jammed", jammed},
                                                      {"surprise_by_age", {surprise[0] / std::max(1.0, surprised[0]), surprise[1] / std::max(1.0, surprised[1]), surprise[2] / std::max(1.0, surprised[2])}}};
    }
    return {{"turn", world.turn()}, {"runaway_value", RUNAWAY_VALUE}, {"jammed_at", JAMMED}, {"minds", result}};
}

std::string Probe::brain_report_text(Simulation& world) const {
    world.sync_brains();  // every brain as it now is
    const auto alive = world.living();
    const std::vector<const Bitzkey*> animals(alive.begin(), alive.end());
    return brain_report(world.turn(), animals, world.insights());
}

std::string Probe::death_report_text(Simulation& world) const {
    std::string text;
    for (const std::string& line : death_report_lines(world.death_tally())) text += line + '\n';
    return text;
}

Json Probe::timings(Simulation& world) const {
    const Simulation::Timings& took = world.timings;
    Json actions = Json::object();
    for (int action = 0; action < ACTION_COUNT; ++action) {
        if (took.taken[static_cast<std::size_t>(action)]) {
            actions[std::string(ACTIONS[static_cast<std::size_t>(action)])] = {{"ms", took.actions[static_cast<std::size_t>(action)]},
                                                                                {"taken", took.taken[static_cast<std::size_t>(action)]}};
        }
    }
    return {{"turn", world.turn()},
            {"ms_total", {{"upkeep", took.upkeep}, {"sense", took.sense}, {"think", took.think}, {"learn", took.learn},
                          {"choose", took.choose}, {"act", took.act}, {"after", took.after}}},
            {"acting", actions}};
}

Json Probe::carrying(Simulation& world) const {
    int meat_piles = 0, fruit_piles = 0;
    long long meat = 0, fruit = 0, stone = 0, bone = 0, wood = 0;
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            const int here = world.meat_pile_at(x, y), there = world.fruit_pile_at(x, y);
            meat_piles += here > 0;
            fruit_piles += there > 0;
            meat += here;
            fruit += there;
            stone += world.pile_at(x, y, StonePile);
            bone += world.pile_at(x, y, BonePile);
            wood += world.pile_at(x, y, WoodPile);
        }
    }
    const auto count = [&](Simulation::Carrying what) { return world.carrying[what].load(); };
    long long carried = 0;
    for (const auto& agent : world.agents()) carried += agent->load();
    return {{"turn", world.turn()},
            {"taken", {{"meat_from_kill", count(Simulation::MeatFromKill)}, {"meat_from_pile", count(Simulation::MeatFromPile)},
                       {"fruit_from_plant", count(Simulation::FruitFromPlant)}, {"fruit_torn", count(Simulation::FruitTorn)},
                       {"fruit_from_pile", count(Simulation::FruitFromPile)}}},
            {"dropped", {{"meat", count(Simulation::MeatDropped)}, {"fruit", count(Simulation::FruitDropped)}}},
            {"eaten", {{"from_pile", count(Simulation::PileEaten)}, {"carried", count(Simulation::CarriedEaten)}}},
            {"spoiled", count(Simulation::Spoiled)},
            {"now", {{"meat_piles", meat_piles}, {"meat", meat}, {"fruit_piles", fruit_piles}, {"fruit", fruit}, {"carried", carried},
                     {"stone", stone}, {"bone", bone}, {"wood", wood}}}};
}

void Probe::install(httplib::Server& server, const std::string& prefix) {
    const auto looked = [this](const std::function<Json(Simulation&)>& look) {
        Json result;
        run_([&](Simulation& world) { result = look(world); });
        return result;
    };
    server.Get(prefix + "/carrying", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return carrying(world); }));
    }));
    server.Get(prefix + "/state", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return state(world); }));
    }));
    server.Get(prefix + "/animals", answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const auto number = [&](const char* name, int fallback) {
            return request.has_param(name) ? std::stoi(request.get_param_value(name)) : fallback;
        };
        const std::string species = request.get_param_value("species");
        const int x = number("x", 0), y = number("y", 0), radius = request.has_param("x") ? number("r", 5) : -1;
        const auto limit = static_cast<std::size_t>(std::max(0, number("limit", 200)));
        reply(response, looked([&](Simulation& world) { return animals(world, species, x, y, radius, limit); }));
    }));
    server.Get(prefix + R"(/animal/(\d+))", answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const int identifier = number_in(request.matches);
        reply(response, looked([&](Simulation& world) { return animal(world, identifier); }));
    }));
    server.Get(prefix + R"(/family/(\d+))", answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const int identifier = number_in(request.matches);
        reply(response, looked([&](Simulation& world) { return family(world, identifier); }));
    }));
    server.Get(prefix + "/herds", answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const std::string species = request.get_param_value("species");
        const bool members = request.get_param_value("members") == "1";
        const int reach = request.has_param("reach") ? std::stoi(request.get_param_value("reach")) : HERD_REACH;
        reply(response, looked([&](Simulation& world) { return herds(world, species, members, reach); }));
    }));
    server.Post(prefix + "/watch", answered([this](const httplib::Request& request, httplib::Response& response) {
        const Json body = Json::parse(request.body.empty() ? std::string("{}") : request.body);
        if (!body.is_object() || !body.contains("ids") || !body["ids"].is_array()) throw std::invalid_argument("expected {\"ids\": [...]}");
        watch(body["ids"].get<std::vector<int>>(), body.value("keep", std::size_t{0}));
        reply(response, tracks());
    }));
    server.Get(prefix + "/watch", answered([this](const httplib::Request&, httplib::Response& response) { reply(response, tracks()); }));
    // The rest of the world.
    const auto param = [](const httplib::Request& request, const char* name, int fallback) {
        return request.has_param(name) ? std::stoi(request.get_param_value(name)) : fallback;
    };
    const auto text = [](httplib::Response& response, const std::string& body) {
        response.set_content(body, "text/plain; charset=utf-8");
    };
    server.Get(prefix + "/rules", answered([this](const httplib::Request&, httplib::Response& response) {
        Json result;
        run_([&](Simulation&) { result = rules(); });  // the rules hold while the world turns
        reply(response, result);
    }));
    server.Get(prefix + "/settings", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return settings(world); }));
    }));
    // Changing the world is for those on this machine. The web server marks all it passes on, and an outsider can't unmark it.
    const auto local_only = [](auto handler) {
        return [handler](const httplib::Request& request, httplib::Response& response) {
            if (request.has_header("X-Forwarded-For")) {
                reply(response, {{"error", "only from the machine itself"}});
                response.status = 403;
                return;
            }
            handler(request, response);
        };
    };
    server.Post(prefix + "/rules", local_only(answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const Json body = Json::parse(request.body.empty() ? std::string("{}") : request.body);
        if (!body.is_object() || !body.contains("rules")) throw std::invalid_argument("expected {\"rules\": {KEY: value, ...}}");
        reply(response, looked([&](Simulation& world) { return change_rules(world, body["rules"]); }));
    })));
    server.Post(prefix + "/settings", local_only(answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const Json body = Json::parse(request.body.empty() ? std::string("{}") : request.body);
        reply(response, looked([&](Simulation& world) { return change_settings(world, body); }));
    })));
    server.Get(prefix + "/climate", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return climate(world); }));
    }));
    server.Get(prefix + "/grass", answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const bool map = request.get_param_value("map") == "1";
        reply(response, looked([&](Simulation& world) { return grass(world, map); }));
    }));
    server.Get(prefix + "/cell", answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const int x = param(request, "x", -1), y = param(request, "y", -1);
        reply(response, looked([&](Simulation& world) { return cell(world, x, y); }));
    }));
    server.Get(prefix + "/bodies", answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const auto limit = static_cast<std::size_t>(std::max(0, param(request, "limit", 500)));
        reply(response, looked([&](Simulation& world) { return bodies(world, limit); }));
    }));
    server.Get(prefix + "/seeds", answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const auto limit = static_cast<std::size_t>(std::max(0, param(request, "limit", 500)));
        reply(response, looked([&](Simulation& world) { return seeds(world, limit); }));
    }));
    server.Get(prefix + "/deaths", answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const int since = param(request, "since", 0);
        const std::string species = request.get_param_value("species"), cause = request.get_param_value("cause");
        const auto limit = static_cast<std::size_t>(std::max(0, param(request, "limit", 500)));
        reply(response, looked([&](Simulation& world) { return deaths(world, since, species, cause, limit); }));
    }));
    server.Get(prefix + "/history", answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const auto points = static_cast<std::size_t>(std::max(1, param(request, "points", 200)));
        reply(response, looked([&](Simulation& world) { return history(world, points); }));
    }));
    server.Get(prefix + "/insights", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return insights(world); }));
    }));
    server.Get(prefix + "/calls", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return calls(world); }));
    }));
    server.Get(prefix + R"(/brain/(\d+))", answered([=, this](const httplib::Request& request, httplib::Response& response) {
        const int identifier = number_in(request.matches);
        const bool weights = request.get_param_value("weights") == "1";
        reply(response, looked([&](Simulation& world) { return brain(world, identifier, weights); }));
    }));
    server.Get(prefix + "/diary", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return diary(world); }));
    }));
    server.Get(prefix + "/language", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return language(world); }));
    }));
    server.Get(prefix + "/towns", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return towns(world); }));
    }));
    server.Get(prefix + "/opinions", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return opinions(world); }));
    }));
    server.Get(prefix + "/gifts", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return gifts(world); }));
    }));
    server.Get(prefix + "/lands", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return lands(world); }));
    }));
    server.Get(prefix + "/society", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return society(world); }));
    }));
    server.Get(prefix + "/regard", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return regard(world); }));
    }));
    server.Get(prefix + "/skills", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return skills(world); }));
    }));
    server.Get(prefix + "/minds", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return minds(world); }));
    }));
    server.Get(prefix + "/reports/brain", answered([=, this](const httplib::Request&, httplib::Response& response) {
        std::string body;
        run_([&](Simulation& world) { body = brain_report_text(world); });
        text(response, body);
    }));
    server.Get(prefix + "/reports/deaths", answered([=, this](const httplib::Request&, httplib::Response& response) {
        std::string body;
        run_([&](Simulation& world) { body = death_report_text(world); });
        text(response, body);
    }));
    server.Get(prefix + "/timings", answered([=, this](const httplib::Request&, httplib::Response& response) {
        reply(response, looked([this](Simulation& world) { return timings(world); }));
    }));
    server.Delete(prefix + "/watch", answered([this](const httplib::Request& request, httplib::Response& response) {
        unwatch(numbers_in(request.get_param_value("ids")));
        reply(response, tracks());
    }));
}

}  // namespace bitzkey::net
