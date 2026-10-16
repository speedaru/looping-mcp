"""Prototype .loo parser (Python) used to validate layouts recovered from Looping.exe.

usage: python -I tools/loo_parse.py FILE.loo [-v]

File = fixed 2592-byte document header (identical across the corpus except 2 bytes in one file),
then an MFC CObList: count + objects written with CArchive::WriteObject (class tags, map indices).
Layouts below are the version-51 store paths, offsets are `this+N` in the C++ objects (see docs/recon.md).
This is a throwaway validation tool; the real implementation will be C++20 in core/.
"""
import struct
import sys

HEADER_LEN = 2592


class R:
    def __init__(self, d, pos=0, verbose=False):
        self.d, self.p, self.v = d, pos, verbose
        self.classes = []      # class names in order of first appearance
        self.map_count = 1     # CArchive map index counter (0 = null)
        self.objs = []         # parsed objects, index = map index (None for class slots)

    def take(self, n):
        b = self.d[self.p:self.p + n]
        if len(b) != n:
            raise EOFError(f"need {n} bytes at {self.p:#x}")
        self.p += n
        return b

    def u8(self): return self.take(1)[0]
    def u16(self): return struct.unpack("<H", self.take(2))[0]
    def u32(self): return struct.unpack("<I", self.take(4))[0]
    def u64(self): return struct.unpack("<Q", self.take(8))[0]

    def cstring(self):
        """MFC CArchive CString (unicode): ff fe ff <len> + UTF-16LE."""
        marker = self.take(3)
        if marker != b"\xff\xfe\xff":
            raise ValueError(f"bad string marker {marker.hex()} at {self.p - 3:#x}")
        n = self.u8()
        if n == 0xFF:
            n = self.u16()
            if n == 0xFFFF:
                n = self.u32()
        return self.take(2 * n).decode("utf-16le")

    def count(self):
        n = self.u16()
        return self.u32() if n == 0xFFFF else n


def parse_obj_base(r):
    o = {"_start": r.p}
    o["f320"] = r.u32()
    for off in (216, 228, 220, 236, 224, 244):
        o[f"w{off}"] = r.u16()
    o["rect40"] = r.take(16); o["s120"] = r.take(12)
    o["rect56"] = r.take(16); o["s152"] = r.take(12)
    o["rect72"] = r.take(16); o["s184"] = r.take(12)
    for off in (16, 776, 780, 784, 788, 792):
        o[f"d{off}"] = r.u32()
    for off in (232, 240, 248):
        o[f"w{off}"] = r.u16()
    o["s136"] = r.take(12); o["s168"] = r.take(12); o["s200"] = r.take(12)
    o["d824"] = r.u32()
    return o


def parse_entite(r, o):
    flags = r.u16()
    o["flag1264"], o["flag1268"] = flags & 1, (flags >> 1) & 1
    o["rect1232"] = struct.unpack("<4i", r.take(16))
    # u32@1236,u32@1244 only if a document flag is set; not seen set in the corpus yet.


def parse_parametre(r):
    p = {"_start": r.p}
    p["d8"] = r.u32(); p["name"] = r.cstring()
    p["type32"] = r.u32(); p["d36"] = r.u32(); p["d40"] = r.u32()
    p["s72"] = r.cstring(); p["s80"] = r.cstring(); p["s88"] = r.cstring()
    p["d96"] = r.u32()
    p["logical24"] = r.cstring(); p["s280"] = r.cstring(); p["s312"] = r.cstring()
    for off in (320, 324, 332, 328, 44, 48, 52, 56, 132, 136, 140, 144):
        p[f"d{off}"] = r.u32()
    p["s152"] = r.cstring()
    p["q160"] = r.u64(); p["d168"] = r.u32()
    p["date176"] = (r.u32(), struct.unpack("<d", r.take(8))[0])   # COleDateTime: status u32 + double (created)
    p["date192"] = (r.u32(), struct.unpack("<d", r.take(8))[0])   # modified
    p["d208"] = r.u32(); p["d100"] = r.u32()
    for off in (104, 108, 112, 116, 120, 124, 128, 212, 216):
        p[f"d{off}"] = r.u32()
    p["s224"] = r.cstring(); p["s232"] = r.cstring(); p["s240"] = r.cstring()
    p["t300"] = r.u32(); p["t304"] = r.u32(); p["d308"] = r.u32()
    p["s248"] = r.cstring(); p["s256"] = r.cstring(); p["d264"] = r.u32()
    p["d268"] = r.u32(); p["d272"] = r.u32(); p["d276"] = r.u32()
    p["s288"] = r.cstring(); p["s64"] = r.cstring(); p["d296"] = r.u32()
    p["_end"] = r.p
    return p


