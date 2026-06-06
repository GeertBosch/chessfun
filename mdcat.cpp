// mdcat - render a GitHub Flavored Markdown subset to an ANSI terminal.
//
// This is a C++17 port of the earlier markfun.sh. Rather than rewriting text with regular
// expressions, it parses the input in two phases that mirror the GFM specification
// (https://github.github.com/gfm):
//
//   1. Block parsing splits the document into a sequence of blocks: ATX headings, thematic
//      breaks, fenced and indented code blocks, GFM tables, and paragraphs (the leaf blocks),
//      plus the container blocks — block quotes and lists — whose stripped contents are rendered
//      recursively (a quote gets a left rule; a list item gets a bullet/number marker and a
//      hanging indent). Blank lines separate blocks; the lines of a paragraph are gathered
//      together so they can be reflowed.
//   2. Inline parsing turns the raw text of headings, paragraphs and table cells into styled
//      runs: emphasis (*x* / _x_), strong emphasis (**x**), code spans (`x`) and links
//      ([text](url)). Code-fence contents are emitted verbatim, not inline-parsed.
//
// Styling is applied with ANSI escape sequences; link targets use OSC 8 hyperlinks. Widths used
// for table layout and paragraph wrapping are measured in display columns, ignoring escapes.
//
// Only the elements the original script dealt with are implemented, made more solid and more
// conformant. As an exception to the "no inline HTML" rule, a paragraph or table cell that
// consists solely of an <img ...> tag pointing at a local PNG is rendered as an actual image by
// shelling out to timg(1) on terminals that support sixel graphics; elsewhere (e.g. Apple's
// Terminal.app) it falls back to the tag's alt text or filename. See renderImageBlock. All other
// inline HTML is passed through literally.
//
// Build:  c++ -std=c++17 -O2 -o mdcat mdcat.cpp
// Usage:  mdcat [--width N] [--] [file ...]   (reads standard input when given no file arguments)
//         --width N forces the render width, overriding $COLUMNS and the terminal size.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// ANSI / OSC helpers
// ---------------------------------------------------------------------------

constexpr char kEsc = '\033';

const std::string kBoldOn = "\033[1m";
const std::string kBoldOff = "\033[22m";
const std::string kItalicOn = "\033[3m";
const std::string kItalicOff = "\033[23m";
const std::string kCodeOn = "\033[48;5;255;38;5;236m";  // light-gray bg, dark-gray fg
const std::string kCodeOff = "\033[39;49m";
const std::string kReset = "\033[0m";
const std::string kLightGray = "\033[38;5;250m";
const std::string kQuoteBar = "\033[38;5;244m▎\033[0m ";  // the left rule drawn before quoted lines

// An explicit width from the --width/-w command-line flag, or <= 0 if none was given. Set by main()
// before any rendering, and so before terminalWidth() is first called and caches its result.
int gWidthOverride = 0;

// Columns currently consumed by container-block decorations (the block-quote left rule, a list
// item's marker indent). Every nested container raises it by the width of its prefix while its
// content is being rendered, so the width that paragraphs reflow to and that tables and rules fill
// shrinks to match the space the prefix leaves. Restored on the way out. See emitBlockQuote /
// emitList / terminalWidth.
int gIndent = 0;

// Current bullet-list nesting depth, used to pick the bullet glyph (• ◦ ▪ by depth, like GitHub's
// disc/circle/square). Raised while a bullet list's items render and restored after. Ordered lists
// don't change it — their marker is the number, not a depth-varying glyph. See emitList.
int gListDepth = 0;

// Set just before rendering the content of a *tight* list item. A tight list draws no blank line
// between its items, and likewise its item bodies should not put a blank line between the item's lead
// paragraph and an immediately-following nested list (GFM renders a tight item's paragraph without
// <p> spacing). render() reads this once at entry to suppress its own top-level block separators,
// then clears it so blocks nested deeper (and recursive renders) separate normally. See emitList.
bool gTightItem = false;

// The full terminal width, before any container indentation is taken out. Determined once and cached
// (see the comment in terminalWidth). Callers want terminalWidth(), which subtracts the indent.
int fullTerminalWidth() {
    // Determined once and cached for the rest of the run: the width is fixed for our purposes and
    // there is no reason to repeat the syscall for every block and every reflowed line.
    //
    // Precedence, most explicit first:
    //   1. --width / -w on the command line (gWidthOverride): an unconditional override.
    //   2. $COLUMNS, when set to a positive value: lets the caller force a width even when a terminal
    //      is attached (e.g. `COLUMNS=44 mdcat ... | less`). Note bash does not export COLUMNS by
    //      default, so it must be exported or passed inline to reach us.
    //   3. The kernel window size of whichever standard stream is a terminal.
    //   4. A fixed default when none of the above applies (e.g. fully piped with no COLUMNS).
    static const int width = [] {
        if (gWidthOverride > 0) return gWidthOverride;
        if (const char* cols = std::getenv("COLUMNS")) {
            int w = std::atoi(cols);
            if (w > 0) return w;
        }
        for (int fd : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
            struct winsize ws;
            if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return static_cast<int>(ws.ws_col);
        }
        return 100;  // sensible default when not attached to a terminal
    }();
    return width;
}

// The width content should be laid out to: the terminal width less the columns taken by any
// surrounding container-block decoration (gIndent). At least one column so layout never collapses.
int terminalWidth() {
    return std::max(1, fullTerminalWidth() - gIndent);
}

// Whether the terminal can display inline images (sixel graphics, which timg emits with -ps).
// Determined once and cached. Many terminals, including Apple's Terminal.app, cannot render sixel;
// painting a sixel block there leaves garbage on screen, so on those terminals an <img> falls back
// to its alt text or filename instead (see renderImageBlock).
//
// There is no portable capability query that works without round-tripping an escape to the terminal
// and reading the reply, which is fragile. Instead we use $TERM_PROGRAM, which terminal emulators
// set to identify themselves, to exclude the ones known not to support sixel; Apple_Terminal is the
// motivating case. When output is not a terminal at all, there is likewise nothing to draw to.
bool terminalSupportsGraphics() {
    static const bool supported = [] {
        if (!isatty(STDOUT_FILENO)) return false;
        if (const char* prog = std::getenv("TERM_PROGRAM")) {
            if (std::string(prog) == "Apple_Terminal") return false;
        }
        return true;
    }();
    return supported;
}

// Metrics for converting an image's pixel footprint (read from the sixel it produces) into a number
// of text rows/columns to reserve. A single cell is often a fractional number of pixels (e.g. ~5.83
// px wide), so rounding it to an integer before dividing mis-counts: rounding up under-counts the
// columns the terminal actually uses (image overflows its reserved space), rounding down over-counts
// (leaves a gap). We therefore keep the text-area pixel size and cell count as a ratio and do the
// pixel->cell conversion exactly, matching how the terminal lays a sixel out. `cellW`/`cellH` are an
// integer fallback used only when the area ratio is unavailable.
struct CellMetrics {
    int areaW = 0, areaH = 0;   // text-area size in pixels (0 if unknown)
    int cols = 0, rows = 0;     // text-area size in cells (0 if unknown)
    int cellW = 8, cellH = 16;  // integer px/cell fallback

    // ceil-divide pixels to the cell count the terminal will use, via the precise area ratio when
    // known (px * cells / areaPx, rounded up) and the integer cell size otherwise.
    int pxToCols(int px) const {
        if (cols > 0 && areaW > 0) return std::max(1, (px * cols + areaW - 1) / areaW);
        return std::max(1, (px + cellW - 1) / cellW);
    }
    int pxToRows(int px) const {
        if (rows > 0 && areaH > 0) return std::max(1, (px * rows + areaH - 1) / areaH);
        return std::max(1, (px + cellH - 1) / cellH);
    }
};

// Kept for the CSI 16 t cell-size query reply.
struct CellSize { int w; int h; };

// Ask the terminal for its cell size with the CSI 16 t report: it replies "ESC [ 6 ; H ; W t".
// Done over /dev/tty (not stdout) so it works even when our output is piped to a pager, and in raw
// mode with a short timeout so the reply is not echoed, line-buffered, or able to hang us. Returns
// {0,0} if there is no controlling terminal or it doesn't answer.
CellSize queryCellSize16t() {
    int fd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if (fd < 0) return {0, 0};
    struct termios saved {};
    if (tcgetattr(fd, &saved) != 0) { close(fd); return {0, 0}; }
    struct termios raw = saved;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 2;  // 0.2s between bytes before giving up
    tcsetattr(fd, TCSANOW, &raw);
    static const char query[] = "\033[16t";
    CellSize cs{0, 0};
    if (write(fd, query, sizeof query - 1) == static_cast<ssize_t>(sizeof query - 1)) {
        std::string reply;
        char c;
        // Read until the report's terminating 't' (or the inter-byte timeout). The reply is short.
        while (reply.size() < 32) {
            ssize_t n = read(fd, &c, 1);
            if (n <= 0) break;
            reply += c;
            if (c == 't') break;
        }
        // Parse "ESC [ 6 ; <height> ; <width> t". sscanf skips the ESC/[ for us via the literal.
        int h = 0, w = 0;
        if (std::sscanf(reply.c_str(), "\033[6;%d;%dt", &h, &w) == 2 && w > 0 && h > 0)
            cs = {w, h};
    }
    tcsetattr(fd, TCSANOW, &saved);
    close(fd);
    return cs;
}

