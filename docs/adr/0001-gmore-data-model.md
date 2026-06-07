# ADR 0001 — gmore input data model: cell grid, not line spans

- Status: Accepted
- Date: 2026-06-07
- Component: `gmore` (the graphics-aware pager)

## Context

`gmore` emulates a *pragmatic subset* of a terminal (decision: option C) so it can
page **any** text+sixel stream with **no constraints on the producer** — crucially
including cursor movement: mdcat places images with `CSI A/B` + `DECSC/DECRC`,
progress bars rewrite in place, etc. We must choose how to store the parsed
screen + scrollback that the parser writes into and the pager renders from.

Two candidates:

- **(B) Line spans** — each line is a list of runs `{text, attr}` plus image refs;
  SGR state carried across lines. (Your "list of attribute changes.")
- **(C) Cell grid** — a 2-D array of `Cell{codepoint, attr-id}` that grows downward
  (= the scrollback buffer); sixels held in a separate image layer anchored to a
  cell.

## Decision

**Cell grid, with attribute interning and a bounded scrollback.**

```c++
struct Cell {            // 8 bytes
    char32_t cp;         // 4: Unicode code point (' ' = blank; 0 = wide-char tail)
    uint16_t attr;       // 2: index into an interned attribute table
    uint8_t  width;      // 1: 1 or 2 (wide)        }
    uint8_t  flags;      // 1: e.g. image-anchor    } (fold together later if desired)
};
struct Attr { uint32_t fg, bg; uint16_t flags; };  // bold/italic/underline/inverse…
// interned: vector<Attr> + hash<Attr→id>; a document uses only a handful of combos.
```

## Rationale (with evidence)

1. **Decisive — we are emulating a terminal, and a terminal *is* a cell grid.**
   Cursor addressing is cell addressing; output may overwrite any cell at any time.
   In a grid that write is `grid[r][c] = cell`: O(1), obviously correct. In a span
   list the same write must: find the column by summing the *display widths* of
   preceding runs (wide/zero-width aware), split the run at that column, replace it,
   and possibly merge neighbors. That column-offset + split/merge arithmetic is the
   "fragility" — the home of off-by-one and wide-char bugs — and it sits on the hot
   path of cursor emulation, the very reason we chose option C. The model must make
   random-access writes trivial; the grid does, spans don't. Every production
   terminal emulator (xterm, vte, iTerm2, notcurses) uses a cell grid for exactly
   this reason. Spans model append-only styled text (a static renderer), not a
   writable screen.

2. **Memory is bounded and modest.** With attribute interning a `Cell` is ~8 bytes
   and the attr table holds only the few distinct `{fg,bg,flags}` a document uses —
   so the "95% of cells match their neighbor" observation costs essentially nothing
   extra. Scrollback is **capped** (default ~10k rows, configurable) and input is
   read **lazily**, so total memory is bounded *regardless of input size*:
   `10,000 rows × 200 cols × 8 B ≈ 16 MB` worst case; a typical screen is trivial.
   Spans would be ~4–8× leaner for plain text (text stored once), but cannot offer
   O(1) overwrites — and the cap makes the grid's overhead a non-issue.

3. **Speed is a wash for our operations.** Parsing is O(input bytes) and inherently
   per-character either way: we must scan every byte for `ESC`/CSI/DCS, UTF-8
   boundaries, and wrap points, so we can't skip bytes. Stamping a cell is O(1).
   Rendering touches only the visible window (O(screen cells)) and run-length-
   collapses SGR on output. The hoped-for "process per span" speedup does not
   materialize because the scan cannot be amortized away.

## Consequences

- Wide chars occupy two cells (trailing cell marked); combining marks deferred (YAGNI).
- Sixels live in a **separate image layer**: `{anchorRow, anchorCol, decoded raster}`.
  Rendering composites images over the text grid and slices strips (height =
  `ceil(cellH/6)*6`, queried at runtime) for images the window cuts through.
- A scrollback cap is required (flag/env); the lazy reader pulls more input on
  page-down. This is also what bounds memory for pipes/infinite streams.
- Rendering re-derives minimal SGR from per-cell attrs (run-length), so each window
  edge is self-contained — no color bleed across the top/bottom of the view.

## Hyperlinks (OSC 8) — planned, requires no rewrite

An OSC 8 hyperlink (`ESC ] 8 ; params ; URI ST` … text … `ESC ] 8 ; ; ST`) is just
another cell attribute whose value happens to be a URI string. The interned-attr
model absorbs it additively:

- Extend `Attr` with `uint32 linkId` (index into an interned URI table; 0 = none).
- Parser: on the OSC 8 open set `pen.linkId = intern(uri)`, on the empty-URI close
  set it back to 0; stamped cells carry it via their already-interned `Attr` (no
  per-cell string storage).
- Render: emit OSC 8 open/close on `linkId` change, run-length like SGR, so each
  window edge re-opens the link and stays self-contained.

**v1 obligation (even though it ignores links):** the parser MUST recognize and
**skip** OSC sequences — consume `ESC ] … (BEL | ST)` as a zero-width no-op — so
link *text* is not garbled. Adding `linkId` later is then purely additive.
(This is the reason the model rejects packing attributes into a too-tight int:
a real interned `Attr` struct has room for `linkId`; a packed int would not.)
