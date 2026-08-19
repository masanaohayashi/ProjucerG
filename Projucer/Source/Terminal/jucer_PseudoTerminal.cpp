#include "../Application/jucer_Headers.h"
#include "jucer_PseudoTerminal.h"

#if JUCE_MAC

#include <util.h>          // forkpty
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

PseudoTerminal::PseudoTerminal() = default;

PseudoTerminal::~PseudoTerminal()
{
    stop();
}

bool PseudoTerminal::start (const juce::File& workingDirectory, int numColumns, int numRows)
{
    stop();

    struct winsize ws = {};
    ws.ws_col = (unsigned short) juce::jmax (1, numColumns);
    ws.ws_row = (unsigned short) juce::jmax (1, numRows);

    // Everything the child needs must be built here, before the fork. Between
    // fork() and exec() only async-signal-safe calls are legal, and this is a
    // multi-threaded app - allocating there would risk deadlocking on a malloc
    // lock held by another thread at the moment we forked.
    const juce::String shellPath = [&]
    {
        if (auto* fromEnv = getenv ("SHELL"))
            if (strlen (fromEnv) > 0)
                return juce::String (fromEnv);

        return juce::String ("/bin/zsh");
    }();

    const std::string shell (shellPath.toRawUTF8());
    const std::string directory (workingDirectory.isDirectory()
                                     ? workingDirectory.getFullPathName().toRawUTF8()
                                     : "/");

    const pid_t pid = forkpty (&masterFd, nullptr, nullptr, &ws);

    if (pid < 0)
    {
        lastError = juce::String ("Could not open a pseudo-terminal: ") + juce::String (strerror (errno));
        masterFd = -1;
        return false;
    }

    if (pid == 0)
    {
        // Child. Only async-signal-safe calls from here to execl().
        if (chdir (directory.c_str()) != 0)
            _exit (127);

        setenv ("TERM", "xterm-256color", 1);

        execl (shell.c_str(), shell.c_str(), "-l", (char*) nullptr);
        _exit (127);
    }

    childPid = (int) pid;
    childHasExited = false;
    exitCode = -1;
    lastError = {};

    // Non-blocking, so that a read on the message thread returns immediately
    // when the child has nothing to say.
    fcntl (masterFd, F_SETFL, fcntl (masterFd, F_GETFL, 0) | O_NONBLOCK);

    return true;
}

void PseudoTerminal::stop()
{
    if (childPid > 0 && ! childHasExited)
    {
        kill ((pid_t) childPid, SIGHUP);

        // Give it a moment to leave on its own before insisting.
        for (int i = 0; i < 100 && ! childHasExited; ++i)
        {
            reapChildIfNeeded();

            if (! childHasExited)
                usleep (5000);
        }

        if (! childHasExited)
        {
            kill ((pid_t) childPid, SIGKILL);
            int status = 0;
            waitpid ((pid_t) childPid, &status, 0);
            childHasExited = true;
        }
    }

    if (masterFd >= 0)
    {
        close (masterFd);
        masterFd = -1;
    }

    childPid = -1;
}

void PseudoTerminal::reapChildIfNeeded()
{
    if (childPid <= 0 || childHasExited)
        return;

    int status = 0;
    const pid_t result = waitpid ((pid_t) childPid, &status, WNOHANG);

    if (result == (pid_t) childPid)
    {
        childHasExited = true;
        exitCode = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
    }
    else if (result < 0 && errno == ECHILD)
    {
        childHasExited = true;
    }
}

bool PseudoTerminal::isRunning() const noexcept
{
    return childPid > 0 && ! childHasExited;
}

int PseudoTerminal::readBytes (char* destination, int maxBytes)
{
    reapChildIfNeeded();

    if (masterFd < 0)
        return -1;

    const auto numRead = ::read (masterFd, destination, (size_t) maxBytes);

    if (numRead > 0)
        return (int) numRead;

    if (numRead == 0)
        return -1;                                  // the slave side closed

    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return 0;                                   // simply nothing waiting

    return -1;                                      // EIO: the child is gone
}

int PseudoTerminal::writeBytes (const char* source, int numBytes)
{
    if (masterFd < 0 || numBytes <= 0)
        return 0;

    const auto numWritten = ::write (masterFd, source, (size_t) numBytes);

    if (numWritten > 0)
        return (int) numWritten;

    return 0;                                       // full buffer: retry later
}

void PseudoTerminal::setSize (int numColumns, int numRows)
{
    if (masterFd < 0)
        return;

    struct winsize ws = {};
    ws.ws_col = (unsigned short) juce::jmax (1, numColumns);
    ws.ws_row = (unsigned short) juce::jmax (1, numRows);

    ioctl (masterFd, TIOCSWINSZ, &ws);
}

#else

// Windows and Linux bodies go here. Until then the panel is never created on
// those platforms, so these do nothing rather than pretending to work.

PseudoTerminal::PseudoTerminal() = default;
PseudoTerminal::~PseudoTerminal() = default;

bool PseudoTerminal::start (const juce::File&, int, int)
{
    lastError = "The integrated terminal is only available on macOS.";
    return false;
}

void PseudoTerminal::stop()                             {}
bool PseudoTerminal::isRunning() const noexcept         { return false; }
int  PseudoTerminal::readBytes (char*, int)             { return -1; }
int  PseudoTerminal::writeBytes (const char*, int)      { return 0; }
void PseudoTerminal::setSize (int, int)                 {}

#endif
