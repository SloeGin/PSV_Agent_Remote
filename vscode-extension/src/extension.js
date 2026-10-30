'use strict';

const vscode = require('vscode');
const path = require('node:path');
const { Client } = require('./client');

let client;

function configuration() {
  const source = vscode.workspace.getConfiguration('psvAgentRemote');
  return Object.fromEntries(['autoStart', 'pythonPath', 'experimentalIpc', 'brokerHost', 'brokerPort',
    'hostId', 'ipcSocket', 'stateDatabase', 'appServerSockets', 'maxSessions'].map((key) => [key, source.get(key)]));
}

function activate(context) {
  const output = vscode.window.createOutputChannel('PSV Agent Remote');
  const bar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 20);
  bar.name = 'PSV Agent Remote';
  bar.command = 'psvAgentRemote.menu';
  let state = { state: 'stopped' };
  let paused = false;
  function update(message) {
    state = message;
    const labels = { starting: '$(sync~spin) PSV: Starting', stopped: '$(debug-disconnect) PSV: Disconnected',
      disabled: '$(circle-slash) PSV: Disabled', conflict: '$(warning) PSV: Settings conflict', error: '$(warning) PSV: Check setup' };
    bar.text = message.state === 'running' ? `$(radio-tower) PSV: ${message.bridges} sessions` : labels[message.state] || 'PSV';
    bar.tooltip = message.message || (message.state === 'running' ?
      `Codex manager running · ${message.bridges} managed sessions. Click for controls.` : 'Click for connection controls.');
    if (message.message) output.appendLine(message.message);
    bar.show();
  }
  client = new Client({ runtime: path.join(context.extensionPath, 'runtime'), status: update,
    log: (text) => output.appendLine(String(text)) });
  async function start(replace = false) {
    if (!['darwin', 'linux'].includes(process.platform)) {
      update({ state: 'error', message: 'This bridge supports macOS and Linux hosts (including Remote SSH hosts), not Windows.' }); return;
    }
    paused = false;
    await client.start(configuration(), replace);
  }
  const commands = {
    start: () => start(),
    stop: async () => { paused = true; await client.stop(); },
    restart: () => start(true),
    logs: () => output.show(true),
    configure: () => vscode.commands.executeCommand('workbench.action.openSettings', '@ext:psv-agent-remote.psv-agent-remote'),
    menu: async () => {
      const items = [
        { label: '$(plug) Connect', action: 'start' }, { label: '$(debug-disconnect) Disconnect this window', action: 'stop' },
        { label: '$(refresh) Reconnect', action: 'restart' }, { label: '$(settings-gear) Configure', action: 'configure' },
        { label: '$(output) Show logs', action: 'logs' }
      ];
      const item = await vscode.window.showQuickPick(items, { title: 'PSV Agent Remote', placeHolder: bar.tooltip });
      if (item) await commands[item.action]();
    }
  };
  for (const [name, callback] of Object.entries(commands))
    context.subscriptions.push(vscode.commands.registerCommand(`psvAgentRemote.${name}`, callback));
  context.subscriptions.push(output, bar, vscode.workspace.onDidChangeConfiguration(async (event) => {
    if (!event.affectsConfiguration('psvAgentRemote')) return;
    if (!configuration().autoStart) await client.stop();
    else if (!paused) await start(true);
  }));
  update(state);
  if (configuration().autoStart) void start();
  return { getStatus: () => ({ ...state }) };
}

async function deactivate() { if (client) await client.stop(false); }

module.exports = { activate, deactivate };
