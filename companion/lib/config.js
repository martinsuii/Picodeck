'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');

const FILE = path.join(__dirname, '..', 'config.json');

const DEFAULTS = {
  sdPath: null,
  espHost: 'picopad.local',
  espPort: 2323,
  serialPort: null,
  serialBaud: 115200,
};

function load() {
  try {
    const raw = fs.readFileSync(FILE, 'utf8');
    return { ...DEFAULTS, ...JSON.parse(raw) };
  } catch {
    return { ...DEFAULTS };
  }
}

function save(patch) {
  const next = { ...load(), ...patch };
  fs.writeFileSync(FILE, JSON.stringify(next, null, 2) + '\n');
  return next;
}

function candidates() {
  const user = os.userInfo().username;
  const roots = [
    `/run/media/${user}`,
    `/media/${user}`,
    `/media`,
    `/mnt`,
    `/Volumes`,
  ];
  const found = [];

  for (const root of roots) {
    let names;
    try {
      names = fs.readdirSync(root);
    } catch {
      continue;
    }
    for (const name of names) {
      const full = path.join(root, name);
      let st;
      try {
        st = fs.statSync(full);
      } catch {
        continue;
      }
      if (!st.isDirectory()) continue;
      // Some distros mount at /media/<label> rather than /media/<user>/<label>.
      if (root === `/media/${user}` || root === `/run/media/${user}` || root === '/Volumes') {
        found.push(full);
      } else {
        try {
          if (fs.statSync(path.join(full, '..')).isDirectory()) found.push(full);
        } catch {
          /* ignore */
        }
      }
    }
  }

  // Deduplicate and keep the most likely SD cards first.
  const seen = new Set();
  const scored = [];
  for (const p of found) {
    if (seen.has(p)) continue;
    seen.add(p);
    scored.push({ path: p, score: scoreCard(p) });
  }
  scored.sort((a, b) => b.score - a.score || a.path.localeCompare(b.path));
  return scored;
}

function scoreCard(p) {
  let score = 0;
  const markers = ['Themes', 'themes', 'arcade', 'payloads', 'web'];
  for (const m of markers) {
    try {
      if (fs.existsSync(path.join(p, m))) score += 2;
    } catch {
      /* ignore */
    }
  }
  try {
    for (const name of fs.readdirSync(p)) {
      if (name.toLowerCase().endsWith('.uf2')) score += 3;
      if (name.toLowerCase() === 'update.uf2') score += 2;
    }
  } catch {
    /* ignore */
  }
  return score;
}

function detectSd() {
  const cfg = load();
  if (cfg.sdPath && fs.existsSync(cfg.sdPath)) {
    return { path: cfg.sdPath, source: 'config', candidates: candidates() };
  }
  const list = candidates();
  // Only auto-pick a mount that actually looks like a PicoPad card. The
  // user can still choose any mount manually from the list.
  const likely = list.find((c) => c.score > 0);
  if (likely) return { path: likely.path, source: 'auto', candidates: list };
  return { path: null, source: 'none', candidates: list };
}

module.exports = { load, save, detectSd, candidates, DEFAULTS, FILE };
