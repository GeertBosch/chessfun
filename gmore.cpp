// gmore — a graphics-aware pager. It emulates a pragmatic subset of a terminal
// into a cell grid (the scrollback buffer), so it can page ANY text+sixel stream
// with no constraints on the producer — including cursor movement. See
// docs/adr/0001-gmore-data-model.md for why a cell grid (not line spans).
//
// STATUS: foundation. The cell-grid emulator (text, UTF-8, wrap, SGR colour/style,
// cursor movement, DECSC/DECRC, EL/ED) + a "more" pager over the grid. OSC and
// sixel-DCS sequences are RECOGNISED and SKIPPED as no-ops for now; the sixel
// image layer + 18px-strip rendering, and OSC 8 hyperlink attrs, come next (the
// model already has room — interned Attr has a linkId field reserved).
//
// Keys: space/f page down, Enter/j line down, q quit.   --dump renders the
// emulated grid to stdout (no paging) for testing.

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace {

// ---------------------------------------------------------------------------
// Attributes (interned). Colour encoding: 0 = default; tag in the high byte —
// PAL|idx for a 256-palette index, TRUE|rgb for 24-bit. linkId reserved for OSC 8.
// ---------------------------------------------------------------------------
constexpr uint32_t PAL = 0x01000000u, TRUE = 0x02000000u;
uint32_t pal(int i) { return PAL | (i & 0xFF); }
uint32_t tru(int r, int g, int b) { return TRUE | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF); }

enum { A_BOLD = 1, A_DIM = 2, A_ITALIC = 4, A_UNDER = 8, A_INVERSE = 16 };

struct Attr {
    uint32_t fg = 0, bg = 0;
    uint16_t flags = 0;
    uint32_t link = 0;  // OSC 8 (reserved; not set yet)
    bool operator==(const Attr& o) const {
        return fg == o.fg && bg == o.bg && flags == o.flags && link == o.link;
    }
};
struct AttrHash {
    size_t operator()(const Attr& a) const {
        size_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix(a.fg); mix(a.bg); mix(a.flags); mix(a.link);
        return h;
    }
};
std::vector<Attr> gAttrs;
std::unordered_map<Attr, uint16_t, AttrHash> gAttrMap;

uint16_t internAttr(const Attr& a) {
    auto it = gAttrMap.find(a);
    if (it != gAttrMap.end()) return it->second;
    uint16_t id = static_cast<uint16_t>(gAttrs.size());
    gAttrs.push_back(a);
    gAttrMap.emplace(a, id);
    return id;
}

// ---------------------------------------------------------------------------
// Cells + UTF-8 helpers
// ---------------------------------------------------------------------------
struct Cell {
    char32_t cp = U' ';
    uint16_t attr = 0;   // index into gAttrs (0 = default)
    uint8_t width = 1;   // 1 (or 2 for wide — deferred; YAGNI)
    uint8_t flags = 0;
};

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string sgrFor(uint16_t id) {
    const Attr& a = gAttrs[id];
    std::string s = "\033[0";  // reset, then apply — always correct from any prior state
    if (a.flags & A_BOLD) s += ";1";
    if (a.flags & A_DIM) s += ";2";
    if (a.flags & A_ITALIC) s += ";3";
    if (a.flags & A_UNDER) s += ";4";
    if (a.flags & A_INVERSE) s += ";7";
    auto color = [&](uint32_t c, const char* p38) {
        if (!c) return;
        if ((c & 0xFF000000u) == PAL) { s += ";"; s += p38; s += ";5;"; s += std::to_string(c & 0xFF); }
        else { int v = c & 0xFFFFFF; s += ";"; s += p38; s += ";2;"; s += std::to_string((v >> 16) & 0xFF);
               s += ";"; s += std::to_string((v >> 8) & 0xFF); s += ";"; s += std::to_string(v & 0xFF); }
    };
    color(a.fg, "38");
    color(a.bg, "48");
    s += "m";
    return s;
}

// ---------------------------------------------------------------------------
// Sixel images. Decoded once to an RGBA raster (px[y*Ph + x]; 0 = unset/transparent,
// else 0xFFrrggbb). Anchored in the grid at an absolute row + column. The 18px-strip
// rendering re-encodes row ranges on demand (next milestone).
// ---------------------------------------------------------------------------
struct Image {
    size_t row = 0;       // absolute grid row of the image's top
    int col = 0;          // column of the image's left edge
    int Ph = 0, Pv = 0;   // painted pixel size
    std::vector<uint32_t> px;
};

