// Every action's name (shown on the insights panel) and every activity an animal is shown doing (in the inspector)
// has a translation in every language's catalog.

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "core/Brain.hpp"

using namespace bitzkey;

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE("every action and activity shown is translated in every language") {
    std::set<std::string> shown;
    for (const std::string_view action : ACTIONS) {
        std::string name(action);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        shown.insert(name);
    }
    const std::string simulation = read(std::filesystem::path(BITZKEY_SOURCE_DIR) / "src/core/Simulation.cpp");
    constexpr std::string_view SET = "last_action = \"";  // searched by hand: std::regex overflows the stack on a file this long
    for (std::size_t at = simulation.find(SET); at != std::string::npos; at = simulation.find(SET, at)) {
        at += SET.size();
        const std::string text = simulation.substr(at, simulation.find('"', at) - at);
        if (!text.empty() && text.back() != ' ') shown.insert(text);  // not the start of words joined on
    }
    REQUIRE(shown.size() > ACTIONS.size());

    int catalogs = 0;
    for (const auto& entry : std::filesystem::directory_iterator(BITZKEY_I18N_DIR)) {
        if (entry.path().extension() != ".json") continue;
        ++catalogs;
        std::set<std::string> keys;  // looked up regardless of case
        const nlohmann::json catalog = nlohmann::json::parse(read(entry.path()));
        for (const auto& [key, value] : catalog.items()) keys.insert(lower(key));
        for (const std::string& text : shown) {
            INFO(entry.path().filename().string() << ": " << text);
            CHECK(keys.contains(lower(text)));
        }
    }
    CHECK(catalogs == 11);
}
