#include "core/Genome.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "core/PyFormat.hpp"

namespace bitzkey {

std::optional<Species> species_from_name(std::string_view name) {
    for (int index = 0; index < 4; ++index) {
        if (SPECIES[index] == name) return static_cast<Species>(index);
    }
    return std::nullopt;
}

namespace {
int trait_at(std::uint32_t word, int index, int bits = TRAIT_BITS) {
    return static_cast<int>((word >> (bits * index)) & ((1U << bits) - 1));
}

// Version 2's dot, in order.
constexpr std::array<MindTrait, 10> V2_DOT = {Aggression, Libido, Sociability, Curiosity, Fear, Appetite, Patience,
                                              Intellect, Empathy, Tolerance};

// A genome from its ring, a temperament written into the dot and social background, and their other bits.
Dna compose(std::uint32_t ring, const Traits& temperament, std::uint32_t dot_spare, std::uint32_t social_spare) {
    std::uint32_t dot = dot_spare, social = social_spare;
    for (int index = 0; index < DOT_TRAIT_COUNT; ++index) dot |= static_cast<std::uint32_t>(temperament[index]) << (TRAIT_BITS * index);
    for (int index = 0; index < SOCIAL_TRAIT_COUNT; ++index) {
        social |= static_cast<std::uint32_t>(temperament[DOT_TRAIT_COUNT + index]) << (TRAIT_BITS * index);
    }
    return {static_cast<std::uint64_t>(ring) << 32 | dot, social};
}
}  // namespace

Traits decode_temperament(Dna dna) {
    Traits traits{};
    for (int index = 0; index < DOT_TRAIT_COUNT; ++index) traits[index] = trait_at(mind_word(dna), index);
    for (int index = 0; index < SOCIAL_TRAIT_COUNT; ++index) traits[DOT_TRAIT_COUNT + index] = trait_at(social_word(dna), index);
    return traits;
}

Body decode_body(Dna dna) {
    Body body{};
    for (int index = 0; index < BODY_TRAIT_COUNT; ++index) body[index] = trait_at(body_word(dna), index);
    body[SpeciesTrait] %= 4;
    return body;
}

Dna encode_dna(Species species, std::initializer_list<std::pair<std::string_view, int>> traits) {
    Dna dna{static_cast<std::uint64_t>(species) << 32};
    int temperature = V1_TEMPERATURE;
    for (const auto& [name, value] : traits) {
        if (name == "temperature") {
            if (value < 0 || value > 3) throw std::invalid_argument("Temperatures run from 0 to 3: " + std::to_string(value) + ".");
            temperature = value;
            continue;
        }
        if (name == "heat_tolerance" || name == "cold_tolerance") {
            if (value) dna.bits |= name == "heat_tolerance" ? HEAT_TOLERANCE : COLD_TOLERANCE;
            continue;
        }
        if (name == "neurodivergence") {
            if (value < 0 || value > NEURODIVERGENCE_MAX) {
                throw std::invalid_argument("Neurodivergence runs from 0 to 15: " + std::to_string(value) + ".");
            }
            dna.social |= static_cast<std::uint32_t>(value) << NEURODIVERGENCE_SHIFT;
            continue;
        }
        if (value < 0 || value > TRAIT_MAX) {
            throw std::invalid_argument("Traits run from 0 to 7: " + std::string(name) + " " + std::to_string(value) + ".");
        }
        bool known = false;
        for (int index = 0; index < TEMPERAMENT_COUNT; ++index) {
            if (TEMPERAMENT[index] != name) continue;
            if (index < DOT_TRAIT_COUNT) dna.bits |= static_cast<std::uint64_t>(value) << (TRAIT_BITS * index);
            else dna.social |= static_cast<std::uint32_t>(value) << (TRAIT_BITS * (index - DOT_TRAIT_COUNT));
            known = true;
        }
        for (int index = 1; index < BODY_TRAIT_COUNT; ++index) {
            if (BODY_TRAITS[index] == name) {
                dna.bits |= static_cast<std::uint64_t>(value) << (32 + TRAIT_BITS * index);
                known = true;
            }
        }
        if (!known) throw std::invalid_argument("Unknown traits: " + std::string(name) + ".");
    }
    dna.bits |= static_cast<std::uint64_t>(temperature) << TEMPERATURE_SHIFT;
    return dna;
}

Dna convert_v1(std::uint64_t dna) {
    // Version 1: the first eight traits of each word, four bits each, the dot's in version 2's order.
    // The traits it lacked start at defaults: empathy 0, tolerance 3, litter 0, carrion 0, a medium
    // home temperature and neither tolerance; and those version 2 lacked at theirs.
    const auto scaled = [](int value) { return static_cast<int>(py_round(static_cast<double>(value) * TRAIT_MAX / V1_MAX)); };
    const auto dot = static_cast<std::uint32_t>(dna & WORD), body = static_cast<std::uint32_t>(dna >> 32);
    Traits temperament;
    temperament.fill(V2_TRAIT_DEFAULT);
    temperament[Empathy] = 0;
    temperament[Tolerance] = 3;
    for (int index = 0; index < 8; ++index) temperament[V2_DOT[index]] = scaled(trait_at(dot, index, 4));
    std::uint32_t ring = static_cast<std::uint32_t>(trait_at(body, 0, 4) % 4) | static_cast<std::uint32_t>(V1_TEMPERATURE) << 30;
    for (int index = 1; index < 8; ++index) ring |= static_cast<std::uint32_t>(scaled(trait_at(body, index, 4))) << (TRAIT_BITS * index);
    return compose(ring, temperament, 0, 0);
}

Dna convert_v2(std::uint64_t dna) {
    const auto dot = static_cast<std::uint32_t>(dna & WORD);
    Traits temperament;
    temperament.fill(V2_TRAIT_DEFAULT);
    for (int index = 0; index < 10; ++index) temperament[V2_DOT[index]] = trait_at(dot, index);
    return compose(static_cast<std::uint32_t>(dna >> 32), temperament, dot & SPARE_BITS, 0);
}

Rgb color_from_word(std::uint32_t word) {
    const int cyan = (word >> 24) & 0xFF, magenta = (word >> 16) & 0xFF, yellow = (word >> 8) & 0xFF,
              black = word & 0xFF;
    const auto channel = [black](int ink) {
        return static_cast<int>(py_round(static_cast<double>((255 - ink) * (255 - black)) / 255));
    };
    return {channel(cyan), channel(magenta), channel(yellow)};
}

double colour_difference(std::uint32_t first, std::uint32_t second) {
    int total = 0;
    for (const int shift : {24, 16, 8, 0}) {
        total += std::abs(static_cast<int>((first >> shift) & 0xFF) - static_cast<int>((second >> shift) & 0xFF));
    }
    return static_cast<double>(total) / (4 * 255);
}

Dna make_dna(PyRandom& rng, std::optional<int> species) {
    Dna dna{rng.getrandbits(64)};  // as Python draws 96 bits: the low words first
    dna.social = static_cast<std::uint32_t>(rng.getrandbits(32));
    if (species) dna.bits = (dna.bits & ~(std::uint64_t{0x3} << 32)) | (static_cast<std::uint64_t>(*species % 4) << 32);
    return dna;
}

Mind to_mind(const Traits& traits) {
    Mind mind{};
    for (int index = 0; index < TEMPERAMENT_COUNT; ++index) mind[index] = traits[index];
    return mind;
}

Mind inherit_mind(const Mind& first, const Mind& second, PyRandom& rng, double chance) {
    Mind mind{};
    for (int index = 0; index < TEMPERAMENT_COUNT; ++index) mind[index] = (first[index] + second[index]) / 2;
    static constexpr std::array<int, 2> steps = {-1, 1};
    for (int roll = 0; roll < MIND_MUTATION_ROLLS; ++roll) {
        if (rng.random() >= chance) continue;
        const int trait = static_cast<int>(rng.randbelow(TEMPERAMENT_COUNT));
        int step = rng.choice(steps);
        if (!(0 <= mind[trait] + step && mind[trait] + step <= TRAIT_MAX)) step = -step;
        mind[trait] = std::max(0.0, std::min(static_cast<double>(TRAIT_MAX), mind[trait] + step));
    }
    return mind;
}

namespace {
// A word for `count` traits of a temperament from `first`, each trait's fraction rounding up by chance.
std::uint32_t write_traits(std::uint32_t word, const Mind& mind, int first, int count, PyRandom& rng) {
    for (int index = 0; index < count; ++index) {
        const double whole = std::floor(mind[first + index]);
        const double fraction = mind[first + index] - whole;
        int value = static_cast<int>(whole) + ((fraction != 0.0 && rng.random() < fraction) ? 1 : 0);
        value = std::min(TRAIT_MAX, value);
        word |= static_cast<std::uint32_t>(value) << (TRAIT_BITS * index);
    }
    return word;
}
}  // namespace

std::uint32_t write_mind(std::uint32_t spare, const Mind& mind, PyRandom& rng) {
    return write_traits(spare & SPARE_BITS, mind, 0, DOT_TRAIT_COUNT, rng);
}

std::uint32_t write_social(std::uint32_t spare, const Mind& mind, PyRandom& rng) {
    return write_traits(spare & SOCIAL_SPARE, mind, DOT_TRAIT_COUNT, SOCIAL_TRAIT_COUNT, rng);
}

Dna cross(Dna first, Dna second, PyRandom& rng, const std::optional<Mind>& mind, double mind_chance,
          double body_chance) {
    const std::uint32_t mask = static_cast<std::uint32_t>((1ULL << rng.randrange(1, 32)) - 1);
    std::uint32_t body = (body_word(first) & mask) | (body_word(second) & ~mask & WORD);
    if (rng.random() < body_chance) {
        int bit = static_cast<int>(rng.randrange(32));
        if (bit < 2 && rng.random() < SPECIES_REROLL) bit = static_cast<int>(rng.randrange(32));  // the bits that set the species: often rolled afresh
        body ^= 1U << bit;
    }
    const Mind start = mind ? *mind : inherit_mind(to_mind(decode_temperament(first)), to_mind(decode_temperament(second)), rng, mind_chance);
    const std::array<Dna, 2> parents = {first, second};
    const std::uint32_t spare = mind_word(rng.choice(parents));
    const std::uint32_t social_spare = social_word(rng.choice(parents));
    const std::uint32_t dot = write_mind(spare, start, rng);
    return {(static_cast<std::uint64_t>(body) << 32) | dot, write_social(social_spare, start, rng)};
}

std::string hex_word(std::uint32_t word) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%08X", word);
    return buffer;
}