// Decode a sixel DCS payload (everything after `ESC P`, up to but not including ST)
// into img. Handles the intro params, `"`-raster attrs, `#`-colour define/select
// (RGB; HLS approximated), the `?`..`~` sixel bytes, `!`-run-length, `$` (CR) and
// `-` (graphics newline). Returns true on a non-empty raster.
bool decodeSixel(const std::string& s, Image& img) {
    size_t i = 0;
    while (i < s.size() && s[i] != 'q') ++i;   // skip intro params P1;P2;P3
    if (i >= s.size()) return false;
    ++i;                                       // past 'q'

    auto readNums = [&](std::vector<int>& v) {
        std::string n;
        while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == ';')) {
            if (s[i] == ';') { v.push_back(n.empty() ? 0 : std::atoi(n.c_str())); n.clear(); }
            else n += s[i];
            ++i;
        }
        if (!n.empty()) v.push_back(std::atoi(n.c_str()));
    };

    int Ph = 0, Pv = 0;
    std::unordered_map<int, uint32_t> pal;
    int cur = 0, x = 0, band = 0, maxX = 0;
    std::vector<std::vector<uint32_t>> g;       // g[pixelRow][x]
    auto colorOf = [&](int idx) { auto it = pal.find(idx); return it != pal.end() ? it->second : 0xFF000000u; };
    auto setpx = [&](int px_x, int px_y, uint32_t c) {
        if ((size_t)px_y >= g.size()) g.resize(px_y + 1);
        auto& r = g[px_y];
        if ((size_t)px_x >= r.size()) r.resize(px_x + 1, 0);
        r[px_x] = c;
    };
    auto plot = [&](int bits, int n) {
        for (int r = 0; r < n; ++r) {
            for (int b = 0; b < 6; ++b) if (bits & (1 << b)) setpx(x, band * 6 + b, colorOf(cur));
            ++x;
        }
        if (x > maxX) maxX = x;
    };

    while (i < s.size()) {
        char c = s[i];
        if (c == '"') { ++i; std::vector<int> v; readNums(v); if (v.size() >= 4) { Ph = v[2]; Pv = v[3]; } continue; }
        if (c == '#') {
            ++i; std::vector<int> v; readNums(v);
            if (v.size() == 1) cur = v[0];
            else if (v.size() >= 5) {
                int idx = v[0], pu = v[1];
                uint32_t rgb = (pu == 2)
                    ? (0xFF000000u | ((v[2] * 255 / 100) << 16) | ((v[3] * 255 / 100) << 8) | (v[4] * 255 / 100))
                    : 0xFF808080u;            // HLS: approximate (our sources use RGB)
                pal[idx] = rgb; cur = idx;
            }
            continue;
        }
        if (c == '!') {
            ++i; std::string n; while (i < s.size() && std::isdigit((unsigned char)s[i])) n += s[i++];
            int cnt = std::max(1, std::atoi(n.c_str()));
            if (i < s.size() && s[i] >= '?' && s[i] <= '~') plot(s[i++] - '?', cnt);
            continue;
        }
        if (c == '$') { x = 0; ++i; continue; }
        if (c == '-') { x = 0; ++band; ++i; continue; }
        if (c >= '?' && c <= '~') { plot(c - '?', 1); ++i; continue; }
        ++i;                                   // ignore stray bytes (whitespace, etc.)
    }

    if (Ph <= 0) Ph = maxX;
    if (Pv <= 0) Pv = (int)g.size();
    if (Ph <= 0 || Pv <= 0) return false;
    img.Ph = Ph; img.Pv = Pv;
    img.px.assign((size_t)Ph * Pv, 0);
    for (int y = 0; y < Pv && y < (int)g.size(); ++y)
        for (int xx = 0; xx < Ph && xx < (int)g[y].size(); ++xx)
            img.px[(size_t)y * Ph + xx] = g[y][xx];
    return true;
}

