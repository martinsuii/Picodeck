'use strict';

// Theme (.thm) parser — host-side mirror of launcher/src/theme.cpp.
// Produces a plain object with colours resolved to #RRGGBB for the preview.

const NAMED = {
  black: '000000', white: 'FFFFFF', gray: '808080', ltgray: 'C0C0C0',
  dkgray: '404040', red: 'FF0000', green: '00FF00', blue: '0000FF',
  yellow: 'FFFF00', cyan: '00FFFF', magenta: 'FF00FF', orange: 'FFA500',
  purple: '800080', pink: 'FFC0CB', brown: 'A52A2A',
};

const TEXT_LEN = 63;
const MAX_ELEMS = 24;

function defaultDuck() {
  return ['         __', '   ___( o)>', '   \\ <_. )', "    `---'"];
}

function defaults() {
  const t = {
    bg: '000000',
    duck_color: 'FFFF00',
    duck: defaultDuck(),

    home_title1: 'PicoDeck',
    home_title2: 'Launcher',
    menu_bg: '000000',
    menu_title: 'FFFF00',
    menu_text: 'FFFFFF',
    menu_sel: 'FFFF00',
    menu_hl: '282800',
    menu_dim: '808080',
    ok: '00FF00',
    warn: 'FFFF00',
    err: 'FF0000',
    accent: 'FFFF00',

    home_time: 1,
    home_time_color: 'C0C0C0',
    home_time_x: -1,
    home_time_y: -1,
    home_weather: 1,
    home_weather_color: 'C0C0C0',

    menu_title_scale: 2,
    menu_title_y: 16,
    menu_title2_y: 36,
    menu_sub_title_y: 30,
    menu_divider_y: 64,
    menu_start_y: 80,
    menu_line_h: 20,
    menu_footer_y: 210,
    menu_sel_style: 0,
    menu_show_footer: 1,
    menu_show_status: 1,
    menu_help: 'A: Open   X: Back',

    boot_ms: 2000,
    boot_show_version: 1,
    saver_timeout: 30,
    saver_speed: 4,
    saver_backlight: 255,

    vars: {},
    boot_append: false,
    saver_append: false,
    boot: [],
    saver: [],
    warnings: [],
  };

  t.boot = [
    { type: 'text', x: -1, y: 46, w: 0, h: 0, align: 1, text: 'PicoDeck', color: 'FFFF00', scale: 2 },
    { type: 'text', x: -1, y: 70, w: 0, h: 0, align: 1, text: 'Launcher', color: 'FFFF00', scale: 2 },
    { type: 'bar', x: 40, y: 150, w: 280, h: 12, text: '', color: '00FF00', scale: 1 },
    { type: 'pct', x: -1, y: 172, w: 0, h: 0, align: 1, text: '', color: 'FFFFFF', scale: 1 },
    { type: 'spinner', x: 24, y: 152, w: 0, h: 0, text: '', color: 'C0C0C0', scale: 1 },
  ];
  t.saver = [
    { type: 'text', x: -1, y: 14, w: 0, h: 0, align: 1, text: 'PicoDeck', color: '808080', scale: 1 },
    { type: 'duck', x: 0, y: 54, w: 0, h: 0, text: '', color: 'FFFF00', scale: 1 },
    { type: 'status', x: 40, y: 130, w: 0, h: 0, text: '', color: 'FFFFFF', scale: 1 },
    { type: 'time', x: -1, y: 182, w: 0, h: 0, text: '', color: 'FFFFFF', scale: 1 },
    { type: 'weather', x: 20, y: 198, w: 0, h: 0, text: '', color: 'C0C0C0', scale: 1 },
    { type: 'text', x: -1, y: 224, w: 0, h: 0, align: 1, text: 'Press any button', color: '808080', scale: 1 },
  ];
  return t;
}

