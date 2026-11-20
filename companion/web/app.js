'use strict';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => [...document.querySelectorAll(sel)];

const state = {
  tab: 'sd',
  sdPath: '',
  sd: null,
  source: 'sd',
  themes: [],
  activeTheme: null,
  theme: null,
  themeDirty: false,
  screen: 'home',
  preview: null,
  uf2Buffer: null,
  uf2Report: null,
  transport: 'usb',
  connected: false,
  apps: [],
  deviceThemes: [],
  collecting: null,
};

async function api(path, opts = {}) {
  const res = await fetch(path, opts);
  let body = null;
  const type = res.headers.get('content-type') || '';
  if (type.includes('application/json')) body = await res.json();
  else body = await res.text();
  if (!res.ok) {
    const msg = body && body.error ? body.error : `Request failed (${res.status})`;
    throw new Error(msg);
  }
  return body;
}

function postJson(path, obj) {
  return api(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(obj || {}),
  });
}

function putJson(path, obj) {
  return api(path, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(obj || {}),
  });
}

let toastTimer = null;
function toast(msg, bad = false) {
  const el = $('#toast');
  el.textContent = msg;
  el.classList.toggle('bad', bad);
  el.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (el.hidden = true), bad ? 5000 : 2600);
}

function fmtSize(n) {
  if (n == null) return '—';
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(n < 10240 ? 1 : 0)} KB`;
  if (n < 1024 * 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MB`;
  return `${(n / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

function fmtTime(ms) {
  if (!ms) return '—';
  const d = new Date(ms);
  return d.toLocaleString(undefined, {
    year: '2-digit', month: 'short', day: '2-digit', hour: '2-digit', minute: '2-digit',
  });
}

function esc(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function pct(part, total) {
  if (!total) return 0;
  return Math.max(0, Math.min(100, (part / total) * 100));
}

function setPill(id, text, kind) {
  const el = $(id);
  el.textContent = text;
  el.classList.remove('good', 'bad', 'warn');
  if (kind) el.classList.add(kind);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

function showTab(name) {
  state.tab = name;
  $$('.tabbtn').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
  $$('.tabpane').forEach((p) => p.classList.toggle('active', p.id === `tab-${name}`));
  if (name === 'themes' && !state.themes.length) loadThemes();
  if (name === 'sd') loadSd(state.sdPath);
  if (name === 'updates') refreshLocalArtifact();
  window.scrollTo({ top: 0, behavior: 'smooth' });
}

$$('.tabbtn').forEach((b) => b.addEventListener('click', () => showTab(b.dataset.tab)));

$$('[data-quick]').forEach((b) =>
  b.addEventListener('click', () => {
    const q = b.dataset.quick;
    if (q === 'upload') $('#fmUploadFile').click();
    else if (q === 'update') showTab('updates');
    else if (q === 'theme') showTab('themes');
    else if (q === 'connect') showTab('device');
  })
);

// ---------------------------------------------------------------------------
// SD card
// ---------------------------------------------------------------------------

async function refreshState() {
  let s;
  try {
    s = await api('/api/state');
  } catch (e) {
    setPill('#sdPill', 'Server offline', 'bad');
    return;
  }

  if (s.sd && s.sd.mounted) {
    setPill('#sdPill', `SD: ${s.sd.label}`, 'good');
    if (s.sd.free == null || !s.sd.total) {
      $('#sdInfo').innerHTML = `<b>${esc(s.sd.path)}</b><br>free space not reported by this device`;
      $('#sdFreeBar').style.width = '0%';
    } else {
      $('#sdInfo').innerHTML = `<b>${esc(s.sd.path)}</b><br>${fmtSize(s.sd.free)} free of ${fmtSize(s.sd.total)}`;
      $('#sdFreeBar').style.width = pct(s.sd.total - s.sd.free, s.sd.total) + '%';
    }
    if (!$('#sdPath').value) $('#sdPath').value = s.sd.path;
  } else {
    setPill('#sdPill', 'SD: not found', 'bad');
    $('#sdInfo').textContent = 'No SD card detected. Choose it below.';
    $('#sdFreeBar').style.width = '0%';
  }

  // Mount candidates
  const sel = $('#sdDetect');
  const opts = (s.detect.candidates || []).map((c) => c.path);
  if (s.detect.path && !opts.includes(s.detect.path)) opts.unshift(s.detect.path);
  sel.innerHTML = opts.length
    ? opts.map((p) => `<option value="${esc(p)}">${esc(p)}</option>`).join('')
    : '<option value="">No mounts found</option>';
  if (s.detect.path) sel.value = s.detect.path;

  const cfg = s.config || {};
  if (cfg.sdPath) $('#sdPath').value = cfg.sdPath;
  if (cfg.espHost) $('#devHost').value = cfg.espHost;
  if (cfg.espPort) $('#devWifiPort').value = cfg.espPort;
  if (cfg.serialBaud) $('#devBaud').value = cfg.serialBaud;

  if (s.device && s.device.connected) {
    state.connected = true;
    setPill('#devPill', `Device: ${s.device.transport}`, 'good');
  } else {
    setPill('#devPill', 'Device: offline', '');
  }

  const sw = s.serial || {};
  if (!sw.available) {
    $('#serialWarn').textContent =
      'USB serial package not installed. Run `npm install` in companion/ to enable USB control.';
  } else {
    $('#serialWarn').textContent = '';
  }
}

async function loadSd(path) {
  state.sdPath = path || '';
  let data;
  try {
    data = await api(`/api/sd/list?path=${encodeURIComponent(state.sdPath)}`);
  } catch (e) {
    $('#fileRows').innerHTML = '';
    $('#fmEmpty').hidden = false;
    $('#fmEmpty').textContent = e.message;
    return;
  }
  renderCrumbs(data.path);
  renderFiles(data.entries);
  await refreshState();
}

function renderCrumbs(path) {
  const parts = String(path || '').split('/').filter(Boolean);
  const el = $('#crumbs');
  el.innerHTML = '';
  const root = document.createElement('button');
  root.textContent = 'SD';
  root.onclick = () => loadSd('');
  el.appendChild(root);
  let acc = '';
  for (const part of parts) {
    acc += (acc ? '/' : '') + part;
    const sep = document.createElement('span');
    sep.className = 'sep';
    sep.textContent = '/';
    el.appendChild(sep);
    const b = document.createElement('button');
    b.textContent = part;
    b.onclick = () => loadSd(acc);
    el.appendChild(b);
  }
}

function renderFiles(entries) {
  const body = $('#fileRows');
  body.innerHTML = '';
  $('#fmEmpty').hidden = entries.length > 0;
  $('#fmEmpty').textContent = 'This folder is empty.';

  for (const e of entries) {
    const tr = document.createElement('tr');

    const nameTd = document.createElement('td');
    const name = document.createElement('div');
    name.className = 'name ' + (e.dir ? 'dir' : 'file');
    name.innerHTML = `<span class="ico">${e.dir ? '▸' : icoFor(e.name)}</span><span>${esc(e.name)}</span>`;
    name.addEventListener('click', () => {
      if (e.dir) loadSd(joinPath(state.sdPath, e.name));
      else openFile(e.name);
    });
    nameTd.appendChild(name);

    const sizeTd = document.createElement('td');
    sizeTd.className = 'num';
    sizeTd.textContent = e.dir ? '—' : fmtSize(e.size);

    const timeTd = document.createElement('td');
    timeTd.className = 'hide-sm muted small';
    timeTd.textContent = fmtTime(e.mtime);

    const actTd = document.createElement('td');
    const actions = document.createElement('div');
    actions.className = 'actions';

    if (!e.dir) {
      actions.appendChild(iconBtn('↓', 'Download', () => downloadFile(e.name)));
      if (/\.thm$/i.test(e.name)) actions.appendChild(iconBtn('✦', 'Edit theme', () => openThemeFromSd(e.name)));
      if (/\.uf2$/i.test(e.name)) actions.appendChild(iconBtn('⇪', 'Inspect', () => inspectSdFile(e.name)));
    }
    actions.appendChild(iconBtn('✎', 'Rename', () => renameEntry(e.name)));
    actions.appendChild(iconBtn('🗑', 'Delete', () => deleteEntry(e), 'danger'));
    actTd.appendChild(actions);

    tr.append(nameTd, sizeTd, timeTd, actTd);
    body.appendChild(tr);
  }
}

function icoFor(name) {
  if (/\.uf2$/i.test(name)) return '⇪';
  if (/\.thm$/i.test(name)) return '✦';
  if (/\.(txt|md|cfg|log|ini)$/i.test(name)) return '▤';
  if (/\.(wav|mp3|mod)$/i.test(name)) return '♪';
  if (/\.(bmp|png|jpg|jpeg|gif)$/i.test(name)) return '▣';
  if (/\.(py|js|json|ino)$/i.test(name)) return '⌘';
  return '·';
}

function iconBtn(text, title, fn, cls = '') {
  const b = document.createElement('button');
  b.className = 'iconbtn ' + cls;
  b.textContent = text;
  b.title = title;
  b.addEventListener('click', (ev) => {
    ev.stopPropagation();
    fn();
  });
  return b;
}

function joinPath(a, b) {
  return [a, b].filter(Boolean).join('/');
}

function downloadFile(name) {
  const p = joinPath(state.sdPath, name);
  if (window.PicoPadNative) {
    window.PicoPadNative.saveSdFile(p, name)
      .then(() => toast(`Saved ${name}`))
      .catch((e) => toast(e.message, true));
    return;
  }
  window.location.href = `/api/sd/download?path=${encodeURIComponent(p)}`;
}

async function uploadFiles(files) {
  if (!files || !files.length) return;
  for (const f of files) {
    const target = joinPath(state.sdPath, f.name);
    try {
      await api(`/api/sd/upload?path=${encodeURIComponent(target)}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: f,
      });
      toast(`Uploaded ${f.name}`);
    } catch (e) {
      toast(`${f.name}: ${e.message}`, true);
    }
  }
  loadSd(state.sdPath);
}