// ---------------------------------------------------------------------------
// The emulator: a cell grid (rows grow downward = scrollback) with a screen
// viewport [top, top+H) in which the cursor lives, fed bytes by a state machine.
// ---------------------------------------------------------------------------
struct Emulator {
    int W, H;
    int cellW, cellH;        // pixels per cell (for sizing sixel images)
    std::vector<std::vector<Cell>> rows;
    std::vector<Image> images;
    size_t top = 0;          // absolute index of screen row 0
    int cr = 0, cc = 0;      // cursor, screen-relative
    Attr pen;                // current pen
    int sr = 0, sc = 0;      // DECSC save
    Attr spen;

    // parser state
    enum St { GROUND, ESC, CSI, OSC, DCS } st = GROUND;
    std::string seq;         // accumulated escape payload
    bool escPend = false;    // saw ESC inside OSC/DCS (looking for ST '\')
    char32_t uacc = 0;       // UTF-8 accumulator
    int uneed = 0;

    Emulator(int w, int h, int cw, int ch)
        : W(w < 1 ? 1 : w), H(h < 1 ? 1 : h), cellW(cw < 1 ? 1 : cw), cellH(ch < 1 ? 1 : ch) { ensure(); }

    void ensure() { while (rows.size() < top + static_cast<size_t>(H)) rows.emplace_back(W); }
    std::vector<Cell>& screen(int r) { return rows[top + r]; }
    void scrollUp() { ++top; ensure(); }
    void blank(std::vector<Cell>& L, int a, int b) {
        for (int i = a; i < b && i < W; ++i) L[i] = Cell{};
    }

    void lfWrap() { if (cr + 1 >= H) scrollUp(); else ++cr; }
    void lf() { cc = 0; lfWrap(); }                       // \n acts as CR+LF (no tty driver here)
    void put(char32_t cp) {
        if (cc >= W) { cc = 0; lfWrap(); }
        Cell& c = screen(cr)[cc];
        c.cp = cp; c.attr = internAttr(pen); c.width = 1;
        ++cc;
    }
    void tab() { cc = std::min(W - 1, ((cc / 8) + 1) * 8); }
    void bs() { if (cc > 0) --cc; }
    void up(int n) { cr = std::max(0, cr - n); }
    void down(int n) { cr = std::min(H - 1, cr + n); }
    void right(int n) { cc = std::min(W - 1, cc + n); }
    void left(int n) { cc = std::max(0, cc - n); }
    void col(int c) { cc = std::min(std::max(0, c), W - 1); }
    void row(int r) { cr = std::min(std::max(0, r), H - 1); }
    void cup(int r, int c) { row(r); col(c); }
    void decsc() { sr = cr; sc = cc; spen = pen; }
    void decrc() { cr = sr; cc = sc; pen = spen; }
    void el(int m) {                                       // erase in line
        auto& L = screen(cr);
        if (m == 1) blank(L, 0, cc + 1);
        else if (m == 2) blank(L, 0, W);
        else blank(L, cc, W);
    }
    void ed(int m) {                                       // erase in display
        if (m == 1) { for (int r = 0; r < cr; ++r) blank(screen(r), 0, W); el(1); }
        else if (m == 2) { for (int r = 0; r < H; ++r) blank(screen(r), 0, W); }
        else { el(0); for (int r = cr + 1; r < H; ++r) blank(screen(r), 0, W); }
    }

    // ---- parser ----------------------------------------------------------
    void feed(const char* p, size_t n) {
        for (size_t k = 0; k < n; ++k) {
            unsigned char b = static_cast<unsigned char>(p[k]);
            switch (st) {
                case GROUND: ground(b); break;
                case ESC: esc(b); break;
                case CSI: csi(b); break;
                case OSC: osc(b); break;
                case DCS: dcs(b); break;
            }
        }
    }

    void ground(unsigned char b) {
        if (b == 0x1B) { st = ESC; return; }
        if (uneed > 0) {                                   // UTF-8 continuation
            if ((b & 0xC0) == 0x80) { uacc = (uacc << 6) | (b & 0x3F); if (--uneed == 0) put(uacc); }
            else { uneed = 0; put(0xFFFD); ground(b); }    // malformed: replace, reprocess
            return;
        }
        if (b < 0x20) {
            switch (b) {
                case '\n': lf(); break;
                case '\r': cc = 0; break;
                case '\t': tab(); break;
                case '\b': bs(); break;
                default: break;                            // ignore other C0
            }
            return;
        }
        if (b < 0x80) { put(b); return; }
        if ((b & 0xE0) == 0xC0) { uacc = b & 0x1F; uneed = 1; }
        else if ((b & 0xF0) == 0xE0) { uacc = b & 0x0F; uneed = 2; }
        else if ((b & 0xF8) == 0xF0) { uacc = b & 0x07; uneed = 3; }
        else put(0xFFFD);
    }

