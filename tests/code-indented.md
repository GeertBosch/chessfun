# TEST: indented code blocks

An indented code block is a run of lines indented four or more columns (GFM
§4.4). It renders verbatim with the same gray panel as a fenced block, and the
four-column indent is stripped. Crucially it **cannot interrupt a paragraph**: an
indented line directly after a paragraph line is lazy continuation, not code.

**Expected:** the blocks below render as gray panels with their indentation
removed; inline markup like `*` and `_` inside them stays literal; and the
paragraph immediately followed by an indented line does NOT become code.

---

A plain paragraph, then a separate indented code block:

    int main() {
        return 0;
    }

Indentation deeper than four columns is preserved past the first four:

    line one
        line two is indented further
    line three

Interior blank lines are kept, trailing ones dropped:

    first

    after a blank line

Markup stays literal in code: *not italic*, _not italic_, `not a span`.

    these *asterisks* and _underscores_ are literal

This paragraph is immediately followed by an indented line, which GFM treats as
part of the paragraph (lazy continuation), so it must NOT render as a code block:
    this line is still the paragraph, reflowed in with the rest.

The end.
