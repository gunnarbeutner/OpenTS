#!/usr/bin/env python3
"""Prints what a saved game holds.

    python3 tools/savedump/savedump.py SAVE0000.SAV
    python3 tools/savedump/savedump.py SAVE0000.SAV --section Rules --depth 3
    python3 tools/savedump/savedump.py SAVE0000.SAV --raw Map --out /tmp/map.bin

The save says most of what a reader needs. Its name table gives every member a
name and a width, so the fields inside a body can be walked, named and stepped
over without this script knowing a thing about the classes; object records carry
their length; and the container's own header is fixed.

What the file does not say is in HARD-CODED below: the top level is a sequence of
sections in the order Put_All writes them, with nothing in the file to name them
or say where one ends. That list is the only thing here that has to be kept in
step with the engine, and a save from a build whose section list differs is read
wrongly rather than refused.

[docs/SAVE-FORMAT.md](../../docs/SAVE-FORMAT.md) owns the format.
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import lzo


# --------------------------------------------------------------------------------------
# HARD-CODED: what the file cannot tell us.
# --------------------------------------------------------------------------------------

# The top level, in the order `Put_All` in code/saveload.cpp writes it. The kind says how
# to read each one: "body" is a run of named members, "heap" a count and that many object
# records, "record" one object record, and "block" a length and a layout only the engine
# knows.
SECTIONS = [
    ("Scenario", "body"),
    ("Environment", "body"),
    ("Rules", "body"),
    ("AnimTypes", "heap"),
    ("Map", "block"),
    ("Tubes", "heap"),
    ("MiscValues", "body"),
    ("Logic", "block"),
    ("TacticalMap", "record"),
    ("HouseTypes", "heap"),
    ("Houses", "heap"),
    ("Units", "heap"),
    ("UnitTypes", "heap"),
    ("InfantryTypes", "heap"),
    ("Infantry", "heap"),
    ("BuildingTypes", "heap"),
    ("Buildings", "heap"),
    ("AircraftTypes", "heap"),
    ("Aircraft", "heap"),
    ("Anims", "heap"),
    ("TaskForces", "heap"),
    ("TeamTypes", "heap"),
    ("Teams", "heap"),
    ("ScriptTypes", "heap"),
    ("Scripts", "heap"),
    ("TagTypes", "heap"),
    ("Tags", "heap"),
    ("TriggerTypes", "heap"),
    ("Triggers", "heap"),
    ("AITriggerTypes", "heap"),
    ("Actions", "heap"),
    ("Events", "heap"),
    ("Factories", "heap"),
    ("VoxelAnimTypes", "heap"),
    ("VoxelAnims", "heap"),
    ("Warheads", "heap"),
    ("Weapons", "heap"),
    ("ParticleTypes", "heap"),
    ("Particles", "heap"),
    ("ParticleSystemTypes", "heap"),
    ("ParticleSystems", "heap"),
    ("BulletTypes", "heap"),
    ("Bullets", "heap"),
    ("WaypointPaths", "heap"),
    ("SmudgeTypes", "heap"),
    ("OverlayTypes", "heap"),
    ("LightSources", "heap"),
    ("BuildingLights", "heap"),
    ("Sides", "heap"),
    ("Tiberiums", "heap"),
    ("EMPulses", "heap"),
    ("SuperWeaponTypes", "heap"),
    ("SuperWeapons", "heap"),
    ("TerrainTypes", "heap"),
    ("Terrains", "heap"),
    ("FoggyObjects", "heap"),
    ("AlphaShapes", "heap"),
    ("Waves", "heap"),
    ("VeinholeMonsters", "block"),
    ("RadarEvents", "block"),
    # Written only when the game is not a campaign, so the sequence has no fixed length.
    ("Session.Options", "body"),
]

# The classes a record can name, from code/classids.cpp. kept in step with it by hand; a class it does not list is printed as its identifier.
CLASS_IDS = {
    "03C4CE76-4CF5-11D2-BC26-00104B8FB04D": "AITriggerClass",
    "BA093524-4CF4-11D2-BC26-00104B8FB04D": "AITriggerTypeClass",
    "4F0EC392-0A55-11D2-ACA7-006008055BB5": "ActionClass",
    "0E272DC2-9C0F-11D1-B709-00A024DDAFD1": "AircraftClass",
    "AE8B33D9-061C-11D2-ACA4-006008055BB5": "AircraftTypeClass",
    "623C7584-74E7-11D2-B8F5-006008C809ED": "AlphaShapeClass",
    "0E272DC3-9C0F-11D1-B709-00A024DDAFD1": "AnimClass",
    "AE8B33DA-061C-11D2-ACA4-006008055BB5": "AnimTypeClass",
    "4A582745-9839-11D1-B709-00A024DDAFD1": "BallisticLocomotion",
    "0E272DC6-9C0F-11D1-B709-00A024DDAFD1": "BuildingClass",
    "54822258-D8A8-11D1-B462-006097C6A979": "BuildingLightClass",
    "AE8B33DB-061C-11D2-ACA4-006008055BB5": "BuildingTypeClass",
    "0E272DC9-9C0F-11D1-B709-00A024DDAFD1": "BulletClass",
    "5AF2CE77-0634-11D2-ACA4-006008055BB5": "BulletTypeClass",
    "FFDAC848-1517-11D2-8175-006008055BB5": "CampaignClass",
    "C1BF99CE-1A8C-11D2-8175-006008055BB5": "CellClass",
    "4A582741-9839-11D1-B709-00A024DDAFD1": "DriveLocomotion",
    "B825CB22-200E-11D2-9FA9-0060089AD458": "EMPulseClass",
    "4F0EC393-0A55-11D2-ACA7-006008055BB5": "EventClass",
    "34ECD9A8-0AB0-11D2-ACA7-006008055BB5": "FactoryClass",
    "4A582746-9839-11D1-B709-00A024DDAFD1": "FlyerLocomotion",
    "1C470B0E-69D7-11D2-B8F2-006008C809ED": "FoggedObjectClass",
    "D9D4A910-87C6-11D1-B707-00A024DDAFD1": "HouseClass",
    "1DD43928-046B-11D2-ACA4-006008055BB5": "HouseTypeClass",
    "4A582742-9839-11D1-B709-00A024DDAFD1": "HoverLocomotion",
    "0E272DC4-9C0F-11D1-B709-00A024DDAFD1": "InfantryClass",
    "AE8B33D8-061C-11D2-ACA4-006008055BB5": "InfantryTypeClass",
    "0E272DC0-9C0F-11D1-B709-00A024DDAFD1": "IsometricTileClass",
    "5AF2CE7A-0634-11D2-ACA4-006008055BB5": "IsometricTileTypeClass",
    "92612C46-F71F-11D1-AC9F-006008055BB5": "JumpjetLocomotion",
    "3DC0B295-6546-11D3-80B0-00902792494C": "LevitateLocomotion",
    "6F9C48F0-1207-11D2-8174-006008055BB5": "LightSource",
    "55D141B8-DB94-11D1-AC98-006008055BB5": "MechLocomotion",
    "241AB316-4CF5-11D2-BC26-00104B8FB04D": "NeuronClass",
    "0E272DC7-9C0F-11D1-B709-00A024DDAFD1": "OverlayClass",
    "5AF2CE79-0634-11D2-ACA4-006008055BB5": "OverlayTypeClass",
    "0E272DCC-9C0F-11D1-B709-00A024DDAFD1": "ParticleClass",
    "0E272DC8-9C0F-11D1-B709-00A024DDAFD1": "ParticleSystemClass",
    "703E044A-0FB1-11D2-8172-006008055BB5": "ParticleSystemTypeClass",
    "703E044B-0FB1-11D2-8172-006008055BB5": "ParticleTypeClass",
    "42F3A646-0789-11D2-ACA5-006008055BB5": "ScriptClass",
    "42F3A647-0789-11D2-ACA5-006008055BB5": "ScriptTypeClass",
    "C53DD372-151E-11D2-8175-006008055BB5": "SideClass",
    "0E272DC5-9C0F-11D1-B709-00A024DDAFD1": "SmudgeClass",
    "5AF2CE78-0634-11D2-ACA4-006008055BB5": "SmudgeTypeClass",
    "D7F754C6-391C-11D2-9B64-00104B972FE8": "SuperWeaponClass",
    "0CF2BCE7-36E4-11D2-B8D8-006008C809ED": "SuperWeaponTypeClass",
    "CF56B38A-240D-11D2-817C-006008055BB5": "TacticalMapClass",
    "54F6E432-09ED-11D2-ACA5-006008055BB5": "TagClass",
    "54F6E433-09ED-11D2-ACA5-006008055BB5": "TagTypeClass",
    "61DE341E-0774-11D2-ACA5-006008055BB5": "TaskForceClass",
    "0E272DCF-9C0F-11D1-B709-00A024DDAFD1": "TeamClass",
    "D1DBA64E-0778-11D2-ACA5-006008055BB5": "TeamTypeClass",
    "4A582747-9839-11D1-B709-00A024DDAFD1": "TeleportLocomotion",
    "0E272DCE-9C0F-11D1-B709-00A024DDAFD1": "TerrainClass",
    "5AF2CE7B-0634-11D2-ACA4-006008055BB5": "TerrainTypeClass",
    "C53DD373-151E-11D2-8175-006008055BB5": "TiberiumClass",
    "C02D1590-0A2A-11D2-ACA7-006008055BB5": "TriggerClass",
    "C02D1591-0A2A-11D2-ACA7-006008055BB5": "TriggerTypeClass",
    "0B4CA41C-B3A7-11D1-B457-006097C6A979": "TubeClass",
    "4A582743-9839-11D1-B709-00A024DDAFD1": "TunnelLocomotion",
    "0E272DCA-9C0F-11D1-B709-00A024DDAFD1": "UnitClass",
    "DCBD42EA-0546-11D2-ACA4-006008055BB5": "UnitTypeClass",
    "5192D06A-C632-11D2-B90B-006008C809ED": "VeinholeMonsterClass",
    "0E272DC1-9C0F-11D1-B709-00A024DDAFD1": "VoxelAnimClass",
    "2EBB6D66-0D4D-11D2-8172-006008055BB5": "VoxelAnimTypeClass",
    "4A582744-9839-11D1-B709-00A024DDAFD1": "WalkLocomotion",
    "A8C54DA4-0F7B-11D2-8172-006008055BB5": "WarheadTypeClass",
    "0E272DCD-9C0F-11D1-B709-00A024DDAFD1": "WaveClass",
    "F73125BA-1054-11D2-8172-006008055BB5": "WaypointPath",
    "9FD219CA-0F7B-11D2-8172-006008055BB5": "WeaponTypeClass",
}


# The identifiers the header's own field table uses, from code/savever.h.
LISTING_FIELDS = {
    2: "ScenarioDescription", 3: "PlayerHouse", 4: "PlayerName1", 8: "PlayerName2",
    9: "GameVersion", 10: "PlayTime", 12: "StartTime", 13: "LastSaveTime",
    16: "InternalVersion", 18: "ExecutableName",
    100: "ScenarioNumber", 101: "CampaignNumber", 102: "GameType",
}

HEADER_SIZE = 32
FORMAT_VERSION = 2
FLAG_LZO = 0x0001
KIND_VARIABLE = 0


# --------------------------------------------------------------------------------------


class Reader:
    def __init__(self, data, position=0):
        self.data = data
        self.at = position

    def u8(self):
        value = self.data[self.at]
        self.at += 1
        return value

    def u16(self):
        value = struct.unpack_from("<H", self.data, self.at)[0]
        self.at += 2
        return value

    def u32(self):
        value = struct.unpack_from("<I", self.data, self.at)[0]
        self.at += 4
        return value

    def bytes(self, count):
        value = self.data[self.at:self.at + count]
        self.at += count
        return value


class Save:
    def __init__(self, path):
        image = Path(path).read_bytes()
        if len(image) < HEADER_SIZE or image[:4] != b"OTSV":
            raise SystemExit("%s does not begin with OTSV, so it is not a saved game" % path)

        (self.signature, self.version, self.flags, self.table_length, self.content_offset,
         self.stored_length, self.content_length, self.content_crc,
         self.header_crc) = struct.unpack_from("<4sHHIIIIII", image, 0)

        self.image = image
        self.listing = self._read_listing()

        checksum = zlib.crc32(image[:28])
        checksum = zlib.crc32(image[HEADER_SIZE:HEADER_SIZE + self.table_length], checksum)
        self.header_ok = (checksum == self.header_crc)

        stored = image[self.content_offset:self.content_offset + self.stored_length]
        self.content_ok = (zlib.crc32(stored) == self.content_crc)

        if self.version != FORMAT_VERSION:
            self.content = None
            return

        self.content = lzo.decompress(stored, self.content_length) if (self.flags & FLAG_LZO) else stored
        self.names = self._read_names()

    def _read_listing(self):
        out = []
        reader = Reader(self.image, HEADER_SIZE)
        end = HEADER_SIZE + self.table_length
        while reader.at + 8 <= end:
            identifier = reader.u16()
            kind = reader.u16()
            value = reader.bytes(reader.u32())
            if kind == 1:
                shown = value.decode("utf-8", "replace")
            elif kind == 2:
                shown = struct.unpack("<I", value)[0]
            else:
                shown = value.hex()
            out.append((LISTING_FIELDS.get(identifier, "field %d" % identifier), shown))
        return out

    def _read_names(self):
        reader = Reader(self.content, 0)
        count = reader.u16()
        names = []
        for _ in range(count):
            kind = reader.u8()
            names.append((reader.bytes(reader.u8()).decode("latin-1"), kind))
        self.content_start = reader.at
        return names


def walk_body(save, start, end, depth, limit, out, indent):
    """Prints the members of one body, and the bodies inside them."""

    at = start
    while at < end:
        if end - at < 2:
            out.append("%s<%d bytes left over>" % (indent, end - at))
            return
        identifier = struct.unpack_from("<H", save.content, at)[0]
        at += 2
        if identifier >= len(save.names):
            out.append("%s<identifier %d is not in the name table>" % (indent, identifier))
            return
        name, kind = save.names[identifier]

        if kind == KIND_VARIABLE:
            width = struct.unpack_from("<I", save.content, at)[0]
            payload = at + 4
        else:
            width = kind
            payload = at

        if payload + width > end:
            out.append("%s%s: <claims %d bytes, past the body>" % (indent, name, width))
            return

        value = save.content[payload:payload + width]
        if kind in (1, 2, 4, 8):
            number = int.from_bytes(value, "little")
            out.append("%s%s = %d (%s)" % (indent, name, number, value.hex()))
        else:
            text = as_text(value)
            if text is not None:
                out.append("%s%s = %s" % (indent, name, text))
                at = payload + width
                continue

            nested = looks_like_body(save, payload, width)
            if nested and (limit < 0 or depth < limit):
                out.append("%s%s: body of %d bytes" % (indent, name, width))
                walk_body(save, payload, payload + width, depth + 1, limit, out, indent + "  ")
            elif nested:
                out.append("%s%s: body of %d bytes, not shown (--depth %d)" %
                           (indent, name, width, depth + 1))
            else:
                out.append("%s%s: %d bytes" % (indent, name, width))

        at = payload + width


def as_text(blob):
    """The text a member holds, when that is plainly what it is.

    A character array is text up to its first NUL and NULs after it; a string is a count
    and that many characters. Either is worth reading as what it says rather than as the
    body its bytes could pass for.
    """

    def printable(raw):
        return raw and all(32 <= byte < 127 or byte in (9, 10, 13) for byte in raw)

    if len(blob) >= 4:
        count = struct.unpack_from("<I", blob, 0)[0]
        if count == len(blob) - 4 and printable(blob[4:]):
            return repr(blob[4:].decode("latin-1"))

    # A fixed buffer is written whole, so what follows the terminator is whatever the
    # memory held; the text is what is in front of it.
    cut = blob.find(0)
    if cut > 0 and printable(blob[:cut]):
        text = repr(blob[:cut].decode("latin-1"))
        tail = blob[cut:]
        if tail.strip(b"\x00"):
            return "%s and %d bytes after the terminator" % (text, len(tail))
        return text

    return None


def looks_like_body(save, at, width):
    """A variable member holds either a body of its own or bytes only its class knows.

    Nothing in the file says which, so the shape is the test: the members of a body tile
    it exactly, every one of them under a name the table carries.
    """

    if width < 2:
        return False

    cursor = at
    end = at + width
    fields = 0
    while cursor < end:
        if end - cursor < 2:
            return False
        identifier = struct.unpack_from("<H", save.content, cursor)[0]
        cursor += 2
        if identifier >= len(save.names):
            return False
        kind = save.names[identifier][1]
        if kind == KIND_VARIABLE:
            if end - cursor < 4:
                return False
            size = struct.unpack_from("<I", save.content, cursor)[0]
            cursor += 4
        else:
            size = kind
        if size > end - cursor:
            return False
        cursor += size
        fields += 1
    return fields > 0


def walk_record(save, at, depth, limit, out, indent):
    """One object record: its class, its length, and the body of members inside it."""

    classid = save.content[at:at + 16]
    length = struct.unpack_from("<I", save.content, at + 16)[0]
    body_at = at + 20
    identity = struct.unpack_from("<I", save.content, body_at)[0]

    out.append("%srecord %s, %d bytes, swizzle id %d" % (indent, format_clsid(classid), length, identity))

    if limit < 0 or depth < limit:
        inner = struct.unpack_from("<I", save.content, body_at + 4)[0]
        walk_body(save, body_at + 8, body_at + 8 + inner, depth + 1, limit, out, indent + "  ")

    return body_at + length


def format_clsid(raw):
    if len(raw) != 16:
        return raw.hex()
    first, second, third = struct.unpack_from("<IHH", raw, 0)
    key = "%08X-%04X-%04X-%s-%s" % (first, second, third, raw[8:10].hex().upper(), raw[10:16].hex().upper())
    name = CLASS_IDS.get(key)
    return "%s {%s}" % (name, key) if name else "{%s}" % key


def dump(save, wanted, limit, out):
    at = save.content_start
    for name, kind in SECTIONS:
        if at >= len(save.content):
            out.append("%s: not written" % name)
            continue

        show = (wanted is None or name.lower() == wanted.lower())

        if kind == "record":
            if show:
                out.append("%s at %d (record)" % (name, at))
                at = walk_record(save, at, 0, limit, out, "  ")
            else:
                at = walk_record(save, at, 0, 0, [], "")
            continue

        length = struct.unpack_from("<I", save.content, at)[0]
        start = at + 4
        end = start + length
        if end > len(save.content):
            out.append("%s: claims %d bytes at %d, past the content" % (name, length, at))
            return
        if show:
            out.append("%s at %d, %d bytes (%s)" % (name, at, length, kind))

        if show and kind == "body":
            walk_body(save, start, end, 0, limit, out, "  ")
        elif show and kind == "heap":
            count = struct.unpack_from("<I", save.content, start)[0]
            out.append("  %d records" % count)
            cursor = start + 4
            for _ in range(count):
                cursor = walk_record(save, cursor, 0, limit, out, "  ")
        elif show and kind == "block":
            out.append("  a layout only the engine knows; %d bytes" % length)

        at = end

    if at != len(save.content):
        out.append("")
        out.append("%d bytes past the last section" % (len(save.content) - at))


def main():
    parser = argparse.ArgumentParser(description="Print what a saved game holds.")
    parser.add_argument("save", help="the .SAV file to read")
    parser.add_argument("--section", help="print only this top level section")
    parser.add_argument("--depth", type=int, default=-1,
                        help="how far to follow the bodies inside a member (all of them by default)")
    parser.add_argument("--names", action="store_true", help="print the name table and stop")
    parser.add_argument("--raw", metavar="SECTION", help="write a section's bytes out instead")
    parser.add_argument("--out", help="where --raw writes")
    options = parser.parse_args()

    save = Save(options.save)

    print("%s: format version %d, flags 0x%04X" % (options.save, save.version, save.flags))
    print("  header checksum %s, content checksum %s" %
          ("ok" if save.header_ok else "WRONG", "ok" if save.content_ok else "WRONG"))
    print("  %d bytes stored, %d unpacked" % (save.stored_length, save.content_length))
    for name, value in save.listing:
        print("  %-20s %s" % (name, value))

    if save.version != FORMAT_VERSION:
        print()
        print("Format version %d carried its members by position rather than by name;" % save.version)
        print("nothing below this line can be read without the build that wrote it.")
        return 0

    print("  %d names in the table, content begins at %d" % (len(save.names), save.content_start))

    if options.names:
        for identifier, (name, kind) in enumerate(save.names):
            print("  %5d %-40s %s" % (identifier, name, "variable" if kind == 0 else "%d bytes" % kind))
        return 0

    if options.raw:
        at = save.content_start
        for name, kind in SECTIONS:
            length = struct.unpack_from("<I", save.content, at)[0]
            if name.lower() == options.raw.lower():
                Path(options.out or (name + ".bin")).write_bytes(save.content[at + 4:at + 4 + length])
                print("wrote %d bytes" % length)
                return 0
            at += 4 + length
        raise SystemExit("no section is named %r" % options.raw)

    print()
    lines = []
    dump(save, options.section, options.depth, lines)
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
