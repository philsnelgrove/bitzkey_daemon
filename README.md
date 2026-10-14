# Bitzkey daemon

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
- brain reports, in `reports/`.

To think on the GPU it needs a Vulkan driver, which the NVIDIA and Mesa drivers include. It reaches the GPU through the `render` and `video` groups. Without a GPU it thinks on the CPU.

## Settings

`/etc/bitzkey_daemon.conf` holds `key = value` lines. A `#` starts a comment. Options on the command line override the file.

| Setting | Option | Default | |
|---|---|---|---|
| `host` | `--host` | `0.0.0.0` | Where it listens. `0.0.0.0` means every network interface; `127.0.0.1` means this machine alone. |
| `port` | `--port` | `49152` | The port for the page. |
| `data_dir` | `--data-dir` | see below | Where it keeps the catalog, exports and reports. |
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
  - the advanced rules.
  Starting a new world replaces the running world for every viewer.
- **DEATH REPORT**, on the insights panel: every death back to the world's start, by cause and species, who killed whom, what the killers were doing (hunting, attacking, defending, browsing, uprooting, spines), and the ten deadliest killers. The graph's hover says what each killer was doing too.
- **INSPECT BRAINS**, beside a selected Bitzkey's heading: pauses the world, for everyone, and shows its chance of choosing each action in each standard situation.
- **ADVANCED**, in the footer. Pauses the world, for everyone, and opens its advanced rules; applied, the world plays by them from its next turn, and so do its restarts. Think on the GPU stays as the world started. The button is amber while the world plays by changed rules.
- **Square popup**: hovering over a map square lists the Bitzkeys stacked in it (a square holds 10 spaces, each Bitzkey taking one plus its size); the list stays while the pointer is on it, and clicking a name selects that Bitzkey.
- **DEPLOY**, in the catalog opened from the map. Closes it and turns the cursor into a target: each click on the map puts 5 of each Bitzkey chosen around that cell, and dragging paints a group every 5 cells, until a right-click or ESC. Everyone sees them join the shared world.
- **OPEN CATALOG**, in the footer and on the colour graph. Looks through the catalog without importing or exporting anything; REMOVE still works.
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
| `web/` | The page: plain HTML, CSS and JavaScript modules, built into the program |
| `packaging/` | The systemd service, the sample settings and the install script |

`src/core/`, `shaders/` and the core's tests are copied from `bitzkey-cpp` and should be kept in step with it. When the simulation changes there, copy those folders again. `src/core/shaders/*.spv.h` are compiled from `shaders/` by `tools/compile_shaders.ps1`, which needs the Vulkan SDK.

The version is in `VERSION`.
