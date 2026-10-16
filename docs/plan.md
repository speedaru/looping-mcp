# Plan

Status legend: [x] done, [ ] todo.

## Phase 1: recon (in progress)
- [x] Identify framework (statically linked MFC), serialization (CArchive), file version (51).
- [x] Locate every `CDraw*` class, its vtable and `Serialize` (see `recon.md`).
- [x] Script to extract each class's write-path field sequence (offset, size, op): `tools/ida/extract_store.py`.
- [x] First dump of all 26 classes: `docs/layouts/store_paths.txt` (see caveats in `recon.md`).
- [ ] Verify the dump against the corpus (field order and sizes), and fix the classes the extractor handles badly
      (`CLoopingDoc`, `CDrawArc`, `CDrawCif`, `CDrawEntiteUML`, where no load/store branch was detected).
- [ ] Identify what each `this+offset` field means (name, position, colour, cardinality...) using the corpus diffs.
- [ ] Document the document header and the object list/back-reference scheme in `docs/format.md`.

## Phase 2: corpus (partly done)
Existing: `empty`, `empty_entity`, `entity_with_1_attribute`, `association_between_2_ents_w_0_attibs_0n_0n`, plus a
larger example. Still needed, each changing one thing:
- [ ] entity with 2 attributes, one marked as identifier
- [ ] same attribute with a different data type
- [ ] association cardinalities `0,1`, `1,1`, `1,n`
- [ ] association with an attribute
- [ ] renamed object
- [ ] moved object (to find the position bytes)
- (dropped from v1 by the user: inheritance, constraint, CIF, rule, sub-model)
- [x] Added 2026-10-16: entity with 2 attributes (one identifier), changed data type, cardinalities 0,1 / 1,1 / 1,n,
      association with an attribute. Each has a PNG of how Looping shows it; `looping_file_example.jpg` too.

## Phase 3: format spec and parser
- [ ] `docs/format.md`: header, version-dependent fields, per-class layouts.
- [ ] C++20 parser and writer in `core/`; always write version 51.
- [ ] Test: byte-exact round trip of every corpus file.

## Phase 4: generation and validation
- [ ] Build schemas from scratch in the core API (no template file).
- [ ] Manual check that Looping opens generated files.
- [ ] MCD to MLD derivation, checked against Looping's own MLD view and SQL export.

## Phase 5: CLI and MCP
- [ ] `looping-cli` commands over the core.
- [ ] MCP server and tool definitions.

## Later
- PNG preview via Looping's export.
- Live editing of the open window, only if asked for.

## Open decisions
- MCP server language (Python vs Node).
- Whether the corpus should be committed in full (the 54 KB example's origin is unknown).
- Whether to keep a copy of the IDA scripts in the repo (`tools/ida/`).