int record_version(const nlohmann::json& record) {
    if (!record.contains("version")) return 1;
    const auto& version = record["version"];
    if (version.is_number_integer() && version.get<long long>() >= 1 && version.get<long long>() <= GENOME_VERSION) {
        return static_cast<int>(version.get<long long>());
    }
    throw std::invalid_argument("Genome version " + version.dump() + " is not one this Bitzkey knows (1 to 3).");
}

namespace {
std::uint32_t hex_part(const nlohmann::json& record, const char* part, int version) {
    if (!record.contains(part)) {
        throw std::invalid_argument("A version " + std::to_string(version) + " genome needs \"" + part + "\".");
    }
    const auto& value = record[part];
    std::string text = value.is_string() ? value.get<std::string>() : value.dump();
    std::string trimmed = text;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
    if (!trimmed.empty() && trimmed.front() == '#') trimmed.erase(trimmed.begin());
    const bool valid = trimmed.size() == 8 && std::all_of(trimmed.begin(), trimmed.end(),
                                                          [](unsigned char c) { return std::isxdigit(c) != 0; });
    if (!valid) throw std::invalid_argument(std::string("\"") + part + "\" must be eight hex digits, not \"" + text + "\".");
    return static_cast<std::uint32_t>(std::stoul(trimmed, nullptr, 16));
}
}  // namespace

Dna genome_from_record(const nlohmann::json& record) {
    const int version = record_version(record);
    const std::uint64_t dna = (static_cast<std::uint64_t>(hex_part(record, "ring", version)) << 32) | hex_part(record, "dot", version);
    if (version == 1) return convert_v1(dna);
    if (version == 2) return convert_v2(dna);
    return {dna, hex_part(record, "social", version)};
}

}  // namespace bitzkey
