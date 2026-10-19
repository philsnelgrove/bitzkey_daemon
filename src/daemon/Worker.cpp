#include "daemon/Worker.hpp"

#include <cstdio>
#include <fstream>

#include <httplib.h>

namespace bitzkey::daemon {

namespace {

constexpr auto CONTACT_LAPSES = std::chrono::seconds(15);  // a main service not heard from in this long has gone
constexpr auto CONNECTED = std::chrono::seconds(5);         // heard from this recently: shown as connected

void reply(httplib::Response& response, const Json& value, int status = 200) {
    response.status = status;
    response.set_content(value.dump(), "application/json; charset=utf-8");
}

Json body_of(const httplib::Request& request) {
    Json value = Json::parse(request.body.empty() ? std::string("{}") : request.body);
    if (!value.is_object()) throw std::invalid_argument("expected a JSON object");
    return value;
}

void check(const Resources& shared) {
    const Machine found = scan_machine();
    if (shared.threads < 1 || shared.threads > found.threads) {
        throw std::invalid_argument("Share between 1 and " + std::to_string(found.threads) + " cores.");
    }
    if (shared.memory < (std::uint64_t{64} << 20)) throw std::invalid_argument("Share at least 64 MB of memory.");
    if (found.memory && shared.memory > found.memory) throw std::invalid_argument("That is more memory than this computer has.");
    for (const int index : shared.gpus) {
        if (index < 0 || static_cast<std::size_t>(index) >= found.gpus.size()) throw std::invalid_argument("There is no such GPU.");
    }
}

}  // namespace

Worker::Worker(std::filesystem::path file) : file_(std::move(file)) {}

Worker::~Worker() {
    if (stopping_thread_.joinable()) stopping_thread_.join();
    stop(false, std::chrono::seconds(0));
    if (closer_.joinable()) closer_.join();
}

Json Worker::machine() {
    const Machine found = scan_machine();
    Json gpus = Json::array();
    for (const gpu::DeviceInfo& device : found.gpus) gpus.push_back({{"name", device.name}, {"memory", device.memory}, {"discrete", device.discrete}});
    return {{"threads", found.threads}, {"memory", found.memory}, {"gpus", gpus}};
}

void Worker::save() const {
    Json saved = {{"worker", on_.load()}, {"port", port_}, {"threads", shared_.threads}, {"memory", shared_.memory}, {"gpus", shared_.gpus}};
    std::filesystem::create_directories(file_.parent_path());
    std::ofstream out(file_);
    out << saved.dump(2) << "\n";
}

void Worker::resume() {
    std::ifstream in(file_);
    if (!in) return;
    try {
        const Json saved = Json::parse(in);
        if (!saved.value("worker", false)) return;
        Resources shared;
        shared.threads = saved.value("threads", 1);
        shared.memory = saved.value("memory", std::uint64_t{0});
        shared.gpus = saved.value("gpus", std::vector<int>());
        start(saved.value("port", DEFAULT_WORKER_PORT), shared);
        std::printf("bitzkey_daemon: a worker, listening for its main service on port %d\n", port_);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "bitzkey_daemon: couldn't carry on as a worker: %s\n", error.what());
    }
}

void Worker::start(int port, const Resources& shared) {
    if (on_) throw std::invalid_argument("This daemon is a worker already.");
    if (port < 1 || port > 65535) throw std::invalid_argument("A port is between 1 and 65535.");
    check(shared);
    auto server = std::make_unique<httplib::Server>();
    if (!server->bind_to_port("0.0.0.0", port)) throw std::invalid_argument("Port " + std::to_string(port) + " can't be listened on: is it in use?");
    auto thinker = std::make_unique<Thinker>(shared);
    {
        std::lock_guard thinking(thinking_);
        std::lock_guard lock(mutex_);
        thinker_ = std::move(thinker);
        shared_ = shared;
        port_ = port;
        session_ = 0;
        main_address_.clear();
        message_.clear();
        leaving_ = released_ = false;
        noted();
    }
    server_ = std::move(server);
    serve(*server_, port);
    serving_ = std::thread([this] { server_->listen_after_bind(); });
    server_->wait_until_ready();  // listening before it can be stopped: a stop before then would be missed, and wait forever
    on_ = true;
    save();
    write_ports();
}

