#include "daemon/Api.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <set>
#include <unordered_map>

#include <httplib.h>

#include "core/Catalog.hpp"
#include "core/Exchange.hpp"
#include "core/Insights.hpp"
#include "daemon/Graph.hpp"
#include "daemon/Views.hpp"
#include "daemon/World.hpp"

namespace bitzkey::daemon {

namespace {

constexpr std::size_t MAX_IMPORT_BYTES = 5'000'000;
constexpr const char* JSON_TYPE = "application/json; charset=utf-8";
constexpr const char* BINARY_TYPE = "application/octet-stream";

std::string format(const char* pattern, auto... values) {
    char buffer[512];
    std::snprintf(buffer, sizeof buffer, pattern, values...);
    return buffer;
}

// A genome as a browser's export list keeps it, to know a Bitzkey is still the one it listed.
std::string dna_key(Dna dna) { return format("%08X%016llX", dna.social, static_cast<unsigned long long>(dna.bits)); }

std::string plural(std::size_t count, const char* word = "Bitzkey") {
    return std::to_string(count) + " " + word + (count != 1 ? "s" : "");
}

std::string content_type(const std::string& path) {
    const auto dot = path.rfind('.');
    const std::string extension = dot == std::string::npos ? "" : path.substr(dot + 1);
    if (extension == "html") return "text/html; charset=utf-8";
    if (extension == "js") return "text/javascript; charset=utf-8";
    if (extension == "css") return "text/css; charset=utf-8";
    if (extension == "svg") return "image/svg+xml";
    if (extension == "png") return "image/png";
    if (extension == "ico") return "image/x-icon";
    if (extension == "json") return JSON_TYPE;
    return "application/octet-stream";
}

void reply(httplib::Response& response, const Json& value, int status = 200) {
    response.status = status;
    response.set_content(value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), JSON_TYPE);
}

void fail(httplib::Response& response, const std::string& message, int status = 400) { reply(response, {{"error", message}}, status); }

Json body(const httplib::Request& request) {
    if (request.body.empty()) return Json::object();
    Json value = Json::parse(request.body);  // throws on bad JSON, answered as a bad request
    if (!value.is_object()) throw std::invalid_argument("expected a JSON object");
    return value;
}

std::set<int> identifiers(const Json& list) {
    std::set<int> found;
    if (list.is_array()) {
        for (const auto& item : list) {
            if (item.is_number_integer()) found.insert(item.get<int>());
        }
    }
    return found;
}

GraphView graph_view(const Json& request, const Simulation& simulation) {
    GraphView view;
    const int frontier = simulation.frontier();
    view.first = std::max(0, std::min(frontier, request.value("first", 0)));
    view.last = std::max(view.first, std::min(frontier, request.value("last", frontier)));
    view.bars = std::max(1, std::min(2000, request.value("bars", 180)));
    view.height = std::max(1, std::min(2000, request.value("height", 400)));
    view.part = std::max(0, std::min(2, request.value("part", request.value("dot", false) ? 1 : 0)));
    view.sort = std::max(0, std::min(sort_count() - 1, request.value("sort", 0)));
    view.zoom = std::max(1.0, std::min(32.0, request.value("zoom", 1.0)));
    view.floor = std::max(0.0, std::min(1 - 1 / view.zoom, request.value("floor", 0.0)));
    return view;
}

nlohmann::ordered_json genome_only(Dna dna) { return genome_record(dna); }

std::unordered_map<int, const Bitzkey*> by_identifier(const Simulation& simulation) {
    std::unordered_map<int, const Bitzkey*> agents;
    for (const auto& agent : simulation.agents()) agents[agent->identifier] = agent.get();
    return agents;
}

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("can't write " + path.string());
    file << text;
    if (!file) throw std::runtime_error("can't write " + path.string());
}

// A file name the daemon wrote itself, so a request can't reach outside its folder.
bool safe_name(const std::string& name) {
    if (name.empty() || name.size() > 200 || name[0] == '.') return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
    });
}

