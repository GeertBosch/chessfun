# TEST: tables that must wrap to fit the width

This is the primary case to check by eye. When the natural (unwrapped) width of
the columns exceeds the terminal width, each text column is capped at roughly
its fair share of the available space and its cells are reflowed onto multiple
lines. A row is as tall as its tallest cell; shorter cells are top-aligned and
padded with blank lines, and the two-space column separators stay aligned down
the whole table.

Check it deterministically at a narrow and a wide width:
`COLUMNS=40 ./mdcat tests/tables-wrap.md | cat` (cells wrap onto many lines)
versus `COLUMNS=120 ./mdcat tests/tables-wrap.md | cat` (fits on single lines).

**Expected at a narrow width:** every header and cell stays within its column
with no spill past the two-space separator; long cells wrap to multiple lines
with the row's columns lined up at the top; column widths are balanced so one
long column does not starve the others; and inline markup inside a cell survives
wrapping.

## Table 1 — three long text columns, all forced to wrap

| Opening          | Main idea                                                              | Typical plans |
| ---------------- | --------------------------------------------------------------------- | ------------- |
| Ruy Lopez        | White pins the knight defending e5 and builds a slow center.           | d2-d4 break, kingside attack, queenside minority play |
| Sicilian Defense | Black answers 1.e4 with c5, fighting for the center asymmetrically.    | ...d6/...e6 setups, ...d5 freeing break, opposite-side castling |
| French Defense   | Black plays ...e6 and ...d5, accepting a cramped but solid structure.  | ...c5 break, light-squared bishop trade, kingside pawn storms |

## Table 2 — one wide column beside two narrow ones

The long column should be capped at its fair share and wrapped while the short
columns stay compact.

| Move | Annotation                                                                                  | Eval |
| ---- | ------------------------------------------------------------------------------------------- | ---- |
| 1.e4 | The most popular first move, staking a claim in the center and freeing the bishop and queen. | +0.3 |
| 1.d4 | A solid alternative leading to closed, strategic positions with a different set of plans.    | +0.2 |

## Table 3 — four columns, tight budget

Several columns should wrap simultaneously and stay aligned.

| Phase   | Goal                          | Pieces                        | Common error                  |
| ------- | ----------------------------- | ----------------------------- | ----------------------------- |
| Opening | Develop quickly and control the center | Knights and bishops out early, then castle | Moving the same piece twice and neglecting development |
| Middle  | Create and exploit weaknesses  | Rooks to open files, queen active but safe | Launching an attack without enough force committed |
| Endgame | Promote a pawn or win material | King becomes an active fighting piece | Leaving the king passive on the back rank |
