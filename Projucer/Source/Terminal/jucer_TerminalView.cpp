#if defined (__APPLE__) && defined (__MACH__)
 #include <TargetConditionals.h>

 #if TARGET_OS_OSX
  #include <ApplicationServices/ApplicationServices.h>
 #endif
#endif

#include "../Application/jucer_Headers.h"
#include "jucer_TerminalView.h"

#include <cstring>

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

                if (view.capturing.load (std::memory_order_acquire))
                {
                    const juce::ScopedLock lock (view.captureLock);
                    const auto already = view.captureBytes.getSize();

                    if (already < TerminalView::maxCaptureBytes)
                    {
                        const auto room = TerminalView::maxCaptureBytes - already;
                        view.captureBytes.append (chunk, (size_t) juce::jmin (numRead, (int) room));
                    }
                }

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

class TerminalView::InertialScroller final
    : public juce::AnimatedPosition<juce::AnimatedPositionBehaviours::ContinuousWithMomentum>::Listener
{
public:
    explicit InertialScroller (TerminalView& ownerToUse)
        : owner (ownerToUse)
    {
        position.addListener (this);
        position.behaviour.setFriction (0.08);
        position.behaviour.setMinimumVelocity (0.15);
    }

    void positionChanged (juce::AnimatedPosition<juce::AnimatedPositionBehaviours::ContinuousWithMomentum>&,
                          double newPosition) override
    {
        owner.setScrollOffset (juce::roundToInt (newPosition));
    }

    juce::AnimatedPosition<juce::AnimatedPositionBehaviours::ContinuousWithMomentum> position;
    TerminalView& owner;
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
    scrollBar.setSingleStepSize (1.0);
    scrollBar.addListener (this);
   #if JUCE_IOS
    scrollBar.setVisible (false);
    inertialScroller = std::make_unique<InertialScroller> (*this);
   #else
    scrollBar.setAutoHide (false);
   #endif

    font = getAppSettings().appearance.getCodeFont();
    updateFontMetrics();

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

        if (inertialScroller != nullptr)
            inertialScroller->position.setPosition (0.0);

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

void TerminalView::sendCommandLine (const juce::String& command)
{
    auto line = command;

    while (line.endsWithChar ('\n') || line.endsWithChar ('\r'))
        line = line.dropLastCharacters (1);

    line << "\n";
    sendToShell (line.toRawUTF8(), (int) std::strlen (line.toRawUTF8()));
}

bool TerminalView::isShellRunning() const noexcept
{
    return shellRunning.load (std::memory_order_acquire);
}

void TerminalView::sendInterrupt()
{
    const char ctrlC = 3;
    sendToShell (&ctrlC, 1);
}

void TerminalView::beginCapture()
{
    const juce::ScopedLock lock (captureLock);
    captureBytes.reset();
    capturing.store (true, std::memory_order_release);
}

void TerminalView::endCapture()
{
    capturing.store (false, std::memory_order_release);
}

juce::String TerminalView::copyCapture() const
{
    const juce::ScopedLock lock (captureLock);
    if (captureBytes.getSize() == 0)
        return {};

    return juce::String::createStringFromData (captureBytes.getData(),
                                               (int) captureBytes.getSize());
}

namespace
{
    juce::String stripTerminalNoise (const juce::String& text)
    {
        juce::MemoryOutputStream out;
        const auto* p = text.toRawUTF8();

        while (*p != 0)
        {
            if (*p == '\x1b')
            {
                ++p;

                if (*p == '[')
                {
                    ++p;
                    while (*p != 0 && ! (*p >= '@' && *p <= '~'))
                        ++p;
                    if (*p != 0)
                        ++p;
                    continue;
                }

                if (*p == ']')
                {
                    ++p;
                    while (*p != 0 && *p != '\x07' && ! (*p == '\x1b' && p[1] == '\\'))
                        ++p;
                    if (*p == '\x07')
                        ++p;
                    else if (*p != 0)
                        p += 2;
                    continue;
                }

                if (*p != 0)
                    ++p;
                continue;
            }

            if (*p == '\r')
            {
                ++p;
                if (*p != '\n')
                    out << '\n';
                continue;
            }

            if (*p == '\x07')
            {
                ++p;
                continue;
            }

            out << *p++;
        }

        return out.toString();
    }
}

bool TerminalView::runCommandAndWait (const juce::String& command,
                                      juce::String& output,
                                      int timeoutMs,
                                      std::atomic<bool>& cancelled)
{
    output = {};
    const auto token = "__PROJUCER_DONE_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
    auto wrapped = command.trimEnd();
    wrapped << "; printf '\\n" << token << ":%s\\n' \"$?\"";

    juce::Component::SafePointer<TerminalView> safe (this);
    juce::WaitableEvent started;
    std::atomic<bool> sent { false };

    juce::MessageManager::callAsync ([safe, wrapped, &started, &sent]
    {
        if (safe != nullptr)
        {
            safe->beginCapture();
            safe->sendCommandLine (wrapped);
            sent.store (true, std::memory_order_release);
        }

        started.signal();
    });

    if (! started.wait (2000) || ! sent.load (std::memory_order_acquire) || safe == nullptr)
        return false;

    const auto startedAt = juce::Time::getMillisecondCounter();
    juce::String captured;
    auto sawToken = false;

    while (! cancelled.load (std::memory_order_acquire)
           && (int) (juce::Time::getMillisecondCounter() - startedAt) < timeoutMs)
    {
        if (safe == nullptr)
            return false;

        captured = copyCapture();

        if (captured.contains (token))
        {
            sawToken = true;
            break;
        }

        juce::Thread::sleep (40);
    }

    if (cancelled.load (std::memory_order_acquire) && safe != nullptr)
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
                safe->sendInterrupt();
        });
    }

    juce::WaitableEvent stopped;
    juce::MessageManager::callAsync ([safe, &stopped]
    {
        if (safe != nullptr)
            safe->endCapture();
        stopped.signal();
    });
    stopped.wait (500);

    captured = stripTerminalNoise (copyCapture());

    auto exitCode = -1;
    const auto tokenIndex = captured.indexOf (token);

    if (tokenIndex >= 0)
    {
        auto after = captured.substring (tokenIndex + token.length());

        if (after.startsWithChar (':'))
            exitCode = after.substring (1).upToFirstOccurrenceOf ("\n", false, false).getIntValue();

        captured = captured.substring (0, tokenIndex).trimEnd();
        sawToken = true;
    }

    juce::String resultText;
    resultText << "exit_code: " << (sawToken ? juce::String (exitCode) : juce::String ("timeout")) << "\n";

    if (captured.isNotEmpty())
        resultText << captured;
    else
        resultText << "(no output)";

    output = resultText;
    return sawToken && exitCode == 0;
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

    if (scrollBar.isVisible())
        bounds.removeFromRight (scrollBar.getWidth());

    return bounds;
}

