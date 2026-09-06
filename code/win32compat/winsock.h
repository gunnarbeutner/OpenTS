/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Stands in for <winsock.h> on the WebAssembly target over Emscripten's BSD
// sockets. Emscripten's sockets are WebSocket-backed, so neither IPX nor UDP
// works here and the engine's network play does not either.

#pragma once

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include "windows.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


typedef int SOCKET;
typedef struct sockaddr SOCKADDR;
typedef struct sockaddr * PSOCKADDR;
typedef struct sockaddr * LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr_in * PSOCKADDR_IN;
typedef struct sockaddr_in * LPSOCKADDR_IN;
typedef struct in_addr IN_ADDR;
typedef struct in_addr * LPIN_ADDR;
typedef struct hostent HOSTENT;
typedef struct hostent * LPHOSTENT;
typedef struct servent SERVENT;
typedef struct servent * LPSERVENT;
typedef struct protoent PROTOENT;
typedef struct protoent * LPPROTOENT;
typedef fd_set FD_SET;
typedef fd_set * LPFD_SET;
typedef struct timeval TIMEVAL;
typedef struct timeval * LPTIMEVAL;

#define INVALID_SOCKET	((SOCKET)(-1))
#define SOCKET_ERROR	(-1)

#ifndef WSADESCRIPTION_LEN
#define WSADESCRIPTION_LEN	256
#define WSASYS_STATUS_LEN	128
#endif

typedef struct WSAData {
	WORD wVersion;
	WORD wHighVersion;
	char szDescription[WSADESCRIPTION_LEN + 1];
	char szSystemStatus[WSASYS_STATUS_LEN + 1];
	unsigned short iMaxSockets;
	unsigned short iMaxUdpDg;
	char * lpVendorInfo;
} WSADATA, * LPWSADATA;

#define MAKEWORD_WSA(low, high)	MAKEWORD(low, high)

#define WSABASEERR			10000
#define WSAEINTR			(WSABASEERR + 4)
#define WSAEBADF			(WSABASEERR + 9)
#define WSAEACCES			(WSABASEERR + 13)
#define WSAEFAULT			(WSABASEERR + 14)
#define WSAEINVAL			(WSABASEERR + 22)
#define WSAEMFILE			(WSABASEERR + 24)
#define WSAEWOULDBLOCK		(WSABASEERR + 35)
#define WSAEINPROGRESS		(WSABASEERR + 36)
#define WSAEALREADY			(WSABASEERR + 37)
#define WSAENOTSOCK			(WSABASEERR + 38)
#define WSAEDESTADDRREQ		(WSABASEERR + 39)
#define WSAEMSGSIZE			(WSABASEERR + 40)
#define WSAEADDRINUSE		(WSABASEERR + 48)
#define WSAEADDRNOTAVAIL	(WSABASEERR + 49)
#define WSAENETDOWN			(WSABASEERR + 50)
#define WSAENETUNREACH		(WSABASEERR + 51)
#define WSAECONNABORTED		(WSABASEERR + 53)
#define WSAECONNRESET		(WSABASEERR + 54)
#define WSAENOBUFS			(WSABASEERR + 55)
#define WSAEISCONN			(WSABASEERR + 56)
#define WSAENOTCONN			(WSABASEERR + 57)
#define WSAETIMEDOUT		(WSABASEERR + 60)
#define WSAECONNREFUSED		(WSABASEERR + 61)
#define WSAEHOSTUNREACH		(WSABASEERR + 65)
#define WSASYSNOTREADY		(WSABASEERR + 91)
#define WSAVERNOTSUPPORTED	(WSABASEERR + 92)
#define WSANOTINITIALISED	(WSABASEERR + 93)
#define WSAHOST_NOT_FOUND	(WSABASEERR + 1001)

#define closesocket(s)	::close(s)

typedef struct linger LINGER;

// Winsock passes lengths as int where POSIX uses socklen_t.
inline int getsockopt(SOCKET socket, int level, int name, void * value, int * length)
{
	socklen_t size = (socklen_t)*length;
	int result = ::getsockopt(socket, level, name, value, &size);

	*length = (int)size;
	return(result);
}


inline int recvfrom(SOCKET socket, void * buffer, int length, int flags, struct sockaddr * from, int * fromlength)
{
	socklen_t size = (socklen_t)*fromlength;
	int result = (int)::recvfrom(socket, buffer, (size_t)length, flags, from, &size);

	*fromlength = (int)size;
	return(result);
}


inline int accept(SOCKET socket, struct sockaddr * address, int * length)
{
	socklen_t size = (socklen_t)*length;
	int result = ::accept(socket, address, &size);

	*length = (int)size;
	return(result);
}


inline int getsockname(SOCKET socket, struct sockaddr * address, int * length)
{
	socklen_t size = (socklen_t)*length;
	int result = ::getsockname(socket, address, &size);

	*length = (int)size;
	return(result);
}

int WSAStartup(WORD version, LPWSADATA data);
int WSACleanup(void);
int WSAGetLastError(void);
void WSASetLastError(int error);
int WSAAsyncSelect(SOCKET socket, HWND window, unsigned int message, long events);

// The one socket option the engine sets, named as Winsock names it. Only FIONBIO is
// answered; any other command reports itself and fails.
#define FIONBIO 0x8004667E

int ioctlsocket(SOCKET socket, long command, unsigned long * argument);

#endif	// OPENTS_WIN32_SUBSTITUTE
