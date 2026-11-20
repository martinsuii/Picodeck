'use strict';

const http = require('http');
const fs = require('fs');
const fsp = fs.promises;
const os = require('os');
const path = require('path');

const config = require('./lib/config');
const sd = require('./lib/sdcard');
const uf2 = require('./lib/uf2');
const theme = require('./lib/theme');
const store = require('./lib/store');
const { Device, serialAvailable, serialError } = require('./lib/device');

const PORT = Number(process.env.PORT || argValue('--port') || 8787);
const HOST = process.env.HOST || argValue('--host') || '0.0.0.0';
const WEB = path.join(__dirname, 'web');
const ASSETS = path.join(__dirname, 'assets');
const REPO_THEMES = path.join(__dirname, '..', 'themes');

const device = new Device();
const sse = new Set();

function argValue(name) {
  const i = process.argv.indexOf(name);
  return i >= 0 ? process.argv[i + 1] : null;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function sendJson(res, code, obj) {
  const body = Buffer.from(JSON.stringify(obj));
  res.writeHead(code, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': body.length,
    'Cache-Control': 'no-store',
  });
  res.end(body);
}

function sendText(res, code, text) {
  const body = Buffer.from(text);
  res.writeHead(code, { 'Content-Type': 'text/plain; charset=utf-8', 'Content-Length': body.length });
  res.end(body);
}

function error(res, code, message) {
  sendJson(res, code, { error: message });
}

function mime(file) {
  const ext = path.extname(file).toLowerCase();
  return (
    {
      '.html': 'text/html; charset=utf-8',
      '.js': 'text/javascript; charset=utf-8',
      '.css': 'text/css; charset=utf-8',
      '.json': 'application/json; charset=utf-8',
      '.webmanifest': 'application/manifest+json',
      '.png': 'image/png',
      '.bmp': 'image/bmp',
      '.svg': 'image/svg+xml',
      '.ico': 'image/x-icon',
      '.txt': 'text/plain; charset=utf-8',
      '.thm': 'text/plain; charset=utf-8',
    }[ext] || 'application/octet-stream'
  );
}

function readBody(req, max) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let total = 0;
    req.on('data', (c) => {
      total += c.length;
      if (total > max) {
        reject(new Error(`Body too large (limit ${(max / 1024 / 1024).toFixed(0)} MB)`));
        req.destroy();
        return;
      }
      chunks.push(c);
    });
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

async function readJson(req, max = 1024 * 1024) {
  const body = await readBody(req, max);
  if (!body.length) return {};
  return JSON.parse(body.toString('utf8'));
}

function themePath(name, source) {
  if (!/^[A-Za-z0-9._ -]+$/.test(name) || name.includes('..')) {
    throw new Error('Invalid theme name');
  }
  if (source === 'repo') {
    return path.join(REPO_THEMES, name);
  }
  const root = sd.currentRoot();
  if (!root) throw new Error('No SD card selected');
  const candidates = [path.join(root, 'themes', name), path.join(root, 'Themes', name)];
  if (fs.existsSync(candidates[0])) return candidates[0];
  if (fs.existsSync(candidates[1])) return candidates[1];
  return candidates[0];
}

async function listThemes(source) {
  const dir = source === 'repo' ? REPO_THEMES : null;
  let roots = [];
  if (dir) {
    roots = [dir];
  } else {
    const root = sd.currentRoot();
    if (!root) return { source: 'sd', themes: [], active: null };
    roots = [path.join(root, 'themes'), path.join(root, 'Themes')];
  }

  const names = new Set();
  for (const d of roots) {
    let entries;
    try {
      entries = await fsp.readdir(d, { withFileTypes: true });
    } catch {
      continue;
    }
    for (const ent of entries) {
      if (!ent.isFile()) continue;
      if (!/\.(thm|ini)$/i.test(ent.name)) continue;
      names.add(ent.name);
    }
  }

  let active = null;
  if (!dir) {
    for (const d of roots) {
      try {
        active = (await fsp.readFile(path.join(d, 'active.cfg'), 'utf8')).trim() || null;
        if (active) break;
      } catch {
        /* ignore */
      }
    }
  }

  return {
    source: dir ? 'repo' : 'sd',
    active,
    themes: [...names].sort().map((name) => {
      let file = null;
      for (const d of roots) {
        const p = path.join(d, name);
        if (fs.existsSync(p)) file = p;
      }
      const st = file ? fs.statSync(file) : null;
      return { name, size: st ? st.size : 0, mtime: st ? st.mtimeMs : 0 };
    }),
  };
}