// Cached cell metrics for the run. Preference order: the kernel's text-area pixels + cell counts
// (TIOCGWINSZ ws_xpixel/ws_ypixel with ws_col/ws_row) — this gives the precise area ratio used for
// exact pixel->cell conversion; then the CSI 16 t cell-size query as an integer fallback; then a
// typical default. Only consulted when an image is actually rendered, so non-image documents never
// pay for the query.
CellMetrics cellMetrics() {
    static const CellMetrics m = [] {
        for (int fd : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
            struct winsize ws;
            if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 && ws.ws_ypixel > 0 &&
                ws.ws_col > 0 && ws.ws_row > 0) {
                CellMetrics c;
                c.areaW = ws.ws_xpixel;
                c.areaH = ws.ws_ypixel;
                c.cols = ws.ws_col;
                c.rows = ws.ws_row;
                c.cellW = std::max(1, ws.ws_xpixel / ws.ws_col);
                c.cellH = std::max(1, ws.ws_ypixel / ws.ws_row);
                return c;
            }
        }
        CellSize q = queryCellSize16t();
        CellMetrics c;
        if (q.w > 0 && q.h > 0) { c.cellW = q.w; c.cellH = q.h; }  // area ratio stays unknown
        return c;  // else the {8,16} struct defaults
    }();
    if (std::getenv("MDCAT_DEBUG_CELL"))
        std::fprintf(stderr, "mdcat: cell metrics: area=%dx%d px, cells=%dx%d, cell~%dx%d px\n",
                     m.areaW, m.areaH, m.cols, m.rows, m.cellW, m.cellH);
    return m;
}

// ---------------------------------------------------------------------------
// UTF-8 / display-width utilities
// ---------------------------------------------------------------------------

// Number of bytes in the UTF-8 sequence whose lead byte is c (1 for invalid lead bytes).
int utf8SequenceLength(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x06) return 2;
    if ((c >> 4) == 0x0E) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

// Display width of a string in terminal columns, skipping ANSI escape (CSI) sequences and OSC 8
// hyperlink sequences so that styled text measures by what is actually shown. Each UTF-8 code
// point counts as one column; this matches the original script's behaviour and is sufficient for
// the Latin text, box-drawing characters and chess glyphs used here.
int displayWidth(const std::string& s) {
    int width = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == kEsc && i + 1 < s.size() && s[i + 1] == '[') {
            // CSI sequence: ESC [ ... final-byte in @..~
            i += 2;
            while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) ++i;
            if (i < s.size()) ++i;  // consume the final byte
            continue;
        }
        if (c == kEsc && i + 1 < s.size() && s[i + 1] == ']') {
            // OSC sequence: ESC ] ... terminated by BEL or ST (ESC backslash)
            i += 2;
            while (i < s.size()) {
                if (static_cast<unsigned char>(s[i]) == 0x07) { ++i; break; }
                if (s[i] == kEsc && i + 1 < s.size() && s[i + 1] == '\\') { i += 2; break; }
                ++i;
            }
            continue;
        }
        i += utf8SequenceLength(c);
        ++width;
    }
    return width;
}

// Append n copies of the box-drawing horizontal line character to out.
std::string horizontalLine(int n, const std::string& bar = "─") {
    std::string out;
    for (int i = 0; i < n; ++i) out += bar;
    return out;
}

std::string padTo(const std::string& s, int width) {
    int w = displayWidth(s);
    std::string out = s;
    for (int i = w; i < width; ++i) out += ' ';
    return out;
}

// Trim ASCII spaces and tabs from both ends.
std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

// ---------------------------------------------------------------------------
// Inline parsing
// ---------------------------------------------------------------------------

// Render markdown inline content from raw text into ANSI-styled output.
//
// The renderer runs in two linear passes so that its cost is O(n) in the input length, with no
// recursion or whole-buffer copying:
//
//   Pass 1 (tokenize): scan left to right producing a flat list of tokens. Backslash escapes,
//   code spans and links are resolved immediately (they don't nest emphasis the way emphasis
//   nests them, and their bodies are short). Every run of '*' or '_' becomes a Delim token whose
//   open/close capability is decided from the characters flanking it.
//
//   Pass 2 (resolve emphasis): walk the tokens with a stack of open delimiters, matching each
//   closer to the nearest compatible opener (the standard GFM algorithm). Matched pairs are
//   marked as strong or emphasis; unmatched delimiters stay literal. A final pass emits text.
//
// Emphasis uses a practical subset of the GFM flanking rules: a delimiter run can open if it is
// not followed by whitespace and can close if it is not preceded by whitespace; '_' additionally
// may not open/close inside a word, so snake_case is left intact. '**' is strong, '*'/'_' is
// emphasis; a run of length >= 2 can serve as either by consuming two or one of its characters.
class InlineRenderer {
public:
    explicit InlineRenderer(const std::string& text) : s_(text) {}

    std::string render() {
        tokenize();
        resolveEmphasis();
        return emit();
    }

private:
    const std::string& s_;

    enum class Kind { Text, Code, Link, Delim };
    struct Token {
        Kind kind;
        std::string text;   // Text: literal; Code: content; Link: rendered link text
        std::string url;    // Link: target URL
        // Delim fields:
        char delim = 0;     // '*' or '_'
        int count = 0;      // remaining usable delimiter characters
        bool canOpen = false;
        bool canClose = false;
        // Emphasis resolution result, applied at emit time:
        int openStrong = 0, openEm = 0;    // styles opened just before this token's text
        int closeStrong = 0, closeEm = 0;  // styles closed just after
    };
    std::vector<Token> toks_;

    static bool isAsciiPunct(char c) {
        return std::string("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~").find(c) != std::string::npos;
    }
    static bool isWordChar(unsigned char c) { return std::isalnum(c) || c == '_'; }
    static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n'; }

    char charAt(size_t i) const { return i < s_.size() ? s_[i] : '\n'; }
    char prevAt(size_t i) const { return i == 0 ? '\n' : s_[i - 1]; }

    void addText(const std::string& t) {
        if (!toks_.empty() && toks_.back().kind == Kind::Text) toks_.back().text += t;
        else { Token tk; tk.kind = Kind::Text; tk.text = t; toks_.push_back(std::move(tk)); }
    }

    // --- Pass 1 -------------------------------------------------------------
    void tokenize() {
        size_t n = s_.size();
        for (size_t i = 0; i < n;) {
            char c = s_[i];
            if (c == '\\' && i + 1 < n && isAsciiPunct(s_[i + 1])) {
                addText(std::string(1, s_[i + 1]));
                i += 2;
                continue;
            }
            if (c == '`') { if (scanCode(i)) continue; }
            if (c == '[') { if (scanLink(i)) continue; }
            if (c == '*' || c == '_') { scanDelim(i); continue; }
            int len = utf8SequenceLength(static_cast<unsigned char>(c));
            addText(s_.substr(i, len));
            i += len;
        }
    }

    // `code`: backtick run closed by an equal-length run; content emitted verbatim. Advances i.
    bool scanCode(size_t& i) {
        size_t open = 0;
        while (i + open < s_.size() && s_[i + open] == '`') ++open;
        size_t j = i + open;
        while (j < s_.size()) {
            if (s_[j] == '`') {
                size_t run = 0;
                while (j + run < s_.size() && s_[j + run] == '`') ++run;
                if (run == open) {
                    Token tk; tk.kind = Kind::Code;
                    tk.text = s_.substr(i + open, j - (i + open));
                    toks_.push_back(std::move(tk));
                    i = j + run;
                    return true;
                }
                j += run;
            } else ++j;
        }
        return false;  // unmatched: fall through and treat the backticks as text
    }

    // [text](url): link text is inline-rendered by a *separate* renderer over just that slice,
    // which is cheap because link text is short and contains no further links to recurse on here.
    bool scanLink(size_t& i) {
        // Find the matching ']' for the opening '[' (no nested brackets expected in our inputs).
        size_t close = i + 1;
        int depth = 1;
        while (close < s_.size()) {
            if (s_[close] == '\\' && close + 1 < s_.size()) { close += 2; continue; }
            if (s_[close] == '[') ++depth;
            else if (s_[close] == ']') { if (--depth == 0) break; }
            ++close;
        }
        if (close >= s_.size() || close + 1 >= s_.size() || s_[close + 1] != '(') return false;
        size_t up = close + 2;
        std::string url;
        int pd = 1;
        while (up < s_.size()) {
            char c = s_[up];
            if (c == '\\' && up + 1 < s_.size()) { url += s_[up + 1]; up += 2; continue; }
            if (c == '(') ++pd;
            else if (c == ')') { if (--pd == 0) break; }
            url += c;
            ++up;
        }
        if (up >= s_.size() || s_[up] != ')') return false;
        Token tk; tk.kind = Kind::Link;
        tk.text = InlineRenderer(s_.substr(i + 1, close - (i + 1))).render();
        tk.url = trim(url);
        toks_.push_back(std::move(tk));
        i = up + 1;
        return true;
    }

