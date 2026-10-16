# `.loo` file format (Looping, file version 51)

Status: **partial**. Everything marked *confirmed* was checked by parsing the corpus with `tools/loo_parse.py`
(the parse of the entity files ends exactly where expected). Everything marked *unknown* still needs work.

## Container
- MFC `CArchive` stream, little-endian, no compression.
- `[0, 2592)`: document header. **Identical in 12 of 13 corpus files**; `looping_file_example.loo` differs in
  2 bytes (`0x8a4`, `0x8a8`, probably a user setting). Writers can copy it verbatim from `empty.loo`.
- `[2592, ...)`: MFC `CObList` of drawing objects: `count`, then each object via `CArchive::WriteObject`.
- After the list: a trailer of per-object second-phase data (see "Trailer"). `empty.loo` has none.

## MFC primitives (confirmed)
- `count`: `u16`; if `0xFFFF` then a `u32` follows.
- `CString` (unicode): `ff fe ff`, length (`u8`; `0xFF` then `u16`; `0xFFFF` then `u32`), then UTF-16LE chars.
- Object tag (`u16`): `0` = null; `0xFFFF` = new class, followed by `u16 schema` (0), `u16 nameLen`, ASCII name;
  `0x8000 | n` = object of a class already seen (n = map index of that class); `n < 0x7FFF` = reference to an
  existing object (map index).
- Map indices start at 1 and increase for every new class and every new object, in stream order.
  Example (`empty_entity.loo`): 1=class CDrawEntite, 2=entity, 3=class CDrawTexte, 4/5/6=the three texts.
- `COleDateTime`: `u32 status` + `double` (OLE date), 12 bytes.

## Objects (version 51, store order; `fNNN` names are `this+NNN` in the C++ object)
### `CDrawObj` base (170 bytes, confirmed by length)
`u32 f320`, six `u16` (`f216 f228 f220 f236 f224 f244`), `RECT f40`(16), `struct12 f120`, `RECT f56`, `struct12 f152`,
`RECT f72`, `struct12 f184`, five `u32` (`f16 f776 f780 f784 f788 f792`: six values), three `u16` (`f232 f240 f248`),
three `struct12` (`f136 f168 f200`), `u32 f824`. Meaning of these fields: unknown.

