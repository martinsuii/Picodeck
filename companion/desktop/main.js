'use strict';

// Electron shell for the standalone Linux (and Windows/macOS) companion.
// The Node server from ../server.js provides the API and serves the same web
// UI a phone browser can open over the LAN.

const path = require('path');
const { app, BrowserWindow, shell } = require('electron');
const { start } = require('../server');

let win = null;
let backend = null;

function createWindow() {
  win = new BrowserWindow({
    width: 1280,
    height: 900,
    minWidth: 900,
    minHeight: 600,
    backgroundColor: '#101014',
    title: 'PicoDeck Companion',
    icon: path.join(__dirname, '..', 'web', 'icon-512.png'),
    autoHideMenuBar: true,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  win.loadURL(backend.url);

  // External links open in the normal browser, not inside the app.
  win.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url);
    return { action: 'deny' };
  });
  win.webContents.on('will-navigate', (event, url) => {
    if (!url.startsWith(backend.url.slice(0, backend.url.indexOf('/', 8)))) {
      event.preventDefault();
      shell.openExternal(url);
    }
  });

  win.on('closed', () => {
    win = null;
  });
}

app.setName('PicoDeck Companion');

app.whenReady().then(async () => {
  try {
    // Port 0 = let the OS pick; LAN access lets a phone use the same UI.
    backend = await start({ port: 0, host: '0.0.0.0' });
  } catch (e) {
    console.error(`Cannot start companion server: ${e.message}`);
    app.quit();
    return;
  }
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});
