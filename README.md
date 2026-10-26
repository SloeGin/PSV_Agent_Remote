# PSV Agent Remote

A PlayStation Vita companion for coding agents. When the agent needs a decision, the Vita lights up and chimes: you read the request, approve or deny it, or answer the agent's question, without going back to your computer. When a session finishes its turn, the Vita tells you it is idle.

It currently works with **Claude Code** (terminal CLI and the VS Code extension).

## Features

- **Approvals on the Vita.** Tool permission requests appear with the full command or diff. Allow, allow for the session, deny, deny with a note typed on the Vita's keyboard, or deny and stop the agent.
- **Answers to the agent's questions.** Single- and multiple-choice questions, including free-text "Other" answers, or hand the question back to the computer.
- **The current turn at a glance.** Your prompt and the agent's latest reply, rendered as Markdown: bold, inline code, code blocks, headings, lists, and tables that scroll sideways.
- **Several sessions at once.** One tab per session, a session list, a chime when another session needs you, and an idle notice when a session finishes.
- **Sofa and desk modes.** In sofa mode decisions go to the Vita; in desk mode they stay on the computer. The mode follows the charger automatically (plugged in = desk, unplugged = sofa) and can be toggled by hand.
- **Never blocks the agent.** If the Vita is offline, in desk mode, or does not answer in time, the request falls back to the agent's normal on-screen dialog.
- **Optional encryption.** Brokers marked `tls://` use TLS with a pre-shared key, so nobody else on the network can read the traffic or answer for you.

## How it works

```
Claude Code ──hooks──► psv_hook.py ──MQTT──► broker (Mosquitto) ◄──MQTT──► PS Vita app
     ▲                      │
     └── decision / answer ─┘
```

- `hooks/` is a Python hook that Claude Code runs on its events (session start/end, prompt, tool use, permission request, question, stop). It publishes the session state to an MQTT broker. For decisions, it waits for the Vita's reply.
- `vita/` is the Vita app (C, VitaSDK, libvita2d). It subscribes to the broker, shows sessions and requests, and publishes your answers.
- The broker is any MQTT 3.1.1 broker; the setup below uses Mosquitto 2.x.

## Requirements

