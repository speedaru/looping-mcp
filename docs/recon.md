# Looping.exe recon notes

Binary: `bin/Looping.exe` (x64, ~8 MB, statically linked MFC, MFC symbols present, app symbols stripped).
IDA image base `0x140000000`. Names below were applied in the IDA database (not yet saved to disk unless you save it).

## File format container
- `.loo` is an MFC `CArchive` stream (not compressed or encrypted).
- Objects are written as `ff ff <schema:u16> <len:u16> <ascii class name>` on first use, then back-referenced by tag.
- Strings: CArchive Unicode marker `ff fe ff <len>` + UTF-16LE.
- `CLoopingDoc` carries a file **version** field (`this+219*4`, current value written = **51**).
  Loader branches on `version >= N` for many fields, so older files are still read. We should always write 51.
- MCD / MLD / UML are views of the same document, not different formats.

## App classes (CRuntimeClass -> vtable -> Serialize, vtable slot 2)
| Class | GetRuntimeClass | vtable | Serialize |
|---|---|---|---|
| CLoopingDoc | 0x14006d050 | 0x1403d6788 | 0x140074c20 (verified: real doc Serialize) |
| CDrawObj (base) | 0x1400b2200 | 0x1403dd260 | 0x1400b2980 (721 lines decompiled) |
| CDrawEntite | 0x1400c3da0 | 0x1403dec60 | 0x1400c4210 |
| CDrawRelation | 0x1400d9f50 | 0x1403e1000 | 0x1400ddba0 |
| CDrawTexte | 0x1400e4900 | 0x1403e1468 | 0x1400e52c0 |
| CDrawTrait | 0x1400f0a10 | 0x1403e1d20 | 0x1400f0da0 |
| CDrawHeritage | 0x1400ceed0 | 0x1403df9b8 | 0x1400cf940 |
| CDrawCif | 0x1400b8340 | 0x1403dd8f0 | 0x1400b8b00 |
| CDrawContrainte | 0x1400bd440 | 0x1403ddd38 | 0x1400bd8b0 |
| CDrawRegle | 0x1400d8560 | 0x1403e0b98 | 0x1400d8710 |
| CDrawEntiteUML | 0x1400cdae0 | 0x1403df0c0 | 0x1400cdaf0 |
| CDrawArc | 0x140098570 | 0x1403daee0 | 0x140099af0 |
| CDrawArc{Cif,Contrainte,Heritage,HeritageUML,ObjetLibre,Regle} | see IDA | 0x1403db388.. | 0x1400a1450 (shared, inherited) |
| CDrawFleche / CDrawImage / CDrawProf / CDrawRectangle / CDrawEllipse / CDrawCrayon / CDrawTitre | see IDA | | |
| CRattach | 0x140103990 | 0x1403e5708 | 0x1401039e0 |

Other app classes: `CLoopingView`, `CMainFrame`, `CChildFrame`, `CUneVueUrbanisme`.

## Pitfalls hit during recon
- The first reference to a `CRuntimeClass` is the `AFX_CLASSINIT` static-init stub, not `GetRuntimeClass`.
  The real `GetRuntimeClass` is the later code reference that just returns the class pointer.
- `.rdata` references near `0x140362000-0x140362600` are the CRT init array, not vtables.

## Store-path extraction (first pass)
`tools/ida/extract_store.py` turns each `Serialize` into one line per written field, e.g. `CDrawObj` starts
`u32 this+320, u16 this+216, u16 this+228, ... raw this+40 len=16, raw this+120 len=12 (struct copy)`.
Output for all classes is in `docs/layouts/store_paths.txt`. Run it through the IDA MCP `execute_python` tool:
`exec(open(r"F:\coding\looping-mcp\tools\ida\extract_store.py").read()); print(store_layout(0x1400b2980, "CDrawObj"))`.

Caveats (this is a heuristic over decompiler text, not ground truth):
- Verified by eye only against `CDrawEntite` (its decompiled store path matches the extracted list).
  Nothing has been checked against the `.loo` corpus yet.
- Classes reported `shape=?` (`CLoopingDoc`, `CDrawArc`, `CDrawCif`, `CDrawEntiteUML`) had no load/store branch
  detected, so their output may mix paths or be incomplete.
- Computed values (bit-packed flags) show as `vN = <expression>`; the bit meanings still need working out.
- `CDrawFleche` and `CDrawTrait` share one `Serialize` (0x1400f0da0); the six `CDrawArc*` subclasses share 0x1400a1450.
- The sequence is the order of writes in each class; each derived class calls its base `Serialize` first.
- Offsets are `this+N` in the C++ object, not file offsets.

## Next
- Extract each class's write-path field sequence (offset, size, archive op) rather than reading raw pseudocode.
- Cross-check against `looping_file_examples/*.loo` by diffing.
