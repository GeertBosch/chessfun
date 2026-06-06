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

Several tests describe behavior at a specific terminal width. To check those
deterministically regardless of your real window size, force a width with
`$COLUMNS` while piping (so the kernel window size doesn't win):

```
make && COLUMNS=40 ./mdcat tests/tables-wrap.md | cat
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
| `images.md`          | `<img>` rendering and its text fallback |
