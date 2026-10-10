#pragma once
// The 64-bit Bitzkey genome.
//
// The low 32 bits are the inner dot: mental, emotional and social traits. The high 32 bits are
// the outer ring: the body and its physical attributes. Each word holds ten three-bit traits
// (0 to 7), from its least-significant bits; its top two bits carry no trait but, like the rest,
// shade its colour. Read as four CMYK bytes, each word sets the colour of its part of the
// Bitzkey. Version 1 genomes held eight four-bit traits (0 to 15) per word; exported records
// without a version are version 1, and are converted when imported.

#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json_fwd.hpp>

#include "core/PyRandom.hpp"

namespace bitzkey {

using Dna = std::uint64_t;

struct Climate {
    int temperature;  // home, an index into TEMPERATURES
    bool heat, cold;  // whether it tolerates heat and cold
};

constexpr int GENOME_VERSION = 2;
constexpr int TRAIT_BITS = 3;
constexpr int TRAIT_MAX = (1 << TRAIT_BITS) - 1;
constexpr std::uint32_t WORD = 0xFFFFFFFFU;
constexpr std::uint32_t SPARE_BITS = 0xC0000000U;  // the dot's two bits that hold no mind trait: the tolerance flags
// The climate genes, in the top two bits of each word: the ring holds the home temperature, the
// dot the heat tolerance flag (bit 30) and the cold tolerance flag (bit 31).
constexpr std::array<std::string_view, 4> TEMPERATURES = {"hot", "medium", "cold", "very cold"};
constexpr int TEMPERATURE_SHIFT = 62;
constexpr Dna HEAT_TOLERANCE = Dna{1} << 30;
constexpr Dna COLD_TOLERANCE = Dna{1} << 31;
constexpr int V1_TEMPERATURE = 1;  // medium
// Default chances, per birth, of a mutation; a world can set its own.
constexpr double MIND_MUTATION_CHANCE = 0.25;  // for each of MIND_MUTATION_ROLLS: one mind trait steps
constexpr int MIND_MUTATION_ROLLS = 3;
constexpr double BODY_MUTATION_CHANCE = 0.25;  // one bit of the ring flips
constexpr double SPECIES_REROLL = 0.5;  // a body mutation landing on the species is moved to another bit, rolled afresh, this often
constexpr int V1_MAX = 15;

enum class Species : std::uint8_t { Plant, Grazer, Omnivore, Predator };
constexpr std::array<std::string_view, 4> SPECIES = {"plant", "grazer", "omnivore", "predator"};
inline std::string_view species_name(Species species) { return SPECIES[static_cast<int>(species)]; }
std::optional<Species> species_from_name(std::string_view name);

enum MindTrait : int {
    Aggression, Libido, Sociability, Curiosity, Fear, Appetite, Patience, Intellect, Empathy, Tolerance
};
constexpr int MIND_TRAIT_COUNT = 10;
constexpr std::array<std::string_view, MIND_TRAIT_COUNT> MIND_TRAITS = {
    "aggression", "libido", "sociability", "curiosity", "fear", "appetite", "patience", "intellect",
    "empathy", "tolerance"};

enum BodyTrait : int {
    SpeciesTrait, Size, Strength, Armour, Weapons, Speed, Sight, Fertility, Litter, Carrion
};
constexpr int BODY_TRAIT_COUNT = 10;
constexpr std::array<std::string_view, BODY_TRAIT_COUNT> BODY_TRAITS = {
    "species", "size", "strength", "armour", "weapons", "speed", "sight", "fertility", "litter", "carrion"};

using Mind = std::array<double, MIND_TRAIT_COUNT>;         // current temperament, 0..7 each
using Traits = std::array<int, MIND_TRAIT_COUNT>;          // inborn values, 0..7 each
using Body = std::array<int, BODY_TRAIT_COUNT>;            // body traits; [0] is the species index

// A trait value as a share of its full range, 0 to 1.
inline double share(double value) { return value / TRAIT_MAX; }

inline std::uint32_t mind_word(Dna dna) { return static_cast<std::uint32_t>(dna & WORD); }
inline std::uint32_t body_word(Dna dna) { return static_cast<std::uint32_t>((dna >> 32) & WORD); }

Traits decode_mind(Dna dna);
Body decode_body(Dna dna);  // the species slot holds the species index, the trait value mod 4
inline Climate decode_climate(Dna dna) {
    return {static_cast<int>((dna >> TEMPERATURE_SHIFT) & 0x3), (dna & HEAT_TOLERANCE) != 0, (dna & COLD_TOLERANCE) != 0};
}

// Build a genome from named traits, each 0 to 7, and climate genes ("temperature" 0 to 3,
// "heat_tolerance" and "cold_tolerance" 0 or 1); unnamed traits are zero, the home temperature
// medium, and neither tolerance. Throws std::invalid_argument for an unknown trait or a value out
// of range.
Dna encode_dna(Species species, std::initializer_list<std::pair<std::string_view, int>> traits = {});

// Re-encode a version 1 genome in this layout.
Dna convert_v1(Dna dna);

struct Rgb {
    int r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};
Rgb color_from_word(std::uint32_t word);  // one word's four CMYK bytes as an RGB colour

double colour_difference(std::uint32_t first, std::uint32_t second);  // 0 (same) to 1
inline double ring_difference(Dna first, Dna second) { return colour_difference(body_word(first), body_word(second)); }
inline double dot_difference(Dna first, Dna second) { return colour_difference(mind_word(first), mind_word(second)); }

// A random genome; given a species, the species trait is set to pick it.
Dna make_dna(PyRandom& rng, std::optional<int> species = std::nullopt);

// A child's starting temperament: the average of its parents', then up to MIND_MUTATION_ROLLS
// times, each with `chance`, a random trait steps one up or down.
Mind inherit_mind(const Mind& first, const Mind& second, PyRandom& rng, double chance = MIND_MUTATION_CHANCE);

// A dot word for a temperament: each trait's fraction rounds up with a chance equal to it.
std::uint32_t write_mind(std::uint32_t spare, const Mind& mind, PyRandom& rng);

// A child's genome: the ring a single-point crossover with a `body_chance` of one flipped bit;
// the dot written from `mind`, or from the average of the parents' inborn traits.
Dna cross(Dna first, Dna second, PyRandom& rng, const std::optional<Mind>& mind = std::nullopt,
          double mind_chance = MIND_MUTATION_CHANCE, double body_chance = BODY_MUTATION_CHANCE);

Mind to_mind(const Traits& traits);

// {"version": 2, "ring": "7421FF41", "dot": "637E4B01"}
std::string hex_word(std::uint32_t word);
Dna genome_from_record(const nlohmann::json& record);  // throws std::invalid_argument
int record_version(const nlohmann::json& record);      // throws std::invalid_argument

}  // namespace bitzkey
