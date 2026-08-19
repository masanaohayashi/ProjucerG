#include "../Application/jucer_Headers.h"
#include "jucer_TerminalView.h"

//==============================================================================
/** Drains the pty on its own thread so that the message thread never waits on
    a read. It only moves bytes; nothing here interprets them. */
class TerminalView::Reader final : public juce::Thread
{
public:
    Reader (PseudoTerminal& p, juce::AbstractFifo& f, char* b, TerminalView& v)
        : juce::Thread ("Projucer terminal reader"), pty (p), fifo (f), buffer (b), view (v)
    {
    }

    void run() override
    {
        char chunk[4096];

        while (! threadShouldExit())
        {
            const int numRead = pty.readBytes (chunk, (int) sizeof (chunk));

            if (numRead > 0)
            {
                // Wait for room rather than truncating: a dropped byte is a
                // dropped escape sequence, which corrupts the screen for good.
                while (fifo.getFreeSpace() < numRead && ! threadShouldExit())
                {
                    view.triggerAsyncUpdate();
                    wait (2);
                }

                int start1, size1, start2, size2;
                fifo.prepareToWrite (numRead, start1, size1, start2, size2);

                if (size1 > 0) memcpy (buffer + start1, chunk, (size_t) size1);
                if (size2 > 0) memcpy (buffer + start2, chunk + size1, (size_t) size2);

                fifo.finishedWrite (size1 + size2);
                view.triggerAsyncUpdate();
            }
            else if (numRead < 0)
            {
                view.shellRunning = false;
                view.triggerAsyncUpdate();
                break;                       // the shell has gone
            }
            else
            {
                wait (10);                   // nothing waiting: idle briefly
            }
        }
    }

private:
    PseudoTerminal& pty;
    juce::AbstractFifo& fifo;
    char* buffer;
    TerminalView& view;
};

//==============================================================================
static const VTermScreenCallbacks screenCallbacks =
{
    TerminalView::damageCallback,
    nullptr,                                  // moverect
    TerminalView::moveCursorCallback,
    nullptr,                                  // settermprop
    nullptr,                                  // bell
    nullptr,                                  // resize
    TerminalView::pushScrollbackLine,
    nullptr,                                  // sb_popline
    nullptr,                                  // sb_clear
    nullptr                                   // sb_pushline4
};

TerminalView::TerminalView (const juce::File& workingDirectory)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    font = getAppSettings().appearance.getCodeFont();
    cellWidth  = juce::jmax (1.0f, juce::GlyphArrangement::getStringWidth (font, "M"));
    cellHeight = juce::jmax (1, (int) std::ceil (font.getHeight()));

    vterm = vterm_new (numRows, numColumns);
    vterm_set_utf8 (vterm, 1);

    screen = vterm_obtain_screen (vterm);
    vterm_screen_reset (screen, 1);
    // Opt in, or vim and less will draw over the user's shell history instead
    // of switching to a buffer of their own.
    vterm_screen_enable_altscreen (screen, 1);
    vterm_screen_set_callbacks (screen, &screenCallbacks, this);
    vterm_output_set_callback (vterm, outputCallback, this);

    if (! pty.start (workingDirectory, numColumns, numRows))
    {
        statusMessage = pty.getLastError();
    }
    else
    {
        shellRunning = true;
        reader = std::make_unique<Reader> (pty, incoming, incomingBuffer.get(), *this);
        reader->startThread();
    }

    startTimer (500);                          // cursor blink
}

TerminalView::~TerminalView()
{
    stopTimer();
    cancelPendingUpdate();

    if (reader != nullptr)
    {
        reader->stopThread (2000);
        reader.reset();
    }

    pty.stop();

    if (vterm != nullptr)
    {
        vterm_free (vterm);
        vterm = nullptr;
        screen = nullptr;
    }
}

//==============================================================================
void TerminalView::handleAsyncUpdate()
{
    consumePendingBytes();

    if (! shellRunning && statusMessage.isEmpty())
    {
        statusMessage = "[process exited " + juce::String (pty.getExitCode()) + "]";
        repaint();
    }
}

