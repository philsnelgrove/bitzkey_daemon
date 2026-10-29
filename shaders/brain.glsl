// Shared by the brain shaders: the sizes of a brain, and where each part of one sits in a pool.
//
// A pool holds brains of one shape, `push.depth` layers of `push.width` neurons, each `slot_floats` floats
// long, laid out as:
//   the first hidden layer's weights   INPUTS rows of width, one row an input's weight to each neuron
//   each deeper hidden layer's weights width rows of width, likewise
//   every hidden layer's biases        width each
//   the output weights                 width rows of ACTIONS, one row a neuron's weight to each action
//   the output biases                  ACTIONS
// Stored input by input, neighbouring threads, each a neuron, read neighbouring weights.
// Its eligibility traces sit in a second buffer laid out the same way.

const uint INPUTS = 142u;
const uint MAX_HIDDEN = 53u;  // MAX_WIDTH
const uint ACTIONS = 47u;  // OUTPUT_COUNT: the actions, then silence and the calls, then the memory's gates and values
const uint MEMORY_NEURONS = 8u;
const uint MEMORY_SENSE = 74u;  // SenseMemory: the first sense of what the memory held
const uint MEMORY_GATES = 31u;  // the first gate's output, then each neuron's, then the values to write
const float VALUE_SCALE = 0.01;  // a brain holds its values in hundredths (Brain.hpp)
const uint MAX_DEPTH = 4u;
const uint THREADS = 64u;

layout(push_constant) uniform Push {
    uint depth;        // hidden layers in this pool's brains
    uint count;        // requests or brains to work, from `first`
    uint slot_floats;  // floats in one brain
    uint first;        // the first request or brain of this dispatch
    uint width;        // neurons in each of their hidden layers
} push;

uint reads_of(uint layer) { return layer == 0u ? INPUTS : push.width; }

uint weights_of(uint base, uint layer) {
    return layer == 0u ? base : base + push.width * INPUTS + (layer - 1u) * push.width * push.width;
}

uint biases_of(uint base) { return base + push.width * INPUTS + (push.depth - 1u) * push.width * push.width; }
uint outputs_of(uint base) { return biases_of(base) + push.depth * push.width; }
uint output_biases_of(uint base) { return outputs_of(base) + ACTIONS * push.width; }
