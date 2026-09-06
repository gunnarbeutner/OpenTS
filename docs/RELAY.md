# The network relay

The engine finds and plays network games by broadcasting onto a local
network. A page has no local network, so `tools/relay/relay.py` takes the
place of one: every client that joins the same room can reach every other,
and a frame addressed to the broadcast id reaches all of them at once.

Nothing above the transport is aware of it. `code/wspudp.cpp` already carries
a tunnel framing and already addresses peers by a sixteen-bit id rather than
an address, so the relay speaks what the engine writes. The game's own Network
dialog, `IPXManagerClass`, the retry logic in `code/connect.cpp` and the
lockstep in `code/queue.cpp` are all unchanged. `code/wsrelay.cpp` is the
carrier that connects the browser build to it, and the main menu offers a LAN
game only when the page names a relay.

## Running it

`compose.yaml` builds and publishes it beside the game:

```bash
OPENTS_ASSETS=~/OpenTS-Assets/build/web docker compose up
```

`OPENTS_RELAY_PORT` moves the published port from its default of 8766. The
image is `opents-relay`, built from `tools/relay/Dockerfile`, and it runs as
an unprivileged user because nothing in it writes.

Out of a container it is a script:

```bash
python3 tools/relay/relay.py --port 8766 --verbose
```

There is nothing to install either way. Like the harness, the relay is
standard library only and needs Python 3.9 or newer;
`tools/relay/requirements.txt` owns that decision.

