// bitzkey_daemon: one Bitzkey world, run as a service, its interface served as a web page.

#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <httplib.h>

#include "core/GpuBrains.hpp"
#include "core/Rules.hpp"
#include "core/Simulation.hpp"
#include "daemon/Api.hpp"
#include "daemon/Config.hpp"
#include "daemon/World.hpp"

using namespace bitzkey;
using namespace bitzkey::daemon;

namespace {
httplib::Server* running = nullptr;

extern "C" void stop(int) {
    if (running) running->stop();  // the world closes as main returns
}
}  // namespace

int main(int argc, char** argv) {
    Config config;
    config.data_dir = default_data_dir();
    std::string message;
    if (!parse_arguments(std::vector<std::string>(argv + 1, argv + argc), config, message)) {
        const bool asked = message.rfind("bitzkey_daemon", 0) == 0;  // --help or --version
        (asked ? std::cout : std::cerr) << message;
        return asked ? 0 : 2;
    }
    try {
        remove_stale_histories();  // left behind by an earlier version's runs
        // Think on the GPU where there is one, unless told otherwise; before the rules' defaults are taken.
        const bool gpu = config.gpu.value_or(true) && gpu::available();
        USE_GPU = gpu ? 1 : 0;
        if (config.gpu.value_or(false) && !gpu) std::cerr << "bitzkey_daemon: no GPU found; thinking on the CPU\n";
        rules::defaults();

        World world(Simulation::Settings{}, !config.start_paused, config.data_dir);
        httplib::Server server;
        install_routes(server, world, config.web_dir);
        if (!server.bind_to_port(config.host, config.port)) {
            std::cerr << "bitzkey_daemon: can't listen on " << config.host << ":" << config.port << "\n";
            return 1;
        }
        running = &server;
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        std::cout << "bitzkey_daemon " << BITZKEY_VERSION << ": serving http://" << config.host << ":" << config.port
                  << "/  (data in " << config.data_dir.string() << ")" << std::endl;
        {
            auto lock = world.lock();
            if (!world.simulation().gpu_status().empty()) std::cout << "  " << world.simulation().gpu_status() << std::endl;
        }
        world.worker().resume();  // a worker when it last stopped: a worker again
        server.listen_after_bind();
        running = nullptr;
        std::cout << "bitzkey_daemon: stopping" << std::endl;
        if (world.worker().on()) {  // its brains handed back to its main service first, while it waits
            std::cout << "bitzkey_daemon: handing the brains back to the main service" << std::endl;
            world.worker().stop(false, std::chrono::seconds(20));
        }
    } catch (const std::exception& error) {
        std::cerr << "bitzkey_daemon: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