async function renameEntry(name) {
  const next = prompt('Rename to', name);
  if (!next || next === name) return;
  try {
    await postJson('/api/sd/rename', { from: joinPath(state.sdPath, name), to: joinPath(state.sdPath, next) });
    toast('Renamed');
    loadSd(state.sdPath);
  } catch (e) {
    toast(e.message, true);
  }
}

async function deleteEntry(e) {
  const what = e.dir ? `folder "${e.name}" and everything inside` : `"${e.name}"`;
  if (!confirm(`Delete ${what}?`)) return;
  try {
    await postJson('/api/sd/delete', { path: joinPath(state.sdPath, e.name) });
    toast('Deleted');
    loadSd(state.sdPath);
  } catch (err) {
    toast(err.message, true);
  }
}

function openFile(name) {
  if (/\.thm$/i.test(name)) return openThemeFromSd(name);
  if (/\.uf2$/i.test(name)) return inspectSdFile(name);
  downloadFile(name);
}

async function inspectSdFile(name) {
  try {
    const res = await fetch(`/api/sd/download?path=${encodeURIComponent(joinPath(state.sdPath, name))}`);
    if (!res.ok) throw new Error('Cannot read file');
    const buf = await res.arrayBuffer();
    showTab('updates');
    await inspectBuffer(buf, name);
  } catch (e) {
    toast(e.message, true);
  }
}

