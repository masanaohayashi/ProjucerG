#if defined (__APPLE__) && defined (__MACH__)
 #include <TargetConditionals.h>

 #if TARGET_OS_OSX
  #include <ApplicationServices/ApplicationServices.h>
 #endif
#endif

#include "../Application/jucer_Headers.h"
#include "jucer_TerminalView.h"

static_assert (std::is_base_of_v<juce::TextInputTarget, TerminalView>);

#if JUCE_MAC
namespace
{
struct ScopedCFType
{
    explicit ScopedCFType (CFTypeRef value = nullptr) : object (value) {}

    ~ScopedCFType()
    {
        if (object != nullptr)
            CFRelease (object);
    }

    ScopedCFType (const ScopedCFType&) = delete;
    ScopedCFType& operator= (const ScopedCFType&) = delete;

    CFTypeRef object = nullptr;
};

juce::File saveClipboardImageToTemporaryFile()
{
    PasteboardRef rawPasteboard = nullptr;

    if (PasteboardCreate (kPasteboardClipboard, &rawPasteboard) != noErr)
        return {};

    ScopedCFType pasteboardOwner { rawPasteboard };
    PasteboardSynchronize (rawPasteboard);

    ItemCount numItems = 0;

    if (PasteboardGetItemCount (rawPasteboard, &numItems) != noErr)
        return {};

    CFDataRef imageData = nullptr;
    ScopedCFType imageDataOwner;
    juce::String fileExtension;

    const struct { CFStringRef type; const char* extension; } imageFlavours[] =
    {
        { CFSTR ("public.png"),  ".png"  },
        { CFSTR ("public.jpeg"), ".jpg"  },
        { CFSTR ("public.tiff"), ".tiff" }
    };

    for (ItemCount index = 1; index <= numItems && imageData == nullptr; ++index)
    {
        PasteboardItemID item = nullptr;

        if (PasteboardGetItemIdentifier (rawPasteboard, (CFIndex) index, &item) != noErr)
            continue;

        for (const auto& flavour : imageFlavours)
        {
            if (PasteboardCopyItemFlavorData (rawPasteboard, item, flavour.type, &imageData) == noErr)
            {
                imageDataOwner.object = imageData;
                fileExtension = flavour.extension;
                break;
            }
        }
    }

    if (imageData == nullptr || CFDataGetLength (imageData) == 0)
        return {};

    const auto directory = juce::File ("/tmp/projucer-terminal-clipboard");

    if (directory.createDirectory().failed())
        return {};

    const auto destination = directory.getChildFile (juce::Uuid().toString() + fileExtension);

    if (! destination.replaceWithData (CFDataGetBytePtr (imageData),
                                       (size_t) CFDataGetLength (imageData)))
        return {};

    return destination;
}
}
#endif

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

    addAndMakeVisible (scrollBar);
    scrollBar.setAutoHide (false);
    scrollBar.setSingleStepSize (1.0);
    scrollBar.addListener (this);

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
    updateScrollBar();
}

TerminalView::~TerminalView()
{
    stopTimer();
    cancelPendingUpdate();
    scrollBar.removeListener (this);

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
    bool consumedBytes = false;

    for (;;)
    {
        int start1, size1, start2, size2;
        incoming.prepareToRead (incoming.getNumReady(), start1, size1, start2, size2);

        if (size1 + size2 == 0)
            break;

        if (size1 > 0) vterm_input_write (vterm, incomingBuffer.get() + start1, (size_t) size1);
        if (size2 > 0) vterm_input_write (vterm, incomingBuffer.get() + start2, (size_t) size2);

        incoming.finishedRead (size1 + size2);
        consumedBytes = true;
    }

    if (consumedBytes)
    {
        clearSelection();
        setScrollOffset (0);
        updateScrollBar();
    }
}

void TerminalView::sendToShell (const char* bytes, int numBytes)
{
    setScrollOffset (0);

    // Whatever the pty will not take right now stays in pendingOutput for the
    // timer to retry, rather than spinning here on the message thread.
    queueTerminalOutput (pendingOutput, bytes, numBytes,
                         [this] (const char* b, int n) { return pty.writeBytes (b, n); });
}