void TerminalView::consumePendingBytes()
{
    for (;;)
    {
        int start1, size1, start2, size2;
        incoming.prepareToRead (incoming.getNumReady(), start1, size1, start2, size2);

        if (size1 + size2 == 0)
            break;

        if (size1 > 0) vterm_input_write (vterm, incomingBuffer.get() + start1, (size_t) size1);
        if (size2 > 0) vterm_input_write (vterm, incomingBuffer.get() + start2, (size_t) size2);

        incoming.finishedRead (size1 + size2);
    }
}

void TerminalView::sendToShell (const char* bytes, int numBytes)
{
    int offset = 0;

    while (offset < numBytes)
    {
        const int written = pty.writeBytes (bytes + offset, numBytes - offset);

        if (written <= 0)
        {
            // The child's buffer is full. Keep the rest and let the timer retry
            // rather than spinning here on the message thread.
            pendingOutput.append (bytes + offset, (size_t) (numBytes - offset));
            return;
        }

        offset += written;
    }
}

//==============================================================================
int TerminalView::damageCallback (VTermRect rect, void* user)
{
    auto& self = *static_cast<TerminalView*> (user);

    const auto topLeft     = self.getCellBounds (rect.start_row, rect.start_col);
    const auto bottomRight = self.getCellBounds (rect.end_row - 1, rect.end_col - 1);

    self.repaint (topLeft.getUnion (bottomRight));
    return 1;
}

int TerminalView::moveCursorCallback (VTermPos pos, VTermPos oldPos, int visible, void* user)
{
    auto& self = *static_cast<TerminalView*> (user);

    self.repaint (self.getCellBounds (oldPos.row, oldPos.col));
    self.cursorRow = pos.row;
    self.cursorColumn = pos.col;
    self.cursorVisible = visible != 0;
    self.repaint (self.getCellBounds (pos.row, pos.col));

    return 1;
}

int TerminalView::pushScrollbackLine (int, const VTermScreenCell*, void*)
{
    // Scrollback arrives in Task 6. Accepting and dropping the line here keeps
    // libvterm happy in the meantime.
    return 1;
}

void TerminalView::outputCallback (const char* bytes, size_t len, void* user)
{
    static_cast<TerminalView*> (user)->sendToShell (bytes, (int) len);
}

//==============================================================================
juce::Rectangle<int> TerminalView::getCellBounds (int row, int column) const
{
    return { (int) ((float) column * cellWidth), row * cellHeight,
             (int) std::ceil (cellWidth) + 1, cellHeight };
}

void TerminalView::updateGridSizeFromBounds()
{
    const int columns = juce::jmax (1, (int) ((float) getWidth() / cellWidth));
    const int rows    = juce::jmax (1, getHeight() / cellHeight);

    if (columns == numColumns && rows == numRows)
        return;

    numColumns = columns;
    numRows = rows;

    // Both halves must be told: libvterm so its model reflows, and the pty so
    // the child gets SIGWINCH and redraws itself.
    vterm_set_size (vterm, numRows, numColumns);
    pty.setSize (numColumns, numRows);

    repaint();
}

void TerminalView::resized()
{
    updateGridSizeFromBounds();
}

void TerminalView::timerCallback()
{
    cursorBlinkOn = ! cursorBlinkOn;
    repaint (getCellBounds (cursorRow, cursorColumn));

    if (! pendingOutput.empty())
    {
        const auto retry = std::move (pendingOutput);
        pendingOutput.clear();
        sendToShell (retry.data(), (int) retry.size());
    }
}

