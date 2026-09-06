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
#include "ipxaddr.h"
#include "dbgprint.h"
#include "wsproto.h"

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


RelayInterfaceClass::RelayInterfaceClass(void) :
	LocalId(0),
	Connected(false)
{
}


RelayInterfaceClass::~RelayInterfaceClass(void)
{
	RelayInterfaceClass::Close_Socket();
}


void RelayInterfaceClass::Set_Relay(char const * url, char const * room)
{
	Url = (url != nullptr) ? url : "";
	Room = (room != nullptr) ? room : "";
}


bool RelayInterfaceClass::Open_Socket(SOCKET)
{
	Close_Socket();

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
	LocalId = htons((unsigned short)Relay_Id());
	Connected = true;

	// Tunnel framing carries the pair of ids the relay routes on. The server
	// address is unused, but its port must be nonzero to engage the framing.
	Configure_Tunnel(LocalId, 0, htons(1));

	// A relayed broadcast arrives naming the broadcast id, not the receiver,
	// so the interface must accept it as its own.
	Set_Tunnel_Broadcast(RELAY_BROADCAST_ID);

	// One address the relay explodes to the room.
	Clear_Broadcast_Addresses();
	Set_Broadcast_Address(IPXAddressClass(INADDR_BROADCAST, RELAY_BROADCAST_ID));

	DebugString("Relay: seated in room %s as %d\n", Room.c_str(), Relay_Id());

	return(true);
}


void RelayInterfaceClass::Close_Socket(void)
{
	if (Connected || Relay_State() != RELAY_FAILED) {
		Relay_Shutdown();
	}

	Connected = false;
	LocalId = 0;
}


bool RelayInterfaceClass::Start_Listening(void)
{
	Listening = Connected;
	return(Connected);
}


bool RelayInterfaceClass::Get_Host_Name(char * name, int len)
{
	if (name == nullptr || len <= 0) return(false);

	std::snprintf(name, (std::size_t)len, "relay:%s", Room.c_str());

	return(true);
}


int RelayInterfaceClass::Carrier_Send(const char * buffer, int buffer_len, const sockaddr_in *)
{
	// The relay routes on the header, so the destination address is unused.
	int const sent = Connected ? Relay_Send_Message(buffer, buffer_len) : -1;
	if (sent < 0) {
		WSASetLastError(WSAENOTCONN);
		return(SOCKET_ERROR);
	}

	return(sent);
}


int RelayInterfaceClass::Carrier_Receive(char * buffer, int buffer_len, sockaddr_in * source)
{
	if (!Connected) {
		WSASetLastError(WSAENOTCONN);
		return(SOCKET_ERROR);
	}

	if (Relay_Pending() == 0) {
		WSASetLastError(WSAEWOULDBLOCK);
		return(SOCKET_ERROR);
	}

	// A message the buffer cannot hold has already left the queue.
	int const length = Relay_Receive_Message(buffer, buffer_len);
	if (length < 0) {
		WSASetLastError(WSAEMSGSIZE);
		return(SOCKET_ERROR);
	}

	if (source != nullptr) {
		// Cleared so a caller reading it before the tunnel header's sender id
		// overwrites it sees nothing rather than the last sender.
		std::memset(source, 0, sizeof(*source));
		source->sin_family = AF_INET;
	}

	return(length);
}

#endif	// __EMSCRIPTEN__
