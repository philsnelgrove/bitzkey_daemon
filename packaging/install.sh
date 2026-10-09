#!/usr/bin/env bash
# Builds bitzkey_daemon and installs it as a systemd service on Ubuntu, listening on port 49152.
#   sudo packaging/install.sh            build, install, enable and start it
#   sudo packaging/install.sh uninstall  stop it and take it away (its data in /var/lib is kept)
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ $EUID -ne 0 ]]; then
    echo "Run it with sudo: sudo $0 $*" >&2
    exit 1
fi

if [[ "${1:-}" == "uninstall" ]]; then
    systemctl disable --now bitzkey_daemon.service 2>/dev/null || true
    rm -f /etc/systemd/system/bitzkey_daemon.service /usr/local/bin/bitzkey_daemon
    systemctl daemon-reload
    echo "Removed bitzkey_daemon. Its settings (/etc/bitzkey_daemon.conf) and data (/var/lib/bitzkey_daemon) are left."
    exit 0
fi

missing=()
for tool in g++ cmake make; do command -v "$tool" >/dev/null || missing+=("$tool"); done
if (( ${#missing[@]} )); then
    echo "Installing the build tools: build-essential cmake libtbb-dev"
    apt-get update
    apt-get install -y build-essential cmake libtbb-dev
fi

# Build as whoever ran sudo, so the build folder isn't left owned by root.
builder="${SUDO_USER:-root}"
sudo -u "$builder" cmake --preset linux
sudo -u "$builder" cmake --build --preset linux
sudo -u "$builder" ./build-linux/bitzkey_tests

install -m 755 build-linux/bitzkey_daemon /usr/local/bin/bitzkey_daemon
[[ -f /etc/bitzkey_daemon.conf ]] || install -m 644 packaging/bitzkey_daemon.conf /etc/bitzkey_daemon.conf
install -m 644 packaging/bitzkey_daemon.service /etc/systemd/system/bitzkey_daemon.service
systemctl daemon-reload
systemctl enable bitzkey_daemon.service
systemctl restart bitzkey_daemon.service

port=$(sed -n 's/^[[:space:]]*port[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' /etc/bitzkey_daemon.conf | tail -1)
port=${port:-49152}
if command -v ufw >/dev/null && ufw status | grep -q "Status: active"; then
    ufw allow "$port"/tcp comment "bitzkey_daemon"
fi

sleep 1
systemctl --no-pager --lines=5 status bitzkey_daemon.service || true
echo
echo "bitzkey_daemon is running: open http://$(hostname -I | awk '{print $1}'):$port/ from any machine on the network."
echo "Its log: journalctl -u bitzkey_daemon -f"
