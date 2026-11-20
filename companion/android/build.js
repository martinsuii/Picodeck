'use strict';

// ---------------------------------------------------------------------------
// Builds the standalone Android app without Gradle.
//
//   node android/build.js
//
// Needs an Android SDK with build-tools and platform 34. Set ANDROID_HOME (or
// ANDROID_SDK_ROOT) if it is not /home/martin/Android.
// ---------------------------------------------------------------------------

const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const ROOT = __dirname;
const COMPANION = path.resolve(ROOT, '..');
const THEMES = path.resolve(COMPANION, '..', 'themes');
const BUILD = path.join(ROOT, 'build');
const DIST = path.join(COMPANION, 'dist');
const VERSION = '1.0.0';

const SDK =
  process.env.ANDROID_HOME ||
  process.env.ANDROID_SDK_ROOT ||
  '/home/martin/Android';

function fail(msg) {
  console.error(`\n${msg}\n`);
  process.exit(1);
}

function sdkDir(kind, wanted) {
  const base = path.join(SDK, kind);
  if (!fs.existsSync(base)) fail(`Android SDK is missing ${base}`);
  if (wanted) {
    const exact = path.join(base, wanted);
    if (fs.existsSync(exact)) return exact;
  }
  // Pick the newest numeric entry.
  const entries = fs
    .readdirSync(base)
    .filter((n) => fs.statSync(path.join(base, n)).isDirectory())
    .sort((a, b) => {
      const na = parseFloat(a.replace(/[^0-9.]/g, '')) || 0;
      const nb = parseFloat(b.replace(/[^0-9.]/g, '')) || 0;
      return nb - na;
    });
  if (!entries.length) fail(`No ${kind} found under ${base}`);
  return path.join(base, entries[0]);
}

const BUILD_TOOLS = sdkDir('build-tools', '34.0.0');
const PLATFORM = sdkDir('platforms', 'android-34');
const ANDROID_JAR = path.join(PLATFORM, 'android.jar');
const AAPT2 = path.join(BUILD_TOOLS, 'aapt2');
const D8 = path.join(BUILD_TOOLS, 'd8');
const R8_JAR = path.join(SDK, 'cmdline-tools', 'latest', 'lib', 'r8.jar');
const ZIPALIGN = path.join(BUILD_TOOLS, 'zipalign');
const APKSIGNER = path.join(BUILD_TOOLS, 'apksigner');
const KEYSTORE = path.join(ROOT, 'debug.keystore');

for (const f of [AAPT2, ZIPALIGN, APKSIGNER, ANDROID_JAR]) {
  if (!fs.existsSync(f)) fail(`Missing build tool: ${f}`);
}
if (!fs.existsSync(D8) && !fs.existsSync(R8_JAR)) {
  fail('Missing d8 (build-tools) and r8.jar (cmdline-tools)');
}

function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { stdio: 'inherit', ...opts });
  if (r.error) fail(`${cmd}: ${r.error.message}`);
  if (r.status !== 0) fail(`${cmd} failed with exit code ${r.status}`);
}

function copy(from, to) {
  if (!fs.existsSync(from)) return;
  fs.mkdirSync(path.dirname(to), { recursive: true });
  fs.cpSync(from, to, { recursive: true });
}

function findFiles(dir, ext, out = []) {
  for (const name of fs.readdirSync(dir)) {
    const p = path.join(dir, name);
    const st = fs.statSync(p);
    if (st.isDirectory()) findFiles(p, ext, out);
    else if (p.endsWith(ext)) out.push(p);
  }
  return out;
}

// --- clean -----------------------------------------------------------------

fs.rmSync(BUILD, { recursive: true, force: true });
fs.mkdirSync(BUILD, { recursive: true });
fs.mkdirSync(DIST, { recursive: true });

// --- assets ----------------------------------------------------------------

const ASSETS = path.join(BUILD, 'assets');
copy(path.join(COMPANION, 'web'), path.join(ASSETS, 'web'));
fs.mkdirSync(path.join(ASSETS, 'lib'), { recursive: true });
for (const name of ['theme.js', 'uf2.js']) {
  fs.copyFileSync(path.join(COMPANION, 'lib', name), path.join(ASSETS, 'lib', name));
}
copy(path.join(COMPANION, 'assets'), path.join(ASSETS, 'assets'));
copy(THEMES, path.join(ASSETS, 'themes'));

