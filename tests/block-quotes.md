# TEST: block quotes

Block quotes are a *container* block (GFM §5.1): a `>` marker on each line,
whose stripped contents are themselves a sequence of blocks rendered one level
in and drawn with a left rule.

**Expected:** each quoted region below shows a left bar down its full height,
including the blank lines separating inner blocks. Quoted paragraphs reflow to
the narrower inner width. Run at a couple of widths to confirm the bar stays
attached and the inner content wraps inside it:
`./mdcat --width 50 tests/block-quotes.md`.

---

A plain paragraph before the quote, to show separation.

> This is a single-paragraph block quote. Its source lines are deliberately
> broken at uneven points so that reflow has visible work to do inside the
> narrower column the left rule leaves.

A paragraph between two quotes.

> First paragraph inside the quote.
>
> Second paragraph inside the *same* quote, separated by a blank quoted line.
> The left rule should run continuously past that blank line.

Lazy continuation: only the first line carries the marker, but the paragraph
keeps going.

> A quoted paragraph whose marker appears on the first line only
and then continues on a line with no `>` of its own, which GFM still treats as
part of the quote.

Nested quotes:

> Outer quote text.
>
> > Inner quote, one level deeper, with its own rule drawn inside the outer one.
>
> Back to the outer level.

A quote containing other block types:

> ## A heading inside a quote
>
> A paragraph, then a fenced code block:
>
> ```
> printf("hello from inside a quote\n");
> ```
>
> - and inline `code` plus **bold** still render.

The end.
