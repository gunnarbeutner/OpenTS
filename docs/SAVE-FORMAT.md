# The saved game format

A saved game is one `.SAV` file written by `code/savefile.cpp` and read back by
it. This document owns the layout. Where the files live, how they are named,
and when they are written is on the manual's
[save games page](../manual/content/formats/save-games.md).

Every integer is little-endian. Offsets are from the start of the file.

## Header

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Signature, the bytes `OTSV` |
| 4 | 2 | Format version, currently 2 |
| 6 | 2 | Flags; bit 0 set when the content is LZO-compressed |
| 8 | 4 | Length of the field table |
| 12 | 4 | Offset of the content |
| 16 | 4 | Stored length of the content |
| 20 | 4 | Uncompressed length of the content |
| 24 | 4 | CRC-32 of the stored content |
| 28 | 4 | CRC-32 of the first 28 bytes of the header, continued over the field table |

The header is 32 bytes, the field table follows it directly, and the content
follows the table directly. The content offset is recorded rather than assumed
so a later format version can put something between the two; this version
refuses a file whose offset says otherwise. A field table is refused above
1 MiB, since a listing is a dozen short fields.

Both checksums are the CRC-32 of IEEE 802.3, polynomial `0xEDB88320`
reflected, initial value and final complement of all ones, as PNG and gzip
use it. The header checksum continues over the field table so a listing can
verify what it shows without reading the content.

## Field table

The fields are what the load dialog lists a save by. Each is:

| Size | Field |
| --- | --- |
| 2 | Identifier |
| 2 | Kind: 1 string, 2 integer, 3 file time |
| 4 | Length of the value |
| | The value: string bytes without a terminator, a 4-byte integer, or an 8-byte file time |

The identifiers are the `PIDSI_` values in `code/savever.h`, the same ones the
compound-document property set carried before this format. A field holds at
most 64 KiB. A reader takes the
first field that matches both identifier and kind and ignores the rest, so a
field it does not know costs nothing. A string longer than the buffer it is
read into is cut on a character boundary, so a shortened description stays
UTF-8. `SaveVersionInfo` in `code/savever.cpp` is the only writer and reader.

A file time counts 100-nanosecond intervals from the start of 1601 UTC, as a
Windows `FILETIME` does, and is stored as two 4-byte words, the low word first.
The engine holds it as a `FileTimeType` (`code/platform/filetime.h`).

## Content

The content is the game state: the bytes `Put_All` in `code/saveload.cpp`
writes through `SaveStreamClass`, compressed as one block with LZO1X-1 when
that makes it smaller, and stored as it is otherwise. The reader checks the
stored length and checksum before decompressing, and refuses a block that does
not expand to exactly the recorded length. An uncompressed length above 256 MiB
is refused before anything is allocated for it.

The block is decompressed through `lzo1x_decompress_safe`, which stops at the
end of the output buffer, so a block forged to expand past the recorded length
is refused rather than written past it. The records after the header are still
read into live objects, so treat a save file from an untrusted source as
untrusted input.

### The name table

The content opens with the table that names every member in it. It is written
once the state has been written, since only then are the names known, and read
before anything else:

| Size | Field |
| --- | --- |
| 2 | How many names follow |
| | Per name: 1 byte of kind, 1 byte of length, and that many bytes of name |

A name's identifier is its position in the table. The kind is the width of the
member's payload -- 1, 2, 4 or 8 bytes -- or 0 for a payload that begins with
its own four-byte length. That width is what lets a reader step over a member
it has none of its own for, so a table entry it does not recognize costs it
nothing. A file carries at most 65536 names and a name is at most 255 bytes; a
table claiming more, or a width that is none of the above, is refused before
the content is read.

Two members of the same name and width share an identifier wherever they
appear. Two of the same name and different widths are two entries, so a name
means one shape throughout a file.

### Members and bodies

A member is its identifier, two bytes, followed by its payload. A body is a
four-byte length followed by the members inside it, and a class that lists
named members travels as one. Loading indexes a body's members before reading
any of them and then takes each by name, so:

- the order the members are written in does not matter,
- a member the reader has no field for is skipped,
- a member the file does not carry keeps the value its object was built with,
- and a name appearing twice in one body fails the load rather than letting
  one of the two win.

That is what lets a member be added to a class without invalidating the saves
written before it. What it does not cover is a member that keeps its name and
changes its meaning or width: the width is part of the name's identity, so a
changed width reads as a different member, but a changed meaning is carried
into the new build as if nothing had happened.

A class built on another gives the base a body of its own, named for the base,
so a base and the class built on it may both have a member called `Timer` and
either may gain one without disturbing the other.

Not everything is named. A container's elements have positions rather than
names, and so do the runs a class lays out itself -- a flag and the object it
guards, a count and the records after it. These travel as one member whose
payload is read in the order it was written, and a class that is nothing but
such a run says so with `SERIALIZE_POSITIONAL`. A run of that kind at the top
level is framed by its length alone, with nothing inside it indexed.

### Object records

The state is a sequence of blocks and object records in the order `Put_All`
names them. An object record is:

| Size | Field |
| --- | --- |
| 16 | The class identifier of the object |
| 4 | Length of the record |
| | The swizzle identity, then a body of the members the class's `Serialize` names |

