// App shell: cache first. API reads: network first with a cached fallback, so a phone
// that loses signal in the workshop still shows the last data it had.
const VERSION = '196-1795160888856';
const FILES = ["./","assets/react-ppAWL3zK.js","assets/index-Dpd6leQH.js","assets/Tests-CfPQZiQt.js","assets/TestDetail-CUk4nfHv.js","assets/Events-Bsm32rIK.js","assets/reportPdf-xfih0En-.js","assets/style-DJH4CQdW.css","index.html"];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(VERSION).then((c) => c.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(caches.keys().then((ks) => Promise.all(ks.filter((k) => k !== VERSION).map((k) => caches.delete(k)))).then(() => self.clients.claim()));
});

self.addEventListener('fetch', (e) => {
  const r = e.request;
  if (r.method !== 'GET') return;
  const url = new URL(r.url);
  if (url.origin === location.origin) {
    e.respondWith(caches.match(r, { ignoreSearch: true }).then((hit) => hit || fetch(r)));
    return;
  }
  // Live polling must be real or fail; only history is worth keeping offline.
  if (!/\/tests|\/events/.test(url.pathname)) return;
  e.respondWith(fetch(r).then((res) => {
    if (res.ok) { const copy = res.clone(); caches.open(VERSION + '-api').then((c) => c.put(r, copy)); }
    return res;
  }).catch(() => caches.match(r)));
});