// Records for an export list kept in a browser: the living as they are now, the dead as they were
// when added (their brain is gone). Returns the records and how many were the dead.
std::pair<std::vector<nlohmann::ordered_json>, int> listed_records(const Simulation& simulation, const Json& entries) {
    const auto agents = by_identifier(simulation);
    std::vector<nlohmann::ordered_json> records;
    int gone = 0;
    if (!entries.is_array()) return {records, gone};
    for (const auto& entry : entries) {
        const int identifier = entry.value("identifier", -1);
        const std::string dna = entry.value("dna", std::string());
        const auto found = agents.find(identifier);
        if (found != agents.end() && found->second->alive() && dna_key(found->second->dna) == dna) {
            records.push_back(bitzkey_record(*found->second));
        } else if (entry.contains("saved") && entry["saved"].is_object()) {
            records.push_back(nlohmann::ordered_json::parse(entry["saved"].dump()));
            ++gone;
        }
    }
    return {records, gone};
}

std::string add_to_catalog(const std::filesystem::path& path, const std::vector<catalog::Entry>& entries, const std::string& what,
                           const std::string& skipped = "") {
    const int added = catalog::add(entries, path);
    const int already = static_cast<int>(entries.size()) - added;
    std::string note = added ? "Added " + what + " to the catalog"
                             : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(what[0])))) + what.substr(1) +
                                   " already in the catalog";
    if (added && already) note += " (" + std::to_string(already) + " already there)";
    return note + "." + (skipped.empty() ? "" : " " + skipped);
}

std::uint64_t fresh_seed() {
    std::random_device device;
    return (static_cast<std::uint64_t>(device()) << 32) | device();
}

}  // namespace

