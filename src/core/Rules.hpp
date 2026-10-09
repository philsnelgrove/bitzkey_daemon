#pragma once
// The simulation's tunable rules: every constant that shapes how the world plays out, so the
// advanced panel can change them before a world starts.
//
// Each rule names a constant, or one entry of an array or table, by the same key as the Python
// version (tools/Rules.py), such as FORAGE_RADIUS, LIFESPAN_YEARS.0, GRAZING.grazer or
// EXPERIENCES.kill.aggression, with the same limits and steps. Rules are program-wide: `apply` sets
// them all, and a world plays by whatever was set when it runs. Settings the configuration panel
// already covers (world size, population, grass density, the population cap and mutation rates)
// are not repeated here, nor are numbers that fix the genome's or the brain's layout, how starting
// groups are sized, or how much history is kept.

#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace bitzkey::rules {

struct Rule {
    std::string key, group, label, hint;
    double minimum, maximum, step;
    bool integer;  // whole numbers only: the rule counts cells, turns or animals
    std::variant<int*, double*> target;

    double clamp(double value) const;
    double get() const;
    void set(double value) const;
};

using Values = std::map<std::string, double, std::less<>>;

const std::vector<Rule>& all();              // by group, in the panel's order
const Rule* find(std::string_view key);      // none for an unknown key
// A warning shown with a rule, in full when it is chosen: a cost it runs up beyond the simulation
// itself. Empty for most.
std::string_view warning(std::string_view key);
// Why a rule can't be changed on this computer, or empty when it can: Think on the GPU, without a GPU.
std::string locked(std::string_view key);
const Values& defaults();                    // as the program started
Values current();
// Why a set of values (every rule's) can't be applied, if it can't.
std::vector<std::string> problems(const Values& values);
// Set every rule: those given to their (clamped) values, the rest to their defaults. Unknown keys
// are ignored.
void apply(const Values& values);
Values changed(const Values& values);
// A value as the panel shows it: up to six significant digits, no trailing zeros.
std::string shown(const Rule& rule, double value);

}  // namespace bitzkey::rules
