/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "manifest.h"

#if defined(__EMSCRIPTEN__)

#include "httpsource.h"

#include <emscripten/emscripten.h>

#include <unordered_map>


// Synchronous because the engine's first file open is not underneath a
// promising export, where a suspending fetch would throw. A missing
// assets.json costs two failed requests once, not at every open.
EM_JS(int, Manifest_Http_Fetch, (void), {
	if (globalThis.__opentsManifest !== undefined) return globalThis.__opentsManifest ? 1 : 0;

	// The tests link this file and run under Node, which has no document; an empty
	// base resolves no URL, so such a host reads as one with no release.
	var base = (typeof Module !== "undefined" && Module["opentsManifestBase"]) ||
		(typeof document !== "undefined" ? document.baseURI : "");
	var cacheKey = "opents-manifest|" + base;

	function accept(text) {
		var manifest = JSON.parse(text);
		if (manifest["format"] !== "opents-web-assets-v2" ||
			!Array.isArray(manifest["files"]) || manifest["movies"] !== undefined) {
			return null;
		}
		return manifest;
	}

	// Distinguishes a tree this build cannot read from a server it could not reach.
	var reason = "unavailable";

	try {
		var pointer = new XMLHttpRequest();
		pointer.open("GET", new URL("assets.json", base).href, false);
		pointer.send(null);
		if (pointer.status !== 200) throw new Error("no assets.json");

		var current = JSON.parse(pointer.responseText);
		if (current["format"] !== "opents-web-assets-pointer-v1" ||
			typeof current["manifest"] !== "string") {
			reason = "incompatible";
			throw new Error("assets.json is not a release pointer");
		}

		var manifestRequest = new XMLHttpRequest();
		manifestRequest.open("GET", new URL(current["manifest"], base).href, false);
		manifestRequest.send(null);
		if (manifestRequest.status !== 200) {
			reason = "incompatible";
			throw new Error("no " + current["manifest"]);
		}

		var manifest = accept(manifestRequest.responseText);
		if (manifest === null) {
			reason = "incompatible";
			throw new Error("not a release manifest");
		}

		globalThis.__opentsManifest = manifest;
		globalThis.__opentsManifestBase = base;

		// Kept so a later reader wanting the release hash does not fetch the pointer
		// a second time; an offline launch has none and leaves it unset.
		globalThis.__opentsAssetsPointer = current;

		// Keep the last good manifest so a later launch resolves game data when the
		// server is unreachable; the files themselves are answered from the block store.
		try { localStorage.setItem(cacheKey, manifestRequest.responseText); } catch (error) {}
		return 1;
	} catch (error) {
		// The server did not answer with a usable tree, so fall back to the manifest last
		// cached for this address; an offline launch reads its game data from the store
		// beneath it.
		try {
			var cached = localStorage.getItem(cacheKey);
			var manifest = cached ? accept(cached) : null;
			if (manifest !== null) {
				globalThis.__opentsManifest = manifest;
				globalThis.__opentsManifestBase = base;
				return 1;
			}
		} catch (nested) {}

		// Nothing usable and no cache: the run has no game data. Record why and where so the
		// page surfaces it rather than leaving a canvas that never draws.
		globalThis.__opentsManifest = null;
		globalThis.__opentsManifestError = reason;
		globalThis.__opentsManifestErrorBase = base;
		return 0;
	}
});


// The manifest is a flat array of {name, path, sha256, size} records, and
// the path is relative to the release root rather than the page. The size
// crosses as a double, which loses nothing below 2^53 bytes.
EM_JS(int, Manifest_Http_Lookup, (char const * section, char const * name, char * url_buf, int url_buf_size, double * out_size), {
	try {
		var manifest = globalThis.__opentsManifest;
		if (!manifest) return 0;

		var group = manifest[UTF8ToString(section)];
		if (!Array.isArray(group)) return 0;

		// Names match without regard to case, as the engine's file layer does.
		var wanted = UTF8ToString(name).toUpperCase();
		var record = null;
		for (var index = 0; index < group.length; index++) {
			var entry_name = group[index]["name"];
			if (typeof entry_name === "string" && entry_name.toUpperCase() === wanted) {
				record = group[index];
				break;
			}
		}
		if (!record || typeof record["path"] !== "string") return 0;

		var url = new URL(record["path"], globalThis.__opentsManifestBase).href;
		var written = Math.min(url.length, url_buf_size - 1);
		for (var index = 0; index < written; index++) {
			HEAPU8[url_buf + index] = url.charCodeAt(index) & 255;
		}
		HEAPU8[url_buf + written] = 0;

		HEAPF64[out_size >> 3] = record["size"] || 0;
		return 1;
	} catch (error) {
		return 0;
	}
});


