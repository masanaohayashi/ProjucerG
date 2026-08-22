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
#include <vector>
#include <string>

extern char** environ;

class PseudoTerminal::InProcessPtyHost {};

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
    // lock held by another thread at the moment we forked. That includes the
    // environment: setenv() after fork() is not async-signal-safe (it can
    // allocate), so the child's whole environment - the parent's, with TERM
    // overridden - is assembled here as a plain envp array and handed to
    // execle() instead.
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

    std::vector<std::string> envStorage;

    for (char** e = environ; e != nullptr && *e != nullptr; ++e)
        if (strncmp (*e, "TERM=", 5) != 0)
            envStorage.emplace_back (*e);

    envStorage.emplace_back ("TERM=xterm-256color");

    std::vector<char*> envp;
    for (auto& entry : envStorage)
        envp.push_back (entry.data());
    envp.push_back (nullptr);

    const pid_t pid = forkpty (&masterFd, nullptr, nullptr, &ws);

    if (pid < 0)
    {
        lastError = juce::String ("Could not open a pseudo-terminal: ") + juce::String (strerror (errno));
        masterFd = -1;
        return false;
    }

    if (pid == 0)
    {
        // Child. Only async-signal-safe calls from here to execle() - no
        // allocation, no setenv(), nothing that could touch a malloc lock
        // another thread held at the moment of the fork.
        if (chdir (directory.c_str()) != 0)
            _exit (127);

        execle (shell.c_str(), shell.c_str(), "-l", (char*) nullptr, envp.data());
        _exit (127);
    }

    childPid = (int) pid;
    childHasExited.store (false, std::memory_order_relaxed);
    exitCode.store (-1, std::memory_order_relaxed);
    lastError = {};

    // Non-blocking, so that a read on the message thread returns immediately
    // when the child has nothing to say.
    fcntl (masterFd, F_SETFL, fcntl (masterFd, F_GETFL, 0) | O_NONBLOCK);

    // Every ChildProcess Projucer spawns (the compiler, git, etc.) would
    // otherwise inherit the pty master across its own fork/exec and keep it
    // open, defeating EOF/hangup detection on our side.
    fcntl (masterFd, F_SETFD, FD_CLOEXEC);

    return true;
}

