#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "core/Genome.hpp"

using namespace bitzkey;

TEST_CASE("traits encode into their words and decode back") {
    const Dna dna = encode_dna(Species::Predator, {{"size", 7}, {"fear", 3}, {"tolerance", 5}, {"carrion", 2}});
    CHECK(decode_body(dna)[SpeciesTrait] == 3);
    CHECK(decode_body(dna)[Size] == 7);
    CHECK(decode_body(dna)[Carrion] == 2);
    CHECK(decode_mind(dna)[Fear] == 3);
    CHECK(decode_mind(dna)[Tolerance] == 5);
    CHECK_THROWS_AS(encode_dna(Species::Grazer, {{"size", 8}}), std::invalid_argument);
    CHECK_THROWS_AS(encode_dna(Species::Grazer, {{"wings", 1}}), std::invalid_argument);
}

TEST_CASE("the climate genes hold the top bits of each word") {
    CHECK(TEMPERATURES[3] == "very cold");
    const Dna dna = encode_dna(Species::Grazer, {{"temperature", 3}, {"cold_tolerance", 1}, {"size", 7}});
    const Climate climate = decode_climate(dna);
    CHECK((climate.temperature == 3 && !climate.heat && climate.cold));
    CHECK((body_word(dna) >> 30) == 3);
    CHECK((mind_word(dna) >> 30) == 2);
    CHECK(decode_body(dna)[Size] == 7);
    CHECK(decode_climate(encode_dna(Species::Plant, {{"heat_tolerance", 1}})).temperature == 1);  // medium by default
    CHECK(decode_climate(encode_dna(Species::Plant, {{"heat_tolerance", 1}})).heat);
    CHECK_THROWS_AS(encode_dna(Species::Grazer, {{"temperature", 4}}), std::invalid_argument);
}

TEST_CASE("a child inherits its tolerances together from one parent") {
    const Dna hardy = encode_dna(Species::Grazer, {{"heat_tolerance", 1}, {"cold_tolerance", 1}});
    const Dna tender = encode_dna(Species::Grazer);
    PyRandom rng(5);
    bool both = false, neither = false;
    for (int child = 0; child < 60; ++child) {
        const Climate climate = decode_climate(cross(hardy, tender, rng));
        CHECK(climate.heat == climate.cold);
        both |= climate.heat;
        neither |= !climate.heat;
    }
    CHECK((both && neither));
}

TEST_CASE("colour words turn into RGB and colour differences") {
    CHECK(color_from_word(0x00000000) == Rgb{255, 255, 255});
    CHECK(color_from_word(0xFF0000FF) == Rgb{0, 0, 0});
    CHECK(colour_difference(0x00000000, 0xFF000000) == doctest::Approx(0.25));
    CHECK(colour_difference(0x12345678, 0x12345678) == 0.0);
}

TEST_CASE("a child's mind averages its parents' and mutates a step at a time") {
    PyRandom rng(3);
    Mind low{}, high{};
    low.fill(2.0);
    high.fill(6.5);
    const Mind stable = inherit_mind(low, high, rng, 0.0);
    for (const double value : stable) CHECK(value == 4.25);
    for (int round = 0; round < 200; ++round) {
        const Mind child = inherit_mind(low, low, rng, 1.0);
        double steps = 0;
        for (const double value : child) steps += std::abs(value - 2.0);
        CHECK(steps <= 3);
        CHECK(steps >= 1);
    }
}

TEST_CASE("a written mind shows in the dot, fractions rounding by chance") {
    PyRandom rng(2);
    Mind mind{};
    mind[Fear] = 3.25;
    mind[Libido] = 7.0;
    int total = 0;
    for (int round = 0; round < 4000; ++round) {
        const Dna dna = cross(0, 0, rng, mind);
        const int fear = decode_mind(dna)[Fear];
        CHECK((fear == 3 || fear == 4));
        CHECK(decode_mind(dna)[Libido] == 7);
        total += fear;
    }
    CHECK(total / 4000.0 == doctest::Approx(3.25).epsilon(0.02));
}

TEST_CASE("genome records read back, and version 1 records convert") {
    const Dna dna = (static_cast<Dna>(0x7421FF41) << 32) | 0x637E4B01;
    const nlohmann::json record = {{"version", 2}, {"ring", hex_word(body_word(dna))}, {"dot", hex_word(mind_word(dna))}};
    CHECK(genome_from_record(record) == dna);
    CHECK(hex_word(body_word(dna)) == "7421FF41");
    const nlohmann::json old = {{"ring", "#00000002"}, {"dot", "0000000F"}};
    const Dna converted = genome_from_record(old);
    CHECK(decode_body(converted)[SpeciesTrait] == 2);
    CHECK(decode_mind(converted)[Aggression] == 7);
    CHECK(decode_mind(converted)[Tolerance] == 3);
    CHECK_THROWS_AS(genome_from_record({{"ring", "xyz"}, {"dot", "00000000"}}), std::invalid_argument);
    CHECK_THROWS_AS(genome_from_record({{"version", 9}, {"ring", "00000000"}, {"dot", "00000000"}}), std::invalid_argument);
}