$('#fmUp').addEventListener('click', () => {
  const parts = state.sdPath.split('/').filter(Boolean);
  parts.pop();
  loadSd(parts.join('/'));
});
$('#fmRefresh').addEventListener('click', () => loadSd(state.sdPath));
$('#sdRefresh').addEventListener('click', () => loadSd(state.sdPath));
$('#fmNewFolder').addEventListener('click', async () => {
  const name = prompt('Folder name');
  if (!name) return;
  try {
    await postJson('/api/sd/mkdir', { path: joinPath(state.sdPath, name) });
    toast('Folder created');
    loadSd(state.sdPath);
  } catch (e) {
    toast(e.message, true);
  }
});
$('#fmUpload').addEventListener('click', () => $('#fmUploadFile').click());
$('#fmUploadFile').addEventListener('change', (e) => {
  uploadFiles(e.target.files);
  e.target.value = '';
});

// Drag and drop
const drop = $('#sdDrop');
['dragenter', 'dragover'].forEach((ev) =>
  drop.addEventListener(ev, (e) => {
    e.preventDefault();
    drop.classList.add('over');
  })
);
['dragleave', 'drop'].forEach((ev) =>
  drop.addEventListener(ev, (e) => {
    e.preventDefault();
    drop.classList.remove('over');
  })
);
drop.addEventListener('drop', (e) => uploadFiles(e.dataTransfer.files));

// SD config
if (window.PicoPadNative) {
  document.body.classList.add('native-app');
  $('#sdPathRow').hidden = true;
  $('#sdPickRow').hidden = false;
  $('#sdBackup').textContent = 'Backup .zip';
  $('#sdRestoreFile').accept = '.zip,application/zip';
  $('#uf2Local').hidden = true;
  const label = window.PicoPadNative.sdLabel();
  $('#sdPickLabel').textContent = label || 'No folder chosen';
  $('#sdPick').addEventListener('click', async () => {
    try {
      const r = await window.PicoPadNative.chooseSdFolder();
      $('#sdPickLabel').textContent = r.label || 'Folder chosen';
      toast('SD folder selected');
      loadSd('');
    } catch (e) {
      toast(e.message, true);
    }
  });
}

$('#sdUse').addEventListener('click', async () => {
  const p = $('#sdDetect').value;
  if (!p) return;
  await api('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ sdPath: p }),
  });
  $('#sdPath').value = p;
  toast('SD card selected');
  loadSd('');
});
$('#sdSetPath').addEventListener('click', async () => {
  const p = $('#sdPath').value.trim();
  await api('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ sdPath: p }),
  });
  toast('SD path saved');
  loadSd('');
});

