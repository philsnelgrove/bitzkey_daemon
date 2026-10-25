#include <doctest/doctest.h>

#include <map>

#include <nlohmann/json.hpp>

#include "core/Genome.hpp"

using namespace bitzkey;

TEST_CASE("traits encode into their words and decode back") {
    const Dna dna = encode_dna(Species::Predator, {{"size", 7}, {"fear", 3}, {"moodiness", 5}, {"carrion", 2}, {"tolerance", 5},
                                                   {"kinship", 1}, {"neurodivergence", 9}});
    CHECK(decode_body(dna)[SpeciesTrait] == 3);
    CHECK(decode_body(dna)[Size] == 7);
    CHECK(decode_body(dna)[Carrion] == 2);
    CHECK(decode_temperament(dna)[Fear] == 3);
    CHECK(decode_temperament(dna)[Moodiness] == 5);
    CHECK(decode_temperament(dna)[Tolerance] == 5);
    CHECK(decode_temperament(dna)[Kinship] == 1);
    CHECK(decode_neurodivergence(dna) == 9);
    CHECK(mind_word(dna) == ((3U << 9) | (5U << 27)));
    CHECK(social_word(dna) == ((5U << 6) | (1U << 18) | (9U << 21)));
    CHECK_THROWS_AS(encode_dna(Species::Grazer, {{"size", 8}}), std::invalid_argument);
    CHECK_THROWS_AS(encode_dna(Species::Grazer, {{"neurodivergence", 16}}), std::invalid_argument);
    CHECK_THROWS_AS(encode_dna(Species::Grazer, {{"wings", 1}}), std::invalid_argument);
}

TEST_CASE("a child inherits its neurodivergence whole from one parent") {
    const Dna first = encode_dna(Species::Grazer, {{"neurodivergence", 3}}), second = encode_dna(Species::Grazer, {{"neurodivergence", 12}});
    PyRandom rng(8);
    bool three = false, twelve = false;
    for (int child = 0; child < 60; ++child) {
        const int neurodivergence = decode_neurodivergence(cross(first, second, rng));
        CHECK((neurodivergence == 3 || neurodivergence == 12));
        three |= neurodivergence == 3;
        twelve |= neurodivergence == 12;
    }
    CHECK((three && twelve));
}

TEST_CASE("terrain sits after neurodivergence, and a child takes it whole from one parent") {
    const Dna avian = encode_dna(Species::Grazer, {{"terrain", 2}, {"neurodivergence", 15}});
    CHECK(decode_terrain(avian) == Terrain::Avian);
    CHECK(decode_neurodivergence(avian) == 15);
    CHECK(social_word(avian) == ((15U << 21) | (2U << 25)));
    CHECK(decode_terrain(encode_dna(Species::Grazer)) == Terrain::Terrestrial);
    CHECK(terrain_from_name("amphibious") == Terrain::Amphibious);
    CHECK_THROWS_AS(encode_dna(Species::Grazer, {{"terrain", 4}}), std::invalid_argument);
    CHECK((fits(Terrain::Terrestrial, false) && !fits(Terrain::Terrestrial, true) && fits(Terrain::Aquatic, true) &&
           !fits(Terrain::Aquatic, false) && fits(Terrain::Avian, true) && fits(Terrain::Amphibious, false)));
    const Dna aquatic = encode_dna(Species::Grazer, {{"terrain", 1}});
    PyRandom rng(4);
    bool both[2] = {};
    for (int child = 0; child < 60; ++child) {
        const Terrain terrain = decode_terrain(cross(avian, aquatic, rng, std::nullopt, 0.0, 0.0));
        CHECK((terrain == Terrain::Avian || terrain == Terrain::Aquatic));
        both[terrain == Terrain::Avian] = true;
    }
    CHECK((both[0] && both[1]));
}

TEST_CASE("a terrain mutation moves it a step along its chain, toward the sea or the air") {
    const auto children = [](Dna parent, bool mutation) {
        PyRandom rng(12);
        std::map<Terrain, int> found;
        for (int child = 0; child < 4000; ++child) found[decode_terrain(cross(parent, parent, rng, std::nullopt, 0.0, 1.0, mutation))] += 1;
        return found;
    };
    const auto amphibious = children(encode_dna(Species::Grazer, {{"terrain", 3}}), true);
    CHECK((amphibious.at(Terrain::Aquatic) > 0 && amphibious.at(Terrain::Terrestrial) > 0));  // a step either way
    CHECK(amphibious.count(Terrain::Avian) == 0);  // never two
    const double moved = 4000 - amphibious.at(Terrain::Amphibious);
    CHECK(moved / 4000 == doctest::Approx(TERRAIN_MUTATION_SHARE).epsilon(0.35));  // at its share of the body mutations
    CHECK(children(encode_dna(Species::Grazer, {{"terrain", 3}}), false).size() == 1);  // none where it's off
    const auto fish = children(encode_dna(Species::Grazer, {{"terrain", 1}}), true);  // at the sea's end already
    CHECK(fish.count(Terrain::Terrestrial) == 0);
}

