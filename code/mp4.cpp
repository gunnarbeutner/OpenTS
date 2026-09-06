/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if defined(OPENTS_MP4_MOVIES)

#include "mp4.h"

#include "_keyboar.h"
#include "audio/audioengine.h"
#include "browser.h"
#include "ccfile.h"
#include "dbgprint.h"
#include "globals.h"
#include "goptions.h"
#include "keyboard.h"
#include "manifest.h"
#include "video.h"
#include "vqoption.h"
#include "win.h"
#include "win32timer.h"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>


namespace {

enum MP4Status {
	MP4_STATUS_FRAME = 1 << 0,
	MP4_STATUS_ENDED = 1 << 1,
	MP4_STATUS_ERROR = 1 << 2,
};

enum MP4Result {
	MP4_PLAY_NO_ERROR = 0,
	MP4_PLAY_USER_BREAK = 1,
};

EM_JS(int, MP4_Browser_Create, (void const * data, int size, int volume), {
	if (typeof document === 'undefined' || typeof Blob === 'undefined') {
		return 0;
	}

	var movies = Module.OpenTSMovies;
	if (!movies) {
		movies = Module.OpenTSMovies = {
			next: 1,
			records: {},
			armed: false,
			gate: null,
			pool: []
		};
	}
	movies.acquire = movies.acquire || function (volume) {
		var video = movies.pool.pop();

		if (!video) {
			video = document.createElement('video');
			video.preload = 'auto';
			video.playsInline = true;
			video.setAttribute('playsinline', 'true');
			video.setAttribute('webkit-playsinline', 'true');
			video.controls = false;
			video.disablePictureInPicture = true;
			video.disableRemotePlayback = true;
			video.style.position = 'fixed';
			video.style.left = '-2px';
			video.style.top = '-2px';
			video.style.width = '1px';
			video.style.height = '1px';
			video.style.opacity = '0.001';
			video.style.pointerEvents = 'none';
			video.style.zIndex = '-2147483648';
			document.body.appendChild(video);
		}

		video.volume = Math.max(0, Math.min(1, volume / 255));
		video.muted = volume <= 0;
		return video;
	};

	// Safari grants a media element the right to make a sound only through a gesture that
	// reached that element, and a film created mid-mission has none of its own.
	movies.release = movies.release || function (video) {
		video.pause();
		video.removeAttribute('src');
		video.load();
		video.muted = false;

		if (movies.pool.length < 3) {
			movies.pool.push(video);
		} else if (video.parentNode) {
			video.parentNode.removeChild(video);
		}
	};

	// Spends a gesture on the idle elements, which is what they are kept for. An element
	// carrying a film is left alone: play/pause on it would interrupt what is showing.
	movies.activate = movies.activate || function () {
		movies.pool.forEach(function (video) {
			try {
				var promise = video.play();
				if (promise) promise.then(function () { video.pause(); }).catch(function () {});
			} catch (error) {}
		});
	};

	movies.showGate = movies.showGate || function () {
		if (!movies.gate) {
			var gate = movies.gate = document.createElement('button');
			gate.type = 'button';
			gate.textContent = 'Tap to play movie';
			gate.style.position = 'fixed';
			gate.style.left = '0';
			gate.style.top = '0';
			gate.style.width = '100%';
			gate.style.height = '100%';
			gate.style.zIndex = '2147483647';
			gate.style.font = 'bold 16px sans-serif';
			gate.style.color = 'white';
			gate.style.background = 'rgba(0, 0, 0, 0.85)';
			gate.style.border = '0';
			document.body.appendChild(gate);
		}
		movies.gate.style.display = 'block';
	};
	// A film that fell back to muted playback is not a blocked one and raises no gate, so
	// the page's sound prompt is told to ask for the gesture instead.
	movies.noteMuted = movies.noteMuted || function () {
		var muted = Object.keys(movies.records).some(function (key) {
			var candidate = movies.records[key];
			return candidate.wanted && candidate.mutedForAutoplay;
		});

		if (globalThis.OpenTS_State) globalThis.OpenTS_State.movieMuted = muted;
	};

	movies.hideGate = movies.hideGate || function () {
		var blocked = Object.keys(movies.records).some(function (key) {
			var candidate = movies.records[key];
			return candidate.blocked && candidate.wanted;
		});
		if (!blocked && movies.gate) movies.gate.style.display = 'none';
	};
	movies.tryPlay = movies.tryPlay || function (record) {
		var promise = record.video.play();
		if (!promise) return;
		promise.catch(function (error) {
			if (!record.wanted) return;
			if (error && error.name === 'NotSupportedError') {
				record.error = true;
				return;
			}
			// A browser refuses autoplay with sound but not muted, so the
			// picture starts and the gate only brings the sound back.
			if (!record.video.muted) {
				record.mutedForAutoplay = true;
				record.video.muted = true;
				movies.noteMuted();
				movies.tryPlay(record);
				return;
			}
			record.blocked = true;
			movies.showGate();
		});
	};

	if (!movies.armed) {
		movies.armed = true;
		var unlock = function () {
			Object.keys(movies.records).forEach(function (key) {
				var record = movies.records[key];
				if (record.mutedForAutoplay) {
					record.mutedForAutoplay = false;
					record.video.muted = false;
				}
				if (record.wanted && record.blocked) {
					record.blocked = false;
					movies.tryPlay(record);
				}
			});
			movies.activate();
			movies.noteMuted();
			movies.hideGate();
		};
		['pointerdown', 'mousedown', 'touchstart', 'touchend', 'keydown', 'click'].forEach(function (name) {
			document.addEventListener(name, unlock, true);
		});
	}

	var id = movies.next++;
	data = data >>> 0;
	var bytes = HEAPU8.slice(data, data + size);
	var url = URL.createObjectURL(new Blob([bytes], {type: 'video/mp4'}));
	var video = movies.acquire(volume);
	video.src = url;
	document.body.appendChild(video);

	var canvas = document.createElement('canvas');
	var context = canvas.getContext('2d', {willReadFrequently: true});
	var record = movies.records[id] = {
		video: video,
		url: url,
		canvas: canvas,
		context: context,
		ready: 0,
		error: false,
		ended: false,
		wanted: false,
		blocked: false,
		frameSerial: 0,
		copiedSerial: -1,
		copiedTime: -1,
		destroyed: false
	};

	video.addEventListener('loadedmetadata', function () {
		canvas.width = video.videoWidth;
		canvas.height = video.videoHeight;
		record.ready = (canvas.width > 0 && canvas.height > 0 && context) ? 1 : -1;
	});
	video.addEventListener('playing', function () {
		record.blocked = false;
		movies.hideGate();
	});
	video.addEventListener('ended', function () { record.ended = true; });

	// Nothing outside the engine stops a film. A media key, the browser's own media
	// controls or a remote playback device can pause an element that carries no controls
	// of its own, and the play loop waits on the film ending rather than on it running --
	// so a pause it did not ask for would hang the game rather than pause it. The engine's
	// own pause clears "wanted" first, and is left alone.
	video.addEventListener('pause', function () {
		if (!record.wanted || record.ended || video.ended) return;
		movies.tryPlay(record);
	});
	video.addEventListener('error', function () { record.error = true; record.ready = -1; });

	if (video.requestVideoFrameCallback) {
		var frame = function () {
			if (record.destroyed) return;
			record.frameSerial++;
			video.requestVideoFrameCallback(frame);
		};
		video.requestVideoFrameCallback(frame);
	}

	video.load();
	return id;
});


// A movie the manifest names is streamed by the video element from its URL;
// URL.revokeObjectURL on a non blob URL in MP4_Browser_Destroy is a no-op.
EM_JS(int, MP4_Browser_Create_From_Url, (char const * url, int volume), {
	if (typeof document === 'undefined') {
		return 0;
	}

	var movies = Module.OpenTSMovies;
	if (!movies) {
		movies = Module.OpenTSMovies = {
			next: 1,
			records: {},
			armed: false,
			gate: null,
			pool: []
		};
	}
	movies.acquire = movies.acquire || function (volume) {
		var video = movies.pool.pop();

		if (!video) {
			video = document.createElement('video');
			video.preload = 'auto';
			video.playsInline = true;
			video.setAttribute('playsinline', 'true');
			video.setAttribute('webkit-playsinline', 'true');
			video.controls = false;
			video.disablePictureInPicture = true;
			video.disableRemotePlayback = true;
			video.style.position = 'fixed';
			video.style.left = '-2px';
			video.style.top = '-2px';
			video.style.width = '1px';
			video.style.height = '1px';
			video.style.opacity = '0.001';
			video.style.pointerEvents = 'none';
			video.style.zIndex = '-2147483648';
			document.body.appendChild(video);
		}

		video.volume = Math.max(0, Math.min(1, volume / 255));
		video.muted = volume <= 0;
		return video;
	};

	// Safari grants a media element the right to make a sound only through a gesture that
	// reached that element, and a film created mid-mission has none of its own.
	movies.release = movies.release || function (video) {
		video.pause();
		video.removeAttribute('src');
		video.load();
		video.muted = false;

		if (movies.pool.length < 3) {
			movies.pool.push(video);
		} else if (video.parentNode) {
			video.parentNode.removeChild(video);
		}
	};

	// Spends a gesture on the idle elements, which is what they are kept for. An element
	// carrying a film is left alone: play/pause on it would interrupt what is showing.
	movies.activate = movies.activate || function () {
		movies.pool.forEach(function (video) {
			try {
				var promise = video.play();
				if (promise) promise.then(function () { video.pause(); }).catch(function () {});
			} catch (error) {}
		});
	};

	movies.showGate = movies.showGate || function () {
		if (!movies.gate) {
			var gate = movies.gate = document.createElement('button');
			gate.type = 'button';
			gate.textContent = 'Tap to play movie';
			gate.style.position = 'fixed';
			gate.style.left = '0';
			gate.style.top = '0';
			gate.style.width = '100%';
			gate.style.height = '100%';
			gate.style.zIndex = '2147483647';
			gate.style.font = 'bold 16px sans-serif';
			gate.style.color = 'white';
			gate.style.background = 'rgba(0, 0, 0, 0.85)';
			gate.style.border = '0';
			document.body.appendChild(gate);
		}
		movies.gate.style.display = 'block';
	};
	// A film that fell back to muted playback is not a blocked one and raises no gate, so
	// the page's sound prompt is told to ask for the gesture instead.
	movies.noteMuted = movies.noteMuted || function () {
		var muted = Object.keys(movies.records).some(function (key) {
			var candidate = movies.records[key];
			return candidate.wanted && candidate.mutedForAutoplay;
		});

		if (globalThis.OpenTS_State) globalThis.OpenTS_State.movieMuted = muted;
	};

	movies.hideGate = movies.hideGate || function () {
		var blocked = Object.keys(movies.records).some(function (key) {
			var candidate = movies.records[key];
			return candidate.blocked && candidate.wanted;
		});
		if (!blocked && movies.gate) movies.gate.style.display = 'none';
	};
	movies.tryPlay = movies.tryPlay || function (record) {
		var promise = record.video.play();
		if (!promise) return;
		promise.catch(function (error) {
			if (!record.wanted) return;
			if (error && error.name === 'NotSupportedError') {
				record.error = true;
				return;
			}
			if (!record.video.muted) {
				record.mutedForAutoplay = true;
				record.video.muted = true;
				movies.noteMuted();
				movies.tryPlay(record);
				return;
			}
			record.blocked = true;
			movies.showGate();
		});
	};

	if (!movies.armed) {
		movies.armed = true;
		var unlock = function () {
			Object.keys(movies.records).forEach(function (key) {
				var record = movies.records[key];
				if (record.mutedForAutoplay) {
					record.mutedForAutoplay = false;
					record.video.muted = false;
				}
				if (record.wanted && record.blocked) {
					record.blocked = false;
					movies.tryPlay(record);
				}
			});
			movies.activate();
			movies.noteMuted();
			movies.hideGate();
		};
		['pointerdown', 'mousedown', 'touchstart', 'touchend', 'keydown', 'click'].forEach(function (name) {
			document.addEventListener(name, unlock, true);
		});
	}

	var id = movies.next++;
	var video = movies.acquire(volume);
	video.crossOrigin = 'anonymous';
	video.src = UTF8ToString(url);
	document.body.appendChild(video);

	var canvas = document.createElement('canvas');
	var context = canvas.getContext('2d', {willReadFrequently: true});
	var record = movies.records[id] = {
		video: video,
		url: null,
		canvas: canvas,
		context: context,
		ready: 0,
		error: false,
		ended: false,
		wanted: false,
		blocked: false,
		frameSerial: 0,
		copiedSerial: -1,
		copiedTime: -1,
		destroyed: false
	};

	video.addEventListener('loadedmetadata', function () {
		canvas.width = video.videoWidth;
		canvas.height = video.videoHeight;
		record.ready = (canvas.width > 0 && canvas.height > 0 && context) ? 1 : -1;
	});
	video.addEventListener('playing', function () {
		record.blocked = false;
		movies.hideGate();
	});
	video.addEventListener('ended', function () { record.ended = true; });

	// Nothing outside the engine stops a film. A media key, the browser's own media
	// controls or a remote playback device can pause an element that carries no controls
	// of its own, and the play loop waits on the film ending rather than on it running --
	// so a pause it did not ask for would hang the game rather than pause it. The engine's
	// own pause clears "wanted" first, and is left alone.
	video.addEventListener('pause', function () {
		if (!record.wanted || record.ended || video.ended) return;
		movies.tryPlay(record);
	});
	video.addEventListener('error', function () { record.error = true; record.ready = -1; });

	if (video.requestVideoFrameCallback) {
		var frame = function () {
			if (record.destroyed) return;
			record.frameSerial++;
			video.requestVideoFrameCallback(frame);
		};
		video.requestVideoFrameCallback(frame);
	}

	video.load();
	return id;
});


EM_JS(int, MP4_Browser_Metadata, (int id, int * width, int * height), {
	var movies = Module.OpenTSMovies;
	var record = movies && movies.records[id];
	if (!record) return -1;
	if (record.ready < 0 || record.error) return -1;
	if (!record.ready) return 0;
	HEAP32[width >> 2] = record.video.videoWidth;
	HEAP32[height >> 2] = record.video.videoHeight;
	return 1;
});


EM_JS(void, MP4_Browser_Play, (int id), {
	var movies = Module.OpenTSMovies;
	var record = movies && movies.records[id];
	if (!record || record.error) return;
	record.wanted = true;
	record.ended = false;
	movies.tryPlay(record);
});


EM_JS(void, MP4_Browser_Pause, (int id), {
	var movies = Module.OpenTSMovies;
	var record = movies && movies.records[id];
	if (!record) return;
	record.wanted = false;
	record.video.pause();
	record.blocked = false;
	movies.hideGate();
});


EM_JS(void, MP4_Browser_Seek, (int id, double seconds), {
	var movies = Module.OpenTSMovies;
	var record = movies && movies.records[id];
	if (!record) return;
	record.ended = false;
	record.copiedTime = -1;
	record.video.currentTime = Math.max(0, seconds);
});


EM_JS(int, MP4_Browser_Status, (int id), {
	var movies = Module.OpenTSMovies;
	var record = movies && movies.records[id];
	if (!record || record.error) return 4;
	var status = record.ended || record.video.ended ? 2 : 0;
	if (record.video.readyState >= 2) {
		if (record.video.requestVideoFrameCallback) {
			if (record.frameSerial !== record.copiedSerial) status |= 1;
		} else if (record.video.currentTime !== record.copiedTime) {
			status |= 1;
		}
	}
	return status;
});


// Firefox on Android hands a hardware decoded frame to WebGL but not to a 2D
// canvas, so the frame is read back off a framebuffer; one context serves
// every movie because a page is allowed only a few.
EM_JS(void, MP4_Browser_Install_Reader, (void), {
	var movies = Module.OpenTSMovies;
	if (!movies || movies.readFrame) return;

	movies.readFrame = function (video, width, height) {
		try {
			if (!movies.readCanvas) {
				movies.readCanvas = document.createElement("canvas");
				movies.readGl = movies.readCanvas.getContext("webgl2", { preserveDrawingBuffer: false })
					|| movies.readCanvas.getContext("webgl");
				if (!movies.readGl) return null;

				movies.readTexture = movies.readGl.createTexture();
				movies.readFrameBuffer = movies.readGl.createFramebuffer();
			}

			var gl = movies.readGl;
			if (gl.isContextLost && gl.isContextLost()) return null;

			gl.bindTexture(gl.TEXTURE_2D, movies.readTexture);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
			gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
			gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
			gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, video);

			gl.bindFramebuffer(gl.FRAMEBUFFER, movies.readFrameBuffer);
			gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0,
				gl.TEXTURE_2D, movies.readTexture, 0);

			if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) return null;

			if (!movies.readPixels || movies.readPixels.length !== width * height * 4) {
				movies.readPixels = new Uint8Array(width * height * 4);
			}

			gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, movies.readPixels);
			return movies.readPixels;
		} catch (exception) {
			return null;
		}
	};
});


