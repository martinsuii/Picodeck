'use strict';

const { EventEmitter } = require('events');
const fs = require('fs');
const net = require('net');
const { spawn, spawnSync } = require('child_process');

let SerialPort = null;
let serialError = null;
try {
  // Optional: USB CDC control. `npm install` in companion/ enables it.
  ({ SerialPort } = require('serialport'));
} catch (e) {
  serialError = e.message;
}

// Linux fallback: no native module needed. stty puts the tty into raw mode and
// a net.Socket wraps the fd so reads integrate with libuv.
const TTY_FALLBACK = !SerialPort && process.platform === 'linux';
const ttyUsable = (() => {
  if (!TTY_FALLBACK) return false;
  const r = spawnSync('stty', ['--version'], { stdio: 'ignore' });
  return r.status === 0;
})();
const SERIAL_OK = !!SerialPort || ttyUsable;

class Device extends EventEmitter {
  constructor() {
    super();
    this.transport = null; // 'usb' | 'wifi'
    this.connected = false;
    this.info = null;
    this.sock = null;
    this.serial = null;
    this.buf = '';
    this.history = [];
    this.maxHistory = 400;
  }

  record(line) {
    this.history.push(line);
    if (this.history.length > this.maxHistory) this.history.shift();
    this.emit('line', line);
  }

  state() {
    return {
      connected: this.connected,
      transport: this.transport,
      info: this.info,
      serialAvailable: SERIAL_OK,
      serialError: SERIAL_OK ? null : serialError,
      history: this.history,
    };
  }

  async listPorts() {
    if (SerialPort) {
      try {
        const ports = await SerialPort.list();
        return ports.map((p) => ({
          path: p.path,
          manufacturer: p.manufacturer || '',
          serialNumber: p.serialNumber || '',
          vendorId: p.vendorId || '',
          productId: p.productId || '',
        }));
      } catch (e) {
        return [];
      }
    }
    // Fallback: just show likely device nodes.
    try {
      return fs
        .readdirSync('/dev')
        .filter((n) => /^tty(ACM|USB)\d+$/.test(n))
        .map((n) => ({ path: `/dev/${n}`, manufacturer: '', serialNumber: '', vendorId: '', productId: '' }));
    } catch {
      return [];
    }
  }

  async connect(opts) {
    this.close();
    this.history = [];
    if (opts.transport === 'wifi') {
      await this.connectWifi(opts.host, opts.port || 2323);
    } else {
      await this.connectUsb(opts.path, opts.baud || 115200);
    }
    return this.state();
  }

  connectWifi(host, port) {
    return new Promise((resolve, reject) => {
      if (!host) return reject(new Error('No host given'));
      const sock = net.createConnection({ host, port }, () => {
        this.transport = 'wifi';
        this.connected = true;
        this.info = { host, port };
        this.record(`# connected to ${host}:${port}`);
        resolve();
      });
      sock.setNoDelay(true);
      sock.setKeepAlive(true, 5000);
      sock.on('data', (chunk) => this.onData(chunk.toString('utf8')));
      sock.on('error', (err) => {
        this.record(`# error: ${err.message}`);
        if (!this.connected) reject(err);
        this.emit('error', err);
      });
      sock.on('close', () => {
        if (this.connected) this.record('# disconnected');
        this.connected = false;
        this.transport = null;
        this.sock = null;
        this.emit('close');
      });
      this.sock = sock;
    });
  }

  connectUsb(serialPath, baud) {
    if (SerialPort) return this.connectSerialPort(serialPath, baud);
    if (ttyUsable) return this.connectTty(serialPath, baud);
    return Promise.reject(
      new Error(
        'USB serial support is not installed. Run `npm install` in companion/ (needs the serialport package).'
      )
    );
  }

  connectSerialPort(serialPath, baud) {
    return new Promise((resolve, reject) => {
      const port = new SerialPort({ path: serialPath, baudRate: baud, autoOpen: false });
      port.open((err) => {
        if (err) return reject(err);
        this.transport = 'usb';
        this.connected = true;
        this.info = { path: serialPath, baud };
        this.record(`# connected to ${serialPath} at ${baud} baud`);
        port.on('data', (chunk) => this.onData(chunk.toString('utf8')));
        port.on('error', (e) => this.record(`# error: ${e.message}`));
        port.on('close', () => this.usbClosed());
        this.serial = port;
        resolve();
      });
    });
  }

