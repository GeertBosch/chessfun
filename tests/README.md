# mdcat tests

Hand-checked rendering samples, one Markdown file per feature. There is no
automatic pass/fail yet — render a file and compare against the description and
"Expected" notes that each file prints at the top of its own output.

Run a single test:

```
make && ./mdcat tests/tables-wrap.md
```

Run them all:

```
make && for f in tests/*.md; do printf '\n===== %s =====\n' "$f"; ./mdcat "$f"; done
```

## Automated checks

Three properties are checked automatically. Run them all via:

```
make check
```

`property-concat.sh` asserts that rendering distributes over the file-argument
list — for any files, `mdcat f1 f2 ... fn` produces exactly the concatenation of
`mdcat f1`, `mdcat f2`, ..., `mdcat fn`. This guards the file-boundary handling:
the last block of one file must never merge with the first block of the next.

`property-width.sh` asserts that the two ways of forcing the render width agree:
`mdcat --width N` and `COLUMNS=N mdcat` produce byte-identical output, checked at
one non-standard width. This guards the width-precedence logic.

`property-blank-lines.sh` asserts that the number of blank lines in the output is
invariant over the render width: blank lines come from block structure, so
wrapping must never add or drop one. Checked across a few widths.

Both run over every `*.md` in this directory and exit non-zero (printing a diff) on a
mismatch.

Several tests describe behavior at a specific terminal width. To check those
deterministically regardless of your real window size, force the width with the
`--width` flag (most reliable, beats everything):

```
make && ./mdcat --width 40 tests/tables-wrap.md
```

`$COLUMNS` also works and takes precedence over the detected terminal size, so
this is equivalent (export it or pass it inline; bash does not export it by
default):

```
make && COLUMNS=40 ./mdcat tests/tables-wrap.md
```

| File | Feature under test |
| ---- | ------------------ |
| `headings.md`        | ATX headings, levels 1-6, and their underlines |
| `inline.md`          | Emphasis, strong, code spans, links |
| `paragraphs.md`      | Paragraph reflow to the terminal width |
| `code-blocks.md`     | Fenced code blocks rendered verbatim |
| `code-indented.md`   | Indented (4-space) code blocks; not interrupting a paragraph |
| `thematic-breaks.md` | Horizontal rules |
| `tables-basic.md`    | A small multi-column table that fits |
| `tables-wrap.md`     | Multi-column tables that must wrap to fit the width |
| `tables-inline-wrap.md` | Inline styling wrapping inside cells, with no spill to adjacent cells |
| `images.md`          | `<img>` rendering and its text fallback |
| `images-aspect.md`   | Tall/square/wide images in one table row; row sized to tallest |
| `images-text-taller.md` | A text cell taller than its neighbor image; text drives row height |
| `images-narrow-col.md`  | Images scaled down to narrow columns; standalone-image regressions |
| `block-quotes.md`    | Block quotes: nesting, lazy continuation, inner blocks, left rule |
| `lists.md`           | Bullet/ordered lists: nesting, tight vs loose, multi-block items |
