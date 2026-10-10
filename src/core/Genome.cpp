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
}  // namespace

Traits decode_mind(Dna dna) {
    Traits traits{};
    for (int index = 0; index < MIND_TRAIT_COUNT; ++index) traits[index] = trait_at(mind_word(dna), index);
    return traits;
}

Body decode_body(Dna dna) {
    Body body{};
    for (int index = 0; index < BODY_TRAIT_COUNT; ++index) body[index] = trait_at(body_word(dna), index);
    body[SpeciesTrait] %= 4;
    return body;
}

Dna encode_dna(Species species, std::initializer_list<std::pair<std::string_view, int>> traits) {
    Dna dna = static_cast<Dna>(species) << 32;
    int temperature = V1_TEMPERATURE;
    for (const auto& [name, value] : traits) {
        if (name == "temperature") {
            if (value < 0 || value > 3) throw std::invalid_argument("Temperatures run from 0 to 3: " + std::to_string(value) + ".");
            temperature = value;
            continue;
        }
        if (name == "heat_tolerance" || name == "cold_tolerance") {
            if (value) dna |= name == "heat_tolerance" ? HEAT_TOLERANCE : COLD_TOLERANCE;
            continue;
        }
        if (value < 0 || value > TRAIT_MAX) {
            throw std::invalid_argument("Traits run from 0 to 7: " + std::string(name) + " " + std::to_string(value) + ".");
        }
        bool known = false;
        for (int index = 0; index < MIND_TRAIT_COUNT; ++index) {
            if (MIND_TRAITS[index] == name) {
                dna |= static_cast<Dna>(value) << (TRAIT_BITS * index);
                known = true;
            }
        }
        for (int index = 1; index < BODY_TRAIT_COUNT; ++index) {
            if (BODY_TRAITS[index] == name) {
                dna |= static_cast<Dna>(value) << (32 + TRAIT_BITS * index);
                known = true;
            }
        }
        if (!known) throw std::invalid_argument("Unknown traits: " + std::string(name) + ".");
    }
    return dna | static_cast<Dna>(temperature) << TEMPERATURE_SHIFT;
}

Dna convert_v1(Dna dna) {
    // Version 1: the first eight traits of each word, four bits each. The traits it lacked start
    // at defaults: empathy 0, tolerance 3, litter 0, carrion 0, a medium home temperature and
    // neither tolerance.
    const auto scaled = [](int value) { return static_cast<int>(py_round(static_cast<double>(value) * TRAIT_MAX / V1_MAX)); };
    Dna result = static_cast<Dna>(trait_at(body_word(dna), 0, 4) % 4) << 32 | static_cast<Dna>(V1_TEMPERATURE) << TEMPERATURE_SHIFT;
    for (int index = 0; index < 8; ++index) {
        result |= static_cast<Dna>(scaled(trait_at(mind_word(dna), index, 4))) << (TRAIT_BITS * index);
    }
    result |= static_cast<Dna>(3) << (TRAIT_BITS * Tolerance);
    for (int index = 1; index < 8; ++index) {
        result |= static_cast<Dna>(scaled(trait_at(body_word(dna), index, 4))) << (32 + TRAIT_BITS * index);
    }
    return result;
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
    Dna dna = rng.getrandbits(64);
    if (species) dna = (dna & ~(static_cast<Dna>(0x3) << 32)) | (static_cast<Dna>(*species % 4) << 32);
    return dna;
}

Mind to_mind(const Traits& traits) {
    Mind mind{};
    for (int index = 0; index < MIND_TRAIT_COUNT; ++index) mind[index] = traits[index];
    return mind;
}

Mind inherit_mind(const Mind& first, const Mind& second, PyRandom& rng, double chance) {
    Mind mind{};
    for (int index = 0; index < MIND_TRAIT_COUNT; ++index) mind[index] = (first[index] + second[index]) / 2;
    static constexpr std::array<int, 2> steps = {-1, 1};
    for (int roll = 0; roll < MIND_MUTATION_ROLLS; ++roll) {
        if (rng.random() >= chance) continue;
        const int trait = static_cast<int>(rng.randbelow(MIND_TRAIT_COUNT));
        int step = rng.choice(steps);
        if (!(0 <= mind[trait] + step && mind[trait] + step <= TRAIT_MAX)) step = -step;
        mind[trait] = std::max(0.0, std::min(static_cast<double>(TRAIT_MAX), mind[trait] + step));
    }
    return mind;
}

std::uint32_t write_mind(std::uint32_t spare, const Mind& mind, PyRandom& rng) {
    std::uint32_t word = spare & SPARE_BITS;
    for (int index = 0; index < MIND_TRAIT_COUNT; ++index) {
        const double whole = std::floor(mind[index]);
        const double fraction = mind[index] - whole;
        int value = static_cast<int>(whole) + ((fraction != 0.0 && rng.random() < fraction) ? 1 : 0);
        value = std::min(TRAIT_MAX, value);
        word |= static_cast<std::uint32_t>(value) << (TRAIT_BITS * index);
    }
    return word;
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
    const Mind start = mind ? *mind : inherit_mind(to_mind(decode_mind(first)), to_mind(decode_mind(second)), rng, mind_chance);
    const std::array<Dna, 2> parents = {first, second};
    const std::uint32_t spare = mind_word(rng.choice(parents));
    return (static_cast<Dna>(body) << 32) | write_mind(spare, start, rng);
}

std::string hex_word(std::uint32_t word) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%08X", word);
    return buffer;
}

int record_version(const nlohmann::json& record) {
    if (!record.contains("version")) return 1;
    const auto& version = record["version"];
    if (version.is_number_integer() && (version.get<long long>() == 1 || version.get<long long>() == GENOME_VERSION)) {
        return static_cast<int>(version.get<long long>());
    }
    throw std::invalid_argument("Genome version " + version.dump() + " is not one this Bitzkey knows (1 or 2).");
}

namespace {
std::uint32_t hex_part(const nlohmann::json& record, const char* part) {
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
    const Dna dna = (static_cast<Dna>(hex_part(record, "ring")) << 32) | hex_part(record, "dot");
    return record_version(record) == 1 ? convert_v1(dna) : dna;
}

}  // namespace bitzkey
