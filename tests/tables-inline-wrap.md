# TEST: inline styling wraps correctly inside table cells

Cells that contain inline markup — emphasis, strong, code spans, links — must
reflow by *visible* width, ignoring the ANSI escape sequences that carry the
styling. A styled run that crosses a wrap boundary should break like any other
text: its visible characters stay inside the column, and nothing spills past the
two-space separator into the neighboring column.

This is the property to check by eye: pick a narrow width so every cell wraps,
then confirm no styled text (no bold word, no gray code-span background, no link
text) reaches into an adjacent column. The code-span background, in particular,
must not paint across the gap between columns.

Run it narrow, where every cell is forced onto several lines:
`COLUMNS=44 ./mdcat tests/tables-inline-wrap.md | cat`. At a wide width the same
table should fit on single lines with the styling intact.

**Expected at a narrow width:** every column's content — including the bold,
italic, code, and link runs — stays within its column boundary; the right edge
of the left column and the left edge of the right column never overlap; a code
span that wraps keeps its gray background only under its own visible text.

## Two columns, every cell mixing styles that must wrap

| Term         | Note                                                                                  |
| ------------ | ------------------------------------------------------------------------------------- |
| `castling`   | A move where the **king** and a `rook` shift together in one *single* turn to safety. |
| en passant   | A *special* pawn capture written with `e.p.` notation, legal only right after a push.  |
| `fianchetto` | Developing a **bishop** to the long *diagonal* via b2/g2 or b7/g7, a `flexible` setup. |
| zugzwang     | A position where *any* move worsens things — see [Wikipedia](https://w.example/zz).    |

## Three columns so the styled runs are squeezed harder

| Move    | Idea                                            | Risk                                          |
| ------- | ----------------------------------------------- | --------------------------------------------- |
| `1.e4`  | Stake the **center** and free the *bishop* fast | Invites the sharp `Sicilian` with **counterplay** |
| `1.d4`  | A *solid*, closed game built on a `c2-c4` break | Can drift into a slow, **strategic** grind     |
| `1.Nf3` | A *flexible* move keeping the `d-pawn` at home   | Lets Black equalize with **accurate** replies  |
