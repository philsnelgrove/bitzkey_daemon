#pragma once
// The probe served on a port of its own, for a program whose world turns on its own thread without a lock, as the
// desktop app's does: each look at the world waits for that thread, which answers the looks waiting between turns.
// It listens on this computer only (127.0.0.1).

#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "net/Probe.hpp"

namespace httplib {
class Server;
}

namespace bitzkey::net {

class ProbeServer {
public:
    // Listen on 127.0.0.1:`port`. Throws std::runtime_error if the port can't be had.
    explicit ProbeServer(int port);
    ~ProbeServer();
    ProbeServer(const ProbeServer&) = delete;
    ProbeServer& operator=(const ProbeServer&) = delete;

    int port() const { return port_; }
    Probe& probe() { return probe_; }

    // On the world's thread. While a world is attached, looks wait for it to serve them; without one, they fail at once.
    void attach(Simulation& world);  // watched tracks follow this world's turns
    void detach();
    void serve(Simulation& world);   // between turns: answer the looks waiting

    // The port asked for in `file` ({"port": N}), if any: the probe is off without one.
    static std::optional<int> configured(const std::filesystem::path& file);

private:
    using Task = std::packaged_task<void(Simulation*)>;
    void run(const std::function<void(Simulation&)>& look);

    int port_;
    Probe probe_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    std::mutex mutex_;
    bool attached_ = false;
    std::deque<std::shared_ptr<Task>> waiting_;
};

}  // namespace bitzkey::net