EM_JS(int, MP4_Browser_Copy_RGBA_Frame, (int id, void * destination), {
	var movies = Module.OpenTSMovies;
	var record = movies && movies.records[id];
	if (!record || record.error || record.video.readyState < 2 || !record.context) return 0;

	var width = record.video.videoWidth;
	var height = record.video.videoHeight;
	if (width <= 0 || height <= 0) return 0;

	// The destination holds width * height * 4 bytes of RGBA, which canvas
	// ImageData already is.
	try {
		var pixels = movies.readFrame(record.video, width, height);
		if (!pixels) {
			record.context.drawImage(record.video, 0, 0, width, height);
			pixels = record.context.getImageData(0, 0, width, height).data;
		}
		HEAPU8.set(pixels, destination >>> 0);
		record.copiedSerial = record.frameSerial;
		record.copiedTime = record.video.currentTime;
		return 1;
	} catch (exception) {
		record.error = true;
		return 0;
	}
});


// An inline movie is composited into a panel under other UI, so it writes
// through the 16 bit draw buffer.
EM_JS(int, MP4_Browser_Copy_Frame,
	(int id, void * destination, int stride, int surface_height, int x_offset, int y_offset), {
	var movies = Module.OpenTSMovies;
	var record = movies && movies.records[id];
	if (!record || record.error || record.video.readyState < 2 || !record.context) return 0;

	var width = record.video.videoWidth;
	var height = record.video.videoHeight;
	var copyWidth = Math.min(width, stride - x_offset);
	var copyHeight = Math.min(height, surface_height - y_offset);
	if (copyWidth <= 0 || copyHeight <= 0 || x_offset < 0 || y_offset < 0) return 0;

	// The frame texture is 565 (Build_Convert_Table in bgfxbackend.cpp); a 4x4
	// ordered dither keeps a gradient from banding on the truncation.
	var BAYER4 = [[0,8,2,10],[12,4,14,6],[3,11,1,9],[15,7,13,5]];

	try {
		var pixels = movies.readFrame(record.video, width, height);
		var sourceStride = width * 4;

		if (!pixels) {
			record.context.drawImage(record.video, 0, 0, width, height);
			pixels = record.context.getImageData(0, 0, copyWidth, copyHeight).data;
			sourceStride = copyWidth * 4;
		}

		destination = destination >>> 0;
		var target = destination >>> 1;
		for (var y = 0; y < copyHeight; y++) {
			var sourceRow = y * sourceStride;
			var targetRow = target + (y + y_offset) * stride + x_offset;
			var bayerRow = BAYER4[y & 3];
			for (var x = 0; x < copyWidth; x++) {
				var source = sourceRow + x * 4;
				var bayer = bayerRow[x & 3];
				var red = pixels[source] + (bayer >> 1);
				var green = pixels[source + 1] + (bayer >> 2);
				var blue = pixels[source + 2] + (bayer >> 1);
				if (red > 255) red = 255;
				if (green > 255) green = 255;
				if (blue > 255) blue = 255;
				HEAPU16[targetRow + x] = ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
			}
		}
		record.copiedSerial = record.frameSerial;
		record.copiedTime = record.video.currentTime;
		return 1;
	} catch (exception) {
		record.error = true;
		return 0;
	}
});


