// The probe: a read-only look at a running world, its Bitzkeys, herds and the tracks of those watched.

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include <httplib.h>

#include "core/Genome.hpp"
#include "core/Rules.hpp"
#include "core/Simulation.hpp"
#include "net/Probe.hpp"
#include "net/ProbeServer.hpp"

using namespace bitzkey;
using Json = net::Probe::Json;

namespace {

Simulation empty_world(int size = 40) {
    Simulation simulation(Simulation::Settings{size, size, 0, 3});
    simulation.clear_grass();
    return simulation;
}

// A probe with no server: each look runs at once.
net::Probe direct(Simulation& world) {
    return net::Probe([&world](const std::function<void(Simulation&)>& look) { look(world); });
}

}  // namespace

TEST_CASE("the probe shows the world, its Bitzkeys in brief and in full, and their families") {
    Simulation world = empty_world();
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"litter", 7}});
    Bitzkey& mother = world.add_agent(dna, 5, 5);
    Bitzkey& father = world.add_agent(dna, 5, 6);
    world.add_agent(encode_dna(Species::Predator), 30, 30);
    world.index_cells();
    world.breed(mother, father);
    world.give_birth(mother);
    net::Probe probe = direct(world);

    const Json state = probe.state(world);
    CHECK((state["turn"] == world.turn() && state["width"] == 40 && state["alive"]["predator"] == 1));
    CHECK(state["alive"]["grazer"].get<int>() > 2);

    const Json beside = probe.animals(world, "grazer", 5, 5, 8, 200);
    CHECK(beside["matched"].get<int>() == state["alive"]["grazer"].get<int>());  // all born beside her
    CHECK(probe.animals(world, "predator", 0, 0, -1, 200)["animals"][0]["x"] == 30);
    CHECK_THROWS_AS(probe.animals(world, "unicorn", 0, 0, -1, 10), std::invalid_argument);

    const Bitzkey& child = *world.agents()[3];
    const Json full = probe.animal(world, child.identifier);
    CHECK((full["mother"] == mother.identifier && full["father"] == father.identifier && full["species"] == "grazer"));
    CHECK((full.contains("mind") && full["home"] == "medium"));
    CHECK_THROWS_AS(probe.animal(world, 9999), std::out_of_range);

    const Json family = probe.family(world, mother.identifier);
    CHECK(family["descendants"]["young"].size() == world.agents().size() - 3);
    CHECK(probe.family(world, child.identifier)["ancestors"]["mother"]["id"] == mother.identifier);
}

TEST_CASE("the probe finds the herds, keeps their numbers from one look to the next, and says how they drift") {
    Simulation world = empty_world(60);
    const Dna dna = encode_dna(Species::Grazer, {{"sight", 2}});
    for (int dx = 0; dx < 4; ++dx) world.add_agent(dna, 10 + dx, 10);  // one herd in the north-west
    world.agents()[1]->mother = world.agents()[0]->identifier;  // a mother and her two young among it
    world.agents()[2]->mother = world.agents()[0]->identifier;
    for (int dx = 0; dx < 3; ++dx) world.add_agent(dna, 40 + dx, 45);  // another in the south-east
    world.add_agent(dna, 25, 30);                                       // and one alone
    world.index_cells();
    net::Probe probe = direct(world);

    const Json first = probe.herds(world, "grazer", true);
    REQUIRE(first["herds"].size() == 2);
    CHECK((first["herds"][0]["size"] == 4 && first["herds"][1]["size"] == 3 && first["alone"] == 1));
    CHECK(first["herds"][0]["drift"].is_null());  // seen for the first time
    CHECK(first["herds"][0]["kin"].get<double>() == doctest::Approx(0.75));  // three of its four with family among it
    CHECK(first["herds"][1]["kin"].get<double>() == 0.0);
    CHECK(first["kin_in_herd"].get<double>() == doctest::Approx(3.0 / 7));
    const int since = first["herds"][0]["since"];
    const int larger = first["herds"][0]["id"], smaller = first["herds"][1]["id"];

    for (const auto& agent : world.agents()) {
        if (agent->y == 10) agent->y = 12;  // the big herd moves two rows south
    }
    world.index_cells();
    const Json second = probe.herds(world, "grazer", false);
    REQUIRE(second["herds"].size() == 2);
    CHECK((second["herds"][0]["id"] == larger && second["herds"][1]["id"] == smaller));
    CHECK(second["herds"][0]["drift"][1].get<double>() == doctest::Approx(2.0));
    CHECK(second["herds"][0]["since"] == since);  // the same herd, seen since
    CHECK(!second["herds"][0].contains("members"));
}