// Backup / restore
$('#sdBackup').addEventListener('click', () => {
  if (window.PicoPadNative) {
    if (!confirm('Save a .zip backup of the whole SD card?')) return;
    window.PicoPadNative.backupSd()
      .then((r) => toast(`Backup saved${r && r.name ? `: ${r.name}` : ''}`))
      .catch((e) => toast(e.message, true));
    return;
  }
  if (!confirm('Download a .tar.gz backup of the whole SD card?')) return;
  window.location.href = '/api/sd/backup';
});
$('#sdPrepare').addEventListener('click', async () => {
  if (!confirm('Delete EVERYTHING on the SD card and create the PicoDeck folders?')) return;
  const word = prompt('Type PREPARE to confirm');
  if (word !== 'PREPARE') return toast('Cancelled');
  try {
    const r = await api('/api/sd/prepare', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ confirm: 'PREPARE' }),
    });
    toast(`Prepared: ${r.folders.join(', ')}`);
    loadSd('');
  } catch (e) {
    toast(e.message, true);
  }
});

$('#sdRestore').addEventListener('click', () => $('#sdRestoreFile').click());
$('#sdRestoreFile').addEventListener('change', async (e) => {
  const f = e.target.files[0];
  e.target.value = '';
  if (!f) return;
  if (!confirm(`Restore ${f.name} into the SD card root? Files with the same names are overwritten.`)) return;
  try {
    await api('/api/sd/restore', {
      method: 'POST',
      headers: { 'Content-Type': 'application/gzip' },
      body: f,
    });
    toast('Restore finished');
    loadSd(state.sdPath);
  } catch (err) {
    toast(err.message, true);
  }
});

// ---------------------------------------------------------------------------
// Theme studio
// ---------------------------------------------------------------------------

async function loadThemes() {
  let res;
  try {
    res = await api(`/api/themes?source=${state.source}`);
  } catch (e) {
    toast(e.message, true);
    return;
  }
  state.themes = res.themes;
  state.activeTheme = res.active;
  const list = $('#thList');
  list.innerHTML = '';
  if (!res.themes.length) {
    list.innerHTML = `<p class="empty">No .thm files ${state.source === 'sd' ? 'on the card' : 'in themes/'}.</p>`;
  }
  for (const t of res.themes) {
    const item = document.createElement('div');
    item.className = 'theme-item' + (t.name === state.activeTheme ? ' active' : '');
    item.innerHTML = `
      <div>
        <div class="name">${esc(t.name)}</div>
        <div class="meta">${fmtSize(t.size)} · ${fmtTime(t.mtime)}</div>
      </div>
      ${t.name === state.activeTheme ? '<span class="badge">active</span>' : '<span class="badge off">—</span>'}
    `;
    item.addEventListener('click', () => openTheme(t.name));
    list.appendChild(item);
  }

  // Load a default preview if nothing is open yet.
  if (!state.theme) {
    try {
      const parsed = await postJson('/api/themes/parse', { text: '' });
      state.theme = parsed.theme;
      state.preview.setTheme(parsed.theme);
    } catch {
      /* ignore */
    }
  }
}

function openThemeFromSd(name) {
  state.source = 'sd';
  $$('.segbtn[data-source]').forEach((x) => x.classList.toggle('active', x.dataset.source === 'sd'));
  showTab('themes');
  openTheme(name);
}

async function openTheme(name) {
  try {
    const file = await api(`/api/themes/file?source=${state.source}&name=${encodeURIComponent(name)}`);
    state.theme = { name, source: state.source, text: file.text, mtime: file.mtime };
    state.themeDirty = false;
    $('#thTitle').textContent = name;
    $('#thText').value = file.text;
    $('#thStatus').textContent = `${fmtSize(file.size)} · saved ${fmtTime(file.mtime)}`;
    updatePreview();
    $('#thText').focus();
  } catch (e) {
    toast(e.message, true);
  }
}

let previewTimer = null;
function updatePreview() {
  clearTimeout(previewTimer);
  previewTimer = setTimeout(async () => {
    const text = $('#thText').value;
    try {
      const parsed = await postJson('/api/themes/parse', { text });
      state.preview.setTheme(parsed.theme);
      const w = $('#previewWarn');
      if (parsed.theme.warnings.length) {
        w.textContent = parsed.theme.warnings[0];
        w.className = 'warn small';
      } else {
        w.textContent = '';
        w.className = 'small';
      }
    } catch (e) {
      $('#previewWarn').textContent = e.message;
    }
  }, 160);
}

$('#thText').addEventListener('input', () => {
  state.themeDirty = true;
  updatePreview();
});

$$('.screens .segbtn').forEach((b) =>
  b.addEventListener('click', () => {
    $$('.screens .segbtn').forEach((x) => x.classList.toggle('active', x === b));
    state.screen = b.dataset.screen;
    if (state.preview) state.preview.setScreen(state.screen);
  })
);

$$('.segbtn[data-source]').forEach((b) =>
  b.addEventListener('click', () => {
    $$('.segbtn[data-source]').forEach((x) => x.classList.toggle('active', x === b));
    state.source = b.dataset.source;
    loadThemes();
  })
);

