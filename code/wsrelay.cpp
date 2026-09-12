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

#include "wsrelay.h"

#include "browser.h"
#include "dbgprint.h"

#include <emscripten/emscripten.h>

#include <cstring>


namespace {

enum RelayState {
	RELAY_CONNECTING = 0,
	RELAY_OPEN = 1,
	RELAY_FAILED = 2,
};

// Waited out here because a network game is set up in one call.
double const CONNECT_TIMEOUT_MS = 10000.0;

}


// Arrivals are queued because the page's event loop can run while the engine
// is suspended inside a fetch, and calling in there would reenter it.
EM_JS(int, Relay_Open, (char const * url), {
	try {
		var state = Module.OpenTSRelay = {
			socket: null,
			id: 0,
			state: 0,
			queue: [],
			reason: ""
		};

		var socket = new WebSocket(UTF8ToString(url));
		socket.binaryType = "arraybuffer";
		state.socket = socket;

		socket.onmessage = function (event) {
			if (typeof event.data === "string") {
				// The relay's only text message names this client.
				try {
					var greeting = JSON.parse(event.data);
					if (greeting && typeof greeting.id === "number") {
						state.id = greeting.id;
						state.state = 1;
					}
				} catch (error) {
					state.reason = "the relay's greeting could not be read";
					state.state = 2;
				}
				return;
			}

			state.queue.push(new Uint8Array(event.data));
		};

		socket.onerror = function () {
			if (state.state !== 1) {
				state.reason = state.reason || "the relay could not be reached";
				state.state = 2;
			}
		};

		socket.onclose = function (event) {
			// A close before the greeting is a refusal, and its reason is all
			// the page learns, since a browser cannot read a failed upgrade.
			if (state.state !== 1) {
				state.reason = event.reason || state.reason || "the relay closed the connection";
			}
			state.state = 2;
		};

		return 1;
	} catch (error) {
		return 0;
	}
});


EM_JS(int, Relay_State, (void), {
	var state = Module.OpenTSRelay;
	return state ? state.state : 2;
});


EM_JS(int, Relay_Id, (void), {
	var state = Module.OpenTSRelay;
	return state ? state.id : 0;
});


EM_JS(int, Relay_Pending, (void), {
	var state = Module.OpenTSRelay;
	return state ? state.queue.length : 0;
});


EM_JS(void, Relay_Reason, (char * buffer, int capacity), {
	var state = Module.OpenTSRelay;
	stringToUTF8(state && state.reason ? state.reason : "", buffer, capacity);
});


EM_JS(int, Relay_Receive_Message, (char * buffer, int capacity), {
	var state = Module.OpenTSRelay;
	if (!state || state.queue.length === 0) return -1;

	var message = state.queue.shift();
	if (message.length > capacity) return -1;

	HEAPU8.set(message, buffer);
	return message.length;
});


EM_JS(int, Relay_Send_Message, (char const * buffer, int length), {
	var state = Module.OpenTSRelay;
	if (!state || !state.socket || state.socket.readyState !== 1) return -1;

	state.socket.send(HEAPU8.slice(buffer, buffer + length));
	return length;
});


EM_JS(void, Relay_Shutdown, (void), {
	var state = Module.OpenTSRelay;
	if (!state) return;

	if (state.socket) {
		state.socket.onmessage = null;
		state.socket.onerror = null;
		state.socket.onclose = null;
		try { state.socket.close(); } catch (error) {}
	}

	Module.OpenTSRelay = null;
});


EM_JS(void, Relay_Read_Query, (char const * name, char * buffer, int capacity), {
	var value = "";
	try {
		value = new URL(location.href).searchParams.get(UTF8ToString(name)) || "";
	} catch (error) {
		value = "";
	}
	stringToUTF8(value, buffer, capacity);
});


// relay.json names the deployment's relay; "" means it has none.
EM_JS(void, Relay_Default_Url, (char * buffer, int capacity), {
	var url = "";
	try {
		var request = new XMLHttpRequest();
		request.open("GET", new URL("relay.json", document.baseURI).href, false);
		request.send(null);
		if (request.status === 200) url = JSON.parse(request.responseText)["url"] || "";
	} catch (error) {
		url = "";
	}
	stringToUTF8(url, buffer, capacity);
});


// The module's file name identifies the build because a deployment hashes it;
// an unhashed build directory gives every client the same answer.
EM_JS(void, Relay_Build_Identity, (char * buffer, int capacity), {
	var name = "";
	try {
		if (typeof Module.wasmBinaryFile === "string") name = Module.wasmBinaryFile;

		if (!name) {
			var scripts = document.getElementsByTagName("script");
			for (var index = 0; index < scripts.length; index++) {
				var source = scripts[index].src || "";
				var leaf = source.split("?")[0].split("/").pop();
				if (leaf.indexOf("Game") === 0 && leaf.slice(-3) === ".js") name = leaf;
			}
		}

		name = name.split("?")[0].split("/").pop();
	} catch (error) {
		name = "";
	}
	stringToUTF8(name || "unknown", buffer, capacity);
});