void PseudoTerminal::stop()
{
    if (childPid > 0 && ! childHasExited.load (std::memory_order_relaxed))
    {
        // Ask the kernel which process group currently owns the terminal
        // before touching anything else - once the master is closed there's
        // no way to ask. With job control on (any interactive shell), the
        // shell puts each foreground job in its own process group, so a
        // running vim or top is *not* in childPid's group: killpg(childPid)
        // alone never reaches it, and it survives as an orphan under init.
        const pid_t foregroundPgid = (masterFd >= 0) ? tcgetpgrp (masterFd) : (pid_t) -1;

        // Hang up first, civilly: SIGHUP is what a real terminal sends when
        // the line drops, and it's what lets a shell write its history, run
        // zshexit, or let an editor save its swap file before it goes.
        // Signal both the shell's own group and whatever job currently owns
        // the foreground, since they can differ (see above).
        //
        // Signal the child by pid as well as by group. forkpty() returns to
        // the parent the moment fork() completes, but the child's own
        // setsid() - inside the login_tty() that forkpty() runs on the child
        // side - has not necessarily happened yet, so its pgid is not yet
        // childPid and killpg() simply returns ESRCH. That is not a rare
        // race: measured over 300 runs, a stop() issued straight after
        // start() lost the group signal every single time. kill() on the pid
        // is valid whatever setsid() has or has not done, so the hangup lands
        // either way, and killpg() below still covers the child's whole group
        // once it has one.
        kill   ((pid_t) childPid, SIGHUP);
        killpg ((pid_t) childPid, SIGHUP);
        if (foregroundPgid > 0 && foregroundPgid != (pid_t) childPid)
            killpg (foregroundPgid, SIGHUP);

        if (masterFd >= 0)
        {
            close (masterFd);
            masterFd = -1;
        }

        // A short, bounded grace period - not the ~500ms poll this replaced,
        // just enough ticks for the signal handlers above to actually run
        // before we stop waiting for the shell. This may run on the message
        // thread, so the budget is small and fixed, never open-ended.
        for (int i = 0; i < 10 && ! childHasExited.load (std::memory_order_relaxed); ++i)
        {
            reapChildIfNeeded();

            if (! childHasExited.load (std::memory_order_relaxed))
                usleep (5000);
        }

        // Insist. A foreground job that trapped or ignored SIGHUP (top,
        // "trap '' HUP") can still be alive here even though the shell
        // above it already exited and was reaped - at that point it's no
        // longer our child, so we can't check it directly, but killing an
        // already-dead process group is a harmless no-op, so this always
        // runs rather than being skipped when the shell is already gone.
        if (foregroundPgid > 0 && foregroundPgid != (pid_t) childPid)
            killpg (foregroundPgid, SIGKILL);

        if (! childHasExited.load (std::memory_order_relaxed))
        {
            kill   ((pid_t) childPid, SIGKILL);
            killpg ((pid_t) childPid, SIGKILL);

            // Bounded, exactly like the grace loop above and for the same
            // reason: stop() runs on the message thread, so a blocking
            // waitpid() here would freeze Projucer's UI outright the moment
            // anything kept the signal from landing - which the pgid race
            // described above did, reliably, until the kill() beside this
            // killpg() was added. ~100ms is far more than an already-SIGKILLed
            // child needs; if it somehow expires the child is left unreaped
            // until Projucer exits, which is a stray zombie, not a hang.
            for (int i = 0; i < 20 && ! childHasExited.load (std::memory_order_relaxed); ++i)
            {
                reapChildIfNeeded();

                if (! childHasExited.load (std::memory_order_relaxed))
                    usleep (5000);
            }
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
    if (childPid <= 0 || childHasExited.load (std::memory_order_relaxed))
        return;

    int status = 0;
    const pid_t result = waitpid ((pid_t) childPid, &status, WNOHANG);

    if (result == (pid_t) childPid)
    {
        // Publish the exit code before the flag that announces it, so that
        // any thread which observes childHasExited become true (via the
        // acquire load in isRunning()) is guaranteed to see this write too.
        exitCode.store (WIFEXITED (status) ? WEXITSTATUS (status) : -1, std::memory_order_relaxed);
        childHasExited.store (true, std::memory_order_release);
    }
    else if (result < 0 && errno == ECHILD)
    {
        childHasExited.store (true, std::memory_order_release);
    }
}

bool PseudoTerminal::isRunning() const noexcept
{
    return childPid > 0 && ! childHasExited.load (std::memory_order_acquire);
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

int PseudoTerminal::getExitCode() const noexcept
{
    return exitCode.load (std::memory_order_relaxed);
}

#elif JUCE_IOS

#include "../Shell/jucer_InProcessTerminal.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <string>

class PseudoTerminal::InProcessPtyHost
{
public:
    InProcessTerminal session;
    std::mutex mutex;
    std::condition_variable cv;
    std::string incoming;
    std::atomic<bool> stopWorker { false };
    std::thread worker;

    void run()
    {
        while (! stopWorker.load (std::memory_order_acquire))
        {
            std::string chunk;

            {
                std::unique_lock<std::mutex> lock (mutex);
                cv.wait (lock, [this]
                {
                    return stopWorker.load (std::memory_order_acquire) || ! incoming.empty();
                });

                if (stopWorker.load (std::memory_order_acquire) && incoming.empty())
                    break;

                chunk.swap (incoming);
            }

            if (! chunk.empty())
                session.feed (chunk.data(), (int) chunk.size());
        }
    }
};

PseudoTerminal::PseudoTerminal() = default;

PseudoTerminal::~PseudoTerminal()
{
    stop();
}

bool PseudoTerminal::start (const juce::File& workingDirectory, int, int)
{
    stop();

    inProcessPty = std::make_unique<InProcessPtyHost>();
    const auto root = workingDirectory.isDirectory() ? workingDirectory
                                                     : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    inProcessPty->session.start (root, root);
    inProcessPty->worker = std::thread ([host = inProcessPty.get()] { host->run(); });

    childHasExited.store (false, std::memory_order_relaxed);
    exitCode.store (-1, std::memory_order_relaxed);
    lastError = {};
    return true;
}

void PseudoTerminal::stop()
{
    if (inProcessPty == nullptr)
        return;

    inProcessPty->session.requestCancel();
    inProcessPty->stopWorker.store (true, std::memory_order_release);
    inProcessPty->cv.notify_one();

    if (inProcessPty->worker.joinable())
        inProcessPty->worker.join();

    inProcessPty->session.stop();
    exitCode.store (inProcessPty->session.getExitCode(), std::memory_order_relaxed);
    childHasExited.store (true, std::memory_order_release);
    inProcessPty.reset();
}

void PseudoTerminal::reapChildIfNeeded() {}

bool PseudoTerminal::isRunning() const noexcept
{
    return inProcessPty != nullptr && inProcessPty->session.isRunning();
}

int PseudoTerminal::getExitCode() const noexcept
{
    if (inProcessPty != nullptr)
        return inProcessPty->session.getExitCode();

    return exitCode.load (std::memory_order_relaxed);
}

int PseudoTerminal::readBytes (char* destination, int maxBytes)
{
    if (inProcessPty == nullptr)
        return -1;

    const int n = inProcessPty->session.read (destination, maxBytes);

    if (n < 0)
    {
        exitCode.store (inProcessPty->session.getExitCode(), std::memory_order_relaxed);
        childHasExited.store (true, std::memory_order_release);
    }

    return n;
}

int PseudoTerminal::writeBytes (const char* source, int numBytes)
{
    if (inProcessPty == nullptr || source == nullptr || numBytes <= 0)
        return 0;

    std::string chunk;

    if (inProcessPty->session.isExecuting())
    {
        chunk.reserve ((size_t) numBytes);

        for (int i = 0; i < numBytes; ++i)
        {
            if (source[i] == 3)
            {
                inProcessPty->session.requestCancel();
                continue;
            }

            chunk.push_back (source[i]);
        }
    }
    else
    {
        chunk.assign (source, (size_t) numBytes);
    }

    if (! chunk.empty())
    {
        const std::lock_guard<std::mutex> lock (inProcessPty->mutex);
        inProcessPty->incoming.append (chunk);
        inProcessPty->cv.notify_one();
    }
    else
    {
        inProcessPty->cv.notify_one();
    }

    return numBytes;
}

void PseudoTerminal::setSize (int, int) {}

#else

// Windows and Linux bodies go here. Until then the panel is never created on
// those platforms, so these do nothing rather than pretending to work.

class PseudoTerminal::InProcessPtyHost {};

PseudoTerminal::PseudoTerminal() = default;
PseudoTerminal::~PseudoTerminal() = default;

bool PseudoTerminal::start (const juce::File&, int, int)
{
    lastError = "The integrated terminal is only available on macOS and iOS.";
    return false;
}

void PseudoTerminal::stop()                             {}
void PseudoTerminal::reapChildIfNeeded()                {}
bool PseudoTerminal::isRunning() const noexcept         { return false; }
int  PseudoTerminal::getExitCode() const noexcept       { return exitCode.load (std::memory_order_relaxed); }
int  PseudoTerminal::readBytes (char*, int)             { return -1; }
int  PseudoTerminal::writeBytes (const char*, int)      { return 0; }
void PseudoTerminal::setSize (int, int)                 {}

#endif