//==============================================================================
void TerminalView::paint (juce::Graphics& g)
{
    const auto defaultBackground = findColour (juce::CodeEditorComponent::backgroundColourId);
    const auto defaultForeground = findColour (juce::CodeEditorComponent::defaultTextColourId);

    const auto defaultBackgroundRGB = defaultBackground.getARGB() & 0xffffff;
    const auto defaultForegroundRGB = defaultForeground.getARGB() & 0xffffff;

    const auto toColour = [] (uint32_t rgb) { return juce::Colour ((juce::uint32) (0xff000000u | rgb)); };

    g.fillAll (defaultBackground);

    const auto clip = g.getClipBounds();
    const int firstRow = juce::jmax (0, clip.getY() / cellHeight);
    const int lastRow  = juce::jmin (numRows - 1, clip.getBottom() / cellHeight);

    for (int row = firstRow; row <= lastRow; ++row)
    {
        for (int column = 0; column < numColumns; ++column)
        {
            VTermScreenCell cell;
            VTermPos pos;
            pos.row = row;
            pos.col = column;

            if (! vterm_screen_get_cell (screen, pos, &cell))
                continue;

            const auto style = getTerminalCellStyle (screen, cell,
                                                     defaultForegroundRGB, defaultBackgroundRGB);

            if (style.width == 0)
                continue;                     // the tail of a double-width cell

            auto area = getCellBounds (row, column);
            area.setRight (getCellBounds (row, column + style.width - 1).getRight());

            if (style.background != defaultBackgroundRGB)
            {
                g.setColour (toColour (style.background));
                g.fillRect (area);
            }

            if (style.character != 0)
            {
                g.setColour (toColour (style.foreground));
                g.setFont (style.bold ? font.withStyle (juce::Font::bold) : font);
                g.drawText (juce::String::charToString ((juce::juce_wchar) style.character),
                            area, juce::Justification::centredLeft, false);
            }

            if (style.underline)
            {
                g.setColour (toColour (style.foreground));
                g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);
            }
        }
    }

    if (cursorVisible && cursorBlinkOn && hasKeyboardFocus (true))
    {
        g.setColour (defaultForeground.withAlpha (0.7f));
        g.fillRect (getCellBounds (cursorRow, cursorColumn));
    }

    if (statusMessage.isNotEmpty())
    {
        g.setColour (defaultForeground.withAlpha (0.6f));
        g.setFont (font);
        g.drawText (statusMessage,
                    getLocalBounds().removeFromBottom (cellHeight).reduced (4, 0),
                    juce::Justification::centredLeft);
    }
}

//==============================================================================
void TerminalView::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

bool TerminalView::keyPressed (const juce::KeyPress& key)
{
    if (! shellRunning)
        return false;

    const auto keyModifiers = key.getModifiers();

    // Leave the command key to the menu bar, or cmd-Q would end up in the shell.
    if (keyModifiers.isCommandDown())
        return false;

    auto modifiers = VTERM_MOD_NONE;

    if (keyModifiers.isShiftDown()) modifiers = (VTermModifier) (modifiers | VTERM_MOD_SHIFT);
    if (keyModifiers.isAltDown())   modifiers = (VTermModifier) (modifiers | VTERM_MOD_ALT);
    if (keyModifiers.isCtrlDown())  modifiers = (VTermModifier) (modifiers | VTERM_MOD_CTRL);

    // JUCE's key codes are runtime constants, so this table cannot be static.
    const struct { int juceKey; VTermKey vtermKey; } specialKeys[] =
    {
        { juce::KeyPress::returnKey,     VTERM_KEY_ENTER },
        { juce::KeyPress::escapeKey,     VTERM_KEY_ESCAPE },
        { juce::KeyPress::backspaceKey,  VTERM_KEY_BACKSPACE },
        { juce::KeyPress::deleteKey,     VTERM_KEY_DEL },
        { juce::KeyPress::tabKey,        VTERM_KEY_TAB },
        { juce::KeyPress::upKey,         VTERM_KEY_UP },
        { juce::KeyPress::downKey,       VTERM_KEY_DOWN },
        { juce::KeyPress::leftKey,       VTERM_KEY_LEFT },
        { juce::KeyPress::rightKey,      VTERM_KEY_RIGHT },
        { juce::KeyPress::homeKey,       VTERM_KEY_HOME },
        { juce::KeyPress::endKey,        VTERM_KEY_END },
        { juce::KeyPress::pageUpKey,     VTERM_KEY_PAGEUP },
        { juce::KeyPress::pageDownKey,   VTERM_KEY_PAGEDOWN },
        { juce::KeyPress::insertKey,     VTERM_KEY_INS },
    };

    auto specialKey = VTERM_KEY_NONE;

    for (const auto& mapping : specialKeys)
    {
        if (key.getKeyCode() == mapping.juceKey)
        {
            specialKey = mapping.vtermKey;
            break;
        }
    }

    return sendTerminalKeyPress (vterm, specialKey, (uint32_t) key.getTextCharacter(),
                                 key.getKeyCode(), modifiers);
}
