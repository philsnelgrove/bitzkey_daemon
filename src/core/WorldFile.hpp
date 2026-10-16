#pragma once
// Saving a whole world to a file and loading it back, to carry on exactly as it would have.
//
// A world file is the same for the Python and C++ versions (tools/WorldFile.py), so a world saved by
// either loads in both:
//
//     WORLD_MAGIC (16 bytes), the JSON part's length (8 bytes, little-endian), the JSON part (UTF-8),
//     then the numbers part: 64-bit little-endian floats.
//
// The JSON part holds the world's settings, its rules, and everything in it: the random generator's
// state, the grass, the seeds waiting to sprout, every Bitzkey and every field of it, the insights, the
// census, the genomes of all who ever lived and the record of every death. Brains and senses go in the
// numbers part, and the JSON part names where:
//
// - a brain is {"at": its first number, "depth": hidden layers, "traced": whether learning traces follow}.
//   Its numbers are each hidden layer's weights (HIDDEN_SIZE rows, each reading the senses or the layer
//   before) and biases, then the output weights (a row of HIDDEN_SIZE for each action) and biases; then,
//   if traced, its traces, laid out the same.
// - a set of senses (its last, or a memory's) is the position of INPUT_SIZE numbers.
//
// A genome is 24 hex digits: the social background word, then the ring, then the dot.

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace bitzkey {

class Simulation;

inline constexpr std::string_view WORLD_MAGIC = "BITZKEY WORLD 1\n";
inline constexpr int WORLD_FORMAT = 1;
inline constexpr std::string_view WORLD_SUFFIX = ".bzworld";

struct WorldFile {
    // The whole world, as a world file holds it. A world thinking on the GPUs brings its brains back first.
    static std::string bytes(Simulation& simulation);
    // The world a world file holds, ready to carry on as it would have; its rules become the rules, all but
    // those saying how this computer runs it. Throws std::runtime_error, naming the problem, if it can't be read.
    static std::unique_ptr<Simulation> from_bytes(std::string_view data);
    static void save(Simulation& simulation, const std::filesystem::path& path);  // throws std::runtime_error
    static std::unique_ptr<Simulation> load(const std::filesystem::path& path);   // throws std::runtime_error
};

}  // namespace bitzkey