EM_JS(void, MP4_Browser_Volume, (int id, int volume), {
	var movies = Module.OpenTSMovies;
	var record = movies && movies.records[id];
	if (record) {
		record.video.volume = Math.max(0, Math.min(1, volume / 255));
		record.video.muted = volume <= 0;
	}
});


EM_JS(void, MP4_Browser_Destroy, (int id), {
	var movies = Module.OpenTSMovies;
	var record = movies && movies.records[id];
	if (!record) return;
	record.destroyed = true;
	record.wanted = false;
	record.blocked = false;
	movies.release(record.video);
	URL.revokeObjectURL(record.url);
	delete movies.records[id];
	movies.noteMuted();
	movies.hideGate();
});

}


MP4Class::MP4Class(char const * filename, int, MovieSurfaceLockCallback surface_lock,
	MovieSurfaceUnlockCallback surface_unlock, MovieSurfaceDrawCallback surface_draw,
	MovieIdleCallback idle, int, int) :
	MovieID(0),
	Width(0),
	Height(0),
	DrawBufferWidth(0),
	DrawBufferHeight(0),
	DrawOffsetX(0),
	DrawOffsetY(0),
	IsFullscreenVideo(false),
	VideoFrameBuffer(NULL),
	Volume(255),
	IsOpen(false),
	IdleCallback(idle),
	IsPaused(false),
	IsStarted(false),
	IsFocusPaused(false),
	SurfaceLockCallback(surface_lock),
	SurfaceUnlockCallback(surface_unlock),
	SurfaceDrawCallback(surface_draw)
{
	std::strncpy(Filename, filename, sizeof(Filename) - 1);
	Filename[sizeof(Filename) - 1] = '\0';
}


