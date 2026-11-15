# PC Stats agent (Windows)

240-MP on the Pi reads two things from the gaming PC:

- **LibreHardwareMonitor** at `http://<pc>:8085/data.json` (CPU, GPU, fans, RAM, drives, network). The Pi parses that tree itself.
- **This script** at `http://<pc>:8086/stats.json` (frame times and FPS for whatever game is in the foreground). PresentMon is the only source for frame times. LibreHardwareMonitor does not have them.

Hardware stats still show if this script is not running. The frame graph then says there is no frame data.

James's PC is `10.0.0.44` (`James5080`). That address is the default in 240-MP. Change it in `config.json` if the PC moves. See [docs/PC-STATS.md](../docs/PC-STATS.md).

## 1. LibreHardwareMonitor

1. Install [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases) and start it **as administrator** (sensor drivers need that).
2. Options → **Remote Web Server** → enable it. Port **8085**. Leave the default.
3. Options → **Run on Windows startup**, still elevated.
4. Allow the port through the firewall, from an elevated PowerShell:

```powershell
netsh advfirewall firewall add rule name="LibreHardwareMonitor" dir=in action=allow protocol=TCP localport=8085
```

Check from another machine: `http://10.0.0.44:8085/data.json` should be a JSON tree, not a connection error.

## 2. PresentMon

1. Download the console build from [PresentMon releases](https://github.com/GameTechDev/PresentMon/releases) (the `PresentMon-*-x64.exe` asset, not the GUI overlay).
2. Put that exe in this folder, next to `pc-stats-agent.ps1`. Any name starting with `PresentMon` and ending in `.exe` is found. Or set `presentmon_path` in `pc-stats.config.json`.

The agent runs it as:

```text
PresentMon.exe --process_name game.exe --output_stdout --no_console_stats --v1_metrics --stop_existing_session --session_name 240mp-pc-stats --exclude_dropped --terminate_on_proc_exit
```

`--v1_metrics` is required on PresentMon 2.6: the default CSV does not include `MsBetweenPresents`. The reader also accepts `FrameTime` and `MsBetweenAppStart` (the 2.x names for the same interval) if a build ignores that flag.

Run the window **as administrator**. The title bar should read **Administrator:**. PresentMon's ETW session fails with access denied (exit code 6) otherwise, unless your account is in Performance Log Users. The agent prints that warning at startup, and `/stats.json` then has `"ok": false` plus the exit code and PresentMon's stderr.

It follows the foreground window, skips the shell (Explorer, Steam, the terminal), and keeps the last game for 15 seconds so a short alt-tab does not wipe the graph. Frame time is the `MsBetweenPresents` column. If a game presents on more than one swap chain, the chain with the most samples in the window is the one that is published.

`/stats.json` looks like:

```json
{
  "ok": true,
  "process": "game.exe",
  "fps_avg": 116.4,
  "fps_1pct_low": 87.2,
  "frame_ms": [8.1, 8.4, 16.7]
}
```

`fps_avg` is `1000 / mean(frame time)` over the recent window (up to 600 frames). `fps_1pct_low` is `1000 / mean(the slowest 1% of those frames)`, at least one frame. `frame_ms` is the newest slice of that window for the graph (160 points).

## 3. Install the agent

From an **elevated** PowerShell, in this folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\pc-stats-agent.ps1 -Install
```

`-Install` does three things and then exits:

- Firewall rule **240-MP PC Stats** for TCP **8086**
- URL reservation `http://+:8086/` so the listener can bind
- Scheduled task **240-MP PC Stats** for the current user only, at logon, highest privileges, hidden window, working directory set to this folder. It does not die after 72 hours, it is allowed on battery, and it restarts up to 3 times, one minute apart. Stdout and stderr are appended to `pc-stats-agent.log` in this folder. The previous `schtasks` task exited immediately with code 1 and left no log.

The PowerShell you use for `-Install` and for a manual start must be elevated (title bar **Administrator:**).

Start it once without logging off:

```powershell
powershell -ExecutionPolicy Bypass -File .\pc-stats-agent.ps1
```

Then open `http://10.0.0.44:8086/stats.json`. With no game in front it is `"process": ""` and an empty `frame_ms`. Launch a game and the samples should show up within a couple of seconds.

Optional `pc-stats.config.json` next to the script:

```json
{
  "listen_port": 8086,
  "presentmon_path": "C:\\Tools\\PresentMon-2.3.1-x64.exe",
  "sample_window": 600,
  "graph_points": 160
}
```

If you change `listen_port`, run `-Install` again and set the same `frames_port` in 240-MP's `config.json`.
