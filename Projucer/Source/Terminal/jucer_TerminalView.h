#pragma once

#include "jucer_PseudoTerminal.h"
#include "jucer_TerminalTranslation.h"

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

//==============================================================================
/**
    One terminal: a shell, an emulator, and the grid of cells they produce.

    Bytes arrive from the shell on a background thread, because a read that
    finds nothing should not spin the message thread. They are handed over
    through a lock-free fifo and interpreted on the message thread, where
    libvterm's damage callbacks tell us which rows actually changed so that a
    busy program like top repaints two lines rather than the whole window.
*/
class TerminalView final : public juce::Component,
                           public juce::TextInputTarget,
                           public juce::ApplicationCommandTarget,
                           private juce::ScrollBar::Listener,
                           private juce::Timer,
                           private juce::AsyncUpdater
{
public:
    explicit TerminalView (const juce::File& workingDirectory);
    ~TerminalView() override;

    /** Types a command into the shell as if the user had pressed Return. */
    void sendCommandLine (const juce::String& command);
    bool isShellRunning() const noexcept;

    /** Sends the command, waits until it finishes, and returns the PTY text.
        Safe to call from a background thread. */
    bool runCommandAndWait (const juce::String& command,
                            juce::String& output,
                            int timeoutMs,
                            std::atomic<bool>& cancelled);

    void sendInterrupt();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    juce::ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    bool isTextInputActive() const override;
    juce::Range<int> getHighlightedRegion() const override;
    void setHighlightedRegion (const juce::Range<int>&) override;
    void setTemporaryUnderlining (const juce::Array<juce::Range<int>>&) override;
    juce::String getTextInRange (const juce::Range<int>&) const override;
    void insertTextAtCaret (const juce::String&) override;
    int getCaretPosition() const override;
    juce::Rectangle<int> getCaretRectangleForCharIndex (int) const override;
    int getTotalNumChars() const override;
    int getCharIndexForPoint (juce::Point<int>) const override;
    juce::RectangleList<int> getTextBounds (juce::Range<int>) const override;

    // libvterm calls these back on the message thread, from inside
    // vterm_input_write(). They are public only because the callback table
    // that names them is built at file scope in the .cpp.
    static int damageCallback (VTermRect, void* user);
    static int moveCursorCallback (VTermPos, VTermPos, int visible, void* user);
    static int pushScrollbackLine (int cols, const VTermScreenCell*, void* user);
    static void outputCallback (const char* bytes, size_t len, void* user);

private:
    class Reader;

    void timerCallback() override;
    void handleAsyncUpdate() override;

    void consumePendingBytes();
    void sendToShell (const char* bytes, int numBytes);
    void beginCapture();
    void endCapture();
    juce::String copyCapture() const;
    void updateFontMetrics();
    void changeFontSize (int steps);
    void updateGridSizeFromBounds();
    void updateScrollBar();
    void setScrollOffset (int);
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void clearSelection();
    void copySelectionToClipboard() const;
    void pasteFromClipboard();
    void selectWordAt (juce::Point<int>);
    void selectAllTerminalText();
    void showSelectionMenu();
    juce::String getSelectedText() const;
    juce::String getRowCharacters (int logicalRow) const;
    bool getCell (int logicalRow, int column, VTermScreenCell&) const;
    bool getVisibleCellBounds (TerminalSelectionPoint, juce::Rectangle<int>&) const;
    bool getSelectionHandleBounds (juce::Rectangle<int>& startHandle,
                                   juce::Rectangle<int>& endHandle) const;
    TerminalSelectionHandle hitSelectionHandle (juce::Point<int>) const;
    bool isPointInsideSelection (juce::Point<int>) const;
    TerminalSelectionPoint getSelectionPoint (juce::Point<int>) const;
    juce::Rectangle<int> getTerminalBounds() const;
    juce::Rectangle<int> getCellBounds (int row, int column, int widthInCells = 1) const;
    void beginTouchScroll (const juce::MouseEvent&);
    void dragTouchScroll (const juce::MouseEvent&);
    void endTouchScroll();

    PseudoTerminal pty;
    VTerm* vterm = nullptr;
    VTermScreen* screen = nullptr;

    std::unique_ptr<Reader> reader;
    juce::AbstractFifo incoming { 1 << 16 };
    juce::HeapBlock<char> incomingBuffer { (size_t) (1 << 16) };

    /** Owned by the reader thread, which is the only thing that can observe the
        shell going away; everything else only reads it. pty.isRunning() is not
        a substitute: the reader is the only thing that ever calls readBytes(),
        which is the only thing that reaps the child, so once the reader stops
        reading nothing reaps it any more and pty.isRunning() would keep
        reporting true forever. This flag is this class's own record of "the
        reader gave up", set at the moment that happens. */
    std::atomic<bool> shellRunning { false };
    std::atomic<bool> capturing { false };
    mutable juce::CriticalSection captureLock;
    juce::MemoryBlock captureBytes;
    static constexpr size_t maxCaptureBytes = 256 * 1024;

    juce::Font font { juce::FontOptions {} };
    float cellWidth = 8.0f;
    int cellHeight = 16;
    int numColumns = 80;
    int numRows = 24;

    int cursorRow = 0, cursorColumn = 0;
    bool cursorVisible = true;
    bool cursorBlinkOn = true;

    juce::String statusMessage;
    std::string pendingOutput;

    juce::ScrollBar scrollBar { true };

    struct ScrollbackLine
    {
        std::vector<VTermScreenCell> cells;
    };

    std::deque<ScrollbackLine> scrollback;
    int scrollOffset = 0;
    static constexpr int maxScrollbackLines = 10000;

    TerminalSelectionPoint selectionAnchor {};
    TerminalSelectionPoint selectionEnd {};
    bool terminalSelectionActive = false;

    class InertialScroller;
    std::unique_ptr<InertialScroller> inertialScroller;

    enum class TouchDragKind
    {
        none,
        pending,
        scroll,
        startHandle,
        endHandle,
        extendSelection
    };

    TouchDragKind touchDrag = TouchDragKind::none;
    float touchScrollStartY = 0.0f;
    bool touchLongPressSelected = false;

    juce::String textInputBuffer;
    juce::Range<int> textInputSelection;
    juce::Array<juce::Range<int>> temporaryUnderlines;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerminalView)
};