MP4Class::~MP4Class(void)
{
	Close_And_Free_VQA();
	delete [] VideoFrameBuffer;
}


bool MP4Class::Open_And_Load_Buffers(void)
{
	// A movie the manifest names is streamed by the video element from its URL,
	// so the manifest is asked before Filename reaches CCFileClass.
	std::string const manifest_url = Manifest_Find_Movie(Filename);
	if (!manifest_url.empty()) {
		MovieID = MP4_Browser_Create_From_Url(manifest_url.c_str(), Volume);
		MP4_Browser_Install_Reader();
	} else {
		CCFileClass file(Filename);
		int const size = file.Size();
		if (size < 12) {
			return(false);
		}

		std::unique_ptr<unsigned char[]> bytes(new unsigned char[size]);
		if (file.Read(bytes.get(), size) != size || std::memcmp(bytes.get() + 4, "ftyp", 4) != 0) {
			return(false);
		}

		MovieID = MP4_Browser_Create(bytes.get(), size, Volume);
		MP4_Browser_Install_Reader();
	}

	if (MovieID == 0) {
		return(false);
	}

	int state = 0;
	while ((state = MP4_Browser_Metadata(MovieID, &Width, &Height)) == 0) {
		Browser_Service();
		AudioEngine.Service();
		Win32_Timer_Service();
		Video_Present_If_Dirty();
		Browser_Yield();
	}

	if (state < 0) {
		DebugString("MP4: the browser refused %s\n", Filename);
		Close_And_Free_VQA();
		return(false);
	}

	IsOpen = true;
	return(true);
}


