# TEST: fenced code blocks

A fenced block is printed verbatim — no inline parsing, no reflow — on a
light-gray background padded to the width of the widest line, so it reads as a
solid rectangle. The info string after the opening fence is not shown.

**Expected:** the code below appears exactly as typed, including the stars,
backticks, and indentation, with NO emphasis or code styling applied to them
(they are literal inside a fence). Every line shares one background panel padded
to the longest line.

---

```cpp
int main() {
    // *not emphasis*, `not a code span` -- verbatim
    printf("hello, world\n");
    return 0;
}
```

A paragraph after the fence, to confirm the block ends cleanly.
