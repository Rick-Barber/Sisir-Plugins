"""Reads a .sirbake (src/SirBakeFormat.h): the scene `Sisir.exe --bake` generated.

The same checks as plugins/common/SirBakeReader.cpp -- magic, version, every chunk's bounds and
CRC, every array against its header's counts -- done once on open, so the numpy views handed out
afterwards point straight into the file's bytes with nothing left to check. The error texts are the
C++ reader's, so every Sisir plug-in says the same thing about the same file.
"""

import json
import struct
import zlib

import numpy as np

MAGIC = b"SIRBAKE\0"
FORMAT_VERSION = 1
CARD_FLAG_SMOOTH = 1


class BakeError(Exception):
    pass


def _align8(v):
    return (v + 7) & ~7


class _Cursor:
    """One payload's arrays, each starting on an 8-byte boundary."""

    def __init__(self, data, start, size, error):
        self.data = data
        self.start = start
        self.size = size
        self.pos = 0
        self.error = error

    def header(self, fmt):
        n = struct.calcsize(fmt)
        if self.pos + n > self.size:
            raise BakeError(self.error)
        out = struct.unpack_from(fmt, self.data, self.start + self.pos)
        self.pos += n
        return out

    def array(self, dtype, count):
        self.pos = _align8(self.pos)
        dt = np.dtype(dtype)
        n = dt.itemsize * count
        if self.pos + n > self.size:
            raise BakeError(self.error)
        out = np.frombuffer(self.data, dtype=dt, count=count, offset=self.start + self.pos)
        self.pos += n
        return out


class Bake:
    """meta (dict), hair(index), guides(index), cards(index, smooth): numpy arrays or None."""

    def __init__(self, path):
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            raise BakeError("cannot open " + str(path))
        if len(data) < 32 or data[:8] != MAGIC:
            raise BakeError("not a Sisir bake file")
        version, count = struct.unpack_from("<2I", data, 8)
        # Says which side to update: a plug-in older than Sisir is updated from Sisir's Plugins menu.
        if version > FORMAT_VERSION:
            raise BakeError("this Sisir is newer than its plug-in: update the plug-in in Sisir, Plugins menu, then restart")
        if version < FORMAT_VERSION:
            raise BakeError("this Sisir is older than its plug-in: update Sisir")
        self._hair = {}
        self._guides = {}
        self._cards = {}
        self.meta = None
        view = memoryview(data)
        pos = 32
        for _ in range(count):
            if pos + 24 > len(data):
                raise BakeError("bake file is truncated")
            tag, index, flags, crc, size = struct.unpack_from("<4sIIIQ", data, pos)
            pos += 24
            if size > len(data) - pos:
                raise BakeError("bake file is truncated")
            if (zlib.crc32(view[pos:pos + size]) & 0xFFFFFFFF) != crc:
                raise BakeError("bake file is corrupt (checksum mismatch)")
            start = pos
            pos += _align8(size)
            if tag == b"META":
                try:
                    self.meta = json.loads(bytes(view[start:start + size]).decode("utf-8"))
                except (ValueError, UnicodeDecodeError):
                    raise BakeError("bake metadata is unreadable")
                if not isinstance(self.meta, dict):
                    raise BakeError("bake metadata is unreadable")
            elif tag == b"HAIR":
                self._hair[index] = self._parse_hair(data, start, size)
            elif tag == b"GUID":
                self._guides[index] = self._parse_curves(data, start, size)
            elif tag == b"CARD":
                self._cards[(index, bool(flags & CARD_FLAG_SMOOTH))] = self._parse_cards(data, start, size)
            # Unknown tags are skipped: a newer Sisir may add chunks an older plug-in does not need.
        if self.meta is None:
            raise BakeError("bake file has no metadata")
        self._data = data

    @staticmethod
    def _parse_hair(data, start, size):
        c = _Cursor(data, start, size, "hair chunk is malformed")
        strands, points, _, _ = c.header("<4I")
        h = {
            "strands": strands,
            "points": points,
            "pointCounts": c.array("<u4", strands),
            "positions": c.array("<f4", points * 3),
            "widths": c.array("<f4", points),
            "albedo": c.array("u1", points * 4),
            "rootUvs": c.array("<f4", strands * 2),
            "clumpIds": c.array("<i4", strands),
            "randoms": c.array("<f4", strands),
        }
        counts = h["pointCounts"]
        if int(counts.sum(dtype=np.uint64)) != points or (strands and int(counts.min()) < 2):
            raise BakeError("hair chunk is malformed")
        return h

    @staticmethod
    def _parse_curves(data, start, size):
        c = _Cursor(data, start, size, "guide chunk is malformed")
        curves, points = c.header("<2I")
        g = {
            "curves": curves,
            "points": points,
            "pointCounts": c.array("<u4", curves),
            "positions": c.array("<f4", points * 3),
        }
        if int(g["pointCounts"].sum(dtype=np.uint64)) != points:
            raise BakeError("guide chunk is malformed")
        return g

    @staticmethod
    def _parse_cards(data, start, size):
        c = _Cursor(data, start, size, "card chunk is malformed")
        verts, faces, corners, _ = c.header("<4I")
        k = {
            "vertices": verts,
            "faces": faces,
            "corners": corners,
            "positions": c.array("<f4", verts * 3),
            "normals": c.array("<f4", verts * 3),
            "uvs": c.array("<f4", verts * 2),
            "faceSizes": c.array("<u4", faces),
            "cornerIndices": c.array("<u4", corners),
        }
        sizes = k["faceSizes"]
        if int(sizes.sum(dtype=np.uint64)) != corners or (faces and int(sizes.min()) < 3):
            raise BakeError("card chunk is malformed")
        if corners and int(k["cornerIndices"].max()) >= verts:
            raise BakeError("card chunk is malformed")
        return k

    def hair(self, index):
        return self._hair.get(index)

    def guides(self, index):
        return self._guides.get(index)

    def cards(self, index, smooth):
        return self._cards.get((index, bool(smooth)))
