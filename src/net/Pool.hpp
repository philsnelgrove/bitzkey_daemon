#pragma once
// The main service's pool of workers: other daemons, in worker mode, that hold some of the world's brains and do
// their thinking and learning. The main service holds a share itself. Each new brain goes to the member with most
// room for it by its speed; every so many turns every brain is brought back (a checkpoint), and the shares are
// evened out. A worker that stops answering has its brains given to the others, as they were at the last
// checkpoint; a worker that stops gracefully hands its brains back first.
//
// Everything but the polling runs on the world's thread, under its lock: the world's turns call it through the
// engine it gives the simulation, and the web interface's changes come in under the same lock.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/GpuBrains.hpp"
#include "net/Brains.hpp"
#include <nlohmann/json.hpp>

namespace httplib {
class Client;
}

namespace bitzkey {
class Simulation;
}

namespace bitzkey::net {

using Json = nlohmann::ordered_json;

class Pool {
public:
    explicit Pool(std::filesystem::path file);  // the list of workers, kept between runs
    ~Pool();
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    bool active() const { return !members_.empty(); }  // any worker is listed
    bool bound() const { return bound_ != nullptr; }   // the world thinks through the pool
    // The engine a world thinks through, its brains shared among the pool: one world at a time.
    std::unique_ptr<gpu::BrainEngine> engine(Simulation& simulation);
    // The world thinks through the pool while it lists workers, and alone (on its GPUs, if the rules say to think there)
    // when it lists none: after the list changes, or the world does.
    void adopt(Simulation& simulation);
    void tend(Simulation& simulation);  // between turns: workers come and go, checkpoints and evening out
    std::string status() const;          // where the world thinks, for the header

    Json list() const;
    void add(const std::string& host, int port, const std::string& message);
    void remove(const std::string& host, int port);  // its brains handed back first
    void set_checkpoint(int turns);
    int checkpoint() const { return checkpoint_turns_; }

    // For the engine.
    int attach(const Brain& brain);
    void detach(int handle);
    void learn(std::span<const gpu::BrainEngine::Lesson> lessons);
    void values(std::span<const int> handles, std::span<const Senses> senses, std::span<Values> out);
    void download(std::span<const int> handles, std::span<Brain* const> into);
    std::size_t attached() const { return owners_.size(); }
    const std::vector<gpu::DeviceInfo>& devices() const;
    void unbind(int binding);  // that world is gone: every brain let go, unless another world has bound since

private:
    enum class State { Connecting, Ready, Lost, Leaving, Away, Refused };
    struct Mirror {  // the senses a worker remembers for a brain, by their hashes
        std::array<std::uint64_t, RING> hashes{};
        int next = 0;
    };
    struct RawLesson {
        int handle, action;
        bool traced;
        double target, rate, decay;
        Senses senses;
    };
    struct Member {
        bool local = false;
        std::string host;
        int port = DEFAULT_WORKER_PORT;
        std::string message;
        // Shared with the polling thread, under mutex_:
        State state = State::Connecting;
        std::string error;
        double ping_ms = -1;
        double speed = 0;
        int threads = 0;
        std::uint64_t memory = 0;  // bytes it shares for brains; 0: no limit
        std::vector<std::string> gpus;
        std::string description;
        std::uint64_t session = 0;
        int failures = 0;
        bool fresh = false;  // just joined: the shares are to be evened out
        bool moved = false;  // it listens on another port now, and is reached there
        std::chrono::steady_clock::time_point retry{};
        // The world's thread alone:
        std::unique_ptr<httplib::Client> client;
        std::unordered_map<int, Mirror> mirrors;
        std::vector<std::pair<int, Brain>> attaches;
        std::vector<RawLesson> lessons;
        std::vector<int> detaches;
        std::size_t count = 0;
        std::uint64_t bytes = 0;
        std::string name() const;
    };
    struct Owner {
        Member* member;
        int depth;
    };

    void load();
    void save() const;
    void poll();  // the polling thread
    void poll_one(const std::shared_ptr<Member>& member);
    void start_polling();
    Member* choose(int depth, const Member* except = nullptr);
    void place(int handle, Brain brain, const Member* except = nullptr);
    // Send every member its waiting work, with these values and brains asked of it; members that fail have their
    // brains given to others, and are asked again.
    void flush(std::span<const int> asks, std::span<const Senses> senses, std::span<Values> out, std::span<const int> downloads,
               std::span<Brain* const> into);
    Batch batch_for(Member& member, const std::vector<std::size_t>& asks, std::span<const int> handles, std::span<const Senses> senses,
                    const std::vector<std::size_t>& downloads, std::span<const int> download_handles);
    Answer send(Member& member, const Batch& batch);
    void fail(Member& member, const std::string& why);
    void evacuate(Member& member);  // its brains brought back and given to others
    void release(Member& member);
    void rebalance();
    void make_local();

    std::filesystem::path file_;
    int checkpoint_turns_ = DEFAULT_CHECKPOINT_TURNS;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Member>> members_;  // the workers
    std::unique_ptr<Member> local_;                 // the main service itself
    std::unique_ptr<Thinker> thinker_;              // its share of the brains
    std::unordered_map<int, Owner> owners_;
    int next_handle_ = 0;
    Simulation* bound_ = nullptr;
    int binding_ = 0;  // counts the worlds bound, so a world gone since doesn't let go of another's brains
    int last_checkpoint_ = 0;
    bool even_out_ = false;
    std::atomic<bool> stopping_{false};
    std::thread poller_;
};

}  // namespace bitzkey::net
