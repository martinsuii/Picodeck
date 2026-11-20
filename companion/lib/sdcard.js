'use strict';

const fs = require('fs');
const fsp = fs.promises;
const path = require('path');
const { spawn } = require('child_process');
const config = require('./config');

function currentRoot() {
  const d = config.detectSd();
  return d.path;
}

function safeJoin(root, rel) {
  if (!root) throw new Error('No SD card selected');
  const clean = String(rel || '').replace(/\\/g, '/').replace(/^\/+/, '');
  if (clean.split('/').includes('..')) throw new Error('Invalid path');
  const full = path.resolve(root, clean);
  const rootResolved = path.resolve(root);
  if (full !== rootResolved && !full.startsWith(rootResolved + path.sep)) {
    throw new Error('Path outside the SD card');
  }
  // Refuse to follow a symlink that escapes the card.
  try {
    const real = fs.realpathSync(full);
    const realRoot = fs.realpathSync(rootResolved);
    if (real !== realRoot && !real.startsWith(realRoot + path.sep)) {
      throw new Error('Symlink outside the SD card');
    }
  } catch (e) {
    if (e.code !== 'ENOENT') throw e;
  }
  return full;
}

async function stat() {
  const root = currentRoot();
  if (!root) return { mounted: false, path: null };
  let total = 0, free = 0;
  try {
    const s = await fsp.statfs(root);
    total = s.blocks * s.bsize;
    free = s.bavail * s.bsize;
  } catch {
    /* statfs is not available on every platform */
  }
  let label = path.basename(root);
  return { mounted: true, path: root, label, total, free };
}

async function list(rel) {
  const root = currentRoot();
  const dir = safeJoin(root, rel);
  const names = await fsp.readdir(dir, { withFileTypes: true });
  const entries = [];
  for (const ent of names) {
    if (ent.name === '.' || ent.name === '..') continue;
    const full = path.join(dir, ent.name);
    let st;
    try {
      st = await fsp.stat(full);
    } catch {
      continue;
    }
    entries.push({
      name: ent.name,
      dir: st.isDirectory(),
      size: st.isDirectory() ? 0 : st.size,
      mtime: st.mtimeMs,
    });
  }
  entries.sort((a, b) => (a.dir === b.dir ? a.name.localeCompare(b.name) : a.dir ? -1 : 1));
  return { root, path: String(rel || '').replace(/^\/+/, ''), entries };
}

function read(rel) {
  return fsp.readFile(safeJoin(currentRoot(), rel));
}

async function write(rel, data) {
  const full = safeJoin(currentRoot(), rel);
  await fsp.mkdir(path.dirname(full), { recursive: true });
  await fsp.writeFile(full, data);
  return { path: rel, size: data.length };
}

async function remove(rel) {
  const full = safeJoin(currentRoot(), rel);
  const st = await fsp.lstat(full);
  if (st.isDirectory()) await fsp.rm(full, { recursive: true, force: true });
  else await fsp.unlink(full);
  return { removed: rel };
}

async function mkdir(rel) {
  const full = safeJoin(currentRoot(), rel);
  await fsp.mkdir(full, { recursive: true });
  return { created: rel };
}

async function rename(from, to) {
  const a = safeJoin(currentRoot(), from);
  const b = safeJoin(currentRoot(), to);
  await fsp.mkdir(path.dirname(b), { recursive: true });
  await fsp.rename(a, b);
  return { from, to };
}

async function prepare() {
  const root = currentRoot();
  if (!root) throw new Error('No SD card selected');
  const resolved = path.resolve(root);
  const parsed = path.parse(resolved);
  if (parsed.root === resolved) throw new Error('Refusing to prepare a filesystem root');

  const forbidden = ['/home', '/root', '/etc', '/usr', '/var', '/boot', '/dev', '/proc', '/sys'];
  for (const f of forbidden) {
    if (resolved === f || resolved.startsWith(f + path.sep)) {
      throw new Error('Refusing to prepare a system folder');
    }
  }

  const names = await fsp.readdir(resolved);
  for (const name of names) {
    await fsp.rm(path.join(resolved, name), { recursive: true, force: true });
  }

  const folders = ['Apps', 'Arcade', 'Backups', 'Conf', 'Themes'];
  for (const d of folders) {
    await fsp.mkdir(path.join(resolved, d), { recursive: true });
  }

  const marker = 'PicoDeck first boot\n';
  await fsp.writeFile(path.join(resolved, 'FRESH.TXT'), marker);
  await fsp.writeFile(path.join(resolved, '.fresh'), marker);

  return { prepared: true, root: resolved, folders };
}

async function stageUpdate(data) {
  const root = currentRoot();
  if (!root) throw new Error('No SD card selected');
  const dest = path.join(root, 'update.uf2');
  let backup = null;
  if (fs.existsSync(dest)) {
    backup = path.join(root, 'update.uf2.bak');
    await fsp.copyFile(dest, backup);
  }
  await fsp.writeFile(dest, data);
  return { path: 'update.uf2', size: data.length, backup: backup ? 'update.uf2.bak' : null };
}

async function backup() {
  const root = currentRoot();
  if (!root) throw new Error('No SD card selected');
  const tmp = path.join(require('os').tmpdir(), `picopad-sd-${Date.now()}.tar.gz`);
  await new Promise((resolve, reject) => {
    const child = spawn('tar', ['-czf', tmp, '-C', root, '.'], { stdio: 'ignore' });
    child.on('error', reject);
    child.on('close', (code) =>
      code === 0 ? resolve() : reject(new Error(`tar exited with ${code}`))
    );
  });
  return { file: tmp, name: `picopad-sd-${new Date().toISOString().slice(0, 10)}.tar.gz` };
}

async function restore(uploadPath) {
  const root = currentRoot();
  if (!root) throw new Error('No SD card selected');
  await new Promise((resolve, reject) => {
    const child = spawn('tar', ['-xzf', uploadPath, '-C', root, '--no-same-owner'], {
      stdio: 'ignore',
    });
    child.on('error', reject);
    child.on('close', (code) =>
      code === 0 ? resolve() : reject(new Error(`tar exited with ${code}`))
    );
  });
  return { restored: true };
}

function localArtifact() {
  const dir = path.join(__dirname, '..', '..', 'launcher');
  const names = [
    'PICODECK_update.uf2',
    'PICODECK_install.uf2',
    'ARCADEMULTITOOL_update.uf2',
    'ARCADEMULTITOOL_install.uf2',
  ];
  for (const name of names) {
    const p = path.join(dir, name);
    try {
      const st = fs.statSync(p);
      if (st.isFile()) return { path: p, name, size: st.size, mtime: st.mtimeMs };
    } catch {
      /* ignore */
    }
  }
  return null;
}

module.exports = {
  currentRoot, detect: config.detectSd, stat, list, read, write, remove, mkdir,
  rename, prepare, stageUpdate, backup, restore, localArtifact, safeJoin,
};
