#!/usr/bin/env node
'use strict';

// Assemble a self-contained Linux app: Electron runtime + companion server/UI.
// Usage: node desktop/build.js
//   ELECTRON_DIR=/usr/lib/electron   (default; needs a full Electron runtime)

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const ROOT = path.join(__dirname, '..');
const DIST = path.join(ROOT, 'dist');
const APP = path.join(DIST, 'linux', 'PicoDeckCompanion');
const RES = path.join(APP, 'resources');
const SRC = path.join(RES, 'app');
const THEMES = path.join(ROOT, '..', 'themes');

function findElectron() {
  const candidates = [
    process.env.ELECTRON_DIR,
    '/usr/lib/electron',
    path.join(ROOT, 'node_modules', 'electron', 'dist'),
  ].filter(Boolean);
  for (const dir of candidates) {
    try {
      const real = fs.realpathSync(dir);
      if (fs.existsSync(path.join(real, 'electron'))) return real;
    } catch {
      /* try next */
    }
  }
  throw new Error('Electron runtime not found. Set ELECTRON_DIR to a full Electron install.');
}

function copyDir(from, to) {
  fs.mkdirSync(to, { recursive: true });
  for (const ent of fs.readdirSync(from, { withFileTypes: true })) {
    const a = path.join(from, ent.name);
    const b = path.join(to, ent.name);
    if (ent.isDirectory()) copyDir(a, b);
    else if (ent.isSymbolicLink()) fs.symlinkSync(fs.readlinkSync(a), b);
    else fs.copyFileSync(a, b);
  }
}

function desktopEntry(exec, icon) {
  return `[Desktop Entry]
Type=Application
Name=PicoDeck Companion
Comment=Manage the PicoDeck SD card, themes and updates
Exec=${exec} --no-sandbox %U
Icon=${icon}
Terminal=false
Categories=Utility;Development;
StartupWMClass=PicoDeck Companion
`;
}

function main() {
  const electron = findElectron();

  fs.rmSync(path.join(DIST, 'linux'), { recursive: true, force: true });
  fs.mkdirSync(SRC, { recursive: true });

  // Runtime files (chrome_*.pak, libffmpeg, locales, ...).
  copyDir(electron, APP);
  fs.rmSync(path.join(RES, 'default_app.asar'), { force: true });

  // App payload.
  for (const name of ['server.js', 'lib', 'web', 'assets']) {
    const from = path.join(ROOT, name);
    const to = path.join(SRC, name);
    if (fs.statSync(from).isDirectory()) copyDir(from, to);
    else fs.copyFileSync(from, to);
  }
  copyDir(path.join(__dirname), path.join(SRC, 'desktop'));
  if (fs.existsSync(THEMES)) copyDir(THEMES, path.join(RES, 'themes'));

  fs.writeFileSync(
    path.join(SRC, 'package.json'),
    JSON.stringify(
      {
        name: 'arcade-multitool-companion',
        productName: 'PicoDeck Companion',
        version: '1.0.0',
        private: true,
        main: 'desktop/main.js',
      },
      null,
      2
    ) + '\n'
  );

  // Rename the Electron binary so the process/window name is ours.
  fs.renameSync(path.join(APP, 'electron'), path.join(APP, 'picodeck-companion'));
  fs.chmodSync(path.join(APP, 'picodeck-companion'), 0o755);

  // Portable launcher + desktop entry for this build directory.
  fs.writeFileSync(
    path.join(APP, 'run.sh'),
    '#!/bin/sh\ncd "$(dirname "$0")"\nexec ./picodeck-companion --no-sandbox "$@"\n'
  );
  fs.chmodSync(path.join(APP, 'run.sh'), 0o755);

  fs.writeFileSync(
    path.join(APP, 'picodeck-companion.desktop'),
    desktopEntry(path.join(APP, 'picodeck-companion'), path.join(SRC, 'web', 'icon-512.png'))
  );

  fs.writeFileSync(
    path.join(APP, 'install.sh'),
    `#!/bin/sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
dest="\${HOME}/.local/opt/picodeck-companion"
rm -rf "$dest"
mkdir -p "$dest"
cp -a "$here/." "$dest/"
apps="\${HOME}/.local/share/applications"
mkdir -p "$apps"
cat > "$apps/picodeck-companion.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=PicoDeck Companion
Comment=Manage the PicoDeck SD card, themes and updates
Exec=$dest/picodeck-companion --no-sandbox %U
Icon=$dest/resources/app/web/icon-512.png
Terminal=false
Categories=Utility;Development;
StartupWMClass=PicoDeck Companion
EOF
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$apps" || true
echo "Installed to $dest"
`
  );
  fs.chmodSync(path.join(APP, 'install.sh'), 0o755);

  // Tarball for copying to another machine.
  fs.mkdirSync(DIST, { recursive: true });
  const tar = path.join(DIST, 'picodeck-companion-linux-x64.tar.gz');
  execFileSync('tar', ['-czf', tar, '-C', path.join(DIST, 'linux'), 'PicoDeckCompanion']);

  const size = execFileSync('du', ['-sh', APP], { encoding: 'utf8' }).trim();
  console.log(`Built ${APP} (${size})`);
  console.log(`Tarball: ${tar}`);
}

main();
