# Building and using the core and the CLI

## Build
Requires CMake >= 3.20 and a C++20 compiler (tested with MinGW g++ 13 and Ninja on Windows).

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```
Outputs: `build/cli/looping-cli.exe` (statically linked, one file) and the test programs.

## Tests
- `corpus_roundtrip`: every `.loo` in `looping_file_examples/` is read and written back; bytes must be identical.
- `editor`: reads the corpus through `Editor::schema()` and checks known facts (cardinalities, attribute types,
  identifier / NOT NULL / UNIQUE flags), builds a schema from scratch, edits it, saves and re-reads it.

## CLI
Every command prints one line of JSON; `{"ok":false,"error":"..."}` and exit code 1 on failure. Arguments are UTF-8
(French accents are fine). Files are replaced atomically (written to `FILE.tmp`, then renamed).

```
looping-cli new FILE.loo
looping-cli show FILE.loo
looping-cli describe FILE.loo                             # MCD + derived MLD as text (read-only)
looping-cli types
looping-cli add-entity FILE NAME [--x N --y N] [--comment TEXT]   # same for add-association
looping-cli set-comment FILE (--entity E | --association A) [--comment TEXT]   # no text clears it
looping-cli rename-entity FILE NAME NEWNAME               # same for rename-association
looping-cli move-entity FILE NAME X Y                     # same for move-association
looping-cli remove-entity FILE NAME                       # same for remove-association (removes its links too)
looping-cli add-attribute FILE (--entity E | --association A) --name N [--type T]
            [--identifier] [--not-null] [--unique] [--logical-name L] [--comment C]
looping-cli update-attribute FILE (--entity E | --association A) --attribute N [--name ..] [--type ..]
            [--identifier true|false] [--not-null true|false] [--unique true|false] [--logical-name ..] [--comment ..]
looping-cli remove-attribute FILE (--entity E | --association A) --attribute N
looping-cli add-link FILE ENTITY ASSOCIATION CARDINALITY [--role TEXT]   # CARDINALITY: 0,1  1,1  0,n  1,n
looping-cli set-cardinality FILE ENTITY ASSOCIATION CARDINALITY [--link NAME]
looping-cli set-link-role FILE ENTITY ASSOCIATION [ROLE] [--link NAME]
looping-cli remove-link FILE ENTITY ASSOCIATION [--link NAME]
# --link NAME (e.g. "Lien 2") is only needed when an entity is linked twice to the same association
```

Types: `VARCHAR NVARCHAR VARBINARY CHAR NCHAR BINARY TEXT NTEXT BLOB TINYINT SMALLINT INT BIGINT DECIMAL FLOAT DOUBLE
MONEY COUNTER DATE TIME DATETIME BOOLEAN XML GEOGRAPHY GEOMETRY`. Lengths and precision go in the type string:
`VARCHAR(255)`, `DECIMAL(10,2)`; a text type without a length uses Looping's default of 50. `DATETIME` accepts
`--time-zone`.

Rules enforced by the editor (they mirror Looping's own): an association can have attributes only when it has at least two
links with cardinality `0,n` or `1,n`, so add the links first; names are unique across entities and associations; an attribute name is unique within
its owner; a link joins exactly one entity and one association; the same pair may be linked more than once (reflexive
association), told apart by `--link`; an association can have only one `1,1` link.

## MCP server
`mcp/server.js` is a dependency-free Node script (stdio transport) that exposes the CLI to AI agents. It needs the
CLI to be built (`build/cli/looping-cli.exe`, or set `LOOPING_CLI`). The repository's `.mcp.json` registers it for
Claude Code as server `looping`; other clients need `command: node`, `args: [<path>/mcp/server.js]`.

Tools: `create_schema`, `show_schema`, `describe_schema` (MCD + derived MLD as text, see `docs/mld.md`), `list_types`, `add_entity`, `add_association`, `rename_object`, `move_object`,
`remove_object`, `set_comment`, `set_link_role`, `add_attribute`, `update_attribute`, `remove_attribute`, `add_link`, `set_cardinality`,
`remove_link`, and `apply` (a list of the editing tools run all-or-nothing: on any failure the file is restored).
Errors are returned as tool errors (`isError`), never as protocol errors, so the agent can read and correct them.
`node mcp/test.js` (also run by ctest as `mcp_server`) drives the server end to end.

## Layout of the code
- `core/include/loo/archive.hpp`: MFC `CArchive` primitives (one code path reads and writes).
- `core/include/loo/objects.hpp`: object model (`Entite`, `Texte`, `Relation`, `Arc`, `Param`).
- `core/include/loo/document.hpp`: whole-file read/write (header, object list, second pass).
- `core/include/loo/editor.hpp`: high-level editing API used by the CLI.
- `core/templates/*.loo`: real files from Looping, embedded at build time and cloned for new objects.
