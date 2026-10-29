#pragma once
// Brains held for a world that turns elsewhere, or for the main service's own share of a pool: what a worker keeps,
// and how a main service tells it what to do with them.
//
// A main service sends each worker one batch at a time: brains to take on, lessons to learn, senses to value, brains
// to send back and brains to let go, done in that order. Each brain's handle is the main service's own, the same on
// every worker. A worker remembers the last RING senses it was sent for each brain, so a lesson from a recent memory,
// or senses seen before, need only name their place in that ring rather than come again.

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/Brain.hpp"
#include "core/GpuBrains.hpp"

namespace bitzkey::net {

constexpr std::uint32_t PROTOCOL = 3;  // a worker and its main service must speak the same (2: brains of any width; 3: calls)
constexpr int DEFAULT_WORKER_PORT = 50000;
constexpr int DEFAULT_CHECKPOINT_TURNS = 100;
constexpr int RING = 32;               // senses remembered for each brain: more than any brain replays in a turn

using FloatSenses = std::array<float, INPUT_SIZE>;

// What a computer has, and what of it is shared.
struct Resources {
    int threads = 1;               // cores to think on
    std::uint64_t memory = 0;      // bytes for brains
    std::vector<int> gpus;         // by their places in gpu::scan()'s list
};
struct Machine {
    int threads = 1;
    std::uint64_t memory = 0;
    std::vector<gpu::DeviceInfo> gpus;
};
Machine scan_machine();
// The bytes a brain of this shape takes on a worker, its traces and remembered senses with it, near enough.
std::uint64_t brain_bytes(int depth, int width);

struct Batch {
    struct Lesson {
        int handle;
        int action;
        bool traced;
        float target, rate, decay;
        int ring = -1;  // its senses' place in the ring, or -1 when they come with it
        FloatSenses senses{};
    };
    struct Ask {
        int handle;
        int ring = -1;
        FloatSenses senses{};
    };
    std::uint64_t session = 0;
    std::vector<std::pair<int, Brain>> attaches;
    std::vector<Lesson> lessons;
    std::vector<Ask> values;
    std::vector<int> downloads;
    std::vector<int> detaches;
    bool empty() const { return attaches.empty() && lessons.empty() && values.empty() && downloads.empty() && detaches.empty(); }
};

struct Answer {
    bool known = true;    // false: the worker doesn't know the session, having restarted since
    bool leaving = false; // the worker is stopping, and wants its brains taken back
    std::vector<std::array<float, OUTPUT_COUNT>> values;
    std::vector<Brain> downloads;
    std::uint32_t held = 0;
};

std::string encode(const Batch& batch);
Batch decode_batch(std::string_view bytes);
std::string encode(const Answer& answer);
Answer decode_answer(std::string_view bytes);

// The brains on the CPU, in doubles, on as many threads as it is given: the GPUs' engine, without GPUs.
class CpuEngine final : public gpu::BrainEngine {
public:
    explicit CpuEngine(int threads);
    void set_threads(int threads) { threads_ = std::max(1, threads); }
    const std::vector<gpu::DeviceInfo>& devices() const override { return none_; }
    Handle attach(const Brain& brain) override;
    void detach(Handle brain) override;
    void download(std::span<const Handle> brains, std::span<Brain* const> into) override;
    void values(std::span<const Handle> brains, std::span<const Senses> senses, std::span<Values> out) override;
    void learn(std::span<const Lesson> lessons) override;
    std::size_t attached() const override { return attached_; }

private:
    int threads_;
    std::vector<std::optional<Brain>> brains_;
    std::vector<Handle> free_;
    std::size_t attached_ = 0;
    std::vector<gpu::DeviceInfo> none_;
};

// Run `work(index)` for every index below `count`, on up to `threads` threads at once.
void parallel_for(std::size_t count, int threads, const std::function<void(std::size_t)>& work);

// Brains held by their main service's handles, on the GPUs shared or else the CPU, with the senses each was last sent.
class Thinker {
public:
    explicit Thinker(const Resources& resources);
    // Take on the shared resources: brains held move across to the GPUs chosen, if those changed.
    void share(const Resources& resources);
    Answer apply(const Batch& batch);  // throws std::runtime_error when the GPUs fail
    void clear();                      // let every brain go
    std::size_t held() const { return held_.size(); }
    const Resources& resources() const { return resources_; }
    std::string description() const;  // where it thinks
    bool on_gpu() const { return on_gpu_; }
    // Brain-turns a second: how many brains it values and teaches (with a few replayed memories) each second.
    double speed() const { return speed_; }
    void measure();  // measure its speed afresh, with brains of its own, while it holds none

private:
    struct Held {
        gpu::BrainEngine::Handle inner;
        int depth, width;
        std::array<FloatSenses, RING> ring{};
        int next = 0;
    };
    std::unique_ptr<gpu::BrainEngine> make_engine(const Resources& resources, bool& gpu) const;
    const FloatSenses& senses_of(Held& held, int ring, const FloatSenses& given);

    Resources resources_;
    std::unique_ptr<gpu::BrainEngine> engine_;
    bool on_gpu_ = false;
    std::unordered_map<int, Held> held_;
    double speed_ = 0.0;
};

}  // namespace bitzkey::net
