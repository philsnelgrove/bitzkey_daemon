# Bitzkey daemon

The Bitzkey project was started years ago, to teach my best friend programming. He wanted to learn about Python and AI, so it seemed like a natural project. Since then, my friend has passed away. So I finished this to honour him. I open-sourced it for the same reason. So, Sam Bunn, wherever you are, may you live on through Bitzkeys!

This is the large-scale Linux service: the C++ simulation running around the clock on a server, shown to everyone as a web page. For reading the code and researching how Bitzkeys work, see the [Python version](https://github.com/philsnelgrove/bitzkey), the code research option; to run worlds on your own Windows computer, the [desktop app](https://github.com/philsnelgrove/bitzkey_cpp), the local option.

Download the package from the [latest release](https://github.com/philsnelgrove/bitzkey_daemon/releases/latest), unpack it on Ubuntu and run `sudo packaging/install.sh`, as described under Installing on Ubuntu below.

Bitzkey runs here as a service on Ubuntu Linux. It is the simulation from the C++ Bitzkey (`bitzkey-cpp`), and its interface is a web page at **http://<server>:49152/**.

There is one world, and everyone who opens the page sees and steers that same world. Each viewer's own choices stay in their browser:

- what they have selected;
- their export list;
- their exemplar cards.

The page has no password, and by default anyone on the network can open it.

## Installing on Ubuntu

```bash
git clone https://github.com/philsnelgrove/bitzkey_daemon.git
cd bitzkey_daemon
sudo packaging/install.sh
```

The script does the following:

1. Installs the build tools if they are missing: `build-essential`, `cmake` and `libtbb-dev`.
2. Builds the daemon and runs its tests.
3. Copies the program to `/usr/local/bin/bitzkey_daemon`.
4. Copies the settings to `/etc/bitzkey_daemon.conf`, unless that file is already there.
5. Starts `bitzkey.service` under systemd, so it runs at every boot.
6. If `ufw` is active, opens port 49152.

You can then open `http://<server>:49152/` from any machine on the network.

To work with the service:

| What | How |
|---|---|
| Show its log | `journalctl -u bitzkey -f` |
| Restart it | `sudo systemctl restart bitzkey` |
| Stop it | `sudo systemctl stop bitzkey` |
| Update it | `git pull`, then `sudo packaging/install.sh` again |
| Remove it | `sudo packaging/install.sh uninstall`; this leaves its settings and data |

The service runs as a user of its own and keeps its files in `/var/lib/bitzkey_daemon`:

- the catalog, `catalog.json`;
- saved exports, in `exports/`;
- brain reports, in `reports/`;
- saved worlds, in `worlds/`. Brains are most of a world file, at about 120 KB an animal, so a busy world can take
  hundreds of megabytes; delete old ones with DELETE in LOAD WORLD's list, or from here by hand.

To think on the GPU it needs a Vulkan driver, which the NVIDIA and Mesa drivers include. It reaches the GPU through the `render` and `video` groups. Without a GPU it thinks on the CPU. If the GPU fails while the world runs, most often by running out of memory as a
world grows (the integrated GPUs of older machines have little of their own), the world carries on thinking on the CPU
and the header says why, rather than the daemon stopping. Any other failure of the world's thread pauses the world and
is logged (`journalctl -u bitzkey`), and the daemon keeps serving it.

## Settings

`/etc/bitzkey_daemon.conf` holds `key = value` lines. A `#` starts a comment. Options on the command line override the file.

| Setting | Option | Default | |
|---|---|---|---|
| `host` | `--host` | `0.0.0.0` | Where it listens. `0.0.0.0` means every network interface; `127.0.0.1` means this machine alone. |
| `port` | `--port` | `49152` | The port for the page. |
| `data_dir` | `--data-dir` | see below | Where it keeps the catalog, exports, reports and saved worlds. |
| `gpu` | `--gpu` | `auto` | `yes`, `no`, or `auto` (use a GPU where there is one). |
| `start_paused` | `--start-paused` | `no` | Start the first world paused. |
| `web_dir` | `--web-dir` | built in | Serve the page from these files, for working on it. |

Under systemd the data folder is `/var/lib/bitzkey_daemon`. Run by hand, it is `~/.local/share/bitzkey_daemon`.

Other options:

- `--config FILE` reads settings from another file.
- `--version` prints the version.
- `--help` lists every option.

## The page

The page has the window app's screens, laid out the same way:

- **The map.** The world, with the exemplar cards on either side of it and the sidebar on the right. Each card's title is a menu of statistics.
- **The colour graph** (G). It can travel back in time, zoom, sort, and pick Bitzkeys, and Tab shows the ring (body), dot (mind) or social background colours.
- **New world** (ESC or NEW WORLD). This opens:
  - the settings;
  - the import box, for pasting, loading a file, the catalog or the designer;
  - the advanced rules;
  - SAVE WORLD, which saves the running world first, and LOAD WORLD, which carries on a saved world instead.
  Starting a new world, or loading one, replaces the running world for every viewer.
- **SAVE WORLD**, in the footer. Saves the whole world on the daemon under a name (letters, digits, - and _), for everyone
  using it to load later from NEW WORLD. A loaded world comes back paused at the turn it was saved, with its own rules,
  and plays on exactly as it would have. World files are the same as the window apps', in either version, so a world
  saved here can be copied out of `worlds/` and loaded there, and back. DELETE, beside each world in LOAD WORLD's list,
  deletes it for everyone, once confirmed.
- Omnivores live alone, in neither herd nor pack (the Solitary omnivores rule), and are cowards that always flee when struck (Cowardly omnivores); grazers herd and predators run in packs, and predators are never overcome by primal fear.
- **DEATH REPORT**, on the insights panel: every death back to the world's start, by cause and species, who killed whom, what the killers were doing (hunting, attacking, defending, browsing, uprooting, spines), and the ten deadliest killers. The graph's hover says what each killer was doing too.
- **INSPECT BRAINS**, beside a selected Bitzkey's heading: pauses the world, for everyone, and shows its chance of choosing each action in each standard situation.
- **ADVANCED**, in the footer. Pauses the world, for everyone, and opens its advanced rules; applied, the world plays by them from its next turn, and so do its restarts. Think on the GPU stays as the world started. The button is amber while the world plays by changed rules.
- **Square popup**: hovering over a map square lists the Bitzkeys stacked in it (a square holds 10 spaces, each Bitzkey taking one plus its size); the list stays while the pointer is on it, and clicking a name selects that Bitzkey.
- **DEPLOY**, in the catalog opened from the map. Closes it and turns the cursor into a target: each click on the map puts 5 of each Bitzkey chosen around that cell, and dragging paints a group every 5 cells, until a right-click or ESC. Everyone sees them join the shared world.
- **OPEN CATALOG**, in the footer and on the colour graph. Looks through the catalog without importing or exporting anything; REMOVE still works, and DESIGNER opens the designer on the entry picked, a design applied joining the catalog as a new entry (its genome alone).
- **Selected Bitzkeys** wear a target's sights on the map, swelling to twice their size and back every two seconds, paused or not.
- **Colour graph popup**: hovering a slice shows the Bitzkey's details, the dead's as far as they are known: their genome, and how and when they died.
- **FILES.** Downloads exports the daemon has saved. Brain reports (B) open in a new tab.

### Keys and mouse

| Input | What it does |
|---|---|
| SPACE | Pause or run |
| - / + | Slower or faster |
| LEFT / RIGHT | A turn back or on |
| Wheel, right-drag, Z | Zoom, pan, and back to the whole map; on the graph, zoom time and height around the pointer, pan both, and show it all again (a right-click without dragging moves the world to that turn) |
| Click | Select a Bitzkey |
| Drag | Select every Bitzkey in a box |
| CTRL+click | Add or take one out |
| Click on nothing | Select nothing |
| I | Insights |
| B | Brain report |
| G | Colour graph |
| C | Copy the selected Bitzkey |
| A | Add to export |
| E | Export list |
| K | Add to catalog |
| R | Restart |

Browsers only give a page the clipboard over HTTPS or from the same machine. Over plain HTTP on the network, the page offers the text to copy by hand when the clipboard is blocked.

The page gives up on any request the daemon hasn't answered within 10 seconds and shows the daemon as lost until it answers again, rather than freezing.

## Workers

One daemon can share its world's thinking with others. The daemon running the world is the main service. The others are **workers**: daemons in worker mode, which run no world of their own. Each worker holds some of the main service's brains, and decides and learns for them. Deciding and learning are most of a turn's work: on the CPU, learning alone takes about three quarters of it.

**Becoming a worker.** On the computer that is to help, open its daemon's page, then **WORKERS** and **BECOME A WORKER...**. Choose:

- the port to listen on for the main service (default 50000);
- how many cores to share;
- how much memory to share for brains;
- which GPUs to share, if any.

The daemon pauses its world, and its page then shows only a gray panel. The panel says it is a worker, gives the main service's IP address and its message, and holds:

- the shared cores, memory and GPUs, which can be changed at any time;
- **STOP BEING A WORKER**.

Worker mode lasts through restarts. Several workers on one computer each need a port of their own. The main service needs no open port for its workers: it connects out to each of them.

The worker's port must be open to the main service, and this is automatic where `install.sh` installed the daemon and `ufw` is active. The daemon can't change the firewall itself, as it runs with no privileges. Instead, it writes the ports it listens on to `worker-ports` in its data folder, and the systemd unit `bitzkey-worker-ports.path` runs `bitzkey-worker-ports` as root whenever that list changes. That script opens the listed ports in `ufw` and closes those it opened before. A port is therefore opened when worker mode turns on, and closed when it turns off for good. A home router in front of the worker still needs that port forwarded to it by hand.

The port can be changed from the worker's panel at any time. The worker listens on both ports until its main service, told the new one, follows it there, and then lets the old one go. No brains are lost in the move.

**Adding workers.** On the main service, open **WORKERS** and add each worker by its IP address. The port is 50000 unless you give another, either in the port box or after the address (`192.168.1.20:50001`). You can also give a message to show on the worker's panel; a link in the message becomes clickable there. The list shows, for each worker:

- its state;
- its ping time;
- what it shares;
- its measured speed;
- how many brains it holds.

There can be any number of workers. When the first is added, the world starts thinking through the pool. As on the GPU, it then keeps no history to rewind. Its header shows how many workers are working.

**How the brains are shared.** Each new brain goes to the worker with the most room for it for its speed, and the main service keeps a share itself. Each turn, every worker is sent one batch, all of them at once:

1. the brains it is to take on;
2. the lessons its brains are to learn;
3. the senses to value.

Its values come back in the same answer. A worker keeps the recent senses of each brain it holds, so a lesson replayed from a memory names those senses rather than sending them again.

Every 100 turns (set under Checkpoints), the main service brings every brain back, and evens out the shares if a worker has joined or changed what it shares.

**When workers stop.**

- **A worker that fails:** its brains go to the others as they were at the last checkpoint, and the world carries on. A worker that misses three pings in a row, or that restarted without warning, counts as failed.
- **A worker that stops gracefully:** its brains are handed back first, and the world turns on meanwhile. This applies to STOP BEING A WORKER, a `systemctl stop` or a restart.
- **A worker you remove:** its brains are brought back first too.

**Saving.** A world saved on the main service brings every brain back from its workers first, so the file holds them all.

**Security.** A worker serves one main service at a time, and turns away any other while its own is still in touch. Like the page, the worker port has no password, so keep workers on a network you trust.

The [Windows desktop app](https://github.com/philsnelgrove/bitzkey_cpp) can fill either role too: it can be a worker for this daemon, or a main service with this daemon among its workers. The two share the same code for workers, in `src/net/`.

Workers speak to the main service in a binary protocol over HTTP, and a worker and its main service must speak the same version of it. A worker that speaks another version is listed as refused.

## Building for development

On Linux:

```bash
sudo apt install build-essential cmake libtbb-dev
./build.sh
```

On Windows, with Visual Studio Build Tools:

```powershell
.\build.ps1
```

Both builds run the tests. On Windows, ports 49152–49251 are often reserved by Hyper-V and WSL, so run the Windows build on another port:

```powershell
build\Release\bitzkey_daemon.exe --host 127.0.0.1 --port 18452 --web-dir web
```

With `--web-dir web`, edits to the page show up on a reload, without rebuilding.

## Layout

| Path | Contents |
|---|---|
| `src/core/` | The simulation, copied from `bitzkey-cpp` (see below) |
| `src/daemon/World.*` | The world, turning on its own thread at the chosen speed, behind a lock the web requests share; a waiting request goes before the next slice of turns, so even at MAX speed the page never stalls |
| `src/daemon/Views.*` | What the page is shown: JSON for the panels, and a binary frame for the map |
| `src/daemon/Graph.*` | The colour graph, drawn on the server, which can hold millions of slices |
| `src/daemon/Api.*` | The HTTP routes |
| `src/daemon/Config.*` | Settings from the file and the command line |
| `src/net/Pool.*` | The main service's pool of workers: sharing brains out, polling, checkpoints, failures and hand-backs |
| `src/net/Worker.*` | Worker mode: the port a worker listens on for its main service, and its settings |
| `src/net/Brains.*` | The brains a worker holds, the batches it is sent, and brains on the CPU's threads |
| `src/net/Wire.hpp` | The binary form brains, senses and values take between them |
| `web/` | The page: plain HTML, CSS and JavaScript modules, built into the program |
| `packaging/` | The systemd service, the sample settings and the install script |

`src/core/`, `src/net/`, `shaders/` and the core's tests are copied from `bitzkey-cpp` and should be kept in step with it. When the simulation changes there, copy those folders again. `src/core/shaders/*.spv.h` are compiled from `shaders/` by `tools/compile_shaders.ps1`, which needs the Vulkan SDK.

The version is in `VERSION`.

## Licence

Copyright (C) 2026 Phil Snelgrove. Bitzkey is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License, version 3 or (at your option) any later version, as published by the Free
Software Foundation. It is distributed in the hope that it will be useful, but without any warranty; see `LICENSE`.