// The app is a file:// page, so it cannot load /assets from the server.
// index.html points the preview font at ../assets/font_bold_8x8.bmp.
const font = path.join(ASSETS, 'assets', 'font_bold_8x8.bmp');
if (!fs.existsSync(font)) fail(`Missing preview font: ${font}`);

console.log('Building Android app…');
console.log(`  SDK:       ${SDK}`);
console.log(`  build-tools: ${BUILD_TOOLS}`);

// --- resources -------------------------------------------------------------

const resZip = path.join(BUILD, 'res.zip');
run(AAPT2, ['compile', '--dir', path.join(ROOT, 'res'), '-o', resZip]);

const gen = path.join(BUILD, 'gen');
fs.mkdirSync(gen, { recursive: true });
const unsigned = path.join(BUILD, 'unsigned.apk');
run(AAPT2, [
  'link',
  '-o', unsigned,
  '-I', ANDROID_JAR,
  '--manifest', path.join(ROOT, 'AndroidManifest.xml'),
  '-A', ASSETS,
  '--java', gen,
  '--min-sdk-version', '23',
  '--target-sdk-version', '34',
  '--version-code', '3',
  '--version-name', VERSION,
  resZip,
]);

// --- Java ------------------------------------------------------------------

const javaFiles = findFiles(path.join(ROOT, 'java'), '.java');
javaFiles.push(...findFiles(gen, '.java'));
if (!javaFiles.length) fail('No Java sources found');

const classes = path.join(BUILD, 'classes');
fs.mkdirSync(classes, { recursive: true });
const javac = spawnSync(
  'javac',
  [
    '-source', '11',
    '-target', '11',
    '-encoding', 'UTF-8',
    '-classpath', ANDROID_JAR,
    '-d', classes,
    ...javaFiles,
  ],
  { stdio: 'inherit' }
);
if (javac.error) fail(`javac: ${javac.error.message}`);
if (javac.status !== 0) fail('javac failed');

const classesJar = path.join(BUILD, 'classes.jar');
run('jar', ['--create', '--file', classesJar, '-C', classes, '.']);

const dex = path.join(BUILD, 'dex');
fs.mkdirSync(dex, { recursive: true });
// The d8 shipped in build-tools 34 crashes on inner classes with newer JDKs;
// the r8.jar from cmdline-tools is the same tool with the fix.
const d8Args = ['--lib', ANDROID_JAR, '--min-api', '23', '--output', dex, classesJar];
if (fs.existsSync(R8_JAR)) {
  run('java', ['-cp', R8_JAR, 'com.android.tools.r8.D8', ...d8Args]);
} else {
  run(D8, d8Args);
}

// aapt2 does not take a dex, so add it afterwards. zipalign runs after this.
const dexFile = path.join(dex, 'classes.dex');
if (!fs.existsSync(dexFile)) fail('d8 did not produce classes.dex');
run('jar', ['--update', '--file', unsigned, '-C', dex, 'classes.dex']);

// --- sign ------------------------------------------------------------------

if (!fs.existsSync(KEYSTORE)) {
  console.log('Creating debug keystore…');
  run('keytool', [
    '-genkeypair',
    '-keystore', KEYSTORE,
    '-storepass', 'android',
    '-keypass', 'android',
    '-alias', 'androiddebugkey',
    '-keyalg', 'RSA',
    '-keysize', '2048',
    '-validity', '10000',
    '-dname', 'CN=Android Debug,O=Android,C=US',
  ]);
}

const aligned = path.join(BUILD, 'aligned.apk');
run(ZIPALIGN, ['-p', '-f', '4', unsigned, aligned]);

const out = path.join(DIST, `PicoDeckCompanion-android-${VERSION}.apk`);
run(APKSIGNER, [
  'sign',
  '--ks', KEYSTORE,
  '--ks-key-alias', 'androiddebugkey',
  '--ks-pass', 'pass:android',
  '--key-pass', 'pass:android',
  '--out', out,
  aligned,
]);

run(APKSIGNER, ['verify', '--verbose', out]);

const size = fs.statSync(out).size;
console.log(`\nAPK: ${out} (${(size / 1024 / 1024).toFixed(1)} MB)`);
