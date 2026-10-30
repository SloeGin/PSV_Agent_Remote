# Kiro support status

- **Kiro IDE conversations: not supported.** The Vita cannot follow or approve a
  conversation you are having in the Kiro IDE. See [Why the IDE is not
  supported](#why-the-ide-is-not-supported).
- **Standalone Kiro CLI session, with the Vita as its user interface:
  experimental.** A bridge starts its own `kiro-cli acp` session; you prompt,
  approve and stop it from the Vita.

## Standalone Kiro CLI session (experimental)

`hooks/psv_kiro.py` starts `kiro-cli acp --agent-engine=v3`, creates a new Kiro
session in a working directory, and connects it to the broker. That session has
no computer-side chat window: the Vita is where you send prompts, read the latest
exchange and answer approvals.

### Requirements

- An MQTT broker reachable by the computer and the Vita, configured as in the
  [main setup guide](../README.md#1-start-the-broker).
- The Vita app built from this repository (it includes the Kiro icon and palette).
- Python 3.11+ with `paho-mqtt` in `hooks/.venv`:

  ```bash
  python3 -m venv hooks/.venv
  hooks/.venv/bin/python -m pip install "paho-mqtt>=2.0"
  ```

- The Kiro CLI, logged in. Tested with `kiro-cli 2.35.1`. The `kiro` command that
  opens the Kiro IDE is a different program and has no `acp` subcommand.

  ```bash
  kiro-cli --version
  kiro-cli login
  ```

### Start

Set the broker in `~/.config/psv-agent-remote/config.toml`, as for the other
agents:

```toml
broker_host = "192.168.1.20" # Replace with your broker's address.
broker_port = 1883
```

Then, from the repository root, start one bridge for the project directory you
want Kiro to work in:

```bash
hooks/.venv/bin/python hooks/psv_kiro.py --cwd /absolute/path/to/project
```

| Option | Default | Use |
| --- | --- | --- |
| `--cwd` | current directory | Directory the Kiro session works in. |
| `--kiro-cli` | `kiro-cli` | Full path, if the CLI is not on `PATH` (often `~/.local/bin/kiro-cli`). |
| `--host-id` | host name | Stable identity for this computer on the broker. |

The broker address can also come from `PSV_REMOTE_HOST` and `PSV_REMOTE_PORT`
instead of the configuration file.

The bridge runs until you press Ctrl-C, which also ends the `kiro-cli` process
and the session. If the broker becomes unreachable, the bridge keeps the session
and reconnects on its own. It does not install a login service; start it again
after a reboot. Only one bridge can run per working directory.

If the CLI is not logged in, the bridge exits with
`Kiro CLI is not logged in; run kiro-cli login`.

### Use it from the Vita

1. Keep the Vita in **sofa** mode. The session appears in the session list with
   the Kiro icon, idle.
2. Open the reply drawer and send your first prompt. Your message and Kiro's
   latest answer are shown, like other agents: the last exchange only.
3. When Kiro asks for permission, the decision sheet offers only the options Kiro
   supplied, from: **Allow**, **Always allow**, **Deny**, **Deny and stop**.
   *Deny with note* is not offered for Kiro.
4. A reply sent while Kiro is working is *steered* into the running turn: Kiro
   picks it up at its next safe point, without starting a new turn.
5. **Stop** interrupts the running turn. An approval that was still open is
   withdrawn with it.

### Limitations

- **Kiro's permission policy decides what reaches the Vita.** Tool calls the
  policy already allows (for example simple shell commands) run without asking.
- **Always allow is persistent.** It is sent as Kiro's *Always allow* with a
  workspace-scoped rule, so it outlives the session; this is why Kiro shows
  *Always allow* where Claude and Codex show *Allow for session*. Use **Allow**
  for a one-time approval.
- **No computer-side fallback.** The Vita is this session's only approval window,
  so an approval is never dropped: if it expires on the Vita (120 s by default)
  it is offered again with a new countdown, and if the Vita goes offline or into
  desk mode (for example when plugged in) it disappears and comes back once the
  Vita is online in sofa mode. Meanwhile the turn stays paused.
- **Questions are not supported.** Kiro's ACP interface has no structured-question
  callback.
- **Verification.** Against the real CLI: session start, a prompt and its answer
  (including your own message), and an approval request translated into the
  decision sheet and answered with Deny. On Vita hardware: the session appears,
  and replies and your own message show. Not yet tested on hardware: approval
  decisions, Always allow, steering and Stop.

## Why the IDE is not supported

PSV Agent Remote's minimum bar for an agent is:

1. The Vita shows the live last exchange (your prompt and the agent's latest
   answer) of the conversation you are actually using.
2. The Vita can approve or deny that conversation's command and tool requests.

An investigation against Kiro CLI 2.35.1 and the Kiro IDE found no supported
interface that meets both for IDE conversations.

**ACP attach.** A second ACP client can discover the IDE conversation with
`session/list` and load its history with `session/load`. After the load, turns
driven from the IDE are not pushed to that client: during a 4-minute trace the
IDE wrote 23 transcript entries, including 2 tool approvals, and the attached
client received no message chunks, no tool calls and no permission requests. An
attached client sees a snapshot taken at load time and cannot answer the IDE's
approvals. Messages it sends do reach the conversation, but the IDE does not
display them until it re-renders. `session/cancel` on the IDE session fails
with an internal persistence-classification error.

**IDE `PreToolUse` hooks.** A command hook runs before every tool call, before
the IDE's own approval prompt.

- Exit code 2 blocks the call and returns stderr to the agent, so deny works.
- `permissionDecision: "ask"` forces the IDE approval prompt, which would serve
  as the computer-side fallback.
- `permissionDecision: "allow"` does not suppress the IDE's own prompt, so an
  approval from the Vita would have to be repeated in the IDE.
- The hook input does not include the transcript or the agent's last message.

The live conversation is only available from the IDE's internal session files,
which Kiro documents as server-owned storage. Relying on them was ruled out.
