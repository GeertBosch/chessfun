# TEST: thematic breaks

A line of three or more `-`, `*` or `_` (spaces allowed between) renders as a
full-width horizontal rule.

**Expected:** three full-width rules, one between each pair of paragraphs below.
The `---`, `***`, and `___` forms all produce the same rule. (The rule directly
under this heading is the level-1 heading underline, not a thematic break.)

---

Paragraph one.

---

Paragraph two.

***

Paragraph three.

___

Paragraph four.
