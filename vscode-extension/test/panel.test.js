'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { buildTree, makeProvider, confirmKiroStop } = require('../src/panel');

const STATUS = {
  type: 'status', state: 'running', bridges: 1, discovered: 1, mode: 'extension',
  vita: { online: true, mode: 'sofa' },
  sessions: [
    { sid: 'k1', agent: 'kiro', project: 'infra', state: 'waiting', local: false },
    { sid: 'c1', agent: 'codex', project: 'notes', state: 'running', local: true },
    { sid: 'a1', agent: 'claude', project: 'PSV_agent_remote', state: 'idle', local: false },
  ],
  removed: [{ sid: 'codex-b', project: 'api' }],
  kiro: { bridges: [{ cwd: '/w/infra', pid: 42, sid: 'k1', starting: false }],
          errors: { '/w/broken': 'Kiro CLI is not logged in; run `kiro-cli login`' } },
};

const find = (nodes, id) => {
  for (const node of nodes) {
    if (node.id === id) return node;
    const hit = find(node.children || [], id);
    if (hit) return hit;
  }
  return undefined;
};

test('tree shows connection, grouped sessions and kiro folders', () => {
  const tree = buildTree(STATUS, ['/w/infra', '/w/app', '/w/broken']);
  assert.deepEqual(tree.map((n) => n.label), ['Connection', 'Sessions (3)', 'Kiro in this window']);
  assert.equal(find(tree, 'broker').description, 'reachable');
  assert.equal(find(tree, 'vita').description, 'online · sofa');
  const groups = find(tree, 'sessions').children.map((n) => n.label);
  assert.deepEqual(groups, ['Claude Code', 'Codex', 'Kiro']);
  assert.equal(find(tree, 'session:k1').description, 'waiting for you');
  assert.equal(find(tree, 'session:c1').description, 'working');
  const removed = find(tree, 'removed:codex-b');
  assert.equal(removed.contextValue, 'removedCodex');
  assert.equal(removed.description, 'removed on Vita');
  assert.equal(find(tree, 'kiro:/w/infra').contextValue, 'kiroRunning');
  assert.equal(find(tree, 'kiro:/w/app').contextValue, 'kiroStopped');
});

test('failed kiro start shows its error and can be started again', () => {
  const node = find(buildTree(STATUS, ['/w/broken']), 'kiro:/w/broken');
  assert.equal(node.contextValue, 'kiroStopped');
  assert.match(node.description, /not logged in/);
});

test('an unreachable broker is shown in the connection section', () => {
  const tree = buildTree({ ...STATUS, state: 'broker', message: "Can't reach the MQTT broker at 127.0.0.1:1883" }, []);
  const broker = find(tree, 'broker');
  assert.equal(broker.description, 'unreachable');
  assert.match(broker.tooltip, /Can't reach/);
});

test('older manager shows update hint', () => {
  const tree = buildTree({ type: 'status', state: 'running', bridges: 0, discovered: 0, mode: 'extension' }, ['/w/app']);
  assert.match(find(tree, 'sessions').children[0].label, /Reconnect/);
  assert.equal(find(tree, 'kiro:/w/app').contextValue, undefined); // can't start without a current client
});

test('no workspace folder hint and empty broker', () => {
  const tree = buildTree({ ...STATUS, sessions: [], removed: [] }, []);
  assert.equal(find(tree, 'sessions').children[0].label, 'No sessions on the broker');
  assert.match(find(tree, 'kiro').children[0].label, /Open a folder/);
});

test('tree unchanged fires no event', () => {
  const fired = [];
  const fakeVscode = {
    EventEmitter: class { constructor() { this.event = () => {}; } fire(x) { fired.push(x); } },
    TreeItem: class { constructor(label, state) { this.label = label; this.collapsibleState = state; } },
    TreeItemCollapsibleState: { None: 0, Collapsed: 1, Expanded: 2 },
    ThemeIcon: class { constructor(id, color) { this.id = id; this.color = color; } },
    ThemeColor: class { constructor(id) { this.id = id; } },
  };
  const provider = makeProvider(fakeVscode);
  provider.update(STATUS, ['/w/app']);
  provider.update({ ...STATUS }, ['/w/app']);
  assert.equal(fired.length, 1);
  provider.update({ ...STATUS, vita: { online: false, mode: 'sofa' } }, ['/w/app']);
  assert.equal(fired.length, 2);
  const roots = provider.getChildren();
  assert.equal(provider.getTreeItem(roots[0]).label, 'Connection');
});

test('kiro stop asks first', async () => {
  const sent = [];
  const node = { cwd: '/w/infra', label: 'infra' };
  await confirmKiroStop(node, async () => undefined, (cmd) => sent.push(cmd));
  assert.deepEqual(sent, []);
  await confirmKiroStop(node, async (message, options, button) => {
    assert.match(message, /Stop the Kiro session in infra\? This ends the conversation\./);
    assert.equal(options.modal, true);
    return button;
  }, (cmd) => sent.push(cmd));
  assert.deepEqual(sent, [{ cmd: 'kiro_stop', cwd: '/w/infra' }]);
});

test('manifest contributes the panel and its title buttons', () => {
  const manifest = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'package.json'), 'utf8'));
  const container = manifest.contributes.viewsContainers.activitybar[0];
  assert.equal(container.id, 'psvAgentRemote');
  assert.ok(fs.existsSync(path.join(__dirname, '..', container.icon)));
  assert.equal(manifest.contributes.views.psvAgentRemote[0].id, 'psvAgentRemote.panel');
  const title = manifest.contributes.menus['view/title'].map((m) => m.command);
  for (const command of ['psvAgentRemote.restart', 'psvAgentRemote.configure', 'psvAgentRemote.logs'])
    assert.ok(title.includes(command), command);
  const inline = manifest.contributes.menus['view/item/context'];
  for (const [command, item] of [['psvAgentRemote.kiroStart', 'kiroStopped'], ['psvAgentRemote.kiroStop', 'kiroRunning'],
    ['psvAgentRemote.restore', 'removedCodex']])
    assert.ok(inline.some((m) => m.command === command && m.when.includes(item)), command);
  assert.ok(fs.readFileSync(path.join(__dirname, '..', '.vscodeignore'), 'utf8').includes('!media/panel.svg'));
});
