# PSV Agent Remote

Use a PlayStation Vita to read requests from your coding agent, approve commands,
answer questions, send a follow-up message, or stop a running turn. The computer
and Vita exchange messages through an MQTT broker on your network.

## Agent support

| Agent | Install once | What starts the connection? |
|---|---|---|
| **Claude Code — CLI and VS Code** | Install the Claude hooks | Claude starts the hooks and reply listener automatically for each session. No separate bridge service to launch. |
| **Codex — VS Code (experimental)** | Install the PSV Agent Remote VSIX | The extension automatically starts and manages a background bridge service when VS Code opens. No per-session setup. |
| **Codex — CLI (experimental)** | Configure a shared Codex App Server endpoint | Run the shared App Server and the PSV bridge manager, or let the VS Code extension manage the bridge. Start the CLI through that shared server; ordinary standalone CLI sessions cannot be attached. |

**An MQTT broker must be running for every option.** Claude's one-time hook
installation and the Codex extension do not install or start the broker.

Claude has been tested on hardware. Codex's supported approval, question, reply
and stop paths have passed real local backend/MQTT tests; full Codex hardware
acceptance remains pending. Codex offers fewer approval/question variants than
Claude. See [Codex support and limitations](docs/codex-integration.md).

## Features

- **Remote decisions.** Read command or file-change requests, approve or deny
  them, or answer the agent's questions. Available controls depend on the agent.
- **Replies and stop.** Continue an idle session with a preset or typed message,
  queue a reply while it works, or request a stop. Delivery progress appears on
  the Vita.
- **Readable conversations.** View your prompt and the agent's latest answer with
  Markdown, scrolling, and horizontal panning for wide tables and code blocks.
- **Multiple sessions.** Switch between sessions, hear a chime when one needs
  attention, and inspect its supported controls in the session list.
- **Sofa and desk modes.** Send decisions to the Vita in sofa mode, or handle them
  on the computer in desk mode. Charging switches modes automatically.
- **Desktop fallback.** Offline, expired and unsupported requests stay with the
  agent's normal computer-side dialog.
