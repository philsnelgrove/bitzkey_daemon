// Shared by the brain shaders: the sizes of a brain, and where each part of one sits in a pool.
//
// A pool holds brains of one depth, each `slot_floats` floats long, laid out as:
//   the first hidden layer's weights   INPUTS rows of HIDDEN, one row an input's weight to each neuron
//   each deeper hidden layer's weights HIDDEN rows of HIDDEN, likewise
//   every hidden layer's biases        HIDDEN each
//   the output weights                 HIDDEN rows of ACTIONS, one row a neuron's weight to each action
//   the output biases                  ACTIONS
// Stored input by input, neighbouring threads, each a neuron, read neighbouring weights.
// Its eligibility traces sit in a second buffer laid out the same way.

const uint INPUTS = 69u;
const uint HIDDEN = 34u;
const uint ACTIONS = 22u;
const uint MAX_DEPTH = 14u;
const uint THREADS = 64u;

layout(push_constant) uniform Push {
    uint depth;        // hidden layers in this pool's brains
    uint count;        // requests or brains to work, from `first`
    uint slot_floats;  // floats in one brain
    uint first;        // the first request or brain of this dispatch
} push;

uint reads_of(uint layer) { return layer == 0u ? INPUTS : HIDDEN; }

uint weights_of(uint base, uint layer) {
    return layer == 0u ? base : base + HIDDEN * INPUTS + (layer - 1u) * HIDDEN * HIDDEN;
}

uint biases_of(uint base) { return base + HIDDEN * INPUTS + (push.depth - 1u) * HIDDEN * HIDDEN; }
uint outputs_of(uint base) { return biases_of(base) + push.depth * HIDDEN; }
uint output_biases_of(uint base) { return outputs_of(base) + ACTIONS * HIDDEN; }
