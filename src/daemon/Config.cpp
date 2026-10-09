#include "daemon/Config.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace bitzkey::daemon {

namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string environment(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || !value) return {};
    std::string found(value);
    std::free(value);
    return found;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

bool truth(const std::string& value, const std::string& key) {
    if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
    if (value == "0" || value == "false" || value == "no" || value == "off") return false;
    throw std::runtime_error(key + " must be yes or no, not " + value);
}

void set(Config& config, const std::string& key, const std::string& value) {
    if (key == "host") config.host = value;
    else if (key == "port") {
        const int port = std::stoi(value);
        if (port < 1 || port > 65535) throw std::runtime_error("port must be between 1 and 65535");
        config.port = port;
    } else if (key == "data_dir") config.data_dir = value;
    else if (key == "gpu") {
        if (value == "auto") config.gpu.reset();
        else config.gpu = truth(value, key);
    } else if (key == "start_paused") config.start_paused = truth(value, key);
    else if (key == "web_dir") config.web_dir = value;
    else throw std::runtime_error("unknown setting " + key);
}

}  // namespace

std::filesystem::path default_data_dir() {
    if (const std::string state = environment("STATE_DIRECTORY"); !state.empty()) return state;  // systemd's
#ifdef _WIN32
    if (const std::string local = environment("LOCALAPPDATA"); !local.empty()) return std::filesystem::path(local) / "bitzkey_daemon";
#else
    if (const std::string data = environment("XDG_DATA_HOME"); !data.empty()) return std::filesystem::path(data) / "bitzkey_daemon";
    if (const std::string home = environment("HOME"); !home.empty()) return std::filesystem::path(home) / ".local/share/bitzkey_daemon";
#endif
    return std::filesystem::current_path() / "bitzkey_daemon_data";
}

void read_config(const std::filesystem::path& path, Config& config) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("can't read the config file " + path.string());
    std::string line;
    int number = 0;
    while (std::getline(file, line)) {
        ++number;
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) throw std::runtime_error(path.string() + ":" + std::to_string(number) + ": expected key = value");
        try {
            set(config, trim(line.substr(0, equals)), trim(line.substr(equals + 1)));
        } catch (const std::exception& error) {
            throw std::runtime_error(path.string() + ":" + std::to_string(number) + ": " + error.what());
        }
    }
}

std::string usage() {
    return "bitzkey_daemon: Bitzkey as a service, its interface served as a web page.\n"
           "  --config FILE      read settings from FILE (key = value lines)\n"
           "  --host ADDRESS     listen on ADDRESS (default 0.0.0.0, every interface)\n"
           "  --port N           listen on port N (default 49152)\n"
           "  --data-dir DIR     keep the catalog, exports and reports in DIR\n"
           "  --gpu yes|no|auto  think on the GPU (default auto: where there is one)\n"
           "  --start-paused     start the first world paused\n"
           "  --web-dir DIR      serve the web interface from DIR rather than the built-in copy\n"
           "  --version          say which version this is\n"
           "  --help             show this\n";
}

bool parse_arguments(const std::vector<std::string>& arguments, Config& config, std::string& message) {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& flag = arguments[index];
        if (flag == "--help" || flag == "-h") {
            message = usage();
            return false;
        }
        if (flag == "--version") {
            message = std::string("bitzkey_daemon ") + BITZKEY_VERSION + "\n";
            return false;
        }
        if (flag == "--start-paused") {
            config.start_paused = true;
            continue;
        }
        if (index + 1 >= arguments.size()) {
            message = "missing value for " + flag + "\n" + usage();
            return false;
        }
        const std::string& value = arguments[++index];
        try {
            if (flag == "--config") read_config(value, config);
            else if (flag == "--host") set(config, "host", value);
            else if (flag == "--port") set(config, "port", value);
            else if (flag == "--data-dir") set(config, "data_dir", value);
            else if (flag == "--gpu") set(config, "gpu", value);
            else if (flag == "--web-dir") set(config, "web_dir", value);
            else {
                message = "unknown option " + flag + "\n" + usage();
                return false;
            }
        } catch (const std::exception& error) {
            message = std::string(error.what()) + "\n";
            return false;
        }
    }
    return true;
}

}  // namespace bitzkey::daemon