- **Optional TLS-PSK.** Encrypt the Vita-to-broker connection; see the
  [encrypted setup](#encrypted-broker-tls-psk) for its computer-side requirements.

## How it works

```text
Claude Code ── hooks ───────────────┐
                                  ├── MQTT broker ── PS Vita
Codex ── background bridge manager ┘
          ↑ started by the VS Code extension, or run manually for CLI
```

The VS Code extension is a local launcher and status interface for the Codex
bridge service. It does not replace the official Codex extension or change
Claude's hooks.

## Requirements

- A **PS Vita with custom firmware** and VitaShell. Guides:
  [中文](docs/vita-hacking.zh.md) · [English](docs/vita-hacking.en.md).
- An **MQTT 3.1.1 broker**, such as Mosquitto, reachable by the computer and Vita.
- **Python 3.11+** on the computer running the agent.
- For **Claude replies**, Claude Code with `asyncRewake` hook support (tested with
  2.1.311). On older clients, omit the `psv_listen.py` hook entries.
- **VitaSDK** to build the Vita app. Building the VSIX from source also needs
  **Node.js 20+**, npm and Python with pip. These build tools are not needed to
  install an already-built VSIX.

## Setup

### 1. Start the broker

For a trusted local network, a Mosquitto listener can use:

```conf
listener 1883 0.0.0.0
allow_anonymous true
```

Add it to your Mosquitto configuration, start/restart the broker, and allow its
port through the host firewall. This listener has no authentication or encryption;
use the [TLS-PSK setup](#encrypted-broker-tls-psk) on shared networks.

Example address throughout this guide: `192.168.1.20:1883`. Replace it with your
broker's actual address.

### 2. Connect your agent

Choose either or both of the following integrations.

#### Claude Code: one-time hook installation

From the repository:

```bash
cd hooks
python3 -m venv .venv
.venv/bin/python -m pip install "paho-mqtt>=2.0"
.venv/bin/python install.py
```

Create or edit `~/.config/psv-agent-remote/config.toml`:

```toml
broker_host = "192.168.1.20"
broker_port = 1883
```

**Install once per computer/user account, not once per session.** The installer
backs up and updates `~/.claude/settings.json` for both Claude CLI and its VS Code
extension. Restart existing Claude sessions once after installation. After that,
Claude starts the hooks and reply listener automatically, including after a
computer restart; there is no separate Claude bridge service to launch manually.

Keep the checkout in place: installed hooks reference its Python and scripts.
Rerun the installer if you move the checkout or update hook definitions. Remove
hooks with `.venv/bin/python install.py --uninstall` from `hooks/`.

#### Codex in VS Code: install the extension

Build the VSIX once from the repository:

```bash
cd vscode-extension
npm ci
npm run package
```

1. In VS Code, run **Extensions: Install from VSIX…** and select
   `vscode-extension/dist/psv-agent-remote-0.1.0.vsix`.
2. Run **PSV Agent Remote: Configure** and set **Broker Host** to your broker's
   address. Alternatively, reuse the configuration file shown above.
3. Open Codex and use it normally. The **PSV** status item shows the bridge service
   state and managed session count; click it for connection controls or logs.

The VSIX bundles the bridge and Python dependencies: no checkout or pip setup is
needed on the machine installing it. Python 3.11+ must still be available. If it
is not found, set **PSV Agent Remote: Python Path**.

**The extension starts the Codex bridge service automatically.** Several VS Code
windows share one service; closing the last window stops an extension-owned
service after a short delay. After rebooting, reopening VS Code starts it again.
You do not need to run `psv_codex.py watch` yourself for normal VS Code use.

This integration currently supports local macOS/Linux agents. It uses a
version-dependent frontend interface; compatible versions, settings and details
are in the [extension guide](vscode-extension/README.md).

#### Codex CLI: shared runtime required

CLI use requires **two running services**: a shared **Codex App Server** and the
**PSV bridge manager**. Connect the CLI to that App Server, then configure its
socket once in the VS Code extension's **App Server Sockets** setting. The
extension can then discover its sessions automatically.

To use the CLI without VS Code, run the bridge manager yourself and restart it
after a reboot. See the [standalone manager](docs/codex-integration.md#persistent-manager-no-per-session-setup)
and [shared CLI setup](docs/codex-integration.md#cli-existing-shared-app-server-runtime)
for the commands. A normally started CLI cannot be attached by starting another server afterward;
launch the CLI through the shared server instead.

### 3. Install and connect the Vita app

Build `vita/build/psv_agent_remote.vpk` using the [Vita build guide](vita/README.md).
Upload it through VitaShell's FTP server, then open the VPK in VitaShell to install.

Open Agent Remote on the Vita, press **START → Add broker**, and enter your
broker's LAN address, such as `192.168.1.20`. Select it; the top bar shows **LINK**
once connected. The Vita and computer must use the same broker.

## Using it

| Input | Action |
|---|---|
| L / R | Previous / next session |
| SELECT | Session list: ✕ open, □ remove, △ view the highlighted session's capabilities |
| Left stick up/down, D-pad up/down, vertical drag | Scroll or move through a list |
| D-pad left/right, left stick left/right | Choose an option |
| Right stick | Pan wide tables/code; switch the table being panned |
| Sideways drag on a table/code block | Pan horizontally |
| Front touch tap | Press a tab, option, list row or button |
| ✕ / ○ | Confirm / back |
| □ | Hide/show a decision sheet, or open/fold the reply drawer |
| △ | Toggle sofa/desk on the main screen; show capabilities in the session list |
| START | Settings: brokers, sound, touch, dimming, exit |

A new request starts with no option selected and a half-second confirmation
lockout. The rear panel scrolls/pans without pressing buttons. Any button closes
the capability popup without also activating the page underneath.

Typed replies remain drafts until **Send** is selected. A failed or unconfirmed
reply shows **NOT CONFIRMED** and preserves typed text. Front touch can be disabled
in Settings.

The application pages, home icon and LiveArea use the independent `#DDD23B`
palette. Conversations and capability popups use the corresponding Agent palette.

## Encrypted broker (TLS-PSK)

On a network you share with others, use TLS with a pre-shared key between the Vita and the broker. An encrypted broker never falls back to plaintext. While one is selected, the Vita never switches to a plaintext broker on its own.

The computer-side Python bridges use plain MQTT. For this setup, run the bridge and broker on the same computer and connect the bridge to `127.0.0.1`; only the Vita uses the network-facing TLS listener.

1. **Generate a key** on any machine you own:

   ```bash
   python3 tools/psk_pair.py ~/psv-key
   ```

   This writes `psk.txt` (for the Vita) and `mosquitto.psk` (for the broker). Both files hold the same key and are readable only by you. Keep them out of chat and email.

2. **Pair the Vita.** Copy `psk.txt` to `ux0:data/psv_agent_remote/psk.txt`. VitaShell's FTP is unencrypted, so do this on a network you trust.

3. **Configure Mosquitto** on the broker host:

   ```bash
   sudo install -o mosquitto -g mosquitto -m 600 mosquitto.psk /etc/mosquitto/psv.psk
   sudo cp tools/mosquitto-psk.conf /etc/mosquitto/conf.d/psv-agent-remote.conf
   sudo systemctl restart mosquitto
   ```

   [tools/mosquitto-psk.conf](tools/mosquitto-psk.conf) keeps port 1883 on localhost for the Python bridge and opens port 8883 with TLS-PSK on the network. Remove any other `listener` lines, such as the plaintext example above.

4. **Point the computer bridge at localhost**: set `broker_host = "127.0.0.1"` in `config.toml`.

5. **Check the broker** (needs `mosquitto-clients`):

   ```bash
   python3 tools/check_broker.py --lan-ip <broker LAN IP> --psk-file <(sudo cat /etc/mosquitto/psv.psk)
   ```

   All three checks should pass: port 1883 is closed on the network, port 8883 accepts the key, and port 8883 refuses a wrong key.

6. **Add the broker on the Vita** as `tls://<broker LAN IP>` (port 8883 by default). It shows a `TLS` tag. If it cannot connect, the row says why: `No key: pair this PSV first`, `Key rejected`, `TLS error`, or `Can't reach broker`.

## Testing without a Vita

`tools/fake_psv.py` can answer broker requests interactively:

```bash
hooks/.venv/bin/python tools/fake_psv.py --host 192.168.1.20
```

Use `--mode desk` to simulate desk mode. `--auto allow` approves requests without
prompting; use it only with a dedicated test session and test broker.

## Development

```bash
hooks/.venv/bin/python -m pip install pytest
hooks/.venv/bin/python -m pytest -q hooks/tests
make -C vita/tests
cd vscode-extension && npm ci && npm test
```

| Path | Contents |
|---|---|
| `hooks/` | Claude hooks/installer, shared Python code, Codex bridge/manager and tests |
| `vscode-extension/` | Codex launcher/status extension and VSIX packaging |
| `vita/` | Vita C app, host tests, fonts, icons and LiveArea artwork |
| `tools/` | Broker, pairing, test-client and asset-generation tools |
| `docs/` | Setup guides, Codex technical notes and HTML mockups |

Local probes, runtime captures, build outputs, generated keys and user
configuration do not belong in Git. The repository's ignore rules cover common
cases; inspect `git status` before publishing. VSIX packaging copies production
Python files and pinned dependency wheels, excluding local configuration and
research artifacts.

Fonts are subsets of Noto Sans SC, JetBrains Mono and Space Grotesk, under the
SIL Open Font License (`vita/assets/OFL-*.txt`). Vendored cJSON and Eclipse Paho
MQTTPacket retain their licenses in `vita/third_party/`. The VSIX includes dependency
license metadata inside its bundled wheels.
