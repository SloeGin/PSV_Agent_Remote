'use strict';
// Run in an isolated VS Code Extension Development Host with a fixture broker/index.
const assert = require('node:assert/strict');
const vscode = require('vscode');
const fs = require('node:fs/promises');
const path = require('node:path');

async function until(predicate, timeout = 45000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) {
    if (await predicate()) return;
    await new Promise((resolve) => setTimeout(resolve, 200));
  }
  throw new Error('Timed out waiting for extension lifecycle.');
}

async function run() {
  assert(process.env.PSV_EXTENSION_TEST_STATE_DIR, 'An isolated fixture is required');
  const extension = vscode.extensions.getExtension('psv-agent-remote.psv-agent-remote');
  assert(extension);
  const api = await extension.activate();
  const commands = await vscode.commands.getCommands(true);
  for (const command of ['start', 'stop', 'restart', 'logs', 'configure'])
    assert(commands.includes(`psvAgentRemote.${command}`));
  try {
    await vscode.commands.executeCommand('psvAgentRemote.start');
    await until(() => api.getStatus().state === 'running' && api.getStatus().bridges === 1);
    console.log('PSVTEST: packaged extension connected a real dedicated session');
    await vscode.commands.executeCommand('psvAgentRemote.stop');
    assert.equal(api.getStatus().state, 'stopped');
    await vscode.commands.executeCommand('psvAgentRemote.restart');
    await until(() => api.getStatus().state === 'running' && api.getStatus().bridges === 1);
    console.log('PSVTEST: disconnect/reconnect commands passed');
  } finally {
    await vscode.commands.executeCommand('psvAgentRemote.stop');
  }
  const directory = process.env.PSV_EXTENSION_TEST_STATE_DIR;
  await until(async () => !(await fs.readdir(directory)).some((file) => /^codex-.*\.watch\.json$/.test(file)));
  console.log('PSVTEST: last window lease cleaned up manager');
  await fs.writeFile(path.join(directory, 'extension-test-passed'), 'passed\n');
}

module.exports = { run };
