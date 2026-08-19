#pragma once

#include <cstdint>
#include <utility>

extern "C" {
 #include "libvterm/include/vterm.h"
}

//==============================================================================
/**
    The parts of the terminal that are pure logic: what a cell should look like
    on screen, and what a keystroke should send to the shell.

    Deliberately free of JUCE, so tools/test_terminal_view.py can compile this
    against libvterm alone and assert both without a message loop or a window.
*/

/** What one cell of the grid draws as. Colours are 0xRRGGBB. */
struct TerminalCellStyle
{
    uint32_t character;         /**< 0 means "draw nothing", including for a plain space. */
    uint32_t foreground;
    uint32_t background;
    bool bold;
    bool underline;
    int width;                  /**< 0 for the trailing half of a double-width cell. */
};

inline uint32_t terminalColourToRGB (const VTermScreen* screen, VTermColor colour,
                                     uint32_t fallback, bool isBackground)
{
    if (isBackground ? VTERM_COLOR_IS_DEFAULT_BG (&colour)
                     : VTERM_COLOR_IS_DEFAULT_FG (&colour))
        return fallback;

    vterm_screen_convert_color_to_rgb (screen, &colour);

    return ((uint32_t) colour.rgb.red   << 16)
         | ((uint32_t) colour.rgb.green <<  8)
         |  (uint32_t) colour.rgb.blue;
}

inline TerminalCellStyle getTerminalCellStyle (const VTermScreen* screen,
                                               const VTermScreenCell& cell,
                                               uint32_t defaultForeground,
                                               uint32_t defaultBackground)
{
    TerminalCellStyle style;

    style.character = cell.chars[0];
    style.width     = cell.width;
    style.bold      = cell.attrs.bold != 0;
    style.underline = cell.attrs.underline != 0;

    // libvterm reports the trailing half of a double-width character as a cell
    // whose only content is (uint32_t) -1, still claiming width 1. Drawing it
    // would put a glyph in a column the wide character already covers.
    if (style.character == (uint32_t) -1)
    {
        style.character = 0;
        style.width = 0;
    }

    // A space and an empty cell are the same thing to draw: nothing. Only the
    // background matters for them, which is what makes a reversed run visible.
    if (style.character == ' ' || cell.attrs.conceal)
        style.character = 0;

    style.foreground = terminalColourToRGB (screen, cell.fg, defaultForeground, false);
    style.background = terminalColourToRGB (screen, cell.bg, defaultBackground, true);

    if (cell.attrs.reverse)
        std::swap (style.foreground, style.background);

    return style;
}

//==============================================================================
/** Turns one keystroke into bytes on the shell's input.

    @param specialKey     VTERM_KEY_NONE unless the key is one of the named ones
                          (arrows, enter, escape...), which the caller resolves
                          from its own toolkit's key codes.
    @param textCharacter  the character the keystroke produces, or 0 if none.
    @param keyCode        the raw key code, used only to recover ctrl+letter on
                          platforms that report no character for it.
    @returns true if anything was sent.
*/
inline bool sendTerminalKeyPress (VTerm* vt, VTermKey specialKey,
                                  uint32_t textCharacter, int keyCode, VTermModifier modifiers)
{
    if (specialKey != VTERM_KEY_NONE)
    {
        vterm_keyboard_key (vt, specialKey, modifiers);
        return true;
    }

    if ((modifiers & VTERM_MOD_CTRL) != 0)
    {
        // libvterm wants the bare lowercase letter plus the ctrl modifier. A
        // toolkit hands us either the already-controlified character or nothing
        // at all, and an uppercase letter here would come out as a CSI u
        // sequence rather than the ^C the shell is waiting for.
        if (textCharacter >= 1 && textCharacter <= 26)
            textCharacter += 'a' - 1;
        else if (textCharacter == 0 && keyCode >= 'A' && keyCode <= 'Z')
            textCharacter = (uint32_t) (keyCode - 'A' + 'a');
        else if (textCharacter >= 'A' && textCharacter <= 'Z')
            textCharacter += 'a' - 'A';
    }

    if (textCharacter != 0)
    {
        vterm_keyboard_unichar (vt, textCharacter, modifiers);
        return true;
    }

    return false;
}
