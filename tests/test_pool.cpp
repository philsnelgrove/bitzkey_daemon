// A main service's pool of workers: the wire between them, the brains a worker holds, and whole worlds thinking
// through workers on spare local ports, as workers come, fail and go.

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <random>
#include <thread>

#include "core/WorldFile.hpp"
#include "daemon/Brains.hpp"
#include "daemon/Pool.hpp"
#include "daemon/Worker.hpp"
#include "daemon/World.hpp"

using namespace bitzkey;
using namespace bitzkey::daemon;

namespace {

std::filesystem::path folder_for(const std::string& name) {
    std::random_device device;
    const auto path = std::filesystem::temp_directory_path() / ("bitzkey-pool-test-" + name + "-" + std::to_string(device()));
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

// Wait, the world turning meanwhile, until `ready` holds, for up to `seconds`.
template <typename Ready>
bool await(Ready ready, double seconds = 15) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < until) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return ready();
}

Json worker_in(World& world, int port) {
    auto lock = world.lock();
    const Json list = world.pool().list();
    for (const auto& worker : list["workers"]) {
        if (worker["port"] == port) return worker;
    }
    return Json::object();
}

std::size_t living_brains(Simulation& simulation) {
    std::size_t count = 0;
    for (const Bitzkey* agent : simulation.living()) count += agent->brain != nullptr;
    return count;
}

}  // namespace

TEST_CASE("a batch and its answer cross the wire unchanged, but for floats") {
    PyRandom random(3);
    Batch batch;
    batch.session = 0x1234567890ABCDEFULL;
    batch.attaches.emplace_back(7, Brain::random(random, 3));
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
    CHECK(back.attaches[0].second.depth() == 3);
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
    const auto main_folder = folder_for("main");
    const auto first_folder = folder_for("first"), second_folder = folder_for("second");
    Worker first(first_folder / "worker.json"), second(second_folder / "worker.json");
    const int first_port = start_worker(first), second_port = start_worker(second);
    {
        World world(busy_world(), false, main_folder);
        {
            auto lock = world.lock();
            world.pool().add("127.0.0.1", first_port, "Thinking for https://example.org/");
            world.pool().add("127.0.0.1", second_port, "");
            world.adopt_pool();
            CHECK(world.pool().bound());
            CHECK(world.simulation().on_gpu());  // its brains elsewhere: no rewinding, as on the GPUs
        }
        const bool joined = await([&] { return worker_in(world, first_port)["state"] == "working" && worker_in(world, second_port)["state"] == "working"; });
        INFO([&] { auto lock = world.lock(); return world.pool().list().dump(); }());
        REQUIRE(joined);
        CHECK(first.status()["message"] == "Thinking for https://example.org/");
        CHECK(first.status()["main"] == "127.0.0.1");

        // A second main service is turned away while the first still works with it.
        {
            const auto other_folder = folder_for("other");
            {
                World other(busy_world(), false, other_folder);
                {
                    auto lock = other.lock();
                    other.pool().add("127.0.0.1", first_port, "");
                    other.adopt_pool();
                }
                CHECK(await([&] { return worker_in(other, first_port)["state"] == "refused"; }));
                CHECK(worker_in(other, first_port)["error"].get<std::string>().find("another main service") != std::string::npos);
            }
            std::filesystem::remove_all(other_folder);
        }

        // Turns, the brains shared out among the three.
        for (int turn = 0; turn < 30; ++turn) {
            auto lock = world.lock();
            world.step();
        }
        REQUIRE(await([&] { return worker_in(world, first_port)["brains"].get<int>() > 0 && worker_in(world, second_port)["brains"].get<int>() > 0; }));
        {
            auto lock = world.lock();
            const Json list = world.pool().list();
            std::size_t held = list["local"]["brains"].get<std::size_t>();
            for (const auto& worker : list["workers"]) held += worker["brains"].get<std::size_t>();
            CHECK(held == living_brains(world.simulation()));
            CHECK(world.pool().attached() == living_brains(world.simulation()));
        }

        // A save brings every brain back from the workers into the world file.
        {
            auto lock = world.lock();
            const auto path = main_folder / "pooled.bkw";
            WorldFile::save(world.simulation(), path);
            const auto loaded = WorldFile::load(path);
            REQUIRE(loaded->living().size() == world.simulation().living().size());
            for (std::size_t index = 0; index < loaded->living().size(); ++index) {
                const Bitzkey* saved = loaded->living()[index];
                const Bitzkey* live = world.simulation().living()[index];
                if (saved->brain) CHECK(saved->brain->output_biases == live->brain->output_biases);
            }
        }

        // The first worker fails outright: its brains go to the others, and the world turns on.
        first.stop(true, std::chrono::seconds(0));
        for (int turn = 0; turn < 10; ++turn) {
            auto lock = world.lock();
            world.step();
        }
        CHECK(await([&] { return worker_in(world, first_port)["brains"] == 0; }));
        {
            auto lock = world.lock();
            CHECK(world.pool().attached() == living_brains(world.simulation()));
            CHECK(world.simulation().turn() == 40);
        }

        // The second stops gracefully: its brains are handed back before it goes, the world turning meanwhile.
        world.play(true);
        const auto before = std::chrono::steady_clock::now();
        second.stop(true, std::chrono::seconds(20));
        CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(15));
        CHECK(worker_in(world, second_port)["brains"] == 0);
        world.play(false);
        {
            auto lock = world.lock();
            CHECK(world.pool().attached() == living_brains(world.simulation()));
            world.pool().remove("127.0.0.1", first_port);
            world.pool().remove("127.0.0.1", second_port);
            world.adopt_pool();
            CHECK_FALSE(world.pool().bound());
            const int turn = world.simulation().turn();
            world.step();
            CHECK(world.simulation().turn() == turn + 1);
        }
    }
    for (const auto& folder : {main_folder, first_folder, second_folder}) std::filesystem::remove_all(folder);
}

TEST_CASE("worker mode is kept between runs, and turned off for good when cancelled") {
    const auto folder = folder_for("kept");
    int port = 0;
    {
        Worker worker(folder / "worker.json");
        port = start_worker(worker);
        CHECK(worker.on());
        CHECK(worker.status()["shared"]["threads"] == 1);
    }  // the daemon stops: it is a worker again when it starts
    {
        Worker worker(folder / "worker.json");
        worker.resume();
        CHECK(worker.on());
        CHECK(worker.status()["port"] == port);
        Resources more = a_little();
        more.memory = std::uint64_t{512} << 20;
        worker.share(more);
        CHECK(worker.status()["shared"]["memory"] == more.memory);
        CHECK_THROWS_AS(worker.share(Resources{0, more.memory, {}}), std::invalid_argument);
        worker.stop(true, std::chrono::seconds(0));
        CHECK_FALSE(worker.on());
    }
    {
        Worker worker(folder / "worker.json");
        worker.resume();
        CHECK_FALSE(worker.on());
    }
    std::filesystem::remove_all(folder);
}
