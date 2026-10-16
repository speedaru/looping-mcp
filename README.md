# looping-mcp

Let AI agents build database schemas in [Looping](https://www.looping-mcd.fr), the French MCD/MLD design tool.

Looping is a Windows desktop app with no API. This project reverse-engineers its `.loo` file format and wraps it in
an [MCP](https://modelcontextprotocol.io) server. An agent creates entities, associations, attributes and links with
cardinalities; you open the resulting `.loo` file in Looping and see a normal MCD. Looping derives the MLD itself,
and the agent can read the same MLD back as text.

```
AI agent --MCP--> mcp/server.js (Node) --> looping-cli (C++20) --> core library --> .loo file
```

## What it can do

- Create a new `.loo` file, or edit an existing one.
- **Entities and associations:** add, rename, move, comment and remove them.
- **Attributes:** every Looping data type (`VARCHAR(255)`, `DECIMAL(10,2)`, `DATETIME` with time zone, `COUNTER`, ...),
  identifier, NOT NULL, UNIQUE, logical name and comment.
- **Links:** `0,1`, `1,1`, `0,n` and `1,n` cardinalities, link roles, reflexive associations.
- **`describe_schema`:** the MCD as text plus the MLD derived from it (tables, primary keys, foreign keys), checked
  against Looping's own output.
- **Automatic layout:** nobody picks coordinates. New objects are placed so boxes do not overlap, links are short,
  mostly horizontal or vertical, and cross as little as possible; `auto_layout` re-arranges a whole diagram.
- **`apply`:** a whole model in one call, all-or-nothing. If one operation fails, the file is restored.

It enforces Looping's own rules and returns a readable error when you break one: an association can have only one
`1,1` link, and it can carry attributes only when it has at least two `0,n`/`1,n` links.

**Scope:** entities, associations, attributes and links. A file that contains anything else (inheritance, constraints,
CIF, rules, sub-models, drawing shapes) is refused with an error, never modified.

## Quick start

You need Windows 10/11 (64-bit), Node.js 18+, CMake 3.20+, Ninja and a C++20 compiler. `Looping.exe` is not needed to
run the tool, only to open the files.

```
git clone <this repository> F:\tools\looping-mcp
cd F:\tools\looping-mcp
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Then register it with Claude Code from the project where you want the schemas:

```
claude mcp add looping -- node F:/tools/looping-mcp/mcp/server.js
```

Restart `claude`, check `looping` shows as connected in `/mcp`, and ask for something like:

> Using the looping tools, create `docs/model.loo` with entities `client` and `commande`, an association `passer`,
> give them identifiers, link client 0,n and commande 1,1 to `passer`, then show me the MLD.

Other ways to register it (every project, or shared through git), a `CLAUDE.md` snippet and troubleshooting are in
[docs/install.md](docs/install.md).

Close the model in Looping while an agent edits it. Looping does not reload an open file, and saving from Looping
would overwrite the agent's changes.

## Repository layout

| Path | What is in it |
|---|---|
| `core/` | C++20 static library `loo_core`: reads and writes `.loo` (MFC `CArchive`, file version 51), the editor API, MCD to MLD derivation. `core/templates/` holds real Looping objects, embedded at build time and cloned for new ones. |
| `cli/` | `looping-cli`, a command-line front end over the core. One line of JSON per command. |
| `mcp/` | `server.js`, the dependency-free Node MCP server that calls the CLI, and `test.js`, its end-to-end test. |
| `tests/` | Round-trip of the whole corpus, editor checks, MLD comparison with Looping's own output, and layout checks. |
| `looping_file_examples/` | The corpus: small `.loo` files (and some screenshots) made one feature at a time in Looping. Used as ground truth by the tests. |
| `tools/` | Python helpers used during reverse engineering: `loo_parse.py`, `loo_diff.py`, and `ida/extract_store.py` (run inside IDA). Not needed to build or use the tool. |
| `docs/` | Documentation, below. |

Git-ignored: `build/`, `out/` (generated demo files), `bin/` (`Looping.exe` and IDA databases, not redistributed) and
`.claude/` (personal Claude Code settings).

## Documentation

| Document | For |
|---|---|
| [docs/install.md](docs/install.md) | Using the tool in another project |
| [docs/usage.md](docs/usage.md) | Building, the CLI commands, the MCP tools, the code layout |
| [docs/mld.md](docs/mld.md) | How the MLD is derived, and what is verified against Looping |
| [docs/layout.md](docs/layout.md) | How the automatic layout works |
| [docs/format.md](docs/format.md) | The reverse-engineered `.loo` format |
| [docs/recon.md](docs/recon.md) | Notes on `Looping.exe` (classes, addresses) |
| [docs/project.md](docs/project.md), [docs/plan.md](docs/plan.md) | Goals, scope, status and open work |

## Tests

`ctest --test-dir build` runs five suites:

- `corpus_roundtrip`: every `.loo` in the corpus is read and written back byte for byte.
- `editor`: schema reading against known facts, building from scratch, editing, re-reading.
- `mld_matches_looping`: the derived MLD equals the MLD Looping shows for the supplied cases.
- `layout`: no overlaps, tidy links, deterministic output, pinned objects stay put.
- `mcp_server`: `mcp/test.js` drives the server over stdio like a real client.

## Known limits

- Windows only. The CLI is plain C++ and may build elsewhere, but only Windows is tested.
- Some MLD cases are not yet confirmed against Looping (for example roles in foreign key names and ternary
  associations with a `1,1` link); the list is in [docs/mld.md](docs/mld.md).
- Not supported: the `Libre` type, text collation and "Complément", the link dialog options (hide link, rename foreign
  key, custom min/max), SQL export, and PNG preview.
- The layout is a heuristic: tidy for typical models, not guaranteed crossing-free for dense ones. Looping draws
  every link as a straight line, so a layout cannot route around boxes.
- No backup is made before an edit (`apply` restores the file only when one of its own operations fails). Keep the
  `.loo` files in git.

## Disclaimer

Independent project, not affiliated with Looping or its author. This repository contains no Looping binary; the
format notes come from the files Looping writes and from studying the program.
