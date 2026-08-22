/*  プロセス内シェルの最小の動作確認。一時ディレクトリを sandbox にして
    echo / true / false と未知コマンドを叩く。 */

#include "../jucer_InProcessShell.h"
#include "../jucer_InProcessTerminal.h"
#include <atomic>
#include <cassert>
#include <iostream>

namespace juce
{
    extern const char* const juce_compilationDate = __DATE__;
    extern const char* const juce_compilationTime = __TIME__;
}

namespace
{
    juce::File root;

    ProjucerShell::Result sh (const juce::String& commandLine)
    {
        ProjucerShell::Request request;
        request.commandLine = commandLine;
        request.workingDirectory = root;
        request.sandboxRoot = root;
        const auto result = ProjucerShell::run (request);
        std::cout << "$ " << commandLine << "\n" << result.output << "\n\n";
        return result;
    }

    void expectExit (const juce::String& commandLine, int code)
    {
        assert (sh (commandLine).exitCode == code);
    }

    void expectOutput (const juce::String& commandLine, const juce::String& text)
    {
        const auto result = sh (commandLine);
        assert (result.exitCode == 0);
        assert (result.output == text);
    }

    void expectContains (const juce::String& commandLine, const juce::String& text)
    {
        const auto result = sh (commandLine);
        assert (result.output.contains (text));
        juce::ignoreUnused (result);
    }
}