char const * Relay_Configured_Url(void)
{
	static char url[256];
	static bool read = false;

	if (!read) {
		read = true;
		Relay_Read_Query("relay", url, sizeof(url));

		if (url[0] == 0) Relay_Default_Url(url, sizeof(url));
	}

	return(url);
}


char const * Relay_Configured_Room(void)
{
	static char room[128];
	static bool read = false;

	if (!read) {
		read = true;
		Relay_Read_Query("room", room, sizeof(room));

		// One relay is one LAN where nothing says otherwise.
		if (room[0] == 0) std::strcpy(room, "lan");
	}

	return(room);
}


RelaySocketClass::~RelaySocketClass(void)
{
	RelaySocketClass::Close();
}


void RelaySocketClass::Set_Relay(char const * url, char const * room)
{
	Url = (url != nullptr) ? url : "";
	Room = (room != nullptr) ? room : "";
}


/// <summary>
/// Joins the configured room, waiting until the relay seats this client or
/// gives up on it. The port is ignored, since a room is the whole address.
/// </summary>
/// <returns>Whether the relay seated this client, which is when Local_Id
/// answers with the id the tunnel header must name it by. A second call
/// repeats that answer rather than seating this client again, which would
/// leave the tunnel header naming an id the relay no longer knows.</returns>
bool RelaySocketClass::Open(unsigned short)
{
	if (Connected) return(true);
	if (Attempted) return(false);

	Close();
	Attempted = true;

	if (Url.empty() || Room.empty()) {
		DebugString("Relay: no relay or no room was named; a network game cannot be started\n");
		return(false);
	}

	// The build name lets the relay hold a room to one build; two builds in a
	// lockstep match desync without looking like a version mismatch.
	std::string address = Url;
	address += (Url.find('?') == std::string::npos) ? '?' : '&';
	address += "room=";
	address += Room;
	address += "&build=";

	char build[128];
	Relay_Build_Identity(build, sizeof(build));
	address += build;

	DebugString("Relay: connecting to %s\n", address.c_str());

	if (!Relay_Open(address.c_str())) {
		DebugString("Relay: the page would not open a socket\n");
		return(false);
	}

	double const started = emscripten_get_now();

	while (Relay_State() == RELAY_CONNECTING) {
		if (emscripten_get_now() - started > CONNECT_TIMEOUT_MS) {
			DebugString("Relay: the relay did not answer\n");
			Relay_Shutdown();
			return(false);
		}

		Browser_Yield();
	}

	if (Relay_State() != RELAY_OPEN) {
		char reason[192];
		Relay_Reason(reason, sizeof(reason));
		DebugString("Relay: refused -- %s\n", reason[0] != 0 ? reason : "no reason given");
		Relay_Shutdown();
		return(false);
	}

	// Held in network order because the tunnel header is written in it.
	LocalId = Socket_Network_Port((unsigned short)Relay_Id());
	Connected = true;

	DebugString("Relay: seated in room %s as %d\n", Room.c_str(), Relay_Id());

	return(true);
}


void RelaySocketClass::Close(void)
{
	if (Connected || Relay_State() != RELAY_FAILED) {
		Relay_Shutdown();
	}

	Connected = false;
	Attempted = false;
	LocalId = 0;
}


/// <summary>
/// Hands the datagram to the relay, which routes it by the tunnel header the
/// transport has already written. The address is unused.
/// </summary>
TransferResult RelaySocketClass::Send_To(void const * buffer, int length, IPXAddressClass const &)
{
	if (!Connected) return(TransferResult{SocketError::OTHER, 0});

	int const sent = Relay_Send_Message(static_cast<char const *>(buffer), length);
	if (sent < 0) return(TransferResult{SocketError::OTHER, 0});

	return(TransferResult{SocketError::NONE, sent});
}


/// <summary>
/// Takes the next datagram the page has queued, or answers WOULD_BLOCK when
/// there is none.
/// </summary>
/// <returns>The payload received. The sender is left blank, since only the
/// tunnel header names it.</returns>
TransferResult RelaySocketClass::Receive_From(void * buffer, int length, IPXAddressClass & from)
{
	if (!Connected) return(TransferResult{SocketError::OTHER, 0});

	if (Relay_Pending() == 0) return(TransferResult{SocketError::WOULD_BLOCK, 0});

	// A message the buffer cannot hold has already left the queue.
	int const received = Relay_Receive_Message(static_cast<char *>(buffer), length);
	if (received < 0) return(TransferResult{SocketError::MESSAGE_SIZE, 0});

	from.Set_Address(0, 0);

	return(TransferResult{SocketError::NONE, received});
}

#endif	// __EMSCRIPTEN__