def parse_stringlist(r, o, key):
    """CStringList::Serialize: count + CStrings (text lines)."""
    n = r.count()
    o[key] = [r.cstring() for _ in range(n)]


def parse_texte(r, o):
    o["d1112"] = r.u32(); o["d1116"] = r.u32()
    parse_stringlist(r, o, "lines1128"); parse_stringlist(r, o, "lines1184")
    o["d1240"] = r.u32(); o["s1528"] = r.cstring(); o["d1120"] = r.u32()
    r.take(8)  # two zero u32
    o["blk912"] = r.take(100); o["blk916"] = r.take(92); o["blk1012"] = r.take(100); o["blk1016"] = r.take(92)
    o["w1244"] = r.u16()
    n = r.u32()
    o["params"] = [parse_parametre(r) for _ in range(n)]


def parse_relation(r, o):
    o["raw912"] = r.take(8); o["d920"] = r.u32(); o["d928"] = r.u32(); o["raw932"] = r.take(8)
    o["d940"] = r.u32(); o["d924"] = r.u32(); r.take(8)  # two zero u32
    o["s_tail"] = r.cstring()


def parse_arc(r, o):
    o["d968"] = r.u32(); o["d972"] = r.u32(); o["d976"] = r.u32(); o["s984"] = r.cstring()


SERIALIZERS = {"CDrawEntite": parse_entite, "CDrawTexte": parse_texte,
               "CDrawRelation": parse_relation, "CDrawArc": parse_arc}


def read_object(r):
    pos = r.p
    tag = r.u16()
    if tag == 0:
        return None
    if tag == 0xFFFF:  # new class
        schema = r.u16(); name = r.take(r.u16()).decode("ascii")
        r.classes.append(name); r.map_count += 1
        cls = name
        r.objs.append(("class", name))
    elif tag & 0x8000:
        cls = r.classes[(tag & 0x7FFF) - 1] if False else None
        # old class: index into map -> find the class slot
        cls = r.objs[(tag & 0x7FFF) - 1][1]
    else:  # reference to an existing object
        return ("ref", tag)
    idx = r.map_count; r.map_count += 1
    o = parse_obj_base(r); o["_class"] = cls; o["_at"] = pos; o["_idx"] = idx
    r.objs.append(("obj", o))
    fn = SERIALIZERS.get(cls)
    if fn is None:
        raise NotImplementedError(f"{cls} layout not implemented (object starts at {pos:#x})")
    fn(r, o)
    if r.v:
        print(f"  [{idx}] {cls} @ {pos:#x}..{r.p:#x}")
    return o


def parse(path, verbose=False):
    d = open(path, "rb").read()
    r = R(d, HEADER_LEN, verbose)
    n = r.count()
    objs = [read_object(r) for _ in range(n)]
    r.list_end = r.p
    # second pass (vtable slot 3): six object ids per object, in list order
    for o in objs:
        o["refs"] = [r.u32() for _ in range(6)]   # this+328,+336,+344, chain+864, chain+872, owner+880
        if o["_class"] == "CDrawArc":             # CRattach member at this+920: u16, 8 raw bytes, u32 0
            o["rattach"] = (r.u16(), r.take(8), r.u32())
    return d, r, objs


if __name__ == "__main__":
    f = sys.argv[1]
    d, r, objs = parse(f, "-v" in sys.argv)
    print(f"{f}: {len(objs)} top-level objects, consumed {r.p}/{len(d)} bytes")
    for o in objs:
        if isinstance(o, dict):
            print(" ", o["_class"], f"idx={o['_idx']}", {k: v for k, v in o.items() if k in ('rect1232',)})
            if o["_class"] == "CDrawTexte":
                print("    lines:", o["lines1128"], o["lines1184"], "s1528=", repr(o["s1528"]))
            for p in o.get("params", []):
                print("     param", repr(p["name"]), "logical=", repr(p["logical24"]), "type=", p["type32"])
