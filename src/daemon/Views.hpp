#pragma once
// What the web interface is shown of the world: JSON for the panels, and a compact binary frame for
// the map, which can hold tens of thousands of Bitzkeys. Each is read under the world's lock.

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Simulation.hpp"
#include "daemon/World.hpp"

namespace bitzkey::daemon {

using Json = nlohmann::ordered_json;

// The header's and sidebar's figures: the turn, the season, the population, how it runs.
Json state(World& world);
// Why the run ended, as the window app says it; empty while it goes on.
std::string end_note(const Simulation& simulation);

// The map as it is this turn, little-endian:
//   "BZF1", u32 width, u32 height, u32 turn, u32 agent count, u32 trail cell count;
//   width x height f32 each row's temperature (one per row: height of them);
//   width x height bytes, 1 where a cell has grass;
//   per agent: u32 identifier, u16 x, u16 y, u8 dot r g b, u8 ring r g b, u8 species, u8 size,
//              u8 flags (1 alive), u8 opacity (0 to 255), u32 mind word, u32 body word;
//   per trail cell: u32 agent index, u16 x, u16 y.
std::string frame(const Simulation& simulation);

// A Bitzkey as the inspector shows it. `known` is false for a genome alone (in the catalog, or the
// designer): its state is not shown. `here` is the temperature where it stands.
Json inspect(const Bitzkey& agent, bool known = true, std::optional<double> here = std::nullopt);
// One genome's facts, as the designer lists them.
Json design(Dna dna);

// The statistics an exemplar card can show, and the ten cards' defaults.
Json exemplar_stats();
// SMARTEST, LARGEST, OLDEST, MOST CHILDREN, LATEST GENERATION; STRONGEST, FASTEST, BEST ARMOURED, MOST FERTILE, BEST FED
constexpr std::array<int, 10> DEFAULT_EXEMPLARS = {0, 21, 34, 36, 37, 23, 26, 24, 29, 38};
// The living animal that stands out most by each statistic chosen.
Json exemplars(const Simulation& simulation, const std::vector<int>& choices);

// The insights panel: behaviour, its extremes, life and temperament by species, the keenest groups.
Json insights(const Simulation& simulation);

// The setup panel's settings and the advanced rules, with their limits.
Json settings_form(const Simulation::Settings& current);
Json rules_form(const rules::Values& current);
// Settings from a submitted form: throws std::invalid_argument naming what is wrong.
Simulation::Settings read_settings(const Json& form, std::string* import_error = nullptr);
// A summary of the Bitzkeys in an import text, or why it can't be read.
Json import_check(const std::string& text);

}  // namespace bitzkey::daemon
