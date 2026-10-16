# Using looping-mcp in another project

This lets Claude Code (or any MCP client) create and edit Looping MCD schemas (`.loo` files) inside **your own
project**. You build the tool once, then register it where you want it.

## What you need
- **Windows 10/11, 64-bit** (the only platform tested).
- **Looping** itself, to open the files the agent produces. This project does not need `Looping.exe` to run.
- **Node.js 18 or newer** (tested with 24): `node --version`.
- **CMake 3.20+**, a **C++20 compiler** and **Ninja** (tested with MinGW-w64 g++ 13.2 and CMake 4.3): to build the
  command-line tool once.
- **Claude Code** with the `claude` command on your PATH.

## 1. Build once
Pick a permanent place for the tool (it stays there; projects point at it), for example `F:\tools\looping-mcp`:

```
git clone <this repository> F:\tools\looping-mcp
cd F:\tools\looping-mcp
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

All five tests should pass. The result is `build\cli\looping-cli.exe`, a single self-contained file. The MCP server
(`mcp\server.js`) finds it automatically at that location. If you build somewhere else, set the environment variable
`LOOPING_CLI` to the exe's full path (see step 2).

## 2. Register it in your project
Use the **absolute path** to `server.js`, with forward slashes. Pick one option.

**Option A, only for this project, private to you (recommended).** Run this from your project's folder:

```
claude mcp add looping -- node F:/tools/looping-mcp/mcp/server.js
```

**Option B, for every project on your machine:**

```
claude mcp add looping --scope user -- node F:/tools/looping-mcp/mcp/server.js
```

**Option C, shared with your team through git.** Put this in `.mcp.json` at the root of your project. The path is
specific to your machine, so teammates must adapt it; Claude Code asks each person to approve a project server once.

```json
{
  "mcpServers": {
    "looping": {
      "command": "node",
      "args": ["F:/tools/looping-mcp/mcp/server.js"]
    }
  }
}
```

If the exe is not in `build\cli`, add its location, e.g. `claude mcp add looping -e LOOPING_CLI=D:/bin/looping-cli.exe
-- node F:/tools/looping-mcp/mcp/server.js` (in `.mcp.json` add `"env": {"LOOPING_CLI": "D:/bin/looping-cli.exe"}`).

## 3. Check that it works
1. Start (or restart) `claude` in your project folder. A server added while a session is open only appears after you
   restart it or reconnect with `/mcp`.
2. For Option C, accept the prompt that asks you to approve the `looping` project server.
3. Run `claude mcp list` or `/mcp`: `looping` should show as **Connected**.
4. Ask Claude something like:

   > Using the looping tools, create `docs/model.loo` with entities `client` and `commande`, an association `passer`,
   > and give them identifiers. Link client 0,n and commande 1,1 to `passer`, then show me the MLD.

   Open `docs/model.loo` in Looping to see the MCD, and switch to the MLD view.

## 4. Working with it
- **Where files go.** Relative paths are relative to the folder Claude Code was started in. Missing folders are
  created. Only `.loo` paths are accepted.
- **Tools** (all prefixed `mcp__looping__` in Claude Code): `create_schema`, `show_schema`, `describe_schema`,
  `list_types`, `add_entity`, `add_association`, `rename_object`, `move_object`, `auto_layout`, `remove_object`, `set_comment`,
  `add_attribute`, `update_attribute`, `remove_attribute`, `add_link`, `set_cardinality`, `set_link_role`,
  `remove_link`, and `apply`.
- **Prefer `apply`** for building a whole model: it runs a list of operations in order and, if one fails, restores the
  file and says which operation failed.
- **`describe_schema`** is the way to look at the result: it prints the MCD and the MLD derived from it (tables, `PK`,
  `FK->table.column`) plus warnings. Ask the agent to call it after every batch of edits.
- **Rules that mirror Looping** (the tools return an explanatory error if you break them):
  - an association can have **only one `1,1` link**;
  - an association can have **attributes only if it has at least two `0,n`/`1,n` links**, so add the links first;
  - names must be unique across entities and associations; an attribute name is unique within its owner;
  - when an entity is linked twice to the same association (reflexive), pass `link` (e.g. `"Lien 2"`) to the tools
    that change one of those links.
- **Types:** `VARCHAR(n)`, `NVARCHAR(n)`, `VARBINARY(n)`, `CHAR(n)`, `NCHAR(n)`, `BINARY(n)`, `TEXT`, `NTEXT`, `BLOB`,
  `TINYINT`, `SMALLINT`, `INT`, `BIGINT`, `DECIMAL(digits,decimals)`, `FLOAT`, `DOUBLE`, `MONEY`, `COUNTER`, `DATE`,
  `TIME`, `DATETIME` (with `time_zone`), `BOOLEAN`, `XML`, `GEOGRAPHY`, `GEOMETRY`. The `N` types are the Unicode
  variants. Cardinalities are written `0,1`, `1,1`, `0,n`, `1,n` and read on the entity side.

### Habits that avoid trouble
- **Close the model in Looping while an agent edits it.** Looping does not reload a file that is open, and saving from
  Looping would overwrite the agent's changes. Reopen the file afterwards.
- **Keep the `.loo` files in git** (or copy them first). The tool writes the file atomically but does not keep a
  backup of the previous version; Looping itself keeps `.lo1` copies when it saves.
- Only entities, associations, links and attributes are supported. A file that contains anything else (inheritance,
  constraints, CIF, rules, sub-models, drawing shapes, images) is refused with an error instead of being modified.
- **Layout is automatic.** The agent does not need to choose coordinates: `apply` places the objects it creates
  (and leaves the ones already in the file where they are), and `auto_layout` re-arranges the whole diagram. Explicit
  `x`, `y` (pixels) are respected. Details in [layout.md](layout.md).

### A snippet for your project's `CLAUDE.md`
Adding this makes agents use the tools correctly without being told each time:

```markdown
## Database modelling (Looping)
The conceptual/logical model is `docs/model.loo`, edited with the `looping` MCP tools (never by hand).
- Build or change it with `apply` (several operations at once), then call `describe_schema` and check the MLD.
- Every entity needs an identifier attribute. Add the links of an association before giving it attributes
  (an association needs two `0,n`/`1,n` links to hold attributes); at most one `1,1` link per association.
- Do not choose coordinates: new objects are placed automatically; call `auto_layout` to tidy the whole diagram.
- Do not add foreign keys by hand: Looping derives them in the MLD.
- Ask me to close the file in Looping before you edit it.
```

## Updating and removing
- **Update:** `git pull` in the tool's folder, `cmake --build build`, then restart `claude`.
- **Remove:** `claude mcp remove looping` (add `--scope user` if you used Option B), and delete the folder if you wish.

## Troubleshooting
| Symptom | Fix |
|---|---|
| `looping` shows "Pending approval" | Start `claude` in the project and approve the server (Option C only). |
| `looping` is not listed after adding it | Restart `claude`, or run `/mcp` and reconnect. |
| `looping-cli not found at ...` (in the server's log) | Build the project (step 1) or set `LOOPING_CLI` to the exe. |
| `node` not found / server fails to start | Install Node.js 18+ and make sure `node` is on the PATH Claude Code uses. |
| A tool returns "cannot open" / "cannot write" | The path is wrong, or the file is locked by another program; close it in Looping. |
| "unsupported object class" | The file contains objects other than entities, associations and links (see above). |
| Looping does not show the agent's changes | Close and reopen the file in Looping. |