    void esc(unsigned char b) {
        switch (b) {
            case '[': st = CSI; seq.clear(); break;
            case ']': st = OSC; seq.clear(); escPend = false; break;
            case 'P': st = DCS; seq.clear(); escPend = false; break;
            case '7': decsc(); st = GROUND; break;
            case '8': decrc(); st = GROUND; break;
            case 'M': up(1); st = GROUND; break;           // RI (no scroll-down for now)
            default: st = GROUND; break;                   // ignore other ESC x
        }
    }

    void csi(unsigned char b) {
        if (b >= 0x40 && b <= 0x7E) { dispatchCsi(static_cast<char>(b)); st = GROUND; return; }
        seq.push_back(static_cast<char>(b));
    }
    // OSC: skip until BEL or ST (kept as a no-op; OSC 8 parsing lands here later).
    void osc(unsigned char b) {
        if (b == 0x07) { st = GROUND; return; }
        if (escPend) { if (b == '\\') { st = GROUND; return; } escPend = false; }
        if (b == 0x1B) { escPend = true; return; }
        seq.push_back(static_cast<char>(b));
    }
    // DCS (sixel): accumulate until ST, then decode + anchor the image.
    void dcs(unsigned char b) {
        if (escPend) { if (b == '\\') { finishSixel(); st = GROUND; return; } escPend = false; }
        if (b == 0x1B) { escPend = true; return; }
        seq.push_back(static_cast<char>(b));
    }
    // Decode the captured sixel, anchor it at the cursor, and advance the cursor to
    // the row directly below the image, column 0 (the terminal's post-sixel behaviour).
    void finishSixel() {
        Image img;
        if (!decodeSixel(seq, img)) return;
        img.row = top + cr; img.col = cc;
        images.push_back(std::move(img));
        int rowsCells = (images.back().Pv + cellH - 1) / cellH;
        cc = 0;
        for (int k = 0; k < rowsCells; ++k) { if (cr + 1 >= H) scrollUp(); else ++cr; }
    }

    void dispatchCsi(char final) {
        std::string s = seq;
        if (!s.empty() && (s[0] == '?' || s[0] == '>' || s[0] == '<' || s[0] == '=')) s.erase(0, 1);
        std::vector<int> ps;
        { size_t i = 0; while (i <= s.size()) {
              size_t j = s.find(';', i);
              std::string t = s.substr(i, (j == std::string::npos ? s.size() : j) - i);
              ps.push_back(t.empty() ? -1 : std::atoi(t.c_str()));
              if (j == std::string::npos) break;
              i = j + 1;
          } }
        auto P = [&](size_t i, int def) { return (i < ps.size() && ps[i] >= 0) ? ps[i] : def; };
        auto P1 = [&](size_t i) { return std::max(1, P(i, 1)); };  // 0/absent -> 1
        switch (final) {
            case 'A': up(P1(0)); break;
            case 'B': down(P1(0)); break;
            case 'C': right(P1(0)); break;
            case 'D': left(P1(0)); break;
            case 'E': cc = 0; down(P1(0)); break;
            case 'F': cc = 0; up(P1(0)); break;
            case 'G': case '`': col(P1(0) - 1); break;
            case 'd': row(P1(0) - 1); break;
            case 'H': case 'f': cup(P1(0) - 1, P1(1) - 1); break;
            case 'J': ed(P(0, 0)); break;
            case 'K': el(P(0, 0)); break;
            case 'm': applySgr(ps); break;
            default: break;                                 // ignore the rest (YAGNI)
        }
    }

