const CACHE = 'clima-v1';
const PRECACHE = [
  './',
  './index.html',
  './manifest.webmanifest',
  './icons/icon-192.png',
  './icons/icon-512.png'
];

self.addEventListener('install', function (event) {
  event.waitUntil(
    caches.open(CACHE)
      .then(function (cache) { return cache.addAll(PRECACHE); })
      .then(function () { return self.skipWaiting(); })
  );
});

self.addEventListener('activate', function (event) {
  event.waitUntil(
    caches.keys()
      .then(function (keys) {
        return Promise.all(keys.filter(function (k) { return k !== CACHE; })
          .map(function (k) { return caches.delete(k); }));
      })
      .then(function () { return self.clients.claim(); })
  );
});

self.addEventListener('fetch', function (event) {
  const req = event.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);

  if (url.origin === location.origin) {
    event.respondWith(
      caches.match(req).then(function (hit) {
        return hit || fetch(req).then(function (res) {
          const copia = res.clone();
          caches.open(CACHE).then(function (c) { c.put(req, copia); });
          return res;
        });
      })
    );
    return;
  }

  if (url.hostname.indexOf('script.google.com') !== -1) {
    event.respondWith(
      fetch(req).then(function (res) {
        const copia = res.clone();
        caches.open(CACHE).then(function (c) { c.put(req, copia); });
        return res;
      }).catch(function () {
        return caches.match(req);
      })
    );
    return;
  }

  event.respondWith(
    caches.match(req).then(function (hit) {
      return hit || fetch(req).then(function (res) {
        const copia = res.clone();
        caches.open(CACHE).then(function (c) { c.put(req, copia); });
        return res;
      });
    })
  );
});
