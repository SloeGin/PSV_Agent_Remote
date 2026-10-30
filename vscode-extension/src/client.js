'use strict';

const { spawn, execFile } = require('node:child_process');
const os = require('node:os');
const path = require('node:path');

function expand(value) {
  if (value === '~') return os.homedir();
  return value.startsWith('~/') ? path.join(os.homedir(), value.slice(2)) : value;
}

function launchOptions(runtime, config, replace = false, baseEnv = process.env) {
  const args = [path.join(runtime, 'launcher.py'), 'vscode-client', '--max-sessions', String(config.maxSessions ?? 16)];
  if (config.experimentalIpc !== false) args.push('--experimental-ipc');
  if (config.hostId) args.push('--host-id', config.hostId);
  if (config.ipcSocket && config.experimentalIpc !== false) args.push('--ipc-socket', expand(config.ipcSocket));
  if (config.stateDatabase) args.push('--state-db', expand(config.stateDatabase));
  for (const socket of config.appServerSockets || []) args.push('--app-server-socket', expand(socket));
  if (replace) args.push('--replace-managed');
  const env = { ...baseEnv, PYTHONUNBUFFERED: '1' };
  if (config.brokerHost) {
    env.PSV_REMOTE_HOST = config.brokerHost;
    env.PSV_REMOTE_PORT = String(config.brokerPort || 1883);
  }
  return { args, env };
}

function probePython(executable) {
  return new Promise((resolve) => {
    execFile(executable, ['-c', 'import sys; sys.exit(0 if sys.version_info >= (3,11) else 1)'],
      { timeout: 4000, windowsHide: true }, (error) => resolve(!error));
  });
}

async function findPython(configured, probe = probePython) {
  const candidates = configured ? [expand(configured)] :
    ['python3', '/opt/homebrew/bin/python3', '/usr/local/bin/python3', '/usr/bin/python3'];
  for (const executable of [...new Set(candidates)]) if (await probe(executable)) return executable;
  throw new Error(configured ? 'Configured Python is unavailable or older than 3.11.' :
    'Python 3.11+ was not found. Set PSV Agent Remote: Python Path.');
}

class Client {
  constructor({ runtime, status, log, spawnProcess = spawn, python = findPython }) {
    this.runtime = runtime;
    this.status = status;
    this.log = log;
    this.spawnProcess = spawnProcess;
    this.python = python;
    this.child = null;
    this.generation = 0;
    this.folders = [];
  }

  // Commands to the Python client, one JSON line each (it treats EOF as "this window left").
  send(command) {
    const child = this.child;
    if (child && child.stdin && child.stdin.writable) child.stdin.write(`${JSON.stringify(command)}\n`);
  }

  setFolders(paths) {
    this.folders = [...paths];
    this.send({ cmd: 'folders', paths: this.folders });
  }

  async start(config, replace = false) {
    const stopping = this.stop(false);
    const generation = this.generation;
    await stopping;
    if (generation !== this.generation) return;
    this.status({ state: 'starting' });
    try {
      const executable = await this.python(config.pythonPath);
      if (generation !== this.generation) return;
      const { args, env } = launchOptions(this.runtime, config, replace);
      const child = this.spawnProcess(executable, args, { env, stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true });
      this.child = child;
      child.stdout.setEncoding('utf8');
      child.stderr.setEncoding('utf8');
      child.stdin.on('error', () => {}); // a failed interpreter may close stdin first
      this.send({ cmd: 'folders', paths: this.folders }); // a new client starts without them
      let buffer = '', lastState = '';
      child.stdout.on('data', (chunk) => {
        if (generation !== this.generation) return;
        buffer += chunk.toString('utf8');
        if (buffer.length > 65536) { buffer = ''; this.log('Discarded oversized runtime output.'); }
        let newline;
        while ((newline = buffer.indexOf('\n')) >= 0) {
          const line = buffer.slice(0, newline); buffer = buffer.slice(newline + 1);
          try {
            const message = JSON.parse(line);
            if (message.type === 'status') { lastState = message.state; this.status(message); }
            else if (message.type === 'log') this.log(message.message);
          } catch { this.log(line); }
        }
      });
      child.stderr.on('data', (chunk) => { if (generation === this.generation) this.log(chunk.toString('utf8')); });
      child.on('error', (error) => {
        if (generation === this.generation) this.status({ state: 'error', message: error.message });
      });
      child.on('exit', (code) => {
        if (generation === this.generation) {
          this.child = null;
          if (lastState !== 'disabled')
            this.status({ state: code === 0 ? 'stopped' : 'error', message: `Bridge client exited (${code}). See logs.` });
        }
      });
    } catch (error) {
      if (generation === this.generation) this.status({ state: 'error', message: error.message });
    }
  }

  async stop(notify = true) {
    this.generation++;
    const child = this.child;
    this.child = null;
    if (child && child.exitCode === null && child.signalCode === null) {
      await new Promise((resolve) => {
        let timer;
        const finish = () => { clearTimeout(timer); resolve(); };
        child.once('exit', finish);
        child.stdin.end(); // EOF releases only this window's lease
        timer = setTimeout(() => { child.kill('SIGTERM'); finish(); }, 5000);
      });
    }
    if (notify) this.status({ state: 'stopped' });
  }
}

module.exports = { Client, expand, findPython, launchOptions };
