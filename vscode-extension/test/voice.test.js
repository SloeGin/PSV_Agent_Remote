'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { EventEmitter } = require('node:events');
const { PassThrough } = require('node:stream');
const { MODEL_URL, defaultModelPath, downloadModel } = require('../src/voice');

function response(status, headers, body) {
  const res = new PassThrough();
  res.statusCode = status;
  res.headers = headers;
  setImmediate(() => res.end(body || ''));
  return res;
}

function fakeGet(responses, seen) {
  return (url, callback) => {
    seen.push(url);
    const req = new EventEmitter();
    const next = responses.shift();
    setImmediate(() => (next instanceof Error ? req.emit('error', next) : callback(next)));
    return req;
  };
}

function tmp() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'psv-voice-'));
}

test('default model path and url', () => {
  assert.equal(defaultModelPath(), path.join(os.homedir(), '.cache/psv-agent-remote/models/ggml-large-v3-turbo-q5_0.bin'));
  assert.match(MODEL_URL, /^https:\/\/huggingface\.co\/ggerganov\/whisper\.cpp\/resolve\/main\/ggml-large-v3-turbo-q5_0\.bin$/);
});

test('download follows redirect and renames', async () => {
  const dir = tmp();
  const dest = path.join(dir, 'models', 'm.bin');
  const seen = [];
  const progress = [];
  await downloadModel({ url: 'https://a/m.bin', dest,
    get: fakeGet([response(302, { location: 'https://b/m.bin' }), response(200, { 'content-length': '5' }, 'hello')], seen),
    onProgress: (done, total) => progress.push([done, total]) });
  assert.deepEqual(seen, ['https://a/m.bin', 'https://b/m.bin']);
  assert.equal(fs.readFileSync(dest, 'utf8'), 'hello');
  assert.ok(!fs.existsSync(dest + '.part'));
  assert.deepEqual(progress.at(-1), [5, 5]);
});

test('download failure removes part', async () => {
  const dir = tmp();
  const dest = path.join(dir, 'm.bin');
  await assert.rejects(downloadModel({ url: 'https://a/m.bin', dest, get: fakeGet([response(404, {})], []) }), /404/);
  assert.ok(!fs.existsSync(dest + '.part') && !fs.existsSync(dest));
  await assert.rejects(downloadModel({ url: 'https://a/m.bin', dest, get: fakeGet([new Error('offline')], []) }), /offline/);
  assert.ok(!fs.existsSync(dest + '.part'));
});

test('too many redirects fail', async () => {
  const dest = path.join(tmp(), 'm.bin');
  const loop = Array.from({ length: 7 }, () => response(302, { location: 'https://a/m.bin' }));
  await assert.rejects(downloadModel({ url: 'https://a/m.bin', dest, get: fakeGet(loop, []) }), /redirect/i);
});