//==============================================================================
int TerminalView::damageCallback (VTermRect rect, void* user)
{
    auto& self = *static_cast<TerminalView*> (user);

    const auto damaged = getTerminalDamageBounds (rect.start_row, rect.start_col,
                                                  rect.end_row, rect.end_col,
                                                  self.cellWidth, self.cellHeight);

    self.repaint ({ damaged.x, damaged.y, damaged.width, damaged.height });
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

int TerminalView::pushScrollbackLine (int cols, const VTermScreenCell* cells, void* user)
{
    auto& self = *static_cast<TerminalView*> (user);
    ScrollbackLine line;
    line.cells.assign (cells, cells + cols);
    self.scrollback.push_back (std::move (line));

    while ((int) self.scrollback.size() > maxScrollbackLines)
    {
        self.scrollback.pop_front();
        self.clearSelection();
    }

    self.updateScrollBar();

    return 1;
}

void TerminalView::outputCallback (const char* bytes, size_t len, void* user)
{
    static_cast<TerminalView*> (user)->sendToShell (bytes, (int) len);
}

//==============================================================================
juce::Rectangle<int> TerminalView::getCellBounds (int row, int column, int widthInCells) const
{
    const auto r = getTerminalCellBounds (row, column, widthInCells, cellWidth, cellHeight);

    return { r.x, r.y, r.width, r.height };
}

juce::Rectangle<int> TerminalView::getTerminalBounds() const
{
    auto bounds = getLocalBounds();
    bounds.removeFromRight (scrollBar.getWidth());
    return bounds;
}

void TerminalView::updateGridSizeFromBounds()
{
    const auto bounds = getTerminalBounds();
    const auto size = getTerminalGridSize (bounds.getWidth(), bounds.getHeight(), cellWidth, cellHeight);

    if (size.numColumns == numColumns && size.numRows == numRows)
        return;

    numColumns = size.numColumns;
    numRows = size.numRows;

    // Both halves must be told: libvterm so its model reflows, and the pty so
    // the child gets SIGWINCH and redraws itself.
    vterm_set_size (vterm, numRows, numColumns);
    pty.setSize (numColumns, numRows);

    updateScrollBar();
    repaint();
}

void TerminalView::resized()
{
    const int width = juce::jmax (10, getLookAndFeel().getDefaultScrollbarWidth());
    scrollBar.setBounds (getLocalBounds().removeFromRight (width));
    updateGridSizeFromBounds();
}

void TerminalView::updateScrollBar()
{
    const double history = (double) scrollback.size();
    const double visible = (double) numRows;

    scrollBar.setRangeLimits (0.0, history + visible, juce::dontSendNotification);
    scrollBar.setCurrentRange (history - (double) scrollOffset, visible,
                               juce::dontSendNotification);
}

void TerminalView::setScrollOffset (int newOffset)
{
    newOffset = juce::jlimit (0, (int) scrollback.size(), newOffset);

    if (newOffset == scrollOffset)
        return;

    scrollOffset = newOffset;
    updateScrollBar();
    repaint();
}

void TerminalView::scrollBarMoved (juce::ScrollBar* moved, double newRangeStart)
{
    if (moved != &scrollBar)
        return;

    setScrollOffset ((int) scrollback.size() - juce::roundToInt (newRangeStart));
}

void TerminalView::timerCallback()
{
    cursorBlinkOn = ! cursorBlinkOn;
    repaint (getCellBounds (cursorRow, cursorColumn));

    if (! pendingOutput.empty())
        drainTerminalOutput (pendingOutput,
                             [this] (const char* b, int n) { return pty.writeBytes (b, n); });
}

//==============================================================================
bool TerminalView::getCell (int logicalRow, int column, VTermScreenCell& cell) const
{
    if (logicalRow < 0 || column < 0)
        return false;

    const int historySize = (int) scrollback.size();

    if (logicalRow < historySize)
    {
        const auto& cells = scrollback[(size_t) logicalRow].cells;

        if (column >= (int) cells.size())
            return false;

        cell = cells[(size_t) column];
        return true;
    }

    const int liveRow = logicalRow - historySize;

    if (liveRow >= numRows || column >= numColumns)
        return false;

    return vterm_screen_get_cell (screen, { liveRow, column }, &cell) != 0;
}

void TerminalView::paint (juce::Graphics& g)
{
    const auto defaultBackground = juce::Colour ((juce::uint32) (0xff000000u | terminalDefaultBackgroundRGB));
    const auto defaultForeground = juce::Colour ((juce::uint32) (0xff000000u | terminalDefaultForegroundRGB));
    const auto selectionBackground = findColour (defaultHighlightColourId).withAlpha (0.75f);
    const auto selectionForeground = findColour (defaultHighlightedTextColourId);

    const auto toColour = [] (uint32_t rgb) { return juce::Colour ((juce::uint32) (0xff000000u | rgb)); };
    const auto getCellText = [] (const VTermScreenCell& cell)
    {
        juce::String result;

        for (const auto character : cell.chars)
        {
            if (character == 0 || character == (uint32_t) -1)
                break;

            result += juce::String::charToString ((juce::juce_wchar) character);
        }

        return result;
    };

    g.fillAll (defaultBackground);

    const auto clip = g.getClipBounds();
    const int firstRow = juce::jmax (0, clip.getY() / cellHeight);
    const int lastRow  = juce::jmin (numRows - 1, clip.getBottom() / cellHeight);
    const int historySize = (int) scrollback.size();

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const auto viewportRow = getTerminalViewportRow (historySize, numRows, scrollOffset, row);
        const int logicalRow = viewportRow.fromScrollback ? viewportRow.sourceRow
                                                          : historySize + viewportRow.sourceRow;
        const auto selectedColumns = terminalSelectionActive
                                       ? getTerminalSelectedColumns (selectionAnchor, selectionEnd,
                                                                     logicalRow, numColumns)
                                       : std::pair<int, int> {};

        if (selectedColumns.first < selectedColumns.second)
        {
            const auto first = getCellBounds (row, selectedColumns.first);
            const auto last = getCellBounds (row, selectedColumns.second - 1);
            const auto selectionArea = first.withRight (last.getRight());

            g.setColour (selectionBackground);
            g.fillRect (selectionArea);
        }

        for (int column = 0; column < numColumns; ++column)
        {
            VTermScreenCell cell {};
            const bool haveCell = getCell (logicalRow, column, cell);
            auto style = haveCell ? getTerminalCellStyle (screen, cell,
                                                          terminalDefaultForegroundRGB,
                                                          terminalDefaultBackgroundRGB)
                                  : TerminalCellStyle { 0, terminalDefaultForegroundRGB,
                                                        terminalDefaultBackgroundRGB, false, false, 1 };

            if (style.width == 0)
                continue;                     // the tail of a double-width cell

            const auto area = getCellBounds (row, column, style.width);
            const bool selected = column >= selectedColumns.first && column < selectedColumns.second;

            if (! selected && style.background != terminalDefaultBackgroundRGB)
            {
                g.setColour (toColour (style.background));
                g.fillRect (area);
            }

            if (style.character != 0)
            {
                g.setColour (selected ? selectionForeground : toColour (style.foreground));
                g.setFont (style.bold ? font.withStyle (juce::Font::bold) : font);
                g.drawText (getCellText (cell), area, juce::Justification::centredLeft, false);
            }

            if (style.underline)
            {
                g.setColour (selected ? selectionForeground : toColour (style.foreground));
                g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);
            }
        }
    }

    if (textInputBuffer.isNotEmpty())
    {
        const auto textBounds = getTextBounds ({ 0, textInputBuffer.length() }).getBounds();

        g.setColour (defaultBackground);
        g.fillRect (textBounds);
        g.setColour (defaultForeground);
        g.setFont (font);
        g.drawText (textInputBuffer, textBounds.expanded (1, 0),
                    juce::Justification::centredLeft, false);

        for (const auto underline : temporaryUnderlines)
        {
            const auto bounds = getTextBounds (underline).getBounds();
            g.fillRect (bounds.getX(), bounds.getBottom() - 1, bounds.getWidth(), 1);
        }

        g.fillRect (getCaretRectangleForCharIndex (getCaretPosition()).withWidth (1));
    }

    if (scrollOffset == 0 && textInputBuffer.isEmpty()
         && cursorVisible && cursorBlinkOn && hasKeyboardFocus (true))
    {
        // A block cursor has to reverse the cell rather than wash over it, or
        // the character you are sitting on - the one vim is about to change -
        // is the one character on screen you cannot read.
        const auto area = getCellBounds (cursorRow, cursorColumn);

        VTermScreenCell cell {};
        const bool haveCell = getCell (historySize + cursorRow, cursorColumn, cell);

        const auto style = haveCell ? getTerminalCellStyle (screen, cell,
                                                            terminalDefaultForegroundRGB,
                                                            terminalDefaultBackgroundRGB)
                                    : TerminalCellStyle {};

        g.setColour (haveCell ? toColour (style.foreground) : defaultForeground);
        g.fillRect (area);

        if (haveCell && style.character != 0)
        {
            g.setColour (toColour (style.background));
            g.setFont (style.bold ? font.withStyle (juce::Font::bold) : font);
            g.drawText (getCellText (cell), area, juce::Justification::centredLeft, false);
        }
    }

    if (statusMessage.isNotEmpty())
    {
        g.setColour (defaultForeground.withAlpha (0.6f));
        g.setFont (font);
        g.drawText (statusMessage,
                    getTerminalBounds().removeFromBottom (cellHeight).reduced (4, 0),
                    juce::Justification::centredLeft);
    }
}