EM_JS(int, Manifest_Http_Offline, (char const * name), {
	try {
		var manifest = globalThis.__opentsManifest;
		if (!manifest || !Array.isArray(manifest["files"])) return 1;

		var wanted = UTF8ToString(name).toUpperCase();

		for (var index = 0; index < manifest["files"].length; index++) {
			var record = manifest["files"][index];
			var found = record["name"];

			if (typeof found !== "string" || found.toUpperCase() !== wanted) continue;
			if (record["offline"] === undefined) return 1;
			return record["offline"] ? 1 : 0;
		}

		return 1;
	} catch (error) {
		return 1;
	}
});


// A release names a few dozen archives with short names, so a fixed buffer
// holds the newline-joined list without a length probe.
EM_JS(int, Manifest_Http_List, (char const * section, char * buffer, int buffer_size), {
	try {
		var manifest = globalThis.__opentsManifest;
		if (!manifest) return 0;

		var group = manifest[UTF8ToString(section)];
		if (!Array.isArray(group)) return 0;

		var joined = group.map(function (record) { return record["name"]; }).join("\n");
		var written = Math.min(joined.length, buffer_size - 1);
		for (var index = 0; index < written; index++) {
			HEAPU8[buffer + index] = joined.charCodeAt(index) & 255;
		}
		HEAPU8[buffer + written] = 0;
		return 1;
	} catch (error) {
		return 0;
	}
});


namespace {

enum { MANIFEST_URL_MAX = 512 };
enum { MANIFEST_LIST_MAX = 8192 };

bool ManifestChecked = false;
bool ManifestAvailable = false;

std::unordered_map<std::string, std::shared_ptr<BlockFileClass>> OpenVolumes;
std::unordered_map<std::string, BlockEntryClass> OpenEntries;

bool Ensure_Loaded(void)
{
	if (!ManifestChecked) {
		ManifestChecked = true;
		ManifestAvailable = Manifest_Http_Fetch() != 0;
	}
	return(ManifestAvailable);
}


bool Lookup(char const * section, char const * name, std::string & url, std::uint64_t & size)
{
	if (!Ensure_Loaded()) return(false);

	char buffer[MANIFEST_URL_MAX];
	double reported_size = 0.0;

	if (Manifest_Http_Lookup(section, name, buffer, sizeof(buffer), &reported_size) == 0) {
		return(false);
	}

	url = buffer;
	size = (std::uint64_t)reported_size;
	return(true);
}

}	// namespace


std::shared_ptr<BlockFileClass> Manifest_Find(char const * name, BlockEntryClass & entry)
{
	if (name == nullptr || *name == '\0') return(nullptr);

	auto cached = OpenVolumes.find(name);
	if (cached != OpenVolumes.end()) {
		entry = OpenEntries[name];
		return(cached->second);
	}

	std::string url;
	std::uint64_t size = 0;

	if (!Lookup("files", name, url, size)) return(nullptr);

	std::unique_ptr<HttpBlockSourceClass> source(new HttpBlockSourceClass);
	if (!source->Open(url.c_str(), size)) return(nullptr);

	std::shared_ptr<BlockFileClass> volume = std::make_shared<BlockFileClass>();
	BlockEntryClass whole;

	// The server's answer for the length is trusted over the manifest's figure.
	std::uint64_t const opened_size = source->Total_Size();

	if (!volume->Attach_Whole(std::move(source), opened_size ? opened_size : size, whole)) {
		return(nullptr);
	}

	OpenVolumes.emplace(name, volume);
	OpenEntries.emplace(name, whole);
	entry = whole;
	return(volume);
}


std::string Manifest_Find_Movie(char const * name)
{
	if (name == nullptr || *name == '\0') return(std::string());

	std::string url;
	std::uint64_t size = 0;

	if (!Lookup("files", name, url, size)) return(std::string());

	return(url);
}


bool Manifest_Offline(char const * name)
{
	if (name == nullptr || *name == '\0') return(false);
	if (!Ensure_Loaded()) return(false);

	return(Manifest_Http_Offline(name) != 0);
}


std::vector<std::string> Manifest_List_Files(void)
{
	std::vector<std::string> names;

	if (!Ensure_Loaded()) return(names);

	std::vector<char> buffer(MANIFEST_LIST_MAX);
	if (Manifest_Http_List("files", buffer.data(), (int)buffer.size()) == 0) return(names);

	std::string joined(buffer.data());
	std::size_t start = 0;

	while (start < joined.size()) {
		std::size_t const newline = joined.find('\n', start);
		std::size_t const stop = (newline == std::string::npos) ? joined.size() : newline;

		if (stop > start) names.push_back(joined.substr(start, stop - start));
		start = stop + 1;
	}

	return(names);
}

#endif
