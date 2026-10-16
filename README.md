# looping-mcp

Lets AI agents create and edit [Looping](https://www.looping-mcd.fr) MCD/MLD database schemas (`.loo` files)
through the Model Context Protocol. Looping derives the MLD from the MCD itself, so agents work on the MCD
(entities, associations, attributes, links with cardinalities) and read back the derived MLD as text.

```
AI agent --MCP--> mcp/server.js (Node) --> looping-cli (C++20) --> core library --> .loo file
```

- **Use it in your project:** [docs/install.md](docs/install.md)
- Tool and CLI reference: [docs/usage.md](docs/usage.md)
- How the MLD is derived and checked against Looping: [docs/mld.md](docs/mld.md)
- The reverse-engineered `.loo` format: [docs/format.md](docs/format.md)
- Status and open work: [docs/plan.md](docs/plan.md)

Scope: entities, associations, attributes and links. Other Looping objects (inheritance, constraints, CIF, rules,
sub-models, drawing shapes) are refused, not modified.

```
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
```

`Looping.exe` itself is not part of this repository and is not needed to run the tool.
