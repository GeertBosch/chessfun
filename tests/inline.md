# TEST: inline styling

This file exercises the inline parser: emphasis (`*x*` / `_x_`) to italic,
strong (`**x**`) to bold, code spans (backtick x backtick) to a light-gray
panel, and `[text](url)` to an OSC 8 hyperlink whose visible text is the label
and whose target is the URL.

**Expected:** each paragraph below renders with the marked spans styled and the
markup characters gone. Unmatched delimiters stay literal. The link line shows
the word Anthropic as a clickable hyperlink (terminal permitting), not the raw
URL.

---

This sentence has *emphasis* and also _emphasis with underscores_.

This sentence has **strong emphasis** and a `code span` in it.

Combined: ***bold italic***, **bold with `code` inside**, and a trailing
asterisk * that is not a delimiter.

A link to [Anthropic](https://www.anthropic.com) inside a paragraph.
