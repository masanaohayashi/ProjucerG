#pragma once

#include "jucer_PseudoTerminal.h"
#include "jucer_TerminalTranslation.h"

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
                           private juce::Timer,
                           private juce::AsyncUpdater
{
public:
    explicit TerminalView (const juce::File& workingDirectory);
    ~TerminalView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

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
    void updateGridSizeFromBounds();
    juce::Rectangle<int> getCellBounds (int row, int column) const;

    PseudoTerminal pty;
    VTerm* vterm = nullptr;
    VTermScreen* screen = nullptr;

    std::unique_ptr<Reader> reader;
    juce::AbstractFifo incoming { 1 << 16 };
    juce::HeapBlock<char> incomingBuffer { (size_t) (1 << 16) };

    /** Owned by the reader thread, which is the only thing that can observe the
        shell going away; everything else only reads it. */
    std::atomic<bool> shellRunning { false };

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerminalView)
};
