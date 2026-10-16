# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Alaxouche

"""Minimal reader for the TES4 plugin format (Skyrim .esm/.esp/.esl).

Enough to walk records and subrecords without xEdit or Mutagen, so recipes can
be checked against an RecipeVariantsFramework config from any OS. Shared with
UniqueRegionNameFramework's tools.
"""
import struct
import zlib

GRUP = b'GRUP'
COMPRESSED = 0x00040000


def zstring(data):
    """Decode a null-terminated string. Plugins are cp1252 unless localized."""
    return data.split(b'\x00', 1)[0].decode('cp1252', 'replace')


def subrecords(data):
    """Yield (signature, payload) pairs, honouring the XXXX large-size override."""
    pos, override = 0, None
    while pos + 6 <= len(data):
        sig, size = struct.unpack_from('<4sH', data, pos)
        pos += 6
        if sig == b'XXXX':
            override = struct.unpack_from('<I', data, pos)[0]
            pos += size
            continue
        if override is not None:
            size, override = override, None
        yield sig, data[pos:pos + size]
        pos += size


def masters(buf):
    """The TES4 header's MAST list. A FormID's high byte indexes into this."""
    size = struct.unpack_from('<I', buf, 4)[0]
    return [zstring(d) for s, d in subrecords(buf[24:24 + size]) if s == b'MAST']


def header_size(buf):
    return struct.unpack_from('<I', buf, 4)[0]


def walk(buf, pos, end, world=None):
    """Recursively yield (signature, formid, data, worldspace_formid).

    GRUP type 1 is "world children", and its label is the parent WRLD FormID --
    the only way to tell which worldspace an exterior CELL belongs to.
    """
    while pos < end:
        sig, size = struct.unpack_from('<4sI', buf, pos)
        if sig == GRUP:
            label, group_type = struct.unpack_from('<II', buf, pos + 8)
            yield from walk(buf, pos + 24, pos + size,
                            label if group_type == 1 else world)
            pos += size
            continue
        flags, formid = struct.unpack_from('<II', buf, pos + 8)
        data = buf[pos + 24:pos + 24 + size]
        if flags & COMPRESSED:
            data = zlib.decompress(data[4:])
        yield sig, formid, data, world
        pos += 24 + size


def read(path):
    """Open a plugin and return (buffer, masters, first_record_offset)."""
    buf = open(path, 'rb').read()
    if buf[:4] != b'TES4':
        raise ValueError(f'{path} is not a TES4 plugin')
    return buf, masters(buf), 24 + header_size(buf)
