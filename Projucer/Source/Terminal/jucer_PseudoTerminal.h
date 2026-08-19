#pragma once

#include <atomic>

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

    This class is mostly NOT internally synchronised. Exactly one split is
    supported, which is the one TerminalView uses:

    - readBytes() may be called from one single background thread.
    - every other member - start(), stop(), writeBytes(), setSize() - belongs
      to the owning thread, and start()/stop() additionally require that the
      reading thread be joined first: start() creates it, stop() must not run
      while it is still reading.

    isRunning() and getExitCode() are the exception: readBytes() reaps the
    child on the background thread (it calls waitpid() and writes exitCode
    and childHasExited), and those two fields are atomic specifically so that
    isRunning() and getExitCode() can be called from the owning thread at any
    time, including while the reading thread is still alive. childHasExited
    is published with release/acquire, which is what makes exitCode's
    (relaxed) write visible once isRunning() has been observed to return
    false.

    One thing atomicity does not buy: once the reading thread stops calling
    readBytes() - which is the only thing that ever reaps the child - nothing
    reaps it any more, so isRunning() will keep reporting true forever after
    that point even though the child is long gone. A caller that needs to
    know "the shell is gone" after the reading thread has stopped reading
    must learn that some other way (TerminalView does, via its own flag) -
    isRunning() alone is not enough once nothing is left to reap.
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
    int getExitCode() const noexcept                  { return exitCode.load (std::memory_order_relaxed); }

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

    // Written by reapChildIfNeeded() on the reading thread, read by
    // isRunning()/getExitCode() on the owning thread - see @section threading
    // above. exitCode only ever needs relaxed order because childHasExited's
    // release store/acquire load already establishes happens-before between
    // the write and any read that saw childHasExited become true.
    std::atomic<int> exitCode { -1 };
    std::atomic<bool> childHasExited { true };

    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PseudoTerminal)
};
