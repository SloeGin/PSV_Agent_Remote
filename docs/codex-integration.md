# Codex setup and support

Codex support is **experimental** and currently requires a local macOS or Linux
agent. The bridge connects existing live conversations to the Vita; it does not
launch an agent or install Claude hooks.

An MQTT broker must already be running, and the computer and Vita must use the
same broker. Start with the [main setup guide](../README.md#setup).

## VS Code: recommended setup

Install the **PSV Agent Remote** VSIX once, configure its broker address, and open
Codex normally. The extension automatically starts the background bridge and
finds existing and new live sessions. No per-session configuration is needed.
See the [extension installation guide](../vscode-extension/README.md#install).

Several VS Code windows share one bridge manager. Closing the last window stops
an extension-owned manager after a short delay. After a computer restart,
reopening VS Code starts it again. The MQTT broker is managed separately.

Python 3.11+ must be installed. The VSIX includes the bridge's Python dependencies;
no checkout or pip installation is needed on the machine installing it.

## Supported features and limits

| Feature | Supported behavior |
| --- | --- |
| Command approval | Accept, accept for the session, decline, or cancel when the native request offers those choices. |
| Questions | Blocking, non-secret, single-choice questions: up to four questions with up to four options each. Free text is available only when Codex permits it. |
| Idle reply | Starts a turn in the same conversation using its existing model and runtime settings. |
| Busy reply | Queues one text reply; a newer reply replaces it. Cancel removes it, and Stop discards it. |
| Stop | Interrupts the active turn; confirmation requires observing the interrupted state. |
| Remove (session list) | Hides an idle session from the Vita until it is active again: a new turn, or a decision it needs. |

File, network and permission approvals, secret input, nonblocking questions, and
questions exceeding these limits stay on the computer. Unsupported requests,
desk mode, an offline Vita, expired requests and computer-side answers leave the
native dialog in charge. Uncertain operations are not automatically replayed.
Stop does not terminate commands left in the background after a turn has ended.

The Vita session list shows each Agent's identity and capabilities. Press Triangle
on a highlighted session to inspect its controls. Conversation colors follow the
active Agent; Settings and session management use the application palette.

## Compatibility

VS Code support uses an internal, version-dependent Codex frontend interface.
The verified combination is Codex `0.164.0-alpha.11.1`, bundled in the official
VS Code extension `26.1021.22027`, with state stream version 11. Other versions
are not guaranteed to work. An incompatible stream disables the affected bridge.
The bridge does not take ownership of the conversation.

Windows is not supported. In a Remote SSH window, Codex runs on the remote host,
and so does the PSV extension: install it on that host too (VS Code offers
**Install in SSH: host**), and set the broker in that host's settings or its
`~/.config/psv-agent-remote/config.toml`. The remote host needs Python 3.11+ and
must be able to reach the broker. This setup is not yet verified on hardware.

Approval, question, reply, stop and bridge lifecycle paths have passed local
backend/MQTT tests. **Full Codex hardware acceptance on the Vita remains pending.**

## Persistent manager: no per-session setup

This is an alternative for users who want to run the bridge without the VS Code
extension. Skip it for normal extension use.

From the repository root, prepare Python once:

```bash
python3 -m venv hooks/.venv
hooks/.venv/bin/python -m pip install 'paho-mqtt>=2.0' 'websockets>=14'
```

Set your broker address in `~/.config/psv-agent-remote/config.toml`:

```toml
broker_host = "192.168.1.20" # Replace with your broker's address.
broker_port = 1883
```

Start the manager for local frontend sessions:

```bash
hooks/.venv/bin/python hooks/psv_codex.py watch --experimental-ipc
```

It discovers live sessions automatically and runs until interrupted. It does not
install a login service; restart it after rebooting. Do not also start a second
manager for the same sessions. The VS Code extension can reuse a compatible
manually started manager and will leave it running when VS Code closes.

## CLI: existing shared App Server runtime

CLI use requires **two running services**: a shared Codex App Server and the PSV
bridge manager. The CLI must connect to that same App Server. A normally started
standalone CLI cannot be attached by starting another server afterward.

On Codex versions supporting remote CLI sessions, start the shared server first:

```bash
codex app-server --listen unix:///absolute/path/to/control.sock
```

In another terminal, start the CLI through it:

```bash
codex --remote unix:///absolute/path/to/control.sock
```

Then either add that socket path once to the VS Code extension's **App Server
Sockets** setting, or run the standalone manager from the repository root:

```bash
hooks/.venv/bin/python hooks/psv_codex.py watch \
  --app-server-socket /absolute/path/to/control.sock
```

Complete the Python and broker configuration above before using the standalone
manager. Repeat `--app-server-socket` for multiple servers; thread IDs are discovered
automatically. A new conversation may appear only after its first message.
Requests already pending before attachment may need to be answered on the computer.

The server and manager must remain running. Neither standalone command installs
a login service. The official [Codex App Server documentation](https://developers.openai.com/codex/app-server/)
describes the public server protocol; the VS Code frontend interface is internal.
