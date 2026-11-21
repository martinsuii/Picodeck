'use strict';

// Store proxy: the PicoPad has no TLS, so the companion fetches the catalogue
// and app packages over HTTPS and streams them as base64 lines over USB.
//
// Device -> host:  STORE LIST
//                  STORE GET <id>
// Host -> device:  STORE <n> / ENTRY id|version|size|name / END
//                  FILE <size> <NAME.PAP> / DATA <base64> / END

const http = require('http');
const https = require('https');

const DEFAULT_URL =
  process.env.STORE_URL || 'https://pico.msuii.dev/assets/apps/index.txt';

function fetchBuffer(url, max = 8 * 1024 * 1024) {
  return new Promise((resolve, reject) => {
    const lib = url.startsWith('https:') ? https : http;
    const req = lib.get(url, { timeout: 20000 }, (res) => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
        res.resume();
        resolve(fetchBuffer(new URL(res.headers.location, url).toString(), max));
        return;
      }
      if (res.statusCode !== 200) {
        res.resume();
        reject(new Error(`HTTP ${res.statusCode}`));
        return;
      }
      const chunks = [];
      let total = 0;
      res.on('data', (c) => {
        total += c.length;
        if (total > max) {
          req.destroy(new Error('Package too large'));
          return;
        }
        chunks.push(c);
      });
      res.on('end', () => resolve(Buffer.concat(chunks)));
    });
    req.on('timeout', () => req.destroy(new Error('Timeout')));
    req.on('error', reject);
  });
}

function parseIndex(text, baseUrl) {
  const items = [];
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith('#') || line.startsWith(';')) continue;
    const parts = line.split('|');
    if (parts.length < 2) continue;
    const [id, version, size, rel, name] = parts;
    if (!id || !version) continue;
    let url = rel || `${id}.pap`;
    try {
      url = new URL(url, baseUrl).toString();
    } catch {
      continue;
    }
    items.push({
      id: id.trim(),
      version: version.trim(),
      size: Number(size) || 0,
      url,
      name: (name || id).trim(),
    });
  }
  return items;
}

let cache = { url: null, at: 0, items: null };

async function getIndex() {
  const url = DEFAULT_URL;
  if (cache.items && cache.url === url && Date.now() - cache.at < 60000) {
    return cache.items;
  }
  const text = (await fetchBuffer(url)).toString('utf8');
  const items = parseIndex(text, url);
  cache = { url, at: Date.now(), items };
  return items;
}

async function handle(line, device) {
  if (line === 'STORE LIST') {
    const items = await getIndex();
    device.send(`STORE ${items.length}`);
    for (const it of items) {
      device.send(`ENTRY ${it.id}|${it.version}|${it.size}|${it.name}`);
    }
    device.send('END');
    return true;
  }

  if (line.startsWith('STORE GET ')) {
    const id = line.slice(10).trim();
    const items = await getIndex();
    const it = items.find((x) => x.id.toLowerCase() === id.toLowerCase());
    if (!it) {
      device.send('ERR no such app');
      return true;
    }
    const buf = await fetchBuffer(it.url);
    const safe = it.id.replace(/[^A-Za-z0-9_]/g, '').slice(0, 8).toUpperCase();
    device.send(`FILE ${buf.length} ${safe}.PAP`);
    const b64 = buf.toString('base64');
    for (let i = 0; i < b64.length; i += 60) {
      device.send('DATA ' + b64.slice(i, i + 60));
      // Give the CDC link a moment so the PicoPad can drain the lines.
      if ((i / 60) % 8 === 7) await new Promise((r) => setTimeout(r, 10));
    }
    device.send('END');
    return true;
  }

  return false;
}

module.exports = { handle, getIndex, DEFAULT_URL };
