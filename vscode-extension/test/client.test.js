'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const { PassThrough } = require('node:stream');
const { Client, findPython, launchOptions } = require('../src/client');

class Child extends EventEmitter {
  constructor() {
    super();
    this.stdout = new PassThrough(); this.stderr = new PassThrough(); this.stdin = new PassThrough();
    this.exitCode = this.signalCode = null;
    this.stdin.on('finish', () => { this.exitCode = 0; this.emit('exit', 0); });
  }
  kill() { this.signalCode = 'SIGTERM'; this.emit('exit', null); }
}

function setup() {
  const children = [], states = [], logs = [];
  const client = new Client({ runtime: '/extension/runtime', python: async () => '/python with spaces',
    status: (state) => states.push(state), log: (text) => logs.push(text),
    spawnProcess: () => { const child = new Child(); children.push(child); return child; } });
  return { client, children, states, logs };
}

test('launch preserves argument boundaries and inherits shared broker settings', () => {
  const result = launchOptions('/extension with spaces/runtime', {
    hostId: 'my host', appServerSockets: ['/a socket/control.sock'], maxSessions: 3
  }, false, { PSV_REMOTE_HOST: 'configured-broker' });
  assert.equal(result.args[0], '/extension with spaces/runtime/launcher.py');
  assert(result.args.includes('/a socket/control.sock'));
  assert(result.args.includes('my host'));
  assert.equal(result.env.PSV_REMOTE_HOST, 'configured-broker');
  assert(!result.args.includes('--replace-managed'));
});

test('explicit broker override and IPC opt-out are respected', () => {
  const result = launchOptions('/runtime', { brokerHost: 'broker', brokerPort: 2883,
    experimentalIpc: false, ipcSocket: '/ignored' }, true, {});
  assert.equal(result.env.PSV_REMOTE_PORT, '2883');
  assert(!result.args.includes('--experimental-ipc'));
  assert(!result.args.includes('--ipc-socket'));
  assert(result.args.includes('--replace-managed'));
});

test('Python selection honors explicit path and minimum version', async () => {
  const tried = [];
  assert.equal(await findPython('/my python', async (path) => { tried.push(path); return true; }), '/my python');
  assert.deepEqual(tried, ['/my python']);
  await assert.rejects(findPython('/old', async () => false), /older than 3.11/);
});

test('chunked runtime events update status and shutdown closes only the client pipe', async () => {
  const { client, children, states, logs } = setup();
  await client.start({});
  children[0].stdout.write('{"type":"status","state":"run');
  children[0].stdout.write('ning","bridges":2}\n{"type":"log","message":"ready"}\n');
  assert.equal(states.at(-1).bridges, 2);
  assert(logs.includes('ready'));
  await client.stop();
  assert.equal(children[0].signalCode, null);
  assert.equal(states.at(-1).state, 'stopped');
});

test('rapid concurrent starts create one client', async () => {
  const { client, children } = setup();
  await Promise.all([client.start({}), client.start({}), client.start({})]);
  assert.equal(children.length, 1);
  await client.stop();
});

test('late events from the previous generation do not overwrite new state', async () => {
  const { client, children, states } = setup();
  await client.start({});
  await client.start({});
  const count = states.length;
  children[0].emit('exit', 1);
  children[0].stdout.write('{"type":"status","state":"error"}\n');
  assert.equal(states.length, count);
  await client.stop();
});

test('disabled backend status remains visible on clean exit', async () => {
  const { client, children, states } = setup();
  await client.start({});
  children[0].stdout.write('{"type":"status","state":"disabled"}\n');
  children[0].exitCode = 0; children[0].emit('exit', 0);
  assert.equal(states.at(-1).state, 'disabled');
  await client.stop();
});