void TerminalView::updateFontMetrics()
{
    cellWidth  = juce::jmax (1.0f, juce::GlyphArrangement::getStringWidth (font, "M"));
    cellHeight = juce::jmax (1, (int) std::ceil (font.getHeight()));
}

void TerminalView::changeFontSize (int steps)
{
    const auto oldHeight = font.getHeight();
    const auto newHeight = getAdjustedTerminalFontHeight (oldHeight, steps);

    if (std::abs (newHeight - oldHeight) < 0.01f)
        return;

    font = font.withHeight (newHeight);
    updateFontMetrics();
    clearSelection();
    updateGridSizeFromBounds();
    repaint();
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
    if (scrollBar.isVisible())
    {
        const int width = juce::jmax (10, getLookAndFeel().getDefaultScrollbarWidth());
        scrollBar.setBounds (getLocalBounds().removeFromRight (width));
    }
    else
    {
        scrollBar.setBounds ({});
    }

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

   #if JUCE_IOS
    if (terminalSelectionActive)
    {
        juce::Rectangle<int> startHandle, endHandle;

        if (getSelectionHandleBounds (startHandle, endHandle))
        {
            g.setColour (selectionBackground.withAlpha (1.0f));
            g.fillEllipse (startHandle.toFloat());
            g.fillEllipse (endHandle.toFloat());
        }
    }
   #endif

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

juce::String TerminalView::getRowCharacters (int logicalRow) const
{
    juce::String row;

    for (int column = 0; column < numColumns; ++column)
    {
        VTermScreenCell cell {};

        if (! getCell (logicalRow, column, cell) || cell.chars[0] == 0 || cell.chars[0] == (uint32_t) -1)
        {
            row += " ";
            continue;
        }

        const auto character = cell.chars[0];
        row += character < 128 ? juce::String::charToString ((juce::juce_wchar) character)
                               : juce::String (" ");
    }

    return row;
}

void TerminalView::selectWordAt (juce::Point<int> point)
{
    const auto cell = getSelectionPoint (point);
    const auto row = getRowCharacters (cell.row);
    const auto range = getTerminalWordColumnRange (row.toRawUTF8(), cell.column);

    if (range.second <= range.first)
        return;

    selectionAnchor = { cell.row, range.first };
    selectionEnd = { cell.row, range.second - 1 };
    terminalSelectionActive = true;
    repaint();
}

void TerminalView::selectAllTerminalText()
{
    const int lastRow = juce::jmax (0, (int) scrollback.size() + numRows - 1);
    selectionAnchor = { 0, 0 };
    selectionEnd = { lastRow, juce::jmax (0, numColumns - 1) };
    terminalSelectionActive = true;
    repaint();
}

void TerminalView::showSelectionMenu()
{
    juce::PopupMenu menu;
    const auto hasSelection = getSelectedText().isNotEmpty();

    menu.addItem ("Copy", hasSelection, false, [this] { copySelectionToClipboard(); });
    menu.addItem ("Select All", true, false, [this]
    {
        selectAllTerminalText();
        showSelectionMenu();
    });
    menu.addItem ("Paste", shellRunning.load(), false, [this] { pasteFromClipboard(); });

    juce::Rectangle<int> startHandle, endHandle;
    auto target = getScreenBounds();

    if (getSelectionHandleBounds (startHandle, endHandle))
        target = localAreaToGlobal (startHandle.getUnion (endHandle));

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (this)
                            .withTargetScreenArea (target));
}