TEST_CASE("the species' third bit is the activity gene") {
    const Dna diurnal = encode_dna(Species::Omnivore);
    CHECK_FALSE(decode_nocturnal(diurnal));
    const Dna nocturnal(diurnal.bits | NOCTURNAL, diurnal.social);
    CHECK(decode_nocturnal(nocturnal));
    CHECK(decode_body(nocturnal)[SpeciesTrait] == static_cast<int>(Species::Omnivore));  // the species as it was
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
    const Dna tender = encode_dna(Species::Grazer, {{"cold_tolerance", 1}});
    PyRandom rng(5);
    bool cold = false, neither = false;
    for (int child = 0; child < 60; ++child) {  // both flags cancel out, or only cold
        const Climate climate = decode_climate(cross(hardy, tender, rng));
        CHECK(!climate.heat);
        cold |= climate.cold;
        neither |= !climate.cold;
    }
    CHECK((cold && neither));
    const Climate both = decode_climate(hardy);
    CHECK((!both.heat && !both.cold));
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
    mind[Trust] = 6.0;
    for (int round = 0; round < 4000; ++round) {
        const Dna dna = cross(0, 0, rng, mind);
        const int fear = decode_temperament(dna)[Fear];
        CHECK((fear == 3 || fear == 4));
        CHECK(decode_temperament(dna)[Libido] == 7);
        CHECK(decode_temperament(dna)[Trust] == 6);
        total += fear;
    }
    CHECK(total / 4000.0 == doctest::Approx(3.25).epsilon(0.02));
}

TEST_CASE("genome records read back, and version 1, 2 and 3 records convert") {
    const Dna dna{(std::uint64_t{0x7421FF41} << 32) | 0x637E4B01, 0x0D1B2C3D};  // avian
    const nlohmann::json record = {{"version", 4}, {"ring", hex_word(body_word(dna))}, {"dot", hex_word(mind_word(dna))},
                                   {"social", hex_word(social_word(dna))}};
    CHECK(decode_terrain(dna) == Terrain::Avian);
    CHECK(genome_from_record(record) == dna);
    nlohmann::json before_terrain = record;  // version 3: those bits were spare, and it is terrestrial
    before_terrain["version"] = 3;
    CHECK(genome_from_record(before_terrain) == with_terrain(dna, Terrain::Terrestrial));
    CHECK(genome_from_record(before_terrain).social == 0x091B2C3DU);
    CHECK(hex_word(body_word(dna)) == "7421FF41");
    CHECK_THROWS_AS(genome_from_record({{"version", 3}, {"ring", "00000000"}, {"dot", "00000000"}}), std::invalid_argument);
    const nlohmann::json old = {{"ring", "#00000002"}, {"dot", "0000000F"}};
    const Dna converted = genome_from_record(old);
    CHECK(decode_body(converted)[SpeciesTrait] == 2);
    CHECK(decode_temperament(converted)[Aggression] == 7);
    CHECK(decode_temperament(converted)[Tolerance] == 3);
    CHECK(decode_temperament(converted)[Memory] == 3);
    // Version 2's dot: aggression 1, sociability 2, curiosity 6, intellect 5, empathy 4, tolerance 3; cold-hardy.
    const std::uint32_t old_dot = 1U | (2U << 6) | (6U << 9) | (5U << 21) | (4U << 24) | (3U << 27) | (1U << 31);
    const Dna moved = genome_from_record({{"version", 2}, {"ring", "40000002"}, {"dot", hex_word(old_dot)}});
    CHECK(moved == convert_v2((std::uint64_t{0x40000002} << 32) | old_dot));
    CHECK(body_word(moved) == 0x40000002U);
    CHECK(decode_climate(moved).cold);
    const Traits traits = decode_temperament(moved);
    CHECK((traits[Aggression] == 1 && traits[Curiosity] == 6 && traits[Intellect] == 5 && traits[Memory] == 3 &&
           traits[Resilience] == 3 && traits[Moodiness] == 3));
    CHECK((traits[Sociability] == 2 && traits[Empathy] == 4 && traits[Tolerance] == 3 && traits[Loyalty] == 3 &&
           traits[Dominance] == 3 && traits[Trust] == 3 && traits[Kinship] == 3));
    CHECK(decode_neurodivergence(moved) == 0);
    CHECK_THROWS_AS(genome_from_record({{"ring", "xyz"}, {"dot", "00000000"}}), std::invalid_argument);
    CHECK_THROWS_AS(genome_from_record({{"version", 9}, {"ring", "00000000"}, {"dot", "00000000"}}), std::invalid_argument);
}
