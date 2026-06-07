// gmore — a graphics-aware pager: a "more" that will understand sixel images and
// scroll through them by slicing them into strips. Composable (pages timg/mdcat/
// any text+sixel stream), so it can serve as $PAGER. See the design notes in the
// chessfun memory (mdcat-pager-stacking): down-scroll is incremental, up-scroll
// (a later phase) full-repaints, never use scroll regions.
//
// PHASE 1 (this file): the text-only "more" skeleton — read input, raw-tty key
// loop, page/line down, a transient --More-- prompt. No sixels yet; that is
// Phase 2 (tokenize sixel DCS blocks, slice 18px strips via ImageMagick, paint
// them as they scroll in). Forward ("more") only for now.
//
// Keys: space/f = page down, Enter/j = line down, q = quit.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace {

// --- terminal raw-mode management (restored on exit/signal) --------------------
int gTtyFd = -1;
struct termios gSavedTermios;
bool gRawActive = false;

void restoreTty() {
    if (gRawActive && gTtyFd >= 0) {
        tcsetattr(gTtyFd, TCSANOW, &gSavedTermios);
        gRawActive = false;
    }
}

void onSignal(int sig) {
    restoreTty();
    // Re-raise with the default handler so the exit status reflects the signal.
    signal(sig, SIG_DFL);
    raise(sig);
}

// Put the tty into cbreak mode: keys available one at a time, no echo, no signals
// disabled (we keep ISIG so Ctrl-C still works and our handler restores the tty).
bool enterRaw() {
    if (tcgetattr(gTtyFd, &gSavedTermios) != 0) return false;
    struct termios raw = gSavedTermios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(gTtyFd, TCSANOW, &raw) != 0) return false;
    gRawActive = true;
    return true;
}

// --- input --------------------------------------------------------------------
std::string readAll(int fd) {
    std::string s;
    char buf[65536];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) s.append(buf, static_cast<size_t>(n));
    return s;
}

// Split into display lines: bytes up to each '\n', trailing '\r' stripped, the
// newline itself dropped (re-added when printing). ANSI escapes are left intact.
// NOTE (Phase 1 limitation): a line longer than the terminal width will soft-wrap
// and occupy more than one screen row, which throws off the line count slightly.
// Proper wrap accounting comes with the sixel work.
std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        size_t end = (nl == std::string::npos) ? s.size() : nl;
        size_t len = end - start;
        if (len > 0 && s[start + len - 1] == '\r') --len;  // strip CR of CRLF
        lines.emplace_back(s, start, len);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    // A trailing newline yields a final empty line; drop it so we don't show a blank.
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

}  // namespace

int main(int argc, char** argv) {
    // Read the content: a file argument, or stdin (also for "-").
    std::string data;
    if (argc > 1 && std::strcmp(argv[1], "-") != 0) {
        int fd = open(argv[1], O_RDONLY);
        if (fd < 0) { std::perror(argv[1]); return 1; }
        data = readAll(fd);
        close(fd);
    } else {
        data = readAll(STDIN_FILENO);
    }

    // If we are not drawing to a terminal, just pass the content through (cat).
    if (!isatty(STDOUT_FILENO)) {
        fwrite(data.data(), 1, data.size(), stdout);
        return 0;
    }

    std::vector<std::string> lines = splitLines(data);
    const int total = static_cast<int>(lines.size());

    // Terminal size (rows H, cols W); fall back to 24x80.
    int H = 24, W = 80;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        H = ws.ws_row;
        W = ws.ws_col;
    }
    (void)W;  // unused in Phase 1 (no wrap accounting yet)

    // Keys come from the controlling terminal, not stdin (which may be the data).
    gTtyFd = open("/dev/tty", O_RDWR);
    if (gTtyFd < 0) gTtyFd = STDIN_FILENO;

    auto printLine = [&](int i) { fwrite(lines[i].data(), 1, lines[i].size(), stdout); };

    // Fits on one screen: dump it and exit, no prompt (pager-as-cat).
    if (total <= H - 1) {
        for (int i = 0; i < total; ++i) { printLine(i); std::fputc('\n', stdout); }
        return 0;
    }

    if (!enterRaw()) {  // can't get a usable tty for keys: degrade to cat
        for (int i = 0; i < total; ++i) { printLine(i); std::fputc('\n', stdout); }
        return 0;
    }
    std::atexit(restoreTty);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    int next = 0;  // index of the next not-yet-shown line

    // Initial screenful: H-1 lines, leaving the bottom row for the prompt.
    for (; next < H - 1 && next < total; ++next) { printLine(next); std::fputc('\n', stdout); }

    auto showPrompt = [&] {
        if (next >= total) {
            std::fputs("\033[7m(END)\033[27m", stdout);
        } else {
            int pct = static_cast<int>(static_cast<long>(next) * 100 / total);
            std::fprintf(stdout, "\033[7m--More--(%d%%)\033[27m", pct);
        }
        std::fflush(stdout);
    };
    auto clearPrompt = [&] { std::fputs("\r\033[K", stdout); };  // CR + erase line

    // Advance by n lines, scrolling the screen (the terminal does the scroll).
    auto advance = [&](int n) {
        for (int k = 0; k < n && next < total; ++k) { printLine(next); std::fputc('\n', stdout); ++next; }
    };

    for (;;) {
        showPrompt();
        unsigned char c;
        if (read(gTtyFd, &c, 1) != 1) break;
        if (c == 'q' || c == 'Q') break;
        clearPrompt();
        if (next >= total) {           // at EOF: space also quits, others ignored
            if (c == ' ') break;
            continue;
        }
        switch (c) {
            case ' ': case 'f': advance(H - 1); break;
            case '\r': case '\n': case 'j': advance(1); break;
            default: break;            // ignore unknown keys
        }
    }

    clearPrompt();
    std::fflush(stdout);
    return 0;
}
