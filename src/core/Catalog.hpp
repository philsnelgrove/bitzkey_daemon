#pragma once
// The catalog of favourite Bitzkeys, kept between runs in catalog.json beside exports/.
//
// Each entry is an export record (its genome, its temperament and its trained brain, as Exchange
// writes them), so the catalog file can itself be imported, plus a "catalog" note of how it was when
// catalogued, for the inspector to show: its identifier, the turn, its hunger, health, age,
// generation, children, last action and the chances it last gave its options.

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Exchange.hpp"
#include "core/Simulation.hpp"

namespace bitzkey::catalog {

constexpr int STATE_DIGITS = 3;  // decimal places kept for hunger, health and chances

using Entry = nlohmann::ordered_json;

Entry entry_for(const Bitzkey& agent, int turn);  // an animal as it is now, its brain among it

// The catalogued entries, oldest first; none if there is no catalog yet. Throws
// std::invalid_argument if the file can't be read as a catalog.
std::vector<Entry> load(const std::filesystem::path& path);
void save(const std::vector<Entry>& entries, const std::filesystem::path& path);
// Add entries, leaving out any already in it exactly. Returns how many were added.
int add(const std::vector<Entry>& entries, const std::filesystem::path& path);
void remove(std::size_t index, const std::filesystem::path& path);

Imported imported(const Entry& entry);  // the Bitzkey it holds, brain and temperament, ready to import
Bitzkey inspected(const Entry& entry);  // rebuilt as it was when catalogued, for the inspector
std::string describe(const Entry& entry);  // a line naming it in the catalog's list

}  // namespace bitzkey::catalog
