#include "net/ProbeServer.hpp"

#include <fstream>
#include <stdexcept>

#include <httplib.h>

namespace bitzkey::net {

ProbeServer::ProbeServer(int port)
    : port_(port), probe_([this](const std::function<void(Simulation&)>& look) { run(look); }),
      server_(std::make_unique<httplib::Server>()) {
    probe_.install(*server_, "/probe");
    if (!server_->bind_to_port("127.0.0.1", port_)) throw std::runtime_error("the probe can't listen on port " + std::to_string(port_));
    thread_ = std::thread([this] { server_->listen_after_bind(); });
}

ProbeServer::~ProbeServer() {
    detach();
    server_->stop();
    if (thread_.joinable()) thread_.join();
}

void ProbeServer::attach(Simulation& world) {
    world.on_turn = [this](const Simulation& turned) { probe_.after_turn(turned); };
    std::lock_guard lock(mutex_);
    attached_ = true;
}

void ProbeServer::detach() {
    std::deque<std::shared_ptr<Task>> left;
    {
        std::lock_guard lock(mutex_);
        attached_ = false;
        left.swap(waiting_);
    }
    for (const auto& task : left) (*task)(nullptr);  // no world now: each fails
}

void ProbeServer::serve(Simulation& world) {
    std::deque<std::shared_ptr<Task>> due;
    {
        std::lock_guard lock(mutex_);
        due.swap(waiting_);
    }
    for (const auto& task : due) (*task)(&world);
}

void ProbeServer::run(const std::function<void(Simulation&)>& look) {
    auto task = std::make_shared<Task>([&look](Simulation* world) {
        if (!world) throw std::runtime_error("no world is running");
        look(*world);
    });
    std::future<void> done = task->get_future();
    {
        std::lock_guard lock(mutex_);
        if (!attached_) throw std::runtime_error("no world is running");
        waiting_.push_back(task);
    }
    done.get();  // served between turns, or failed as the world went; rethrows what the look threw
}

std::optional<int> ProbeServer::configured(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return std::nullopt;
    try {
        const nlohmann::json settings = nlohmann::json::parse(in);
        const int port = settings.value("port", 0);
        if (port > 0 && port < 65536) return port;
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

}  // namespace bitzkey::net