//==============================================================================
TerminalSelectionPoint TerminalView::getSelectionPoint (juce::Point<int> point) const
{
    const int row = juce::jlimit (0, numRows - 1, point.y / cellHeight);
    const int column = juce::jlimit (0, numColumns - 1,
                                     (int) ((float) point.x / cellWidth));

    return ::getTerminalSelectionPoint ((int) scrollback.size(), numRows,
                                        scrollOffset, row, column);
}

void TerminalView::clearSelection()
{
    if (! terminalSelectionActive)
        return;

    terminalSelectionActive = false;
    repaint();
}

void TerminalView::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    if (event.mods.isPopupMenu())
        return;

    clearSelection();
    selectionAnchor = getSelectionPoint (event.position.toInt());
    selectionEnd = selectionAnchor;
}

void TerminalView::mouseDrag (const juce::MouseEvent& event)
{
    selectionEnd = getSelectionPoint (event.position.toInt());
    terminalSelectionActive = true;
    repaint();
}

void TerminalView::mouseUp (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() || ! event.mouseWasDraggedSinceMouseDown())
        return;

    selectionEnd = getSelectionPoint (event.position.toInt());
    terminalSelectionActive = true;
    repaint();
}

void TerminalView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    setScrollOffset (scrollOffset + getTerminalWheelRows (wheel.deltaY));
}

