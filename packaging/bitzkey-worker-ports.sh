#!/usr/bin/env bash
# Opens in ufw the ports a Bitzkey worker listens on for its main service, and closes those it no longer does.
# The daemon can't change the firewall itself (it runs as a user of its own, with no privileges): it writes the ports
# it listens on to worker-ports in its data folder, empty when it isn't a worker, and bitzkey-worker-ports.path runs
# this, as root, whenever that file changes. Nothing is done unless ufw is installed and active.
#   BITZKEY_WORKER_PORTS  the daemon's list (default /var/lib/bitzkey_daemon/worker-ports)
#   BITZKEY_PORTS_OPENED  what this opened last time (default /var/lib/bitzkey-worker-ports/opened)
set -euo pipefail

wanted_file="${BITZKEY_WORKER_PORTS:-/var/lib/bitzkey_daemon/worker-ports}"
opened_file="${BITZKEY_PORTS_OPENED:-/var/lib/bitzkey-worker-ports/opened}"

if ! command -v ufw >/dev/null || ! ufw status | grep -q "Status: active"; then
    exit 0
fi

# Ports alone: the list is written by the daemon's own user, so anything else in it is ignored.
ports() {
    [[ -f "$1" ]] || return 0
    tr -s ' \t\n' '\n' < "$1" | grep -E '^[0-9]{1,5}$' | awk '$1 >= 1 && $1 <= 65535' | sort -un
}

wanted=$(ports "$wanted_file")
opened=$(ports "$opened_file")

for port in $opened; do
    if ! grep -qx "$port" <<< "$wanted"; then
        ufw delete allow "$port"/tcp >/dev/null && echo "closed port $port (no longer a Bitzkey worker's)"
    fi
done
for port in $wanted; do
    if ! grep -qx "$port" <<< "$opened"; then
        ufw allow "$port"/tcp comment "bitzkey worker" >/dev/null && echo "opened port $port for a Bitzkey worker"
    fi
done

mkdir -p "$(dirname "$opened_file")"
printf '%s\n' $wanted > "$opened_file"
