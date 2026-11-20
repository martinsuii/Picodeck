'use strict';

// UF2 inspector — host-side mirror of launcher/src/uf2_inspect.cpp.
// Returns a structured report used by the Updates tab.

const MAGIC0 = 0x0a324655; // "UF2\n"
const MAGIC1 = 0x9e5d5157;
const MAGIC_END = 0x0ab16f30;
const FLAG_FAMILY_ID = 0x00002000;
const BLOCK_SIZE = 512;

const FAMILIES = {
  0xe48bff56: 'RP2040',
  0xe48bff59: 'RP2350 ARM-S',
  0xe48bff5a: 'RP2350 RISC-V',
};

function familyName(id) {
  if (!id) return 'none';
  return FAMILIES[id] || '0x' + id.toString(16).toUpperCase().padStart(8, '0');
}

function hex32(v) {
  return '0x' + (v >>> 0).toString(16).toUpperCase().padStart(8, '0');
}

// Works with both Node Buffers and plain Uint8Arrays (browser/Android).
function readU32(buf, p) {
  if (typeof buf.readUInt32LE === 'function') return buf.readUInt32LE(p);
  return (buf[p] | (buf[p + 1] << 8) | (buf[p + 2] << 16) | (buf[p + 3] << 24)) >>> 0;
}

/**
 * @param {Buffer} buf
 * @returns {object} report
 */
function inspect(buf) {
  const report = {
    valid: false,
    fileSize: buf.length,
    blocks: 0,
    declared: 0,
    badBlocks: 0,
    family: 0,
    familyName: 'none',
    payloadSize: 0,
    start: 0,
    end: 0,
    linear: true,
    gaps: 0,
    overlaps: 0,
    version: null,
    status: 'CHECK',
    problems: [],
  };

  let first = true;
  let prevEnd = 0;
  let verState = 0;
  let verCollect = false;
  let verLen = 0;
  const ver = [];

  const total = Math.floor(buf.length / BLOCK_SIZE);
  for (let b = 0; b < total; b++) {
    const p = b * BLOCK_SIZE;
    const okMagic =
      readU32(buf, p) === MAGIC0 &&
      readU32(buf, p + 4) === MAGIC1 &&
      readU32(buf, p + 508) === MAGIC_END;

    let flags = 0, addr = 0, ps = 0, num = 0, family = 0;
    let ok = okMagic;
    if (okMagic) {
      flags = readU32(buf, p + 8);
      addr = readU32(buf, p + 12);
      ps = readU32(buf, p + 16);
      num = readU32(buf, p + 24);
      family = flags & FLAG_FAMILY_ID ? readU32(buf, p + 28) : 0;
      ok = ps > 0 && ps <= 476;
    }

    if (!ok) {
      // A non-UF2 prelude before the first block is tolerated by the
      // firmware; anything after that is a corrupt block.
      if (first) break;
      report.badBlocks++;
      continue;
    }

    if (first) {
      first = false;
      report.declared = num;
      report.family = family;
      report.familyName = familyName(family);
      report.payloadSize = ps;
      report.start = addr;
      report.end = addr + ps;
      prevEnd = addr + ps;
    } else {
      if (ps !== report.payloadSize) report.badBlocks++;
      if (num !== report.declared) report.badBlocks++;
      if (family !== report.family) report.badBlocks++;
      // Address gaps are normal (boot2 sits below the launcher image).
      // Only descending or overlapping blocks are an actual layout problem.
      if (addr < prevEnd) {
        report.linear = false;
        report.overlaps++;
      } else if (addr > prevEnd) {
        report.gaps++;
      }
      prevEnd = addr + ps;
    }

    if (addr < report.start) report.start = addr;
    if (addr + ps > report.end) report.end = addr + ps;

    // Version tag "AMTV<digits/dots>", possibly split across blocks.
    for (let i = 0; i < ps && !report.version; i++) {
      const c = buf[p + 32 + i];
      if (!verCollect) {
        const tag = 'AMTV';
        if (c === tag.charCodeAt(verState)) {
          if (++verState === 4) verCollect = true;
        } else {
          verState = c === 0x41 ? 1 : 0; // 'A'
        }
      } else if ((c >= 0x30 && c <= 0x39) || c === 0x2e) {
        if (verLen < 15) ver[verLen++] = String.fromCharCode(c);
      } else {
        if (verLen > 0) report.version = ver.join('');
        verCollect = false;
        verState = 0;
        verLen = 0;
      }
    }

    report.blocks++;
  }

  if (!report.version && verLen > 0) report.version = ver.join('');

  report.valid = report.blocks > 0;
  if (!report.valid) {
    report.problems.push('No valid 512-byte UF2 block found.');
  } else {
    if (report.badBlocks) report.problems.push(`${report.badBlocks} corrupt or inconsistent block(s).`);
    if (report.declared !== report.blocks) {
      report.problems.push(`Header declares ${report.declared} blocks, file has ${report.blocks}.`);
    }
    if (!report.linear) {
      report.problems.push(`Blocks are not in ascending address order (${report.overlaps} overlapping or reordered).`);
    }
  }

  const ok =
    report.valid &&
    report.badBlocks === 0 &&
    report.declared === report.blocks;
  report.status = ok ? 'OK' : 'CHECK';
  return report;
}

module.exports = { inspect, familyName, hex32, BLOCK_SIZE, FAMILIES };