    // A maximal run of identical '*' or '_'. Decide open/close capability from flanking chars.
    void scanDelim(size_t& i) {
        char d = s_[i];
        size_t run = 0;
        while (i + run < s_.size() && s_[i + run] == d) ++run;
        char before = prevAt(i);
        char after = charAt(i + run);
        bool spaceBefore = isSpace(before), spaceAfter = isSpace(after);
        Token tk; tk.kind = Kind::Delim; tk.delim = d; tk.count = static_cast<int>(run);
        tk.canOpen = !spaceAfter;
        tk.canClose = !spaceBefore;
        if (d == '_') {
            // Intraword underscores neither open nor close (keeps snake_case intact).
            if (isWordChar(static_cast<unsigned char>(before))) tk.canOpen = false;
            if (isWordChar(static_cast<unsigned char>(after))) tk.canClose = false;
        }
        toks_.push_back(std::move(tk));
        i += run;
    }

    // --- Pass 2 -------------------------------------------------------------
    void resolveEmphasis() {
        std::vector<size_t> stack;  // indices of Delim tokens that can still open
        for (size_t idx = 0; idx < toks_.size(); ++idx) {
            Token& t = toks_[idx];
            if (t.kind != Kind::Delim) continue;
            if (t.canClose) {
                // Try to match against the nearest compatible opener on the stack.
                while (t.count > 0 && !stack.empty()) {
                    bool matched = false;
                    for (size_t k = stack.size(); k-- > 0;) {
                        Token& o = toks_[stack[k]];
                        if (o.delim == t.delim && o.canOpen && o.count > 0) {
                            int use = (t.count >= 2 && o.count >= 2) ? 2 : 1;
                            if (use == 2) { o.openStrong++; t.closeStrong++; }
                            else { o.openEm++; t.closeEm++; }
                            o.count -= use;
                            t.count -= use;
                            if (o.count == 0) stack.resize(k);  // opener spent; drop it and any above
                            matched = true;
                            break;
                        }
                    }
                    if (!matched) break;
                }
            }
            if (t.canOpen && t.count > 0) stack.push_back(idx);
        }
    }

    // --- Emit ---------------------------------------------------------------
    std::string emit() {
        std::string out;
        for (const Token& t : toks_) {
            switch (t.kind) {
                case Kind::Text:
                    out += t.text;
                    break;
                case Kind::Code:
                    out += kCodeOn + t.text + kCodeOff;
                    break;
                case Kind::Link:
                    out += "\033]8;;" + t.url + "\033\\" + t.text + "\033]8;;\033\\";
                    break;
                case Kind::Delim: {
                    // Closers emit their off-codes first, then any opens, then leftover literals.
                    for (int k = 0; k < t.closeEm; ++k) out += kItalicOff;
                    for (int k = 0; k < t.closeStrong; ++k) out += kBoldOff;
                    for (int k = 0; k < t.openStrong; ++k) out += kBoldOn;
                    for (int k = 0; k < t.openEm; ++k) out += kItalicOn;
                    // Any delimiter characters not consumed by a pair stay as literal text.
                    out.append(static_cast<size_t>(t.count), t.delim);
                    break;
                }
            }
        }
        return out;
    }
};

std::string renderInline(const std::string& text) {
    return InlineRenderer(text).render();
}

// ---------------------------------------------------------------------------
// Image rendering: <img ...> -> timg
// ---------------------------------------------------------------------------
//
// As an exception to the "inline HTML is literal" rule, a paragraph or table cell whose entire
// content is a single <img ...> tag referring to a local PNG is rendered as a real image. We read
// the PNG's intrinsic pixel size from its fixed header, decide a target size in character cells,
// and let timg(1) paint it with sixel graphics. If anything goes wrong (not a lone PNG <img>, file
// unreadable, timg missing or failing) we fall back to the tag's alt text, or the literal tag.

// Read the pixel dimensions of a PNG from its 8-byte signature + IHDR chunk. The layout is fixed:
// bytes 0..7 are the PNG signature, then a length+type, then IHDR data beginning at byte 16 with
// big-endian uint32 width (offset 16) and height (offset 20). Returns false if `path` is not a
// readable PNG. No image data is decoded — only the 24-byte header is touched.
bool readPngSize(const std::string& path, int& width, int& height) {
    static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    unsigned char h[24];
    if (!f.read(reinterpret_cast<char*>(h), sizeof h)) return false;
    for (int i = 0; i < 8; ++i)
        if (h[i] != kSig[i]) return false;
    // The IHDR chunk must follow the signature; its type tag sits at bytes 12..15.
    if (h[12] != 'I' || h[13] != 'H' || h[14] != 'D' || h[15] != 'R') return false;
    auto be32 = [](const unsigned char* p) {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    };
    uint32_t w = be32(h + 16), ht = be32(h + 20);
    if (w == 0 || ht == 0) return false;
    width = static_cast<int>(w);
    height = static_cast<int>(ht);
    return true;
}

// Whitespace test usable on signed char without the locale surprises of std::isspace.
inline bool isSpaceCh(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Parse a single HTML start tag of the form <img key="value" key2="value2" ...> into a map of
// attribute names (lowercased) to values. Only the simple quoted-value form described by the task
// is recognised. Returns false if `text` (already trimmed) is not exactly one <img ...> tag with
// nothing before or after it. Values may be single- or double-quoted.
bool parseImgTag(const std::string& text, std::map<std::string, std::string>& attrs) {
    const std::string& s = text;
    size_t i = 0, n = s.size();
    if (n < 5 || s[0] != '<') return false;
    // Tag name must be "img" (case-insensitive), followed by whitespace or '>'.
    if (!(std::tolower(s[1]) == 'i' && std::tolower(s[2]) == 'm' && std::tolower(s[3]) == 'g'))
        return false;
    if (!(isSpaceCh(s[4]) || s[4] == '>' || s[4] == '/')) return false;
    i = 4;
    while (i < n) {
        while (i < n && isSpaceCh(s[i])) ++i;
        if (i < n && s[i] == '/') ++i;          // tolerate a self-closing slash
        while (i < n && isSpaceCh(s[i])) ++i;
        if (i >= n) return false;               // no closing '>'
        if (s[i] == '>') return i == n - 1;     // the tag must end the string
        // attribute name
        size_t ks = i;
        while (i < n && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '_'))
            ++i;
        if (i == ks) return false;              // expected a name
        std::string key = s.substr(ks, i - ks);
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        while (i < n && isSpaceCh(s[i])) ++i;
        std::string value;
        if (i < n && s[i] == '=') {
            ++i;
            while (i < n && isSpaceCh(s[i])) ++i;
            if (i < n && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                size_t vs = i;
                while (i < n && s[i] != q) ++i;
                if (i >= n) return false;        // unterminated quote
                value = s.substr(vs, i - vs);
                ++i;                             // consume closing quote
            } else {                             // unquoted value up to whitespace or '>'
                size_t vs = i;
                while (i < n && !isSpaceCh(s[i]) && s[i] != '>') ++i;
                value = s.substr(vs, i - vs);
            }
        }
        attrs[key] = value;
    }
    return false;  // ran off the end without a closing '>'
}

// Run timg to render `path` at the given -g geometry with sixel graphics, returning its stdout.
// `geom` is a timg geometry string in character cells: "WxH" (a full box), "Wx" (width only, height
// derived from the aspect ratio) or "xH" (height only, width derived). Partial geometry lets a
// single requested dimension govern, with the other following the true aspect ratio, instead of our
// own rounding double-constraining the box. Returns an empty string if timg cannot be launched or
// exits non-zero.
//
// The image data is captured (rather than letting timg draw straight to the terminal) for two
// reasons: with its stdout on a pipe timg emits plain sixel without doing its own cursor/scroll
// positioning, so we can later replay the bytes at whatever cursor position we choose; and we can
// read the painted pixel size out of the sixel to compute an exact cell footprint. timg still
// detects the real cell size via /dev/tty, so `-g` is honoured in actual character cells.
std::string runTimg(const std::string& path, const std::string& geom) {
    std::ostringstream cmd;
    // -ps : sixel pixelation;  -g <geom> : fit inside the given character-cell box.  The path is
    // passed single-quoted with embedded single quotes escaped, so odd filenames stay safe.
    std::string quoted = "'";
    for (char c : path) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += "'";
    cmd << "timg -ps -g" << geom << " " << quoted << " 2>/dev/null";
    FILE* p = popen(cmd.str().c_str(), "r");
    if (!p) return std::string();
    std::string out;
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, got);
    int rc = pclose(p);
    if (rc != 0) return std::string();
    return out;
}

