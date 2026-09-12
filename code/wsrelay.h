/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// A page can neither broadcast nor open a datagram socket, so a network game
// reaches a relay that forwards by the recipient id in the tunnel header.
// docs/RELAY.md describes it.

#pragma once

#if defined(__EMSCRIPTEN__)

#include "netsocket.h"

#include <string>


// Reaches every other member of the room. It reads the same in either byte
// order, so an endianness mistake cannot turn it into a unicast.
#define RELAY_BROADCAST_ID 0xFFFF


class RelaySocketClass : public SocketClass
{
	public:

		~RelaySocketClass(void) override;

		// Names the relay, "ws://host:8766" or "wss://host/relay", and the
		// room to join; call before Open.
		void Set_Relay(char const * url, char const * room);

		// Waits, yielding to the page, until the relay seats this client, and
		// returns false if it does not. The port is ignored. One socket is
		// seated once: an Open that follows a Close is seated under a new id,
		// which the tunnel header has to be told about again.
		bool Open(unsigned short port) override;

		void Close(void) override;
		bool Is_Open(void) const override {return(Connected);}

		// A WebSocket has no such option; the relay is what carries a
		// broadcast to the room.
		bool Set_Broadcast(bool) override {return(true);}

		bool Set_Buffer_Sizes(int, int) override {return(true);}

		// Nothing is held back from a transfer to fail the next one.
		void Clear_Error(void) override {}

		TransferResult Send_To(void const * buffer, int length, IPXAddressClass const & to) override;
		TransferResult Receive_From(void * buffer, int length, IPXAddressClass & from) override;

		// A page answers on no address of its own.
		bool Local_Interfaces(std::vector<InterfaceType> &) override {return(false);}

		// The id the tunnel header names this client by, in network order, or
		// zero before the relay has given one.
		unsigned short Local_Id(void) const {return(LocalId);}

	private:

		std::string Url;
		std::string Room;
		unsigned short LocalId = 0;
		bool Connected = false;
		bool Attempted = false;
};


// The "?relay=" and "?room=" values from the query string, or empty strings
// when the page named none.
char const * Relay_Configured_Url(void);
char const * Relay_Configured_Room(void);

#endif	// __EMSCRIPTEN__
