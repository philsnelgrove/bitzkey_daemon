// Workers, as the desktop app has them: the wire between a main service and its workers, the brains a worker holds,
// and a world thinking through workers on spare local ports as they come, fail and go. The same code as the Linux
// daemon's (src/net is copied between them), so either can be the other's main service or worker.

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

#include "core/Simulation.hpp"
#include "net/Brains.hpp"
#include "net/Pool.hpp"
#include "net/Worker.hpp"

using namespace bitzkey;
using namespace bitzkey::net;

namespace {

std::filesystem::path folder_for(const std::string& name) {
    std::random_device device;
    const auto path = std::filesystem::temp_directory_path() / ("bitzkey-net-test-" + name + "-" + std::to_string(device()));
    std::filesystem::create_directories(path);
    return path;
}

Simulation::Settings busy_world() {
    Simulation::Settings settings;
    settings.width = 40;
    settings.height = 40;
    settings.population = 80;
    settings.seed = 11;
    return settings;
}

Resources a_little() {
    Resources shared;
    shared.threads = 1;
    shared.memory = std::uint64_t{256} << 20;
    return shared;
}

// A worker listening on a spare port.
int start_worker(Worker& worker, const Resources& shared = a_little()) {
    std::random_device device;
    for (int tries = 0; tries < 50; ++tries) {
        const int port = 52000 + static_cast<int>(device() % 8000);
        try {
            worker.start(port, shared);
            return port;
        } catch (const std::invalid_argument&) {
        }
    }
    FAIL("no spare port for a worker");
    return 0;
}

// Tend the pool, as the app does each frame, until `ready` holds, for up to `seconds`.
template <typename Ready>
bool await(Pool& pool, Simulation& simulation, Ready ready, double seconds = 15) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < until) {
        pool.tend(simulation);
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return ready();
}

Json worker_in(const Pool& pool, int port) {
    const Json list = pool.list();
    for (const auto& worker : list["workers"]) {
        if (worker["port"] == port) return worker;
    }
    return Json::object();
}

std::size_t living_brains(Simulation& simulation) {
    std::size_t count = 0;
    for (const Bitzkey* agent : simulation.living()) count += (agent->brain != nullptr) + (agent->target != nullptr);  // and its target brain
    return count;
}

}  // namespace

TEST_CASE("a batch and its answer cross the wire unchanged, but for floats") {
    PyRandom random(3);
    Batch batch;
    batch.session = 0x1234567890ABCDEFULL;
    batch.attaches.emplace_back(7, Brain::random(random, 3, 43));
    Batch::Lesson lesson{7, 4, true, 0.5f, 0.01f, 0.8f, -1, {}};
    for (float& sense : lesson.senses) sense = static_cast<float>(random.random());
    batch.lessons.push_back(lesson);
    batch.lessons.push_back({7, 2, false, -0.25f, 0.02f, 0.0f, 0, {}});
    batch.values.push_back({7, 0, {}});
    batch.downloads = {7};
    batch.detaches = {9};
    const Batch back = decode_batch(encode(batch));
    CHECK(back.session == batch.session);
    REQUIRE(back.attaches.size() == 1);
    CHECK(back.attaches[0].first == 7);
    CHECK((back.attaches[0].second.depth() == 3 && back.attaches[0].second.width() == 43));
    CHECK(back.attaches[0].second.layers[1].weights[5] == doctest::Approx(batch.attaches[0].second.layers[1].weights[5]).epsilon(1e-6));
    REQUIRE(back.lessons.size() == 2);
    CHECK(back.lessons[0].senses == lesson.senses);
    CHECK(back.lessons[0].traced);
    CHECK(back.lessons[1].ring == 0);
    CHECK(back.values[0].ring == 0);
    CHECK(back.downloads == batch.downloads);
    CHECK(back.detaches == batch.detaches);
    CHECK_THROWS(decode_batch(encode(batch).substr(0, 40)));

    Answer answer;
    answer.held = 3;
    answer.leaving = true;
    answer.values.push_back({});
    answer.values[0][5] = 1.5f;
    answer.downloads.push_back(Brain::random(random, 2));
    answer.downloads.emplace_back();  // one it didn't hold
    const Answer again = decode_answer(encode(answer));
    CHECK(again.held == 3);
    CHECK(again.leaving);
    CHECK(again.values[0][5] == 1.5f);
    CHECK(again.downloads[0].depth() == 2);
    CHECK(again.downloads[1].layers.empty());
}