// Read the painted pixel size out of a sixel data stream's raster attributes. A sixel begins
// ESC P <params> q "Pan;Pad;Ph;Pv ... where Ph/Pv are the graphic's pixel width/height. We scan for
// the '"' that introduces them and parse the last two of the four numbers. Returns false if absent
// (older sixel without raster attributes), in which case callers fall back to the requested size.
bool sixelPixelSize(const std::string& sixel, int& pw, int& ph) {
    size_t q = sixel.find("\033P");
    if (q == std::string::npos) return false;
    size_t quote = sixel.find('"', q);
    if (quote == std::string::npos) return false;
    int pan = 0, pad = 0, w = 0, h = 0;
    if (std::sscanf(sixel.c_str() + quote + 1, "%d;%d;%d;%d", &pan, &pad, &w, &h) == 4 && w > 0 &&
        h > 0) {
        pw = w;
        ph = h;
        return true;
    }
    return false;
}

// If `text` (already trimmed) is a single <img> tag for a local PNG, render it and return true,
// writing the raw sixel bytes to `out` and the image's exact cell footprint to `cellWidth` /
// `cellHeight` (columns / rows). Otherwise return false and leave the outputs untouched.
// `availWidth` caps the width in columns. On any failure to produce an image, the alt text (or
// literal tag) is returned as a one-line text block (cellHeight == 1) so callers can treat the
// result uniformly.
//
// `out` is the plain sixel as timg produced it — it does NOT position itself. The caller is
// responsible for placing the cursor before replaying it: timg leaves the cursor at column 1 on the
// row just below the image, so a sixel painted at the top of a reserved block lands within it. The
// footprint is computed from the painted pixel size read back out of the sixel (which reflects
// timg's aspect-preserving fit), divided by the real cell size — not from the requested -g box.
bool renderImageBlock(const std::string& text, int availWidth, std::string& out, int& cellWidth,
                      int& cellHeight) {
    std::map<std::string, std::string> attrs;
    if (!parseImgTag(text, attrs)) return false;

    auto srcIt = attrs.find("src");

    // Fall back to a one-line text block when an image can't (or shouldn't) be drawn: the alt text if
    // present, else the source filename, else the literal tag. Used both for error cases and for
    // terminals that cannot display graphics at all.
    auto fallback = [&](const std::string& reason) {
        (void)reason;
        auto altIt = attrs.find("alt");
        std::string label;
        if (altIt != attrs.end() && !altIt->second.empty()) label = altIt->second;
        else if (srcIt != attrs.end() && !srcIt->second.empty()) label = srcIt->second;
        else label = text;
        out = renderInline(label);
        cellWidth = displayWidth(out);
        cellHeight = 1;
        return true;
    };

    // On a terminal that can't render sixel graphics, show the alt text or filename instead.
    if (!terminalSupportsGraphics()) return fallback("no graphics support");

    if (srcIt == attrs.end() || srcIt->second.empty()) return fallback("no src");
    const std::string& src = srcIt->second;

    int pw = 0, ph = 0;
    if (!readPngSize(src, pw, ph)) return fallback("not a readable PNG");

    // Target pixel size: an explicit width/height overrides the intrinsic size. With only one of the
    // two given, the other is derived to preserve the aspect ratio; with both, the ratio is free.
    auto attrInt = [&](const char* key, int& dst) {
        auto it = attrs.find(key);
        if (it == attrs.end()) return false;
        int v = std::atoi(it->second.c_str());
        if (v <= 0) return false;
        dst = v;
        return true;
    };
    int tw = pw, th = ph;
    int aw = 0, ah = 0;
    bool haveW = attrInt("width", aw), haveH = attrInt("height", ah);
    if (haveW && haveH) { tw = aw; th = ah; }
    else if (haveW)     { tw = aw; th = std::max(1, ph * aw / pw); }
    else if (haveH)     { th = ah; tw = std::max(1, pw * ah / ph); }

    // Convert the target pixel size to character cells (timg detects the real cell size itself, so a
    // -g in cells is honoured). Then pick a SINGLE governing dimension so the other follows the true
    // aspect ratio rather than our own rounding double-constraining the box:
    //   - too wide for the available columns -> width-bound ("<availWidth>x");
    //   - an explicit height with no explicit width -> height-bound ("x<rows>");
    //   - otherwise a full box ("<cols>x<rows>") from both target dimensions.
    // The exact footprint is then read back from the painted sixel, which reflects timg's fit.
    CellMetrics cell = cellMetrics();
    int wCells = cell.pxToCols(tw);
    int hCells = cell.pxToRows(th);
    std::string geom;
    if (availWidth > 0 && wCells > availWidth)
        geom = std::to_string(availWidth) + "x";          // width-bound: clamp to the column
    else if (haveH && !haveW)
        geom = "x" + std::to_string(hCells);              // height-bound: width follows aspect
    else
        geom = std::to_string(wCells) + "x" + std::to_string(hCells);

    std::string img = runTimg(src, geom);
    if (img.empty()) return fallback("timg failed");

    // Exact footprint from the painted pixel size (aspect-fit may differ from the requested box),
    // converted with the precise area ratio so the reserved cells match what the terminal uses.
    // Fall back to the requested cells if the sixel lacks raster attributes.
    int paintedW = 0, paintedH = 0;
    if (sixelPixelSize(img, paintedW, paintedH)) {
        cellWidth = cell.pxToCols(paintedW);
        cellHeight = cell.pxToRows(paintedH);
    } else {
        cellWidth = wCells;
        cellHeight = hCells;
    }
    out = img;
    return true;
}

// Whether `s` is sixel image data rather than a text fallback: a sixel contains a DCS introducer
// (ESC P), which rendered inline text never does (it uses only ESC[ for SGR and ESC] for OSC 8).
bool isSixelImage(const std::string& s) { return s.find("\033P") != std::string::npos; }

// Emit a captured sixel `image` (exactly `rows` tall) as a standalone block at the left margin,
// leaving the cursor at column 1 on the line just below it. Scroll-safe and needs no cursor report:
// print `rows` newlines to reserve the band (this scrolls if we are near the screen bottom, making
// room first), move the cursor back up `rows` lines (relative, so unaffected by any scroll), then
// replay the sixel. timg's bytes paint at the current cursor and leave it at column 1 on the row
// below the image; with the band sized to the image, that is the band bottom — ready for the next
// block, just like a text paragraph's trailing newline.
void emitImageParagraph(const std::string& image, int rows, std::ostream& out) {
    out << std::string(static_cast<size_t>(rows), '\n');  // reserve the band (may scroll)
    out << "\033[" << rows << 'A';                         // back to the band's top row, column 1
    out << image;                                          // paint; cursor ends at the band bottom
}

// ---------------------------------------------------------------------------
// Block model
// ---------------------------------------------------------------------------

// Length in bytes of the next display "unit" at offset i: a whole ANSI CSI escape, a whole OSC 8
// hyperlink escape, or a single UTF-8 code point. Escapes are kept intact so they are never split
// across a line break and so they don't count toward the display width.
size_t unitLength(const std::string& s, size_t i) {
    char c = s[i];
    if (c == kEsc && i + 1 < s.size() && s[i + 1] == '[') {  // CSI: ESC [ ... final @..~
        size_t j = i + 2;
        while (j < s.size() && !(s[j] >= '@' && s[j] <= '~')) ++j;
        if (j < s.size()) ++j;
        return j - i;
    }
    if (c == kEsc && i + 1 < s.size() && s[i + 1] == ']') {  // OSC: ESC ] ... BEL or ST
        size_t j = i + 2;
        while (j < s.size()) {
            if (static_cast<unsigned char>(s[j]) == 0x07) { ++j; break; }
            if (s[j] == kEsc && j + 1 < s.size() && s[j + 1] == '\\') { j += 2; break; }
            ++j;
        }
        return j - i;
    }
    return static_cast<size_t>(utf8SequenceLength(static_cast<unsigned char>(c)));
}

// Keep ANSI styling from bleeding across the line breaks that reflow (or table layout) introduces.
// A code span carries a background colour (kCodeOn .. kCodeOff); if a span wraps, the line would end
// with the background still on and paint gray to the right edge of the terminal. Walk the text line
// by line, tracking whether a code span is open at each '\n': close it before the break and reopen
// it after, so each line is self-contained and no background extends past its text.
std::string closeStylesAtLineBreaks(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool inCode = false;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\n') {
            if (inCode) out += kCodeOff;      // close the span at end of line
            out += '\n';
            if (inCode) out += kCodeOn;        // reopen it at the start of the next line
            ++i;
            continue;
        }
        size_t len = unitLength(s, i);
        if (s.compare(i, kCodeOn.size(), kCodeOn) == 0) inCode = true;
        else if (s.compare(i, kCodeOff.size(), kCodeOff) == 0) inCode = false;
        out.append(s, i, len);
        i += len;
    }
    return out;
}