The class identifier is the `ClassID` the object's `Class_ID` reports, the
same one registered in `code/startup.cpp` and, for a locomotor, named by the
`Locomotor=` key. Its sixteen bytes are those of the COM class identifier
the class once registered, kept because the `Locomotor=` values in rules
files carry them. The reader creates the object through that
registration, hands it the stream, checks that it consumed exactly the
recorded length, and only then lets it finish restoring itself, so a refused
record never reaches the map or a side table. A record that comes up short
or long fails the load with the object's type and offset in the debug log,
which is what a member added to one build and not the other looks like. A
record read where a locomotor belongs fails the load the same way when its
class is not one. A vector of objects is a 4-byte count followed by that
many records, all of the heap's own class; a record naming any other class
fails the load, since nothing else belongs in that heap. A locomotor nested
inside a unit's record is a record of its own. A count that the bytes
remaining in the content could not hold fails the load before anything is
allocated for it.

An object whose record fails is destroyed before the load fails. The pointer
slots it had registered are cleared first, since they still hold identities
rather than addresses, and the slots the records before it registered are
cleared the same way. Those earlier objects stay in their heaps, and the ones
that had finished loading have already taken their place in the map or a side
table. A failed load therefore leaves a partly built game that the caller has
to clear, not one it can carry on from.

A character buffer travels as its text: a length and that many characters, and
a load clears the rest of the buffer. How much room a build keeps for a string
is its own business, so the file carries neither the capacity nor whatever the
memory held past the terminator. The text is at most one character shorter than
the buffer, so a loaded buffer is always terminated; a length that would fill it
outright fails the load, since the engine reads these buffers as C strings.

The body is what each class's `Serialize` produces, member by member, in host
byte order. It is not described here; the classes are the description.

The swizzle identity and every pointer member travel as four bytes. The save
numbers the objects it meets rather than writing the address one sat at, so
the body depends neither on the pointer width of the build that wrote it nor
on where the objects were in memory.

## Versions

Two numbers gate a save. The format version in the header says how to parse
the file, and a reader refuses a version above its own. The header flags are
gated the same way: a reader refuses a file with a flag bit it does not know,
so a later version can mark content it stores differently without moving the
format version. The internal version in the field table,
`PIDSI_INTERNAL_VER`, is `ExpectedGameVersion`, the packed project version,
and a save whose value differs from the running build's is not offered to the
player. The format version moves only when the layout in this document
changes; the internal version moves with every release.

## What the reader refuses

`SaveFileClass::Read` and `Read_Fields` answer one of:

| Result | When |
| --- | --- |
| `RESULT_MISSING` | No file under that name |
| `RESULT_NOT_A_SAVE` | The first bytes are not the signature |
| `RESULT_UNSUPPORTED_VERSION` | A format version above the reader's, or a header flag it does not know |
| `RESULT_CORRUPT` | A length, checksum or compressed block that does not add up, including a truncated file, a forged block, a field table above 1 MiB, a content offset that does not follow the table, or a content length above 256 MiB |
| `RESULT_NO_MEMORY` | A file within those limits that the process cannot hold |

`Read` judges the header before it reads or allocates anything else, so a file
of any size costs the reader no more than the limits above allow, and
`Read_Fields` reads the header and the table only, so listing a folder never
allocates for a file's content.

`Load_Game` reads and checks the whole file before it tears down the running
game, so a refused file costs nothing.

A save written before this format is an OLE compound document, which begins
with a signature of its own, so the reader answers `RESULT_NOT_A_SAVE` and the
load dialog leaves the file out of its list. A save written in format version 1,
whose members travelled by position rather than by name, is refused as an
unsupported version. Nothing converts either.

## Writing

`SaveFileClass::Write` builds the whole image in memory, writes it to the
target name with `.tmp` appended, flushes and closes it, and then moves it over
the target with `Platform_Replace_File`: `MoveFileExA` with
`MOVEFILE_REPLACE_EXISTING` on Windows, `rename` elsewhere. A save
interrupted at any point leaves the previous file untouched under its name,
and at most a `.tmp` beside it, which the next successful save replaces.
The reader's limits bind the writer too: content above 256 MiB, a field above
64 KiB or a table above 1 MiB is refused with `RESULT_TOO_LARGE` before
anything is written, so a save this build writes is one it reads, and the
file on disk is left as it was.

## Reading one

`python3 tools/savedump/savedump.py SAVE0000.SAV` prints what a save holds: the
header, the listing fields, the name table, and every section with the members
inside it. `--section` picks one, `--depth` says how far into the bodies to
follow, `--names` prints the table alone, and `--raw` writes a section's bytes
out. It needs nothing but Python; the LZO decompressor beside it is a port of
the one the engine links.

The name table is what makes this possible: a body can be walked, named and
stepped over without knowing anything about the classes. The section list is
not in the file, so the tool carries its own copy of it, and a save from a
build whose sections differ is read wrongly rather than refused.

## Checks

`tests/save` builds `code/savefile.cpp` against the vendored LZO library and
covers the round trip, the fields-only read, replacement of an existing file
and of a stale `.tmp`, and each refusal above, including a later version, an
unknown flag, a file cut at every boundary, a byte flipped in the header, the
table and the content, a field table above its limit, a gap before the
content, a block that ends before or expands past its declared length, and a
write above each limit that leaves the earlier save in place. One save written
from fixed fields and content is compared by length and checksum with a
recorded image of that file, and its time field byte for byte. It reads no
game data.
