# syntax=docker/dockerfile:1

# Alpine rather than slim: the relay is standard library alone, with nothing to compile
# against libc, so musl costs nothing and the image is half the size to copy to a host.
# The tag is pinned the way the Emscripten toolchain is.
FROM python:3.13-alpine

EXPOSE 8766

RUN adduser --system --uid 10001 --no-create-home relay

WORKDIR /relay
# requirements.txt carries no packages, only the decision that there are none.
COPY relay.py requirements.txt ./

USER relay

HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD python3 -c "import socket; socket.create_connection(('127.0.0.1', 8766), 2).close()"

# Split so that "docker run opents-relay --port 9000" replaces the defaults rather than the
# program. The bind address is not one of the defaults: a server on the container's
# loopback answers nothing a published port sends it.
ENTRYPOINT ["python3", "relay.py", "--host", "0.0.0.0"]
CMD ["--verbose"]
