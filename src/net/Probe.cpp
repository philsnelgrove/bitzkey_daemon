#include "net/Probe.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>
#include <optional>
#include <regex>
#include <stdexcept>
#include <tuple>

#include <httplib.h>

namespace bitzkey::net {

namespace {

using Json = Probe::Json;

constexpr std::size_t MOST_KEPT = 100000;  // turns of a watched track kept at most
constexpr int FAMILY_DEPTH = 4;            // generations up and down a family is followed

std::optional<Species> species_named(const std::string& name) {
    for (std::size_t index = 0; index < SPECIES.size(); ++index) {
        if (SPECIES[index] == name) return static_cast<Species>(index);
    }
    return std::nullopt;
}

Json brief(const Bitzkey& agent) {
    return {{"id", agent.identifier},
            {"species", std::string(species_name(agent.species))},
            {"x", agent.x},
            {"y", agent.y},
            {"age", agent.age},
            {"health", agent.max_health > 0 ? std::max(0.0, agent.health) / agent.max_health : 0.0},
            {"hunger", agent.hunger},
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
    for (const Bitzkey* agent : world.living()) counts[static_cast<std::size_t>(agent->species)] += 1;
    Json alive = Json::object();
    for (std::size_t index = 0; index < SPECIES.size(); ++index) alive[std::string(SPECIES[index])] = counts[index];
    return {{"turn", world.turn()},
            {"year", year(world.turn())},
            {"season", std::string(SEASONS[static_cast<std::size_t>(season(world.turn()))])},
            {"year_length", YEAR_LENGTH},
            {"width", world.width()},
            {"height", world.height()},
            {"alive", alive}};
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
    result["mother"] = agent->mother;
    result["father"] = agent->father;
    result["children"] = agent->children;
    result["home"] = std::string(TEMPERATURES[static_cast<std::size_t>(agent->climate.temperature)]);
    result["heat_tolerant"] = agent->climate.heat;
    result["cold_tolerant"] = agent->climate.cold;
    if (agent->y >= 0 && agent->y < world.height()) {
        const double here = world.temperature(agent->y);
        result["temperature_here"] = here;
        result["steps_from_home"] = agent->climate_steps(here);
    }
    if (agent->species == Species::Plant) return result;
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
    for (std::size_t a = 0; a < now.size(); ++a) {
        Herd& herd = now[a];
        Json entry;
        if (heir[a]) {
            const Herd& before = last[*heir[a]];
            herd.id = before.id;
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
        if (!split_from.empty()) entry["split_from"] = split_from;
        if (!merged_from.empty()) entry["merged_from"] = merged_from;
        if (members) entry["members"] = ids;
        listed.push_back(std::move(entry));
    }
    std::sort(listed.begin(), listed.end(), [](const Json& p, const Json& q) { return p["size"].get<std::size_t>() > q["size"].get<std::size_t>(); });
    last = std::move(now);
    return {{"turn", world.turn()}, {"species", std::string(species_name(*kind))}, {"herds", listed}, {"alone", alone}};
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

void Probe::install(httplib::Server& server, const std::string& prefix) {
    const auto looked = [this](const std::function<Json(Simulation&)>& look) {
        Json result;
        run_([&](Simulation& world) { result = look(world); });
        return result;
    };
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
    server.Delete(prefix + "/watch", answered([this](const httplib::Request& request, httplib::Response& response) {
        unwatch(numbers_in(request.get_param_value("ids")));
        reply(response, tracks());
    }));
}

}  // namespace bitzkey::net
