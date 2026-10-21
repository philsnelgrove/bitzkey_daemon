#pragma once
// Saving a whole world to a file and loading it back, to carry on exactly as it would have.
//
// A world file is the same for the Python and C++ versions (tools/WorldFile.py), so a world saved by
// either loads in both. Format 2, written now:
//
//     "BITZKEY WORLD 2\n" (16 bytes), the JSON part's length (8 bytes, little-endian), the JSON part (UTF-8),
//     the count of numbers (8 bytes), the numbers part: 64-bit little-endian floats,
//     the count of whole numbers (8 bytes), the whole-numbers part: 32-bit little-endian integers.
//
// The JSON part holds the world's settings, its rules, and everything in it: the random generator's
// state, the grass, the seeds waiting to sprout, every Bitzkey and every field of it, and the insights.
// What would make it enormous goes in the other parts, and the JSON part names where:
//
// - a brain is {"at": its first number, "depth": hidden layers, "width": neurons in each, "traced": whether
//   learning traces follow}. Its numbers are each hidden layer's weights (width rows, each reading the senses
//   or the layer before) and biases, then the output weights (a row of width for each action) and biases;
//   then, if traced, its traces, laid out the same. A brain saved without a width, before brains grew wider
//   with intellect, is OLD_WIDTH wide, and loads resized to its owner's shape, without its traces.
// - a set of senses (its last, or a memory's) is the position of INPUT_SIZE numbers.
// - the census's snapshots are [turn, the first whole number, how many]: the identifiers alive then.
// - "lineage" {"at", "count"}: the genome of every Bitzkey that ever lived, by identifier, three whole numbers
//   each (the social background word, the ring, the dot, as unsigned words).
// - "death_records" {"at", "count"}: every death, seven whole numbers each: the dead's identifier, its cause
//   (an index into "death_strings"), its species (an index into SPECIES), the turn, the killer's identifier,
//   its species and what it was doing (an index into "death_strings"); the last three -1 for none.
//
// Elsewhere a genome is 24 hex digits: the social background word, then the ring, then the dot. Format 1 had
// no counts and no whole-numbers part, the numbers running to the file's end, and kept the census, lineage
// and deaths in the JSON part; it still loads.

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>

namespace bitzkey {

class Simulation;

inline constexpr std::string_view WORLD_MAGIC = "BITZKEY WORLD 2\n";
inline constexpr std::string_view WORLD_MAGIC_1 = "BITZKEY WORLD 1\n";  // format 1's, still read
inline constexpr int WORLD_FORMAT = 2;
inline constexpr std::string_view WORLD_SUFFIX = ".bzworld";

struct WorldFile {
    // The whole world, as a world file holds it. A world thinking on the GPUs brings its brains back first.
    static std::string bytes(Simulation& simulation);
    // The same, written as it goes, so a long-lived world's history needn't be held twice over.
    static void write(Simulation& simulation, std::ostream& out);
    // The world a world file holds, ready to carry on as it would have; its rules become the rules, all but
    // those saying how this computer runs it. Throws std::runtime_error, naming the problem, if it can't be read.
    static std::unique_ptr<Simulation> from_bytes(std::string_view data);
    static void save(Simulation& simulation, const std::filesystem::path& path);  // throws std::runtime_error
    static std::unique_ptr<Simulation> load(const std::filesystem::path& path);   // throws std::runtime_error
};

}  // namespace bitzkey