### `CDrawEntite` (base + 18 bytes, confirmed)
`u16` flags (bit0 = `f1264`, bit1 = `f1268`), `RECT f1232` (left, top, right, bottom in pixels; the entity's box).
Two further `u32` (`f1236`, `f1244`) are written only when a document flag is set; not seen set in the corpus.

### `CDrawTexte` (confirmed for entity files)
base, `u32 f1112`, `u32 f1116`, `CStringList f1128` (count + CStrings = text lines), `CStringList f1184`,
`u32 f1240`, `CString f1528`, `u32 f1120`, `u32 0`, `u32 0`, 100+92+100+92 raw bytes (`f912 f916 f1012 f1016`),
`u16 f1244`, `u32 n`, then `n` `CUnParametre` records inline (no class tags).
An entity is 1 `CDrawEntite` + 3 `CDrawTexte`. In `empty_entity.loo` they are: an empty text, a text with one
empty line, and a text whose first line is the entity name (`['my_entity']`). Which text is which role is still
to be pinned down; their order in the list varies between files (it follows creation order).

### `CUnParametre` = one attribute ("rubrique") (confirmed, all fields in order)
`u32 d8` (**is identifier**), `CString name`, `u32 type` (**1 = INT, 0 = VARCHAR**; other types unknown),
`u32 d36`, `u32 d40`, `CString s72`, `CString s80`, `CString s88`, `u32 d96`, `CString logicalName`,
`CString s280`, `CString s312`, 12 x `u32` (`d320 d324 d332 d328 d44 d48 d52 d56 d132 d136 d140 d144`;
**d136 = NOT NULL, d140 = UNIQUE**), `CString s152`, `u64 q160`, `u32 d168`, `COleDateTime created`,
`COleDateTime modified`, `u32 d208`, 7 x `u32` (`d100..d128`; six of them are 50 by default, probably size
parameters), `u32 d212`, `u32 d216`, `CString s224/s232/s240`, `u32 t300`, `u32 t304`, `u32 d308`, `CString s248`,
`CString s256`, `u32 d264`, `u32 d268`, `u32 d272`, `u32 d276`, `CString s288`, `CString s64`, `u32 d296`.
Observed in the corpus: the attribute *comment* ("this attribute is an identifier of...") is stored somewhere in
here too (which string is unknown).

## Single-change results (attribute)
| Change in Looping | Bytes that changed |
|---|---|
| INT -> VARCHAR(50) | `type` 1 -> 0 (VARCHAR size 50 was already stored, so the size is another field) |
| mark as identifier | `d8` 0 -> 1, `d136` 0 -> 1 |
| tick NOT NULL and UNIQUE | `d136` 0 -> 1, `d140` 0 -> 1 |
| logical name `a_logical` | `logicalName` string |

### `CDrawRelation` = association (confirmed by length)
base, 8 raw bytes `f912` (**x, y as two `i32`: the association's position**), `u32 f920`, `u32 f928`, 8 raw bytes
`f932`, `u32 f940`, `u32 f924`, `u32 0`, `u32 0`, `CString` (written empty). Same 3 texts as an entity (name etc.).

### `CDrawArc` = one half of a link (confirmed by length)
base, `u32 f968`, `u32 f972`, `u32 f976`, `CString f984`. All zero / empty in every corpus file.

## Second pass (the "trailer", confirmed)
After the object list, for each object **in list order** the document calls the object's slot-3 method
(`CDrawObj` version at `0x1400b3840`, thunked by `CDrawEntite`/`CDrawTexte`, reused by `CDrawRelation`):
six `u32` object ids (`0` = none; ids are the `d16` field of the base): `ref328`, `ref336`, `ref344`, `chain864`,
`chain872`, `owner880`. `CDrawArc` additionally writes its `CRattach` member right after those six ids:
`u16`, 8 raw bytes, `u32 0` (14 bytes).
Meaning, from the corpus:
- An entity or association owns its 3 texts: `ref328/336/344` = text ids; the texts point back through `owner880`.
- Object ids (`d16`) are small integers; creation order gave 1,2,3,4 = entity + its 3 texts.

## Links (confirmed from the corpus)
A link between an entity E and an association A is **2 `CDrawArc` + 3 `CDrawTexte`**:
- Arc 1: `ref328/336/344` = the 3 texts, `chain872` = arc 2, `owner880` = **E's id**.
- Arc 2: `chain864` = arc 1, `owner880` = **A's id**.
- Texts (owned by arc 1): first = the link name (`Lien N`), then two texts whose line is `2`.
So the file contains the endpoints of each link, but see the cardinality question below.

## Parser status
`tools/loo_parse.py` (Python prototype) parses **all 14 corpus files and consumes every byte exactly**, including
`looping_file_example.loo` (86 objects). It keeps unknown fields as raw values, so it is also the basis for a
byte-exact round trip.

## Cardinality (confirmed with `link_base/01/11/1n.loo`)
The link's text whose line is `2` (there are two; only the one holding an attribute record matters) contains exactly
one `CUnParametre`. Its fields encode the cardinality of the link:

| Cardinality | `d216` | `d328` |
|---|---|---|
| 0,n (default) | 2 | 0 |
| 0,1 | 0 | 1 |
| 1,1 | 1 | 0 |
| 1,n | 3 | 0 |

`d216` follows the dialog's 0..3 option numbering (0,1 / 1,1 / 0,n / 1,n). `d328` is set only for 0,1; its meaning is
unknown, so writers should copy this table. The older `cardinalities 0,1, 1,1 and 1,n.loo` really does hold only
defaults; that file was saved before the changes (its link records equal the default ones).
Note: earlier I compared objects while skipping each text's attribute records, which is why cardinality looked
absent. A link whose second arc has no texts is valid (Looping creates `???` texts for it on load and then saves them).

## Attribute types (confirmed with `all_sql_types.loo`)
`type32`: 0 text, 1 numeric, 2 date/time, 3 boolean, 5 XML, 6 geographic, 7 geometric. (`Libre` was not in the file.)

| UI choice | fields |
|---|---|
| text, charset | `d40`: 0 ASCII (default), 1 Unicode, 2 Binary |
| text, kind | `d36`: 0 Fixe (CHAR), 1 Variable (VARCHAR, default), 2 Volumineux (TEXT) |
| numeric, subtype | `d44`: 0 Entier, 1 Décimal, 2 Réel, 3 Monétaire, 4 Compteur |
| integer size | `d48`: 0 = 8 bit, 1 = 16, 2 = 32 (default), 3 = 64 |
| real precision | `d52`: 0 = float32, otherwise double (default) |
| decimal | `d124` = digits in total (default 15), `d128` = digits after the decimal point (default 2) |
| date/time | `d56`: 0 Date, 1 Heure, 2 Date-Heure |
| time zone | `d96`: 1 = "avec fuseau horaire" (seen on Date-Heure; 0 otherwise) |
| boolean / xml / geographic / geometric | `type32` only |

### Text length (confirmed with `longueur.loo`)
One stored length per charset x kind, all 50 by default (`d124 = 15` and `d128 = 2` are not lengths):

| | Fixe | Variable |
|---|---|---|
| ASCII | `d100` | `d112` |
| Unicode | `d104` | `d116` |
| Binaire | `d108` | `d120` |

Looping labels both ASCII and Unicode variable text `VARCHAR(n)` in its attribute list; this project writes the
Unicode ones as `NVARCHAR` / `NCHAR` to keep them apart. In `longueur.loo` the first attribute also has a stray
`12` in `d100` (the fixed-ASCII slot); it did not affect the other lengths.

## Comments (confirmed with `comment_*.loo`)
- **Attribute comment** (the "Commentaire" box of the attribute dialog): `CUnParametre.s248`.
- **Entity comment** ("Commentaire" of the entity dialog): the **third text** of the entity (`refs[2]`, `w1244 = 2`);
  its text lines (`lines1128`) are the comment. Empty comment = no lines. Multi-line comments are written here as
  one line per text line (assumed from the structure; no multi-line sample yet).
- **Association comment**: same third text of the association (same structure; not verified with a file).
- Looping reorders objects in the list when it saves an edit, so two files with the same content can differ in
  byte order; compare content, not bytes.

## Link role (confirmed with `link_base.loo` / `link_role.loo`)
The link text that holds the cardinality record (record 0, `d212 = 1`) gets a **second record** when the link has a
role ("Libellé"): same fields as a default record but `d212 = 2` and the role text in `s240`. Removing the role
removes the record (the file is then byte-identical to one that never had a role). The three other controls of the
link dialog (Afficher, Suffixer clés étrangères, Renommer clé étrangère) and the foreign-key place spinner showed no
changed field when the role was typed with their default state, so they are stored elsewhere or as the default
value; each still needs its own single-change file.

## Open questions
- Collation, "Complément", the `Libre` type (skipped for now), the other link-dialog controls, and whether
  "avec fuseau horaire" also exists for `Heure`: not yet identified (see `plan.md`).
- `entity_with_1_attribute_decimal.loo` (`DECIMAL(8,3)`) and `date_heure_with/no_fuseau.loo` decoded the two
  new fields. Both date files carry the comment text `"avec fuseau" not checked` (left over from how they were made);
  only `d96` differs between them, so `d96` is taken as the time-zone flag.
- What most `CDrawObj` fields mean (colours, fonts, ...); and the position of entities (`rect1232` is the entity
  box, pixels) vs the base `RECT`s.
- Ordering and ids a writer must produce for new objects (map indices follow stream order; ids are the `d16`).
