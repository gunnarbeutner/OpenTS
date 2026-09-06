"""A WebSocket relay that stands in for a LAN broadcast domain.

The engine finds and plays network games by broadcasting onto a local network. A page has
no local network, so this takes the place of one: every client that joins the same room is
reachable from every other, and a frame addressed to the broadcast id reaches all of them.
Nothing above the transport knows the difference, so the game's own Network dialog works
unchanged.

Frames are the engine's own tunnel framing, which code/wspudp.cpp already writes and reads:

    byte 0-1   sender's id
    byte 2-3   recipient's id, or 0xFFFF for everyone else in the room
    byte 4-N   the engine's packet, untouched

Ids are two bytes in network order, which is what the engine puts on the wire because it
holds them htons()-encoded. The relay never looks past byte four.

A client connects with the room it wants and the build it is running:

    ws://host:8766/?room=<key>&build=<module hash>

and is answered with a text frame naming the id it was given:

    {"id": 4242, "room": "<key>", "members": 2}

Text frames are the relay's own; binary frames are the game's. A client sends nothing but
binary after that, and receives nothing but binary.

Both ends of a lockstep match have to be running the same code, and a page caches its
module by content hash forever, so two players can easily be on different builds without
either noticing. A room therefore takes the build of whoever opens it and refuses anyone
else, which turns a desync that would look like a network fault into a message. A room
lasts exactly as long as somebody is in it: there is nobody for an empty one to desync,
so the next player to open it sets the build afresh.

Run it with no arguments to listen on 8766:

    python3 tools/relay/relay.py --port 8766 --verbose
"""

import argparse
import asyncio
import base64
import hashlib
import json
import random
import struct
import sys
import time


# The magic the WebSocket handshake appends to the client's key before hashing it.
WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# code/wspudp.cpp: TUNNEL_HEADER_SIZE plus WS_RECEIVE_BUFFER_LEN. A frame longer than the
# engine can ever send is not the engine's.
HEADER_SIZE = 4
MAX_PAYLOAD = 1024
MAX_FRAME = HEADER_SIZE + MAX_PAYLOAD

# Reaches every other member of the room. Chosen for being the same two bytes whichever way
# round they are read, so it cannot be got wrong by an endianness mistake at either end.
BROADCAST_ID = 0xFFFF

# A room is a broadcast domain, not a game: everyone browsing the lobby is in it, and only
# some of them go on to play. The engine enforces its own eight seats per game, so this is
# here to stop one room becoming a resource sink rather than to model a rule of the game.
MAX_MEMBERS = 256

# An id of zero means "no id" to the engine, and the broadcast id is spoken for.
FIRST_ID = 1
LAST_ID = BROADCAST_ID - 1

# A client that has said nothing for this long is dropped; a ping goes out at half of it.
IDLE_SECONDS = 60

OPCODE_CONTINUATION = 0x0
OPCODE_TEXT = 0x1
OPCODE_BINARY = 0x2
OPCODE_CLOSE = 0x8
OPCODE_PING = 0x9
OPCODE_PONG = 0xA

CLOSE_NORMAL = 1000
CLOSE_PROTOCOL_ERROR = 1002
CLOSE_TOO_LARGE = 1009
CLOSE_POLICY = 1008


class ProtocolError(Exception):
    """A client did something the relay will not carry on from."""

    def __init__(self, message, code=CLOSE_PROTOCOL_ERROR):
        super().__init__(message)
        self.code = code


def _log(verbose, message):
    if verbose:
        sys.stderr.write("%s %s\n" % (time.strftime("%H:%M:%S"), message))
        sys.stderr.flush()


def _parse_query(target):
    """Reads the query string of a request target into a dict, without urllib's parser.

    Only two keys are ever read and both are opaque, so this decodes percent escapes and
    leaves everything else alone rather than pulling in a general parser.
    """
    query = target.split("?", 1)[1] if "?" in target else ""
    fields = {}

    for pair in query.split("&"):
        if not pair:
            continue
        name, _, value = pair.partition("=")
        fields[_unquote(name)] = _unquote(value)

    return fields


