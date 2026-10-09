#pragma once
// The daemon's web server: the interface's files at /, and the world through /api. Every viewer sees
// and steers the same world; what each has selected, and its export list, stay in its own browser.

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace httplib {
class Server;
}

namespace bitzkey::daemon {

class World;

// The web interface's files, built into the program: each path (from web/) and its bytes.
const std::map<std::string, std::string_view>& web_assets();

// Set up every route on the server, for this world. The interface comes from `web_dir` when it is
// given, else from the copy built in.
void install_routes(httplib::Server& server, World& world, const std::filesystem::path& web_dir = {});

}  // namespace bitzkey::daemon
