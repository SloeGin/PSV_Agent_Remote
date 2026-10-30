'use strict';

// The PSV Agent Remote side-bar panel: what the Vita sees, and Kiro sessions for
// this window's folders. buildTree is pure (tested without VS Code); makeProvider
// adapts it to a TreeDataProvider.

const STATE_TEXT = { waiting: 'waiting for you', running: 'working', idle: 'idle' };
const STATE_ICON = { waiting: ['bell', 'charts.yellow'], running: ['sync', undefined], idle: ['circle-outline', undefined] };
const AGENTS = [['claude', 'Claude Code'], ['codex', 'Codex'], ['kiro', 'Kiro']];

function folderName(cwd) {
  return cwd.split(/[\\/]/).filter(Boolean).pop() || cwd;
}

function info(id, label, icon = 'info') {
  return { id, label, icon: [icon, undefined] };
}

function connection(status) {
  const broker = { id: 'broker', label: 'Broker', icon: ['pass', 'testing.iconPassed'], description: 'reachable' };
  if (status.state === 'broker') Object.assign(broker, { description: 'unreachable', icon: ['error', 'errorForeground'], tooltip: status.message });
  else if (status.state !== 'running') Object.assign(broker, { description: status.state || 'stopped', icon: ['circle-slash', undefined], tooltip: status.message });
  const vita = { id: 'vita', label: 'Vita' };
  if (!status.vita) Object.assign(vita, { description: 'unknown', icon: ['question', undefined] });
  else {
    const online = status.vita.online === true ? 'online' : status.vita.online === false ? 'offline' : 'unknown';
    vita.description = status.vita.mode ? `${online} · ${status.vita.mode}` : online;
    vita.icon = status.vita.online ? ['device-mobile', 'testing.iconPassed'] : ['device-mobile', undefined];
  }
  return { id: 'connection', label: 'Connection', expanded: true, children: [broker, vita] };
}

function sessions(status) {
  if (!('sessions' in status)) return { id: 'sessions', label: 'Sessions', expanded: true,
    children: [info('sessions:update', 'Run Reconnect to update the bridge manager')] };
  if (status.sessions === null) return { id: 'sessions', label: 'Sessions', expanded: true,
    children: [info('sessions:down', "Can't see the broker right now", 'warning')] };
  const removed = status.removed || [];
  if (!status.sessions.length && !removed.length) return { id: 'sessions', label: 'Sessions (0)', expanded: true,
    children: [info('sessions:none', 'No sessions on the broker')] };
  const groups = [];
  for (const [agent, label] of AGENTS) {
    const children = status.sessions.filter((s) => s.agent === agent).map((s) => ({
      id: `session:${s.sid}`, label: s.project, description: STATE_TEXT[s.state] || s.state,
      icon: STATE_ICON[s.state] || ['circle-outline', undefined],
      tooltip: `${label} · ${s.project} · ${STATE_TEXT[s.state] || s.state}${s.local ? ' · bridged by this computer' : ''}`,
    }));
    if (agent === 'codex') for (const r of removed) children.push({
      id: `removed:${r.sid}`, label: r.project, description: 'removed on Vita', icon: ['eye-closed', undefined],
      contextValue: 'removedCodex', sid: r.sid, tooltip: 'Removed on the Vita. Show on Vita brings it back.',
    });
    if (children.length) groups.push({ id: `agent:${agent}`, label, expanded: true, children });
  }
  return { id: 'sessions', label: `Sessions (${status.sessions.length})`, expanded: true, children: groups };
}

function kiro(status, folders) {
  if (!folders.length) return { id: 'kiro', label: 'Kiro in this window', expanded: true,
    children: [info('kiro:none', 'Open a folder to start a Kiro session')] };
  const available = Boolean(status.kiro);
  const bridges = available ? status.kiro.bridges : [];
  const errors = available ? status.kiro.errors || {} : {};
  return { id: 'kiro', label: 'Kiro in this window', expanded: true, children: folders.map((cwd) => {
    const bridge = bridges.find((b) => b.cwd === cwd);
    const node = { id: `kiro:${cwd}`, label: folderName(cwd), cwd, tooltip: cwd };
    if (bridge) return Object.assign(node, { description: bridge.starting ? 'starting…' : 'running',
      icon: ['debug-start', 'testing.iconPassed'], contextValue: 'kiroRunning' });
    if (!available) return Object.assign(node, { description: 'not available', icon: ['circle-slash', undefined] });
    const error = errors[cwd];
    return Object.assign(node, { description: error || 'not started', icon: error ? ['warning', 'errorForeground'] : ['circle-outline', undefined],
      tooltip: error ? `${cwd}\n${error}` : cwd, contextValue: 'kiroStopped' });
  }) };
}

function buildTree(status, folders) {
  return [connection(status || {}), sessions(status || {}), kiro(status || {}, folders || [])];
}

function makeProvider(vscode) {
  const emitter = new vscode.EventEmitter();
  let roots = buildTree({}, []);
  let rendered = JSON.stringify(roots);
  return {
    onDidChangeTreeData: emitter.event,
    update(status, folders) {
      const next = buildTree(status, folders);
      const text = JSON.stringify(next);
      if (text === rendered) return; // status lines repeat every few seconds
      roots = next; rendered = text;
      emitter.fire(undefined);
    },
    getChildren(node) { return node ? node.children || [] : roots; },
    getTreeItem(node) {
      const state = node.children ? (node.expanded ? vscode.TreeItemCollapsibleState.Expanded : vscode.TreeItemCollapsibleState.Collapsed)
        : vscode.TreeItemCollapsibleState.None;
      const item = new vscode.TreeItem(node.label, state);
      item.id = node.id;
      if (node.description) item.description = node.description;
      if (node.tooltip) item.tooltip = node.tooltip;
      if (node.contextValue) item.contextValue = node.contextValue;
      if (node.icon) item.iconPath = new vscode.ThemeIcon(node.icon[0], node.icon[1] ? new vscode.ThemeColor(node.icon[1]) : undefined);
      return item;
    },
  };
}

async function confirmKiroStop(node, showWarningMessage, send) {
  const button = 'Stop';
  const choice = await showWarningMessage(`Stop the Kiro session in ${node.label}? This ends the conversation.`,
    { modal: true }, button);
  if (choice === button) send({ cmd: 'kiro_stop', cwd: node.cwd });
}

module.exports = { buildTree, makeProvider, confirmKiroStop };
