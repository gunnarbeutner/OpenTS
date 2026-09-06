/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                     $Archive:: /Sun/WSPUDP.h                                               $*
 *                                                                                             *
 *                      $Author:: Joe_b                                                       $*
 *                                                                                             *
 *                     $Modtime:: 8/05/97 6:45p                                               $*
 *                                                                                             *
 *                    $Revision:: 3                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "wsproto.h"

#include <nspapi.h>


/*
**	Class to allow access to UDP specific portions of the Winsock interface.
**
*/
class UDPInterfaceClass : public WinsockInterfaceClass {
		typedef WinsockInterfaceClass BASECLASS;

	public:

		UDPInterfaceClass (void);
		virtual ~UDPInterfaceClass(void) override;

		virtual bool Open_Socket ( SOCKET socketnum ) override;
		virtual void Set_Broadcast_Address ( const IPXAddressClass &address ) override;
		virtual void Clear_Broadcast_Addresses(void) override;
		virtual void Broadcast (void *buffer, int buffer_len) override;

		/*
		 * Ports are in host order. Left unset, both follow WestwoodOnline_PortNumber; a
		 * local port of zero binds whatever port Winsock hands out.
		 */
		void Set_Local_Port(unsigned short port);
		void Set_Destination_Port(unsigned short port);

		void Enable_Broadcast(bool enable);

		/*
		 * Route everything through a CnCNet tunnel server, which forwards between players
		 * that cannot reach each other directly. Every datagram gains a routing header
		 * naming the sender and the recipient by their tunnel ID; a player is addressed by
		 * that ID in place of a real endpoint. All arguments are in network order.
		 */
		void Configure_Tunnel(unsigned short local_id, unsigned long tunnel_ip, unsigned short tunnel_port);

		/*
		 * The recipient id that means every player at once. A CnCNet tunnel
		 * has none and leaves this at zero; a relayed broadcast arrives naming
		 * this id rather than the receiver.
		 */
		void Set_Tunnel_Broadcast(unsigned short id);

		virtual ProtocolEnum Get_Protocol (void) override {
			return(PROTOCOL_UDP);
		};

		virtual int Get_Num_Local_Addresses(void) override {
			return(LocalAddresses.Count());
		};

		virtual unsigned char *Get_Local_Address(int index) override {
			return(LocalAddresses[index]);
		};

	protected:

		virtual void Receive_Pending(void) override;
		virtual void Send_Pending(void) override;

		/*
		 * A carrier moves the bytes and answers as sendto and recvfrom do: the number
		 * moved, or SOCKET_ERROR. The destination names the tunnel server when a tunnel
		 * is in use; a carrier with one place to send may ignore it.
		 */
		virtual int Carrier_Send(const char *buffer, int buffer_len, const sockaddr_in *destination);
		virtual int Carrier_Receive(char *buffer, int buffer_len, sockaddr_in *source);

	private:

		void Register_Local_Addresses();

		/*
		 * Wrappers around the carrier that add and strip the tunnel routing
		 * header. They pass the datagram through untouched when no tunnel is
		 * configured. Receive_From answers RECEIVE_IGNORED for a datagram the
		 * carrier delivered that was not for this client, which a caller
		 * draining the carrier passes over where SOCKET_ERROR stops it.
		 */
		static constexpr int RECEIVE_IGNORED = -2;

		int Send_To(const char *buffer, int buffer_len, sockaddr_in *destination);
		int Receive_From(char *buffer, int buffer_len, sockaddr_in *source);

		/*
		**	Addresses to send to when broadcasting a packet.
		*/
		DynamicVectorClass <IPXAddressClass *> BroadcastAddresses;

		/*
		**	List of local addresses.
		*/
		DynamicVectorClass <unsigned char *> LocalAddresses;

		unsigned short LocalPort;
		unsigned short DestinationPort;
		bool LocalPortSet;
		bool DestinationPortSet;
		bool UseBroadcast;

		// A tunnel is in use when TunnelPort is non-zero.
		unsigned short TunnelID;
		unsigned short TunnelBroadcast;
		unsigned long TunnelIP;
		unsigned short TunnelPort;
};
