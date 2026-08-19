#!/usr/bin/env python3
"""Self-check for the vendored libvterm.

Compiles the unity build of libvterm together with a small C driver, feeds it
canned escape sequences and asserts the resulting cell grid. This is what keeps
us honest that vim-class TUI output is being interpreted, not just echoed.

Run: python3 tools/test_terminal_screen.py
"""
import pathlib, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).parent.parent
TERMINAL = ROOT / "Projucer/Source/Terminal"
UNITY = TERMINAL / "jucer_libvterm_unity.c"

DRIVER = r"""
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "libvterm/include/vterm.h"

static VTerm *vt;
static VTermScreen *screen;

static void feed (const char *s) { vterm_input_write (vt, s, strlen (s)); }

/* The character at (row, col), or '?' for anything not a single plain char. */
static char charAt (int row, int col)
{
    VTermScreenCell cell;
    VTermPos pos = { .row = row, .col = col };
    if (! vterm_screen_get_cell (screen, pos, &cell)) return '?';
    if (cell.chars[0] == 0) return ' ';
    return (char) cell.chars[0];
}

static void rowText (int row, int cols, char *out)
{
    for (int c = 0; c < cols; ++c) out[c] = charAt (row, c);
    out[cols] = 0;
    for (int c = cols - 1; c >= 0 && out[c] == ' '; --c) out[c] = 0;
}

static int gridCols = 80;

static void expectRow (int row, const char *expected)
{
    char got[256];
    rowText (row, gridCols, got);
    if (strcmp (got, expected) != 0)
    {
        printf ("FAIL row %d: expected \"%s\" got \"%s\"\n", row, expected, got);
        exit (1);
    }
}

int main (void)
{
    vt = vterm_new (24, 80);
    vterm_set_utf8 (vt, 1);
    screen = vterm_obtain_screen (vt);
    vterm_screen_reset (screen, 1);
    /* The altscreen buffer is opt-in: without this, DEC 1049 is tracked as a
       mode bit but never actually swaps the visible buffer. */
    vterm_screen_enable_altscreen (screen, 1);

    /* Plain text lands on the first row. */
    feed ("hello");
    expectRow (0, "hello");

    /* CUP moves the cursor absolutely: row 5, column 10 (1-based in the wire
       protocol, 0-based in the API). */
    feed ("\x1b[6;11Hworld");
    expectRow (5, "          world");

    /* SGR sets colour and boldness on subsequent cells. */
    feed ("\x1b[2;1H\x1b[1;31mred\x1b[0m");
    {
        VTermScreenCell cell;
        VTermPos pos = { .row = 1, .col = 0 };
        vterm_screen_get_cell (screen, pos, &cell);
        assert (cell.attrs.bold == 1);
        VTermColor fg = cell.fg;
        vterm_screen_convert_color_to_rgb (screen, &fg);
        if (! (fg.rgb.red > fg.rgb.blue && fg.rgb.red > fg.rgb.green))
        {
            printf ("FAIL: expected a reddish foreground, got %d,%d,%d\n",
                    fg.rgb.red, fg.rgb.green, fg.rgb.blue);
            return 1;
        }
    }

    /* The alternate screen must hide, then restore, the primary screen.
       This is what vim and less switch into, and getting it wrong is the
       classic way a home-made terminal eats the user's scrollback. */
    feed ("\x1b[1;1H\x1b[?1049h");
    expectRow (0, "");
    feed ("alt-screen");
    expectRow (0, "alt-screen");
    feed ("\x1b[?1049l");
    expectRow (0, "hello");

    /* DECSTBM: with a scroll region set, a linefeed at the bottom of the
       region scrolls only inside it. */
    vterm_screen_reset (screen, 1);
    feed ("\x1b[1;3r");          /* scroll region = rows 1..3 */
    feed ("\x1b[1;1Ha\x1b[2;1Hb\x1b[3;1Hc");
    feed ("\x1b[3;1H\n");        /* linefeed at the bottom of the region */
    expectRow (0, "b");
    expectRow (1, "c");

    /* Resizing must not lose the visible text. This is the path the panel
       exercises every time the user drags the separator, so a regression here
       shows up as vim redrawing into a corrupt grid. */
    vterm_screen_reset (screen, 1);
    feed ("\x1b[1;1Hkeep-me");
    vterm_set_size (vt, 30, 100);  gridCols = 100;
    expectRow (0, "keep-me");
    vterm_set_size (vt, 24, 80);   gridCols = 80;
    expectRow (0, "keep-me");

    printf ("all terminal screen checks passed\n");
    vterm_free (vt);
    return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        driver = tmp / "driver.c"
        driver.write_text(DRIVER)
        binary = tmp / "driver"

        compile_cmd = [
            "cc", "-std=c99", "-o", str(binary),
            str(driver), str(UNITY),
            f"-I{TERMINAL}",
            f"-I{TERMINAL / 'libvterm/include'}",
        ]
        result = subprocess.run(compile_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit("compile failed:\n" + result.stdout + result.stderr)

        result = subprocess.run([str(binary)], capture_output=True, text=True)
        print(result.stdout, end="")
        if result.returncode != 0:
            sys.exit("terminal screen check failed:\n" + result.stderr)


if __name__ == "__main__":
    main()