TEST_CASE("the probe tells how calls are made, with a hunter in sight or not, and what hearing them does") {
    Simulation world = empty_world();
    Bitzkey& caller = world.add_agent(encode_dna(Species::Grazer), 10, 10);
    Bitzkey& listener = world.add_agent(encode_dna(Species::Grazer), 12, 10);
    world.index_cells();
    caller.has_last_senses = listener.has_last_senses = true;
    caller.last_senses[SenseThreatCloseness] = 0.5;  // it saw a hunter, and called
    caller.last_call = FirstCall;
    listener.last_senses[SenseThreatCloseness] = 0.5;  // and the other, hunted too, heard it and fled
    listener.last_senses[heard_sense(FirstCall, HeardLoudness)] = 1.0;
    listener.last_choice = Flee;
    listener.last_call = Silent;
    net::Probe probe = direct(world);
    const Json grazers = probe.calls(world)["calls"]["grazer"];
    CHECK(grazers["with_hunter"] == 2);
    CHECK(grazers["calls"]["call_1"]["made_with_hunter"].get<double>() == doctest::Approx(0.5));
    CHECK(grazers["calls"]["call_1"]["hunter_in_sight"]["fled_having_heard"].get<double>() == 1.0);
}

TEST_CASE("the probe tracks the Bitzkeys it watches, turn by turn, until they are gone") {
    Simulation world = empty_world();
    Bitzkey& watched = world.add_agent(encode_dna(Species::Grazer), 10, 10);
    world.add_agent(encode_dna(Species::Grazer), 30, 30);
    world.index_cells();
    net::Probe probe = direct(world);
    world.on_turn = [&probe](const Simulation& turned) { probe.after_turn(turned); };
    probe.watch({watched.identifier}, 0);
    for (int turn = 0; turn < 3; ++turn) world.advance();
    Json tracks = probe.tracks();
    const Json& track = tracks["tracks"][std::to_string(watched.identifier)];
    REQUIRE(track.size() == 3);
    CHECK((track[2]["turn"] == world.turn() && track[2].contains("x") && track[2].contains("steps_from_home")));

    world.die(watched, "killed");
    world.advance();
    tracks = probe.tracks();
    CHECK(tracks["tracks"][std::to_string(watched.identifier)].back()["gone"] == true);
    probe.unwatch({});
    CHECK(probe.tracks()["watching"] == 0);
}

TEST_CASE("the probe served on a port answers between the world's turns, and not without a world") {
    net::ProbeServer server(49731);
    httplib::Client client("127.0.0.1", server.port());
    client.set_read_timeout(10);
    auto absent = client.Get("/probe/state");
    REQUIRE(absent);
    CHECK(absent->status == 503);  // no world yet

    Simulation world = empty_world();
    world.add_agent(encode_dna(Species::Grazer), 10, 10);
    server.attach(world);
    std::atomic<bool> answered = false;
    httplib::Result got;
    std::thread asking([&] {
        got = client.Get("/probe/state");
        answered = true;
    });
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!answered && std::chrono::steady_clock::now() < until) {  // the world's thread, between turns
        server.serve(world);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    asking.join();
    REQUIRE(got);
    CHECK(got->status == 200);
    CHECK(Json::parse(got->body)["alive"]["grazer"] == 1);
    server.detach();
}

