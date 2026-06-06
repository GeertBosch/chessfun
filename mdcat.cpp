// mdcat - render a GitHub Flavored Markdown subset to an ANSI terminal.
//
// This is a C++17 port of the earlier markfun.sh. Rather than rewriting text with regular
// expressions, it parses the input in two phases that mirror the GFM specification
// (https://github.github.com/gfm):
//
//   1. Block parsing splits the document into a sequence of leaf blocks: ATX headings,
//      thematic breaks, fenced code blocks, GFM tables, and paragraphs. Blank lines separate
//      blocks; the lines of a paragraph are gathered together so they can be reflowed.
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
// Usage:  mdcat [file ...]   (reads standard input when given no file arguments)

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/ioctl.h>
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
const std::string kCodeOn = "\033[48;5;253;38;5;236m";  // light-gray bg, dark-gray fg
const std::string kCodeOff = "\033[39;49m";
const std::string kReset = "\033[0m";
const std::string kLightGray = "\033[38;5;250m";

// The display width used for thematic breaks, header underlines and paragraph reflow.
int terminalWidth() {
    // Determined once and cached for the rest of the run: the width is fixed for our purposes and
    // there is no reason to repeat the syscall for every block and every reflowed line.
    //
    // Ask the terminal directly. $COLUMNS is a shell-internal variable that is usually not
    // exported to child processes, so it is unreliable here; the window size from the kernel is
    // authoritative when any of our standard streams is a terminal. Fall back to $COLUMNS (in
    // case output is piped but the caller exported a width) and finally to a fixed default.
    static const int width = [] {
        for (int fd : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
            struct winsize ws;
            if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return static_cast<int>(ws.ws_col);
        }
        if (const char* cols = std::getenv("COLUMNS")) {
            int w = std::atoi(cols);
            if (w > 0) return w;
        }
        return 100;  // sensible default when not attached to a terminal
    }();
    return width;
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
                    out += kCodeOn + ' ' + t.text + ' ' + kCodeOff;
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

// Run timg to render `path` at W x H character cells with sixel graphics, returning its stdout.
// Returns an empty string if timg cannot be launched or exits non-zero. The image data is captured
// so that the multi-line vertical-reserve trick in renderImageBlock can wrap it.
std::string runTimg(const std::string& path, int W, int H) {
    std::ostringstream cmd;
    // -ps : sixel pixelation;  -g WxH : fit inside W x H character cells.  The path is passed via a
    // single-quoted argument with embedded single quotes escaped, so odd filenames stay safe.
    std::string quoted = "'";
    for (char c : path) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += "'";
    cmd << "timg -ps -g" << W << "x" << H << " " << quoted << " 2>/dev/null";
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

// If `text` (already trimmed) is a single <img> tag for a local PNG, render it as an image block and
// return true, writing the block to `out` and its cell footprint to `cellWidth` / `cellHeight` (in
// columns / rows). Otherwise return false and leave the outputs untouched. `availWidth` caps the
// width in columns. On any failure to produce an image, the alt text (or literal tag) is returned
// as a one-line block so callers can treat the result uniformly.
//
// The rendered block follows the table-cell recipe: H-1 newlines reserve vertical space, an
// ESC[<H-1>A moves the cursor back to the top of that space, and the timg sixel output is appended.
// Emitted line by line (split on '\n'), the first H-1 lines are blank and the final line carries the
// cursor-up plus the image, so the image paints across the rows the blank lines reserved.
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

    // Pixels -> character cells: 8 px per column, 15 px per row. Clamp the width to the available
    // columns, scaling the height to match so the aspect ratio of the chosen target is preserved.
    int W = std::max(1, (tw + 7) / 8);
    int H = std::max(1, (th + 14) / 15);
    if (availWidth > 0 && W > availWidth) {
        H = std::max(1, H * availWidth / W);
        W = availWidth;
    }

    std::string img = runTimg(src, W, H);
    if (img.empty()) return fallback("timg failed");

    std::string block;
    block.append(static_cast<size_t>(H - 1), '\n');
    if (H > 1) block += "\033[" + std::to_string(H - 1) + 'A';
    block += img;
    out = block;
    cellWidth = W;
    cellHeight = H;
    return true;
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
    return out;
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
    std::string image;
    int iw = 0, ih = 0;
    if (renderImageBlock(trim(joined), terminalWidth(), image, iw, ih)) {
        if (!image.empty()) out << image << '\n';
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

    // Render every cell, distinguishing image cells (a lone <img> for a local PNG) from text cells.
    // An image cell holds a multi-line sixel block whose bytes must never reach displayWidth/reflow,
    // so we record its fixed column width separately and carry it through layout untouched.
    std::vector<std::vector<bool>> isImage(rows.size(), std::vector<bool>(ncols, false));
    std::vector<int> imgWidth(ncols, 0);   // forced width of an image column, if any
    for (size_t i = 0; i < rows.size(); ++i) {
        for (size_t j = 0; j < ncols; ++j) {
            std::string block;
            int iw = 0, ih = 0;
            if (renderImageBlock(trim(rows[i][j]), budget, block, iw, ih) && ih > 0) {
                rows[i][j] = block;
                isImage[i][j] = true;
                imgWidth[j] = std::max(imgWidth[j], iw);
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

    int total = 0;
    for (int w : widths) total += w;
    total += 2 * static_cast<int>(ncols - 1);  // two-space column separators
    std::string hline = horizontalLine(total);

    // Render a single (possibly multi-line) table row: each cell is reflowed text, top-aligned,
    // its lines padded to the column width and joined by two spaces. `bold` wraps each cell line
    // in bold for the header. Cells shorter than the tallest cell are padded with blank lines.
    // An image cell's lines are emitted verbatim and padded by its known image width, since their
    // sixel bytes can't be measured by displayWidth; the image's final line paints across the rows
    // its leading blank lines reserved.
    auto emitRow = [&](size_t rowIdx, bool bold) {
        const std::vector<std::string>& cells = rows[rowIdx];
        std::vector<std::vector<std::string>> cellLines(ncols);
        size_t height = 1;
        for (size_t j = 0; j < ncols; ++j) {
            cellLines[j] = splitOnNewlines(cells[j]);
            height = std::max(height, cellLines[j].size());
        }
        for (size_t k = 0; k < height; ++k) {
            std::string line;
            for (size_t j = 0; j < ncols; ++j) {
                if (j) line += "  ";
                std::string piece = k < cellLines[j].size() ? cellLines[j][k] : std::string();
                if (isImage[rowIdx][j]) {
                    // Pad by the image's known column footprint; its bytes are opaque to displayWidth.
                    int pad = widths[j] - (k + 1 == cellLines[j].size() ? imgWidth[j] : 0);
                    line += piece;
                    if (pad > 0) line.append(static_cast<size_t>(pad), ' ');
                } else {
                    if (bold && !piece.empty()) piece = kBoldOn + piece + kBoldOff;
                    line += padTo(piece, widths[j]);
                }
            }
            out << line << '\n';
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
    if (lines[j].find('|') != std::string::npos && j + 1 < lines.size() &&
        isTableDelimiterRow(lines[j + 1]))
        return true;  // a GFM table header row
    return false;
}

void render(const std::vector<std::string>& lines, std::ostream& out) {
    size_t n = lines.size();
    bool emitted = false;  // has any block been printed yet?
    // Print a blank line before every block except the first, so blocks are visually separated
    // without a leading or trailing blank line in the output.
    auto separate = [&] { if (emitted) out << '\n'; emitted = true; };
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
    if (argc > 1) {
        // Render each file independently and concatenate the results, rather than merging the files'
        // lines into one document. This keeps the boundary between files clean: the last block of one
        // file can never merge with the first block of the next. It also makes rendering distribute
        // over the argument list — `mdcat a b` produces exactly `mdcat a` followed by `mdcat b` —
        // which is checked by tests/property-concat.sh.
        for (int a = 1; a < argc; ++a) {
            std::ifstream f(argv[a]);
            if (!f) {
                std::cerr << "mdcat: " << argv[a] << ": cannot open file\n";
                return 1;
            }
            render(splitLines(f), std::cout);
        }
    } else {
        render(splitLines(std::cin), std::cout);
    }
    return 0;
}