bool MP4Class::Set_Loop(int, int)
{
	return(false);
}


bool MP4Class::Set_Loop(int, int, int)
{
	return(false);
}


void MP4Class::Seek_To_Frame(int frame)
{
	if (frame <= 0) MP4_Browser_Seek(MovieID, 0.0);
}


void MP4Class::Start(void)
{
	if (IsOpen && !IsStarted) {
		MP4_Browser_Play(MovieID);
		IsStarted = true;
	}
}


bool MP4Class::Draw_Frame(void)
{
	if (Width <= 0 || Height <= 0) {
		return(false);
	}

	// An inline movie keeps writing through the draw buffer that compositing
	// expects.
	if (IsFullscreenVideo) {
		if (VideoFrameBuffer == NULL) {
			VideoFrameBuffer = new unsigned char[(std::size_t)Width * Height * 4];
		}

		bool const copied = MP4_Browser_Copy_RGBA_Frame(MovieID, VideoFrameBuffer) != 0;
		if (copied) {
			// Fit every frame, because a resize during playback is not serviced
			// elsewhere.
			VideoScaleInfo const & scale = Video_Get_Scale_Info();
			int fit_x = 0, fit_y = 0, fit_width = Width, fit_height = Height;

			if (scale.DrawableWidth > 0 && scale.DrawableHeight > 0) {
				if (Width < scale.DrawableWidth && Height < scale.DrawableHeight) {
					float const across = (float)scale.DrawableWidth / (float)Width;
					float const down = (float)scale.DrawableHeight / (float)Height;
					float const ratio = (across < down) ? across : down;
					fit_width = (int)(Width * ratio);
					fit_height = (int)(Height * ratio);
				}
				fit_x = (scale.DrawableWidth - fit_width) / 2;
				fit_y = (scale.DrawableHeight - fit_height) / 2;
			}

			Video_Queue_Movie_Frame(VideoFrameBuffer, Width * 4, Width, Height,
				fit_x, fit_y, fit_width, fit_height);
		}
		return(copied);
	}

	if (SurfaceLockCallback == NULL || SurfaceUnlockCallback == NULL) {
		return(false);
	}

	void * surface = SurfaceLockCallback();
	if (surface == NULL) {
		return(false);
	}

	bool const copied = MP4_Browser_Copy_Frame(MovieID, surface, DrawBufferWidth,
		DrawBufferHeight, DrawOffsetX, DrawOffsetY) != 0;
	SurfaceUnlockCallback();

	if (copied && SurfaceDrawCallback != NULL) {
		SurfaceDrawCallback();
	}
	return(copied);
}


