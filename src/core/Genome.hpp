#pragma once
// The 96-bit Bitzkey genome.
//
// The low 32 bits are the inner dot: mental and emotional traits. The next 32 bits are the outer
// ring: the body and its physical attributes. The high 32 bits are the social background, behind
// the species icon: how it treats others. The dot and ring each hold ten three-bit traits (0 to 7),
// from their least-significant bits; their top two bits carry no trait but the climate genes. The
// ring's first trait is the species, in its lower two bits; its third bit is the activity gene, nocturnal (set) or
// diurnal. The social background holds seven
// three-bit traits, then the four-bit neurodivergence gene (0 to 15), which does nothing yet, then
// the two-bit terrain gene; its top five bits are spare. Like the rest, they all shade the colour:
// read as four CMYK bytes, each word sets the colour of its part of the Bitzkey. Version 1 genomes
// held eight four-bit traits (0 to 15) per word, and version 2 ten three-bit traits in each of the
// dot and ring, with sociability, empathy and tolerance in the dot; exported records without a
// version are version 1, and older versions are converted when imported. Version 3 genomes had no
// terrain, its bits spare and random: they are terrestrial when converted.

#include <array>
#include <compare>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json_fwd.hpp>

#include "core/PyRandom.hpp"

namespace bitzkey {

// A genome: the dot and the ring in `bits` (the dot the low word), and the social background.
struct Dna {
    std::uint64_t bits = 0;
    std::uint32_t social = 0;
    constexpr Dna() = default;
    constexpr Dna(std::uint64_t bits_, std::uint32_t social_ = 0) : bits(bits_), social(social_) {}
    auto operator<=>(const Dna&) const = default;  // in order, for sorting and maps
};

struct Climate {
    int temperature;  // home, an index into TEMPERATURES
    bool heat, cold;  // whether it tolerates heat or cold, never both
};

constexpr int GENOME_VERSION = 4;
constexpr int TRAIT_BITS = 3;
constexpr int TRAIT_MAX = (1 << TRAIT_BITS) - 1;
constexpr std::uint32_t WORD = 0xFFFFFFFFU;
constexpr std::uint32_t SPARE_BITS = 0xC0000000U;  // the dot's two bits that hold no mind trait: the tolerance flags
// The climate genes, in the top two bits of the dot and ring: the ring holds the home temperature, the
// dot the heat tolerance flag (bit 30) and the cold tolerance flag (bit 31).
constexpr std::array<std::string_view, 4> TEMPERATURES = {"hot", "medium", "cold", "very cold"};
constexpr int TEMPERATURE_SHIFT = 62;
constexpr std::uint64_t HEAT_TOLERANCE = std::uint64_t{1} << 30;
constexpr std::uint64_t COLD_TOLERANCE = std::uint64_t{1} << 31;
constexpr int V1_TEMPERATURE = 1;  // medium
// Default chances, per birth, of a mutation; a world can set its own.
constexpr double MIND_MUTATION_CHANCE = 0.25;  // for each of MIND_MUTATION_ROLLS: one temperament trait steps
constexpr int MIND_MUTATION_ROLLS = 3;
constexpr double BODY_MUTATION_CHANCE = 0.25;  // one bit of the ring flips
constexpr double SPECIES_REROLL = 0.5;  // a body mutation landing on the species is moved to another bit, rolled afresh, this often
// A birth's chance of its terrain moving a step along TERRAIN_CHAIN, as a share of its body mutation chance.
constexpr double TERRAIN_MUTATION_SHARE = 1.0 / 32;
constexpr std::uint64_t NOCTURNAL = std::uint64_t{1} << 34;  // the species trait's third bit: active by night
constexpr int V1_MAX = 15;
constexpr int V2_TRAIT_DEFAULT = 3;  // the traits version 2 lacked start here when converted

enum class Species : std::uint8_t { Plant, Grazer, Omnivore, Predator };
constexpr std::array<std::string_view, 4> SPECIES = {"plant", "grazer", "omnivore", "predator"};
inline std::string_view species_name(Species species) { return SPECIES[static_cast<int>(species)]; }
std::optional<Species> species_from_name(std::string_view name);

// The temperament: the dot's ten mind traits, then the social background's seven.
enum MindTrait : int {
    Aggression, Libido, Curiosity, Fear, Appetite, Patience, Intellect, Memory, Resilience, Moodiness,
    Sociability, Empathy, Tolerance, Loyalty, Dominance, Trust, Kinship
};
constexpr int DOT_TRAIT_COUNT = 10;
constexpr int SOCIAL_TRAIT_COUNT = 7;
constexpr int TEMPERAMENT_COUNT = DOT_TRAIT_COUNT + SOCIAL_TRAIT_COUNT;
constexpr std::array<std::string_view, TEMPERAMENT_COUNT> TEMPERAMENT = {
    "aggression", "libido", "curiosity", "fear", "appetite", "patience", "intellect", "memory", "resilience",
    "moodiness", "sociability", "empathy", "tolerance", "loyalty", "dominance", "trust", "kinship"};
constexpr int NEURODIVERGENCE_SHIFT = TRAIT_BITS * SOCIAL_TRAIT_COUNT;  // in the social background, after its traits
constexpr int NEURODIVERGENCE_MAX = 15;                                // four bits
constexpr std::uint32_t SOCIAL_SPARE = WORD & ~((1U << NEURODIVERGENCE_SHIFT) - 1);  // its bits that hold no trait
// Terrain: where a Bitzkey can live, in the two bits after neurodivergence. The terrestrial keep to the land and
// the aquatic to the sea; the avian and the amphibious go freely between them.
enum class Terrain : std::uint8_t { Terrestrial, Aquatic, Avian, Amphibious };
constexpr std::array<std::string_view, 4> TERRAINS = {"terrestrial", "aquatic", "avian", "amphibious"};
constexpr int TERRAIN_SHIFT = NEURODIVERGENCE_SHIFT + 4;
constexpr std::uint32_t TERRAIN_BITS = 0x3U << TERRAIN_SHIFT;
inline std::string_view terrain_name(Terrain terrain) { return TERRAINS[static_cast<int>(terrain)]; }
std::optional<Terrain> terrain_from_name(std::string_view name);
// Whether a Bitzkey of this terrain may be in the sea, or on the land.
inline bool swims(Terrain terrain) { return terrain != Terrain::Terrestrial; }
inline bool walks(Terrain terrain) { return terrain != Terrain::Aquatic; }
inline bool fits(Terrain terrain, bool sea) { return sea ? swims(terrain) : walks(terrain); }
// A terrain mutation moves it a step along this chain, from the sea to the air.
constexpr std::array<Terrain, 4> TERRAIN_CHAIN = {Terrain::Aquatic, Terrain::Amphibious, Terrain::Terrestrial, Terrain::Avian};

enum BodyTrait : int {
    SpeciesTrait, Size, Strength, Armour, Weapons, Speed, Sight, Fertility, Litter, Carrion
};
constexpr int BODY_TRAIT_COUNT = 10;
constexpr std::array<std::string_view, BODY_TRAIT_COUNT> BODY_TRAITS = {
    "species", "size", "strength", "armour", "weapons", "speed", "sight", "fertility", "litter", "carrion"};

using Mind = std::array<double, TEMPERAMENT_COUNT>;         // current temperament, 0..7 each
using Traits = std::array<int, TEMPERAMENT_COUNT>;          // inborn values, 0..7 each
using Body = std::array<int, BODY_TRAIT_COUNT>;            // body traits; [0] is the species index

// A trait value as a share of its full range, 0 to 1.
inline double share(double value) { return value / TRAIT_MAX; }

inline std::uint32_t mind_word(Dna dna) { return static_cast<std::uint32_t>(dna.bits & WORD); }
inline std::uint32_t body_word(Dna dna) { return static_cast<std::uint32_t>((dna.bits >> 32) & WORD); }
inline std::uint32_t social_word(Dna dna) { return dna.social; }

Traits decode_temperament(Dna dna);  // the dot's traits, then the social background's
Body decode_body(Dna dna);  // the species slot holds the species index, the trait value mod 4
inline int decode_neurodivergence(Dna dna) { return static_cast<int>((dna.social >> NEURODIVERGENCE_SHIFT) & NEURODIVERGENCE_MAX); }
inline bool decode_nocturnal(Dna dna) { return (dna.bits & NOCTURNAL) != 0; }
inline Terrain decode_terrain(Dna dna) { return static_cast<Terrain>((dna.social & TERRAIN_BITS) >> TERRAIN_SHIFT); }
inline Dna with_terrain(Dna dna, Terrain terrain) {
    dna.social = (dna.social & ~TERRAIN_BITS) | static_cast<std::uint32_t>(terrain) << TERRAIN_SHIFT;
    return dna;
}
inline Climate decode_climate(Dna dna) {  // both tolerance flags together cancel out, tolerating neither
    const bool heat = (dna.bits & HEAT_TOLERANCE) != 0, cold = (dna.bits & COLD_TOLERANCE) != 0;
    return {static_cast<int>((dna.bits >> TEMPERATURE_SHIFT) & 0x3), heat && !cold, cold && !heat};
}

// Build a genome from named traits, each 0 to 7 ("neurodivergence" 0 to 15, "terrain" 0 to 3), and climate
// genes ("temperature" 0 to 3, "heat_tolerance" and "cold_tolerance" 0 or 1); unnamed traits are zero, the
// home temperature medium, neither tolerance, and terrestrial. Throws std::invalid_argument for an unknown trait
// or a value out of range.
Dna encode_dna(Species species, std::initializer_list<std::pair<std::string_view, int>> traits = {});

// Re-encode a version 1 genome in this layout.
Dna convert_v1(std::uint64_t dna);
// Re-encode a version 2 genome in this layout: its dot's sociability, empathy and tolerance moved to the
// social background, and the traits version 2 lacked at their defaults.
Dna convert_v2(std::uint64_t dna);
// Re-encode a version 3 genome in this layout: terrestrial, whatever its spare bits held.
inline Dna convert_v3(Dna dna) { return with_terrain(dna, Terrain::Terrestrial); }

struct Rgb {
    int r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};
Rgb color_from_word(std::uint32_t word);  // one word's four CMYK bytes as an RGB colour

double colour_difference(std::uint32_t first, std::uint32_t second);  // 0 (same) to 1
inline double ring_difference(Dna first, Dna second) { return colour_difference(body_word(first), body_word(second)); }
inline double dot_difference(Dna first, Dna second) { return colour_difference(mind_word(first), mind_word(second)); }
inline double social_difference(Dna first, Dna second) { return colour_difference(social_word(first), social_word(second)); }

// A random genome; given a species, the species trait is set to pick it.
Dna make_dna(PyRandom& rng, std::optional<int> species = std::nullopt);

// A child's starting temperament: the average of its parents', then up to MIND_MUTATION_ROLLS
// times, each with `chance`, a random trait steps one up or down.
Mind inherit_mind(const Mind& first, const Mind& second, PyRandom& rng, double chance = MIND_MUTATION_CHANCE);

// A dot word for a temperament's mind traits: each trait's fraction rounds up with a chance equal to it.
std::uint32_t write_mind(std::uint32_t spare, const Mind& mind, PyRandom& rng);
// The same for a social background word, from its social traits; `spare` gives its neurodivergence and spare bits.
std::uint32_t write_social(std::uint32_t spare, const Mind& mind, PyRandom& rng);

// A child's genome: the ring a single-point crossover with a `body_chance` of one flipped bit;
// the dot and social background written from `mind`, or from the average of the parents' inborn traits.
// Its terrain is one parent's, as the rest of the social background's spare bits are; with `terrain_mutation`, a
// birth has TERRAIN_MUTATION_SHARE of the body mutation chance of moving it a step along TERRAIN_CHAIN, toward the
// sea or the air as likely (and no further than the chain's end).
Dna cross(Dna first, Dna second, PyRandom& rng, const std::optional<Mind>& mind = std::nullopt,
          double mind_chance = MIND_MUTATION_CHANCE, double body_chance = BODY_MUTATION_CHANCE,
          bool terrain_mutation = true);

Mind to_mind(const Traits& traits);

// {"version": 4, "ring": "7421FF41", "dot": "637E4B01", "social": "0A1B2C3D"}
std::string hex_word(std::uint32_t word);
Dna genome_from_record(const nlohmann::json& record);  // throws std::invalid_argument
int record_version(const nlohmann::json& record);      // throws std::invalid_argument

}  // namespace bitzkey
