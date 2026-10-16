# MLD derivation

Looping derives the MLD (logical model) from the MCD itself; there is nothing to store in the `.loo` file. To let an
agent *see* the MLD, `core/src/mld.cpp` re-implements the derivation, and `tests/mld_test.cpp` compares it with the
MLD that Looping actually displayed (screenshots supplied by the user). The `describe_schema` MCP tool (CLI:
`looping-cli describe FILE`) prints the MCD and this derived MLD as text.

## Rules implemented
1. Every entity is a table: attributes become columns, identifier attributes form the primary key.
2. An association with **exactly two links** where **at least one side has maximum cardinality 1** (`0,1` or `1,1`)
   produces **no table**. The foreign key goes into the entity of a max-1 side and points at the other entity:
   - one max-1 side: that entity holds the key;
   - both max-1: the `1,1` side holds it; if neither is `1,1`, the first link (lowest creation id) does;
   - the foreign key is NOT NULL when the holder's link is `1,1`;
   - such an association cannot have attributes (see below), so there is nothing to move.
3. Any other association (`n,n`, or three or more links) is a **table**: its primary key is the foreign keys of all its
   links, followed by its own attributes.
4. A foreign key repeats the referenced identifier's column name(s); on a clash with a column already in the table the
   name gets `_1`, `_2`, ... (also for an entity referencing itself).
5. A foreign key to a `COUNTER` column has type `INT`.
6. Links are taken in creation order; entities, then associations, in file order.

Looping allows only one `1,1` link per association, and an association can carry attributes only when at least two
of its links have cardinality `0,n` or `1,n` (it is then a table; otherwise it disappears from the MLD and the
attribute would have no place). The editor enforces both: it refuses `add_attribute` on an association without two n
links, and refuses a cardinality change, link removal or entity removal that would leave an association with
attributes and fewer than two n links.

## Verified against Looping (all match)
| Case | Source |
|---|---|
| `0,n` - `1,1`: foreign key in the `1,1` entity | `out/demo.loo` (client / commande) |
| `n,n` with an attribute on the association: association table, composite primary key | demo (`contenir`) |
| `0,1` - `0,1`: foreign key in the first entity | `different_associations.loo` (`asso1`) |
| `0,1` - `1,1`: foreign key in the `1,1` entity | `different_associations.loo` (`asso1_1`) |
| three `0,n` links: association table, clashing `id2` becomes `id2_1` | `different_associations.loo` (`asso1_2`) |
| one entity linked twice, `0,1` + `1,1`: foreign key to itself, named `id2_1` | `different_associations.loo` (`asso1_3`) |

13 tables in 2 files; the test fails if any table, column, primary key or foreign key differs.

## Not verified (assumptions; `describe_schema` warns for some of them)
- **`0,1` - `0,n`** and **`1,n` - `1,1`** placement (standard Merise, but no Looping sample).
- **Tie-break for `0,1` - `0,1`**: "first link". It matched the one sample, where the first link is also the entity
  created first and the left-most one.
- **Roles**: Looping's link dialog has "Suffixer clés étrangères" and "Renommer clé étrangère"; the role is **not**
  used in foreign key names here (the MLD warns when a link has a role).
- **Three or more links with a `1,1` link**: treated as an association table (warning).
- **Composite identifiers**, **`COUNTER` identifiers** (rule 5), foreign key **nullability** for `0,1`, and entities
  **without identifier** (the MLD warns and skips the key).
- Types of copied columns, `UNIQUE` on foreign keys, "Classe d'entités fictive" (not read), "Place clé étrangère dans
  clé primaire associée".

## If Looping and this derivation ever disagree
Options, cheapest first: (1) add the case to the corpus and fix the rule; (2) mark the case as a warning in the output;
(3) reverse-engineer Looping's own generator (it is in the binary: `COutputEditMLD`, `COutputEditSQL`,
`CDlgComplementScriptSQL`) and port it exactly. Option 3 is accurate but large; it is only worth it if the rules above
turn out to need many exceptions.
