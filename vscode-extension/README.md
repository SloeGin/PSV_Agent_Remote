# PSV Agent Remote for VS Code

Automatically connect local Codex sessions to your PlayStation Vita. Install this
extension once, then open VS Code: the bridge manager discovers existing and new
sessions without per-thread setup.

## Install

Requires desktop VS Code on **macOS or Linux**, **Python 3.11+**, a reachable MQTT
broker, and a Vita running the Agent Remote client. The extension includes the
Python bridge, paho-mqtt 2.1.0 and websockets 15.0.1. Running the installed extension
does not require a repo checkout, a virtual environment, pip installation, or an
additional API key.

1. **Build the VSIX once** from this directory (Node.js 20+, npm and Python with
   pip are needed on the build machine):

   ```bash
   npm ci
   npm run package
   ```

2. In VS Code, run **Extensions: Install from VSIX…**, select
   `dist/psv-agent-remote-0.1.0.vsix`, and enable it in a trusted window. For a
   Remote SSH window, also install it on that host (**Install in SSH: host**).
3. Run **PSV Agent Remote: Configure** and set **Broker Host** and **Broker Port**,
   or reuse the existing configuration file below. Open Codex and use it normally.

The extension starts the background Codex bridge service automatically. There is
no per-session setup and no need to launch `psv_codex.py watch` separately. After
a reboot, reopening VS Code starts the service again. The MQTT broker itself must
already be running; the extension does not start it.

If Python is not found, open **PSV Agent Remote: Configure** and set **Python Path**
to its executable. The extension searches `python3` and standard local paths.

## Connect and configure

The extension uses the existing `~/.config/psv-agent-remote/config.toml` broker
configuration. Alternatively set **Broker Host** and **Broker Port** in VS Code's
PSV Agent Remote settings. The broker configuration is shared with Claude hooks;
the extension does not install or change those hooks.

The **PSV Agent Remote** panel in the Activity Bar shows:

- **Connection**: whether the broker is reachable, and whether the Vita is online
  and in sofa or desk mode.
- **Sessions**: every session on the broker (Claude Code, Codex and Kiro, from all
  your computers) and its state. Codex sessions you removed on the Vita are listed
  as "removed on Vita", with **Show on Vita** to bring one back.
- **Kiro in this window**: **Start** or **Stop** a Kiro session for each workspace
  folder (see [Kiro support](../docs/kiro-integration.md)).

Its title bar has Reconnect, Configure and Show logs. The status bar shows the
manager state and the number of managed sessions. Click it for:

- **Connect** and **Disconnect this window**.
- **Reconnect**, including applying changed settings to an extension-owned manager.
- **Configure** and **Show logs**.

**Auto Start** is on by default. Turning it off persists across VS Code restarts;
Connect still works manually. Disconnect pauses only this window until Connect
or the next VS Code launch.

Multiple VS Code windows share one manager. Closing one releases only its own
connection. After the last window closes, the extension-owned manager stops
after a short grace period and cleans up its bridge processes. A crashed window's
connection expires automatically. No OS login service is installed.

An independently launched, compatible persistent manager can be reused. Closing
VS Code does not stop that manager.

If a manager with different settings is already running (for example after you
change the broker or update the extension):

- An extension-owned manager that no window is using is replaced automatically.
- One that another window is using is left alone, and the status names the
  settings that differ. Make the settings match, or run **Reconnect** to take it
  over.
- A manually started manager is never replaced. Stop it, then run **Reconnect**.

## Codex runtimes

**Frontend IPC** is enabled by default for local Codex frontend sessions,
including VS Code. It is the internal, version-dependent interface verified with
Codex `0.164.0-alpha.11.1` / extension `26.1021.22027`. Stream version mismatches
disable the affected bridge. The extension does not modify the official Codex
extension or use its private stdio connection.

For CLI sessions, add each existing shared Unix App Server socket to **App Server
Sockets** once. An ordinary CLI with private stdio is not attachable this way.
Empty defaults retain the manager's standard socket discovery. Optional settings
override the frontend socket, read-only state index and stable host ID. The local
index defaults to `~/.codex/state_5.sqlite`; only live-owned interactive sessions
are bridged, not archived history or spawned agents.

The extension runs wherever the workspace runs, next to Codex. In a Remote SSH
window that is the remote host: install the extension there too (**Install in
SSH: host**), and set **Broker Host** in that host's Remote settings (or its own
`~/.config/psv-agent-remote/config.toml`). The host needs Python 3.11+ and must
reach the broker. Windows is not supported.

## Development

From this directory:

```bash
npm ci
npm test
npm run package
```

Packaging requires Python with pip and downloads the two pinned pure Python
wheels. Generated runtime files, dependencies and `dist/` artifacts are ignored
by Git. Only production Python sources are copied; local probes and user
configuration are excluded. This local VSIX is not published to the Marketplace.

Dependency license metadata is included inside each bundled wheel.
