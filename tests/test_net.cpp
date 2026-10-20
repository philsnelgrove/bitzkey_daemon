
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
