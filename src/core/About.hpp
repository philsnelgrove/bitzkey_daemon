#pragma once
// What every screen shows at its top: the version running, and the processor cores it thinks on.

#include <algorithm>
#include <string>
#include <thread>

namespace bitzkey {

// The cores the simulation works on: every hardware thread, as sensing and making brains run on all of them at once.
inline int cores_used() { return std::max(1, static_cast<int>(std::thread::hardware_concurrency())); }

// "v3.46.0  /  16 CORES"
inline std::string about() {
    const int cores = cores_used();
    return std::string("v") + BITZKEY_VERSION + "  /  " + std::to_string(cores) + (cores == 1 ? " CORE" : " CORES");
}

}  // namespace bitzkey
