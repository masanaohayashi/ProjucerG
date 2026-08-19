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

    /* --- keystroke to bytes ---------------------------------------------- */

    vterm_output_set_callback (vt, collectOutput, NULL);

    EXPECT_KEY ("plain letter", VTERM_KEY_NONE, 'a', 'A', VTERM_MOD_NONE, "a");
    EXPECT_KEY ("return", VTERM_KEY_ENTER, 0, 0, VTERM_MOD_NONE, "\r");
    EXPECT_KEY ("escape", VTERM_KEY_ESCAPE, 0, 0, VTERM_MOD_NONE, "\x1b");
    EXPECT_KEY ("backspace", VTERM_KEY_BACKSPACE, 0, 0, VTERM_MOD_NONE, "\x7f");
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
