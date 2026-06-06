# TEST: paragraph reflow

A paragraph's source lines are gathered and re-wrapped to the current terminal
width, so the input's hard line breaks are ignored and only a blank line starts
a new paragraph.

**Expected:** the first sample paragraph below has deliberately uneven source
lines; rendered, it should fill the terminal width rather than echoing those
short input lines. Run it at two widths and confirm the wrapping differs:
`COLUMNS=40 ./mdcat tests/paragraphs.md | cat` versus `COLUMNS=80 ...`. The
later paragraphs are separated by blank lines and must stay distinct.

---

This is a single paragraph whose
source lines are deliberately broken
at uneven
points so that reflow has visible work to do; when rendered it should fill the
terminal width rather than echoing these short input lines verbatim.

A second, separate paragraph. Inline markup like *emphasis* survives reflow and
should not break the wrapping math even when it falls near a line boundary.

A third paragraph, kept short.