TEST_CASE("a herd holds together within a couple of steps, not merely within sight") {
    Simulation world = empty_world(60);
    const Dna dna = encode_dna(Species::Grazer, {{"sight", 3}});  // they see far beyond the gap
    for (int dx = 0; dx < 3; ++dx) world.add_agent(dna, 10 + dx, 20);
    for (int dx = 0; dx < 3; ++dx) world.add_agent(dna, 16 + dx, 20);  // four steps from the first group
    world.index_cells();
    net::Probe probe = direct(world);
    const Json tight = probe.herds(world, "grazer", false);
    REQUIRE(tight["herds"].size() == 2);
    CHECK((tight["herds"][0]["size"] == 3 && tight["herds"][1]["size"] == 3));
    const Json loose = probe.herds(world, "grazer", false, 4);
    REQUIRE(loose["herds"].size() == 1);
    CHECK(loose["herds"][0]["size"] == 6);
    CHECK_THROWS_AS(probe.herds(world, "grazer", false, 0), std::invalid_argument);
}

TEST_CASE("the probe reads the rest of the world: rules, settings, climate, grass, cells, bodies, deaths, history, brains") {
    Simulation world = empty_world();
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"litter", 7}});
    Bitzkey& mother = world.add_agent(dna, 5, 5);
    Bitzkey& father = world.add_agent(dna, 5, 6);
    world.index_cells();
    for (int turn = 0; turn < 3; ++turn) world.advance();
    net::Probe probe = direct(world);

    const Json rules = probe.rules()["rules"];
    CHECK(rules["SEASON_TURNING"]["value"].get<double>() == doctest::Approx(SEASON_TURNING));
    CHECK(rules.size() == rules::all().size());
    CHECK(probe.settings(world)["width"] == 40);
    const Json climate = probe.climate(world);
    REQUIRE(climate["rows"].size() == 40);
    CHECK(climate["rows"][0]["temperature"].get<double>() == doctest::Approx(world.temperature(0)));
    CHECK(probe.grass(world, true)["map"].size() == 40);

    const Json square = probe.cell(world, mother.x, mother.y);
    CHECK(square["living"].size() >= 1);
    CHECK_THROWS_AS(probe.cell(world, -1, 0), std::invalid_argument);

    world.die(father, "killed");
    const Json bodies = probe.bodies(world, 10);
    CHECK(bodies["count"] == 1);
    const Json deaths = probe.deaths(world, 0, "grazer", "", 10);
    CHECK((deaths["matched"].get<int>() >= 1 && deaths["deaths"][0]["cause"] == "killed"));
    CHECK(deaths["since_the_start"]["grazer"]["killed"].get<int>() >= 1);

    CHECK(probe.history(world, 50)["points"].get<int>() >= 1);
    CHECK(probe.insights(world).contains("insights"));
    const Json brain = probe.brain(world, mother.identifier, true);
    CHECK((brain["depth"].get<int>() >= 1 && brain.contains("weights")));
    if (mother.has_last_senses) CHECK(brain["values"].size() == ACTION_COUNT);
    CHECK(!probe.death_report_text(world).empty());
    CHECK(probe.timings(world).contains("ms_total"));
    CHECK(probe.animal(world, mother.identifier).contains("body"));
}

TEST_CASE("the probe counts each species' infants, and those sheltered beside a parent") {
    FEEBLE_YOUTH = 0.5;  // infants for half their lives, in this test
    Simulation world = empty_world();
    Bitzkey& mother = world.add_agent(encode_dna(Species::Grazer), 5, 5);
    Bitzkey& beside = world.add_agent(encode_dna(Species::Grazer), 5, 6);
    Bitzkey& apart = world.add_agent(encode_dna(Species::Grazer), 20, 20);
    beside.mother = apart.mother = mother.identifier;
    mother.age = static_cast<int>(mother.lifespan * 0.6);  // grown
    world.index_cells();
    const Json young = direct(world).state(world)["infants"]["grazer"];
    CHECK((young["infants"] == 2 && young["sheltered"] == 1));
    FEEBLE_YOUTH = 0.0;
}
