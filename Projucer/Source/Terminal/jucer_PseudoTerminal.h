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

    @section threading

    This class is NOT internally synchronised. Exactly one split is supported,
    which is the one TerminalView uses:

    - readBytes() may be called from one single background thread.
    - every other member - start(), stop(), writeBytes(), setSize(),
      isRunning(), getExitCode() - belongs to the owning thread.

    The split is not free, because readBytes() reaps the child: it calls
    waitpid() and writes exitCode and childHasExited, which isRunning() and
    getExitCode() read. Those two fields are plain, not atomic, so the owner
    must not read them while the reading thread can still run. Two rules keep
    that true, and both are load-bearing:

    1. Join the reading thread before calling stop(), and never call
       isRunning() or getExitCode() while it is alive.
    2. Learn that the child is gone from readBytes() returning -1, not by
       polling isRunning() - then publish that fact to the owning thread
       through your own atomic. The release/acquire pair that publishing
       forms is what makes the reap's writes visible, so getExitCode() is
       only valid on the far side of it.

    TerminalView::Reader does both. If you need a second concurrent caller,
    or an owner that polls isRunning(), make exitCode and childHasExited
    atomic first - do not assume the current arrangement generalises.
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