// Reflow a rendered (ANSI-styled) string so that no output line exceeds `width` display columns,
// breaking on spaces between words. A word longer than `width` on its own is hard-cut at exactly
// `width` columns and continued on the next line. ANSI escapes don't count toward the width and
// are never split. Line breaks in the result are '\n'; the result has no trailing newline.
std::string reflow(const std::string& s, int width) {
    if (width < 1) width = 1;

    // Split into whitespace-separated words; escapes travel with the word they are attached to.
    std::vector<std::string> words;
    std::string cur;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == ' ') {
            if (!cur.empty()) { words.push_back(cur); cur.clear(); }
            ++i;
        } else {
            size_t len = unitLength(s, i);
            cur.append(s, i, len);
            i += len;
        }
    }
    if (!cur.empty()) words.push_back(cur);

    std::string out;
    int lineWidth = 0;          // display columns already on the current line
    bool lineEmpty = true;      // nothing placed on the current line yet
    for (std::string w : words) {
        // A word that cannot fit on a line by itself is hard-cut into width-sized pieces.
        while (displayWidth(w) > width) {
            if (!lineEmpty) { out += '\n'; lineWidth = 0; lineEmpty = true; }
            std::string piece;
            int pieceWidth = 0;
            size_t i = 0;
            for (; i < w.size(); ) {
                size_t len = unitLength(w, i);
                int cw = displayWidth(w.substr(i, len));  // 0 for escapes, 1 for a code point
                if (pieceWidth + cw > width) break;
                piece.append(w, i, len);
                pieceWidth += cw;
                i += len;
            }
            out += piece;
            out += '\n';
            w = w.substr(i);
        }
        int ww = displayWidth(w);
        if (!lineEmpty && lineWidth + 1 + ww > width) {  // start a new line for this word
            out += '\n';
            lineWidth = 0;
            lineEmpty = true;
        }
        if (!lineEmpty) { out += ' '; ++lineWidth; }
        out += w;
        lineWidth += ww;
        lineEmpty = false;
    }
    return closeStylesAtLineBreaks(out);
}

void emitParagraph(const std::vector<std::string>& lines, std::ostream& out) {
    // Join the paragraph's source lines (a soft line break becomes a space), render inline markup,
    // then reflow the styled result to the terminal width by display columns.
    std::string joined;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) joined += ' ';
        joined += trim(lines[i]);
    }
    // A paragraph that is just an <img> tag for a local PNG renders as an image instead of text.
    // renderImageBlock returns either a multi-row sixel (ih > 1, or ih == 1 with sixel bytes) or a
    // one-line text fallback (ih == 1, plain text — e.g. on a non-graphics terminal). The sixel is
    // placed via the reserve-and-paint helper; the text fallback is emitted as a normal line.
    std::string image;
    int iw = 0, ih = 0;
    if (renderImageBlock(trim(joined), terminalWidth(), image, iw, ih)) {
        if (image.empty()) return;
        if (isSixelImage(image)) emitImageParagraph(image, ih, out);
        else                     out << image << '\n';   // one-line text fallback
        return;
    }
    std::string reflowed = reflow(renderInline(joined), terminalWidth());
    if (!reflowed.empty()) out << reflowed << '\n';
}

void emitHeading(int level, const std::string& text, std::ostream& out) {
    // Bold heading text, followed by an underline whose character depends on the level, matching
    // the original script's "━─┈┄" for levels 1..4 (deeper levels reuse the level-4 character).
    static const std::vector<std::string> kUnderlines = {"━", "─", "┈", "┄"};
    std::string styled = renderInline(text);
    out << kBoldOn << styled << kBoldOff << '\n';
    const std::string& bar = kUnderlines[std::min(level, 4) - 1];
    out << horizontalLine(terminalWidth(), bar) << '\n';
}

void emitThematicBreak(std::ostream& out) {
    out << horizontalLine(terminalWidth()) << '\n';
}

void emitCodeBlock(const std::vector<std::string>& lines, std::ostream& out) {
    // Fenced code: print verbatim with the same light-gray background used for inline code,
    // padded to the width of the widest line so the block reads as a solid panel.
    int maxw = 0;
    for (const auto& l : lines) maxw = std::max(maxw, displayWidth(l));
    for (const auto& l : lines) {
        out << kCodeOn << ' ' << padTo(l, maxw) << ' ' << kCodeOff << '\n';
    }
}

// Is `line` a GFM table delimiter row, e.g. |---|:--:| ? It must contain only pipes, colons,
// hyphens and spaces, include at least one hyphen, and at least one pipe.
bool isTableDelimiterRow(const std::string& line) {
    bool hasDash = false, hasPipe = false;
    for (char c : line) {
        if (c == '-') hasDash = true;
        else if (c == '|') hasPipe = true;
        else if (c != ':' && c != ' ' && c != '\t') return false;
    }
    return hasDash && hasPipe;
}

// Split a table row on unescaped '|', dropping one optional leading and trailing pipe, and trim
// each resulting cell. Pipes escaped as \| become literal pipes within a cell.
std::vector<std::string> splitTableRow(const std::string& raw) {
    std::string row = trim(raw);
    size_t b = 0, e = row.size();
    if (b < e && row[b] == '|') ++b;
    if (e > b && row[e - 1] == '|') --e;
    std::vector<std::string> cells;
    std::string cur;
    for (size_t i = b; i < e; ++i) {
        if (row[i] == '\\' && i + 1 < e && row[i + 1] == '|') { cur += '|'; ++i; continue; }
        if (row[i] == '|') { cells.push_back(trim(cur)); cur.clear(); continue; }
        cur += row[i];
    }
    cells.push_back(trim(cur));
    return cells;
}

