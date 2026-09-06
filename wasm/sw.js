/*
 * Keeps the page and its modules so a later visit starts with no network at all. Game data
 * is not its business: the engine reads that through its own store in OPFS, which already
 * holds what a session has fetched.
 *
 * Nothing is precached from a list, because the module filenames carry a content hash that
 * a container build assigns after this file is written. One completed visit caches the
 * shell instead, which is the visit that has to happen anyway.
 */

var CACHE = "opents-shell-v2";

// A name carrying a content hash never changes meaning, so it is answered from the cache
// without asking. Everything else is asked for first and falls back.
var IMMUTABLE = /\/(Game(-asyncify)?|page|favicon|apple-touch-icon|icon-\d+)\.[0-9a-f]{12}\.(js|css|wasm|ico|png)$/;

// Read from the network every time: two name a release and one names a deployment, and a
// stale answer to any of them is worse than none.
var NEVER = /\/(assets\.json|relay\.json|downloads\.json|sw\.js)$/;

// Streamed by an element of the page's own rather than read through the engine's store.
var STREAMED = /\.(mp4|m4a)$/i;

self.addEventListener("install", function (event) {
	self.skipWaiting();
});

self.addEventListener("activate", function (event) {
	event.waitUntil(caches.keys().then(function (names) {
		return Promise.all(names.map(function (name) {
			return name === CACHE ? null : caches.delete(name);
		}));
	}).then(function () {
		return self.clients.claim();
	}));
});


// The modules a page no longer names are the previous release's, and they are what a shell
// cache would otherwise grow by on every deployment.
function prune(text) {
	return caches.open(CACHE).then(function (cache) {
		return cache.keys().then(function (requests) {
			return Promise.all(requests.map(function (request) {
				var name = request.url.split("/").pop();

				if (!/^Game(-asyncify)?\./.test(name) || text.indexOf(name) !== -1) {
					return null;
				}

				return cache.delete(request);
			}));
		});
	});
}


function keep(request, response) {
	if (!response || !response.ok || response.type === "opaque") return response;

	var copy = response.clone();
	caches.open(CACHE).then(function (cache) { cache.put(request, copy); });
	return response;
}


self.addEventListener("fetch", function (event) {
	var request = event.request;

	if (request.method !== "GET") return;

	var url = new URL(request.url);

	if (url.origin !== self.location.origin) return;
	if (NEVER.test(url.pathname)) return;

	// Archives are answered by the engine's own store, which holds ranges rather than whole
	// files; a second copy here would double what a release costs on disk. The score tracks
	// and the transmissions the radar plays are the exception: an audio or video element
	// fetches those for itself and never reads the store, so this is the only place they
	// can be kept.
	if (url.pathname.indexOf("/assets/") === 0 || url.pathname.indexOf("/files/") === 0) {
		if (!STREAMED.test(url.pathname)) return;

		// Network first even though the name carries a content hash, because a media
		// element asks in ranges and what is kept here is the whole file: answering a
		// range from it is worth doing when there is no network and not before.
		event.respondWith(fetch(request).then(function (answer) {
			return request.headers.has("range") ? answer : keep(request, answer);
		}).catch(function () {
			return caches.match(request, {ignoreVary: true}).then(function (hit) {
				return hit || caches.match(url.pathname);
			});
		}));
		return;
	}

	if (IMMUTABLE.test(url.pathname)) {
		event.respondWith(caches.match(request).then(function (hit) {
			return hit || fetch(request).then(function (answer) {
				return keep(request, answer);
			});
		}));
		return;
	}

	// The page itself, which has to follow a new release when there is a network and has to
	// be there when there is not.
	event.respondWith(fetch(request).then(function (answer) {
		if (answer.ok && /\.html$|\/$/.test(url.pathname)) {
			answer.clone().text().then(prune);
		}
		return keep(request, answer);
	}).catch(function () {
		return caches.match(request).then(function (hit) {
			return hit || caches.match(new URL("index.html", self.location).href);
		});
	}));
});