    void applySgr(std::vector<int>& ps) {
        if (ps.empty()) ps.push_back(0);
        for (size_t i = 0; i < ps.size(); ++i) {
            int c = ps[i] < 0 ? 0 : ps[i];
            switch (c) {
                case 0: pen = Attr{}; break;
                case 1: pen.flags |= A_BOLD; break;
                case 2: pen.flags |= A_DIM; break;
                case 3: pen.flags |= A_ITALIC; break;
                case 4: pen.flags |= A_UNDER; break;
                case 7: pen.flags |= A_INVERSE; break;
                case 22: pen.flags &= ~(A_BOLD | A_DIM); break;
                case 23: pen.flags &= ~A_ITALIC; break;
                case 24: pen.flags &= ~A_UNDER; break;
                case 27: pen.flags &= ~A_INVERSE; break;
                case 39: pen.fg = 0; break;
                case 49: pen.bg = 0; break;
                case 38: case 48: {
                    uint32_t& slot = (c == 38) ? pen.fg : pen.bg;
                    int mode = (i + 1 < ps.size()) ? ps[i + 1] : -1;
                    if (mode == 5) { slot = pal((i + 2 < ps.size() && ps[i + 2] >= 0) ? ps[i + 2] : 0); i += 2; }
                    else if (mode == 2) {
                        int r = (i + 2 < ps.size() && ps[i + 2] >= 0) ? ps[i + 2] : 0;
                        int g = (i + 3 < ps.size() && ps[i + 3] >= 0) ? ps[i + 3] : 0;
                        int bb = (i + 4 < ps.size() && ps[i + 4] >= 0) ? ps[i + 4] : 0;
                        slot = tru(r, g, bb); i += 4;
                    }
                    break;
                }
                default:
                    if (c >= 30 && c <= 37) pen.fg = pal(c - 30);
                    else if (c >= 90 && c <= 97) pen.fg = pal(c - 90 + 8);
                    else if (c >= 40 && c <= 47) pen.bg = pal(c - 40);
                    else if (c >= 100 && c <= 107) pen.bg = pal(c - 100 + 8);
                    break;
            }
        }
    }

    // Number of meaningful rows = up to the last non-blank row (trailing blanks trimmed).
    size_t contentRows() const {
        size_t last = 0;
        for (size_t r = 0; r < rows.size(); ++r) {
            for (const Cell& c : rows[r]) {
                if (!(c.cp == U' ' && c.attr == 0)) { last = r + 1; break; }
            }
        }
        return last;
    }

    void renderRow(size_t absRow, std::string& out) const {
        if (absRow >= rows.size()) return;
        const std::vector<Cell>& L = rows[absRow];
        int last = -1;
        for (int i = 0; i < static_cast<int>(L.size()); ++i)
            if (!(L[i].cp == U' ' && L[i].attr == 0)) last = i;
        uint16_t cur = 0;
        for (int i = 0; i <= last; ++i) {
            if (L[i].attr != cur) { out += sgrFor(L[i].attr); cur = L[i].attr; }
            appendUtf8(out, L[i].cp);
        }
        if (cur != 0) out += "\033[0m";
    }
};

// ---------------------------------------------------------------------------
// terminal raw-mode (restored on exit/signal)
// ---------------------------------------------------------------------------
int gTtyFd = -1;
struct termios gSaved;
bool gRaw = false;
void restoreTty() { if (gRaw && gTtyFd >= 0) { tcsetattr(gTtyFd, TCSANOW, &gSaved); gRaw = false; } }
void onSignal(int sig) { restoreTty(); signal(sig, SIG_DFL); raise(sig); }
bool enterRaw() {
    if (tcgetattr(gTtyFd, &gSaved) != 0) return false;
    struct termios raw = gSaved;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    if (tcsetattr(gTtyFd, TCSANOW, &raw) != 0) return false;
    gRaw = true; return true;
}

std::string readAll(int fd) {
    std::string s; char buf[65536]; ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) s.append(buf, static_cast<size_t>(n));
    return s;
}

int envInt(const char* name, int def) {
    const char* v = std::getenv(name);
    if (!v) return def;
    int x = std::atoi(v);
    return x > 0 ? x : def;
}

}  // namespace

