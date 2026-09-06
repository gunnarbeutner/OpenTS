/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if defined(__EMSCRIPTEN__)

#include "musicbackend.h"

#include "manifest.h"

#include <emscripten/emscripten.h>

#include <string>


namespace {

EM_JS(int, Music_Element_Create, (char const * url, int volume), {
	if (typeof document === 'undefined') return 0;

	var music = Module.OpenTSMusic;
	if (!music) {
		music = Module.OpenTSMusic = {
			next: 1,
			records: {},
			armed: false
		};
	}

	// A track that autoplay blocked is retried on the same first-gesture unlock
	// movies use.
	if (!music.armed) {
		music.armed = true;
		var unlock = function () {
			Object.keys(music.records).forEach(function (key) {
				var record = music.records[key];
				if (!record.blocked) return;
				record.blocked = false;
				var promise = record.audio.play();
				if (promise) promise.catch(function () {});
			});
		};
		['pointerdown', 'mousedown', 'touchstart', 'touchend', 'keydown', 'click'].forEach(function (name) {
			document.addEventListener(name, unlock, true);
		});
	}

	var id = music.next++;
	var audio = new Audio();
	audio.preload = 'auto';
	audio.crossOrigin = 'anonymous';
	audio.volume = Math.max(0, Math.min(1, volume / 255));
	audio.src = UTF8ToString(url);

	var record = music.records[id] = {
		audio: audio,
		ended: false,
		error: false,
		blocked: false,
		focusPaused: false,
		fadeTimer: null
	};

	audio.addEventListener('ended', function () { record.ended = true; });
	audio.addEventListener('error', function () { record.error = true; });

	var promise = audio.play();
	if (promise) {
		promise.catch(function (error) {
			if (error && error.name === 'NotSupportedError') {
				record.error = true;
				return;
			}
			record.blocked = true;
		});
	}

	return id;
});


EM_JS(void, Music_Element_Stop, (int id), {
	var music = Module.OpenTSMusic;
	var record = music && music.records[id];
	if (!record) return;
	if (record.fadeTimer) clearInterval(record.fadeTimer);
	record.audio.pause();
	record.audio.removeAttribute('src');
	record.audio.load();
	delete music.records[id];
});


EM_JS(void, Music_Element_Fade, (int id, int milliseconds), {
	var music = Module.OpenTSMusic;
	var record = music && music.records[id];
	if (!record || record.fadeTimer) return;

	var durationMs = Math.max(1, milliseconds);
	var startVolume = record.audio.volume;
	var startTime = performance.now();

	record.fadeTimer = setInterval(function () {
		var t = Math.min(1, (performance.now() - startTime) / durationMs);
		record.audio.volume = startVolume * (1 - t);
		if (t >= 1) {
			clearInterval(record.fadeTimer);
			record.fadeTimer = null;
			record.audio.pause();
			record.ended = true;
		}
	}, 50);
});


EM_JS(int, Music_Element_Still_Playing, (int id), {
	var music = Module.OpenTSMusic;
	var record = music && music.records[id];
	if (!record || record.error || record.ended || record.audio.ended) return 0;
	return 1;
});


// A track the window's focus silenced is marked, so what resumes is only what this
// paused: not one autoplay is still holding, and not one the game stopped meanwhile.
EM_JS(void, Music_Element_Pause_All, (void), {
	var music = Module.OpenTSMusic;
	if (!music) return;

	Object.keys(music.records).forEach(function (key) {
		var record = music.records[key];
		if (record.blocked || record.ended || record.error || record.audio.paused) return;
		record.focusPaused = true;
		record.audio.pause();
	});
});


EM_JS(void, Music_Element_Resume_All, (void), {
	var music = Module.OpenTSMusic;
	if (!music) return;

	Object.keys(music.records).forEach(function (key) {
		var record = music.records[key];
		if (!record.focusPaused) return;
		record.focusPaused = false;
		if (record.ended || record.error || record.audio.ended) return;

		var promise = record.audio.play();
		if (promise) promise.catch(function () { record.blocked = true; });
	});
});


EM_JS(void, Music_Element_Set_Volume, (int id, int volume), {
	var music = Module.OpenTSMusic;
	var record = music && music.records[id];
	if (!record) return;
	record.audio.volume = Math.max(0, Math.min(1, volume / 255));
});


std::string Browser_Name(char const * theme_filename)
{
	std::string name(theme_filename);
	std::size_t const dot = name.find_last_of('.');
	return((dot == std::string::npos ? name : name.substr(0, dot)) + ".M4A");
}

}	// namespace


bool Music_Browser_Available(char const * theme_filename)
{
	if (theme_filename == nullptr || *theme_filename == '\0') return(false);
	return(!Manifest_Find_Movie(Browser_Name(theme_filename).c_str()).empty());
}


// A track resolves through the manifest's "files" section like a movie does,
// to a URL the browser fetches and caches itself.
int Music_Browser_Play(char const * theme_filename, int volume)
{
	if (theme_filename == nullptr || *theme_filename == '\0') return(-1);

	std::string const url = Manifest_Find_Movie(Browser_Name(theme_filename).c_str());
	if (url.empty()) return(-1);

	int const id = Music_Element_Create(url.c_str(), volume);
	return(id > 0 ? id : -1);
}


void Music_Browser_Stop(int handle)
{
	if (handle > 0) Music_Element_Stop(handle);
}


void Music_Browser_Fade(int handle, int milliseconds)
{
	if (handle > 0) Music_Element_Fade(handle, milliseconds);
}


bool Music_Browser_Still_Playing(int handle)
{
	return(handle > 0 && Music_Element_Still_Playing(handle) != 0);
}


void Music_Browser_Pause(void)
{
	Music_Element_Pause_All();
}


void Music_Browser_Resume(void)
{
	Music_Element_Resume_All();
}


void Music_Browser_Set_Volume(int handle, int volume)
{
	if (handle > 0) Music_Element_Set_Volume(handle, volume);
}

#endif
