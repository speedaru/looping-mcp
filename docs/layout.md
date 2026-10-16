# Automatic layout

Agents never choose coordinates. `looping-cli layout`, the MCP tool `auto_layout` and `apply` (for the objects it
creates without `x`/`y`) arrange entities and associations so the diagram reads well. The algorithm is in
`core/src/layout.cpp` (`loo::arrangeGraph`); `Editor::autoLayout` connects it to a document.

## What it knows
- Boxes: entities use their stored size; an association's ellipse is estimated from its text (Looping computes the
  real one), so layout leaves a margin of `kLayoutGap` (40 px) between any two boxes.
- Links: one straight line between an entity and an association (Looping draws every link that way and does not
  route around boxes). An entity linked twice to one association counts once.
- Positions: an entity is placed by its top-left corner, an association by its centre. Boxes that are moved end up
  with their centre on a 10 px grid.

## Steps
1. **Pieces.** Connected parts of the graph are arranged separately and then packed in rows, so unrelated tables do
   not pull each other around. Pieces start at (60, 60).
2. **Force-directed placement** (Fruchterman-Reingold): boxes repel, linked boxes attract, a weak pull towards the
   centre keeps strays close. Several seeded starts are tried (30 for small models, fewer for large ones).
3. **Overlap removal**, then snapping to the grid.
4. **Local search**: each box is tried at nearby grid positions, and lined up with the row or column of a linked
   neighbour, as long as the cost drops.
5. The attempt with the lowest cost wins. Cost, from most to least important: boxes overlapping, links crossing,
   links running across a box they do not end in, links that are neither horizontal nor vertical, total link
   length, and a compact landscape shape.

The result is deterministic: the same schema always gives the same layout (fixed seeds, no clock), and running it
again on its own output changes nothing.

## Keeping a layout you like
- `layout FILE` / `auto_layout` without `only` re-arranges **everything**, replacing any hand layout.
- `--only NAME` / `only: [names]` places just those objects around the rest, which do not move. `apply` uses this for
  the objects it creates without coordinates, so extending an existing model never shuffles it.
- Objects given explicit `x`, `y` are never moved by `apply`.

## Tuning it yourself
All the knobs are constants in one block at the top of `core/include/loo/layout.hpp` ("Tuning"). Edit, then rebuild
(`cmake --build build`); no other change is needed, and the tests still apply.

| Constant | Effect | Default |
|---|---|---|
| `kLayoutGap` | Smallest empty space between two boxes, in px. Lower packs tighter. | 40 |
| `kLayoutSpread` | Typical distance between linked boxes, relative to box size. Lower is tighter, higher is airier. | 0.85 |
| `kLayoutLinkLength` | Cost per px of link length. Higher pulls linked boxes together, even at the price of a little less alignment. | 0.15 |
| `kAssociationMinWidth`, `kAssociationBaseWidth`, `kAssociationCharWidth` | Assumed width of an association ellipse: `max(min, base + char * longest text length)`. | 75, 32, 7 |
| `kAssociationHeight`, `kAssociationAttributeHeight` | Assumed height: `height + attributeHeight * attributes`. | 60, 17 |

Entity sizes are not tunable here: they are the sizes stored in the file (`Editor::addOwner` and the attribute
commands fit them, see `fitEntity` in `core/src/editor.cpp`). The cost weights (crossings, links through boxes,
slanted links, overall shape) are at the top of the "Cost terms" section of `core/src/layout.cpp`.

## Limits
- It is a heuristic. Typical models (up to a few dozen boxes) come out tidy; a dense graph can still have crossings.
- It takes about half a second for 80 boxes in a Release build (the build defaults to Release).
- Association sizes are estimates (measured on Looping at 100% zoom: about 75-90 px wide, 60 px high); adjust the
  `kAssociation*` constants if boxes look too close or too far apart.