int main(int argc, char** argv) {
    bool dump = false, imginfo = false;
    const char* path = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dump") == 0) dump = true;
        else if (std::strcmp(argv[i], "--imginfo") == 0) imginfo = true;
        else if (std::strcmp(argv[i], "-") == 0) path = nullptr;
        else path = argv[i];
    }

    std::string data;
    if (path) {
        int fd = open(path, O_RDONLY);
        if (fd < 0) { std::perror(path); return 1; }
        data = readAll(fd); close(fd);
    } else {
        data = readAll(STDIN_FILENO);
    }

    internAttr(Attr{});  // id 0 = default

    // Terminal size: real size when interactive, else env/defaults (for --dump/tests).
    int H = envInt("LINES", 24), W = envInt("COLUMNS", 80);
    int cellW = envInt("GMORE_CELLW", 0), cellH = envInt("GMORE_CELLH", 0);
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        H = ws.ws_row; W = ws.ws_col;
        if (!cellW && ws.ws_xpixel > 0 && ws.ws_col > 0) cellW = ws.ws_xpixel / ws.ws_col;
        if (!cellH && ws.ws_ypixel > 0 && ws.ws_row > 0) cellH = ws.ws_ypixel / ws.ws_row;
    }
    if (!cellW) cellW = 8;
    if (!cellH) cellH = 16;

    // Not a terminal and not --dump/--imginfo: pass through verbatim (pager-as-cat).
    if (!isatty(STDOUT_FILENO) && !dump && !imginfo) {
        fwrite(data.data(), 1, data.size(), stdout);
        return 0;
    }

    Emulator em(W, H, cellW, cellH);
    em.feed(data.data(), data.size());
    const size_t total = em.contentRows();

    if (imginfo) {
        for (size_t k = 0; k < em.images.size(); ++k) {
            const Image& I = em.images[k];
            int cols = (I.Ph + cellW - 1) / cellW, rws = (I.Pv + cellH - 1) / cellH;
            std::printf("image %zu @%zu,%d %dx%dpx %dx%dcells\n", k + 1, I.row, I.col, I.Ph, I.Pv, cols, rws);
            if (I.Ph <= 40 && I.Pv <= 40) {       // small enough to show as ASCII
                for (int y = 0; y < I.Pv; ++y) {
                    std::string line;
                    for (int x = 0; x < I.Ph; ++x) line += I.px[(size_t)y * I.Ph + x] ? '#' : '.';
                    std::printf("%s\n", line.c_str());
                }
            }
        }
        return 0;
    }

    auto emitRow = [&](size_t r) { std::string s; em.renderRow(r, s); fwrite(s.data(), 1, s.size(), stdout); };

    if (dump) {
        for (size_t r = 0; r < total; ++r) { emitRow(r); std::fputc('\n', stdout); }
        return 0;
    }

    // Fits on one screen: print and exit, no prompt.
    if (total <= static_cast<size_t>(H - 1)) {
        for (size_t r = 0; r < total; ++r) { emitRow(r); std::fputc('\n', stdout); }
        return 0;
    }

    gTtyFd = open("/dev/tty", O_RDWR);
    if (gTtyFd < 0) gTtyFd = STDIN_FILENO;
    if (!enterRaw()) {
        for (size_t r = 0; r < total; ++r) { emitRow(r); std::fputc('\n', stdout); }
        return 0;
    }
    std::atexit(restoreTty);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    size_t next = 0;
    for (; next < static_cast<size_t>(H - 1) && next < total; ++next) { emitRow(next); std::fputc('\n', stdout); }

    auto showPrompt = [&] {
        if (next >= total) std::fputs("\033[7m(END)\033[27m", stdout);
        else std::fprintf(stdout, "\033[7m--More--(%d%%)\033[27m", static_cast<int>(next * 100 / total));
        std::fflush(stdout);
    };
    auto clearPrompt = [&] { std::fputs("\r\033[K", stdout); };
    auto advance = [&](int n) {
        for (int k = 0; k < n && next < total; ++k) { emitRow(next); std::fputc('\n', stdout); ++next; }
    };

    for (;;) {
        showPrompt();
        unsigned char c;
        if (read(gTtyFd, &c, 1) != 1) break;
        if (c == 'q' || c == 'Q') break;
        clearPrompt();
        if (next >= total) { if (c == ' ') break; continue; }
        switch (c) {
            case ' ': case 'f': advance(H - 1); break;
            case '\r': case '\n': case 'j': advance(1); break;
            default: break;
        }
    }
    clearPrompt();
    std::fflush(stdout);
    return 0;
}
