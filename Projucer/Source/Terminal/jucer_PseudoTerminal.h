#pragma once

//==============================================================================
/**
    A shell running under a pseudo-terminal.

    This owns the master side of a pty and the child process attached to its
    slave side. It deals in raw bytes and knows nothing about escape sequences -
    interpreting those is libvterm's job, one layer up.

    Reads and writes never block, so the message thread can drive this directly.
    A read that returns 0 means "nothing right now", not "finished".

    Only the macOS implementation exists today; the interface is deliberately
    free of anything platform-specific so a ConPTY body can be added later
    without disturbing anything above it.
*/
class PseudoTerminal
{
public:
    PseudoTerminal();
    ~PseudoTerminal();

    /** Launches the user's login shell with the given working directory and
        terminal size. Returns false on failure, with the reason in
        getLastError(). */
    bool start (const juce::File& workingDirectory, int numColumns, int numRows);

    /** Hangs up the child and closes the pty. Safe to call more than once, and
        safe to call when start() was never called or failed. */
    void stop();

    bool isRunning() const noexcept;
    juce::String getLastError() const                 { return lastError; }

    /** Valid once isRunning() returns false. -1 if the child never ran. */
    int getExitCode() const noexcept                  { return exitCode; }

    /** Returns the number of bytes read, 0 if none are waiting, or -1 if the
        child has gone away. */
    int readBytes (char* destination, int maxBytes);

    /** Returns the number of bytes actually written, which may be fewer than
        requested. The caller must keep the remainder and retry. */
    int writeBytes (const char* source, int numBytes);

    /** Tells the child its window has changed size, which raises SIGWINCH so
        that a running vim or top redraws itself. */
    void setSize (int numColumns, int numRows);

private:
    void reapChildIfNeeded();

    int masterFd = -1;
    int childPid = -1;
    int exitCode = -1;
    bool childHasExited = true;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PseudoTerminal)
};
