# PC Stats

PC Stats is a main-menu row directly under Plex. The other rows stay in their existing order. It is a screen for the gaming PC on the LAN. It polls two local services on that PC and draws the result. It does not run during playback, and it does not poll while the screen is closed.

The default PC is **10.0.0.44** (James5080, static LAN address). Tailscale works the same way if you put that address in the config instead: the Pi just needs a route to the host.

## What the screen shows

Left side: CPU temperature, load, average core clock, and package power; RAM used/total and load; fan speeds; drive temperatures; the busiest network adapter's download and upload.

Right side: GPU core temperature and hotspot, load, core clock, and power; memory clock and VRAM used/total; the foreground game's average FPS and 1% low; a frame-time line. Spikes go up. The scale always includes at least a 33 ms (30 fps) line so a stutter is visible.

The screen is black with light text, inset about 8% so a 4:3 SD CRT does not clip the left edge. CPU, RAM, fans, drives and network use the full width. Under that, the GPU block is on the left and the game name, FPS and frame-time graph are on the right, no taller than the GPU block. The graph scales to the samples in view, so an 8 ms run is not a flat line along the bottom.

There is no second screensaver. **Block Screensaver** (Settings → Modules → PC Stats, default on) holds off 240-MP's own screensaver while this screen is open, using the same idle tracker as the rest of the app. Turn it off and the normal timeout applies here too. It does not change the screensaver anywhere else. Stat updates are not input. The panels still glide a few pixels every few minutes (Panel Drift, default **3 MIN**, about twelve seconds for the move).

If the PC is asleep or the address is wrong, the screen says **PC OFFLINE** and keeps retrying. One missed reply keeps the last reading; the next failure switches to offline. It comes back on its own when the PC answers again. Hardware still shows when the frame agent is down; the game block then says there is no frame data.

## 240-MP config

Turn the row on or off, the poll rate, and the drift interval from **Settings → Modules → PC Stats**. The module is on by default.

The address and ports are keys in `config.json` (the same file Settings writes). A remote cannot type an address, so they are not a spinner on that page. On the Pi the file is `~/.local/share/240-MP/config.json`.

```json
{
  "modules": {
    "com.240mp.pc_stats": {
      "enabled": true,
      "host": "10.0.0.44",
      "lhm_port": 8085,
      "frames_port": 8086,
      "poll_interval": "2 SEC",
      "drift_interval": "3 MIN"
    }
  }
}
```

| Key | Default | What it is |
|---|---|---|
| `host` | `10.0.0.44` when the key is absent | PC address. Set `""` to force the setup screen. |
| `lhm_port` | `8085` | LibreHardwareMonitor remote web server |
| `frames_port` | `8086` | The frame-time agent |
| `poll_interval` | `2 SEC` | Also in Settings. `1 SEC` or `2 SEC`. |
| `drift_interval` | `3 MIN` | Also in Settings. `1 MIN`, `3 MIN`, `5 MIN`, `10 MIN`. |

Polling is every one or two seconds, on a background thread, and only while this screen is open. A request that does not finish is abandoned; it does not block the UI.

## PC setup

Do this on the Windows PC. The full click-path, firewall commands, and logon task are in [pc-agent/README.md](../pc-agent/README.md). Short version:

1. LibreHardwareMonitor, run as administrator, **Remote Web Server** on port **8085**, start with Windows. Allow TCP 8085 through the firewall.
2. Download the PresentMon console exe into `pc-agent/` (or point `presentmon_path` at it). PresentMon 2.6 does not write `MsBetweenPresents` unless asked, so the agent passes `--v1_metrics` and also accepts `FrameTime` or `MsBetweenAppStart` if a build still uses those headers.
3. From an elevated PowerShell — the title bar must say **Administrator:** — run `powershell -ExecutionPolicy Bypass -File .\pc-stats-agent.ps1 -Install`

That opens TCP 8086, reserves the URL, and registers a logon task for the current user only: hidden window, highest privileges, no 72-hour cutoff, restart up to 3 times a minute apart. Output is appended to `pc-agent/pc-stats-agent.log`. PresentMon's ETW session needs that elevation (or membership in Performance Log Users). If it is not elevated, `/stats.json` reports `ok: false` with the exit code and stderr (access denied is exit code 6).

The agent watches the foreground process, ignores the shell, and restarts PresentMon when the game changes. `http://10.0.0.44:8086/stats.json` is the frame payload. `http://10.0.0.44:8085/data.json` is the sensor tree.