void Worker::move_to(int port) {
    if (!on_) throw std::invalid_argument("This daemon isn't a worker.");
    if (port < 1 || port > 65535) throw std::invalid_argument("A port is between 1 and 65535.");
    bool bound;
    {
        std::lock_guard lock(mutex_);
        if (port == port_) throw std::invalid_argument("It listens on that port already.");
        bound = session_ != 0;
    }
    auto server = std::make_unique<httplib::Server>();
    if (!server->bind_to_port("0.0.0.0", port)) throw std::invalid_argument("Port " + std::to_string(port) + " can't be listened on: is it in use?");
    serve(*server, port);
    if (closer_.joinable()) closer_.join();
    close_old();  // a port moved from before, and not yet let go
    {
        std::lock_guard lock(servers_);
        old_server_ = std::move(server_);
        old_serving_ = std::move(serving_);
    }
    server_ = std::move(server);
    serving_ = std::thread([this] { server_->listen_after_bind(); });
    server_->wait_until_ready();
    {
        std::lock_guard lock(mutex_);
        old_port_ = port_;
        port_ = port;
    }
    save();
    write_ports();
    if (!bound) close_old();  // no main service to wait for
}

void Worker::followed(int port) {
    if (!old_port_ || port == old_port_) return;
    std::lock_guard lock(servers_);
    if (closer_.joinable() || !old_server_) return;
    closer_ = std::thread([this] { close_old(); });
}

void Worker::close_old() {
    std::unique_ptr<httplib::Server> server;
    std::thread serving;
    {
        std::lock_guard lock(servers_);
        server = std::move(old_server_);
        serving = std::move(old_serving_);
    }
    if (server) server->stop();
    if (serving.joinable()) serving.join();
    if (old_port_.exchange(0)) write_ports();
}

void Worker::write_ports() const {
    std::string ports;
    if (on_) {
        std::lock_guard lock(mutex_);
        ports = std::to_string(port_);
        if (old_port_) ports += " " + std::to_string(old_port_.load());
    }
    std::ofstream out(file_.parent_path() / "worker-ports");
    out << ports << "\n";
}

void Worker::share(const Resources& shared) {
    if (!on_) throw std::invalid_argument("This daemon isn't a worker.");
    check(shared);
    std::lock_guard thinking(thinking_);
    thinker_->share(shared);
    std::lock_guard lock(mutex_);
    shared_ = shared;
    noted();
    save();
}

void Worker::noted() {
    held_ = thinker_ ? thinker_->held() : 0;
    speed_ = thinker_ ? thinker_->speed() : 0.0;
    description_ = thinker_ ? thinker_->description() : std::string();
    on_gpu_ = thinker_ && thinker_->on_gpu();
}