$('#thSave').addEventListener('click', async () => {
  if (!state.theme) return toast('Open a theme first', true);
  try {
    const saved = await putJson(
      `/api/themes/file?source=${state.theme.source}&name=${encodeURIComponent(state.theme.name)}`,
      { text: $('#thText').value }
    );
    state.themeDirty = false;
    $('#thStatus').textContent = `${fmtSize(saved.size)} · saved ${fmtTime(saved.mtime)}`;
    toast('Theme saved');
    if (state.source === state.theme.source) loadThemes();
  } catch (e) {
    toast(e.message, true);
  }
});

$('#thApply').addEventListener('click', async () => {
  if (!state.theme) return toast('Open a theme first', true);
  if (state.theme.source !== 'sd') return toast('Examples are read-only; save it to the SD card first', true);
  try {
    await postJson('/api/themes/active', { name: state.theme.name });
    toast('Theme activated');
    loadThemes();
  } catch (e) {
    toast(e.message, true);
  }
});

// Stream the editor contents to Companion Link over USB or WiFi.
$('#thSendDevice').addEventListener('click', async () => {
  if (!state.theme) return toast('Open a theme first', true);
  if (!state.connected) return toast('Connect to the device first', true);
  if (!/\.thm$/i.test(state.theme.name)) return toast('Theme name must end in .thm', true);
  const text = $('#thText').value;
  if (!confirm(`Send ${state.theme.name} to the device? It overwrites the same file on the card.`)) return;
  const send = (line) => postJson('/api/device/send', { line, target: 'pico' });
  try {
    await send(`THEME PUT ${state.theme.name}`);
    for (const line of text.split(/\r?\n/)) {
      await send(line);
      await new Promise((r) => setTimeout(r, 12));
    }
    await send('.');
    toast('Theme sent to the device');
  } catch (e) {
    toast(e.message, true);
  }
});

$('#thRevert').addEventListener('click', () => {
  if (state.theme) openTheme(state.theme.name);
});

$('#thNew').addEventListener('click', async () => {
  let name = $('#thNewName').value.trim();
  if (!name) name = prompt('New theme file name', 'mytheme.thm') || '';
  if (!name) return;
  if (!/\.(thm|ini)$/i.test(name)) name += '.thm';
  const template = [
    `# ${name}`,
    'set accent=FFD23F',
    'set okc=00FF00',
    'set dim=808080',
    '',
    'bg=000000',
    'duck_color=FFFF00',
    'menu_bg=000000',
    'menu_title=$accent',
    'menu_text=FFFFFF',
    'menu_sel=$accent',
    'menu_hl=282800',
    'menu_dim=$dim',
    'ok=$okc',
    'warn=FFFF00',
    'err=FF0000',
    'home_title1=PicoDeck',
    'home_title2=Launcher',
    '',
    'boot=text,-1,40,PICODECK,$accent,2,1',
    'boot=text,-1,64,LAUNCHER,$accent,2,1',
    'boot=bar,40,150,240,12,$okc',
    'boot=pct,-1,172,FFFFFF',
    'boot=spinner,24,152,$dim',
    '',
    'saver=text,-1,14,PicoDeck,$dim',
    'saver=duck,0,54',
    'saver=status,40,130',
    'saver=time,-1,182,FFFFFF,1,1',
    'saver=weather,20,198,C0C0C0',
    'saver=text,-1,224,Press any button,$dim',
    '',
  ].join('\n');
  try {
    state.source = 'sd';
    $$('.segbtn[data-source]').forEach((x) => x.classList.toggle('active', x.dataset.source === 'sd'));
    await putJson(`/api/themes/file?source=sd&name=${encodeURIComponent(name)}`, { text: template });
    $('#thNewName').value = '';
    await loadThemes();
    openTheme(name);
  } catch (e) {
    toast(e.message, true);
  }
});

$$('[data-insert]').forEach((b) =>
  b.addEventListener('click', () => {
    const kind = b.dataset.insert;
    const prefix = state.screen === 'home' ? 'boot' : state.screen;
    const snippets = {
      text: `${prefix}=text,-1,100,Hello,FFD23F,1,1`,
      marquee: `${prefix}=marquee,20,100,Scrolling text,FFD23F,150,280`,
      bar: `${prefix}=bar,40,150,240,12,00FF00`,
      rect: `${prefix}=rect,10,10,300,220,112233`,
      frame: `${prefix}=frame,10,10,300,220,FFD23F,2`,
      circle: `${prefix}=circle,160,120,60,FFD23F`,
      duck: `${prefix}=duck,0,54`,
      time: `${prefix}=time,-1,182,FFFFFF,1,1`,
    };
    const ta = $('#thText');
    const snippet = snippets[kind];
    const pos = ta.selectionStart;
    ta.value = ta.value.slice(0, pos) + (pos && ta.value[pos - 1] !== '\n' ? '\n' : '') + snippet + '\n' + ta.value.slice(pos);
    ta.selectionStart = ta.selectionEnd = pos + snippet.length + 2;
    ta.focus();
    updatePreview();
    state.themeDirty = true;
  })
);

