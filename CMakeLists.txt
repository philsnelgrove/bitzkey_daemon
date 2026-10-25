cmake_minimum_required(VERSION 3.25)
# The version lives in the VERSION file.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BITZKEY_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)
project(BitzkeyDaemon VERSION ${BITZKEY_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# The Vulkan headers, for brains on the GPU: Vulkan itself is loaded when the program runs.
FetchContent_Declare(vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.296.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# The web server: one header.
FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json doctest vulkan_headers httplib)

find_package(Threads REQUIRED)

if(MSVC)
    # Strict IEEE floating point (no contraction into fused multiply-adds), as the simulation needs.
    add_compile_options(/W4 /permissive- /fp:precise /fp:contract- /utf-8 /bigobj)
else()
    add_compile_options(-Wall -Wextra -ffp-contract=off)
endif()

# The simulation core, copied from the C++ Bitzkey (bitzkey-cpp).
add_library(bitzkey_core STATIC
    src/core/PyRandom.cpp
    src/core/PyFormat.cpp
    src/core/Genome.cpp
    src/core/Brain.cpp
    src/core/Insights.cpp
    src/core/Census.cpp
    src/core/Exchange.cpp
    src/core/Simulation.cpp
    src/core/Rules.cpp
    src/core/Catalog.cpp
    src/core/GpuBrains.cpp
    src/core/WorldFile.cpp
)
target_include_directories(bitzkey_core PUBLIC src)
target_link_libraries(bitzkey_core PUBLIC nlohmann_json::nlohmann_json Threads::Threads PRIVATE Vulkan::Headers ${CMAKE_DL_LIBS})
if(NOT MSVC)
    # Parallel algorithms (std::execution::par) need Intel's TBB with GCC; without it they run in order.
    find_package(TBB QUIET)
    if(TBB_FOUND)
        target_link_libraries(bitzkey_core PUBLIC TBB::tbb)
    endif()
endif()

# The web interface, built into the daemon so it needs no files beside it.
file(GLOB_RECURSE BITZKEY_WEB_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/*")
set(BITZKEY_WEB_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp")
add_custom_command(
    OUTPUT "${BITZKEY_WEB_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -DWEB_DIR=${CMAKE_CURRENT_SOURCE_DIR}/web -DOUTPUT=${BITZKEY_WEB_SOURCE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_web.cmake
    DEPENDS ${BITZKEY_WEB_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_web.cmake
    COMMENT "Building the web interface into the daemon")

# Workers: a main service's pool, and worker mode. Copied to and from bitzkey-cpp, as the core is.
add_library(bitzkey_net STATIC
    src/net/Brains.cpp
    src/net/Pool.cpp
    src/net/Probe.cpp
    src/net/ProbeServer.cpp
    src/net/Worker.cpp
)
target_compile_definitions(bitzkey_net PRIVATE BITZKEY_VERSION="${BITZKEY_VERSION}")
target_link_libraries(bitzkey_net PUBLIC bitzkey_core httplib::httplib)

add_executable(bitzkey_daemon
    src/daemon/main.cpp
    src/daemon/Config.cpp
    src/daemon/World.cpp
    src/daemon/Api.cpp
    src/daemon/Views.cpp
    src/daemon/Graph.cpp
    "${BITZKEY_WEB_SOURCE}"
)
target_include_directories(bitzkey_daemon PRIVATE src)
target_compile_definitions(bitzkey_daemon PRIVATE BITZKEY_VERSION="${BITZKEY_VERSION}")
target_link_libraries(bitzkey_daemon PRIVATE bitzkey_net)

# Unit tests: the core's, from bitzkey-cpp, and the daemon's own.
add_executable(bitzkey_tests
    tests/test_main.cpp
    tests/test_random.cpp
    tests/test_genome.cpp
    tests/test_brain.cpp
    tests/test_simulation.cpp
    tests/test_exchange.cpp
    tests/test_rules.cpp
    tests/test_climate.cpp
    tests/test_catalog.cpp
    tests/test_gpu.cpp
    tests/test_worldfile.cpp
    tests/test_daemon.cpp
    tests/test_pool.cpp
    tests/test_probe.cpp
    tests/test_sea.cpp
    tests/test_day.cpp
    src/daemon/Config.cpp
    src/daemon/World.cpp
    src/daemon/Api.cpp
    src/daemon/Views.cpp
    src/daemon/Graph.cpp
    "${BITZKEY_WEB_SOURCE}"
)
target_include_directories(bitzkey_tests PRIVATE src)
target_compile_definitions(bitzkey_tests PRIVATE BITZKEY_VERSION="${BITZKEY_VERSION}")
target_link_libraries(bitzkey_tests PRIVATE bitzkey_net doctest::doctest)

include(GNUInstallDirs)
install(TARGETS bitzkey_daemon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