void Worker::stop(bool forget, std::chrono::seconds wait) {
    if (!on_) return;
    const auto start = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex_);
        leaving_ = true;
    }
    // The main service takes its brains back when next it asks after this worker, about once a second.
    for (;;) {
        {
            std::lock_guard lock(mutex_);
            const auto now = std::chrono::steady_clock::now();
            if (released_ || !session_ || held_ == 0 || now - contact_ > CONTACT_LAPSES || now - start >= wait) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (closer_.joinable()) closer_.join();
    close_old();
    server_->stop();
    if (serving_.joinable()) serving_.join();
    server_.reset();
    {
        std::lock_guard thinking(thinking_);
        std::lock_guard lock(mutex_);
        thinker_.reset();
        session_ = 0;
        main_address_.clear();
        message_.clear();
        leaving_ = released_ = false;
        noted();
    }
    on_ = false;
    if (forget) {  // its port closed in the firewall; stopping with the daemon, it is kept for its return
        save();
        write_ports();
    }
}

void Worker::stop_soon() {
    if (stopping_thread_.joinable()) stopping_thread_.join();
    {
        std::lock_guard lock(mutex_);
        leaving_ = true;  // shown at once
    }
    stopping_thread_ = std::thread([this] { stop(true); });
}

Json Worker::hello() const {  // under mutex_
    return {{"version", BITZKEY_VERSION},
            {"protocol", PROTOCOL},
            {"threads", shared_.threads},
            {"memory", shared_.memory},
            {"gpus", on_gpu_ ? Json::array({description_}) : Json::array()},
            {"speed", speed_},
            {"description", description_},
            {"held", held_},
            {"leaving", leaving_},
            {"main", main_address_},
            {"port", port_}};  // where it listens now: a main service reaching it on another follows it here
}

Json Worker::status() const {
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    Json result = hello();
    result["on"] = on_.load();
    result["port"] = port_;
    result["message"] = message_;
    result["connected"] = session_ != 0 && now - contact_ < CONNECTED;
    result["since_contact"] = session_ ? std::chrono::duration<double>(now - contact_).count() : -1.0;
    result["shared"] = {{"threads", shared_.threads}, {"memory", shared_.memory}, {"gpus", shared_.gpus}};
    return result;
}

void Worker::serve(httplib::Server& server, int port) {
    server.set_payload_max_length(std::size_t{1} << 31);

    server.Get("/bitzkey/hello", [this](const httplib::Request&, httplib::Response& response) {
        std::lock_guard lock(mutex_);
        reply(response, hello());
    });

    server.Post("/bitzkey/join", [this](const httplib::Request& request, httplib::Response& response) {
        try {
            const Json given = body_of(request);
            std::lock_guard thinking(thinking_);
            std::lock_guard lock(mutex_);
            const auto now = std::chrono::steady_clock::now();
            if (leaving_) {
                reply(response, {{"error", "it is stopping being a worker"}}, 409);
                return;
            }
            if (session_ && now - contact_ < CONTACT_LAPSES) {  // a main service restarted joins again once this lapses
                reply(response, {{"error", "it works for another main service, at " + main_address_}}, 409);
                return;
            }
            thinker_->clear();  // whatever it held for a main service before is let go
            session_ = given.at("session").get<std::uint64_t>();
            main_address_ = request.remote_addr;
            message_ = given.value("message", std::string()).substr(0, 1000);
            contact_ = now;
            noted();
            reply(response, hello());
        } catch (const std::exception& error) {
            reply(response, {{"error", error.what()}}, 400);
        }
    });

    server.Post("/bitzkey/batch", [this](const httplib::Request& request, httplib::Response& response) {
        try {
            const Batch batch = decode_batch(request.body);
            std::lock_guard thinking(thinking_);  // one batch at a time; the pings answered meanwhile
            Answer answer;
            {
                std::lock_guard lock(mutex_);
                if (!session_ || batch.session != session_) answer.known = false;
                else contact_ = std::chrono::steady_clock::now();
            }
            if (answer.known) {
                try {
                    answer = thinker_->apply(batch);
                } catch (const std::runtime_error& error) {  // its GPUs failed: it thinks on the CPU, starting afresh
                    std::fprintf(stderr, "bitzkey_daemon: the worker's GPUs failed (%s); thinking on the CPU\n", error.what());
                    Resources shared = shared_;
                    shared.gpus.clear();
                    thinker_ = std::make_unique<Thinker>(shared);
                    std::lock_guard lock(mutex_);
                    session_ = 0;  // the main service gives its brains to others, and joins again
                    noted();
                    reply(response, {{"error", std::string("its GPUs failed: ") + error.what()}}, 500);
                    return;
                }
            }
            std::lock_guard lock(mutex_);
            noted();
            answer.leaving = leaving_;
            response.set_content(encode(answer), "application/octet-stream");
        } catch (const std::exception& error) {
            reply(response, {{"error", error.what()}}, 400);
        }
    });

    server.Get("/bitzkey/status", [this, port](const httplib::Request& request, httplib::Response& response) {
        bool known;
        {
            std::lock_guard lock(mutex_);
            known = session_ && request.has_param("session") && request.get_param_value("session") == std::to_string(session_);
            if (known) contact_ = std::chrono::steady_clock::now();
            Json result = hello();
            result["known"] = known;
            reply(response, result);
        }
        if (known) followed(port);
    });

    // The main service has taken back every brain (or is letting this worker go): all are let go here.
    const auto let_go = [this](const httplib::Request& request, httplib::Response& response, bool unbind) {
        try {
            const Json given = body_of(request);
            std::lock_guard thinking(thinking_);
            std::lock_guard lock(mutex_);
            if (session_ && given.value("session", std::uint64_t{0}) == session_) {
                thinker_->clear();
                if (leaving_) released_ = true;
                if (unbind) {
                    session_ = 0;
                    main_address_.clear();
                    message_.clear();
                }
                noted();
            }
            reply(response, {{"held", held_}});
        } catch (const std::exception& error) {
            reply(response, {{"error", error.what()}}, 400);
        }
    };
    server.Post("/bitzkey/release", [let_go](const httplib::Request& request, httplib::Response& response) { let_go(request, response, false); });
    server.Post("/bitzkey/leave", [let_go](const httplib::Request& request, httplib::Response& response) { let_go(request, response, true); });
}

}  // namespace bitzkey::daemon