def _unquote(text):
    out = []
    index = 0

    while index < len(text):
        character = text[index]
        if character == "+":
            out.append(" ")
            index += 1
        elif character == "%" and index + 2 < len(text) + 1:
            try:
                out.append(chr(int(text[index + 1:index + 3], 16)))
                index += 3
            except ValueError:
                out.append(character)
                index += 1
        else:
            out.append(character)
            index += 1

    return "".join(out)


class WebSocket:
    """One connection, framed. Speaks only what this relay needs of RFC 6455."""

    def __init__(self, reader, writer):
        self.Reader = reader
        self.Writer = writer
        self.Target = ""
        self.Closed = False

    @property
    def peer(self):
        try:
            address = self.Writer.get_extra_info("peername")
        except Exception:
            return "?"
        if not address:
            return "?"
        return "%s:%s" % (address[0], address[1])

    async def accept(self):
        """Completes the opening handshake, or raises ProtocolError."""
        request = await self._read_headers()
        line, headers = request

        parts = line.split(" ")
        if len(parts) < 2 or parts[0].upper() != "GET":
            raise ProtocolError("not a GET")
        self.Target = parts[1]

        if headers.get("upgrade", "").lower() != "websocket":
            raise ProtocolError("not an upgrade")

        key = headers.get("sec-websocket-key")
        if not key:
            raise ProtocolError("no key")

        digest = hashlib.sha1((key + WEBSOCKET_GUID).encode("ascii")).digest()
        accept = base64.b64encode(digest).decode("ascii")

        self.Writer.write(
            (
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: %s\r\n\r\n" % accept
            ).encode("ascii")
        )
        await self.Writer.drain()

    async def _read_headers(self):
        # A handshake this size is not a real one, and reading without a bound is how a
        # socket that never sends a blank line becomes unbounded memory.
        raw = await asyncio.wait_for(self.Reader.readuntil(b"\r\n\r\n"), timeout=10)
        if len(raw) > 8192:
            raise ProtocolError("handshake too large")

        text = raw.decode("latin-1")
        lines = text.split("\r\n")
        headers = {}

        for line in lines[1:]:
            name, _, value = line.partition(":")
            if name:
                headers[name.strip().lower()] = value.strip()

        return lines[0], headers

    async def receive(self):
        """Returns (opcode, payload) for the next whole message, or None at end of stream.

        Fragmented messages are reassembled; control frames are answered here and never
        returned, apart from a close, which is reported so the caller can stop.
        """
        opcode = None
        body = bytearray()

        while True:
            frame = await self._read_frame()
            if frame is None:
                return None

            final, code, payload = frame

            if code in (OPCODE_PING, OPCODE_PONG, OPCODE_CLOSE):
                if code == OPCODE_PING:
                    await self._write_frame(OPCODE_PONG, payload)
                    continue
                if code == OPCODE_PONG:
                    continue
                return (OPCODE_CLOSE, bytes(payload))

            if code == OPCODE_CONTINUATION:
                if opcode is None:
                    raise ProtocolError("continuation without a start")
            else:
                if opcode is not None:
                    raise ProtocolError("a new message inside one already started")
                opcode = code

            body.extend(payload)
            if len(body) > MAX_FRAME:
                raise ProtocolError("message longer than the engine can send", CLOSE_TOO_LARGE)

            if final:
                return (opcode, bytes(body))

    async def _read_frame(self):
        try:
            head = await self.Reader.readexactly(2)
        except asyncio.IncompleteReadError:
            return None

        final = bool(head[0] & 0x80)
        if head[0] & 0x70:
            raise ProtocolError("reserved bits set")

        code = head[0] & 0x0F
        masked = bool(head[1] & 0x80)
        length = head[1] & 0x7F

        if not masked:
            # Every frame a client sends must be masked. An unmasked one is either a broken
            # client or something that is not a client at all.
            raise ProtocolError("client frame is not masked")

        if length == 126:
            length = struct.unpack("!H", await self.Reader.readexactly(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", await self.Reader.readexactly(8))[0]

        if length > MAX_FRAME:
            raise ProtocolError("frame longer than the engine can send", CLOSE_TOO_LARGE)

        mask = await self.Reader.readexactly(4)
        payload = bytearray(await self.Reader.readexactly(length))

        for index in range(length):
            payload[index] ^= mask[index & 3]

        return (final, code, payload)

    async def _write_frame(self, opcode, payload):
        if self.Closed:
            return

        length = len(payload)
        header = bytearray([0x80 | opcode])

        # Server frames are never masked.
        if length < 126:
            header.append(length)
        elif length < 65536:
            header.append(126)
            header.extend(struct.pack("!H", length))
        else:
            header.append(127)
            header.extend(struct.pack("!Q", length))

        try:
            self.Writer.write(bytes(header) + bytes(payload))
            await self.Writer.drain()
        except (ConnectionError, RuntimeError):
            self.Closed = True

    async def send_binary(self, payload):
        await self._write_frame(OPCODE_BINARY, payload)

    async def send_text(self, text):
        await self._write_frame(OPCODE_TEXT, text.encode("utf-8"))

    async def ping(self):
        await self._write_frame(OPCODE_PING, b"")

    async def close(self, code=CLOSE_NORMAL, reason=""):
        if self.Closed:
            return
        payload = struct.pack("!H", code) + reason.encode("utf-8")[:123]
        await self._write_frame(OPCODE_CLOSE, payload)
        self.Closed = True
        try:
            self.Writer.close()
        except Exception:
            pass


class Member:
    def __init__(self, identifier, socket):
        self.Id = identifier
        self.Socket = socket


class Room:
    """The clients that can reach each other, and the build they all have to be running.

    A room exists only while somebody is in it.
    """

    def __init__(self, key, build):
        self.Key = key
        self.Build = build
        self.Members = {}

    def take_id(self):
        """Picks an id nobody in the room holds, or None when the room is full."""
        if len(self.Members) >= MAX_MEMBERS:
            return None

        # Drawn rather than counted up, so an id is not reused the moment its holder leaves
        # and a late packet cannot be delivered to whoever took their place.
        for _attempt in range(64):
            candidate = random.randint(FIRST_ID, LAST_ID)
            if candidate not in self.Members:
                return candidate

        for candidate in range(FIRST_ID, LAST_ID + 1):
            if candidate not in self.Members:
                return candidate

        return None


class Relay:
    def __init__(self, verbose=False, trace=False):
        self.Rooms = {}
        self.Verbose = verbose or trace
        self.Trace = trace
        self.Forwarded = 0
        self.Dropped = 0

    def _room(self, key, build):
        room = self.Rooms.get(key)

        if room is None:
            room = Room(key, build)
            self.Rooms[key] = room

        return room

    async def serve_client(self, reader, writer):
        socket = WebSocket(reader, writer)
        room = None
        member = None

        try:
            await socket.accept()

            fields = _parse_query(socket.Target)
            key = fields.get("room", "").strip()
            build = fields.get("build", "").strip()

            if not key or len(key) > 128:
                raise ProtocolError("no room named", CLOSE_POLICY)
            if not build or len(build) > 128:
                raise ProtocolError("no build named", CLOSE_POLICY)

            room = self._room(key, build)

            if room.Build != build:
                # Two builds in one lockstep match desync on the first frame that differs,
                # and it presents as a network fault. Refusing here says what it really is.
                raise ProtocolError(
                    "room is playing build %s, this client has %s" % (room.Build, build),
                    CLOSE_POLICY,
                )

            identifier = room.take_id()
            if identifier is None:
                raise ProtocolError("room is full", CLOSE_POLICY)

            member = Member(identifier, socket)
            room.Members[identifier] = member

            await socket.send_text(json.dumps({
                "id": identifier,
                "room": key,
                "members": len(room.Members),
            }))

            _log(self.Verbose, "join  %s room=%s id=%d (%d in room)"
                 % (socket.peer, key, identifier, len(room.Members)))

            await self._pump(room, member, socket)

        except ProtocolError as error:
            _log(self.Verbose, "refuse %s: %s" % (socket.peer, error))
            await socket.close(error.code, str(error))
        except (asyncio.IncompleteReadError, asyncio.TimeoutError, ConnectionError):
            pass
        except asyncio.LimitOverrunError:
            await socket.close(CLOSE_TOO_LARGE, "handshake too large")
        finally:
            if room is not None:
                if member is not None:
                    room.Members.pop(member.Id, None)
                    _log(self.Verbose, "leave %s room=%s id=%d (%d left)"
                         % (socket.peer, room.Key, member.Id, len(room.Members)))

                # A room is its members. One with none of them is not a room that has been
                # emptied, it is one that does not exist, so nothing of it is kept -- the
                # build included, which is what stops a deployment locking the next player
                # out of the room the last one just left.
                if not room.Members:
                    self.Rooms.pop(room.Key, None)
            await socket.close()

    async def _pump(self, room, member, socket):
        while True:
            try:
                message = await asyncio.wait_for(socket.receive(), timeout=IDLE_SECONDS / 2)
            except asyncio.TimeoutError:
                await socket.ping()
                continue

            if message is None:
                return

            opcode, payload = message

            if opcode == OPCODE_CLOSE:
                return

            if opcode != OPCODE_BINARY:
                # The relay's own messages go one way. A client that sends text is not
                # speaking this protocol.
                raise ProtocolError("only binary frames carry a game")

            if len(payload) < HEADER_SIZE:
                self._drop(member, "a frame too short to carry a header")
                continue

            sender, recipient = struct.unpack("!HH", payload[:HEADER_SIZE])

            if sender != member.Id:
                # A client may only speak as itself. Dropping rather than rewriting keeps a
                # carrier bug visible instead of quietly working.
                self._drop(member, "a frame sent as %d" % sender)
                continue

            await self._forward(room, member, recipient, payload)

    def _drop(self, member, why):
        self.Dropped += 1
        _log(self.Verbose, "drop  id=%d: %s" % (member.Id, why))

    async def _forward(self, room, member, recipient, payload):
        if recipient == BROADCAST_ID:
            reached = 0
            for other in list(room.Members.values()):
                if other.Id != member.Id:
                    await other.Socket.send_binary(payload)
                    self.Forwarded += 1
                    reached += 1

            _log(self.Trace, "cast  %d -> %d others, %d bytes"
                 % (member.Id, reached, len(payload)))
            return

        target = room.Members.get(recipient)
        if target is None:
            self._drop(member, "a frame for %d, who is not in the room" % recipient)
            return

        await target.Socket.send_binary(payload)
        self.Forwarded += 1
        _log(self.Trace, "send  %d -> %d, %d bytes" % (member.Id, recipient, len(payload)))


async def _serve(host, port, verbose, trace):
    relay = Relay(verbose=verbose, trace=trace)
    server = await asyncio.start_server(relay.serve_client, host, port, limit=MAX_FRAME * 4)

    bound = ", ".join("%s:%d" % s.getsockname()[:2] for s in server.sockets)
    sys.stderr.write("relay listening on %s\n" % bound)
    sys.stderr.flush()

    async with server:
        await server.serve_forever()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1",
        help="address to listen on (default: loopback only)")
    parser.add_argument("--port", type=int, default=8766, help="port to listen on")
    parser.add_argument("--verbose", action="store_true",
        help="log joins, leaves, refusals and dropped frames")
    parser.add_argument("--trace", action="store_true",
        help="log every frame forwarded, which is what shows a game reaching its peers")
    args = parser.parse_args(argv)

    if sys.version_info < (3, 9):
        sys.stderr.write("the relay needs Python 3.9 or newer\n")
        return 1

    try:
        asyncio.run(_serve(args.host, args.port, args.verbose, args.trace))
    except KeyboardInterrupt:
        pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