int MP4Class::Play_VQA(int, bool breakout)
{
	Start();
	bool focus_paused = false;

	while (true) {
		Browser_Service();
		AudioEngine.Service();
		Win32_Timer_Service();
		Video_Present_If_Dirty();

		if (!GameInFocus && !focus_paused) {
			MP4_Browser_Pause(MovieID);
			focus_paused = true;
		} else if (GameInFocus && focus_paused && !IsPaused) {
			MP4_Browser_Play(MovieID);
			focus_paused = false;
		}

		int const status = MP4_Browser_Status(MovieID);
		if ((status & MP4_STATUS_FRAME) != 0) {
			Draw_Frame();
		}
		if ((status & (MP4_STATUS_ENDED | MP4_STATUS_ERROR)) != 0) {
			break;
		}

		if (IdleCallback != NULL && IdleCallback() && !breakout) {
			return(MP4_PLAY_USER_BREAK);
		}

		if (!breakout && Keyboard->Check() && Keyboard->Get() == (KN_ESC | WWKEY_RLS_BIT)) {
			return(MP4_PLAY_USER_BREAK);
		}

		Browser_Yield();
	}

	return(MP4_PLAY_NO_ERROR);
}


bool MP4Class::Advance_Frame(bool &finished)
{
	finished = false;
	if (!IsOpen || IsPaused) {
		return(false);
	}
	if (!GameInFocus) {
		if (IsStarted && !IsFocusPaused) MP4_Browser_Pause(MovieID);
		IsFocusPaused = true;
		return(false);
	}
	if (IsFocusPaused) {
		MP4_Browser_Play(MovieID);
		IsFocusPaused = false;
	}

	Start();
	int const status = MP4_Browser_Status(MovieID);
	bool const drew = (status & MP4_STATUS_FRAME) != 0 && Draw_Frame();
	finished = (status & (MP4_STATUS_ENDED | MP4_STATUS_ERROR)) != 0;
	return(drew);
}