function broadcast(event, data) {
  const msg = `event: ${event}\ndata: ${JSON.stringify(data)}\n\n`;
  for (const res of sse) {
    try {
      res.write(msg);
    } catch {
      sse.delete(res);
    }
  }
}

device.on('line', (line) => {
  broadcast('line', { line });

  // The PicoPad's App Store asks the companion to fetch over HTTPS.
  if (line.startsWith('STORE ')) {
    store.handle(line, device).catch((e) => {
      try {
        device.send('ERR ' + String(e.message || e).replace(/[\r\n]+/g, ' '));
      } catch {}
    });
  }
});
device.on('close', () => broadcast('state', device.state()));

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

async function api(req, res, url) {
  const p = url.pathname;
  const q = url.searchParams;

  if (p === '/api/health') return sendJson(res, 200, { ok: true, version: 1 });

  if (p === '/api/state') {
    return sendJson(res, 200, {
      sd: await sd.stat(),
      detect: config.detectSd(),
      device: device.state(),
      serial: { available: serialAvailable(), error: serialError() },
      config: config.load(),
    });
  }

  if (p === '/api/config') {
    if (req.method === 'GET') return sendJson(res, 200, config.load());
    if (req.method === 'POST') {
      const body = await readJson(req);
      const patch = {};
      if (typeof body.sdPath === 'string') patch.sdPath = body.sdPath || null;
      if (typeof body.espHost === 'string') patch.espHost = body.espHost.trim();
      if (body.espPort != null) patch.espPort = Number(body.espPort) || 2323;
      if (typeof body.serialPort === 'string') patch.serialPort = body.serialPort || null;
      if (body.serialBaud != null) patch.serialBaud = Number(body.serialBaud) || 115200;
      return sendJson(res, 200, config.save(patch));
    }
    return error(res, 405, 'Method not allowed');
  }

  // --- SD card -----------------------------------------------------------
  if (p === '/api/sd') {
    return sendJson(res, 200, { ...(await sd.stat()), detect: config.detectSd() });
  }

  if (p === '/api/sd/list') {
    try {
      return sendJson(res, 200, await sd.list(q.get('path') || ''));
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/sd/download') {
    try {
      const rel = q.get('path') || '';
      const full = sd.safeJoin(sd.currentRoot(), rel);
      const st = await fsp.stat(full);
      if (st.isDirectory()) return error(res, 400, 'Is a directory');
      res.writeHead(200, {
        'Content-Type': mime(full),
        'Content-Length': st.size,
        'Content-Disposition': `attachment; filename="${path.basename(full)}"`,
      });
      fs.createReadStream(full).pipe(res);
    } catch (e) {
      error(res, 404, e.message);
    }
    return;
  }

  if (p === '/api/sd/upload' && req.method === 'POST') {
    try {
      const rel = q.get('path');
      if (!rel) return error(res, 400, 'Missing path');
      const body = await readBody(req, 128 * 1024 * 1024);
      return sendJson(res, 200, await sd.write(rel, body));
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/sd/mkdir' && req.method === 'POST') {
    try {
      const body = await readJson(req);
      return sendJson(res, 200, await sd.mkdir(body.path));
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/sd/delete' && req.method === 'POST') {
    try {
      const body = await readJson(req);
      return sendJson(res, 200, await sd.remove(body.path));
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/sd/rename' && req.method === 'POST') {
    try {
      const body = await readJson(req);
      return sendJson(res, 200, await sd.rename(body.from, body.to));
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/sd/prepare' && req.method === 'POST') {
    try {
      const body = await readJson(req);
      if (body.confirm !== 'PREPARE') {
        return error(res, 400, 'Type PREPARE to confirm');
      }
      return sendJson(res, 200, await sd.prepare());
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/sd/backup') {
    try {
      const r = await sd.backup();
      res.writeHead(200, {
        'Content-Type': 'application/gzip',
        'Content-Disposition': `attachment; filename="${r.name}"`,
      });
      const stream = fs.createReadStream(r.file);
      stream.pipe(res);
      stream.on('close', () => fsp.unlink(r.file).catch(() => {}));
    } catch (e) {
      error(res, 500, e.message);
    }
    return;
  }

  if (p === '/api/sd/restore' && req.method === 'POST') {
    let tmp = null;
    try {
      const body = await readBody(req, 512 * 1024 * 1024);
      tmp = path.join(os.tmpdir(), `picopad-restore-${Date.now()}.tar.gz`);
      await fsp.writeFile(tmp, body);
      return sendJson(res, 200, await sd.restore(tmp));
    } catch (e) {
      return error(res, 500, e.message);
    } finally {
      if (tmp) fsp.unlink(tmp).catch(() => {});
    }
  }

  // --- Themes ------------------------------------------------------------
  if (p === '/api/themes') {
    try {
      return sendJson(res, 200, await listThemes(q.get('source') || 'sd'));
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/themes/file') {
    try {
      const source = q.get('source') || 'sd';
      const name = q.get('name');
      const full = themePath(name, source);
      if (req.method === 'GET') {
        const text = await fsp.readFile(full, 'utf8');
        const st = await fsp.stat(full);
        return sendJson(res, 200, { name, source, size: st.size, text, mtime: st.mtimeMs });
      }
      if (req.method === 'PUT') {
        const body = await readJson(req, 256 * 1024);
        await fsp.mkdir(path.dirname(full), { recursive: true });
        await fsp.writeFile(full, String(body.text ?? ''), 'utf8');
        const st = await fsp.stat(full);
        return sendJson(res, 200, { name, source, size: st.size, mtime: st.mtimeMs });
      }
      return error(res, 405, 'Method not allowed');
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/themes/active' && req.method === 'POST') {
    try {
      const body = await readJson(req);
      const root = sd.currentRoot();
      if (!root) return error(res, 409, 'No SD card selected');
      const dir = fs.existsSync(path.join(root, 'themes')) ? 'themes' : 'Themes';
      const cfg = path.join(root, dir, 'active.cfg');
      await fsp.mkdir(path.dirname(cfg), { recursive: true });
      if (body.name) await fsp.writeFile(cfg, String(body.name).trim() + '\n');
      else await fsp.unlink(cfg).catch(() => {});
      return sendJson(res, 200, { active: body.name || null });
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  // --- UF2 ---------------------------------------------------------------
  if (p === '/api/uf2/inspect' && req.method === 'POST') {
    try {
      const body = await readBody(req, 8 * 1024 * 1024);
      return sendJson(res, 200, { report: uf2.inspect(body) });
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/uf2/stage' && req.method === 'POST') {
    try {
      const body = await readBody(req, 8 * 1024 * 1024);
      const report = uf2.inspect(body);
      if (!report.valid) return error(res, 400, 'Not a valid UF2 image');
      const staged = await sd.stageUpdate(body);
      return sendJson(res, 200, { report, staged });
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/uf2/local') {
    const local = sd.localArtifact();
    return sendJson(res, 200, { local });
  }

  if (p === '/api/uf2/local/file') {
    const local = sd.localArtifact();
    if (!local) return error(res, 404, 'No build artifact found');
    res.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Content-Length': local.size,
      'Content-Disposition': `attachment; filename="${local.name}"`,
    });
    fs.createReadStream(local.path).pipe(res);
    return;
  }

  if (p === '/api/themes/parse' && req.method === 'POST') {
    try {
      const body = await readJson(req, 1024 * 1024);
      return sendJson(res, 200, { theme: theme.parse(String(body.text ?? '')) });
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  // --- Device ------------------------------------------------------------
  if (p === '/api/device/ports') {
    return sendJson(res, 200, {
      serialAvailable: serialAvailable(),
      serialError: serialError(),
      ports: await device.listPorts(),
    });
  }

  if (p === '/api/device/state') return sendJson(res, 200, device.state());

  if (p === '/api/device/connect' && req.method === 'POST') {
    try {
      const body = await readJson(req);
      const cfg = config.load();
      const state = await device.connect({
        transport: body.transport === 'wifi' ? 'wifi' : 'usb',
        host: body.host || cfg.espHost,
        port: Number(body.port || cfg.espPort),
        path: body.path || cfg.serialPort,
        baud: Number(body.baud || cfg.serialBaud),
      });
      broadcast('state', state);
      return sendJson(res, 200, state);
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/device/disconnect' && req.method === 'POST') {
    device.close();
    broadcast('state', device.state());
    return sendJson(res, 200, device.state());
  }

  if (p === '/api/device/send' && req.method === 'POST') {
    try {
      const body = await readJson(req);
      device.send(body.line, body.target || 'pico');
      return sendJson(res, 200, { sent: body.line });
    } catch (e) {
      return error(res, 400, e.message);
    }
  }

  if (p === '/api/device/events') {
    res.writeHead(200, {
      'Content-Type': 'text/event-stream; charset=utf-8',
      'Cache-Control': 'no-store',
      Connection: 'keep-alive',
      'X-Accel-Buffering': 'no',
    });
    res.write('retry: 2000\n\n');
    res.write(`event: state\ndata: ${JSON.stringify(device.state())}\n\n`);
    sse.add(res);
    const timer = setInterval(() => {
      try {
        res.write(': ping\n\n');
      } catch {
        clearInterval(timer);
        sse.delete(res);
      }
    }, 25000);
    req.on('close', () => {
      clearInterval(timer);
      sse.delete(res);
    });
    return;
  }

  return error(res, 404, 'Unknown API endpoint');
}

// ---------------------------------------------------------------------------
// Static files
// ---------------------------------------------------------------------------

function serveFile(res, base, rel) {
  const full = path.resolve(base, rel);
  if (full !== path.resolve(base) && !full.startsWith(path.resolve(base) + path.sep)) {
    return error(res, 403, 'Forbidden');
  }
  fs.stat(full, (err, st) => {
    if (err || !st.isFile()) return error(res, 404, 'Not found');
    res.writeHead(200, {
      'Content-Type': mime(full),
      'Content-Length': st.size,
      'Cache-Control': 'no-cache',
    });
    fs.createReadStream(full).pipe(res);
  });
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  try {
    if (url.pathname.startsWith('/api/')) {
      await api(req, res, url);
      return;
    }
    if (url.pathname.startsWith('/assets/')) {
      return serveFile(res, ASSETS, url.pathname.slice('/assets/'.length));
    }
    if (url.pathname.startsWith('/lib/')) {
      return serveFile(res, path.join(__dirname, 'lib'), url.pathname.slice('/lib/'.length));
    }
    const rel = url.pathname === '/' ? 'index.html' : url.pathname.slice(1);
    return serveFile(res, WEB, rel);
  } catch (e) {
    if (!res.headersSent) error(res, 500, e.message);
    else res.end();
  }
});

function lanUrls(port) {
  const out = [];
  for (const list of Object.values(os.networkInterfaces())) {
    for (const ni of list || []) {
      if (ni.family === 'IPv4' && !ni.internal) out.push(`http://${ni.address}:${port}`);
    }
  }
  return out;
}

function banner(port) {
  const lines = [
    '',
    '  PicoDeck Companion',
    `  Local:   http://localhost:${port}`,
  ];
  for (const u of lanUrls(port)) lines.push(`  Network: ${u}   (open this on your phone)`);
  const d = config.detectSd();
  lines.push(`  SD card: ${d.path || 'not found - set it in the app'}`);
  lines.push(`  USB serial: ${serialAvailable() ? 'available' : 'not installed (npm install)'}`);
  lines.push('');
  console.log(lines.join('\n'));
}

/**
 * Start the HTTP API/UI. Used by the CLI, the Electron shell and tests.
 * @param {{port?: number, host?: string, log?: boolean}} opts
 */
function start(opts = {}) {
  const port = opts.port == null ? PORT : Number(opts.port);
  const host = opts.host || HOST;
  return new Promise((resolve, reject) => {
    const onError = (e) => reject(e);
    server.once('error', onError);
    server.listen(port, host, () => {
      server.removeListener('error', onError);
      const actual = server.address().port;
      if (opts.log !== false) banner(actual);
      resolve({ port: actual, host, url: `http://localhost:${actual}/` });
    });
  });
}

if (require.main === module) {
  start().catch((e) => {
    console.error(`Companion failed to start: ${e.message}`);
    process.exit(1);
  });
}

module.exports = { start, server, lanUrls };

