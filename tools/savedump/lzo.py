"""LZO1X decompression, so a save can be read without building anything.

This is a port of the reference decompressor the engine links against
(``thirdparty/lzo/src/lzo1x_d.ch``). It is here because the dumper has no other
way in: the content of every save the engine writes is one LZO1X-1 block, and
the standard library decompresses everything except that.
"""


class LZOError(Exception):
    pass


def decompress(src, expected):
    """Returns the ``expected`` bytes packed into ``src``."""

    out = bytearray()
    ip = 0
    end = len(src)

    def literal(count):
        nonlocal ip
        if ip + count > end:
            raise LZOError("literal run of %d runs past the block" % count)
        out.extend(src[ip:ip + count])
        ip += count

    def extend_length(base):
        """A length of zero in the token means the count continues in the bytes after it."""
        nonlocal ip
        total = 0
        while True:
            if ip >= end:
                raise LZOError("a length runs past the block")
            if src[ip] != 0:
                break
            total += 255
            ip += 1
        total += base + src[ip]
        ip += 1
        return total

    def copy_match(distance, count):
        if distance <= 0 or distance > len(out):
            raise LZOError("a match points %d back, outside what is decoded" % distance)
        start = len(out) - distance
        # The runs overlap by design, so this copies one byte at a time.
        for index in range(count):
            out.append(out[start + index])

    if ip >= end:
        raise LZOError("the block is empty")

    state = 0
    token = 0

    # The first token is special: a value above 17 is a literal run and nothing else.
    if src[ip] > 17:
        count = src[ip] - 17
        ip += 1
        if count < 4:
            literal(count)
            state = count
            token = src[ip]
            ip += 1
            first = False
        else:
            literal(count)
            state = 4
            first = True
    else:
        first = None

    while True:
        if first is None:
            if ip >= end:
                raise LZOError("the block ends in the middle of a token")
            token = src[ip]
            ip += 1

            if token < 16:
                if token == 0:
                    count = extend_length(15)
                else:
                    count = token
                literal(count + 3)
                state = 4
                first = True
            else:
                first = False

        if first:
            # The token after a literal run names a match of its own.
            if ip >= end:
                raise LZOError("the block ends after a literal run")
            token = src[ip]
            ip += 1
            if token < 16:
                if ip >= end:
                    raise LZOError("the block ends inside a short match")
                distance = 1 + 0x0800 + (token >> 2) + (src[ip] << 2)
                ip += 1
                copy_match(distance, 3)
                state = src[ip - 2] & 3
                first = None
                if state != 0:
                    literal(state)
                    if ip >= end:
                        raise LZOError("the block ends after a trailing literal")
                    token = src[ip]
                    ip += 1
                    first = False
                continue
            first = False

        # A match, named by the token.
        while True:
            if token >= 64:
                distance = 1 + ((token >> 2) & 7) + (src[ip] << 3)
                ip += 1
                count = (token >> 5) - 1 + 2
            elif token >= 32:
                count = token & 31
                if count == 0:
                    count = extend_length(31)
                count += 2
                if ip + 2 > end:
                    raise LZOError("the block ends inside a match")
                distance = 1 + ((src[ip] | (src[ip + 1] << 8)) >> 2)
                ip += 2
            elif token >= 16:
                distance = (token & 8) << 11
                count = token & 7
                if count == 0:
                    count = extend_length(7)
                count += 2
                if ip + 2 > end:
                    raise LZOError("the block ends inside a long match")
                distance += (src[ip] | (src[ip + 1] << 8)) >> 2
                ip += 2
                if distance == 0:
                    if len(out) != expected:
                        raise LZOError("the block ends after %d bytes, not %d" % (len(out), expected))
                    return bytes(out)
                distance += 0x4000
            else:
                if ip >= end:
                    raise LZOError("the block ends inside a two byte match")
                distance = 1 + (token >> 2) + (src[ip] << 2)
                ip += 1
                count = 2

            copy_match(distance, count)

            state = src[ip - 2] & 3
            if state == 0:
                first = None
                break

            literal(state)
            if ip >= end:
                raise LZOError("the block ends after a trailing literal")
            token = src[ip]
            ip += 1
