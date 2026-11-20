'use strict';

// Offline shell cache. The API is always network-first so SD contents,
// themes and device state stay live.
const CACHE = 'multitool-companion-v1';
const SHELL = [
  './',
  'index.html',
  'style.css',
  'app.js',
  'theme-preview.js',
  'manifest.webmanifest',
  'icon-192.png',
  'icon-512.png',
  '/assets/font_bold_8x8.bmp',
];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys().then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (e) => {
  const url = new URL(e.request.url);
  if (e.request.method !== 'GET' || url.pathname.startsWith('/api/')) return;

  e.respondWith(
    caches.match(e.request).then((hit) => {
      if (hit) return hit;
      return fetch(e.request)
        .then((res) => {
          if (res.ok && res.type === 'basic') {
            const copy = res.clone();
            caches.open(CACHE).then((c) => c.put(e.request, copy));
          }
          return res;
        })
        .catch(() => caches.match('index.html'));
    })
  );
});
