#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "core/Exchange.hpp"
#include "core/Simulation.hpp"

using namespace bitzkey;

namespace {
Simulation empty_world() {
    Simulation world(Simulation::Settings{12, 12, 0, 3});
    world.clear_grass();
    return world;
}
}  // namespace

TEST_CASE("an exported animal carries its genome, temperament and brain, in Python's layout") {
    Simulation world = empty_world();
    Bitzkey& animal = world.add_agent(encode_dna(Species::Grazer, {{"fear", 3}}), 2, 2);
    animal.mind[Fear] = 3.14159;
    const std::string text = dumps(bitzkey_record(animal));
    CHECK(text.rfind("{\"version\": 3, \"ring\": \"", 0) == 0);
    CHECK(text.find("\"social\": \"" + hex_word(social_word(animal.dna)) + "\", \"mind\"") != std::string::npos);
    CHECK(text.find("\"fear\": 3.142") != std::string::npos);
    CHECK(text.find("\"brain\": {\"senses\": [\"hunger\"") != std::string::npos);

    const auto imported = import_bitzkeys(text);
    REQUIRE(imported.size() == 1);
    CHECK(imported[0].dna == animal.dna);
    REQUIRE(imported[0].mind);
    CHECK(*imported[0].mind->traits[Fear] == 3.142);
    REQUIRE(imported[0].brain);
}

TEST_CASE("several Bitzkeys form an array, one per line, and pasted blocks all import") {
    Simulation world = empty_world();
    const Bitzkey& plant = world.add_agent(encode_dna(Species::Plant), 1, 1);
    const Bitzkey& grazer = world.add_agent(encode_dna(Species::Grazer), 2, 2);
    const std::vector<const Bitzkey*> both = {&plant, &grazer};
    const std::string text = export_bitzkeys(both);
    CHECK(text.rfind("[\n  {", 0) == 0);
    CHECK(text.find("\"mind\"", 0) > text.find('\n', 3));  // the plant's line has no mind
    const auto imported = import_bitzkeys(text + "\n,\n" + dumps(bitzkey_record(grazer)));
    CHECK(imported.size() == 3);
}

TEST_CASE("unreadable imports name the problem") {
    CHECK_THROWS_WITH_AS(import_bitzkeys("{\"ring\": 1"), doctest::Contains("Not valid JSON"), std::invalid_argument);
    CHECK_THROWS_WITH_AS(import_bitzkeys("[1, 2]"), doctest::Contains("Expected a Bitzkey"), std::invalid_argument);
    CHECK_THROWS_WITH_AS(import_bitzkeys(R"({"ring": "00000001", "dot": "00000000", "mind": {"fear": "lots"}})"),
                         doctest::Contains("must be a number"), std::invalid_argument);
}

TEST_CASE("version 1 temperament is rescaled from 0-15 to 0-7") {
    const auto imported = import_bitzkeys(R"({"ring": "00000001", "dot": "00000000", "mind": {"fear": 15}})");
    REQUIRE(imported.size() == 1);
    CHECK(*imported[0].mind->traits[Fear] == doctest::Approx(7.0));
}

TEST_CASE("a genome alone is written with all three words and imports back whole") {
    const Dna dna = encode_dna(Species::Grazer, {{"fear", 3}, {"loyalty", 5}, {"neurodivergence", 9}});
    const std::string text = dumps(genome_record(dna));
    CHECK(text == "{\"version\": 3, \"ring\": \"" + hex_word(body_word(dna)) + "\", \"dot\": \"" + hex_word(mind_word(dna)) +
                      "\", \"social\": \"" + hex_word(social_word(dna)) + "\"}");
    const auto imported = import_bitzkeys(text);  // as a design pasted into the import list
    REQUIRE(imported.size() == 1);
    CHECK(imported[0].dna == dna);
}