juce::String TerminalView::getSelectedText() const
{
    if (! terminalSelectionActive)
        return {};

    auto first = selectionAnchor;
    auto last = selectionEnd;

    if (last < first)
        std::swap (first, last);

    juce::String result;

    for (int row = first.row; row <= last.row; ++row)
    {
        const auto columns = getTerminalSelectedColumns (first, last, row, numColumns);
        juce::String line;

        for (int column = columns.first; column < columns.second; ++column)
        {
            VTermScreenCell cell {};

            if (! getCell (row, column, cell) || cell.chars[0] == 0)
            {
                line += " ";
                continue;
            }

            if (cell.chars[0] == (uint32_t) -1)
                continue;

            for (const auto character : cell.chars)
            {
                if (character == 0)
                    break;

                line += juce::String::charToString ((juce::juce_wchar) character);
            }
        }

        result += line.trimEnd();

        if (row != last.row)
            result += "\n";
    }

    return result;
}

void TerminalView::copySelectionToClipboard() const
{
    const auto text = getSelectedText();

    if (text.isNotEmpty())
        juce::SystemClipboard::copyTextToClipboard (text);
}

void TerminalView::pasteFromClipboard()
{
    if (! shellRunning)
        return;

    juce::String text;

   #if JUCE_MAC
    // A pty can only carry bytes, not an image object. Save the clipboard image
    // and paste its path; terminal clients such as Codex can then attach it.
    if (const auto image = saveClipboardImageToTemporaryFile(); image.existsAsFile())
        text = image.getFullPathName();
   #endif

    if (text.isEmpty())
        text = juce::SystemClipboard::getTextFromClipboard();

    if (text.isEmpty())
        return;

    clearSelection();
    vterm_keyboard_start_paste (vterm);
    sendToShell (text.toRawUTF8(), (int) text.getNumBytesAsUTF8());
    vterm_keyboard_end_paste (vterm);
}

juce::ApplicationCommandTarget* TerminalView::getNextCommandTarget()
{
    return findFirstTargetParentComponent();
}

void TerminalView::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    const juce::CommandID ids[] = { juce::StandardApplicationCommandIDs::copy,
                                    juce::StandardApplicationCommandIDs::paste };

    commands.addArray (ids, juce::numElementsInArray (ids));
}

void TerminalView::getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result)
{
   #if JUCE_MAC || JUCE_IOS
    constexpr auto clipboardModifiers = juce::ModifierKeys::commandModifier;
   #else
    constexpr auto clipboardModifiers = juce::ModifierKeys::ctrlModifier
                                      | juce::ModifierKeys::shiftModifier;
   #endif

    switch (commandID)
    {
        case juce::StandardApplicationCommandIDs::copy:
            result.setInfo (TRANS ("Copy"), TRANS ("Copies the selected terminal text."), "Editing", 0);
            result.setActive (terminalSelectionActive);
            result.defaultKeypresses.add (juce::KeyPress ('c', clipboardModifiers, 0));
            break;

        case juce::StandardApplicationCommandIDs::paste:
            result.setInfo (TRANS ("Paste"), TRANS ("Pastes text into the terminal."), "Editing", 0);
            result.setActive (shellRunning);
            result.defaultKeypresses.add (juce::KeyPress ('v', clipboardModifiers, 0));
            break;

        default:
            break;
    }
}