TEST_CASE("the CPU engine thinks and learns as the brains themselves do") {
    PyRandom random(5);
    const Brain original = Brain::random(random, 4);
    Brain alone = original;
    CpuEngine engine(3);
    const auto handle = engine.attach(original);
    Senses senses{};
    for (double& sense : senses) sense = random.random();
    const gpu::BrainEngine::Lesson lessons[] = {{handle, &senses, 3, 0.7, 0.05, true, 0.8}, {handle, &senses, 5, -0.2, 0.05, false, 0.0}};
    engine.learn(lessons);
    alone.learn_traced_by(senses, 3, 0.7, 0.05, 0.8);
    alone.learn(senses, 5, -0.2, 0.05);
    Values out[1];
    const gpu::BrainEngine::Handle handles[] = {handle};
    engine.values(handles, std::span<const Senses>(&senses, 1), out);
    CHECK(out[0] == alone.values(senses));
    Brain back;
    Brain* into[] = {&back};
    engine.download(handles, into);
    CHECK(back == alone);
}

TEST_CASE("a worker remembers the senses it was sent, so a lesson can name them by their place") {
    PyRandom random(9);
    Thinker thinker(a_little());
    CHECK(thinker.speed() > 0);
    const Brain original = Brain::random(random, 2);
    Batch first;
    first.attaches.emplace_back(1, original);
    Batch::Ask ask{1, -1, {}};
    for (float& sense : ask.senses) sense = static_cast<float>(random.random());
    first.values.push_back(ask);
    const Answer valued = thinker.apply(first);
    REQUIRE(valued.values.size() == 1);
    Senses felt{};
    std::copy(ask.senses.begin(), ask.senses.end(), felt.begin());
    CHECK(valued.values[0][0] == static_cast<float>(original.values(felt)[0]));

    Batch second;  // a lesson from those senses, by their place in the ring
    second.lessons.push_back({1, 6, true, 0.4f, 0.03f, 0.75f, 0, {}});
    second.downloads = {1};
    const Answer learnt = thinker.apply(second);
    Brain expected = original;
    expected.learn_traced_by(felt, 6, 0.4f, 0.03f, 0.75f);
    CHECK(learnt.downloads[0] == expected);
    CHECK(thinker.held() == 1);
    thinker.clear();
    CHECK(thinker.held() == 0);
}


TEST_CASE("a world thinks through its workers, keeps on when one fails, and takes back the brains of one that stops") {
    const auto main_folder = folder_for("main"), first_folder = folder_for("first"), second_folder = folder_for("second");
    Worker first(first_folder / "worker.json"), second(second_folder / "worker.json");
    const int first_port = start_worker(first), second_port = start_worker(second);
    {
        Pool pool(main_folder / "workers.json");
        Simulation simulation(busy_world());
        pool.add("127.0.0.1", first_port, "Thinking for us");
        pool.add("127.0.0.1", second_port, "");
        pool.adopt(simulation);
        CHECK(pool.bound());
        REQUIRE(await(pool, simulation, [&] {
            return worker_in(pool, first_port)["state"] == "working" && worker_in(pool, second_port)["state"] == "working";
        }));
        CHECK(first.status()["message"] == "Thinking for us");
        for (int turn = 0; turn < 30; ++turn) {
            simulation.advance();
            pool.tend(simulation);
        }
        REQUIRE(await(pool, simulation, [&] {
            return worker_in(pool, first_port)["brains"].get<int>() > 0 && worker_in(pool, second_port)["brains"].get<int>() > 0;
        }));
        CHECK(pool.attached() == living_brains(simulation));

        first.stop(true, std::chrono::seconds(0));  // it fails outright: its brains go to the others
        for (int turn = 0; turn < 10; ++turn) {
            simulation.advance();
            pool.tend(simulation);
        }
        CHECK(worker_in(pool, first_port)["brains"] == 0);
        CHECK(pool.attached() == living_brains(simulation));
        CHECK(simulation.turn() == 40);

        // The second stops gracefully, handing its brains back while the world turns.
        std::thread stopping([&] { second.stop(true, std::chrono::seconds(20)); });
        CHECK(await(pool, simulation, [&] { return !second.on(); }, 25));
        stopping.join();
        CHECK(worker_in(pool, second_port)["brains"] == 0);
        CHECK(pool.attached() == living_brains(simulation));

        pool.remove("127.0.0.1", first_port);
        pool.remove("127.0.0.1", second_port);
        pool.adopt(simulation);  // alone again
        CHECK_FALSE(pool.bound());
        const int turn = simulation.turn();
        simulation.advance();
        CHECK(simulation.turn() == turn + 1);
    }
    for (const auto& folder : {main_folder, first_folder, second_folder}) std::filesystem::remove_all(folder);
}