// Split a string on '\n' into its constituent lines (no trailing empty element added for a final
// newline; an empty string yields a single empty line).
std::vector<std::string> splitOnNewlines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '\n') {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

void emitTable(const std::vector<std::string>& rawRows, std::ostream& out) {
    // rawRows[0] is the header, rawRows[1] the delimiter (consumed by the caller's detection and
    // not printed), and the rest are body rows. Cells are inline-rendered.
    std::vector<std::vector<std::string>> rows;  // rendered cells, header + body
    rows.push_back(splitTableRow(rawRows[0]));
    for (size_t i = 2; i < rawRows.size(); ++i) rows.push_back(splitTableRow(rawRows[i]));

    size_t ncols = 0;
    for (auto& r : rows) ncols = std::max(ncols, r.size());
    for (auto& r : rows) r.resize(ncols);  // pad short rows with empty cells
    if (ncols == 0) return;

    const int W = terminalWidth();
    int budget = W - 2 * static_cast<int>(ncols - 1);
    if (budget < static_cast<int>(ncols)) budget = static_cast<int>(ncols);  // at least 1 col each

    // Render every cell, distinguishing image cells (a lone <img> for a local PNG that produced an
    // actual sixel) from text cells. An image cell holds raw sixel bytes that must never reach
    // displayWidth/reflow, so we record its column width and row height separately and carry the
    // bytes through layout untouched. A non-image result (ordinary text, or an <img> fallback to its
    // alt text/filename when graphics are unavailable) is treated as normal reflowable text.
    std::vector<std::vector<bool>> isImage(rows.size(), std::vector<bool>(ncols, false));
    std::vector<std::vector<int>> imgRows(rows.size(), std::vector<int>(ncols, 0));  // image height
    std::vector<std::vector<int>> imgCellW(rows.size(), std::vector<int>(ncols, 0)); // image width
    std::vector<std::vector<std::string>> imgText(rows.size(), std::vector<std::string>(ncols));
    std::vector<int> imgWidth(ncols, 0);   // forced width of an image column, if any
    for (size_t i = 0; i < rows.size(); ++i) {
        for (size_t j = 0; j < ncols; ++j) {
            std::string block;
            int iw = 0, ih = 0;
            // Render at the full content budget so an explicit height is honoured at the image's
            // natural width; a column too narrow to hold it is handled by a re-render after layout.
            std::string cellText = trim(rows[i][j]);
            if (renderImageBlock(cellText, budget, block, iw, ih) && isSixelImage(block)) {
                rows[i][j] = block;
                isImage[i][j] = true;
                imgRows[i][j] = std::max(1, ih);
                imgCellW[i][j] = iw;
                imgText[i][j] = cellText;     // kept so we can re-render at a clamped width
                imgWidth[j] = std::max(imgWidth[j], iw);
            } else if (!block.empty() && ih == 1) {
                rows[i][j] = block;          // <img> fallback text (alt/filename): reflow as text
            } else {
                rows[i][j] = renderInline(rows[i][j]);
            }
        }
    }

    // Natural (unwrapped) width of each column = its widest rendered cell. Image cells contribute
    // their recorded width rather than displayWidth (which can't measure a sixel block).
    std::vector<int> natural(ncols, 0);
    for (size_t i = 0; i < rows.size(); ++i)
        for (size_t j = 0; j < ncols; ++j)
            natural[j] = std::max(natural[j], isImage[i][j] ? imgWidth[j] : displayWidth(rows[i][j]));

    // Allocate column widths left to right. The content budget excludes the two-space separators
    // between columns. A text column may take up to its fair share of the remaining budget (rounded
    // up, so earlier columns can be one wider), but never more than it needs; after reflowing to
    // that cap its actual width may be smaller still, and the slack rolls forward. An image column
    // is fixed at its image width and never reflowed.
    std::vector<int> widths(ncols, 0);
    int remaining = budget, remCols = static_cast<int>(ncols);
    for (size_t j = 0; j < ncols; ++j) {
        bool imageCol = imgWidth[j] > 0;
        int actual;
        if (imageCol) {
            actual = std::min(natural[j], remaining);   // images don't wrap; clamp to what's left
        } else {
            int fair = (remaining + remCols - 1) / remCols;   // ceil(remaining / remCols)
            int cap = std::min(natural[j], fair);
            actual = 0;
            for (auto& r : rows) {
                r[j] = reflow(r[j], cap);
                for (auto& ln : splitOnNewlines(r[j])) actual = std::max(actual, displayWidth(ln));
            }
            if (actual < 1) actual = (natural[j] == 0 ? 0 : 1);
        }
        widths[j] = actual;
        remaining -= actual;
        --remCols;
    }

    // An image whose column ended up narrower than its natural width (the table could not give it the
    // room its requested height implied) is re-rendered to fit that width, so it never overflows into
    // the next column. This is the only case that runs timg twice, and only when columns are tight.
    for (size_t i = 0; i < rows.size(); ++i) {
        for (size_t j = 0; j < ncols; ++j) {
            if (!isImage[i][j] || widths[j] >= imgCellW[i][j]) continue;
            std::string block;
            int iw = 0, ih = 0;
            if (renderImageBlock(imgText[i][j], widths[j], block, iw, ih) && isSixelImage(block)) {
                rows[i][j] = block;
                imgRows[i][j] = std::max(1, ih);
                imgCellW[i][j] = iw;
            }
        }
    }

    int total = 0;
    for (int w : widths) total += w;
    total += 2 * static_cast<int>(ncols - 1);  // two-space column separators
    std::string hline = horizontalLine(total);

    // The 1-based starting column of each cell, accounting for the two-space separators. Used to
    // position image sixels during the overlay pass.
    std::vector<int> colOrigin(ncols, 1);
    for (size_t j = 1; j < ncols; ++j) colOrigin[j] = colOrigin[j - 1] + widths[j - 1] + 2;

    // Render one table row. The row occupies a band as tall as the tallest cell — text line count or
    // image row height, whichever is greater. Text is laid out first, top-aligned, one line at a
    // time across the band; image cells contribute blank padding of their column width during this
    // pass (reserving their space). Then, in a second pass, each image's sixel is painted into its
    // reserved column with relative cursor moves: up to the band top, across to the cell's column,
    // replay (which leaves the cursor at column 1 just below the image), then back down to the band
    // bottom. This keeps every image inside its own cell and keeps row separators below the band.
    auto emitRow = [&](size_t rowIdx, bool bold) {
        const std::vector<std::string>& cells = rows[rowIdx];
        std::vector<std::vector<std::string>> cellLines(ncols);
        int bandH = 1;
        for (size_t j = 0; j < ncols; ++j) {
            if (isImage[rowIdx][j]) {
                bandH = std::max(bandH, imgRows[rowIdx][j]);
            } else {
                cellLines[j] = splitOnNewlines(cells[j]);
                bandH = std::max(bandH, static_cast<int>(cellLines[j].size()));
            }
        }
        // Text pass: bandH lines. Image cells reserve their column width with spaces.
        for (int k = 0; k < bandH; ++k) {
            std::string line;
            for (size_t j = 0; j < ncols; ++j) {
                if (j) line += "  ";
                if (isImage[rowIdx][j]) {
                    line.append(static_cast<size_t>(widths[j]), ' ');
                } else {
                    std::string piece =
                        static_cast<size_t>(k) < cellLines[j].size() ? cellLines[j][k] : std::string();
                    if (bold && !piece.empty()) piece = kBoldOn + piece + kBoldOff;
                    line += padTo(piece, widths[j]);
                }
            }
            out << line << '\n';
        }
        // Overlay pass: paint each image into its reserved space. The cursor is at column 1 on the
        // row just below the band; all moves are relative to that, so the band's earlier scroll
        // (if any) does not matter.
        for (size_t j = 0; j < ncols; ++j) {
            if (!isImage[rowIdx][j]) continue;
            int r = imgRows[rowIdx][j];
            out << "\033[" << bandH << 'A';                  // up to the band top
            out << "\033[" << colOrigin[j] << 'G';           // across to this cell's column
            out << cells[j];                                 // paint; cursor ends below the image
            if (bandH > r) out << "\033[" << (bandH - r) << 'B';  // back down to the band bottom
            out << '\r';                                     // column 1 for the next overlay / row
        }
    };

    for (size_t i = 0; i < rows.size(); ++i) {
        emitRow(i, /*bold=*/i == 0);
        if (i == 0)
            out << hline << '\n';                                  // header underline
        else if (i + 1 < rows.size())
            out << kLightGray << hline << kReset << '\n';          // light-gray row separator
    }
}

// ---------------------------------------------------------------------------
// Block parser / document driver
// ---------------------------------------------------------------------------

void render(const std::vector<std::string>& lines, std::ostream& out);

// Drop a single trailing '\n' if present. render() terminates every block with one, so the buffered
// output of a recursive render ends in a newline; removing it keeps splitOnNewlines from yielding a
// spurious empty final line (which would draw an extra bare quote rule).
std::string trimTrailingNewline(const std::string& s) {
    if (!s.empty() && s.back() == '\n') return s.substr(0, s.size() - 1);
    return s;
}

// True if `line` opens a block quote: up to three spaces of indentation, then a '>'. (GFM allows
// the block-quote marker to be indented by at most three spaces; four would be a code indent, which
// this renderer does not implement, but checking the bound keeps the detection honest.)
bool isBlockQuote(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') ++i;
    return i < line.size() && line[i] == '>';
}

// Strip one block-quote marker from a line that isBlockQuote(): drop up to three leading spaces,
// the '>', and one optional space after it. The remainder is the line's content one level in.
std::string stripBlockQuote(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') ++i;
    if (i < line.size() && line[i] == '>') ++i;      // the marker itself
    if (i < line.size() && line[i] == ' ') ++i;       // one optional space of padding
    return line.substr(i);
}

// A parsed list-item marker. `markerWidth` is the number of leading columns the marker plus its
// trailing spaces occupy on the first line — i.e. the column the item's content begins at, which is
// also the indent every continuation line and nested block must reach to stay in the item.
struct ListMarker {
    bool ordered = false;       // true for "1." / "1)", false for a "-"/"+"/"*" bullet
    char delim = 0;             // bullet char ('-','+','*') or ordered delimiter ('.'/')')
    int start = 0;              // ordered list's starting number (the digits before the delimiter)
    size_t markerWidth = 0;     // columns from line start to the item's content (the content indent)
};

// If `line` begins a list item, fill `m` and return true. A marker is up to three spaces of indent,
// then either a bullet (-, +, *) or an ordered marker (1-9 digits then '.' or ')'), then at least
// one space (or end of line). The trailing run of spaces is folded into markerWidth so the content
// indent is known. An empty item (marker then end of line) gets a content indent of marker + 1, the
// conventional single space. (We don't implement GFM's 4-space-content rule for over-indented item
// bodies; up to a normal run of spaces after the marker is consumed.)
bool parseListMarker(const std::string& line, ListMarker& m) {
    size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') ++i;
    if (i >= line.size()) return false;
    size_t markerStart = i;
    if (line[i] == '-' || line[i] == '+' || line[i] == '*') {
        m.ordered = false;
        m.delim = line[i];
        ++i;
    } else if (std::isdigit(static_cast<unsigned char>(line[i]))) {
        size_t d = i;
        while (d < line.size() && std::isdigit(static_cast<unsigned char>(line[d]))) ++d;
        if (d - i > 9 || d >= line.size() || (line[d] != '.' && line[d] != ')')) return false;
        m.ordered = true;
        m.start = std::atoi(line.substr(i, d - i).c_str());
        m.delim = line[d];
        i = d + 1;
    } else {
        return false;
    }
    // The marker must be followed by a space (or be the whole line, an empty item).
    if (i < line.size() && line[i] != ' ') return false;
    (void)markerStart;
    size_t afterMarker = i;
    while (i < line.size() && line[i] == ' ') ++i;
    // An empty item, or one indented by many spaces, still uses a single space of content indent.
    if (i >= line.size() || i - afterMarker == 0) m.markerWidth = afterMarker + 1;
    else m.markerWidth = i;
    return true;
}

// True if `line` begins a list item of any kind.
bool isListItem(const std::string& line) {
    ListMarker m;
    return parseListMarker(line, m);
}

// Two list markers belong to the same list when they are the same type, and for bullets the same
// bullet character, and for ordered lists the same delimiter ('.'/')'). A change of any of these (or
// a blank line followed by a different type) starts a new list. (GFM also treats a change of bullet
// character as a new list; we follow that.)
bool sameListType(const ListMarker& a, const ListMarker& b) {
    if (a.ordered != b.ordered) return false;
    return a.delim == b.delim;
}

// True if the trimmed line is a valid ATX heading marker (1-6 '#' then space or end of line).
bool isAtxHeading(const std::string& t) {
    size_t h = 0;
    while (h < t.size() && t[h] == '#') ++h;
    return h >= 1 && h <= 6 && (h == t.size() || t[h] == ' ');
}

// True if the trimmed line opens or closes a fenced code block (>= 3 backticks or tildes).
bool isCodeFence(const std::string& t) {
    if (t.size() < 3 || (t[0] != '`' && t[0] != '~')) return false;
    size_t r = 0;
    while (r < t.size() && t[r] == t[0]) ++r;
    return r >= 3;
}

// True if `line` is indented enough to be an indented code line: at least four leading columns of
// indentation, counting a leading tab as reaching the four-column stop. A blank line does not count
// (it is handled separately as a possible interior blank). See GFM 4.4.
bool isIndentedCode(const std::string& line) {
    int cols = 0;
    for (char c : line) {
        if (c == ' ') ++cols;
        else if (c == '\t') cols += 4;
        else break;
        if (cols >= 4) return true;
    }
    return false;  // ran out of leading whitespace before reaching column four (or the line is blank)
}

// Remove the four-column indent that introduces an indented code block, leaving the rest verbatim. A
// leading tab counts as four columns and is consumed whole; otherwise up to four leading spaces are
// dropped. Content beyond the first four columns is preserved exactly.
std::string stripCodeIndent(const std::string& line) {
    size_t i = 0;
    int cols = 0;
    while (i < line.size() && cols < 4) {
        if (line[i] == ' ') { ++cols; ++i; }
        else if (line[i] == '\t') { cols += 4; ++i; }
        else break;
    }
    return line.substr(i);
}

// True if the trimmed line is a thematic break: >= 3 of the same -, * or _ (spaces allowed).
bool isThematicBreak(const std::string& t) {
    std::string compact;
    for (char c : t) if (c != ' ' && c != '\t') compact += c;
    if (compact.size() < 3) return false;
    return compact.find_first_not_of('-') == std::string::npos ||
           compact.find_first_not_of('*') == std::string::npos ||
           compact.find_first_not_of('_') == std::string::npos;
}

// True if line `j` begins a new leaf block, so a paragraph in progress must stop before it.
bool startsBlock(const std::vector<std::string>& lines, size_t j) {
    const std::string t = trim(lines[j]);
    if (t.empty()) return true;
    if (isAtxHeading(t)) return true;
    if (isCodeFence(t)) return true;
    if (isThematicBreak(t)) return true;
    if (isBlockQuote(lines[j])) return true;
    if (isListItem(lines[j])) return true;
    if (lines[j].find('|') != std::string::npos && j + 1 < lines.size() &&
        isTableDelimiterRow(lines[j + 1]))
        return true;  // a GFM table header row
    return false;
}

// Strip up to `n` leading spaces from `line` (fewer if the line has fewer). Used to remove a list
// item's content indent from its continuation lines so the item's body renders at column zero.
std::string stripIndent(const std::string& line, size_t n) {
    size_t i = 0;
    while (i < n && i < line.size() && line[i] == ' ') ++i;
    return line.substr(i);
}

// Count the leading spaces of `line` (its indentation in columns; tabs are not expanded — the inputs
// here use spaces, matching the rest of the parser).
size_t leadingSpaces(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    return i;
}

// One gathered list item: the content lines with the item's content indent already stripped, plus
// whether a blank line appeared anywhere inside it (which, like a blank line between items, makes the
// whole list loose — GFM 5.3).
struct ListItem {
    std::vector<std::string> lines;
    bool hadBlank = false;
};

// Render a gathered list. Each item's content is a full block sequence rendered recursively (one
// container level deeper) into a buffer; the buffer's first line is prefixed with the item marker and
// the rest with equal-width padding so the body hangs under the marker. Bullets use a depth-varying
// glyph (• ◦ ▪, matching GitHub's disc/circle/square); ordered items keep their number with a '.'.
// gListDepth tracks bullet nesting for the glyph; gIndent is raised by the marker width so inner
// content reflows within the narrower column. A loose list prints a blank line between items.
void emitList(const std::vector<ListItem>& items, const ListMarker& marker, bool loose,
              std::ostream& out) {
    static const std::vector<std::string> kBullets = {"•", "◦", "▪"};
    for (size_t idx = 0; idx < items.size(); ++idx) {
        if (idx && loose) out << '\n';

        // Build the marker text and the matching blank padding for continuation lines.
        std::string mark;
        if (marker.ordered) {
            mark = std::to_string(marker.start + static_cast<int>(idx)) + marker.delim + ' ';
        } else {
            mark = kBullets[std::min<size_t>(gListDepth, kBullets.size() - 1)] + std::string(" ");
        }
        int markWidth = displayWidth(mark);
        std::string pad(static_cast<size_t>(markWidth), ' ');

        std::ostringstream buf;
        gIndent += markWidth;
        if (!marker.ordered) ++gListDepth;
        if (!loose) gTightItem = true;  // suppress lead-paragraph/sublist spacing in a tight item
        render(items[idx].lines, buf);
        if (!marker.ordered) --gListDepth;
        gIndent -= markWidth;

        std::vector<std::string> body = splitOnNewlines(trimTrailingNewline(buf.str()));
        for (size_t k = 0; k < body.size(); ++k)
            out << (k == 0 ? mark : pad) << body[k] << '\n';
    }
}

// Render the gathered lines of a block quote (markers already stripped) one container level deeper.
// The contents are a full sequence of blocks, so they go through render() recursively; the rendered
// output is then prefixed line by line with the quote rule. gIndent is raised by the rule's width
// while the contents render, so paragraphs reflow and rules fill within the narrower inner column.
void emitBlockQuote(const std::vector<std::string>& inner, std::ostream& out) {
    const int prefixWidth = 2;  // displayWidth(kQuoteBar): the bar glyph plus one space
    std::ostringstream buf;
    gIndent += prefixWidth;
    render(inner, buf);
    gIndent -= prefixWidth;
    // Prefix every line of the rendered contents with the quote rule. Blank separator lines between
    // inner blocks get the bar too, so the rule is continuous down the whole quote.
    for (const std::string& line : splitOnNewlines(trimTrailingNewline(buf.str())))
        out << kQuoteBar << line << '\n';
}

void render(const std::vector<std::string>& lines, std::ostream& out) {
    size_t n = lines.size();
    bool emitted = false;  // has any block been printed yet?
    // A tight list item suppresses the blank line between its own top-level blocks (its lead
    // paragraph and a nested list). Snapshot the flag and clear it immediately, so this applies only
    // to this render's top level — blocks rendered deeper, and recursive renders, separate normally.
    bool tight = gTightItem;
    gTightItem = false;
    // Print a blank line before every block except the first, so blocks are visually separated
    // without a leading or trailing blank line in the output. A tight item skips that blank.
    auto separate = [&] { if (emitted && !tight) out << '\n'; emitted = true; };
    for (size_t i = 0; i < n;) {
        const std::string& line = lines[i];
        std::string t = trim(line);

        if (t.empty()) { ++i; continue; }  // blank lines separate blocks

        // ATX heading: 1-6 '#' followed by a space (or end of line).
        if (isAtxHeading(t)) {
            separate();
            size_t h = 0;
            while (h < t.size() && t[h] == '#') ++h;
            std::string text = trim(t.substr(h));
            // A heading may have an optional closing run of '#'.
            size_t e = text.size();
            while (e > 0 && text[e - 1] == '#') --e;
            if (e < text.size() && (e == 0 || text[e - 1] == ' ')) text = trim(text.substr(0, e));
            emitHeading(static_cast<int>(h), text, out);
            ++i;
            continue;
        }

        // Fenced code block: a run of >= 3 backticks or tildes; closed by a matching fence.
        if (isCodeFence(t)) {
            separate();
            char fence = t[0];
            size_t run = 0;
            while (run < t.size() && t[run] == fence) ++run;
            std::vector<std::string> body;
            size_t j = i + 1;
            for (; j < n; ++j) {
                std::string tj = trim(lines[j]);
                size_t r = 0;
                while (r < tj.size() && tj[r] == fence) ++r;
                if (r >= run && tj.find_first_not_of(fence) == std::string::npos) break;  // closing fence
                body.push_back(lines[j]);
            }
            emitCodeBlock(body, out);
            i = (j < n) ? j + 1 : j;  // skip the closing fence if present
            continue;
        }

        // Thematic break: a line of three or more -, * or _ (optionally spaced).
        if (isThematicBreak(t)) {
            separate();
            emitThematicBreak(out);
            ++i;
            continue;
        }

        // Block quote: one or more lines starting with '>'. Gather the run, including lazy
        // continuation lines — a non-blank line without its own '>' that continues a paragraph
        // inside the quote (GFM 5.1). A blank line, or an unquoted line that itself starts a new
        // block, ends the quote. The markers are stripped and the contents rendered recursively.
        if (isBlockQuote(line)) {
            separate();
            std::vector<std::string> inner;
            size_t j = i;
            for (; j < n; ++j) {
                if (isBlockQuote(lines[j])) {
                    inner.push_back(stripBlockQuote(lines[j]));
                } else if (!trim(lines[j]).empty() && !startsBlock(lines, j)) {
                    inner.push_back(lines[j]);   // lazy paragraph continuation
                } else {
                    break;
                }
            }
            emitBlockQuote(inner, out);
            i = j;
            continue;
        }

        // List: a run of items sharing a marker type (GFM 5.3). Reached only after the thematic-break
        // check above, so a line like "- - -" or "***" is a rule, not a one-item list. Each item is
        // gathered as the content lines belonging to it (its content indent stripped); a line at the
        // list's own indent that opens a same-type marker starts the next item; one that opens a
        // different-type marker, a non-indented block start, or follows a blank line at a lower indent
        // ends the list. The list is loose if any blank line falls between items or inside an item.
        if (isListItem(line)) {
            separate();
            ListMarker listMarker;
            parseListMarker(line, listMarker);  // defines the list's type for the whole run

            std::vector<ListItem> items;
            bool loose = false;
            size_t j = i;
            while (j < n) {
                ListMarker m;
                // A new item: a marker line at this list's indent level and matching type.
                if (parseListMarker(lines[j], m) && leadingSpaces(lines[j]) < listMarker.markerWidth) {
                    if (!sameListType(m, listMarker)) break;  // different marker type: a separate list
                    ListItem item;
                    // The first line keeps everything after the marker (drop the marker itself, not
                    // just leading spaces — there are none before it at this point).
                    item.lines.push_back(lines[j].substr(std::min(m.markerWidth, lines[j].size())));
                    size_t k = j + 1;
                    bool pendingBlank = false;  // a blank line seen but not yet committed to the item
                    for (; k < n; ++k) {
                        if (trim(lines[k]).empty()) { pendingBlank = true; continue; }
                        size_t ind = leadingSpaces(lines[k]);
                        if (ind >= m.markerWidth) {
                            // Indented to (or past) the content column: part of this item's body. A
                            // committed blank line before it is preserved and makes the list loose.
                            if (pendingBlank) {
                                item.hadBlank = true;
                                item.lines.push_back("");
                                pendingBlank = false;
                            }
                            item.lines.push_back(stripIndent(lines[k], m.markerWidth));
                        } else if (!pendingBlank && !startsBlock(lines, k)) {
                            // Lazy continuation: an unindented paragraph line directly following the
                            // item's text (no blank between) still belongs to its paragraph.
                            item.lines.push_back(lines[k]);
                        } else {
                            break;  // unindented block start, marker, or post-blank line: item ends.
                        }
                    }
                    if (item.hadBlank) loose = true;
                    // A blank line separating this item from a following same-type item at the list's
                    // own indent makes the whole list loose (GFM 5.3).
                    if (pendingBlank && k < n) {
                        ListMarker next;
                        if (parseListMarker(lines[k], next) &&
                            leadingSpaces(lines[k]) < listMarker.markerWidth &&
                            sameListType(next, listMarker))
                            loose = true;
                    }
                    items.push_back(std::move(item));
                    j = k;
                } else {
                    break;
                }
            }
            emitList(items, listMarker, loose, out);
            i = j;
            continue;
        }

        // GFM table: a header row whose next line is a delimiter row. Gather contiguous rows.
        if (line.find('|') != std::string::npos && i + 1 < n && isTableDelimiterRow(lines[i + 1])) {
            separate();
            std::vector<std::string> rows;
            rows.push_back(line);              // header
            rows.push_back(lines[i + 1]);      // delimiter
            size_t j = i + 2;
            for (; j < n; ++j) {
                if (trim(lines[j]).empty() || lines[j].find('|') == std::string::npos) break;
                rows.push_back(lines[j]);
            }
            emitTable(rows, out);
            i = j;
            continue;
        }

        // Indented code block: lines indented four or more columns, rendered verbatim (GFM 4.4).
        // Reached only at a fresh block position — an indented line right after a paragraph line is
        // gathered as that paragraph's lazy continuation instead, because isIndentedCode is
        // deliberately NOT part of startsBlock(), so a code block cannot interrupt a paragraph.
        // Interior blank lines are kept; trailing blank lines are dropped. The four-column indent is
        // stripped from each line and the rest emitted exactly, reusing the fenced-code panel style.
        if (isIndentedCode(line)) {
            separate();
            std::vector<std::string> body;
            size_t j = i;
            for (; j < n; ++j) {
                if (trim(lines[j]).empty()) { body.push_back(std::string()); continue; }
                if (!isIndentedCode(lines[j])) break;
                body.push_back(stripCodeIndent(lines[j]));
            }
            while (!body.empty() && body.back().empty()) body.pop_back();  // drop trailing blanks
            emitCodeBlock(body, out);
            // Resume right after the lines that became code; any trailing blank lines we gathered but
            // popped are left for the main loop, which treats blank lines as block separators.
            i += body.size();
            continue;
        }

        // Paragraph: line i is not a block start, so consume it and any following lines until a
        // blank line or the start of another block. The same startsBlock() predicate that the
        // dispatcher relies on is reused here, so the two can never disagree (which previously
        // produced an empty paragraph and an infinite loop on lines like "#620](...)").
        separate();
        std::vector<std::string> para;
        size_t j = i;
        do {
            para.push_back(lines[j]);
            ++j;
        } while (j < n && !startsBlock(lines, j));
        emitParagraph(para, out);
        i = j;
    }
}

std::vector<std::string> splitLines(std::istream& in) {
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // tolerate CRLF
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

int main(int argc, char** argv) {
    // Collect the file operands, parsing options first. Supported: --width N / -w N / --width=N to
    // force the render width (overriding $COLUMNS and the terminal size), and -- to end options so a
    // filename may begin with a dash. Option parsing stops at the first non-option operand.
    std::vector<std::string> files;
    auto usage = [&] { std::cerr << "usage: mdcat [--width N] [--] [file ...]\n"; };

    int a = 1;
    for (; a < argc; ++a) {
        std::string arg = argv[a];
        if (arg == "--") { ++a; break; }
        if (arg.size() < 2 || arg[0] != '-') break;  // first operand: stop option parsing

        std::string val;
        bool haveVal = false;
        if (arg == "-w" || arg == "--width") {        // value is the next argument
            if (a + 1 >= argc) { std::cerr << "mdcat: " << arg << " requires a value\n"; return 2; }
            val = argv[++a];
            haveVal = true;
        } else if (arg.rfind("--width=", 0) == 0) {   // value is inline after '='
            val = arg.substr(8);
            haveVal = true;
        } else {
            std::cerr << "mdcat: unknown option: " << arg << "\n";
            usage();
            return 2;
        }
        if (haveVal) {
            int w = std::atoi(val.c_str());
            if (w <= 0) { std::cerr << "mdcat: invalid width: " << val << "\n"; return 2; }
            gWidthOverride = w;
        }
    }
    for (; a < argc; ++a) files.emplace_back(argv[a]);

    if (!files.empty()) {
        // Render each file independently and concatenate the results, rather than merging the files'
        // lines into one document. This keeps the boundary between files clean: the last block of one
        // file can never merge with the first block of the next. It also makes rendering distribute
        // over the argument list — `mdcat a b` produces exactly `mdcat a` followed by `mdcat b` —
        // which is checked by tests/property-concat.sh.
        for (const auto& path : files) {
            std::ifstream f(path);
            if (!f) {
                std::cerr << "mdcat: " << path << ": cannot open file\n";
                return 1;
            }
            render(splitLines(f), std::cout);
        }
    } else {
        render(splitLines(std::cin), std::cout);
    }
    return 0;
}