bool TerminalView::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case juce::StandardApplicationCommandIDs::copy:  copySelectionToClipboard(); return true;
        case juce::StandardApplicationCommandIDs::paste: pasteFromClipboard(); return true;
        default:                                         return false;
    }
}

//==============================================================================
bool TerminalView::isTextInputActive() const
{
    return shellRunning;
}

juce::Range<int> TerminalView::getHighlightedRegion() const
{
    return textInputSelection;
}

void TerminalView::setHighlightedRegion (const juce::Range<int>& range)
{
    textInputSelection = juce::Range<int> { 0, textInputBuffer.length() }.constrainRange (range);
    repaint();
}

void TerminalView::setTemporaryUnderlining (const juce::Array<juce::Range<int>>& underlines)
{
    temporaryUnderlines = underlines;

    if (temporaryUnderlines.isEmpty() && textInputBuffer.isNotEmpty())
    {
        const auto committed = textInputBuffer;
        textInputBuffer.clear();
        textInputSelection = {};
        sendToShell (committed.toRawUTF8(), (int) committed.getNumBytesAsUTF8());
    }

    repaint();
}

juce::String TerminalView::getTextInRange (const juce::Range<int>& range) const
{
    const auto constrained = juce::Range<int> { 0, textInputBuffer.length() }.constrainRange (range);
    return textInputBuffer.substring (constrained.getStart(), constrained.getEnd());
}

void TerminalView::insertTextAtCaret (const juce::String& text)
{
    const auto selection = juce::Range<int> { 0, textInputBuffer.length() }
                               .constrainRange (textInputSelection);

    textInputBuffer = textInputBuffer.replaceSection (selection.getStart(), selection.getLength(), text);
    textInputSelection = juce::Range<int>::emptyRange (selection.getStart() + text.length());

    if (scrollOffset != 0)
        scrollOffset = 0;

    repaint();
}

int TerminalView::getCaretPosition() const
{
    return textInputSelection.getEnd();
}

juce::Rectangle<int> TerminalView::getCaretRectangleForCharIndex (int characterIndex) const
{
    characterIndex = juce::jlimit (0, textInputBuffer.length(), characterIndex);
    const auto prefix = textInputBuffer.substring (0, characterIndex);
    const int offset = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, prefix));
    const auto cell = getCellBounds (cursorRow, cursorColumn);

    return { cell.getX() + offset, cell.getY(), 2, cellHeight };
}

int TerminalView::getTotalNumChars() const
{
    return textInputBuffer.length();
}

int TerminalView::getCharIndexForPoint (juce::Point<int> point) const
{
    for (int index = 0; index < textInputBuffer.length(); ++index)
    {
        const auto left = getCaretRectangleForCharIndex (index).getX();
        const auto right = getCaretRectangleForCharIndex (index + 1).getX();

        if (point.x < (left + right) / 2)
            return index;
    }

    return textInputBuffer.length();
}

juce::RectangleList<int> TerminalView::getTextBounds (juce::Range<int> range) const
{
    juce::RectangleList<int> result;
    range = juce::Range<int> { 0, textInputBuffer.length() }.constrainRange (range);

    for (int index = range.getStart(); index < range.getEnd(); ++index)
    {
        const auto left = getCaretRectangleForCharIndex (index);
        const auto right = getCaretRectangleForCharIndex (index + 1);
        result.add ({ left.getX(), left.getY(), juce::jmax (1, right.getX() - left.getX()), cellHeight });
    }

    return result;
}

bool TerminalView::keyPressed (const juce::KeyPress& key)
{
    if (! shellRunning)
        return false;

    const auto keyModifiers = key.getModifiers();

   #if JUCE_MAC || JUCE_IOS
    const bool isClipboardShortcut = keyModifiers.isCommandDown();
   #else
    const bool isClipboardShortcut = keyModifiers.isCtrlDown() && keyModifiers.isShiftDown();
   #endif

    if (isClipboardShortcut)
    {
        const auto character = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());

        if (character == 'c')
        {
            copySelectionToClipboard();
            return true;
        }

        if (character == 'v')
        {
            pasteFromClipboard();
            return true;
        }
    }

    // Leave other command-key shortcuts to the menu bar, or cmd-Q would end up in the shell.
   #if JUCE_MAC || JUCE_IOS
    if (keyModifiers.isCommandDown())
        return false;
   #endif

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