function normHex(s) {
  return (s || '').replace(/^#/, '').toUpperCase();
}

function parseColor(v, vars, def) {
  if (v == null) return def;
  let s = String(v).trim();
  if (s.startsWith('#')) s = s.slice(1);
  if (s.startsWith('$')) {
    const key = s.slice(1).toLowerCase();
    return vars[key] || def;
  }
  const named = NAMED[s.toLowerCase()];
  if (named) return named;

  if (/^0x/i.test(s)) s = s.slice(2);
  if (!/^[0-9a-f]+$/i.test(s)) return def;
  if (s.length === 3) {
    return (s[0] + s[0] + s[1] + s[1] + s[2] + s[2]).toUpperCase();
  }
  if (s.length === 6) return s.toUpperCase();
  return def;
}

function parseInt10(v, def) {
  const n = parseInt(String(v).trim(), 10);
  return Number.isFinite(n) ? n : def;
}

function clamp(v, lo, hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

function splitFields(v, max) {
  return String(v).split(',').slice(0, max).map((s) => s.trim());
}

function parseAlign(v) {
  if (v == null) return 0;
  const s = String(v).trim().toLowerCase();
  if (s === 'center' || s === 'c' || s === 'middle' || s === 'm') return 1;
  if (s === 'right' || s === 'r') return 2;
  const n = parseInt10(s, 0);
  return n < 0 ? 0 : n > 2 ? 2 : n;
}

function parseElem(value, vars) {
  const f = splitFields(value, 8);
  if (!f.length) return null;
  const type = f[0].toLowerCase();
  const e = { type, x: 0, y: 0, w: 0, h: 0, text: '', color: 'FFFFFF', scale: 1 };
  const gi = (i, d) => parseInt10(f[i], d);
  const gs = (i, d) => (f[i] != null ? f[i] : d);

  switch (type) {
    case 'text':
      if (f.length < 5) return null;
      Object.assign(e, {
        x: gi(1), y: gi(2), text: gs(3, '').slice(0, TEXT_LEN),
        color: parseColor(f[4], vars, 'FFFFFF'),
        scale: gi(5, 1) >= 2 ? 2 : 1,
        align: parseAlign(f[6]),
      });
      return e;
    case 'blink':
      if (f.length < 6) return null;
      Object.assign(e, {
        x: gi(1), y: gi(2), text: gs(3, '').slice(0, TEXT_LEN),
        color: parseColor(f[4], vars, 'FFFFFF'),
        h: gi(5, 500), scale: gi(6, 1) >= 2 ? 2 : 1, align: parseAlign(f[7]),
      });
      return e;
    case 'marquee':
      if (f.length < 5) return null;
      Object.assign(e, {
        x: gi(1), y: gi(2), text: gs(3, '').slice(0, TEXT_LEN),
        color: parseColor(f[4], vars, 'FFFFFF'),
        h: gi(5, 150), w: gi(6, 0),
      });
      return e;
    case 'rect':
      if (f.length < 6) return null;
      Object.assign(e, { x: gi(1), y: gi(2), w: gi(3), h: gi(4), color: parseColor(f[5], vars, 'FFFFFF') });
      return e;
    case 'frame':
      if (f.length < 6) return null;
      Object.assign(e, {
        x: gi(1), y: gi(2), w: gi(3), h: gi(4),
        color: parseColor(f[5], vars, 'FFFFFF'),
        scale: clamp(gi(6, 1), 1, 8),
      });
      return e;
    case 'line':
      if (f.length < 6) return null;
      Object.assign(e, {
        x: gi(1), y: gi(2), w: gi(3), h: gi(4),
        color: parseColor(f[5], vars, 'FFFFFF'),
        scale: clamp(gi(6, 1), 1, 8),
      });
      return e;
    case 'circle':
    case 'disc':
      if (f.length < 5) return null;
      Object.assign(e, { x: gi(1), y: gi(2), w: clamp(gi(3, 0), 0, 240), color: parseColor(f[4], vars, 'FFFFFF') });
      return e;
    case 'duck':
      if (f.length < 3) return null;
      Object.assign(e, { x: gi(1), y: gi(2) });
      return e;
    case 'bar':
      if (f.length < 6) return null;
      Object.assign(e, {
        x: gi(1), y: gi(2), w: gi(3), h: gi(4),
        color: parseColor(f[5], vars, '00FF00'),
      });
      return e;
    case 'pct':
      if (f.length < 4) return null;
      Object.assign(e, { x: gi(1), y: gi(2), color: parseColor(f[3], vars, 'FFFFFF') });
      return e;
    case 'spinner':
      if (f.length < 4) return null;
      Object.assign(e, { x: gi(1), y: gi(2), color: parseColor(f[3], vars, 'C0C0C0') });
      return e;
    case 'status':
    case 'usb':
    case 'sd':
    case 'battery':
      if (f.length < 3) return null;
      Object.assign(e, { x: gi(1), y: gi(2) });
      return e;
    case 'weather':
      if (f.length < 3) return null;
      Object.assign(e, { x: gi(1), y: gi(2), color: parseColor(f[3], vars, 'C0C0C0') });
      return e;
    case 'time':
      if (f.length < 4) return null;
      Object.assign(e, {
        x: gi(1), y: gi(2), color: parseColor(f[3], vars, 'FFFFFF'),
        scale: gi(4, 1) >= 2 ? 2 : 1, align: parseAlign(f[5]),
      });
      return e;
    default:
      return null;
  }
}

function parse(text) {
  const t = defaults();
  t.boot = [];
  t.saver = [];

  // The append flags are global, even if they appear after the elements.
  const bootAppend = /^\s*boot_append\s*=\s*[1-9]/im.test(text);
  const saverAppend = /^\s*saver_append\s*=\s*[1-9]/im.test(text);
  t.boot_append = bootAppend;
  t.saver_append = saverAppend;

  let bootStarted = false;
  let saverStarted = false;

  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith('#') || line.startsWith(';')) continue;
    const eq = rawLine.indexOf('=');
    if (eq < 0) continue;

    const key = rawLine.slice(0, eq).trim();
    let val = rawLine.slice(eq + 1).trim();

    // Trailing " ; comment" is ignored except on mascot art lines.
    if (!key.toLowerCase().startsWith('duck')) {
      const m = val.match(/^(.*?)(?:\s;.*)$/);
      if (m) val = m[1].trim();
    }

    const lk = key.toLowerCase();

    if (lk === 'set' || lk.startsWith('set ')) {
      const name = key.slice(4).trim().toLowerCase();
      if (!name) continue;
      if (Object.keys(t.vars).length >= 16) {
        t.warnings.push('More than 16 colour variables; extra ones ignored.');
        continue;
      }
      t.vars[name] = parseColor(val, t.vars, '000000');
      continue;
    }

    if (lk === 'boot') {
      if (!bootStarted) {
        bootStarted = true;
        if (!bootAppend) t.boot = [];
      }
      const e = parseElem(val, t.vars);
      if (e && t.boot.length < MAX_ELEMS) t.boot.push(e);
      else if (e) t.warnings.push('boot has more than 24 elements; extra ones ignored.');
      continue;
    }
    if (lk === 'saver') {
      if (!saverStarted) {
        saverStarted = true;
        if (!saverAppend) t.saver = [];
      }
      const e = parseElem(val, t.vars);
      if (e && t.saver.length < MAX_ELEMS) t.saver.push(e);
      else if (e) t.warnings.push('saver has more than 24 elements; extra ones ignored.');
      continue;
    }

    const col = (def) => parseColor(val, t.vars, def);

    switch (lk) {
      case 'bg': t.bg = col(t.bg); break;
      case 'duck_color': t.duck_color = col(t.duck_color); break;
      case 'menu_bg': t.menu_bg = col(t.menu_bg); break;
      case 'menu_title': t.menu_title = col(t.menu_title); break;
      case 'menu_text': t.menu_text = col(t.menu_text); break;
      case 'menu_sel': t.menu_sel = col(t.menu_sel); break;
      case 'menu_hl': t.menu_hl = col(t.menu_hl); break;
      case 'menu_dim': t.menu_dim = col(t.menu_dim); break;
      case 'accent': t.accent = col(t.accent); break;
      case 'ok': t.ok = col(t.ok); break;
      case 'warn': t.warn = col(t.warn); break;
      case 'err': t.err = col(t.err); break;

      case 'home_title1': t.home_title1 = val.slice(0, 31); break;
      case 'home_title2': t.home_title2 = val.slice(0, 31); break;
      case 'home_time': t.home_time = parseInt10(val, 0) !== 0 ? 1 : 0; break;
      case 'home_time_color': t.home_time_color = col(t.home_time_color); break;
      case 'home_time_x': t.home_time_x = parseInt10(val, -1); break;
      case 'home_time_y': t.home_time_y = parseInt10(val, -1); break;
      case 'home_weather': t.home_weather = parseInt10(val, 0) !== 0 ? 1 : 0; break;
      case 'home_weather_color': t.home_weather_color = col(t.home_weather_color); break;

      case 'menu_title_scale': t.menu_title_scale = parseInt10(val, 2) >= 2 ? 2 : 1; break;
      case 'menu_title_y': t.menu_title_y = parseInt10(val, t.menu_title_y); break;
      case 'menu_title2_y': t.menu_title2_y = parseInt10(val, t.menu_title2_y); break;
      case 'menu_sub_title_y': t.menu_sub_title_y = parseInt10(val, t.menu_sub_title_y); break;
      case 'menu_divider_y': t.menu_divider_y = parseInt10(val, t.menu_divider_y); break;
      case 'menu_start_y': t.menu_start_y = parseInt10(val, t.menu_start_y); break;
      case 'menu_line_h': t.menu_line_h = clamp(parseInt10(val, 20), 8, 64); break;
      case 'menu_footer_y': t.menu_footer_y = clamp(parseInt10(val, 210), 0, 232); break;
      case 'menu_sel_style': t.menu_sel_style = clamp(parseInt10(val, 0), 0, 3); break;
      case 'menu_show_footer': t.menu_show_footer = parseInt10(val, 0) !== 0 ? 1 : 0; break;
      case 'menu_show_status': t.menu_show_status = parseInt10(val, 0) !== 0 ? 1 : 0; break;
      case 'menu_help': t.menu_help = val.slice(0, TEXT_LEN); break;

      case 'boot_ms': t.boot_ms = clamp(parseInt10(val, 2000), 100, 60000); break;
      case 'boot_show_version': t.boot_show_version = parseInt10(val, 0) !== 0 ? 1 : 0; break;
      case 'saver_timeout': t.saver_timeout = clamp(parseInt10(val, 30), 0, 3600); break;
      case 'saver_speed': t.saver_speed = clamp(parseInt10(val, 4), 1, 40); break;
      case 'saver_backlight': t.saver_backlight = clamp(parseInt10(val, 255), 0, 255); break;

      case 'duck1': t.duck[0] = val; break;
      case 'duck2': t.duck[1] = val; break;
      case 'duck3': t.duck[2] = val; break;
      case 'duck4': t.duck[3] = val; break;
      case 'boot_append':
      case 'saver_append':
        break;
      default:
        break;
    }
  }

  // Append mode: keep the built-in elements in front of the theme's list.
  if (bootAppend) t.boot = defaults().boot.concat(t.boot);
  if (saverAppend) t.saver = defaults().saver.concat(t.saver);

  for (const e of [...t.boot, ...t.saver]) {
    if (e.type === 'text' && e.text.length > TEXT_LEN) {
      t.warnings.push('An element text is longer than 63 characters.');
      break;
    }
  }
  return t;
}

module.exports = { parse, defaults, parseColor, NAMED, MAX_ELEMS, TEXT_LEN };
