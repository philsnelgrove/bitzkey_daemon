#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/Brain.hpp"

namespace bitzkey::gpu {

struct DeviceInfo {
    std::string name;
    bool discrete = false;
    std::uint64_t memory = 0;  // bytes of its own memory
};

// Brains kept on the GPUs, through Vulkan compute. Their weights and eligibility traces live in the
// GPUs' memory, in pools of one depth, shared out among every GPU found; only each turn's senses go
// up, and only the values come back. A brain's copy on the CPU falls behind as it learns, until it
// is downloaded. On the GPU a brain works in floats, not the CPU's doubles, so its results are near
// the CPU's, not the same.
//
// Vulkan is loaded when the engine is made, so the program runs without it: create() then gives
// none, and the brains stay on the CPU. Discrete GPUs are used when there are any, else integrated
// ones; BITZKEY_GPU=off turns the GPUs off, and BITZKEY_GPU=all uses integrated ones beside discrete.
// Whether this computer has a GPU the brains can think on, found once and remembered; `why` says why
// not when it hasn't.
bool available(std::string* why = nullptr);
// Every GPU with Vulkan compute, discrete or integrated, in the order Vulkan lists them; none without Vulkan.
std::vector<DeviceInfo> scan();

class BrainEngine {
public:
    using Handle = int;
    struct Lesson {
        Handle brain;
        const Senses* senses;
        int action;
        double target;
        double rate;
        bool traced;   // TD(lambda), learning from its own choice, as Brain::learn_traced; else as Brain::learn
        double decay;  // for a traced lesson: the discount times lambda
    };

    // None when there is no Vulkan, no GPU, or they are turned off; `why` then says which.
    static std::unique_ptr<BrainEngine> create(std::string* why = nullptr);
    // On these GPUs alone, by their places in scan()'s list, whatever BITZKEY_GPU says.
    static std::unique_ptr<BrainEngine> create_on(const std::vector<int>& chosen, std::string* why = nullptr);
    virtual ~BrainEngine() = default;

    virtual const std::vector<DeviceInfo>& devices() const = 0;
    // Put a brain on a GPU, its traces too; the handle names it there until detached.
    virtual Handle attach(const Brain& brain) = 0;
    virtual void detach(Handle brain) = 0;
    // Bring brains' weights back into their CPU copies, of the same depth. Their traces stay on the GPU:
    // they belong to one life, and are neither inherited nor exported.
    virtual void download(std::span<const Handle> brains, std::span<Brain* const> into) = 0;
    // Value every action for each brain asked, from its senses: all at once.
    virtual void values(std::span<const Handle> brains, std::span<const Senses> senses, std::span<Values> out) = 0;
    // Learn every lesson, each brain its own lessons in the order given: all brains at once.
    virtual void learn(std::span<const Lesson> lessons) = 0;
    virtual std::size_t attached() const = 0;  // brains on the GPUs now
};

}  // namespace bitzkey::gpu
