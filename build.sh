#!/usr/bin/env bash
# Builds bitzkey_daemon (Release) on Linux and runs the tests. Needs: sudo apt install build-essential cmake libtbb-dev
set -euo pipefail
cd "$(dirname "$0")"
cmake --preset linux
cmake --build --preset linux
./build-linux/bitzkey_tests
echo "Built $(pwd)/build-linux/bitzkey_daemon"
