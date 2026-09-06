"""Checks the relay against a client written to the same description it is.

The client here frames its own WebSocket rather than reusing the relay's, so a mistake in
the framing has to be made twice in opposite directions to pass. Every test starts a relay
on a port the operating system picks, so the suite can run beside anything else.

    python3 tools/relay/test_relay.py
"""

import asyncio
import base64
import json
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import relay as relay_module  # noqa: E402


BUILD = "aabbccddeeff"


class Client:
    """A WebSocket client that masks what it sends, as a real one must."""

    def __init__(self, reader, writer):
        self.Reader = reader
        self.Writer = writer

    @classmethod
    async def connect(cls, port, room, build=BUILD, host="127.0.0.1"):
        reader, writer = await asyncio.open_connection(host, port)
        key = base64.b64encode(os.urandom(16)).decode("ascii")

        target = "/?room=%s&build=%s" % (room, build)
        writer.write(
            (
                "GET %s HTTP/1.1\r\n"
                "Host: %s:%d\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Key: %s\r\n"
                "Sec-WebSocket-Version: 13\r\n\r\n" % (target, host, port, key)
            ).encode("ascii")
        )
        await writer.drain()

        head = await reader.readuntil(b"\r\n\r\n")
        if b"101" not in head.split(b"\r\n")[0]:
            raise AssertionError("handshake refused: %r" % head[:80])

        return cls(reader, writer)

    async def send(self, opcode, payload):
        mask = os.urandom(4)
        masked = bytes(byte ^ mask[index & 3] for index, byte in enumerate(payload))

        header = bytearray([0x80 | opcode])
        length = len(payload)
        if length < 126:
            header.append(0x80 | length)
        elif length < 65536:
            header.append(0x80 | 126)
            header.extend(struct.pack("!H", length))
        else:
            header.append(0x80 | 127)
            header.extend(struct.pack("!Q", length))

        self.Writer.write(bytes(header) + mask + masked)
        await self.Writer.drain()

    async def send_game(self, sender, recipient, body=b"payload"):
        await self.send(relay_module.OPCODE_BINARY,
                        struct.pack("!HH", sender, recipient) + body)

    async def receive(self, timeout=2.0):
        """Returns (opcode, payload); control frames other than close are skipped."""
        while True:
            head = await asyncio.wait_for(self.Reader.readexactly(2), timeout)
            opcode = head[0] & 0x0F
            length = head[1] & 0x7F

            if length == 126:
                length = struct.unpack("!H", await self.Reader.readexactly(2))[0]
            elif length == 127:
                length = struct.unpack("!Q", await self.Reader.readexactly(8))[0]

            payload = await self.Reader.readexactly(length) if length else b""

            if opcode == relay_module.OPCODE_PING:
                await self.send(relay_module.OPCODE_PONG, payload)
                continue

            return (opcode, payload)

    async def hello(self):
        opcode, payload = await self.receive()
        assert opcode == relay_module.OPCODE_TEXT, opcode
        return json.loads(payload.decode("utf-8"))

    async def close(self):
        try:
            self.Writer.close()
            await self.Writer.wait_closed()
        except Exception:
            pass


