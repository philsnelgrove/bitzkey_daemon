// The daemon: its settings, its world service, and its web API, served on a spare local port.

#include <doctest/doctest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "core/Rules.hpp"
#include "core/WorldFile.hpp"
#include "daemon/Api.hpp"
#include "daemon/Config.hpp"
#include "daemon/Graph.hpp"
#include "daemon/Views.hpp"
#include "daemon/World.hpp"

using namespace bitzkey;
using namespace bitzkey::daemon;

namespace {

std::filesystem::path scratch(const std::string& name) {
    std::random_device device;
    const auto path = std::filesystem::temp_directory_path() / ("bitzkey-daemon-test-" + name + "-" + std::to_string(device()));
    std::filesystem::create_directories(path);
    return path;
}

Simulation::Settings small_world() {
    Simulation::Settings settings;
    settings.width = 30;
    settings.height = 30;
    settings.population = 40;
    settings.seed = 7;
    return settings;
}

// A server on a spare port, for the length of a test.
struct Served {
    explicit Served(World& world) {
        install_routes(server, world);
        port = server.bind_to_any_port("127.0.0.1");
        thread = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    ~Served() {
        server.stop();
        thread.join();
    }
    httplib::Client client() const {
        httplib::Client made("127.0.0.1", port);
        made.set_read_timeout(30);
        return made;
    }
    httplib::Server server;
    int port = 0;
    std::thread thread;
};

Json post(httplib::Client& client, const std::string& path, const Json& body) {
    const auto result = client.Post(path, body.dump(), "application/json");
    REQUIRE(result);
    INFO(result->body);
    REQUIRE(result->status == 200);
    return Json::parse(result->body);
}

Json get(httplib::Client& client, const std::string& path) {
    const auto result = client.Get(path);
    REQUIRE(result);
    INFO(result->body);
    REQUIRE(result->status == 200);
    return Json::parse(result->body);
}

template <typename T>
T read(const std::string& data, std::size_t offset) {
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

}  // namespace

TEST_CASE("the daemon's settings come from a file, then the command line") {
    const auto folder = scratch("config");
    const auto path = folder / "bitzkey_daemon.conf";
    std::ofstream(path) << "# a comment\nport = 50000\nhost = 127.0.0.1   # just here\ngpu = no\nstart_paused = yes\n";
    Config config;
    std::string message;
    CHECK(parse_arguments({"--config", path.string(), "--port", "50001"}, config, message));
    CHECK(config.port == 50001);
    CHECK(config.host == "127.0.0.1");
    CHECK(config.gpu == false);
    CHECK(config.start_paused);
    CHECK_FALSE(parse_arguments({"--port", "70000"}, config, message));
    CHECK(message.find("port") != std::string::npos);
    CHECK_FALSE(parse_arguments({"--nonsense"}, config, message));
    CHECK_FALSE(parse_arguments({"--help"}, config, message));
    CHECK(message.find("--data-dir") != std::string::npos);
    std::ofstream(path) << "colour = blue\n";
    CHECK_THROWS_AS(read_config(path, config), std::runtime_error);
    Config defaults;
    CHECK(defaults.port == 49152);
    CHECK(defaults.host == "0.0.0.0");
    std::filesystem::remove_all(folder);
}

TEST_CASE("the world turns on its own while playing, and waits while paused") {
    const auto folder = scratch("world");
    {
        World world(small_world(), false, folder);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        {
            auto lock = world.lock();
            CHECK(world.simulation().turn() == 0);
            CHECK(world.step());
            CHECK(world.simulation().turn() == 1);
            world.set_speed(4);  // as fast as it goes
            world.play(true);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        auto lock = world.lock();
        CHECK(world.simulation().turn() > 1);
        world.play(false);
    }
    std::filesystem::remove_all(folder);
}

TEST_CASE("at full speed, viewers are let in between turns rather than kept waiting") {
    const auto folder = scratch("fair");
    {
        World world(small_world(), false, folder);
        {
            auto lock = world.lock();
            world.set_speed(4);  // as fast as it goes
            world.play(true);
        }
        using Clock = std::chrono::steady_clock;
        auto longest = Clock::duration::zero();
        for (int look = 0; look < 40; ++look) {
            const auto asked = Clock::now();
            auto lock = world.lock();
            longest = std::max(longest, Clock::now() - asked);
        }
        CHECK(longest < std::chrono::milliseconds(500));  // a slice of turns at most, never indefinitely
        auto lock = world.lock();
        world.play(false);
    }
    std::filesystem::remove_all(folder);
}

TEST_CASE("the map frame holds every Bitzkey and the grass") {
    const auto folder = scratch("frame");
    {
        World world(small_world(), false, folder);
        auto lock = world.lock();
        world.step();
        const Simulation& simulation = world.simulation();
        const std::string data = frame(simulation);
        CHECK(data.substr(0, 4) == "BZF2");
        CHECK(read<std::uint32_t>(data, 4) == 30);
        CHECK(read<std::uint32_t>(data, 8) == 30);
        CHECK(read<std::uint32_t>(data, 12) == 1);
        const auto agents = read<std::uint32_t>(data, 16), trails = read<std::uint32_t>(data, 20);
        CHECK(agents == simulation.agents().size());
        CHECK(data.size() == 24 + 4 * 30 + 30 * 30 + agents * 24 + trails * 8);
        const std::size_t first = 24 + 4 * 30 + 30 * 30;
        CHECK(read<std::uint32_t>(data, first) == static_cast<std::uint32_t>(simulation.agents()[0]->identifier));
        CHECK(read<std::uint32_t>(data, first + 20) == social_word(simulation.agents()[0]->dna));  // its social background, last
    }
    std::filesystem::remove_all(folder);
}

TEST_CASE("the colour graph is drawn bar by bar, and finds who lies under a point") {
    BIOME_GROUPS = 0;  // a world of its population alone, few enough for the zoomed chart to leave some out
    const auto folder = scratch("graph");
    {
        World world(small_world(), false, folder);
        auto lock = world.lock();
        for (int turn = 0; turn < 20; ++turn) world.step();
        Simulation& simulation = world.simulation();
        Graph graph;
        GraphView view{0, simulation.turn(), 10, 100, false, 0};
        const std::string data = graph.image(simulation, view, {});
        CHECK(data.substr(0, 4) == "BZG1");
        const auto bars = read<std::uint32_t>(data, 4);
        CHECK(bars == 10);
        CHECK(read<std::uint32_t>(data, 8) == 100);
        const std::size_t pixels = 24 + bars * 5;
        const std::size_t bands_at = pixels + bars * 100 * 3;
        CHECK(read<std::uint32_t>(data, bands_at) >= 1);
        const auto bottom = graph.at(simulation, view, 9, 99.5);  // the last bar's lowest slice is the present's
        REQUIRE(bottom);
        CHECK(bottom->second);
        const auto all = graph.living_in(simulation, view, 9, 9, 0, 100);
        CHECK(all.size() == simulation.living().size());

        GraphView zoomed = view;  // zoomed fourfold into the height, from the bottom: fewer fit the chart
        zoomed.zoom = 4.0;
        CHECK(graph.at(simulation, zoomed, 9, 99.5) == bottom);
        const auto some = graph.living_in(simulation, zoomed, 9, 9, 0, 100);
        const double peak = read<float>(data, 12);  // the population the whole chart's height holds
        CHECK(some.size() < all.size());
        CHECK(static_cast<double>(some.size()) <= peak / 4 + 1);
        zoomed.floor = 0.75;  // and raised to the top quarter: the lowest slice lies below the chart
        CHECK(graph.at(simulation, zoomed, 9, 99.5) != bottom);
    }
    std::filesystem::remove_all(folder);
    BIOME_GROUPS = 2;
}

TEST_CASE("the web API shows the world and steers it") {
    const auto folder = scratch("api");
    {
        World world(small_world(), false, folder);
        Served served(world);
        auto client = served.client();

        Json state = get(client, "/api/state");
        CHECK(state["turn"] == 0);
        CHECK(state["width"] == 30);
        CHECK_FALSE(state["playing"].get<bool>());
        state = post(client, "/api/control", {{"action", "step"}});
        CHECK(state["turn"] == 1);
        state = post(client, "/api/control", {{"action", "speed"}, {"speed", 2}});
        CHECK(state["speed"] == 2);
        CHECK(state["note"] == "Speed 4x.");

        const auto frame = client.Get("/api/frame");
        REQUIRE(frame);
        CHECK(frame->body.substr(0, 4) == "BZF2");

        int identifier = 0;
        {
            auto lock = world.lock();
            const auto living = world.simulation().living();  // an animal: the plants, spawned first, have no brain
            identifier = (*std::find_if(living.begin(), living.end(), [](const Bitzkey* one) { return one->species != Species::Plant; }))->identifier;
        }
        const Json agent = get(client, "/api/agent/" + std::to_string(identifier));
        CHECK(agent["identifier"] == identifier);
        CHECK(agent["known"].get<bool>());
        CHECK(agent["inborn"].size() == TEMPERAMENT_COUNT);
        CHECK(agent["social"].get<std::string>().size() == 8);
        CHECK(client.Get("/api/agent/999999")->status == 404);

        const Json deaths = get(client, "/api/deaths");
        CHECK(deaths["species"].size() == 4);
        CHECK(deaths["total"] == deaths["deaths"]);  // every death back to the start
        CHECK((deaths["all"].size() == 4 && deaths["causes"].is_array() && deaths["whom"].is_array() &&
               deaths["activities"].is_array() && deaths["deadliest"].is_array()));
        const Json brain = get(client, "/api/brain/" + std::to_string(identifier));
        CHECK_FALSE(brain["rows"].empty());
        CHECK(client.Get("/api/brain/999999")->status == 404);

        const Json cards = post(client, "/api/exemplars", Json::object());
        CHECK(cards.size() == 10);
        CHECK(cards[0]["title"] == "SMARTEST");
        std::vector<std::string> titles;
        for (const auto& card : cards) titles.push_back(card["title"].get<std::string>());
        CHECK(titles == std::vector<std::string>{"SMARTEST", "LARGEST", "OLDEST", "MOST CHILDREN", "LATEST GENERATION", "STRONGEST",
                                                 "FASTEST", "BEST ARMOURED", "MOST FERTILE", "BEST FED"});  // the defaults
        CHECK(get(client, "/api/insights")["behaviour"].size() == ACTION_COUNT);

        const Json records = post(client, "/api/records", {{"identifiers", {identifier}}});
        CHECK(records["count"] == 1);
        CHECK(records["text"].get<std::string>().find("\"brain\"") != std::string::npos);

        const Json saved = post(client, "/api/export", {{"save", true}});
        CHECK(std::filesystem::exists(folder / "exports" / saved["name"].get<std::string>()));
        const auto download = client.Get("/api/exports/" + saved["name"].get<std::string>());
        REQUIRE(download);
        CHECK(download->status == 200);
        CHECK(client.Get("/api/exports/..%2Fcatalog.json")->status == 404);

        CHECK(post(client, "/api/catalog/add", {{"identifiers", {identifier}}})["note"].get<std::string>().find("Added") == 0);
        const Json catalog = get(client, "/api/catalog");
        CHECK(catalog["entries"].size() == 1);
        CHECK(post(client, "/api/catalog/records", {{"indexes", {0}}})["count"] == 1);
        const std::size_t before = [&] { auto lock = world.lock(); return world.simulation().agents().size(); }();
        const Json deployed = post(client, "/api/deploy", {{"indexes", {0}}, {"x", 10}, {"y", 10}});  // from the catalog, added above
        CHECK(deployed["count"] == DEPLOY_GROUP);
        CHECK([&] { auto lock = world.lock(); return world.simulation().agents().size(); }() == before + DEPLOY_GROUP);
        CHECK(client.Post("/api/deploy", Json{{"indexes", {0}}, {"x", -1}, {"y", 3}}.dump(), "application/json")->status == 400);
        post(client, "/api/catalog/remove", {{"indexes", {0}}});
        CHECK(get(client, "/api/catalog")["entries"].empty());
        // A design from the designer joins the catalog as a genome alone.
        CHECK(post(client, "/api/catalog/design", {{"ring", "00000021"}, {"dot", "0"}, {"social", "0"}})["note"] == "Designed and added to the catalog.");
        const Json with_design = get(client, "/api/catalog");
        REQUIRE(with_design["entries"].size() == 1);
        CHECK(with_design["entries"][0]["line"].get<std::string>().rfind("designed ", 0) == 0);
        CHECK(client.Post("/api/catalog/design", Json{{"ring", "nothex"}}.dump(), "application/json")->status == 400);
        post(client, "/api/catalog/remove", {{"indexes", {0}}});

        const Json report = post(client, "/api/report", Json::object());
        CHECK(std::filesystem::exists(folder / "reports" / report["name"].get<std::string>()));

        const Json setup = get(client, "/api/setup");
        CHECK(setup["settings"].size() == 10);  // the sea among them
        CHECK(setup["rules"].size() == rules::all().size());
        CHECK(post(client, "/api/import/check", {{"text", records["text"]}})["count"] == 1);
        CHECK_FALSE(post(client, "/api/import/check", {{"text", "nonsense"}})["ok"].get<bool>());

        const Json changed = post(client, "/api/rules/world", {{"rules", {{"CORPSE_TURNS", 7}, {"GRASS_REGROWTH", 0.05}}}});
        CHECK(changed["problems"].empty());
        CHECK(changed["changed"].size() == 2);
        CHECK(CORPSE_TURNS == 7);
        {
            auto lock = world.lock();
            CHECK_FALSE(world.playing());  // it pauses for the change
            CHECK(world.rules().size() == 2);  // and its restarts keep the rules
        }
        CHECK_FALSE(post(client, "/api/rules/world", {{"rules", {{"LITTER_RANGE.0", 9}, {"LITTER_RANGE.1", 2}}}})["problems"].empty());
        post(client, "/api/rules/world", {{"rules", Json::object()}});
        CHECK(CORPSE_TURNS == rules::defaults().at("CORPSE_TURNS"));

        const Json designed = post(client, "/api/design", {{"ring", "00000001"}, {"dot", "00000000"}, {"social", "0000ABCD"}});
        CHECK(designed["species"] == "grazer");
        CHECK(designed["record"]["social"] == "0000ABCD");
        CHECK(designed["facts"].size() >= 6);

        Json settings = Json::object();
        for (const auto& setting : setup["settings"]) settings[setting["key"].get<std::string>()] = setting["value"];
        settings["width"] = 40;
        settings["import"] = records["text"];
        const Json fresh = post(client, "/api/new_world", {{"settings", settings}, {"rules", Json::object()}});
        CHECK(fresh["width"] == 40);
        CHECK(fresh["world"] == 2);
        settings["import"] = "nonsense";
        CHECK(client.Post("/api/new_world", Json{{"settings", settings}}.dump(), "application/json")->status == 400);
        CHECK(get(client, "/api/state")["width"] == 40);  // the world it had stays

        post(client, "/api/control", {{"action", "pause"}});
        for (int turn = 0; turn < 5; ++turn) post(client, "/api/control", {{"action", "step"}});
        const auto graph = client.Post("/api/graph", Json{{"bars", 6}, {"height", 50}}.dump(), "application/json");
        REQUIRE(graph);
        CHECK(graph->body.substr(0, 4) == "BZG1");
        const Json picked = post(client, "/api/graph/pick", {{"bars", 6}, {"height", 50}, {"bar_from", 5}, {"bar_to", 5}, {"top", 0}, {"bottom", 50}});
        CHECK_FALSE(picked["identifiers"].empty());
        const Json hovered = post(client, "/api/graph/hover", {{"bars", 6}, {"height", 50}, {"bar", 5}, {"y", 49.5}});
        CHECK(hovered["alive"].get<bool>());

        const auto page = client.Get("/");
        REQUIRE(page);
        CHECK(page->status == 200);
        CHECK(page->body.find("<html") != std::string::npos);
        CHECK(client.Get("/no-such-file.js")->status == 404);
    }
    std::filesystem::remove_all(folder);
}

TEST_CASE("a world saved on the daemon loads back, to carry on exactly as it would have") {
    rules::apply(rules::current());  // the tests' own settings, held within the rules' limits as loading holds them
    const auto folder = scratch("worlds");
    {
    World world(small_world(), false, folder);
    Served served(world);
    auto client = served.client();
    for (int turn = 0; turn < 20; ++turn) post(client, "/api/control", {{"action", "step"}});
    const Json saved = post(client, "/api/world/save", {{"name", "twenty-turns"}});
    CHECK(saved["note"] == "Saved the world at turn 20 as twenty-turns.");
    const auto again = client.Post("/api/world/save", Json{{"name", "twenty-turns"}}.dump(), "application/json");
    REQUIRE(again);
    CHECK(again->status == 400);  // the name is taken
    const auto bad = client.Post("/api/world/save", Json{{"name", "../escape"}}.dump(), "application/json");
    REQUIRE(bad);
    CHECK(bad->status == 400);
    const Json listed = get(client, "/api/worlds");
    REQUIRE(listed["worlds"].size() == 1);
    CHECK(listed["worlds"][0]["name"] == "twenty-turns");
    std::string at_twenty, at_thirty;
    {
        auto lock = world.lock();
        at_twenty = WorldFile::bytes(world.simulation());
    }
    for (int turn = 0; turn < 10; ++turn) post(client, "/api/control", {{"action", "step"}});
    {
        auto lock = world.lock();
        at_thirty = WorldFile::bytes(world.simulation());
    }
    const Json loaded = post(client, "/api/world/load", {{"name", "twenty-turns"}});
    CHECK(loaded["turn"] == 20);
    CHECK(loaded["playing"] == false);  // it waits, paused
    {
        auto lock = world.lock();
        CHECK(WorldFile::bytes(world.simulation()) == at_twenty);
    }
    for (int turn = 0; turn < 10; ++turn) post(client, "/api/control", {{"action", "step"}});
    {
        auto lock = world.lock();
        CHECK(WorldFile::bytes(world.simulation()) == at_thirty);  // carried on as before
    }
    CHECK(post(client, "/api/world/delete", {{"name", "twenty-turns"}})["note"] == "Deleted twenty-turns.");
    CHECK(get(client, "/api/worlds")["worlds"].empty());
    const auto gone = client.Post("/api/world/delete", Json{{"name", "twenty-turns"}}.dump(), "application/json");
    REQUIRE(gone);
    CHECK(gone->status == 400);
    }
    std::filesystem::remove_all(folder);
}

TEST_CASE("the world is kept as the daemon stops, and carried on as it starts again") {
    rules::apply(rules::current());  // the tests' own settings, held within the rules' limits as loading holds them
    const auto folder = scratch("kept");
    const auto kept = folder / "worlds" / "autosave.bzworld";
    std::string as_kept;
    {
        World world(small_world(), false, folder);
        CHECK(!world.resume(kept, true));  // none kept yet
        for (int turn = 0; turn < 15; ++turn) {
            auto lock = world.lock();
            world.step();
        }
        world.keep(kept);
        auto lock = world.lock();
        as_kept = WorldFile::bytes(world.simulation());
    }
    CHECK(std::filesystem::exists(kept));
    CHECK(!std::filesystem::exists(folder / "worlds" / "autosave.bzworld.partial"));
    {
        World again(small_world(), false, folder);
        REQUIRE(again.resume(kept, false));
        auto lock = again.lock();
        CHECK((again.simulation().turn() == 15 && !again.playing()));
        CHECK(WorldFile::bytes(again.simulation()) == as_kept);  // exactly as it stopped
    }
    {
        World playing(small_world(), false, folder);
        REQUIRE(playing.resume(kept, true));
        auto lock = playing.lock();
        CHECK(playing.playing());  // and plays on, when asked to
    }
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

TEST_CASE("the daemon serves the probe among its routes, and watched tracks follow the world's turns") {
    const auto folder = scratch("probe");
    {
        World world(small_world(), false, folder);
        Served served(world);
        auto client = served.client();
        const Json state = get(client, "/api/probe/state");
        CHECK(state["turn"] == 0);
        int watched = -1;
        {
            auto lock = world.lock();
            for (const Bitzkey* agent : world.simulation().living()) {
                if (agent->species != Species::Plant) {
                    watched = agent->identifier;
                    break;
                }
            }
        }
        REQUIRE(watched >= 0);
        CHECK(get(client, "/api/probe/animal/" + std::to_string(watched))["id"] == watched);
        post(client, "/api/probe/watch", {{"ids", Json::array({watched})}});
        for (int turn = 0; turn < 3; ++turn) post(client, "/api/control", {{"action", "step"}});
        const Json tracks = get(client, "/api/probe/watch");
        CHECK(tracks["tracks"][std::to_string(watched)].size() == 3);
        CHECK(get(client, "/api/probe/herds?species=grazer").contains("herds"));
        const auto missing = client.Get("/api/probe/animal/999999");
        REQUIRE(missing);
        CHECK(missing->status == 404);
    }
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}
