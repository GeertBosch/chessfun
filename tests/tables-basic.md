# TEST: basic multi-column table

GFM table layout when nothing needs to wrap: the header row is bold with a
full-width rule under it, columns are separated by two spaces, and cells are
left-aligned and padded to their column width.

**Expected:** a clean three-column table at any normal width. The alignment
markers in the delimiter row are accepted but content is still left-aligned —
this renderer does not yet honor per-column alignment, so do not expect the
third column to be right-aligned.

---

| Piece  | Symbol | Value |
| :----- | :----: | ----: |
| Pawn   | P      | 1     |
| Knight | N      | 3     |
| Bishop | B      | 3     |
| Rook   | R      | 5     |
| Queen  | Q      | 9     |
