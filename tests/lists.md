# TEST: lists

Lists are a *container* block (GFM §5.2–5.3). Bullet markers (`-`, `+`, `*`) all
render as a depth-varying glyph (• ◦ ▪, matching GitHub's disc/circle/square);
ordered markers keep the author's number with a `.`. Item bodies hang under the
marker, reflowing to the narrower inner width. Tight lists pack their items;
loose lists (any blank line between or within items) get blank lines between them.

**Expected:** run at a couple of widths, e.g.
`./mdcat --width 50 tests/lists.md`, and confirm bodies wrap under their marker
and nested bullets change glyph by depth.

---

A tight bullet list:

- First item
- Second item, whose text is long enough that at a narrow width it must wrap and
  the continuation should hang under the marker, not under the bullet glyph.
- Third item

A tight ordered list, starting at 3:

3. Third
4. Fourth
5. Fifth

Nested lists (bullets change glyph by depth):

- Top level one
  - Second level
    - Third level
  - Back to second
- Top level two
  1. Ordered nested under a bullet
  2. Second ordered

A loose list (blank lines between items):

- First loose item.

- Second loose item, with its own paragraph that wraps when the width is narrow
  enough to force it onto more than one line.

- Third loose item.

A list item containing multiple blocks:

- An item with a paragraph, then a fenced code block:

  ```
  printf("inside a list item\n");
  ```

  and a second paragraph after the code, still inside the item.
- A second item to show the list continues.

Markup inside items works: **bold**, *italic*, `code`, and [links](https://x).

- One with `inline code` and **strong** text.
- [A link item](https://example.com).

The end.
