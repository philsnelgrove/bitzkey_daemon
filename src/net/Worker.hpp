#pragma once
// Worker mode: this daemon runs no world of its own, but holds brains for a main service elsewhere and thinks for
// them, on the cores, memory and GPUs it is told to share. It listens for its main service on a port of its own
// (DEFAULT_WORKER_PORT unless told otherwise); the main service finds it there. Its web page shows only that it is a
// worker, whom for, and the main service's message.
//
// Stopping, it asks its main service to take its brains back, and waits for that a while: whether worker mode is
// turned off, or the daemon is stopped or restarted. Worker mode is kept between runs.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "net/Brains.hpp"
#include <nlohmann/json.hpp>

namespace httplib {
class Server;
}

namespace bitzkey::net {

using Json = nlohmann::ordered_json;

class Worker {
public:
    explicit Worker(std::filesystem::path file);  // its settings, kept between runs
    ~Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    bool on() const { return on_; }
    // Become a worker, listening on `port`, sharing these. Throws std::invalid_argument if it can't.
    void start(int port, const Resources& shared);
    void resume();  // as it was when the daemon last stopped, if a worker then
    void share(const Resources& shared);
    // Listen on this port from now on. The main service is told, by the port it reaches this worker on, and follows;
    // the old port is let go once it has. Throws std::invalid_argument if the port can't be had.
    void move_to(int port);
    // Stop being a worker: its brains handed back to the main service first, for up to `wait`. With `forget`, worker
    // mode is turned off for good; without, the daemon is stopping, and is a worker again when it starts.
    void stop(bool forget, std::chrono::seconds wait = std::chrono::seconds(30));
    void stop_soon();  // stop and forget, on a thread of its own: for the web page, which can't wait so long
    Json status() const;
    static Json machine();  // what this computer has to share

    // Told the ports to have open, besides worker-ports being written, whenever the user's own doing changes them:
    // becoming a worker, moving to another port (the old one with it, still open), and stopping for good (none).
    // The desktop app opens them in Windows' firewall, which asks for approval each time; set before resume().
    std::function<void(const std::vector<int>&)> on_ports;

private:
    void begin(int port, const Resources& shared, bool tell);  // start, telling on_ports or not
    void serve(httplib::Server& server, int port);
    Json hello() const;
    void save() const;
    // The ports it listens on, to worker-ports beside its settings, for bitzkey-worker-ports.sh to open in the firewall.
    void write_ports(bool tell = true) const;  // and, unless not to `tell`, on_ports
    void close_old();      // stop listening on the port moved from
    void followed(int port);  // its main service reached it on this port: the old one, if any, is let go
    void noted();  // what is shown of the brains held, brought up to date: under both locks

    std::filesystem::path file_;
    std::mutex thinking_;       // the brains: held while they think; taken before mutex_ when both are
    mutable std::mutex mutex_;  // the rest
    std::size_t held_ = 0;
    double speed_ = 0.0;
    std::string description_;
    bool on_gpu_ = false;
    std::atomic<bool> on_{false};
    int port_ = 0;
    Resources shared_;
    std::unique_ptr<Thinker> thinker_;
    std::unique_ptr<httplib::Server> server_;
    std::thread serving_;
    std::mutex servers_;  // the old port's server, while the main service follows to the new
    std::unique_ptr<httplib::Server> old_server_;
    std::thread old_serving_;
    std::atomic<int> old_port_{0};
    std::thread closer_;
    std::thread stopping_thread_;
    // The main service it works for.
    std::string main_address_;
    std::string message_;
    std::uint64_t session_ = 0;
    std::chrono::steady_clock::time_point contact_{};
    bool leaving_ = false;
    bool released_ = false;
    std::atomic<bool> stopping_{false};
};

}  // namespace bitzkey::net
