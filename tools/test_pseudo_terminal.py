#!/usr/bin/env python3
"""Self-check for PseudoTerminal.

Compiles jucer_PseudoTerminal.cpp against minimal stubs for the two JUCE types
it touches, then drives a real shell through a real pty: send a command, read
the output back, and confirm the child exits. Run:
    python3 tools/test_pseudo_terminal.py
"""
import pathlib, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).parent.parent
TERMINAL = ROOT / "Projucer/Source/Terminal"

STUBS = r"""
#include <string>
#include <cstdio>

/* The parts of JUCE that PseudoTerminal touches, and nothing else. */
namespace juce
{
    class String
    {
    public:
        String() = default;
        String (const char* s) : text (s ? s : "") {}
        String (const std::string& s) : text (s) {}
        String& operator+= (const String& o) { text += o.text; return *this; }
        String operator+ (const String& o) const { String r (*this); r += o; return r; }
        bool isEmpty() const { return text.empty(); }
        bool isNotEmpty() const { return ! text.empty(); }
        const char* toRawUTF8() const { return text.c_str(); }
        std::string text;
    };

    inline String operator+ (const char* lhs, const String& rhs)
    {
        return String (lhs) + rhs;
    }

    class File
    {
    public:
        File() = default;
        File (const String& p) : path (p) {}
        String getFullPathName() const { return path; }
        bool isDirectory() const { return ! path.isEmpty(); }
        String path;
    };

    template <typename T> T jmax (T a, T b) { return a > b ? a : b; }
}

#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(C) \
    C (const C&) = delete; C& operator= (const C&) = delete;

#define JUCE_MAC 1
"""