bool TerminalView::getVisibleCellBounds (TerminalSelectionPoint point, juce::Rectangle<int>& bounds) const
{
    const int visibleRow = point.row - ((int) scrollback.size() - scrollOffset);

    if (visibleRow < 0 || visibleRow >= numRows
         || point.column < 0 || point.column >= numColumns)
        return false;

    bounds = getCellBounds (visibleRow, point.column);
    return true;
}

bool TerminalView::getSelectionHandleBounds (juce::Rectangle<int>& startHandle,
                                             juce::Rectangle<int>& endHandle) const
{
    if (! terminalSelectionActive)
        return false;

    auto first = selectionAnchor;
    auto last = selectionEnd;

    if (last < first)
        std::swap (first, last);

    juce::Rectangle<int> firstCell, lastCell;

    if (! getVisibleCellBounds (first, firstCell) || ! getVisibleCellBounds (last, lastCell))
        return false;

    const auto toRect = [] (TerminalRect r)
    {
        return juce::Rectangle<int> { r.x, r.y, r.width, r.height };
    };

    startHandle = toRect (getTerminalStartHandleBounds ({ firstCell.getX(), firstCell.getY(),
                                                          firstCell.getWidth(), firstCell.getHeight() }));
    endHandle = toRect (getTerminalEndHandleBounds ({ lastCell.getX(), lastCell.getY(),
                                                      lastCell.getWidth(), lastCell.getHeight() }));
    return true;
}

TerminalSelectionHandle TerminalView::hitSelectionHandle (juce::Point<int> point) const
{
    juce::Rectangle<int> startHandle, endHandle;

    if (! getSelectionHandleBounds (startHandle, endHandle))
        return TerminalSelectionHandle::none;

    const auto toRect = [] (juce::Rectangle<int> r) -> TerminalRect
    {
        return { r.getX(), r.getY(), r.getWidth(), r.getHeight() };
    };

    return hitTerminalSelectionHandle (toRect (startHandle), toRect (endHandle), point.x, point.y);
}

bool TerminalView::isPointInsideSelection (juce::Point<int> point) const
{
    if (! terminalSelectionActive)
        return false;

    const auto cell = getSelectionPoint (point);
    const auto columns = getTerminalSelectedColumns (selectionAnchor, selectionEnd, cell.row, numColumns);
    return cell.column >= columns.first && cell.column < columns.second;
}

void TerminalView::beginTouchScroll (const juce::MouseEvent& event)
{
    if (inertialScroller == nullptr)
        return;

    inertialScroller->position.setLimits ({ 0.0, (double) scrollback.size() });
    inertialScroller->position.setPosition ((double) scrollOffset);
    inertialScroller->position.beginDrag();
    juce::ignoreUnused (event);
    touchDrag = TouchDragKind::scroll;
}

void TerminalView::dragTouchScroll (const juce::MouseEvent& event)
{
    if (inertialScroller == nullptr || cellHeight <= 0)
        return;

    const auto deltaRows = (event.position.y - touchScrollStartY) / (float) cellHeight;
    inertialScroller->position.drag ((double) deltaRows);
}

void TerminalView::endTouchScroll()
{
    if (inertialScroller != nullptr)
        inertialScroller->position.endDrag();
}

