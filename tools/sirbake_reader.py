"""Read and validate a .sirbake (src/SirBakeFormat.h): CRCs, chunk sizes, array bounds.

Usage:
  python tools/sirbake_reader.py <file.sirbake>              # summary
  python tools/sirbake_reader.py <file.sirbake> --json       # META as JSON
  python tools/sirbake_reader.py --bake <Sisir.exe> <in.sir> <out.sirbake>   # bake, then validate

Also importable: load(path) -> (meta, chunks), where chunks are dicts of numpy-free lists via
array.array. Exits non-zero on any inconsistency, so it doubles as a test.
"""
import array
import json
import struct
import subprocess
import sys
import zlib

MAGIC = b"SIRBAKE\0"


def align8(v):
    return (v + 7) & ~7


class Reader:
    def __init__(self, data, offset=0):
        self.data = data
        self.pos = offset
        self.end = len(data)

    def struct(self, fmt):
        size = struct.calcsize(fmt)
        if self.pos + size > self.end:
            raise ValueError("payload truncated")
        out = struct.unpack_from(fmt, self.data, self.pos)
        self.pos += size
        return out

    def array(self, typecode, count):
        self.pos = align8(self.pos)
        a = array.array(typecode)
        size = a.itemsize * count
        if self.pos + size > self.end:
            raise ValueError("array exceeds payload")
        a.frombytes(self.data[self.pos:self.pos + size])
        self.pos += size
        return a


def parse_hair(p):
    r = Reader(p)
    strands, points, _, _ = r.struct("<4I")
    counts = r.array("I", strands)
    if sum(counts) != points:
        raise ValueError("HAIR point counts do not sum to pointCount")
    if any(c < 2 for c in counts):
        raise ValueError("HAIR strand with fewer than 2 points")
    pos = r.array("f", points * 3)
    widths = r.array("f", points)
    albedo = r.array("B", points * 4)
    root_uv = r.array("f", strands * 2)
    clump = r.array("i", strands)
    rnd = r.array("f", strands)
    return dict(strands=strands, points=points, counts=counts, positions=pos, widths=widths,
                albedo=albedo, rootUvs=root_uv, clumpIds=clump, randoms=rnd)


def parse_curves(p):
    r = Reader(p)
    curves, points = r.struct("<2I")
    counts = r.array("I", curves)
    if sum(counts) != points:
        raise ValueError("GUID point counts do not sum to pointCount")
    pos = r.array("f", points * 3)
    return dict(curves=curves, points=points, counts=counts, positions=pos)


def parse_cards(p):
    r = Reader(p)
    verts, faces, corners, _ = r.struct("<4I")
    pos = r.array("f", verts * 3)
    nrm = r.array("f", verts * 3)
    uvs = r.array("f", verts * 2)
    sizes = r.array("I", faces)
    idx = r.array("I", corners)
    if sum(sizes) != corners:
        raise ValueError("CARD face sizes do not sum to cornerCount")
    if any(s not in (3, 4) for s in sizes):
        raise ValueError("CARD face with a size other than 3 or 4")
    if corners and max(idx) >= verts:
        raise ValueError("CARD corner out of range")
    return dict(vertices=verts, faces=faces, corners=corners, positions=pos, normals=nrm, uvs=uvs,
                faceSizes=sizes, cornerIndices=idx)


def load(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 32 or data[:8] != MAGIC:
        raise ValueError("not a .sirbake file")
    version, count = struct.unpack_from("<2I", data, 8)
    if version != 1:
        raise ValueError("unsupported format version %d" % version)
    pos = 32
    meta = None
    chunks = []
    for _ in range(count):
        if pos + 24 > len(data):
            raise ValueError("chunk header truncated")
        tag, index, flags, crc, size = struct.unpack_from("<4sIIIQ", data, pos)
        pos += 24
        if pos + size > len(data):
            raise ValueError("chunk %s payload truncated" % tag)
        payload = data[pos:pos + size]
        if (zlib.crc32(payload) & 0xFFFFFFFF) != crc:
            raise ValueError("chunk %s #%d CRC mismatch" % (tag, index))
        pos += align8(size)
        tag = tag.decode("ascii")
        if tag == "META":
            meta = json.loads(payload.decode("utf-8"))
            continue
        parsed = {"HAIR": parse_hair, "GUID": parse_curves, "CARD": parse_cards}.get(tag)
        chunks.append(dict(tag=tag, index=index, flags=flags, size=size,
                           data=parsed(payload) if parsed else None))
    if meta is None:
        raise ValueError("no META chunk")
    if pos != len(data):
        raise ValueError("trailing bytes after last chunk")
    return meta, chunks


def summary(meta, chunks):
    print("Sisir %s  source: %s" % (meta.get("sisirVersion"), meta.get("source")))
    for key in ("mesh", "refMesh"):
        m = meta.get(key, {})
        if m.get("present"):
            print("  %-8s %s (original=%s, scale=%g, %d tris)" % (key, m.get("file"), m.get("original"),
                                                                 m.get("importScale", 0), m.get("triangles", 0)))
    by = {}
    for c in chunks:
        by.setdefault(c["tag"], []).append(c)
    for s in meta.get("hairSets", []):
        print("  hair     %-24s strands=%-8d points=%-9d visible=%s mat=%d" % (
            s["name"], s["strands"], s["points"], s["visible"], s["materialId"]))
    for l in meta.get("guideLayers", []):
        print("  guides   %-24s curves=%d" % (l["name"], l["curves"]))
    for l in meta.get("cardLayers", []):
        print("  cards    %-24s raw=%s smooth=%s uvId=%s" % (l["name"], l.get("facesRaw"), l.get("facesSmooth"), l["uvId"]))
    print("  chunks: " + ", ".join("%s x%d" % (k, len(v)) for k, v in sorted(by.items())))


def main(argv):
    if len(argv) >= 4 and argv[0] == "--bake":
        exe, sir, out = argv[1:4]
        proc = subprocess.run([exe, "--bake", sir, out, "--progress"], capture_output=True, text=True)
        if proc.returncode != 0:
            print(proc.stderr.strip() or "bake failed with exit code %d" % proc.returncode)
            return proc.returncode
        argv = [out]
    if not argv:
        print(__doc__)
        return 2
    try:
        meta, chunks = load(argv[0])
    except ValueError as e:
        print("INVALID: %s" % e)
        return 1
    if "--json" in argv:
        print(json.dumps(meta, indent=2))
    else:
        summary(meta, chunks)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
