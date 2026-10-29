#include "net/Pool.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <future>
#include <random>

#include <httplib.h>

#include "core/Simulation.hpp"

namespace bitzkey::net {

namespace {

constexpr auto POLL_EVERY = std::chrono::seconds(1);
constexpr auto RETRY_AFTER = std::chrono::seconds(3);
constexpr auto REFUSED_RETRY_AFTER = std::chrono::seconds(10);
constexpr int MISSED_PINGS = 3;  // a worker that misses this many pings in a row has failed
constexpr int MIN_MOVES = 16;    // evening out moves at least this many brains a time, and at most a tenth of them
constexpr int MIN_PACED = 8;     // brains a member must hold before its turns tell its pace
constexpr double PACE_SMOOTHING = 0.2;  // the share of a turn's timing taken into a member's pace

std::uint64_t fresh_session() {
    std::random_device device;
    return ((static_cast<std::uint64_t>(device()) << 32) | device()) | 1;
}

// A hash of senses as a worker holds them, in floats; never 0, which marks an empty place in a mirror.
std::uint64_t hash(const FloatSenses& senses) {
    std::uint64_t value = 1469598103934665603ULL;
    const auto* bytes = reinterpret_cast<const unsigned char*>(senses.data());
    for (std::size_t index = 0; index < sizeof(FloatSenses); ++index) value = (value ^ bytes[index]) * 1099511628211ULL;
    return value | 1;
}

FloatSenses to_floats(const Senses& senses) {
    FloatSenses made;
    for (int index = 0; index < INPUT_SIZE; ++index) made[static_cast<std::size_t>(index)] = static_cast<float>(senses[static_cast<std::size_t>(index)]);
    return made;
}

std::unique_ptr<httplib::Client> client_for(const std::string& host, int port, int read_seconds) {
    auto client = std::make_unique<httplib::Client>(host, port);
    client->set_connection_timeout(3);
    client->set_read_timeout(read_seconds);
    client->set_write_timeout(read_seconds);
    client->set_keep_alive(true);
    return client;
}

std::string error_of(const httplib::Result& result) {
    if (!result) return httplib::to_string(result.error());
    try {
        const auto body = nlohmann::json::parse(result->body);
        if (body.contains("error")) return body["error"].get<std::string>();
    } catch (const std::exception&) {
    }
    return "it answered " + std::to_string(result->status);
}

// The engine a world thinks through, its brains in the pool.
class PoolEngine final : public gpu::BrainEngine {
public:
    PoolEngine(Pool& pool, int binding) : pool_(pool), binding_(binding) {}
    ~PoolEngine() override { pool_.unbind(binding_); }
    const std::vector<gpu::DeviceInfo>& devices() const override { return pool_.devices(); }
    Handle attach(const Brain& brain) override { return pool_.attach(brain); }
    void detach(Handle brain) override { pool_.detach(brain); }
    void download(std::span<const Handle> brains, std::span<Brain* const> into) override { pool_.download(brains, into); }
    void values(std::span<const Handle> brains, std::span<const Senses> senses, std::span<Values> out) override {
        pool_.values(brains, senses, out);
    }
    void learn(std::span<const Lesson> lessons) override { pool_.learn(lessons); }
    std::size_t attached() const override { return pool_.attached(); }

private:
    Pool& pool_;
    int binding_;  // which of the pool's worlds this engine is
};

}  // namespace

std::string Pool::Member::name() const { return local ? std::string("this service") : host + ":" + std::to_string(port); }

Pool::Pool(std::filesystem::path file) : file_(std::move(file)) {
    load();
    if (!members_.empty()) start_polling();
}

Pool::~Pool() {
    stopping_ = true;
    if (poller_.joinable()) poller_.join();
}

// ---------------------------------------------------------------------------------------------
// The list of workers

void Pool::load() {
    std::ifstream in(file_);
    if (!in) return;
    try {
        const auto saved = nlohmann::json::parse(in);
        checkpoint_turns_ = std::max(1, saved.value("checkpoint_turns", DEFAULT_CHECKPOINT_TURNS));
        host_share_ = std::clamp(saved.value("host_share", 100), 0, 100);
        for (const auto& entry : saved.value("workers", nlohmann::json::array())) {
            auto member = std::make_shared<Member>();
            member->host = entry.value("host", std::string());
            member->port = entry.value("port", DEFAULT_WORKER_PORT);
            member->message = entry.value("message", std::string());
            if (!member->host.empty()) members_.push_back(std::move(member));
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "bitzkey_daemon: the list of workers in %s is damaged: %s\n", file_.string().c_str(), error.what());
    }
}

void Pool::save() const {
    nlohmann::json saved = {{"checkpoint_turns", checkpoint_turns_}, {"host_share", host_share_}, {"workers", nlohmann::json::array()}};
    {
        std::lock_guard lock(mutex_);
        for (const auto& member : members_) saved["workers"].push_back({{"host", member->host}, {"port", member->port}, {"message", member->message}});
    }
    std::filesystem::create_directories(file_.parent_path());
    std::ofstream out(file_);
    out << saved.dump(2) << "\n";
}

void Pool::add(const std::string& host, int port, const std::string& message) {
    if (host.empty() || host.size() > 253 || host.find_first_of(" /\\?#@") != std::string::npos) {
        throw std::invalid_argument("Give the worker's IP address or name.");
    }
    if (port < 1 || port > 65535) throw std::invalid_argument("A port is between 1 and 65535.");
    if (message.size() > 1000) throw std::invalid_argument("The message is too long: at most 1000 characters.");
    {
        std::lock_guard lock(mutex_);
        for (const auto& member : members_) {
            if (member->host == host && member->port == port) throw std::invalid_argument("That worker is in the pool already.");
        }
        auto member = std::make_shared<Member>();
        member->host = host;
        member->port = port;
        member->message = message;
        members_.push_back(std::move(member));
    }
    save();
    start_polling();
}

void Pool::remove(const std::string& host, int port) {
    std::shared_ptr<Member> found;
    {
        std::lock_guard lock(mutex_);
        for (const auto& member : members_) {
            if (member->host == host && member->port == port) found = member;
        }
    }
    if (!found) throw std::invalid_argument("That worker isn't in the pool.");
    State state;
    std::uint64_t session;
    {
        std::lock_guard lock(mutex_);
        state = found->state;
        session = found->session;
    }
    if (bound_ && found->count) {
        if (state == State::Ready || state == State::Leaving) evacuate(*found);
        else fail(*found, "removed");
    }
    if (session) {  // the worker works for no one now
        auto client = client_for(found->host, found->port, 5);
        client->Post("/bitzkey/leave", nlohmann::json{{"session", session}}.dump(), "application/json");
    }
    {
        std::lock_guard lock(mutex_);
        std::erase(members_, found);
    }
    save();
}

void Pool::set_checkpoint(int turns) {
    if (turns < 1 || turns > 100000) throw std::invalid_argument("Checkpoints come every 1 to 100000 turns.");
    checkpoint_turns_ = turns;
    save();
}

void Pool::set_host_share(int percent) {
    if (percent < 0 || percent > 100) throw std::invalid_argument("The host share runs from 0 to 100 percent.");
    host_share_ = percent;
    even_out_ = true;
    save();
}

Json Pool::list() const {
    const auto state_name = [](State state) {
        switch (state) {
            case State::Connecting: return "connecting";
            case State::Ready: return "working";
            case State::Lost: return "lost";
            case State::Leaving: return "stopping";
            case State::Away: return "away";
            case State::Refused: return "refused";
        }
        return "";
    };
    Json workers = Json::array();
    std::lock_guard lock(mutex_);
    for (const auto& member : members_) {
        workers.push_back({{"host", member->host},
                           {"port", member->port},
                           {"message", member->message},
                           {"state", state_name(member->state)},
                           {"error", member->error},
                           {"ping_ms", member->ping_ms},
                           {"threads", member->threads},
                           {"memory", member->memory},
                           {"gpus", member->gpus},
                           {"description", member->description},
                           {"speed", member->speed},
                           {"pace", member->turn_seconds > 0 && member->count ? member->count / member->turn_seconds : 0.0},
                           {"brains", member->count}});
    }
    Json local = {{"description", local_ ? local_->description : std::string()},
                  {"speed", local_ ? local_->speed : 0.0},
                  {"pace", local_ && local_->turn_seconds > 0 && local_->count ? local_->count / local_->turn_seconds : 0.0},
                  {"brains", local_ ? local_->count : 0}};
    return {{"checkpoint_turns", checkpoint_turns_}, {"host_share", host_share_}, {"workers", workers}, {"local", local},
            {"thinking", bound_ != nullptr}};
}

std::string Pool::status() const {
    std::size_t working = 0;
    {
        std::lock_guard lock(mutex_);
        for (const auto& member : members_) working += member->state == State::Ready;
    }
    const std::string here = local_ ? local_->description : std::string("this service");
    return "thinking on " + here + " and " + std::to_string(working) + " of " + std::to_string(members_.size()) +
           (members_.size() == 1 ? " worker" : " workers");
}

const std::vector<gpu::DeviceInfo>& Pool::devices() const {
    static const std::vector<gpu::DeviceInfo> none;
    return none;
}

// ---------------------------------------------------------------------------------------------
// Polling the workers

void Pool::start_polling() {
    if (poller_.joinable()) return;
    poller_ = std::thread([this] { poll(); });
}

void Pool::poll() {
    while (!stopping_) {
        std::vector<std::shared_ptr<Member>> members;
        {
            std::lock_guard lock(mutex_);
            members = members_;
        }
        for (const auto& member : members) {
            if (stopping_) return;
            poll_one(member);
        }
        for (auto waited = std::chrono::milliseconds(0); waited < POLL_EVERY && !stopping_; waited += std::chrono::milliseconds(50)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

void Pool::poll_one(const std::shared_ptr<Member>& member) {
    State state;
    std::uint64_t session;
    std::chrono::steady_clock::time_point retry;
    std::string host;
    int port;
    {
        std::lock_guard lock(mutex_);
        state = member->state;
        session = member->session;
        retry = member->retry;
        host = member->host;
        port = member->port;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto take = [&](const nlohmann::json& info) {  // what a worker says of itself
        member->threads = info.value("threads", 0);
        member->memory = info.value("memory", std::uint64_t{0});
        member->speed = info.value("speed", 0.0);
        member->description = info.value("description", std::string());
        member->gpus = info.value("gpus", std::vector<std::string>());
    };
    auto client = client_for(host, port, 5);
    if (state == State::Ready) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = client->Get("/bitzkey/status?session=" + std::to_string(session));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::lock_guard lock(mutex_);
        if (member->state != State::Ready || member->session != session) return;  // changed on the world's thread meanwhile
        if (!result || result->status != 200) {
            if (++member->failures >= MISSED_PINGS) {
                member->state = State::Lost;
                member->error = "stopped answering (" + error_of(result) + ")";
            }
            return;
        }
        member->failures = 0;
        member->ping_ms = ms;
        try {
            const auto info = nlohmann::json::parse(result->body);
            take(info);
            const int moved_to = info.value("port", member->port);
            if (moved_to != member->port && moved_to >= 1 && moved_to <= 65535) {  // it listens elsewhere now: followed there
                member->port = moved_to;
                member->moved = true;
            }
            if (!info.value("known", false)) {
                member->state = State::Lost;
                member->error = "it restarted";
            } else if (info.value("leaving", false)) {
                member->state = State::Leaving;
            }
        } catch (const std::exception&) {
        }
        return;
    }
    if (state != State::Connecting && state != State::Away && state != State::Refused) return;
    if (now < retry) return;
    const auto start = std::chrono::steady_clock::now();
    const auto hello = client->Get("/bitzkey/hello");
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (!hello || hello->status != 200) {
        std::lock_guard lock(mutex_);
        if (member->state == State::Away) member->error = "stopped";
        else member->error = "unreachable (" + error_of(hello) + ")";
        member->ping_ms = -1;
        member->retry = now + RETRY_AFTER;
        return;
    }
    nlohmann::json info;
    try {
        info = nlohmann::json::parse(hello->body);
    } catch (const std::exception&) {
        std::lock_guard lock(mutex_);
        member->state = State::Refused;
        member->error = "it isn't a Bitzkey worker";
        member->retry = now + REFUSED_RETRY_AFTER;
        return;
    }
    if (info.value("protocol", 0u) != PROTOCOL) {
        std::lock_guard lock(mutex_);
        member->state = State::Refused;
        member->error = "it runs another version of Bitzkey (" + info.value("version", std::string("?")) + ")";
        member->retry = now + REFUSED_RETRY_AFTER;
        return;
    }
    if (info.value("leaving", false)) {
        std::lock_guard lock(mutex_);
        member->error = "it is stopping";
        member->retry = now + RETRY_AFTER;
        return;
    }
    const std::uint64_t fresh = fresh_session();
    const auto joined = client->Post("/bitzkey/join", nlohmann::json{{"session", fresh}, {"message", member->message}}.dump(), "application/json");
    std::lock_guard lock(mutex_);
    if (!joined || joined->status != 200) {
        member->state = joined && joined->status == 409 ? State::Refused : State::Connecting;
        member->error = error_of(joined);
        member->retry = now + (member->state == State::Refused ? REFUSED_RETRY_AFTER : RETRY_AFTER);
        return;
    }
    try {
        take(nlohmann::json::parse(joined->body));
    } catch (const std::exception&) {
    }
    member->session = fresh;
    member->state = State::Ready;
    member->error.clear();
    member->failures = 0;
    member->ping_ms = ms;
    member->fresh = true;
}

// ---------------------------------------------------------------------------------------------
// The world's brains

void Pool::make_local() {
    Resources resources;
    resources.threads = scan_machine().threads;
    if (USE_GPU) {  // the GPUs this service would think on alone: the discrete ones, else the integrated
        const auto gpus = gpu::scan();
        for (int pass = 0; pass < 2 && resources.gpus.empty(); ++pass) {
            for (std::size_t index = 0; index < gpus.size(); ++index) {
                if (gpus[index].discrete == (pass == 0)) resources.gpus.push_back(static_cast<int>(index));
            }
        }
    }
    thinker_ = std::make_unique<Thinker>(resources);
    if (!local_) {
        local_ = std::make_unique<Member>();
        local_->local = true;
    }
    std::lock_guard lock(mutex_);
    local_->state = State::Ready;
    local_->speed = thinker_->speed();
    local_->description = thinker_->description();
    local_->threads = resources.threads;
}

void Pool::adopt(Simulation& simulation) {
    if (active() && bound_ != &simulation) {
        simulation.use_engine(engine(simulation), status());
    } else if (!active() && bound_ == &simulation) {  // thinking alone again, as a world begins
        std::string why, status;
        std::unique_ptr<gpu::BrainEngine> alone;
        if (USE_GPU) {
            alone = gpu::BrainEngine::create(&why);
            if (alone) {
                status = "thinking on";
                for (const gpu::DeviceInfo& device : alone->devices()) status += " " + device.name + ";";
                status.pop_back();
            } else {
                status = "thinking on the CPU: " + why;
            }
        }
        simulation.use_engine(std::move(alone), status);
    }
}

std::unique_ptr<gpu::BrainEngine> Pool::engine(Simulation& simulation) {
    if (!thinker_) make_local();
    if (bound_) unbind(binding_);  // one world at a time: the last one's brains let go
    bound_ = &simulation;
    synced_turn_ = simulation.turn();
    even_out_ = true;
    return std::make_unique<PoolEngine>(*this, ++binding_);
}

void Pool::unbind(int binding) {
    if (binding != binding_ || !bound_) return;  // a world whose brains were let go already
    bound_ = nullptr;
    owners_.clear();
    const auto forget = [](Member& member) {
        member.attaches.clear();
        member.lessons.clear();
        member.detaches.clear();
        member.mirrors.clear();
        member.count = 0;
        member.bytes = 0;
    };
    if (local_) forget(*local_);
    if (thinker_) thinker_->clear();
    std::vector<std::shared_ptr<Member>> members;
    {
        std::lock_guard lock(mutex_);
        members = members_;
    }
    for (const auto& member : members) {
        const bool held = member->count || !member->attaches.empty();
        forget(*member);
        if (held) release(*member);
    }
}

Pool::Member* Pool::choose(std::uint64_t bytes, const Member* except) {
    // The member with most room for one more, by its pace, among those working with memory to spare.
    Member* best = nullptr;
    double best_load = 0.0;
    const auto consider = [&](Member& member) {
        if (&member == except || member.state != State::Ready) return;
        if (!member.local && member.memory && member.bytes + bytes > member.memory) return;
        const double speed = pace(member);
        if (speed <= 0) return;  // the host, thinking for none
        const double load = static_cast<double>(member.count + 1) / speed;
        if (!best || load < best_load) {
            best = &member;
            best_load = load;
        }
    };
    std::lock_guard lock(mutex_);
    if (local_) consider(*local_);
    for (const auto& member : members_) consider(*member);
    return best ? best : local_.get();  // with no room anywhere, the main service holds it
}

double Pool::pace(const Member& member) const {
    // The brains a second it has got through on real turns, once it has thought for some; until then, as it measured.
    const double measured = member.turn_seconds > 0 && member.count >= MIN_PACED ? member.count / member.turn_seconds
                                                                                 : std::max(member.speed, 1.0);
    return member.local ? measured * host_share_ / 100.0 : measured;
}

void Pool::place(int handle, Brain brain, const Member* except) {
    const std::uint64_t bytes = brain_bytes(brain.depth(), brain.width());
    Member* member = choose(bytes, except);
    member->attaches.emplace_back(handle, std::move(brain));
    member->count += 1;
    member->bytes += bytes;
    owners_[handle] = {member, bytes};
}

int Pool::attach(const Brain& brain) {
    const int handle = next_handle_++;
    place(handle, brain);
    return handle;
}

void Pool::detach(int handle) {
    const auto found = owners_.find(handle);
    if (found == owners_.end()) return;
    Member& member = *found->second.member;
    const auto waiting = std::find_if(member.attaches.begin(), member.attaches.end(), [&](const auto& item) { return item.first == handle; });
    if (waiting != member.attaches.end()) member.attaches.erase(waiting);  // never sent: nothing to let go there
    else member.detaches.push_back(handle);
    member.mirrors.erase(handle);
    member.count -= 1;
    member.bytes -= found->second.bytes;
    owners_.erase(found);
}

void Pool::learn(std::span<const gpu::BrainEngine::Lesson> lessons) {
    for (const auto& lesson : lessons) {
        const auto found = owners_.find(lesson.brain);
        if (found == owners_.end()) continue;  // a brain since let go
        found->second.member->lessons.push_back({lesson.brain, lesson.action, lesson.traced, lesson.target, lesson.rate, lesson.decay, *lesson.senses});
    }
}

void Pool::values(std::span<const int> handles, std::span<const Senses> senses, std::span<Values> out) {
    flush(handles, senses, out, {}, {});
}

void Pool::download(std::span<const int> handles, std::span<Brain* const> into) { flush({}, {}, {}, handles, into); }

Batch Pool::batch_for(Member& member, const std::vector<std::size_t>& asks, std::span<const int> handles, std::span<const Senses> senses,
                      const std::vector<std::size_t>& downloads, std::span<const int> download_handles) {
    Batch batch;
    {
        std::lock_guard lock(mutex_);
        batch.session = member.session;
    }
    // A remote worker is spared senses it already holds: those of a recent memory, most often.
    const auto refer = [&](int handle, const Senses& given, int& ring, FloatSenses& floats) {
        floats = to_floats(given);
        ring = -1;
        if (member.local) return;
        Mirror& mirror = member.mirrors[handle];
        const std::uint64_t key = hash(floats);
        for (int index = 0; index < RING; ++index) {
            if (mirror.hashes[static_cast<std::size_t>(index)] == key) {
                ring = index;
                return;
            }
        }
        mirror.hashes[static_cast<std::size_t>(mirror.next)] = key;
        mirror.next = (mirror.next + 1) % RING;
    };
    for (const auto& [handle, brain] : member.attaches) {
        member.mirrors[handle] = Mirror{};
        batch.attaches.emplace_back(handle, brain);
    }
    batch.lessons.reserve(member.lessons.size());
    for (const RawLesson& raw : member.lessons) {
        Batch::Lesson& lesson = batch.lessons.emplace_back();
        lesson.handle = raw.handle;
        lesson.action = raw.action;
        lesson.traced = raw.traced;
        lesson.target = static_cast<float>(raw.target);
        lesson.rate = static_cast<float>(raw.rate);
        lesson.decay = static_cast<float>(raw.decay);
        refer(raw.handle, raw.senses, lesson.ring, lesson.senses);
    }
    for (const std::size_t index : asks) {
        Batch::Ask& ask = batch.values.emplace_back();
        ask.handle = handles[index];
        refer(ask.handle, senses[index], ask.ring, ask.senses);
    }
    for (const std::size_t index : downloads) batch.downloads.push_back(download_handles[index]);
    batch.detaches = member.detaches;
    for (const int handle : member.detaches) member.mirrors.erase(handle);
    return batch;
}

Answer Pool::send(Member& member, const Batch& batch) {
    if (member.local) return thinker_->apply(batch);
    if (!member.client) {
        std::lock_guard lock(mutex_);  // its port may change on the polling thread
        member.client = client_for(member.host, member.port, 120);
    }
    const auto result = member.client->Post("/bitzkey/batch", encode(batch), "application/octet-stream");
    if (!result || result->status != 200) throw std::runtime_error(error_of(result));
    return decode_answer(result->body);
}

void Pool::flush(std::span<const int> asks, std::span<const Senses> senses, std::span<Values> out, std::span<const int> downloads,
                 std::span<Brain* const> into) {
    std::vector<std::size_t> asks_left(asks.size()), downloads_left(downloads.size());
    for (std::size_t index = 0; index < asks.size(); ++index) asks_left[index] = index;
    for (std::size_t index = 0; index < downloads.size(); ++index) downloads_left[index] = index;
    for (std::size_t attempt = 0; attempt < members_.size() + 3; ++attempt) {
        std::unordered_map<Member*, std::pair<std::vector<std::size_t>, std::vector<std::size_t>>> work;
        for (const std::size_t index : asks_left) {
            const auto found = owners_.find(asks[index]);
            if (found == owners_.end()) out[index].fill(0.0);  // no brain: nothing to value with
            else work[found->second.member].first.push_back(index);
        }
        for (const std::size_t index : downloads_left) {
            const auto found = owners_.find(downloads[index]);
            if (found != owners_.end()) work[found->second.member].second.push_back(index);
        }
        const auto waiting = [](const Member& member) { return !member.attaches.empty() || !member.lessons.empty() || !member.detaches.empty(); };
        if (local_ && waiting(*local_)) work[local_.get()];
        for (const auto& member : members_) {
            if (waiting(*member)) work[member.get()];
        }
        if (work.empty()) return;
        struct Sent {
            Member* member;
            Batch batch;
            std::future<Answer> remote;
            Answer answer;
            std::string error;
            double seconds = 0;  // how long it took to answer, its network included
        };
        std::vector<Sent> sent;
        sent.reserve(work.size());
        for (auto& [member, mine] : work) sent.push_back({member, batch_for(*member, mine.first, asks, senses, mine.second, downloads), {}, {}, {}});
        const auto timed = [this](Sent& each) {
            const auto start = std::chrono::steady_clock::now();
            Answer answer = send(*each.member, each.batch);
            each.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            return answer;
        };
        for (Sent& each : sent) {  // every worker at once, the main service's own share meanwhile
            if (!each.member->local) each.remote = std::async(std::launch::async, [&timed, &each] { return timed(each); });
        }
        for (Sent& each : sent) {
            try {
                each.answer = each.member->local ? timed(each) : each.remote.get();
                if (!each.answer.known) each.error = "it restarted";
            } catch (const std::exception& error) {
                each.error = error.what();
            }
        }
        asks_left.clear();
        downloads_left.clear();
        for (Sent& each : sent) {
            Member& member = *each.member;
            const auto& [mine_asks, mine_downloads] = work[&member];
            if (each.error.empty()) {
                for (std::size_t k = 0; k < mine_asks.size() && each.error.empty(); ++k) {
                    if (k >= each.answer.values.size() || std::isnan(each.answer.values[k][0])) each.error = "it lost a brain";
                }
                for (std::size_t k = 0; k < mine_downloads.size() && each.error.empty(); ++k) {
                    if (k >= each.answer.downloads.size() || each.answer.downloads[k].layers.empty()) each.error = "it lost a brain";
                }
            }
            if (!each.error.empty()) {
                fail(member, each.error);
                asks_left.insert(asks_left.end(), mine_asks.begin(), mine_asks.end());
                downloads_left.insert(downloads_left.end(), mine_downloads.begin(), mine_downloads.end());
                continue;
            }
            member.busy += each.seconds;
            // What it was sent is done; anything given it since, in place of a member that failed, waits.
            member.attaches.erase(member.attaches.begin(), member.attaches.begin() + static_cast<std::ptrdiff_t>(each.batch.attaches.size()));
            member.lessons.erase(member.lessons.begin(), member.lessons.begin() + static_cast<std::ptrdiff_t>(each.batch.lessons.size()));
            member.detaches.erase(member.detaches.begin(), member.detaches.begin() + static_cast<std::ptrdiff_t>(each.batch.detaches.size()));
            for (std::size_t k = 0; k < mine_asks.size(); ++k) {
                for (int output = 0; output < OUTPUT_COUNT; ++output) out[mine_asks[k]][output] = each.answer.values[k][output];
            }
            for (std::size_t k = 0; k < mine_downloads.size(); ++k) {
                Brain& brain = *into[mine_downloads[k]];
                Brain& given = each.answer.downloads[k];
                brain.layers = std::move(given.layers);
                brain.output_weights = given.output_weights;
                brain.output_biases = given.output_biases;
            }
            if (each.answer.leaving) {
                std::lock_guard lock(mutex_);
                if (member.state == State::Ready) member.state = State::Leaving;
            }
        }
        if (asks_left.empty() && downloads_left.empty()) {
            bool more = false;  // work given to members in place of those that failed is still to go
            if (local_ && waiting(*local_)) more = true;
            for (const auto& member : members_) more = more || waiting(*member);
            if (!more) return;
        }
    }
    for (const std::size_t index : asks_left) out[index].fill(0.0);  // every member failed it: nothing to value with
}

void Pool::fail(Member& member, const std::string& why) {
    std::fprintf(stderr, "bitzkey_daemon: %s failed (%s); its brains go to the others\n", member.name().c_str(), why.c_str());
    {
        std::lock_guard lock(mutex_);
        if (!member.local) {
            member.state = State::Connecting;
            member.session = 0;
            member.retry = std::chrono::steady_clock::now() + RETRY_AFTER;
            member.ping_ms = -1;
        }
        member.error = why;
        member.failures = 0;
    }
    member.client.reset();
    member.mirrors.clear();
    std::unordered_map<int, Brain> waiting;
    for (auto& [handle, brain] : member.attaches) waiting.emplace(handle, std::move(brain));
    std::vector<RawLesson> lessons = std::move(member.lessons);
    member.attaches.clear();
    member.lessons.clear();
    member.detaches.clear();
    member.count = 0;
    member.bytes = 0;
    if (member.local) {  // its GPUs failed: it thinks on the CPU from here on
        Resources resources = thinker_->resources();
        resources.gpus.clear();
        thinker_ = std::make_unique<Thinker>(resources);
        std::lock_guard lock(mutex_);
        member.speed = thinker_->speed();
        member.description = thinker_->description();
    }
    std::vector<int> mine;
    for (const auto& [handle, owner] : owners_) {
        if (owner.member == &member) mine.push_back(handle);
    }
    std::sort(mine.begin(), mine.end());
    for (const int handle : mine) {
        // As it was last brought back, or as it was given if never sent; a brain no animal has is gone.
        if (auto found = waiting.find(handle); found != waiting.end()) {
            place(handle, std::move(found->second), member.local ? nullptr : &member);
        } else if (const Brain* kept = bound_ ? bound_->engine_brain(handle) : nullptr) {
            place(handle, *kept, member.local ? nullptr : &member);
        } else {
            owners_.erase(handle);
        }
    }
    for (RawLesson& lesson : lessons) {  // this turn's lessons, learnt where its brain is now
        if (const auto found = owners_.find(lesson.handle); found != owners_.end()) found->second.member->lessons.push_back(std::move(lesson));
    }
}

void Pool::release(Member& member) {
    std::uint64_t session;
    std::string host;
    int port;
    {
        std::lock_guard lock(mutex_);
        session = member.session;
        host = member.host;
        port = member.port;
    }
    if (!session || member.local) return;
    auto client = client_for(host, port, 10);
    client->Post("/bitzkey/release", nlohmann::json{{"session", session}}.dump(), "application/json");
}

void Pool::evacuate(Member& member) {
    std::vector<int> mine;
    for (const auto& [handle, owner] : owners_) {
        if (owner.member == &member) mine.push_back(handle);
    }
    std::sort(mine.begin(), mine.end());
    std::vector<Brain> brains(mine.size());
    std::vector<Brain*> into;
    for (Brain& brain : brains) into.push_back(&brain);
    flush({}, {}, {}, mine, into);
    for (std::size_t index = 0; index < mine.size(); ++index) {
        const auto found = owners_.find(mine[index]);
        if (found == owners_.end() || found->second.member != &member || brains[index].layers.empty()) continue;  // moved already
        detach(mine[index]);
        place(mine[index], std::move(brains[index]), &member);
    }
    member.detaches.clear();  // let go of all at once, below
    member.mirrors.clear();
    flush({}, {}, {}, {}, {});
    release(member);
    std::lock_guard lock(mutex_);
    if (!member.local && member.state != State::Connecting) {
        member.state = State::Away;
        member.session = 0;
        member.retry = std::chrono::steady_clock::now() + RETRY_AFTER;
    }
}

void Pool::rebalance() {
    // Each member's fair share, by its pace; those holding too many give some up to the rest.
    std::vector<Member*> working;
    std::unordered_map<Member*, double> paces;
    double total_speed = 0.0;
    {
        std::lock_guard lock(mutex_);
        if (local_ && local_->state == State::Ready) working.push_back(local_.get());
        for (const auto& member : members_) {
            if (member->state == State::Ready) working.push_back(member.get());
        }
        for (Member* member : working) total_speed += paces[member] = pace(*member);
    }
    if (working.size() < 2 || owners_.empty() || total_speed <= 0) return;
    const double total = static_cast<double>(owners_.size());
    std::size_t budget = std::max<std::size_t>(MIN_MOVES, owners_.size() / 10);
    std::vector<int> moving;
    std::unordered_map<Member*, std::size_t> taking;
    for (Member* member : working) {
        const double share = total * paces[member] / total_speed;
        const double excess = static_cast<double>(member->count) - share;
        if (excess > (share > 0 ? std::max(4.0, 0.1 * share) : 0.0)) taking[member] = static_cast<std::size_t>(std::ceil(excess));
    }
    if (taking.empty()) return;
    for (const auto& [handle, owner] : owners_) {
        if (!budget) break;
        auto found = taking.find(owner.member);
        if (found == taking.end() || !found->second) continue;
        moving.push_back(handle);
        --found->second;
        --budget;
    }
    std::sort(moving.begin(), moving.end());
    std::vector<Member*> from;
    for (const int handle : moving) from.push_back(owners_.at(handle).member);
    std::vector<Brain> brains(moving.size());
    std::vector<Brain*> into;
    for (Brain& brain : brains) into.push_back(&brain);
    flush({}, {}, {}, moving, into);
    for (std::size_t index = 0; index < moving.size(); ++index) {
        const auto found = owners_.find(moving[index]);
        if (found == owners_.end() || found->second.member != from[index] || brains[index].layers.empty()) continue;
        detach(moving[index]);
        place(moving[index], std::move(brains[index]), from[index]);
    }
    flush({}, {}, {}, {}, {});
}

void Pool::tend(Simulation& simulation) {
    if (bound_ != &simulation) return;
    std::vector<Member*> lost, leaving;
    bool moved = false;
    {
        std::lock_guard lock(mutex_);
        for (const auto& member : members_) {
            if (member->moved) {  // its batches go to its new port
                member->moved = false;
                member->client.reset();
                moved = true;
            }
            if (member->state == State::Lost) lost.push_back(member.get());
            if (member->state == State::Leaving) leaving.push_back(member.get());
            if (member->fresh) {
                member->fresh = false;
                even_out_ = true;
            }
        }
    }
    for (Member* member : lost) {
        std::string why;
        {
            std::lock_guard lock(mutex_);
            why = member->error;
        }
        fail(*member, why);
    }
    if (moved) save();
    for (Member* member : leaving) evacuate(*member);
    bool remote = false;
    for (const auto& member : members_) remote = remote || member->count;
    const int turn = simulation.turn(), parts = checkpoint_turns_;
    if (turn != synced_turn_) {
        // How long each took over its share these turns, smoothed, for its pace.
        const int turns = synced_turn_ < turn ? turn - synced_turn_ : 1;
        const auto count_turns = [&](Member& member) {
            if (member.count && member.busy > 0) {
                const double seconds = member.busy / turns;
                member.turn_seconds = member.turn_seconds > 0 ? (1 - PACE_SMOOTHING) * member.turn_seconds + PACE_SMOOTHING * seconds : seconds;
            } else if (!member.count) {
                member.turn_seconds = 0;  // nothing to go by
            }
            member.busy = 0;
        };
        if (local_) count_turns(*local_);
        for (const auto& member : members_) count_turns(*member);
        if (remote) {
            // Every brain as it now is, to give to others should its worker fail: a share of them, by handle, each
            // turn, so each is brought back once a checkpoint without the turn that brings back all at once. Turns
            // played since this was last called bring back their shares too.
            const int count = synced_turn_ < turn ? std::min(turn - synced_turn_, parts) : 1;
            simulation.sync_brains(((turn - count + 1) % parts + parts) % parts, count, parts);
            if (turn / parts != synced_turn_ / parts) even_out_ = true;  // a checkpoint's round done
        }
        synced_turn_ = turn;
    }
    if (even_out_) {
        even_out_ = false;
        rebalance();
    }
}

}  // namespace bitzkey::net