DRIVER = r"""
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <string>
#include <set>
#include <vector>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstdlib>

/* Every currently-open fd number, read from /dev/fd. No access to
   PseudoTerminal's private members needed - this is how any other process
   on the system would see the descriptor too. */
static std::set<int> listOpenFds()
{
    std::set<int> fds;
    DIR* dir = opendir ("/dev/fd");

    if (dir != nullptr)
    {
        const int dirFd = dirfd (dir); // exclude the handle this call itself
                                        // holds open, or every snapshot would
                                        // see a "new" fd that is really just
                                        // our own readdir() in progress.
        while (auto* entry = readdir (dir))
        {
            char* end = nullptr;
            const long fd = strtol (entry->d_name, &end, 10);

            if (end != entry->d_name && *end == '\0' && (int) fd != dirFd)
                fds.insert ((int) fd);
        }

        closedir (dir);
    }

    return fds;
}

int main()
{
    /* I2 regression check: masterFd must be FD_CLOEXEC, so a ChildProcess
       Projucer spawns afterwards (the compiler, git, ...) doesn't inherit
       the pty master across its own fork/exec. Diff /dev/fd around start()
       to find the new descriptor without touching any private member
       (verified: with the FD_CLOEXEC fcntl() call removed, this fails). */
    {
        PseudoTerminal cloexecPty;
        const auto before = listOpenFds();

        if (! cloexecPty.start (juce::File ("/tmp"), 80, 24))
        {
            printf ("FAIL: start() failed for CLOEXEC test: %s\n", cloexecPty.getLastError().toRawUTF8());
            return 1;
        }

        const auto after = listOpenFds();
        std::vector<int> newFds;

        for (int fd : after)
            if (before.find (fd) == before.end())
                newFds.push_back (fd);

        if (newFds.empty())
        {
            printf ("FAIL: start() did not open any descriptor visible via /dev/fd\n");
            return 1;
        }

        for (int fd : newFds)
        {
            const int flags = fcntl (fd, F_GETFD);

            if (flags < 0 || (flags & FD_CLOEXEC) == 0)
            {
                printf ("FAIL: descriptor %d opened by start() is not FD_CLOEXEC\n", fd);
                return 1;
            }
        }

        cloexecPty.stop();
    }

    /* A marker set on *this* process before start(), so the only way the
       child can see it is by inheriting our environment through envp. PATH
       cannot be used for this on macOS: a login shell (`-l`, which is what
       start() always passes) re-derives PATH from /etc/paths via
       /usr/libexec/path_helper in /etc/zprofile regardless of what it
       inherited, so a PATH=set check silently passes even when the rest of
       the parent's environment was dropped - confirmed empirically by
       stripping PATH from envp and re-running this test unmodified: it
       still passed. A private variable name nothing else touches doesn't
       have that problem. */
    setenv ("PSEUDOTERM_ENV_PROBE", "present", 1);

    PseudoTerminal pty;

    if (! pty.start (juce::File ("/tmp"), 80, 24))
    {
        printf ("FAIL: start() failed: %s\n", pty.getLastError().toRawUTF8());
        return 1;
    }

    /* Ask the shell to print a distinctive string, its TERM, and the probe
       variable above, then leave. These are plain behaviour checks, not a
       C1 regression test: C1 is about *when* the environment is assembled
       (before fork(), via envp/execle(), vs. setenv() racing with other
       threads between fork() and exec()) - a code-shape property that is
       not black-box observable, because a setenv() done right would produce
       the same TERM value seen here. C1 is verified by reading the child
       branch (see the review), not by this test. What *is* genuinely
       checked here: the probe fails if envp were ever rebuilt with only
       TERM in it, which is the realistic way this rewrite would actually
       break (verified: stripping everything but TERM from envp makes this
       assertion fail). */
    const char* cmd = "printf 'PTYOK TERM=%s PROBE=%s\\n' \"$TERM\" \"$PSEUDOTERM_ENV_PROBE\"; exit 0\n";
    pty.writeBytes (cmd, (int) strlen (cmd));

    /* Drain until we see our marker or the child goes away. Reading must never
       block, so an empty read is normal and we simply try again. */
    std::string collected;
    for (int attempt = 0; attempt < 2000; ++attempt)
    {
        char buffer[1024];
        const int numRead = pty.readBytes (buffer, sizeof (buffer));

        if (numRead > 0)          collected.append (buffer, (size_t) numRead);
        else if (numRead < 0)     break;
        else                      usleep (5000);

        if (collected.find ("PTYOK") != std::string::npos && ! pty.isRunning())
            break;
    }

    if (collected.find ("PTYOK") == std::string::npos)
    {
        printf ("FAIL: never saw the marker. Got: [%s]\n", collected.c_str());
        return 1;
    }

    if (collected.find ("TERM=xterm-256color") == std::string::npos)
    {
        printf ("FAIL: child TERM was not xterm-256color. Got: [%s]\n", collected.c_str());
        return 1;
    }

    if (collected.find ("PROBE=present") == std::string::npos)
    {
        printf ("FAIL: child did not inherit the parent's environment. Got: [%s]\n", collected.c_str());
        return 1;
    }

    /* Resizing a live pty must not throw or corrupt the descriptor. */
    pty.setSize (100, 40);

    pty.stop();
    if (pty.isRunning())
    {
        printf ("FAIL: still running after stop()\n");
        return 1;
    }

    /* I4 regression check: a foreground grandchild (like vim or top under the
       shell) must not be orphaned when stop() hangs up the shell - it has to
       go down with the pty too. The grandchild traps SIGHUP (`trap "" HUP`)
       so it cannot be taken out by SIGHUP alone, and runs `sleep`, not
       something that reads stdin, so it cannot be taken out by the pty EOF
       either - this makes the test actually exercise stop()'s process-group
       SIGKILL escalation instead of passing for the wrong reason (verified:
       with the I4 fix removed entirely, this block reports the grandchild
       survived). Spawn a shell that forks a second shell into this state,
       record its pid, then confirm stop() takes it out along with the login
       shell. */
    {
        PseudoTerminal gcPty;

        if (! gcPty.start (juce::File ("/tmp"), 80, 24))
        {
            printf ("FAIL: start() failed for grandchild test: %s\n", gcPty.getLastError().toRawUTF8());
            return 1;
        }

        char pidFileTemplate[] = "/tmp/pty_gc_pid_XXXXXX";
        const int pidFileFd = mkstemp (pidFileTemplate);
        assert (pidFileFd >= 0);
        close (pidFileFd);

        char gcCmd[256];
        snprintf (gcCmd, sizeof (gcCmd), "sh -c 'trap \"\" HUP; echo $$ > %s; exec sleep 300'\n", pidFileTemplate);
        gcPty.writeBytes (gcCmd, (int) strlen (gcCmd));

        long grandchildPid = 0;

        for (int attempt = 0; attempt < 200 && grandchildPid == 0; ++attempt)
        {
            char buffer[256];
            gcPty.readBytes (buffer, sizeof (buffer)); // keep the pty drained

            FILE* f = fopen (pidFileTemplate, "r");
            if (f != nullptr)
            {
                if (fscanf (f, "%ld", &grandchildPid) != 1)
                    grandchildPid = 0;
                fclose (f);
            }

            if (grandchildPid == 0)
                usleep (5000);
        }

        unlink (pidFileTemplate);

        if (grandchildPid == 0)
        {
            printf ("FAIL: grandchild never reported its pid\n");
            return 1;
        }

        if (kill ((pid_t) grandchildPid, 0) != 0)
        {
            printf ("FAIL: grandchild pid %ld was not alive before stop()\n", grandchildPid);
            return 1;
        }

        gcPty.stop();

        bool grandchildGone = false;
        for (int attempt = 0; attempt < 200 && ! grandchildGone; ++attempt)
        {
            if (kill ((pid_t) grandchildPid, 0) != 0 && errno == ESRCH)
                grandchildGone = true;
            else
                usleep (5000);
        }

        if (! grandchildGone)
        {
            printf ("FAIL: grandchild pid %ld survived stop() - orphaned\n", grandchildPid);
            return 1;
        }
    }

    /* A bad working directory is a user-visible failure, not a crash. */
    PseudoTerminal broken;
    broken.start (juce::File ("/definitely/not/a/real/directory"), 80, 24);
    broken.stop();

    printf ("all pseudo terminal checks passed\n");
    return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        source = (TERMINAL / "jucer_PseudoTerminal.cpp").read_text()
        header = (TERMINAL / "jucer_PseudoTerminal.h").read_text()

        # Splice the real header and implementation between our stubs and the
        # driver, so the code under test is compiled exactly as it ships.
        header = header.replace('#pragma once', '')
        source = source.replace('#include "../Application/jucer_Headers.h"', '')
        source = source.replace('#include "jucer_PseudoTerminal.h"', '')

        combined = tmp / "combined.cpp"
        combined.write_text(STUBS + header + source + DRIVER)

        binary = tmp / "driver"
        result = subprocess.run(
            ["c++", "-std=c++17", "-o", str(binary), str(combined)],
            capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit("compile failed:\n" + result.stdout + result.stderr)

        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        print(result.stdout, end="")
        if result.returncode != 0:
            sys.exit("pseudo terminal check failed:\n" + result.stderr)


if __name__ == "__main__":
    main()
