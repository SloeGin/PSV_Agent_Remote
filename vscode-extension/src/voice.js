'use strict';

// The default whisper.cpp model for voice replies, fetched on the machine that
// runs the workspace (where the bridge manager transcribes).

const fs = require('node:fs');
const https = require('node:https');
const os = require('node:os');
const path = require('node:path');

const MODEL_URL = 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin';
const MAX_REDIRECTS = 5;

function defaultModelPath() {
  return path.join(os.homedir(), '.cache/psv-agent-remote/models/ggml-large-v3-turbo-q5_0.bin');
}

function fetch(url, get, redirects) {
  return new Promise((resolve, reject) => {
    const req = get(url, (res) => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
        res.resume();
        if (redirects >= MAX_REDIRECTS) { reject(new Error('Too many redirects')); return; }
        resolve(fetch(new URL(res.headers.location, url).toString(), get, redirects + 1));
      } else if (res.statusCode !== 200) {
        res.resume();
        reject(new Error(`HTTP ${res.statusCode}`));
      } else resolve(res);
    });
    req.on('error', reject);
  });
}

// Downloads to dest.part and renames it when complete, so a failed or
// interrupted download never leaves a truncated model where whisper looks.
async function downloadModel({ url = MODEL_URL, dest, get = https.get, onProgress = () => {} }) {
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  const part = `${dest}.part`;
  try {
    const res = await fetch(url, get, 0);
    const total = Number(res.headers['content-length']) || 0;
    let done = 0;
    await new Promise((resolve, reject) => {
      const out = fs.createWriteStream(part);
      res.on('data', (chunk) => { done += chunk.length; onProgress(done, total); });
      res.on('error', reject);
      out.on('error', reject);
      out.on('finish', resolve);
      res.pipe(out);
    });
    fs.renameSync(part, dest);
  } catch (error) {
    fs.rmSync(part, { force: true });
    throw error;
  }
}

module.exports = { MODEL_URL, defaultModelPath, downloadModel };
