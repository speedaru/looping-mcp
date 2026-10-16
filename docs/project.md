# looping-mcp: project overview

## What this is
Looping (https://www.looping-mcd.fr) is a Windows desktop tool for drawing MCD (conceptual) and MLD (logical)
database schemas. This project makes it possible for AI agents to create and edit those schemas through an MCP server.

## Goals
1. An MCP server whose tools let an agent build and modify a Looping schema (entities, attributes, associations,
   cardinalities) and read the derived MLD.
2. Output is a real `.loo` file that Looping opens and displays correctly.
3. A standalone, testable core library, independent of the GUI and of IDA.

## v1 scope (decided by the user)
Only three MCD element kinds: **entities** (with attributes), **associations** (with attributes), and the **links**
(chains of `CDrawArc` objects, carrying the cardinality) that connect an entity to an association, plus the derived MLD.
Out of scope: inheritance (Héritage), constraints (Contrainte), CIF, rules (Règle), sub-models (Sous-modèle),
and the cosmetic tools (text, lines, shapes, images, break "Cassure"). These can be added later.
Files that contain them are rejected with a clear error instead of being modified.

## Non-goals (for v1)
- Controlling the live Looping window (DLL injection, UI automation).
- PNG preview rendering. Looping has a PNG export we could drive after a build; deferred to a later version.
- Redistributing Looping or any of its code. The binary stays out of git.

## Architecture
```
AI agent --MCP--> MCP server (thin wrapper) --CLI args--> looping-cli (C++20) --> core library --> .loo file
```
- `core/`: C++20 library. Reads, writes and validates `.loo` (MFC CArchive stream, file version 51), plus MCD to MLD
  derivation rules.
- `looping-cli`: command line front end over the core, the only thing the MCP server talks to.
- MCP server: `mcp/server.js`, a dependency-free Node script exposing tools such as `add_entity`, `add_attribute`,
  `add_link`, `set_cardinality`, `describe_schema` and the all-or-nothing `apply`.
- Build system: CMake.

## Method
Reverse engineering with IDA (via the IDA MCP plugin) to learn each class's field layout, cross-checked against a
corpus of small `.loo` files made one feature at a time in Looping. Success is judged by byte-exact round trips of
every corpus file and by Looping opening files we generate.

## Repository layout
See the README. `bin/` (git-ignored) holds `Looping.exe` and IDA database files; it is only needed for further
reverse engineering.
