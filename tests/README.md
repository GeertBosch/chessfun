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

## Automated check

One property is checked automatically by `property-concat.sh`, run via:

```
make check
```

It asserts that rendering distributes over the file-argument list — for any
files, `mdcat f1 f2 ... fn` produces exactly the concatenation of `mdcat f1`,
`mdcat f2`, ..., `mdcat fn`. This guards the file-boundary handling: the last
block of one file must never merge with the first block of the next. It runs
over every `*.md` in this directory and exits non-zero (printing a diff) on a
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
| `thematic-breaks.md` | Horizontal rules |
| `tables-basic.md`    | A small multi-column table that fits |
| `tables-wrap.md`     | Multi-column tables that must wrap to fit the width |
| `tables-inline-wrap.md` | Inline styling wrapping inside cells, with no spill to adjacent cells |
| `images.md`          | `<img>` rendering and its text fallback |
