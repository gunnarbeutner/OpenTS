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

#include "wspudp.h"

#include <string>


// Reaches every other member of the room. It reads the same in either byte
// order, so an endianness mistake cannot turn it into a unicast.
#define RELAY_BROADCAST_ID 0xFFFF


class RelayInterfaceClass : public UDPInterfaceClass
{
		typedef UDPInterfaceClass BASECLASS;

	public:

		RelayInterfaceClass(void);
		virtual ~RelayInterfaceClass(void) override;

		// Names the relay, "ws://host:8766" or "wss://host/relay", and the
		// room to join; call before Open_Socket.
		void Set_Relay(char const * url, char const * room);

		// Waits, yielding to the page, until the relay seats this client, and
		// returns false if it does not. The socket argument is ignored.
		virtual bool Open_Socket(SOCKET socketnum) override;

		virtual void Close_Socket(void) override;

		// There is no socket to prepare; the queue the page fills is polled.
		virtual bool Start_Listening(void) override;

		virtual bool Get_Host_Name(char * name, int len) override;

		virtual int Get_Num_Local_Addresses(void) override {
			return(0);
		};

		// The id the tunnel header names this client by, in network order, or
		// zero before the relay has given one.
		unsigned short Local_Id(void) const {return(LocalId);}

	protected:

		virtual int Carrier_Send(const char * buffer, int buffer_len, const sockaddr_in * destination) override;
		virtual int Carrier_Receive(char * buffer, int buffer_len, sockaddr_in * source) override;

	private:

		std::string Url;
		std::string Room;
		unsigned short LocalId;
		bool Connected;
};


// The "?relay=" and "?room=" values from the query string, or empty strings
// when the page named none.
char const * Relay_Configured_Url(void);
char const * Relay_Configured_Room(void);

#endif	// __EMSCRIPTEN__
