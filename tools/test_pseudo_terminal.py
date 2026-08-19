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
#include <string>
#include <unistd.h>

int main()
{
    PseudoTerminal pty;

    if (! pty.start (juce::File ("/tmp"), 80, 24))
    {
        printf ("FAIL: start() failed: %s\n", pty.getLastError().toRawUTF8());
        return 1;
    }

    /* Ask the shell to print a distinctive string and leave. */
    const char* cmd = "printf 'PTYOK\\n'; exit 0\n";
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

    /* Resizing a live pty must not throw or corrupt the descriptor. */
    pty.setSize (100, 40);

    pty.stop();
    if (pty.isRunning())
    {
        printf ("FAIL: still running after stop()\n");
        return 1;
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
