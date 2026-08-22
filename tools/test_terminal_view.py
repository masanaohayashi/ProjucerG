#!/usr/bin/env python3
"""Self-check for the pure logic behind TerminalView.

jucer_TerminalTranslation.h holds the two halves of the view that are not
drawing or event plumbing: turning a libvterm cell into what should be painted,
and turning a keystroke into the bytes the shell receives. Both are free of
JUCE, so they can be compiled against libvterm alone and asserted here without
a window or a message loop.

Run: python3 tools/test_terminal_view.py
"""
import pathlib, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).parent.parent
TERMINAL = ROOT / "Projucer/Source/Terminal"
UNITY = TERMINAL / "jucer_libvterm_unity.c"

DRIVER = r"""
#include <stdio.h>
#include <string.h>
#include <string>

#include "jucer_TerminalTranslation.h"

static const uint32_t DEFAULT_FG = 0xc0c0c0;
static const uint32_t DEFAULT_BG = 0x101010;

static VTerm* vt;
static VTermScreen* screen;
static int failures = 0;

static void feed (const char* s) { vterm_input_write (vt, s, strlen (s)); }

static TerminalCellStyle styleAt (int row, int col)
{
    VTermScreenCell cell;
    VTermPos pos;
    pos.row = row;
    pos.col = col;

    if (! vterm_screen_get_cell (screen, pos, &cell))
    {
        printf ("FAIL: no cell at %d,%d\n", row, col);
        ++failures;
        memset (&cell, 0, sizeof (cell));
    }

    return getTerminalCellStyle (screen, cell, DEFAULT_FG, DEFAULT_BG);
}

#define CHECK(what, cond) do { if (! (cond)) { printf ("FAIL: %s\n", what); ++failures; } } while (0)

//==============================================================================
// Output captured from the keyboard half.
static char sent[256];
static size_t sentLength;

static void collectOutput (const char* s, size_t len, void* user)
{
    (void) user;
    memcpy (sent + sentLength, s, len);
    sentLength += len;
    sent[sentLength] = 0;
}

static void describe (const char* label, const char* bytes, size_t len, char* out)
{
    (void) label;
    char* p = out;
    for (size_t i = 0; i < len; ++i)
        p += sprintf (p, (bytes[i] >= 32 && bytes[i] < 127) ? "%c" : "\\x%02x",
                      (unsigned char) bytes[i]);
    *p = 0;
}

static void expectKey (const char* label, VTermKey special, uint32_t character,
                       int keyCode, VTermModifier mods,
                       const char* expected, size_t expectedLength)
{
    sentLength = 0;
    sent[0] = 0;

    const bool handled = sendTerminalKeyPress (vt, special, character, keyCode, mods);

    if (handled != (expectedLength != 0)
         || sentLength != expectedLength
         || memcmp (sent, expected, expectedLength) != 0)
    {
        char got[512], want[512];
        describe (label, sent, sentLength, got);
        describe (label, expected, expectedLength, want);
        printf ("FAIL %s: expected \"%s\" got \"%s\" (handled=%d)\n",
                label, want, got, (int) handled);
        ++failures;
    }
}

#define EXPECT_KEY(label, special, ch, code, mods, expected) \
    expectKey (label, special, ch, code, mods, expected, sizeof (expected) - 1)

//==============================================================================
int main (void)
{
    CHECK ("terminal default background is black", terminalDefaultBackgroundRGB == 0x000000);
    CHECK ("terminal default foreground is light grey", terminalDefaultForegroundRGB == 0xc0c0c0);
    CHECK ("command-plus increases terminal font size",
           getTerminalFontSizeStep ('+', '+', true, false, false) == 1);
    CHECK ("command-equals also increases terminal font size",
           getTerminalFontSizeStep ('=', 0, true, false, false) == 1);
    CHECK ("command-minus decreases terminal font size",
           getTerminalFontSizeStep ('-', '-', true, false, false) == -1);
    CHECK ("plain plus is not a terminal font shortcut",
           getTerminalFontSizeStep ('+', '+', false, false, false) == 0);
    CHECK ("command-alt-plus is not a terminal font shortcut",
           getTerminalFontSizeStep ('+', '+', true, false, true) == 0);
    CHECK ("terminal font size changes in one-pixel steps",
           getAdjustedTerminalFontHeight (14.0f, 1) == 15.0f
            && getAdjustedTerminalFontHeight (14.0f, -1) == 13.0f);
    CHECK ("terminal font size is clamped",
           getAdjustedTerminalFontHeight (terminalMaximumFontHeight, 1) == terminalMaximumFontHeight
            && getAdjustedTerminalFontHeight (terminalMinimumFontHeight, -1) == terminalMinimumFontHeight);

    {
        const auto live = getTerminalViewportRow (100, 24, 0, 0);
        CHECK ("live viewport starts at live row zero", ! live.fromScrollback && live.sourceRow == 0);

        const auto newestHistory = getTerminalViewportRow (100, 24, 1, 0);
        CHECK ("one wheel row reveals the newest history line",
               newestHistory.fromScrollback && newestHistory.sourceRow == 99);

        const auto shiftedLive = getTerminalViewportRow (100, 24, 1, 1);
        CHECK ("partial scroll shifts the live screen down",
               ! shiftedLive.fromScrollback && shiftedLive.sourceRow == 0);

        const auto oldestHistory = getTerminalViewportRow (100, 24, 100, 0);
        CHECK ("maximum scroll reaches the oldest history line",
               oldestHistory.fromScrollback && oldestHistory.sourceRow == 0);

        CHECK ("small upward trackpad motion still scrolls one row",
               getTerminalWheelRows (0.01f) == 1);
        CHECK ("small downward trackpad motion still scrolls one row",
               getTerminalWheelRows (-0.01f) == -1);
        CHECK ("trackpad scrolling uses the faster three-times speed",
               getTerminalWheelRows (0.2f) == 3
                && getTerminalWheelRows (-0.2f) == -3);

        const TerminalSelectionPoint anchor = getTerminalSelectionPoint (100, 24, 10, 3, 7);
        const TerminalSelectionPoint caret  = getTerminalSelectionPoint (100, 24, 10, 5, 2);
        CHECK ("selection points use combined history and live rows",
               anchor.row == 93 && anchor.column == 7 && caret.row == 95 && caret.column == 2);

        const auto first = getTerminalSelectedColumns (anchor, caret, 93, 80);
        const auto middle = getTerminalSelectedColumns (anchor, caret, 94, 80);
        const auto last = getTerminalSelectedColumns (caret, anchor, 95, 80);
        const auto outside = getTerminalSelectedColumns (anchor, caret, 96, 80);
        CHECK ("selection starts at the anchor column", first.first == 7 && first.second == 80);
        CHECK ("selection includes complete middle rows", middle.first == 0 && middle.second == 80);
        CHECK ("reverse drags normalize and include the final cell", last.first == 0 && last.second == 3);
        CHECK ("rows outside a selection are empty", outside.first == 0 && outside.second == 0);

        const auto word = getTerminalWordColumnRange ("hello world_1", 1);
        CHECK ("a letter expands to the surrounding word", word.first == 0 && word.second == 5);
        const auto second = getTerminalWordColumnRange ("hello world_1", 8);
        CHECK ("a later word includes underscore and digits", second.first == 6 && second.second == 13);
        const auto space = getTerminalWordColumnRange ("hello world_1", 5);
        CHECK ("whitespace selects only that cell", space.first == 5 && space.second == 6);
        const auto pastEnd = getTerminalWordColumnRange ("ab", 8);
        CHECK ("a column past the line still expands the last word",
               pastEnd.first == 0 && pastEnd.second == 2);

        const TerminalRect cell { 10, 20, 8, 16 };
        const auto startHandle = getTerminalStartHandleBounds (cell);
        const auto endHandle = getTerminalEndHandleBounds (cell);
        CHECK ("the start handle sits above the first cell",
               startHandle.y + startHandle.height <= cell.y + 1);
        CHECK ("the end handle sits below the last cell",
               endHandle.y >= cell.y + cell.height - 1);
        CHECK ("a point on the start handle is a start-handle hit",
               hitTerminalSelectionHandle (startHandle, endHandle,
                                           startHandle.x + startHandle.width / 2,
                                           startHandle.y + startHandle.height / 2)
                   == TerminalSelectionHandle::start);
        CHECK ("a point on the end handle is an end-handle hit",
               hitTerminalSelectionHandle (startHandle, endHandle,
                                           endHandle.x + endHandle.width / 2,
                                           endHandle.y + endHandle.height / 2)
                   == TerminalSelectionHandle::end);
        CHECK ("a point just outside the drawn circle still hits the handle",
               hitTerminalSelectionHandle (startHandle, endHandle,
                                           startHandle.x + startHandle.width / 2,
                                           startHandle.y - 4)
                   == TerminalSelectionHandle::start);
        CHECK ("a distant point is not a handle hit",
               hitTerminalSelectionHandle (startHandle, endHandle, 200, 200)
                   == TerminalSelectionHandle::none);

        const auto startCentreX = startHandle.x + startHandle.width / 2;
        const auto startCentreY = startHandle.y + startHandle.height / 2;
        const auto fromStart = adjustPointForTerminalHandleDrag (startCentreX, startCentreY,
                                                                 TerminalSelectionHandle::start);
        CHECK ("dragging the start handle samples the cell below the knob",
               fromStart.second >= cell.y && fromStart.second < cell.y + cell.height);

        const auto endCentreX = endHandle.x + endHandle.width / 2;
        const auto endCentreY = endHandle.y + endHandle.height / 2;
        const auto fromEnd = adjustPointForTerminalHandleDrag (endCentreX, endCentreY,
                                                               TerminalSelectionHandle::end);
        CHECK ("dragging the end handle samples the cell above the knob",
               fromEnd.second >= cell.y && fromEnd.second < cell.y + cell.height);

        TerminalSelectionPoint a { 0, 8 };
        TerminalSelectionPoint b { 0, 2 };
        applyTerminalHandleDrag (a, b, TerminalSelectionHandle::start, { 0, 1 });
        CHECK ("dragging the visual start handle moves the leftmost end",
               a.column == 8 && b.column == 1);
        applyTerminalHandleDrag (a, b, TerminalSelectionHandle::end, { 0, 12 });
        CHECK ("dragging the visual end handle moves the rightmost end",
               a.column == 12 && b.column == 1);
    }

    vt = vterm_new (24, 80);
    vterm_set_utf8 (vt, 1);
    screen = vterm_obtain_screen (vt);
    vterm_screen_reset (screen, 1);

    /* --- cell to drawing ------------------------------------------------- */

    feed ("\x1b[1;1Hhi");
    {
        const TerminalCellStyle s = styleAt (0, 0);
        CHECK ("plain cell keeps its character", s.character == 'h');
        CHECK ("plain cell uses the default foreground", s.foreground == DEFAULT_FG);
        CHECK ("plain cell uses the default background", s.background == DEFAULT_BG);
        CHECK ("plain cell is one column wide", s.width == 1);
        CHECK ("plain cell is not bold", ! s.bold);
    }

    /* An untouched cell and a space both draw nothing; only their background
       matters, which is what makes a reversed run of spaces visible. */
    CHECK ("an empty cell draws nothing", styleAt (0, 40).character == 0);
    feed ("\x1b[2;1H \x1b[3;1H");
    CHECK ("a space draws nothing", styleAt (1, 0).character == 0);

    /* Reverse video swaps the two colours; a status bar is nothing but this. */
    feed ("\x1b[4;1H\x1b[7mR\x1b[0m");
    {
        const TerminalCellStyle s = styleAt (3, 0);
        CHECK ("reverse puts the background in front", s.foreground == DEFAULT_BG);
        CHECK ("reverse puts the foreground behind", s.background == DEFAULT_FG);
    }

    /* An explicit colour must not fall back to the default. */
    feed ("\x1b[5;1H\x1b[1;4;31mX\x1b[0m");
    {
        const TerminalCellStyle s = styleAt (4, 0);
        CHECK ("an explicit colour is not the default", s.foreground != DEFAULT_FG);
        CHECK ("red is reddish", ((s.foreground >> 16) & 0xff) > (s.foreground & 0xff)
                                  && ((s.foreground >> 16) & 0xff) > ((s.foreground >> 8) & 0xff));
        CHECK ("bold is reported", s.bold);
        CHECK ("underline is reported", s.underline);
        CHECK ("an unset background is still the default", s.background == DEFAULT_BG);
    }

    /* A CJK character occupies two columns. libvterm reports the second one as
       a cell whose content is (uint32_t) -1, and still claims width 1 for it -
       drawing that cell would stamp a glyph over the wide one. */
    feed ("\x1b[6;1H\xe3\x81\x82");
    {
        const TerminalCellStyle lead = styleAt (5, 0);
        const TerminalCellStyle tail = styleAt (5, 1);
        CHECK ("a wide character keeps its codepoint", lead.character == 0x3042);
        CHECK ("a wide character spans two columns", lead.width == 2);
        CHECK ("the trailing half draws nothing", tail.character == 0);
        CHECK ("the trailing half has no width", tail.width == 0);
    }

    /* --- output ordering under short writes -------------------------------- */

    /* A pty with a finite buffer: it accepts `budget` more bytes in total and
       then returns 0, so a short write is guaranteed rather than a matter of
       timing. */
    {
        std::string shell;          // what the child actually received, in order
        int budget = 0;

        auto write = [&] (const char* b, int n)
        {
            const int taken = budget < n ? budget : n;
            shell.append (b, (size_t) taken);
            budget -= taken;
            return taken;
        };

        std::string queue;

        /* Buffer full: nothing reaches the shell, and nothing is lost either. */
        queueTerminalOutput (queue, "\x1b[A", 3, write);
        CHECK ("a refused write reaches nobody", shell.empty());
        CHECK ("a refused write is kept whole", queue == "\x1b[A");

        /* Room for one byte, so the arrow key is split across the boundary. */
        budget = 1;
        drainTerminalOutput (queue, write);
        CHECK ("a short write sends what fits", shell == "\x1b");
        CHECK ("a short write keeps the tail", queue == "[A");

        /* The child now drains its buffer, so the pty will accept again - but
           the tail is still sitting in our queue. This is the moment that
           matters: a keystroke arriving now must go behind the tail, not
           straight down the pty ahead of it. Writing it through would send
           "\x1bb[A" - the shell reads ESC-b as a word-left, then prints "[A".
           Both halves of the fix are on trial here, so they are asserted after
           the single call that has to get it right. */
        budget = 64;
        queueTerminalOutput (queue, "b", 1, write);
        CHECK ("the shell receives the original order", shell == "\x1b[Ab");
        CHECK ("a fully drained queue is empty", queue.empty());

        /* Retrying a drain that already succeeded must not resend anything. */
        drainTerminalOutput (queue, write);
        CHECK ("draining an empty queue sends nothing again", shell == "\x1b[Ab");

        /* The straightforward path still writes through in one go. */
        shell.clear();
        queueTerminalOutput (queue, "ls\r", 3, write);
        CHECK ("an accepted write goes straight through", shell == "ls\r");
        CHECK ("an accepted write leaves nothing queued", queue.empty());
    }

    /* --- grid geometry ---------------------------------------------------- */

    /* A deliberately fractional cell width: an integral one hides every
       rounding mistake in here. */
    {
        const float cw = 7.5f;
        const int ch = 16;

        const TerminalRect c = getTerminalCellBounds (2, 3, 1, cw, ch);
        CHECK ("cell x truncates the fractional advance", c.x == 22);
        CHECK ("cell y is a whole number of rows", c.y == 32);
        CHECK ("cell height is one row", c.height == 16);
        /* One pixel wider than the advance, so neighbouring background fills
           meet instead of leaving a seam of stale pixels between columns. */
        CHECK ("a cell overlaps its neighbour by a pixel", c.width == 9);
        CHECK ("cells overlap rather than gap",
               c.x + c.width > getTerminalCellBounds (2, 4, 1, cw, ch).x);

        /* A double-width character must end exactly where the cell it covers
           ends - not at twice its own width, which would over-run by the seam
           pixel and paint into the column after it. */
        const TerminalRect wide = getTerminalCellBounds (2, 3, 2, cw, ch);
        const TerminalRect next = getTerminalCellBounds (2, 4, 1, cw, ch);
        CHECK ("a wide cell starts where a narrow one would", wide.x == c.x);
        CHECK ("a wide cell ends where its second column ends",
               wide.x + wide.width == next.x + next.width);
        CHECK ("a wide cell is not simply twice as wide", wide.width != c.width * 2);

        /* The trailing half reports width 0; asking for its bounds anyway must
           not produce a negative rectangle. */
        CHECK ("a zero width still yields one cell",
               getTerminalCellBounds (2, 3, 0, cw, ch).width == c.width);

        /* libvterm's damage rows and columns are half-open. */
        const TerminalRect d = getTerminalDamageBounds (1, 2, 3, 5, cw, ch);
        CHECK ("damage starts at the first cell", d.x == 15 && d.y == 16);
        CHECK ("damage covers the last row before the end", d.height == 32);
        CHECK ("damage covers the last column before the end", d.width == 24);
        CHECK ("damage stops at the end column, not past it",
               d.x + d.width == getTerminalCellBounds (2, 4, 1, cw, ch).x
                                 + getTerminalCellBounds (2, 4, 1, cw, ch).width);

        /* A single damaged cell is exactly that cell. */
        const TerminalRect one = getTerminalDamageBounds (2, 3, 3, 4, cw, ch);
        CHECK ("a one-cell damage rect is one cell",
               one.x == c.x && one.y == c.y && one.width == c.width && one.height == c.height);

        const TerminalGridSize g = getTerminalGridSize (800, 600, cw, ch);
        CHECK ("only whole columns count", g.numColumns == 106);
        CHECK ("only whole rows count", g.numRows == 37);

        /* The first layout pass hands a component zero bounds. libvterm and
           the pty both reject an empty grid, so this may never return 0. */
        const TerminalGridSize empty = getTerminalGridSize (0, 0, cw, ch);
        CHECK ("an unlaid-out view still has one column", empty.numColumns == 1);
        CHECK ("an unlaid-out view still has one row", empty.numRows == 1);
    }

    /* --- keystroke to bytes ---------------------------------------------- */

    vterm_output_set_callback (vt, collectOutput, NULL);

    EXPECT_KEY ("plain letter", VTERM_KEY_NONE, 'a', 'A', VTERM_MOD_NONE, "a");
    EXPECT_KEY ("return", VTERM_KEY_ENTER, 0, 0, VTERM_MOD_NONE, "\r");
    EXPECT_KEY ("escape", VTERM_KEY_ESCAPE, 0, 0, VTERM_MOD_NONE, "\x1b");
    EXPECT_KEY ("backspace", VTERM_KEY_BACKSPACE, 0, 0, VTERM_MOD_NONE, "\x7f");

    {
        char bytes[4] = {};
        CHECK ("iOS empty replacement with an empty buffer sends DEL",
               ptyBytesForEmptyTextInputReplacement (0, bytes, 4) == 1
                && bytes[0] == '\x7f');
        CHECK ("iOS empty replacement while composing sends nothing",
               ptyBytesForEmptyTextInputReplacement (1, bytes, 4) == 0);
    }
    EXPECT_KEY ("cursor up", VTERM_KEY_UP, 0, 0, VTERM_MOD_NONE, "\x1b[A");
    EXPECT_KEY ("alt-a is an escape prefix", VTERM_KEY_NONE, 'a', 'A', VTERM_MOD_ALT, "\x1b" "a");

    /* Ctrl-C must reach the child as ^C however the toolkit describes it, or
       an interrupt turns into a CSI u sequence the shell prints as garbage.
       macOS hands us the already-controlified character... */
    EXPECT_KEY ("ctrl-C as a control character", VTERM_KEY_NONE, 3, 'C', VTERM_MOD_CTRL, "\x03");
    /* ...some keystrokes report no character at all... */
    EXPECT_KEY ("ctrl-C with no character", VTERM_KEY_NONE, 0, 'C', VTERM_MOD_CTRL, "\x03");
    /* ...and others report the uppercase letter. */
    EXPECT_KEY ("ctrl-C as an uppercase letter", VTERM_KEY_NONE, 'C', 'C', VTERM_MOD_CTRL, "\x03");

    /* A modifier key on its own produces neither a character nor a named key,
       and must not be forwarded as anything. */
    EXPECT_KEY ("a bare modifier sends nothing", VTERM_KEY_NONE, 0, 0, VTERM_MOD_SHIFT, "");

    vterm_free (vt);

    if (failures != 0)
    {
        printf ("%d terminal view check(s) failed\n", failures);
        return 1;
    }

    printf ("all terminal view checks passed\n");
    return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        driver = tmp / "driver.cpp"
        driver.write_text(DRIVER)
        unity = tmp / "unity.o"
        binary = tmp / "driver"

        for cmd in (
            ["cc", "-std=c99", "-c", str(UNITY), "-o", str(unity),
             f"-I{TERMINAL}", f"-I{TERMINAL / 'libvterm/include'}"],
            ["c++", "-std=c++17", "-o", str(binary), str(driver), str(unity),
             f"-I{TERMINAL}", f"-I{TERMINAL / 'libvterm/include'}"],
        ):
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                sys.exit("compile failed:\n" + result.stdout + result.stderr)

        result = subprocess.run([str(binary)], capture_output=True, text=True)
        print(result.stdout, end="")
        if result.returncode != 0:
            sys.exit("terminal view check failed:\n" + result.stderr)


if __name__ == "__main__":
    main()
