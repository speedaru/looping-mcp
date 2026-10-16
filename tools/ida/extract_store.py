"""Extract the write ("store") path of an MFC Serialize function from Hex-Rays pseudocode.

Run inside IDA through the IDA MCP `execute_python` tool:

    exec(open(r"F:\\coding\\looping-mcp\\tools\\ida\\extract_store.py").read())
    print(store_layout(0x1400b2980, "CDrawObj"))

It rewrites the inlined CArchive operator<< boilerplate into one line per field:

    u32  this+320
    u16  this+216
    raw  this+848  len=4
    call sub_140024C10(this+840)        <- non-inlined archive call (strings, sub-objects)

Version / flag conditions found around the writes are kept (indented) so version-dependent
fields stay visible. Names are guesses by decompiler; offsets are the ground truth.
"""
import re

SIZES = {"_DWORD": "u32", "_WORD": "u16", "_BYTE": "u8", "_QWORD": "u64"}

_ASSIGN = re.compile(r"^\s*(v\d+) = (.+);\s*(//.*)?$")
_STORE = re.compile(r"^\s*\*\*\((_DWORD|_WORD|_BYTE|_QWORD) \*\*\)\((\w+) \+ 56\) = (.+);")
_WRITE = re.compile(r"CArchive::Write\(.*?a2: (.+?), a3: (\w+)\)")
_FLAGS = re.compile(r"\((\w+) \+ 32\) & 1\)")
_READ = re.compile(r"sub_14013DC10\(")  # CArchive::Read
_VFLAG = re.compile(r"^if \( \(v\d+ & 1\) [!=]= 0 \)")
_CALL = re.compile(r"\bsub_\w+\(|\w+__Serialize\(|\(\*\(")
_ASSIGN_PLAIN = re.compile(r"^v\d+ = [^(]*;$")


def _clean(expr, this):
    """`*(_DWORD *)(a1 + 320)` -> `this+320`, `a1` -> `this`."""
    expr = re.sub(r"\*\((?:_DWORD|_WORD|_BYTE|_QWORD|\w+ \*) \*\)\((\w+) \+ (\d+)\)", r"\1+\2", expr)
    expr = re.sub(rf"\b{this}\b", "this", expr)
    return expr.replace("this + ", "this+")


def _resolve(val, env, this):
    """Follow `vN` / `*vN` through simple temporaries until it names a this+offset (or give up)."""
    orig = val
    for _ in range(4):
        m = re.fullmatch(r"(\*?)(v\d+)", val.strip())
        if not m or m.group(2) not in env:
            break
        val = env[m.group(2)]
    val = _clean(val, this)
    if val != orig and not re.search(r"this\+\d+", val):
        return f"{orig} = {val}"  # computed value (bit packing etc.), keep the expression
    return val


_NOISE = (
    "AfxThrowArchiveException",
    "sub_14000DF50",  # CFile name getter used only for exception text
    "+ 64) )",  # buffer overflow check
    "sub_14013D890",  # CArchive::FillBuffer/flush
    "sub_14013D6DC",
    "~*(_DWORD *)(",
    "+ 56) = v",  # pointer advance
    "(_QWORD *)(",
)


def _block_end(lines, start):
    """Index of the line closing the brace block that opens right after lines[start]."""
    depth = 0
    for i in range(start, len(lines)):
        depth += lines[i].count("{") - lines[i].count("}")
        if i > start and depth <= 0:
            return i
    return len(lines) - 1


def _find_store_range(lines, ar):
    """Return (start, end, shape). Shape A: `if (!loading) {store}`. Shape B: `if (loading) {...}` then store."""
    for i, l in enumerate(lines):
        m = _FLAGS.search(l)
        if m and m.group(1) == ar and l.lstrip().startswith("if"):
            end = _block_end(lines, i)
            if "== 0" in l:
                return i + 1, end, "A"
            return end + 1, len(lines) - 1, "B"
    return 0, len(lines) - 1, "?"


def store_layout(ea, label="", ar="a2", this="a1", max_lines=400):
    text = "\n".join(db.pseudocode.decompile(ea).to_text()).splitlines()
    start, end, shape = _find_store_range(text, ar)
    env, out, cursors = {}, [], {}
    # Statements before the load/store split, typically the base-class Serialize call.
    flag_at = next((i for i, l in enumerate(text) if _FLAGS.search(l) and l.lstrip().startswith("if")), start)
    for l in text[:flag_at]:
        s = l.strip()
        if re.match(r"\w+__Serialize\(", s):
            out.append(f"call {s}")
    base_indent = None
    for l in text[start:end + 1]:
        if not l.strip():
            continue
        if base_indent is None:
            base_indent = len(l) - len(l.lstrip())
        indent = (len(l) - len(l.lstrip()) - base_indent) // 2
        pad = "  " * max(indent, 0)
        m = _ASSIGN.match(l)
        if m and f"({ar} + 56)" not in m.group(2):
            env[m.group(1)] = m.group(2)
        mc = re.match(rf"^\s*(\w+) = \*\((_DWORD|_WORD|_BYTE|_QWORD) \*\*\)\({ar} \+ 56\);", l)
        if mc:
            cursors[mc.group(1)] = mc.group(2)  # `result = *(_DWORD **)(a2 + 56)`: later `*result = x` is a store
        mk = re.match(r"^\s*\*(\w+) = (.+);", l)
        if mk and mk.group(1) in cursors:
            out.append(f"{pad}{SIZES[cursors[mk.group(1)]]:4s} {_resolve(mk.group(2), env, this)}")
            continue
        ms = _STORE.match(l)
        if ms and ms.group(2) == ar:
            val = _resolve(ms.group(3), env, this)
            out.append(f"{pad}{SIZES[ms.group(1)]:4s} {val}")
            continue
        mw = _WRITE.search(l)
        if mw:
            name = mw.group(1)
            ptr = _clean(env.get(name.lstrip("&"), name), this)
            if name.startswith("&") and name.lstrip("&") in env:
                ptr = f"{ptr}  (struct copy)"
            out.append(f"{pad}raw  {ptr} len={int(mw.group(2).rstrip('uU'), 0)}")
            continue
        s = l.strip()
        if any(n in l for n in _NOISE) or _READ.search(s):
            continue
        if s.startswith(("if", "else")):
            if _FLAGS.search(s) or _VFLAG.search(s) or s == "else":
                continue  # IsLoading/IsStoring ladders, not version conditions
            out.append(f"{pad}{_clean(s, this)}")
        elif _CALL.search(s) and ar in s and not _ASSIGN_PLAIN.match(s):
            out.append(f"{pad}call {_clean(s, this)}")
    head = f"== {label or hex(ea)} @ {ea:#x}  store-branch shape={shape}  lines={len(out)}"
    return head + "\n" + "\n".join(out[:max_lines])
