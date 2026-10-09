#pragma once
// The daemon's settings: where it listens, where it keeps its files, and how it thinks. Read from a
// config file of `key = value` lines (`#` starts a comment), then from the command line, which wins.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bitzkey::daemon {

struct Config {
    std::string host = "0.0.0.0";  // every network interface
    int port = 49152;
    std::filesystem::path data_dir;  // the catalog, exports and reports
    std::optional<bool> gpu;         // think on the GPU: unset uses one where there is one
    bool start_paused = false;       // a fresh world waits to be started, rather than running at once
    std::filesystem::path web_dir;   // serve the interface from these files, for working on it; empty: the built-in copy
};

// The usual place for the daemon's files: $STATE_DIRECTORY under systemd, else ~/.local/share/bitzkey_daemon
// (on Windows, %LOCALAPPDATA%\bitzkey_daemon).
std::filesystem::path default_data_dir();

// Read a config file into `config`; throws std::runtime_error naming the line on a bad one.
void read_config(const std::filesystem::path& path, Config& config);

// Apply the command-line options; returns false and fills `message` for --help or an error.
bool parse_arguments(const std::vector<std::string>& arguments, Config& config, std::string& message);

std::string usage();

}  // namespace bitzkey::daemon