void TerminalView::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

   #if JUCE_IOS
    if (event.source.isTouch())
    {
        touchLongPressSelected = false;

        if (event.mods.isPopupMenu())
        {
            selectWordAt (event.position.toInt());
            touchLongPressSelected = true;
            touchDrag = TouchDragKind::extendSelection;
            return;
        }

        const auto handle = hitSelectionHandle (event.position.toInt());

        if (handle == TerminalSelectionHandle::start)
        {
            touchDrag = TouchDragKind::startHandle;
            return;
        }

        if (handle == TerminalSelectionHandle::end)
        {
            touchDrag = TouchDragKind::endHandle;
            return;
        }

        if (inertialScroller != nullptr)
            inertialScroller->position.setPosition ((double) scrollOffset);

        touchDrag = TouchDragKind::pending;
        touchScrollStartY = event.position.y;
        return;
    }
   #endif

    if (event.mods.isPopupMenu())
        return;

    clearSelection();
    selectionAnchor = getSelectionPoint (event.position.toInt());
    selectionEnd = selectionAnchor;
}

void TerminalView::mouseDrag (const juce::MouseEvent& event)
{
   #if JUCE_IOS
    if (event.source.isTouch())
    {
        if (touchDrag == TouchDragKind::pending
             && event.source.hasMouseMovedSignificantlySincePressed())
            beginTouchScroll (event);

        if (touchDrag == TouchDragKind::scroll)
        {
            dragTouchScroll (event);
            return;
        }

        if (touchDrag == TouchDragKind::startHandle || touchDrag == TouchDragKind::endHandle)
        {
            const auto handle = touchDrag == TouchDragKind::startHandle
                                    ? TerminalSelectionHandle::start
                                    : TerminalSelectionHandle::end;
            const auto adjusted = adjustPointForTerminalHandleDrag (event.position.x,
                                                                    event.position.y,
                                                                    handle);
            applyTerminalHandleDrag (selectionAnchor, selectionEnd, handle,
                                     getSelectionPoint ({ adjusted.first, adjusted.second }));
            terminalSelectionActive = true;
            repaint();
            return;
        }

        if (touchDrag == TouchDragKind::extendSelection)
        {
            selectionEnd = getSelectionPoint (event.position.toInt());
            terminalSelectionActive = true;
            repaint();
            return;
        }

        return;
    }
   #endif

    selectionEnd = getSelectionPoint (event.position.toInt());
    terminalSelectionActive = true;
    repaint();
}

void TerminalView::mouseUp (const juce::MouseEvent& event)
{
   #if JUCE_IOS
    if (event.source.isTouch())
    {
        if (touchDrag == TouchDragKind::scroll)
            endTouchScroll();

        if (touchLongPressSelected
             || touchDrag == TouchDragKind::startHandle
             || touchDrag == TouchDragKind::endHandle
             || touchDrag == TouchDragKind::extendSelection)
        {
            if (terminalSelectionActive)
                showSelectionMenu();
        }
        else if (touchDrag == TouchDragKind::pending)
        {
            if (terminalSelectionActive && isPointInsideSelection (event.position.toInt()))
                showSelectionMenu();
            else
                clearSelection();
        }

        touchDrag = TouchDragKind::none;
        return;
    }
   #endif

    if (event.mods.isPopupMenu() || ! event.mouseWasDraggedSinceMouseDown())
        return;

    selectionEnd = getSelectionPoint (event.position.toInt());
    terminalSelectionActive = true;
    repaint();
}

void TerminalView::mouseDoubleClick (const juce::MouseEvent& event)
{
    selectWordAt (event.position.toInt());

   #if JUCE_IOS
    if (event.source.isTouch())
        showSelectionMenu();
   #endif
}

void TerminalView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto next = juce::jlimit (0, (int) scrollback.size(),
                                    scrollOffset + getTerminalWheelRows (wheel.deltaY));

    if (inertialScroller != nullptr)
        inertialScroller->position.setPosition ((double) next);
    else
        setScrollOffset (next);
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
    if (text.isEmpty() && textInputBuffer.isEmpty())
    {
        char del = 0;
        const int n = ptyBytesForEmptyTextInputReplacement (0, &del, 1);

        if (n > 0)
            sendToShell (&del, n);

        return;
    }

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
    const auto keyModifiers = key.getModifiers();

   #if JUCE_MAC || JUCE_IOS
    const auto fontSizeStep = getTerminalFontSizeStep (key.getKeyCode(),
                                                       (uint32_t) key.getTextCharacter(),
                                                       keyModifiers.isCommandDown(),
                                                       keyModifiers.isCtrlDown(),
                                                       keyModifiers.isAltDown());

    if (fontSizeStep != 0)
    {
        changeFontSize (fontSizeStep);
        return true;
    }
   #endif

    if (! shellRunning)
        return false;

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