Run directly it listens on loopback. The image answers on every address,
because a published port reaches the container's external one; see
[Exposing it](#exposing-it) for what that does and does not settle.

```bash
python3 tools/relay/test_relay.py
```

runs the protocol suite. The client in it frames its own WebSockets rather
than reusing the relay's, so a framing mistake has to be made twice in
opposite directions to pass.

## Joining

A client opens a WebSocket naming the room it wants and the build it is
running:

```
ws://host:8766/?room=<key>&build=<module hash>
```

and is answered with one text frame:

```json
{"id": 4242, "room": "sunday-game", "members": 2}
```

That id is the only thing a client is told. It is not told who else is in the
room, because it does not need to be: the engine discovers its peers by
broadcasting, and every packet carries the id of whoever sent it.

Text frames are the relay's; binary frames are the game's. After the greeting
a client sends nothing but binary and receives nothing but binary.

## Frames

Binary frames are the engine's own tunnel framing, unchanged:

| Bytes | Field |
| --- | --- |
| 0–1 | The sender's id |
| 2–3 | The recipient's id, or `0xFFFF` for everyone else in the room |
| 4–N | The engine's packet, which the relay never reads |

Ids are two bytes in network order, which is what the engine puts on the wire
because it holds them `htons()`-encoded. `0xFFFF` is the broadcast id because
it reads the same whichever way round the two bytes are taken, so an
endianness mistake at either end cannot turn a broadcast into a unicast or
the reverse.

A frame is forwarded whole. The relay reads four bytes of it and copies the
rest.

## What it refuses

A refusal is a completed handshake followed by a close carrying the reason,
not an HTTP error: a browser cannot read the body of a failed upgrade, but it
can read a close reason, so that is the only way to tell a page why it was
turned away.

| Refused | Because |
| --- | --- |
| No room or no build named | Both are required; neither has a default worth guessing. |
| A build the room is not playing | See [One room, one build](#one-room-one-build). |
| A member past the 256th | A cap so one room cannot become a resource sink. The game enforces its own eight seats per match. |
| A frame over 1028 bytes | `TUNNEL_HEADER_SIZE` plus `WS_RECEIVE_BUFFER_LEN`. Longer than the engine can send is not the engine. |
| A text frame from a client | The relay's own messages go one way. |

Two things are dropped rather than refused, because a client can recover from
both: a frame whose sender id is not the sender's own, and a frame for an id
nobody in the room holds. The first is dropped rather than rewritten so that a
carrier bug stays visible instead of quietly working.

## One room, one build

A room takes the build of whoever opens it and refuses anyone else.

Both ends of a lockstep match have to be running the same code or they
diverge, and a page caches its module by content hash forever while only
`index.html` revalidates, so two players can easily be on different builds
without either noticing. Without this check that would present as a desync
partway into a match, which looks like a network fault and is not one. With
it, the second player is turned away before the game starts and told why.

A room whose members have all left forgets its build after five minutes, so a
room name can be reused after a deployment without the relay having to be
restarted.

## Rooms

A room is a broadcast domain, not a game. Everyone who has the lobby open is
in it, and the game seats eight of them per match; the relay holds 256, which
is a limit on the room rather than a rule of the game.

Where a client names no room it joins `lan`, so one relay is one LAN and two
people who open the page find each other. `?room=` puts a client somewhere
else, which is how two games are kept apart.

## Ids

Ids are drawn at random from `1` to `0xFFFE` rather than counted up, so that
an id is not reused the moment its holder leaves and a late packet cannot be
delivered to whoever took their place. Zero is not used: the engine reads it
as "no id".

The engine accepts ids from the whole signed sixteen-bit range, so a value
above `0x7FFF` reaching it as a negative number is expected and harmless.

## Exposing it

A relay reachable from anywhere is an open one, and an open relay is both an
amplifier and a way into other people's games. The script defaults to
loopback for that reason; the image cannot, so what keeps it private is
whatever publishes its port. What limits it beyond that is only that a room
key has to be known to join a room, so a key needs to be long and unguessable
rather than a name people would pick.

Nothing here terminates TLS. A page served over HTTPS cannot open a `ws://`
socket, so a deployment needs `wss://`, which means a reverse proxy in front
of this; the same one that already serves the game would do.

Access control is still unsettled. TLS is settled wherever something in front
terminates it, and [Deploying it](#deploying-it) is one arrangement that does.

## Deploying it

`tools/relay/fly.toml` runs it on Fly as a single machine that sleeps while no
one is in a room:

```bash
cd tools/relay
fly apps create opents-relay
fly deploy --ha=false
```

`fly launch` is not the command here: it exists to write a `fly.toml`, and
there is one. The app name is global to Fly, so a name already taken has to
be changed in both places.

`--ha=false` is not optional. A deploy places two machines by default, and a
room is held in the process that serves it, so the second machine is a second
broadcast domain with the proxy seating players on whichever it likes. That
presents as a lobby which never lists the other player, and looks like a
transport fault rather than the deployment mistake it is. One machine is one
LAN; a second region is a separate LAN. `fly status` shows how many are
running, and `fly scale count 1` puts it back.

### Its own name, and TLS

Fly terminates TLS itself, so nothing needs to be put in front of it. A
certificate is asked for by name and issued once the name resolves to the
app:

```bash
fly certs add relay.play-ts.net
fly certs setup relay.play-ts.net
```

`setup` prints two ways to point the name at the app: a pair of `A` and
`AAAA` records, or one `CNAME` to a per-app `.fly.dev` host. This deployment
uses the address records. `fly certs check` reports issuance, which follows
within a few minutes of the name resolving; Fly answers the ACME challenge
over the connection itself, so neither the `_acme-challenge` nor the
`_fly-ownership` record it also lists is needed.

That record has to be **DNS only**. `play-ts.net` is served through
Cloudflare, and a proxied record answers with Cloudflare's own addresses, so
Fly never sees the hostname resolve to itself and will not attach a
certificate to it. The name then fails its TLS handshake outright: the proxy
offers no certificate at all rather than a wrong one, so the failure reads as
a dropped connection rather than a naming error. Proxying it would also put
back the extra hop that hosting the relay separately was meant to remove.

The app keeps its `.fly.dev` name as well, and that name is reachable by
anyone who finds it. Nothing about a custom domain closes the relay; see
[Exposing it](#exposing-it).

The game is pointed at the relay through the environment `relay.json` is
rendered from:

```
OPENTS_RELAY_URL=wss://relay.play-ts.net
```

`?relay=` overrides that for a single page, which is how a relay is tried
before a deployment is moved onto it.
