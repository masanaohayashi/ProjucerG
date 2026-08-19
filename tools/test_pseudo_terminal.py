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
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>

int main()
{
    PseudoTerminal pty;

    if (! pty.start (juce::File ("/tmp"), 80, 24))
    {
        printf ("FAIL: start() failed: %s\n", pty.getLastError().toRawUTF8());
        return 1;
    }

    /* Ask the shell to print a distinctive string, its TERM (must be the
       xterm-256color the child sets via envp, not inherited-then-overwritten
       with setenv - regression check for C1) and PATH (must be non-empty,
       proving the child got the rest of the parent's environment too, not
       just a one-variable envp), then leave. */
    const char* cmd = "printf 'PTYOK TERM=%s PATH=%s\\n' \"$TERM\" \"${PATH:+set}\"; exit 0\n";
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

    if (collected.find ("PATH=set") == std::string::npos)
    {
        printf ("FAIL: child did not inherit the parent's PATH. Got: [%s]\n", collected.c_str());
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
       go down with the pty too. Spawn a shell that forks a second shell,
       which execs into `cat` (a distinct, easily-identified process), record
       its pid, then confirm stop() takes it out along with the login shell. */
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
        snprintf (gcCmd, sizeof (gcCmd), "sh -c 'echo $$ > %s; exec cat'\n", pidFileTemplate);
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