- **A PS Vita running custom firmware** (HENkaku/Ensō) with VitaShell. A step-by-step guide (in Chinese) is in [docs/vita-hacking.md](docs/vita-hacking.md); the source it follows is [vita.hacks.guide](https://vita.hacks.guide).
- **An MQTT broker** on your local network. Both the Vita and the machine running the agent must reach it.
- **Python 3.11+** on the machine running Claude Code.
- **VitaSDK** to build the app (see [vita/README.md](vita/README.md)).

## Setup

### 1. Broker

For a trusted network, a plaintext listener is enough. Mosquitto 2.x listens only on localhost by default, so add a LAN listener, for example in `/etc/mosquitto/conf.d/psv.conf`:

```
listener 1883 0.0.0.0
allow_anonymous true
```

Restart Mosquitto and allow port 1883 through the host firewall. On a network you share with others, use the [encrypted setup](#encrypted-broker-tls-psk) instead.

### 2. Hooks (on the machine running Claude Code)

```bash
cd hooks
python3 -m venv .venv
.venv/bin/pip install "paho-mqtt>=2.0" pytest
.venv/bin/pytest -q
.venv/bin/python install.py
```

`install.py` backs up `~/.claude/settings.json` and merges the hooks into it. The hooks point at this checkout, so reinstall if you move it. Restart running Claude Code sessions to pick them up. To remove them, run `.venv/bin/python install.py --uninstall`.

Tell the hook where the broker is in `~/.config/psv-agent-remote/config.toml`. Every key is optional:

```toml
broker_host = "192.168.1.20"
broker_port = 1883
wait_timeout_s = 120     # seconds to wait for the Vita before falling back to the on-screen dialog
enabled = true
```

Environment variables override the file: `PSV_REMOTE_HOST`, `PSV_REMOTE_PORT`, `PSV_REMOTE_TIMEOUT`, `PSV_REMOTE_ENABLED=0`. Hook errors never affect Claude Code; they are logged to `~/.cache/psv-agent-remote/hook.log`.

### 3. Vita app

Build `vita/build/psv_agent_remote.vpk` as described in [vita/README.md](vita/README.md). Then install it with VitaShell: press SELECT to start its FTP server, upload the VPK to `ux0:`, and open it in VitaShell to install.

On the Vita, press START → **Add broker** and enter the broker's address, for example `192.168.1.20` (port 1883 by default). Select it to connect; the top bar shows `LINK` once connected.

## Using it

| Input | Action |
|---|---|
| L / R | Previous / next session |
| SELECT | Session list (✕ open, □ remove an idle session) |
| Left stick up/down, D-pad up/down, drag on either touch panel | Scroll |
| D-pad left/right, left stick left/right | Choose an option |
| Right stick | Pan a wide table (left/right), switch between tables (up/down) |
| Front touch tap | Press what you tap: tabs, options, list rows, buttons, settings |
| ✕ | Confirm |
| ○ | Back |
| □ | Hide / show the decision sheet |
| △ | Toggle sofa / desk mode |
| START | Settings: brokers, sounds, front touch, screen dimming, exit |

For safety, no option is preselected on a new request, and ✕ is ignored for half a second after a request appears. The rear touch panel only scrolls, so holding the Vita never presses anything. Front touch can be turned off in Settings.

The app's files live in `ux0:data/psv_agent_remote/`: `config.txt` holds the settings and broker list, and `psk.txt` holds the encryption key.

## Encrypted broker (TLS-PSK)

On a network you share with others, use TLS with a pre-shared key between the Vita and the broker. Nobody without the key can connect, read sessions, or answer for you. An encrypted broker never falls back to plaintext. While one is selected, the Vita never switches to a plaintext broker on its own.

The hook itself speaks plain MQTT, so in this setup it runs on the broker host and connects to `127.0.0.1`. Only the Vita connects over the network.

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

   [tools/mosquitto-psk.conf](tools/mosquitto-psk.conf) keeps port 1883 on localhost for the hook and opens port 8883 with TLS-PSK on the network. Remove any other `listener` lines, such as the plaintext example above.

4. **Point the hook at localhost**: set `broker_host = "127.0.0.1"` in `config.toml`.

5. **Check the broker** (needs `mosquitto-clients`):

   ```bash
   python3 tools/check_broker.py --lan-ip <broker LAN IP> --psk-file <(sudo cat /etc/mosquitto/psv.psk)
   ```

   All three checks should pass: port 1883 is closed on the network, port 8883 accepts the key, and port 8883 refuses a wrong key.

6. **Add the broker on the Vita** as `tls://<broker LAN IP>` (port 8883 by default). It shows a `TLS` tag. If it cannot connect, the row says why: `No key: pair this PSV first`, `Key rejected`, `TLS error`, or `Can't reach broker`.

## Testing without a Vita

`tools/fake_psv.py` stands in for the Vita on the broker:

```bash
hooks/.venv/bin/python tools/fake_psv.py --host 192.168.1.20                # answer interactively (a/A/d/D)
hooks/.venv/bin/python tools/fake_psv.py --host 192.168.1.20 --auto allow   # approve everything
hooks/.venv/bin/python tools/fake_psv.py --host 192.168.1.20 --mode desk    # report desk mode
```

## Development

```bash
cd hooks && .venv/bin/pytest -q      # hook tests
make -C vita/tests                   # Vita app logic, built for the host with ASan/UBSan
```

Repository layout:

| Path | Contents |
|---|---|
| `hooks/` | Claude Code hook (`psv_hook.py`), its package `psv_remote/`, the installer, and tests |
| `vita/` | Vita app sources, host tests, assets, and LiveArea art |
| `tools/` | Pairing and broker tools, the fake Vita, and font/artwork generators |
| `docs/` | The UI mockup and the Vita hacking guide (in Chinese) |

The fonts are subsets of Noto Sans SC, JetBrains Mono and Space Grotesk, under the SIL Open Font License (`vita/assets/OFL-*.txt`). The vendored cJSON and Eclipse Paho MQTTPacket libraries keep their own licenses in `vita/third_party/`.