TEST_CASE("a worker told its ports asks for them when the user changes them, not when it carries on") {
    const auto folder = folder_for("ports");
    std::vector<std::vector<int>> asked;
    int port = 0;
    {
        Worker worker(folder / "worker.json");
        worker.on_ports = [&](const std::vector<int>& ports) { asked.push_back(ports); };
        port = start_worker(worker);
        REQUIRE(asked.size() == 1);
        CHECK(asked[0] == std::vector<int>{port});
    }  // the app closed: nothing asked
    CHECK(asked.size() == 1);
    {
        Worker worker(folder / "worker.json");
        worker.on_ports = [&](const std::vector<int>& ports) { asked.push_back(ports); };
        worker.resume();  // its port open already
        CHECK(worker.on());
        CHECK(asked.size() == 1);
        int moved = 0;
        for (int tries = 1; tries < 50 && !moved; ++tries) {
            try {
                worker.move_to(port + tries);
                moved = port + tries;
            } catch (const std::invalid_argument&) {
            }
        }
        REQUIRE(moved);
        REQUIRE(asked.size() == 2);
        CHECK(asked[1].front() == moved);  // no main service: the old port let go at once, without asking
        worker.stop(true, std::chrono::seconds(0));
        REQUIRE(asked.size() == 3);
        CHECK(asked[2].empty());  // closed for good
    }
    std::filesystem::remove_all(folder);
}

TEST_CASE("a host that thinks for none hands every brain to its workers, and takes its share back when it thinks again") {
    const auto main_folder = folder_for("host"), worker_folder = folder_for("helper");
    Worker helper(worker_folder / "worker.json");
    const int port = start_worker(helper);
    {
        Pool pool(main_folder / "workers.json");
        Simulation simulation(busy_world());
        pool.add("127.0.0.1", port, "");
        pool.set_checkpoint(1);  // evening out every turn
        pool.adopt(simulation);
        REQUIRE(await(pool, simulation, [&] { return worker_in(pool, port)["state"] == "working"; }));
        pool.set_host_share(0);
        CHECK(pool.host_share() == 0);
        for (int turn = 0; turn < 30 && pool.list()["local"]["brains"].get<int>() > 0; ++turn) {
            simulation.advance();
            pool.tend(simulation);
        }
        CHECK(pool.list()["local"]["brains"] == 0);
        CHECK(worker_in(pool, port)["brains"].get<int>() > 0);
        CHECK(pool.attached() == living_brains(simulation));
        for (int turn = 0; turn < 10; ++turn) {  // its turns are timed, giving each its pace
            simulation.advance();
            pool.tend(simulation);
        }
        CHECK(worker_in(pool, port)["pace"].get<double>() > 0);

        pool.set_host_share(100);
        for (int turn = 0; turn < 30 && pool.list()["local"]["brains"].get<int>() == 0; ++turn) {
            simulation.advance();
            pool.tend(simulation);
        }
        CHECK(pool.list()["local"]["brains"].get<int>() > 0);
        CHECK(pool.attached() == living_brains(simulation));
        CHECK_THROWS_AS(pool.set_host_share(101), std::invalid_argument);
        pool.remove("127.0.0.1", port);
    }
    helper.stop(true, std::chrono::seconds(0));
    for (const auto& folder : {main_folder, worker_folder}) std::filesystem::remove_all(folder);
}
