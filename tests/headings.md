# TEST: ATX headings, levels 1-6

Each heading marker prints its text in bold followed by a full-width underline.
The underline glyph depends on the level: heavy for level 1, light for level 2,
densely dotted for level 3, dashed for level 4; levels 5-6 reuse the level-4
glyph. Inline markup inside a heading is still styled.

**Expected:** six bold heading lines below, each with a full-width rule beneath
it, the rule glyph changing by level. The final heading shows styled emphasis
and a code span.

---

# Level 1 heading
## Level 2 heading
### Level 3 heading
#### Level 4 heading
##### Level 5 heading
###### Level 6 heading

## Heading with *emphasis* and `code`