class RelayTest(unittest.IsolatedAsyncioTestCase):

    async def asyncSetUp(self):
        self.relay = relay_module.Relay()
        self.server = await asyncio.start_server(
            self.relay.serve_client, "127.0.0.1", 0, limit=relay_module.MAX_FRAME * 4)
        self.port = self.server.sockets[0].getsockname()[1]
        self.clients = []

    async def asyncTearDown(self):
        for client in self.clients:
            await client.close()
        self.server.close()
        await self.server.wait_closed()

    async def join(self, room, build=BUILD):
        client = await Client.connect(self.port, room, build)
        self.clients.append(client)
        return client

    async def assert_refused(self, room, build=BUILD, because=b""):
        """A refusal is a completed handshake and then a close carrying the reason.

        A browser cannot read the body of a failed upgrade, so a relay that answered with
        an HTTP error would leave the page with nothing to show. The close reason is the
        only thing that reaches it.
        """
        client = await Client.connect(self.port, room, build)
        self.clients.append(client)

        opcode, payload = await client.receive()
        self.assertEqual(opcode, relay_module.OPCODE_CLOSE)
        self.assertEqual(struct.unpack("!H", payload[:2])[0], relay_module.CLOSE_POLICY)
        if because:
            self.assertIn(because, payload)

    async def test_join_is_answered_with_an_id(self):
        client = await self.join("alpha")
        hello = await client.hello()

        self.assertEqual(hello["room"], "alpha")
        self.assertEqual(hello["members"], 1)
        self.assertGreaterEqual(hello["id"], relay_module.FIRST_ID)
        self.assertLessEqual(hello["id"], relay_module.LAST_ID)
        self.assertNotEqual(hello["id"], relay_module.BROADCAST_ID)

    async def test_ids_are_unique_within_a_room(self):
        ids = set()
        for _ in range(12):
            client = await self.join("alpha")
            ids.add((await client.hello())["id"])

        self.assertEqual(len(ids), 12)

    async def test_a_unicast_reaches_only_its_recipient(self):
        one, two, three = [await self.join("alpha") for _ in range(3)]
        first, second, third = [(await c.hello())["id"] for c in (one, two, three)]

        await one.send_game(first, second, b"for-two")

        opcode, payload = await two.receive()
        self.assertEqual(opcode, relay_module.OPCODE_BINARY)
        self.assertEqual(payload, struct.pack("!HH", first, second) + b"for-two")

        with self.assertRaises(asyncio.TimeoutError):
            await three.receive(timeout=0.25)

    async def test_a_broadcast_reaches_everyone_but_the_sender(self):
        one, two, three = [await self.join("alpha") for _ in range(3)]
        first = (await one.hello())["id"]
        await two.hello()
        await three.hello()

        await one.send_game(first, relay_module.BROADCAST_ID, b"query")

        for peer in (two, three):
            opcode, payload = await peer.receive()
            self.assertEqual(opcode, relay_module.OPCODE_BINARY)
            self.assertEqual(payload[relay_module.HEADER_SIZE:], b"query")
            self.assertEqual(struct.unpack("!HH", payload[:4]),
                             (first, relay_module.BROADCAST_ID))

        with self.assertRaises(asyncio.TimeoutError):
            await one.receive(timeout=0.25)

    async def test_a_reply_reaches_the_sender_of_a_broadcast(self):
        """This is the whole of discovery: query goes out wide, answers come back narrow."""
        one, two = [await self.join("alpha") for _ in range(2)]
        first = (await one.hello())["id"]
        second = (await two.hello())["id"]

        await one.send_game(first, relay_module.BROADCAST_ID, b"NET_QUERY_GAME")

        _opcode, payload = await two.receive()
        querier = struct.unpack("!HH", payload[:4])[0]
        self.assertEqual(querier, first)

        await two.send_game(second, querier, b"NET_ANSWER_GAME")

        _opcode, answer = await one.receive()
        self.assertEqual(answer[relay_module.HEADER_SIZE:], b"NET_ANSWER_GAME")

    async def test_rooms_do_not_reach_each_other(self):
        here = await self.join("alpha")
        there = await self.join("beta")
        first = (await here.hello())["id"]
        await there.hello()

        await here.send_game(first, relay_module.BROADCAST_ID, b"query")

        with self.assertRaises(asyncio.TimeoutError):
            await there.receive(timeout=0.25)

    async def test_a_different_build_is_refused(self):
        first = await self.join("alpha")
        await first.hello()

        await self.assert_refused("alpha", build="0123456789ab", because=b"build")

    async def test_the_same_build_is_admitted(self):
        first = await self.join("alpha")
        await first.hello()

        second = await self.join("alpha", build=BUILD)
        self.assertEqual((await second.hello())["members"], 2)

    async def test_a_full_room_is_refused(self):
        # The cap is a policy number and a large one; what is worth checking is that it is
        # enforced, so the room is filled to whatever it happens to be rather than to 256
        # sockets' worth of it.
        original = relay_module.MAX_MEMBERS
        relay_module.MAX_MEMBERS = 3
        try:
            for _ in range(3):
                client = await self.join("alpha")
                await client.hello()

            await self.assert_refused("alpha", because=b"full")
        finally:
            relay_module.MAX_MEMBERS = original

    async def test_a_room_without_a_name_is_refused(self):
        await self.assert_refused("", because=b"room")

    async def test_speaking_as_somebody_else_is_dropped(self):
        one, two = [await self.join("alpha") for _ in range(2)]
        first = (await one.hello())["id"]
        second = (await two.hello())["id"]

        await one.send_game(second, first, b"spoofed")

        with self.assertRaises(asyncio.TimeoutError):
            await one.receive(timeout=0.25)
        self.assertEqual(self.relay.Dropped, 1)

    async def test_a_packet_for_nobody_is_dropped(self):
        one = await self.join("alpha")
        first = (await one.hello())["id"]

        absent = first + 1 if first < relay_module.LAST_ID else first - 1
        await one.send_game(first, absent, b"nowhere")

        await asyncio.sleep(0.15)
        self.assertEqual(self.relay.Dropped, 1)
        self.assertEqual(self.relay.Forwarded, 0)

    async def test_an_oversized_frame_closes_the_connection(self):
        one = await self.join("alpha")
        first = (await one.hello())["id"]

        await one.send_game(first, relay_module.BROADCAST_ID,
                            b"x" * (relay_module.MAX_PAYLOAD + 1))

        opcode, payload = await one.receive()
        self.assertEqual(opcode, relay_module.OPCODE_CLOSE)
        self.assertEqual(struct.unpack("!H", payload[:2])[0], relay_module.CLOSE_TOO_LARGE)

    async def test_the_largest_frame_the_engine_sends_is_carried(self):
        one, two = [await self.join("alpha") for _ in range(2)]
        first = (await one.hello())["id"]
        await two.hello()

        body = bytes(range(256)) * 4
        self.assertEqual(len(body) + relay_module.HEADER_SIZE, relay_module.MAX_FRAME)

        await one.send_game(first, relay_module.BROADCAST_ID, body)

        _opcode, payload = await two.receive()
        self.assertEqual(payload[relay_module.HEADER_SIZE:], body)

    async def test_leaving_frees_the_id(self):
        one = await self.join("alpha")
        await one.hello()
        two = await self.join("alpha")
        await two.hello()

        self.assertEqual(len(self.relay.Rooms["alpha"].Members), 2)

        await two.close()
        await asyncio.sleep(0.15)

        self.assertEqual(len(self.relay.Rooms["alpha"].Members), 1)

    async def test_an_emptied_room_forgets_its_build(self):
        """A deployment must not lock the next player out of the room it just left."""
        one = await self.join("alpha")
        await one.hello()
        await one.close()
        await asyncio.sleep(0.15)

        self.assertNotIn("alpha", self.relay.Rooms)

        rejoined = await self.join("alpha", build="0123456789ab")
        self.assertEqual((await rejoined.hello())["members"], 1)

    async def test_text_from_a_client_is_refused(self):
        one = await self.join("alpha")
        await one.hello()

        await one.send(relay_module.OPCODE_TEXT, b"hello")

        opcode, _payload = await one.receive()
        self.assertEqual(opcode, relay_module.OPCODE_CLOSE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