void MP4Class::Pause_VQA(void)
{
	if (IsOpen) MP4_Browser_Pause(MovieID);
	IsPaused = true;
}


void MP4Class::Resume_VQA(void)
{
	IsPaused = false;
	if (IsOpen && IsStarted) MP4_Browser_Play(MovieID);
}


void MP4Class::Close_And_Free_VQA(void)
{
	if (MovieID != 0) {
		MP4_Browser_Destroy(MovieID);
		MovieID = 0;
	}
	Video_Clear_Movie_Frame();
	IsOpen = false;
	IsStarted = false;
	IsFocusPaused = false;
}


void MP4Class::Reset_VQA(void)
{
	if (MovieID != 0) {
		MP4_Browser_Pause(MovieID);
		MP4_Browser_Seek(MovieID, 0.0);
	}
	IsStarted = false;
	IsPaused = false;
	IsFocusPaused = false;
}


bool MP4Class::Set_Draw_Buffer(void *, int buffer_width, int buffer_height, int x_offset, int y_offset)
{
	if (buffer_width != -1) DrawBufferWidth = buffer_width;
	if (buffer_height != -1) DrawBufferHeight = buffer_height;
	DrawOffsetX = std::max(x_offset, 0);
	DrawOffsetY = std::max(y_offset, 0);
	return(true);
}


void MP4Class::Set_Fullscreen_Video(void)
{
	IsFullscreenVideo = true;
}


int MP4Class::Get_Desired_Color_Mode(void)
{
	return(1);
}


void MP4Class::Set_Primary_Color_Mode(int)
{
}


int MP4Class::Get_VQA_Width(void) const
{
	return(Width);
}


int MP4Class::Get_VQA_Height(void) const
{
	return(Height);
}


int MP4Class::Set_VQA_Volume(int volume)
{
	int const previous = Volume;
	if (Get_Option(OPTION_NO_AUDIO)) {
		Volume = 0;
	} else if (volume != -1) {
		Volume = std::clamp(volume, 0, 255);
	}
	if (MovieID != 0) MP4_Browser_Volume(MovieID, Volume);
	return(previous);
}


bool MP4Class::Is_Paused(void) const
{
	return(IsPaused);
}

#endif