// ---------------------------------------------------------------------------
// Updates
// ---------------------------------------------------------------------------

async function inspectBuffer(buf, name) {
  state.uf2Buffer = buf;
  state.uf2Report = null;
  $('#uf2Report').className = 'muted';
  $('#uf2Report').textContent = `Checking ${name || 'file'}…`;
  try {
    const res = await api('/api/uf2/inspect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: buf,
    });
    state.uf2Report = res.report;
    state.uf2Name = name || 'UF2 image';
    renderReport();
  } catch (e) {
    $('#uf2Report').textContent = e.message;
    $('#uf2Report').className = 'warn';
  }
}

function renderReport() {
  const r = state.uf2Report;
  if (!r) return;
  const el = $('#uf2Report');
  const ok = r.status === 'OK';
  const rows = [
    ['File', `${state.uf2Name || 'UF2'} · ${fmtSize(r.fileSize)}`],
    ['Version', r.version ? `v${r.version}` : 'not found'],
    ['Family', r.familyName],
    ['Blocks', `${r.blocks} / ${r.declared} declared`],
    ['Payload', `${r.payloadSize} bytes per block`],
    ['Range', `${hex(r.start)} – ${hex(r.end)}`],
    ['Layout', !r.linear ? 'reordered / overlapping' : r.gaps ? `linear, ${r.gaps} gap${r.gaps === 1 ? '' : 's'}` : 'linear'],
  ];
  el.className = '';
  el.innerHTML = `
    <div class="bigstatus ${ok ? 'ok' : 'check'}">${ok ? '✓ VALID' : '⚠ CHECK'}</div>
    <div class="report">
      ${rows.map(([k, v]) => `<div class="rrow"><span>${esc(k)}</span><b>${esc(v)}</b></div>`).join('')}
    </div>
    ${r.problems.length ? `<ul class="problems">${r.problems.map((p) => `<li>${esc(p)}</li>`).join('')}</ul>` : ''}
  `;
  $('#uf2Stage').disabled = !ok;
  $('#uf2StageNote').textContent = ok
    ? 'Staging writes update.uf2 to the SD root and keeps a .bak of the old one.'
    : 'Fix the file before staging.';
}

function hex(v) {
  return '0x' + (v >>> 0).toString(16).toUpperCase().padStart(8, '0');
}

async function pickUf2File(file) {
  if (!file) return;
  if (file.size > 8 * 1024 * 1024) return toast('That file is too big for a UF2', true);
  const buf = await file.arrayBuffer();
  await inspectBuffer(buf, file.name);
}

$('#uf2Pick').addEventListener('click', () => $('#uf2File').click());
$('#uf2File').addEventListener('change', async (e) => {
  await pickUf2File(e.target.files[0]);
  e.target.value = '';
});

const uf2Drop = $('#uf2Drop');
['dragenter', 'dragover'].forEach((ev) =>
  uf2Drop.addEventListener(ev, (e) => {
    e.preventDefault();
    uf2Drop.classList.add('over');
  })
);
['dragleave', 'drop'].forEach((ev) =>
  uf2Drop.addEventListener(ev, (e) => {
    e.preventDefault();
    uf2Drop.classList.remove('over');
  })
);
uf2Drop.addEventListener('drop', (e) => pickUf2File(e.dataTransfer.files[0]));

$('#uf2Stage').addEventListener('click', async () => {
  if (!state.uf2Buffer) return;
  if (!confirm('Stage this image as update.uf2 on the SD card root?')) return;
  try {
    const res = await api('/api/uf2/stage', {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: state.uf2Buffer,
    });
    $('#uf2StageNote').textContent = `Staged ${fmtSize(res.staged.size)} as update.uf2${res.staged.backup ? ' (old file saved as update.uf2.bak)' : ''}.`;
    toast('Update staged on the SD card');
  } catch (e) {
    toast(e.message, true);
  }
});

$('#uf2Local').addEventListener('click', async () => {
  try {
    const res = await fetch('/api/uf2/local');
    const info = await res.json();
    if (!info.local) return toast('No build artifact found in launcher/', true);
    const file = await fetch('/api/uf2/local/file');
    if (!file.ok) throw new Error('Cannot read build artifact');
    await inspectBuffer(await file.arrayBuffer(), info.local.name);
  } catch (e) {
    toast(e.message, true);
  }
});

$('#uf2SdUpdate').addEventListener('click', () => {
  inspectSdFile('update.uf2');
});

async function refreshLocalArtifact() {
  try {
    const res = await api('/api/uf2/local');
    if (res.local) {
      $('#uf2StageNote').textContent = `Latest build: ${res.local.name} · ${fmtSize(res.local.size)} · ${fmtTime(res.local.mtime)}`;
    }
  } catch {
    /* ignore */
  }
}

$('[data-quick="update"]')?.addEventListener('click', refreshLocalArtifact);

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