void install_routes(httplib::Server& server, World& world, const std::filesystem::path& web_dir) {
    // Turn a handler's exceptions into a message for the page, rather than a dropped connection.
    const auto guarded = [](auto handler) {
        return [handler](const httplib::Request& request, httplib::Response& response) {
            try {
                handler(request, response);
            } catch (const nlohmann::json::exception& error) {
                fail(response, std::string("Bad request: ") + error.what());
            } catch (const std::invalid_argument& error) {
                fail(response, error.what());
            } catch (const std::exception& error) {
                fail(response, error.what(), 500);
            }
        };
    };
    auto graph = std::make_shared<Graph>();

    server.set_payload_max_length(4 * MAX_IMPORT_BYTES);

    // ---- The world as it is

    server.Get("/api/state", guarded([&world](const httplib::Request&, httplib::Response& response) {
        auto lock = world.lock();
        reply(response, state(world));
    }));

    server.Get("/api/frame", guarded([&world](const httplib::Request&, httplib::Response& response) {
        std::string data;
        {
            auto lock = world.lock();
            data = frame(world.simulation());
        }
        response.set_content(std::move(data), BINARY_TYPE);
    }));

    server.Get(R"(/api/agent/(\d+))", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const int identifier = std::stoi(request.matches[1]);
        auto lock = world.lock();
        Simulation& simulation = world.simulation();
        for (const auto& agent : simulation.agents()) {
            if (agent->identifier == identifier) {
                reply(response, inspect(*agent, true, simulation.temperature(agent->y)));
                return;
            }
        }
        if (identifier >= 0 && identifier < simulation.next_identifier()) {  // gone from the world: its genome alone
            const Bitzkey sample(identifier, simulation.lineage(identifier), 0, 0);
            reply(response, inspect(sample, false));
            return;
        }
        fail(response, "No such Bitzkey.", 404);
    }));

    server.Get("/api/exemplar_stats", guarded([](const httplib::Request&, httplib::Response& response) { reply(response, exemplar_stats()); }));

    server.Post("/api/exemplars", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        std::vector<int> choices(DEFAULT_EXEMPLARS.begin(), DEFAULT_EXEMPLARS.end());
        if (given.contains("choices") && given["choices"].is_array()) choices = given["choices"].get<std::vector<int>>();
        auto lock = world.lock();
        reply(response, exemplars(world.simulation(), choices));
    }));

    // Every death so far, by species and cause: a row for each cause, the most common first.
    server.Get("/api/deaths", guarded([&world](const httplib::Request&, httplib::Response& response) {
        auto lock = world.lock();
        const Simulation& simulation = world.simulation();
        const auto& by_species = simulation.death_causes();
        std::map<std::string, int> totals;
        for (const auto& causes : by_species) {
            for (const auto& [cause, count] : causes) totals[cause] += count;
        }
        std::vector<std::pair<std::string, int>> ranked(totals.begin(), totals.end());
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        Json rows = Json::array();
        for (const auto& [cause, total] : ranked) {
            Json counts = Json::array();
            for (const auto& causes : by_species) {
                const auto found = causes.find(cause);
                counts.push_back(found == causes.end() ? 0 : found->second);
            }
            rows.push_back({{"cause", cause}, {"counts", counts}, {"total", total}});
        }
        reply(response, {{"turn", simulation.turn()}, {"deaths", simulation.deaths()}, {"species", SPECIES}, {"rows", rows}});
    }));

    // One animal's brain probed with the standard situations: its chance of choosing each action open to it.
    server.Get(R"(/api/brain/(\d+))", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const int identifier = std::stoi(request.matches[1]);
        auto lock = world.lock();
        Simulation& simulation = world.simulation();
        simulation.sync_brains();  // the brains as they now are, when they think on the GPUs
        for (const Bitzkey* agent : simulation.living()) {
            if (agent->identifier != identifier || !agent->brain) continue;
            Json rows = Json::array();
            std::vector<bool> used(ACTION_COUNT, false);
            for (const BrainRow& row : brain_matrix(*agent)) {
                Json chances = Json::object();
                for (const auto& [action, chance] : row.chances) {
                    chances[std::string(ACTIONS[static_cast<std::size_t>(action)])] = chance;
                    used[static_cast<std::size_t>(action)] = true;
                }
                rows.push_back({{"situation", row.situation}, {"chances", chances}});
            }
            Json actions = Json::array();
            for (int action = 0; action < ACTION_COUNT; ++action) {
                if (used[static_cast<std::size_t>(action)]) {
                    actions.push_back({{"name", std::string(ACTIONS[static_cast<std::size_t>(action)])},
                                       {"short", std::string(short_action(action))}});
                }
            }
            reply(response, {{"identifier", identifier}, {"species", std::string(species_name(agent->species))}, {"actions", actions}, {"rows", rows}});
            return;
        }
        fail(response, "Only a living animal has a brain to inspect.", 404);
    }));

    server.Get("/api/insights", guarded([&world](const httplib::Request&, httplib::Response& response) {
        auto lock = world.lock();
        reply(response, insights(world.simulation()));
    }));

    // ---- Steering it

    server.Post("/api/control", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        const std::string action = given.value("action", std::string());
        auto lock = world.lock();
        Simulation& simulation = world.simulation();
        std::string note;
        if (action == "play") {
            if (simulation.ended()) note = end_note(simulation);
            else world.play(true);
        } else if (action == "pause") {
            world.play(false);
        } else if (action == "toggle") {
            if (simulation.ended()) note = end_note(simulation);
            else world.play(!world.playing());
        } else if (action == "step") {
            world.step();
        } else if (action == "rewind") {
            world.rewind();
        } else if (action == "speed") {
            world.set_speed(given.value("speed", world.speed()));
            note = std::string("Speed ") + SPEED_LABELS[static_cast<std::size_t>(world.speed())] + ".";
        } else if (action == "seek") {
            const int turn = given.value("turn", simulation.turn());
            world.seek(turn);
            note = turn < simulation.turn() ? format("The history reaches back only to turn %d; moved there.", simulation.turn())
                                            : format("Moved to turn %d.", simulation.turn());
        } else if (action == "restart") {
            world.restart();
            note = "A new world, from the same settings.";
        } else {
            throw std::invalid_argument("unknown action " + action);
        }
        Json result = state(world);
        result["note"] = note;
        reply(response, result);
    }));

    // ---- The colour graph

    server.Get("/api/graph/sorts", guarded([](const httplib::Request&, httplib::Response& response) {
        Json sorts = Json::array();
        for (int sort = 0; sort < sort_count(); ++sort) sorts.push_back(sort_label(sort));
        reply(response, {{"sorts", sorts}, {"mind_traits", DOT_TRAIT_COUNT}, {"social_traits", SOCIAL_TRAIT_COUNT},
                         {"body_traits", BODY_TRAIT_COUNT - 1}});
    }));

    server.Post("/api/graph", guarded([&world, graph](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        std::string data;
        {
            auto lock = world.lock();
            const Simulation& simulation = world.simulation();
            data = graph->image(simulation, graph_view(given, simulation), identifiers(given.value("selection", Json::array())));
        }
        response.set_content(std::move(data), BINARY_TYPE);
    }));

    server.Post("/api/graph/pick", guarded([&world, graph](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        auto lock = world.lock();
        const Simulation& simulation = world.simulation();
        const std::set<int> found = graph->living_in(simulation, graph_view(given, simulation), given.value("bar_from", 0),
                                                     given.value("bar_to", 0), given.value("top", 0.0), given.value("bottom", 0.0));
        reply(response, {{"identifiers", found}});
    }));

    server.Post("/api/graph/hover", guarded([&world, graph](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        auto lock = world.lock();
        Simulation& simulation = world.simulation();
        const auto hit = graph->at(simulation, graph_view(given, simulation), given.value("bar", 0), given.value("y", 0.0));
        if (!hit) {
            reply(response, {{"identifier", nullptr}});
            return;
        }
        const auto [identifier, alive] = *hit;
        const Dna dna = simulation.lineage(identifier);
        Json result = {{"identifier", identifier},
                       {"alive", alive},
                       {"ring", hex_word(body_word(dna))},
                       {"dot", hex_word(mind_word(dna))},
                       {"social", hex_word(social_word(dna))},
                       {"species", std::string(SPECIES[static_cast<std::size_t>(decode_body(dna)[SpeciesTrait])])}};
        if (const Simulation::DeathRecord* record = alive ? nullptr : simulation.death_record(identifier)) {  // how it died
            result["cause"] = record->cause;
            if (record->killer >= 0) {
                result["killer"] = record->killer;
                result["killer_species"] = std::string(species_name(record->killer_species));
            }
        }
        if (alive) {  // the dead show only their colour, and how they died
            for (const Bitzkey* agent : simulation.living()) {
                if (agent->identifier == identifier) result["details"] = inspect(*agent, true, simulation.temperature(agent->y));
            }
        }
        reply(response, result);
    }));

    // ---- Exporting: records, files, the catalog and the brain report

    // Records for Bitzkeys by identifier: the living with brain and temperament, the dead without a
    // brain, and the long dead their genome alone. With "living_only", the dead are left out.
    server.Post("/api/records", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        const std::set<int> wanted = identifiers(given.value("identifiers", Json::array()));
        const bool living_only = given.value("living_only", false);
        auto lock = world.lock();
        Simulation& simulation = world.simulation();
        simulation.sync_brains();  // the brains as they now are, when they think on the GPUs
        const auto agents = by_identifier(simulation);
        std::vector<nlohmann::ordered_json> records;
        Json listed = Json::array();
        int dead = 0;
        for (const int identifier : wanted) {
            const auto found = agents.find(identifier);
            if (found != agents.end() && (found->second->alive() || !living_only)) {
                records.push_back(bitzkey_record(*found->second));
                dead += !found->second->alive();
                listed.push_back({{"identifier", identifier},
                                  {"dna", dna_key(found->second->dna)},
                                  {"saved", Json::parse(dumps(records.back()))}});
            } else if (!living_only && identifier >= 0 && identifier < simulation.next_identifier()) {
                records.push_back(genome_only(simulation.lineage(identifier)));
                ++dead;
            }
        }
        reply(response, {{"text", records.empty() ? std::string() : format_records(records)},
                         {"count", records.size()},
                         {"dead", dead},
                         {"entries", listed}});
    }));

    // Everyone alive, or the Bitzkeys picked on the graph, to the clipboard or a file in exports/.
    server.Post("/api/export", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        const bool to_file = given.value("save", false);
        auto lock = world.lock();
        Simulation& simulation = world.simulation();
        simulation.sync_brains();
        std::vector<nlohmann::ordered_json> records;
        std::string what, kind;
        if (given.contains("list")) {  // a browser's export list
            const auto [listed, gone] = listed_records(simulation, given["list"]);
            records = listed;
            what = plural(records.size()) + " from the export list";
            if (gone) what += format(" (%d as they were when added)", gone);
            kind = "chosen";
        } else {
            const std::set<int> chosen = identifiers(given.value("identifiers", Json::array()));
            const auto agents = by_identifier(simulation);
            int dead = 0;
            if (!chosen.empty()) {
                for (const int identifier : chosen) {
                    const auto found = agents.find(identifier);
                    if (found != agents.end()) {
                        records.push_back(bitzkey_record(*found->second));
                        dead += !found->second->alive();
                    } else if (identifier >= 0 && identifier < simulation.next_identifier()) {
                        records.push_back(genome_only(simulation.lineage(identifier)));
                        ++dead;
                    }
                }
            } else {
                for (const Bitzkey* agent : simulation.living()) records.push_back(bitzkey_record(*agent));
            }
            what = std::to_string(records.size()) + " " + (chosen.empty() ? "" : "selected ") + "Bitzkey" + (records.size() != 1 ? "s" : "");
            if (dead) what += format(" (%d dead, so without a brain)", dead);
            kind = chosen.empty() ? "alive" : "selected";
        }
        if (records.empty()) {
            reply(response, {{"text", ""}, {"note", "Nobody is alive to export."}});
            return;
        }
        const std::string text = format_records(records);
        Json result = {{"text", text}, {"what", what}};
        const std::string name = format("bitzkeys-turn-%05d-%s-%zu.json", simulation.turn(), kind.c_str(), records.size());
        result["name"] = name;
        if (to_file) {
            write_file(world.paths().exports / name, text + "\n");
            result["note"] = "Saved " + what + " to exports/" + name;
        } else {
            result["note"] = "Copied " + what + " to the clipboard.";
        }
        reply(response, result);
    }));

    server.Get("/api/exports", guarded([&world](const httplib::Request&, httplib::Response& response) {
        Json files = Json::array();
        const auto folder = world.paths().exports;
        if (std::filesystem::is_directory(folder)) {
            std::vector<std::pair<std::filesystem::file_time_type, std::string>> found;
            for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                if (entry.is_regular_file()) found.emplace_back(entry.last_write_time(), entry.path().filename().string());
            }
            std::sort(found.rbegin(), found.rend());  // the newest first
            for (const auto& [time, name] : found) files.push_back({{"name", name}, {"size", std::filesystem::file_size(folder / name)}});
        }
        reply(response, {{"files", files}});
    }));

    server.Get(R"(/api/(exports|reports)/([^/]+))", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const std::string folder = request.matches[1], name = request.matches[2];
        if (!safe_name(name)) {
            fail(response, "No such file.", 404);
            return;
        }
        const auto path = (folder == "exports" ? world.paths().exports : world.paths().reports) / name;
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            fail(response, "No such file.", 404);
            return;
        }
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        response.set_header("Content-Disposition", "attachment; filename=\"" + name + "\"");
        response.set_content(std::move(text), name.ends_with(".json") ? "application/json" : "text/plain; charset=utf-8");
    }));

    server.Post("/api/report", guarded([&world](const httplib::Request&, httplib::Response& response) {
        auto lock = world.lock();
        Simulation& simulation = world.simulation();
        simulation.sync_brains();
        const auto alive = simulation.living();
        const std::vector<const Bitzkey*> animals(alive.begin(), alive.end());
        const std::string name = format("brain-report-turn-%05d.txt", simulation.turn());
        write_file(world.paths().reports / name, brain_report(simulation.turn(), animals, simulation.insights()));
        reply(response, {{"name", name}, {"note", "Brain report saved to reports/" + name}});
    }));

    // ---- The catalog

    server.Get("/api/catalog", guarded([&world](const httplib::Request&, httplib::Response& response) {
        Json list = Json::array();
        std::string note;
        try {
            for (const auto& entry : catalog::load(world.paths().catalog)) {
                list.push_back({{"line", catalog::describe(entry)}, {"details", inspect(catalog::inspected(entry))}});
            }
        } catch (const std::exception& error) {
            note = error.what();
        }
        reply(response, {{"entries", list}, {"note", note}});
    }));

    server.Post("/api/catalog/add", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        auto lock = world.lock();
        Simulation& simulation = world.simulation();
        simulation.sync_brains();
        std::vector<catalog::Entry> entries;
        std::string what, skipped;
        if (given.contains("list")) {  // a browser's export list: the dead as they were when added
            const auto agents = by_identifier(simulation);
            for (const auto& entry : given["list"]) {
                const int identifier = entry.value("identifier", -1);
                const auto found = agents.find(identifier);
                if (found != agents.end() && found->second->alive() &&
                    dna_key(found->second->dna) == entry.value("dna", std::string())) {
                    entries.push_back(catalog::entry_for(*found->second, simulation.turn()));
                } else if (entry.contains("saved") && entry["saved"].is_object()) {
                    catalog::Entry saved = catalog::Entry::parse(entry["saved"].dump());
                    saved["catalog"] = {{"identifier", identifier}};
                    entries.push_back(std::move(saved));
                }
            }
            what = plural(entries.size()) + " from the export list";
        } else {
            const std::set<int> wanted = identifiers(given.value("identifiers", Json::array()));
            std::unordered_map<int, const Bitzkey*> living;
            for (const Bitzkey* agent : simulation.living()) living[agent->identifier] = agent;
            for (const int identifier : wanted) {
                const auto found = living.find(identifier);
                if (found != living.end()) entries.push_back(catalog::entry_for(*found->second, simulation.turn()));
            }
            const int dead = static_cast<int>(wanted.size() - entries.size());
            skipped = dead ? std::to_string(dead) + " dead left out: their brains are gone." : "";
            if (entries.size() == 1 && wanted.size() == 1) what = format("#%03d", *wanted.begin());
            else what = plural(entries.size());
        }
        if (entries.empty()) {
            reply(response, {{"note", given.contains("list") ? std::string("Nothing on the export list to catalog.")
                                                            : "Only living Bitzkeys can be catalogued, with their brains" +
                                                                  (skipped.empty() ? std::string(".") : "; " + skipped)}});
            return;
        }
        reply(response, {{"note", add_to_catalog(world.paths().catalog, entries, what, skipped)}});
    }));

    server.Post("/api/catalog/remove", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        std::vector<int> indexes = given.value("indexes", std::vector<int>());
        std::sort(indexes.rbegin(), indexes.rend());  // the last first, so the rest keep their places
        indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
        auto lock = world.lock();  // one change to the file at a time
        const std::size_t count = catalog::load(world.paths().catalog).size();
        for (const int index : indexes) {
            if (index >= 0 && static_cast<std::size_t>(index) < count) catalog::remove(static_cast<std::size_t>(index), world.paths().catalog);
        }
        reply(response, {{"note", indexes.size() > 1 ? "Removed " + std::to_string(indexes.size()) + " from the catalog." : "Removed from the catalog."}});
    }));

    server.Post("/api/catalog/records", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        std::vector<int> indexes = given.value("indexes", std::vector<int>());
        std::sort(indexes.begin(), indexes.end());
        const auto entries = catalog::load(world.paths().catalog);
        std::vector<catalog::Entry> chosen;
        for (const int index : indexes) {
            if (index >= 0 && static_cast<std::size_t>(index) < entries.size()) chosen.push_back(entries[static_cast<std::size_t>(index)]);
        }
        reply(response, {{"text", chosen.empty() ? std::string() : format_records(chosen)}, {"count", chosen.size()}});
    }));

    // Catalogued Bitzkeys deployed onto the running world: DEPLOY_GROUP of each around a cell.
    server.Post("/api/deploy", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        std::vector<int> indexes = given.value("indexes", std::vector<int>());
        std::sort(indexes.begin(), indexes.end());
        const auto entries = catalog::load(world.paths().catalog);
        std::vector<Imported> bitzkeys;
        for (const int index : indexes) {
            if (index >= 0 && static_cast<std::size_t>(index) < entries.size()) {
                bitzkeys.push_back(catalog::imported(entries[static_cast<std::size_t>(index)]));
            }
        }
        if (bitzkeys.empty()) throw std::invalid_argument("Nothing chosen from the catalog to deploy.");
        auto lock = world.lock();
        Simulation& simulation = world.simulation();
        const int x = given.value("x", -1), y = given.value("y", -1);
        if (x < 0 || x >= simulation.width() || y < 0 || y >= simulation.height()) throw std::invalid_argument("Deploy on the map.");
        const auto placed = simulation.deploy(bitzkeys, x, y);
        world.touch();
        reply(response, {{"count", placed.size()}, {"note", format("Deployed %zu at (%d, %d).  RIGHT-CLICK or ESC  stop", placed.size(), x, y)}});
    }));

    // ---- A new world: the setup panel, the advanced rules and the designer

    server.Get("/api/setup", guarded([&world](const httplib::Request&, httplib::Response& response) {
        auto lock = world.lock();
        Json result = settings_form(world.settings());
        result["rules"] = rules_form(world.rules())["rules"];
        result["changed"] = world.rules();
        reply(response, result);
    }));

    // The running world's advanced rules, changed on the map: it pauses and plays by them from its next turn.
    // Where it thinks stays as it started.
    server.Post("/api/rules/world", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        rules::Values values = rules::defaults();
        const Json asked = given.value("rules", Json::object());
        for (const auto& [key, value] : asked.items()) {
            if (const rules::Rule* rule = rules::find(key); rule && value.is_number()) values[key] = rule->clamp(value.get<double>());
        }
        auto lock = world.lock();
        values["USE_GPU"] = rules::current().at("USE_GPU");
        if (const auto problems = rules::problems(values); !problems.empty()) {
            reply(response, {{"problems", problems}});
            return;
        }
        const rules::Values changed = rules::changed(values);
        world.change_rules(changed);
        reply(response, {{"problems", Json::array()},
                         {"changed", changed},
                         {"note", format("This world now plays by %zu rule%s changed from the defaults. Paused: SPACE plays on.",
                                         changed.size(), changed.size() != 1 ? "s" : "")}});
    }));

    server.Post("/api/import/check", guarded([](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        const std::string text = given.value("text", std::string());
        if (text.size() > MAX_IMPORT_BYTES) {
            reply(response, {{"ok", false}, {"count", 0}, {"summary", "Too large to import."}});
            return;
        }
        reply(response, import_check(text));
    }));

    server.Post("/api/rules/check", guarded([](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        rules::Values values = rules::defaults();
        const Json asked = given.value("rules", Json::object());  // kept, for the loop to read
        for (const auto& [key, value] : asked.items()) {
            if (const rules::Rule* rule = rules::find(key); rule && value.is_number()) values[key] = rule->clamp(value.get<double>());
        }
        Json shown = Json::object();
        for (const rules::Rule& rule : rules::all()) shown[rule.key] = rules::shown(rule, values.at(rule.key));
        reply(response, {{"problems", rules::problems(values)}, {"changed", rules::changed(values)}, {"values", values}, {"shown", shown}});
    }));

    server.Post("/api/design", guarded([](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        Dna dna = 0;
        if (given.value("random", false)) {
            PyRandom rng(fresh_seed());
            dna = Dna{rng.getrandbits(64), static_cast<std::uint32_t>(rng.getrandbits(32))};
            if (given.value("animal", false)) {
                dna.bits = (dna.bits & ~(std::uint64_t{0x3} << 32)) | (static_cast<std::uint64_t>(rng.randrange(1, 4)) << 32);
            }
        } else {
            const auto word = [&](const char* key) {
                const std::string text = given.value(key, std::string("0"));
                if (text.empty() || text.size() > 8 || !std::all_of(text.begin(), text.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
                    throw std::invalid_argument(std::string(key) + " must be up to 8 hex digits");
                }
                return static_cast<std::uint32_t>(std::stoul(text, nullptr, 16));
            };
            dna = Dna{(static_cast<std::uint64_t>(word("ring")) << 32) | word("dot"), word("social")};
        }
        Json result = design(dna);
        result["record_text"] = dumps(nlohmann::ordered_json::parse(result["record"].dump()));
        reply(response, result);
    }));

    server.Post("/api/new_world", guarded([&world](const httplib::Request& request, httplib::Response& response) {
        const Json given = body(request);
        Simulation::Settings settings = read_settings(given.value("settings", Json::object()));
        rules::Values changed;
        rules::Values values = rules::defaults();
        const Json asked = given.value("rules", Json::object());  // kept, for the loop to read
        for (const auto& [key, value] : asked.items()) {
            if (const rules::Rule* rule = rules::find(key); rule && value.is_number()) values[key] = rule->clamp(value.get<double>());
        }
        if (const auto problems = rules::problems(values); !problems.empty()) throw std::invalid_argument(problems.front() + ".");
        changed = rules::changed(values);
        auto lock = world.lock();
        world.new_world(std::move(settings), std::move(changed));
        Json result = state(world);
        result["note"] = "A new world has begun.";
        reply(response, result);
    }));

    // ---- The interface's files

    server.Get(R"(/(.*))", [web_dir](const httplib::Request& request, httplib::Response& response) {
        std::string path = request.matches[1];
        if (path.empty()) path = "index.html";
        response.set_header("Cache-Control", "no-cache");
        if (!web_dir.empty() && path.find("..") == std::string::npos) {  // from the files themselves, for working on them
            std::ifstream file(web_dir / path, std::ios::binary);
            if (file) {
                response.set_content(std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()), content_type(path));
                return;
            }
        }
        const auto& assets = web_assets();
        const auto found = assets.find(path);
        if (found == assets.end()) {
            response.status = 404;
            response.set_content("Not found", "text/plain");
            return;
        }
        response.set_content(std::string(found->second), content_type(path));
    });
}

}  // namespace bitzkey::daemon
