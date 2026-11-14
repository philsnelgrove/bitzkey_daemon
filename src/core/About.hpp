#pragma once
// What every screen shows at its top: the version running, and the processor cores it thinks on.

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>

namespace bitzkey {

// Every hardware thread the computer has.
inline int hardware_cores() { return std::max(1, static_cast<int>(std::thread::hardware_concurrency())); }

// What the configuration panel offers at first: three quarters of them, rounded, leaving the rest for the computer's other work.
inline int default_cores() { return std::max(1, (hardware_cores() * 3 + 2) / 4); }

// The cores chosen for the simulation, 0 for all of them. A world unfolds the same on any number.
inline std::atomic<int>& chosen_cores() {
    static std::atomic<int> chosen{0};
    return chosen;
}

// The cores the simulation works on: those chosen, else every hardware thread.
inline int cores_used() {
    const int chosen = chosen_cores().load();
    return chosen > 0 ? std::min(chosen, hardware_cores()) : hardware_cores();
}

// "v3.46.0  /  16 CORES"
inline std::string about() {
    const int cores = cores_used();
    return std::string("v") + BITZKEY_VERSION + "  /  " + std::to_string(cores) + (cores == 1 ? " CORE" : " CORES");
}

}  // namespace bitzkey