  // Linux only: stty configures the tty; `cat` pumps reads into the event
  // loop and a separate fd is used for writes. (net.Socket around a tty fd
  // throws "Unsupported fd type: TTY".)
  connectTty(serialPath, baud) {
    return new Promise((resolve, reject) => {
      const stty = spawnSync('stty', ['-F', serialPath, 'raw', '-echo', String(baud)]);
      if (stty.status !== 0) {
        return reject(new Error(`stty failed on ${serialPath}`));
      }

      let wfd;
      try {
        wfd = fs.openSync(serialPath,
          fs.constants.O_WRONLY | fs.constants.O_NOCTTY);
      } catch (e) {
        return reject(new Error(`Cannot open ${serialPath}: ${e.message}`));
      }

      let closed = false;
      let cat;
      const finish = (why) => {
        if (closed) return;
        closed = true;
        try { fs.closeSync(wfd); } catch { /* ignore */ }
        try { cat.kill(); } catch { /* ignore */ }
        if (why) this.record(`# ${why}`);
        this.usbClosed();
      };

      try {
        cat = spawn('cat', [serialPath], { stdio: ['ignore', 'pipe', 'ignore'] });
      } catch (e) {
        try { fs.closeSync(wfd); } catch { /* ignore */ }
        return reject(new Error(`Cannot read ${serialPath}: ${e.message}`));
      }

      cat.stdout.on('data', (chunk) => this.onData(chunk.toString('utf8')));
      cat.on('error', (e) => {
        this.record(`# serial: ${e.message}`);
        finish('serial read failed');
      });
      cat.on('exit', () => finish('serial closed'));

      this.transport = 'usb';
      this.connected = true;
      this.info = { path: serialPath, baud };
      this.record(`# connected to ${serialPath} at ${baud} baud (stty/cat)`);
      this.serial = {
        write: (tx) => {
          try { fs.writeSync(wfd, tx); }
          catch (e) { this.record(`# serial write: ${e.message}`); }
        },
        close: () => finish('disconnected'),
      };
      resolve();
    });
  }

  usbClosed() {
    if (this.connected) this.record('# disconnected');
    this.connected = false;
    this.transport = null;
    this.serial = null;
    this.emit('close');
  }

  onData(text) {
    this.buf += text;
    for (;;) {
      const i = this.buf.indexOf('\n');
      if (i < 0) break;
      let line = this.buf.slice(0, i);
      this.buf = this.buf.slice(i + 1);
      if (line.endsWith('\r')) line = line.slice(0, -1);
      this.record(line);
    }
    // Avoid unbounded growth from a device that never sends a newline.
    if (this.buf.length > 4096) {
      const tail = this.buf.slice(-2048);
      this.buf = '';
      this.record(tail);
    }
  }

  /**
   * Send one protocol line.
   * target = 'pico' (default) or 'esp'.
   * WiFi sends plain lines to the PicoPad (the ESP32 adds the '@' marker) and
   * '!' prefixed lines to the ESP32 itself.
   */
  send(line, target = 'pico') {
    if (!this.connected) throw new Error('Not connected');
    let out = String(line).replace(/[\r\n]+/g, ' ').trim();
    if (!out) return;

    if (this.transport === 'wifi' && target === 'esp' && !out.startsWith('!')) {
      out = '!' + out;
    }

    this.record(`> ${out}`);
    if (this.transport === 'wifi') this.sock.write(out + '\n');
    else this.serial.write(out + '\n');
  }

  close() {
    if (this.serial) {
      try {
        this.serial.close();
      } catch {
        /* ignore */
      }
    }
    if (this.sock) {
      try {
        this.sock.destroy();
      } catch {
        /* ignore */
      }
    }
    this.serial = null;
    this.sock = null;
    if (this.connected) this.record('# disconnected');
    this.connected = false;
    this.transport = null;
  }
}

module.exports = { Device, serialAvailable: () => SERIAL_OK, serialError: () => serialError };