function setTransport(t) {
  state.transport = t;
  $$('.segbtn[data-transport]').forEach((b) => b.classList.toggle('active', b.dataset.transport === t));
  $('#usbFields').hidden = t !== 'usb';
  $('#wifiFields').hidden = t !== 'wifi';
}

$$('.segbtn[data-transport]').forEach((b) =>
  b.addEventListener('click', () => setTransport(b.dataset.transport))
);

async function loadPorts() {
  try {
    const res = await api('/api/device/ports');
    const sel = $('#devPort');
    sel.innerHTML = '';
    if (!res.ports.length) {
      sel.innerHTML = '<option value="">No serial ports found</option>';
    }
    for (const p of res.ports) {
      const label = `${p.path}${p.manufacturer ? ' · ' + p.manufacturer : ''}`;
      sel.insertAdjacentHTML('beforeend', `<option value="${esc(p.path)}">${esc(label)}</option>`);
    }
    if (!res.serialAvailable) {
      $('#serialWarn').textContent =
        'USB serial package not installed. Run `npm install` in companion/ to enable USB control.';
    }
  } catch (e) {
    toast(e.message, true);
  }
}

$('#devPorts').addEventListener('click', loadPorts);

$('#devConnect').addEventListener('click', async () => {
  const transport = state.transport;
  const body =
    transport === 'usb'
      ? { transport, path: $('#devPort').value, baud: Number($('#devBaud').value) || 115200 }
      : { transport, host: $('#devHost').value.trim(), port: Number($('#devWifiPort').value) || 2323 };
  if (transport === 'usb' && !body.path) return toast('Pick a serial port first', true);
  if (transport === 'wifi' && !body.host) return toast('Enter the ESP32 host or IP', true);
  try {
    const st = await api('/api/device/connect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    onDeviceState(st);
    toast(transport === 'usb' ? 'Connected over USB' : 'Connected over WiFi');
    // Persist the last used target.
    await api('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(
        transport === 'usb'
          ? { serialPort: body.path, serialBaud: body.baud }
          : { espHost: body.host, espPort: body.port }
      ),
    });
    setTimeout(() => {
      sendLine('PING');
      sendLine('STATUS');
    }, 300);
  } catch (e) {
    toast(e.message, true);
  }
});

$('#devDisconnect').addEventListener('click', async () => {
  try {
    await postJson('/api/device/disconnect', {});
    onDeviceState({ connected: false });
    toast('Disconnected');
  } catch (e) {
    toast(e.message, true);
  }
});

async function sendLine(line, target) {
  try {
    await postJson('/api/device/send', { line, target: target || $('#devTarget').value });
  } catch (e) {
    toast(e.message, true);
  }
}

$$('[data-cmd]').forEach((b) =>
  b.addEventListener('click', () => {
    const cmd = b.dataset.cmd;
    if (cmd === 'REBOOT' || cmd === 'BOOTSEL') {
      if (!confirm(`${cmd === 'REBOOT' ? 'Reboot' : 'Reboot into the bootloader'}?`)) return;
    }
    sendLine(cmd);
  })
);

$$('[data-send]').forEach((b) => b.addEventListener('click', () => sendLine(b.dataset.send)));

$('#devForm').addEventListener('submit', (e) => {
  e.preventDefault();
  const v = $('#devInput').value.trim();
  if (!v) return;
  sendLine(v);
  $('#devInput').value = '';
});

$('#devClear').addEventListener('click', () => {
  $('#devLog').innerHTML = '';
});

$('#devApps').addEventListener('click', () => {
  state.apps = [];
  state.collecting = 'apps';
  sendLine('APPS');
});

$('#devThemes').addEventListener('click', () => {
  state.deviceThemes = [];
  state.collecting = 'themes';
  sendLine('THEMES');
});

function onDeviceState(st) {
  state.connected = !!st.connected;
  setPill('#devPill', state.connected ? `Device: ${st.transport}` : 'Device: offline', state.connected ? 'good' : '');
  $('#stConn').textContent = state.connected ? st.transport : 'offline';
  $('#stConn').className = state.connected ? 'good' : 'bad';
  logLine(state.connected ? `# connected (${st.transport})` : '# disconnected', 'sys');

  // The first SSE state carries the server's recent history, which is handy
  // after a page reload. Later state events repeat it, so only use it once.
  if (!state.logInitialized) {
    state.logInitialized = true;
    for (const line of st.history || []) {
      logLine(line);
      parseDeviceLine(line);
    }
  }
}

function logLine(line, cls) {
  const el = $('#devLog');
  const div = document.createElement('div');
  if (!cls) {
    if (line.startsWith('> ')) cls = 'sent';
    else if (/^(OK|PONG|READY|DONE)\b/.test(line)) cls = 'ok';
    else if (/^(ERR|FAIL|ERROR)\b/.test(line)) cls = 'err';
    else if (line.startsWith('#')) cls = 'sys';
    else cls = 'data';
  }
  div.className = 'l-' + cls;
  div.textContent = line;
  el.appendChild(div);
  while (el.children.length > 600) el.firstChild.remove();
  el.scrollTop = el.scrollHeight;
}

