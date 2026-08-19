#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

extern "C" {
 #include "libvterm/include/vterm.h"
}

//==============================================================================
/**
    The parts of the terminal that are pure logic: where a cell lands on screen,
    what it should look like, and what a keystroke should send to the shell.

    Deliberately free of JUCE, so tools/test_terminal_view.py can compile this
    against libvterm alone and assert them without a message loop or a window.
*/

//==============================================================================
/** A pixel rectangle, in the same convention as juce::Rectangle<int>. */
struct TerminalRect
{
    int x, y, width, height;
};

/** Where a run of cells lands on screen.

    @param widthInCells  1 for an ordinary cell, 2 for a double-width character.

    Cells are one pixel wider than their advance so that adjacent background
    fills meet rather than leaving a seam at fractional cell widths.
*/
inline TerminalRect getTerminalCellBounds (int row, int column, int widthInCells,
                                           float cellWidth, int cellHeight)
{
    const int glyphWidth = (int) std::ceil (cellWidth) + 1;
    const int lastColumn = column + std::max (1, widthInCells) - 1;

    const int x     = (int) ((float) column * cellWidth);
    const int right = (int) ((float) lastColumn * cellWidth) + glyphWidth;

    return { x, row * cellHeight, right - x, cellHeight };
}

/** The area a libvterm damage rectangle covers. Its rows and columns are
    half-open, so the last cell touched is one before each end. */
inline TerminalRect getTerminalDamageBounds (int startRow, int startColumn,
                                             int endRow, int endColumn,
                                             float cellWidth, int cellHeight)
{
    const auto first = getTerminalCellBounds (startRow, startColumn, 1, cellWidth, cellHeight);
    const auto last  = getTerminalCellBounds (endRow - 1, endColumn - 1, 1, cellWidth, cellHeight);

    const int x = std::min (first.x, last.x);
    const int y = std::min (first.y, last.y);

    return { x, y,
             std::max (first.x + first.width, last.x + last.width) - x,
             std::max (first.y + first.height, last.y + last.height) - y };
}

/** How many whole cells fit in a component of this size. Never zero: libvterm
    and the pty both reject an empty grid. */
struct TerminalGridSize
{
    int numColumns, numRows;
};

inline TerminalGridSize getTerminalGridSize (int width, int height,
                                             float cellWidth, int cellHeight)
{
    return { std::max (1, (int) ((float) width / cellWidth)),
             std::max (1, height / cellHeight) };
}

/** What one cell of the grid draws as. Colours are 0xRRGGBB. */
inline constexpr uint32_t terminalDefaultBackgroundRGB = 0x000000;
inline constexpr uint32_t terminalDefaultForegroundRGB = 0xc0c0c0;
inline constexpr float terminalMinimumFontHeight = 8.0f;
inline constexpr float terminalMaximumFontHeight = 48.0f;

/** Returns +1/-1 for a terminal font-size shortcut, or zero for another key. */
inline int getTerminalFontSizeStep (int keyCode, uint32_t textCharacter,
                                    bool commandDown, bool controlDown, bool altDown)
{
    if (! commandDown || controlDown || altDown)
        return 0;

    if (textCharacter == '+' || keyCode == '+' || keyCode == '=')
        return 1;

    if (textCharacter == '-' || keyCode == '-')
        return -1;

    return 0;
}

inline float getAdjustedTerminalFontHeight (float currentHeight, int steps)
{
    return std::clamp (currentHeight + (float) steps,
                       terminalMinimumFontHeight, terminalMaximumFontHeight);
}

struct TerminalViewportRow
{
    bool fromScrollback;
    int sourceRow;
};

/** Maps one visible row onto the combined scrollback + live-screen timeline. */
inline TerminalViewportRow getTerminalViewportRow (int scrollbackSize, int liveRows,
                                                   int scrollOffset, int visibleRow)
{
    scrollbackSize = std::max (0, scrollbackSize);
    liveRows = std::max (1, liveRows);
    scrollOffset = std::clamp (scrollOffset, 0, scrollbackSize);
    visibleRow = std::clamp (visibleRow, 0, liveRows - 1);

    const int logicalRow = scrollbackSize - scrollOffset + visibleRow;

    if (logicalRow < scrollbackSize)
        return { true, logicalRow };

    return { false, logicalRow - scrollbackSize };
}

/** Converts trackpad deltas into a useful row movement at 3x the base speed. */
inline int getTerminalWheelRows (float deltaY)
{
    if (deltaY == 0.0f)
        return 0;

    const int scaled = (int) std::lround (deltaY * 15.0f);
    return scaled != 0 ? scaled : (deltaY > 0.0f ? 1 : -1);
}

struct TerminalSelectionPoint
{
    int row;
    int column;
};

inline bool operator< (TerminalSelectionPoint a, TerminalSelectionPoint b)
{
    return a.row < b.row || (a.row == b.row && a.column < b.column);
}

/** Maps a cell in the visible viewport to the combined scrollback + live-screen timeline. */
inline TerminalSelectionPoint getTerminalSelectionPoint (int scrollbackSize, int liveRows,
                                                         int scrollOffset, int visibleRow,
                                                         int column)
{
    const auto viewportRow = getTerminalViewportRow (scrollbackSize, liveRows,
                                                     scrollOffset, visibleRow);

    return { viewportRow.fromScrollback ? viewportRow.sourceRow
                                        : scrollbackSize + viewportRow.sourceRow,
             std::max (0, column) };
}

/** Returns the selected half-open column range for one logical row. */
inline std::pair<int, int> getTerminalSelectedColumns (TerminalSelectionPoint a,
                                                       TerminalSelectionPoint b,
                                                       int row, int numColumns)
{
    if (b < a)
        std::swap (a, b);

    numColumns = std::max (1, numColumns);

    if (row < a.row || row > b.row)
        return { 0, 0 };

    const int start = row == a.row ? std::clamp (a.column, 0, numColumns) : 0;
    const int end = row == b.row ? std::clamp (b.column + 1, 0, numColumns) : numColumns;

    return { std::min (start, end), std::max (start, end) };
}

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
/** Pushes as much of the queue into the shell as it will take right now.

    @param write  takes up to n bytes and returns how many it accepted, 0 when
                  the child's input buffer is full. It must not block.
*/
template <typename WriteFn>
void drainTerminalOutput (std::string& queue, WriteFn&& write)
{
    size_t offset = 0;

    while (offset < queue.size())
    {
        const int written = write (queue.data() + offset, (int) (queue.size() - offset));

        if (written <= 0)
            break;                          // full buffer: the rest waits for the retry

        offset += (size_t) written;
    }

    queue.erase (0, offset);
}

/** Hands bytes to the shell, in order.

    Everything goes through the queue even when it is empty, which is the whole
    point: a short write leaves a tail behind, and anything produced afterwards
    has to land behind that tail. Writing it directly instead would splice the
    newer bytes into the middle of the older escape sequence.
*/
template <typename WriteFn>
void queueTerminalOutput (std::string& queue, const char* bytes, int numBytes, WriteFn&& write)
{
    if (numBytes > 0)
        queue.append (bytes, (size_t) numBytes);

    drainTerminalOutput (queue, write);
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
