#pragma once
// Exporting and importing Bitzkeys as JSON, so they can move between simulations, and between
// this and the Python version.
//
// A Bitzkey is written as its two colour words in hex; an animal adds its current temperament
// and its brain:
// {"version": 2, "ring": "7421FF41", "dot": "637E4B01", "mind": {"aggression": 3.412, ...},
//  "brain": {"senses": [...], "actions": [...], ...}}
// Several Bitzkeys form a JSON array, one per line.

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "core/Brain.hpp"
#include "core/Genome.hpp"

namespace bitzkey {

struct Bitzkey;

constexpr int MIND_DIGITS = 3;  // decimal places kept for exported temperament

// Saved traits only; any missing start at the genes.
struct PartialMind {
    std::array<std::optional<double>, TEMPERAMENT_COUNT> traits;
    bool operator==(const PartialMind&) const = default;
};

// A genome read from an export, with the temperament and trained brain saved with it.
struct Imported {
    Dna dna = 0;
    std::shared_ptr<const Brain> brain;
    std::optional<PartialMind> mind;
};

// Python's json.dumps layout: ", " and ": " separators, Python float repr, keys in order.
std::string dumps(const nlohmann::ordered_json& value);

nlohmann::ordered_json bitzkey_record(const Bitzkey& agent);
std::string export_bitzkeys(std::span<const Bitzkey* const> agents);
// One record as a JSON object; several as a JSON array, one record per line.
std::string format_records(std::span<const nlohmann::ordered_json> records);

// Read every Bitzkey in the text: exported objects, arrays of them, or several pasted one after
// another. Throws std::invalid_argument, naming the problem, if any part cannot be read.
std::vector<Imported> import_bitzkeys(std::string_view text);

}  // namespace bitzkey