int main()
{
    root = juce::File::getSpecialLocation (juce::File::tempDirectory)
             .getChildFile ("projucer-shell-selfcheck-" + juce::String (juce::Time::currentTimeMillis()));
    root.createDirectory();

    expectOutput ("echo hello", "hello\n");
    expectOutput ("echo -n hello", "hello");
    expectOutput ("echo 'a b'", "a b\n");
    expectOutput ("echo \"a b\"", "a b\n");
    expectOutput ("echo a # comment\n", "a\n");
    expectOutput ("true && echo ok", "ok\n");
    {
        // expectOutput は exit 0 を要求するが、false && … の POSIX 終了コードは 1。
        const auto result = sh ("false && echo no");
        assert (result.exitCode == 1);
        assert (result.output == "");
    }
    expectOutput ("false || echo yes", "yes\n");
    expectOutput ("true || echo no", "");
    expectOutput ("false; echo after", "after\n");
    expectExit ("true && false", 1);
    expectExit ("false || true", 0);
    expectExit ("true", 0);
    expectExit ("false", 1);
    expectExit ("not_a_command", 127);
    expectContains ("not_a_command", "command not found");
    expectContains ("not_a_command", "echo");

    expectOutput ("echo hi > f.txt && cat f.txt", "hi\n");
    expectOutput ("echo a >> f.txt && echo b >> f.txt && cat f.txt", "hi\na\nb\n");
    expectOutput ("echo hello | tr e a", "hallo\n");
    expectOutput ("echo x 2>&1", "x\n");

    expectOutput ("FOO=bar echo $FOO", "bar\n");
    expectOutput ("FOO=bar echo x; echo z$FOO", "x\nz\n");
    expectOutput ("export FOO=baz && echo $FOO", "baz\n");
    expectOutput ("echo $(echo nested)", "nested\n");
    expectOutput ("sh -c 'echo nested2'", "nested2\n");
    expectOutput ("bash -c \"echo nested3\"", "nested3\n");
    expectOutput ("pwd", root.getFullPathName() + "\n");

    root.getChildFile ("glob-a.txt").replaceWithText ("a\n");
    root.getChildFile ("glob-b.txt").replaceWithText ("b\n");
    expectContains ("echo glob-*.txt", "glob-a.txt");
    expectContains ("echo glob-*.txt", "glob-b.txt");
    expectOutput ("echo glob-'*.txt'", "glob-*.txt\n");

    expectOutput ("cd subdir_missing || echo fail", "fail\n");

    expectExit ("mkdir -p a/b/c", 0);
    assert (root.getChildFile ("a/b/c").isDirectory());
    expectExit ("touch a/b/c/f.txt", 0);
    root.getChildFile ("a/b/c/f.txt").replaceWithText ("one\ntwo\nthree\n", false, false, "\n");
    expectOutput ("cat a/b/c/f.txt", "one\ntwo\nthree\n");
    expectOutput ("head -n 1 a/b/c/f.txt", "one\n");
    expectOutput ("tail -n 1 a/b/c/f.txt", "three\n");
    expectContains ("ls a/b/c", "f.txt");
    expectExit ("cp a/b/c/f.txt a/copied.txt", 0);
    expectExit ("mv a/copied.txt a/moved.txt", 0);
    assert (root.getChildFile ("a/moved.txt").existsAsFile());
    expectExit ("rm -rf a", 0);
    assert (! root.getChildFile ("a").exists());

    {
        auto target = root.getChildFile ("link-target.txt");
        target.replaceWithText ("keep\n", false, false, "\n");
        auto link = root.getChildFile ("the-link");
        assert (target.createSymbolicLink (link, true));
        expectExit ("rm the-link", 0);
        assert (target.existsAsFile());
        assert (! link.isSymbolicLink());
        assert (! link.exists());

        auto broken = root.getChildFile ("broken-link");
        assert (juce::File::createSymbolicLink (broken, "missing-target", true));
        expectExit ("rm broken-link", 0);
        assert (! broken.isSymbolicLink());
        assert (! broken.exists());
    }

    root.getChildFile ("t.cpp").replaceWithText ("int foo;\nint bar;\n", false, false, "\n");
    root.getChildFile ("u.cpp").replaceWithText ("int foo;\nint baz;\n", false, false, "\n");
    expectContains ("grep -n foo t.cpp", "1:int foo;");
    expectOutput ("grep -c foo t.cpp", "1\n");
    expectContains ("grep -r bar .", "t.cpp");
    expectContains ("rg foo t.cpp", "1:int foo;");
    expectContains ("rg bar", "t.cpp");
    expectContains ("rg -g '*.cpp' foo", "t.cpp");
    expectExit ("rg nosuchpattern", 1);
    expectOutput ("wc -l t.cpp", "2 t.cpp\n");
    expectOutput ("sed 's/foo/qux/' t.cpp", "int qux;\nint bar;\n");
    expectExit ("sed -i 's/bar/qux/' t.cpp", 0);
    assert (root.getChildFile ("t.cpp").loadFileAsString().contains ("qux"));
    expectContains ("find . -name '*.cpp'", "u.cpp");
    expectExit ("test -f u.cpp", 0);
    expectExit ("[ -d . ]", 0);
    expectExit ("test -f missing", 1);
    expectContains ("diff -u t.cpp u.cpp", "-");

    expectExit ("git init", 0);
    expectExit ("git status", 0);
    expectContains ("git status", "On branch");
    expectContains ("echo hi | git", "usage");
    expectExit ("true | git status", 0);

    expectExit ("clang++ foo.cpp -o foo", 127);
    expectContains ("clang++ foo.cpp -o foo", "On-Device Build");
    expectContains ("make", "On-Device Build");

    expectExit ("echo pwned > ../outside.txt", 1);
    expectExit ("cd ..", 1);

    expectExit ("which echo", 0);
    expectContains ("help", "grep");
    expectOutput ("printf '%s\\n' hi", "hi\n");

    {
        auto escape = root.getChildFile ("escape");
        assert (juce::File::createSymbolicLink (escape, "..", true));
        expectExit ("cd escape", 1);
    }

    {
        std::atomic<bool> cancelled { true };
        ProjucerShell::Request request;
        request.commandLine = "echo should-not-run";
        request.workingDirectory = root;
        request.sandboxRoot = root;
        request.cancelFlag = &cancelled;
        const auto result = ProjucerShell::run (request);
        assert (result.exitCode != 0);
        assert (result.output.contains ("stopped") || result.output.contains ("cancel"));
    }

    {
        ProjucerShell::Request request;
        request.commandLine = "grep -r e .";
        request.workingDirectory = root;
        request.sandboxRoot = root;
        request.timeoutMs = 1;
        const auto result = ProjucerShell::run (request);
        // 1ms では走り切る可能性あり。確実な timeout は下の 5000×true で見る。
        juce::ignoreUnused (result);
    }

    {
        juce::String many;

        for (int i = 0; i < 5000; ++i)
            many << "true; ";

        ProjucerShell::Request request;
        request.commandLine = many;
        request.workingDirectory = root;
        request.sandboxRoot = root;
        request.timeoutMs = 1;
        const auto result = ProjucerShell::run (request);
        assert (result.exitCode == 124);
    }

    expectOutput ("false; echo $?", "1\n");
    expectOutput ("true; echo $?", "0\n");
    expectExit ("exit 3", 3);
    expectExit ("exit", 0);

    {
        InProcessTerminal term;
        term.start (root, root);
        assert (term.isRunning());

        auto drain = [&term]()
        {
            juce::String all;
            char buf[1024];

            for (;;)
            {
                const int n = term.read (buf, (int) sizeof (buf));

                if (n <= 0)
                    break;

                all += juce::String::fromUTF8 (buf, n);
            }

            return all;
        };

        const auto banner = drain();
        assert (banner.contains ("in-process"));
        assert (banner.contains ("$"));

        const char line1[] = "echo hi\r";
        term.feed (line1, (int) sizeof (line1) - 1);
        const auto out1 = drain();
        assert (out1.contains ("hi"));

        const char line2[] = "mkdir -p termdir && cd termdir && pwd\r";
        term.feed (line2, (int) sizeof (line2) - 1);
        const auto out2 = drain();
        assert (out2.contains ("termdir"));

        const char line3[] = "pwd\r";
        term.feed (line3, (int) sizeof (line3) - 1);
        assert (drain().contains ("termdir"));

        const char line4[] = "export FOO=bar\r";
        term.feed (line4, (int) sizeof (line4) - 1);
        drain();
        const char line5[] = "echo $FOO\r";
        term.feed (line5, (int) sizeof (line5) - 1);
        assert (drain().contains ("bar"));

        const char line6[] = "false\r";
        term.feed (line6, (int) sizeof (line6) - 1);
        drain();
        const char line7[] = "echo $?\r";
        term.feed (line7, (int) sizeof (line7) - 1);
        assert (drain().contains ("1"));

        const char typed[] = "abc";
        term.feed (typed, (int) sizeof (typed) - 1);
        const char ctrlC[] = "\x03";
        term.feed (ctrlC, 1);
        const auto afterIntr = drain();
        assert (afterIntr.contains ("^C") || afterIntr.contains ("$"));
        const char line8[] = "echo afterintr\r";
        term.feed (line8, (int) sizeof (line8) - 1);
        assert (drain().contains ("afterintr"));

        const char typed2[] = "abcd";
        term.feed (typed2, (int) sizeof (typed2) - 1);
        const char bs[] = "\x7f\x7f";
        term.feed (bs, 2);
        const char rest[] = "\r";
        term.feed (rest, 1);
        const auto afterBs = drain();
        assert (afterBs.contains ("command not found: ab"));
        assert (! afterBs.contains ("command not found: abcd"));

        const char line9[] = "exit 7\r";
        term.feed (line9, (int) sizeof (line9) - 1);
        drain();
        assert (! term.isRunning());
        assert (term.getExitCode() == 7);
    }

    std::cout << "all in-process shell checks passed\n";
    return 0;
}