function parseDeviceLine(line) {
  if (!line) return;

  if (line.startsWith('STATUS ')) {
    const p = line.split(' ');
    // STATUS <ver> <mv> <pct> <sd> <usb> <esp> <theme...>
    const ver = p[1] || '—';
    const mv = p[2] || '';
    const pctv = p[3] || '';
    const sd = p[4] || '0';
    const usb = p[5] || '0';
    const esp = p[6] || '0';
    const theme = p.slice(7).join(' ') || '(default)';
    $('#stVer').textContent = ver;
    $('#stBat').textContent = mv ? `${pctv}% · ${mv} mV` : '—';
    $('#stBat').className = Number(pctv) > 20 ? 'good' : 'bad';
    $('#stSd').textContent = truthy(sd) ? 'present' : 'missing';
    $('#stSd').className = truthy(sd) ? 'good' : 'bad';
    $('#stUsb').textContent = truthy(usb) ? 'connected' : '—';
    $('#stUsb').className = truthy(usb) ? 'good' : '';
    $('#stEsp').textContent = truthy(esp) ? 'linked' : '—';
    $('#stEsp').className = truthy(esp) ? 'good' : '';
    $('#stTheme').textContent = theme;
    return;
  }

  if (line.startsWith('INFO ')) {
    const p = line.split(' ');
    $('#stVer').textContent = p[1] || '—';
    return;
  }

  if (line.startsWith('APPS ')) {
    state.apps = [];
    state.collecting = 'apps';
    return;
  }
  if (line.startsWith('APP ')) {
    const p = line.split(' ');
    const id = Number(p[1]);
    const label = p.slice(2).join(' ');
    if (Number.isFinite(id)) state.apps.push({ id, label });
    return;
  }

  if (line.startsWith('THEMES ')) {
    state.deviceThemes = [];
    state.collecting = 'themes';
    return;
  }
  if (line.startsWith('THEME ')) {
    state.deviceThemes.push(line.slice(6).trim());
    return;
  }

  if (line === 'END') {
    if (state.collecting === 'apps') renderApps();
    if (state.collecting === 'themes') renderDeviceThemes();
    state.collecting = null;
  }
}

function truthy(v) {
  return v === '1' || /^(yes|on|true|ok|present|connected)$/i.test(v);
}

function renderApps() {
  const el = $('#appList');
  el.innerHTML = '';
  if (!state.apps.length) {
    el.innerHTML = '<p class="muted">No apps reported. Make sure Companion Link is open.</p>';
    return;
  }
  for (const a of state.apps) {
    const b = document.createElement('button');
    b.className = 'appitem';
    b.innerHTML = `<span>${esc(a.label)}</span><small>${a.id}</small>`;
    b.addEventListener('click', () => {
      if (!confirm(`Launch "${a.label}"?`)) return;
      sendLine(`LAUNCH ${a.id}`);
    });
    el.appendChild(b);
  }
}

function renderDeviceThemes() {
  const el = $('#devThemeList');
  el.innerHTML = '';
  if (!state.deviceThemes.length) {
    el.innerHTML = '<p class="muted">No themes on the card.</p>';
    return;
  }
  for (const name of state.deviceThemes) {
    const b = document.createElement('button');
    b.className = 'appitem' + (name === $('#stTheme').textContent ? ' active' : '');
    b.innerHTML = `<span>${esc(name)}</span><small>apply</small>`;
    b.addEventListener('click', () => {
      sendLine(`THEME SET ${name}`);
      $('#stTheme').textContent = name;
    });
    el.appendChild(b);
  }
}

function connectEvents() {
  const es = new EventSource('/api/device/events');
  es.addEventListener('state', (ev) => {
    try {
      onDeviceState(JSON.parse(ev.data));
    } catch {
      /* ignore */
    }
  });
  es.addEventListener('line', (ev) => {
    let line;
    try {
      line = JSON.parse(ev.data).line;
    } catch {
      return;
    }
    logLine(line);
    parseDeviceLine(line);
  });
  es.onerror = () => {
    // EventSource reconnects on its own; just note it once in the log.
    setPill('#devPill', 'Device: link lost', 'warn');
  };
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

async function init() {
  state.preview = new window.ThemePreview($('#preview'));
  setTransport('usb');
  connectEvents();
  try {
    await refreshState();
  } catch {
    /* ignore */
  }
  await loadSd('');
  await loadPorts();
  await refreshLocalArtifact();
  setInterval(refreshState, 15000);
  if ('serviceWorker' in navigator && !window.PicoPadNative) {
    navigator.serviceWorker.register('sw.js').catch(() => {});
  }
}

init();
