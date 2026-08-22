#include "jucer_ShellApplets.h"
#include "jucer_InProcessShell.h"
#include "../Git/jucer_GitCommand.h"

#include <algorithm>
#include <cstring>
#include <regex>
#include <vector>

namespace
{
    void writeText (juce::OutputStream* stream, const juce::String& text)
    {
        if (stream == nullptr)
            return;

        const auto utf8 = text.toUTF8();
        stream->write (utf8.getAddress(), (size_t) utf8.sizeInBytes() - 1);
    }

    bool globMatch (const juce::String& text, const juce::String& pattern);

    int appletEcho (const juce::StringArray& argv, ShellIo& io, ShellState&)
    {
        bool noNewline = false;
        int start = 1;

        if (argv.size() > 1 && argv[1] == "-n")
        {
            noNewline = true;
            start = 2;
        }

        juce::String text;

        for (int i = start; i < argv.size(); ++i)
        {
            if (i > start)
                text << " ";

            text << argv[i];
        }

        if (! noNewline)
            text << "\n";

        writeText (io.out, text);
        return 0;
    }

    int appletTrue (const juce::StringArray&, ShellIo&, ShellState&)
    {
        return 0;
    }

    int appletFalse (const juce::StringArray&, ShellIo&, ShellState&)
    {
        return 1;
    }

    juce::File fileFromOperand (const ShellState& state, const juce::String& path)
    {
        if (juce::File::isAbsolutePath (path))
            return juce::File (path);

        return state.cwd.getChildFile (path);
    }

    void copyStream (juce::InputStream& in, juce::OutputStream* out)
    {
        if (out == nullptr)
            return;

        char buffer[4096];

        while (! in.isExhausted())
        {
            const auto n = in.read (buffer, (int) sizeof buffer);

            if (n <= 0)
                break;

            out->write (buffer, (size_t) n);
        }
    }

    int appletCat (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        if (argv.size() <= 1)
        {
            if (io.in != nullptr)
                copyStream (*io.in, io.out);

            return 0;
        }

        int status = 0;

        for (int i = 1; i < argv.size(); ++i)
        {
            juce::FileInputStream in (fileFromOperand (state, argv[i]));

            if (! in.openedOk())
            {
                writeText (io.err, "cat: " + argv[i] + ": No such file or directory\n");
                status = 1;
                continue;
            }

            copyStream (in, io.out);
        }

        return status;
    }

    juce::String expandTrSet (const juce::String& set)
    {
        juce::String out;

        for (int i = 0; i < set.length(); ++i)
        {
            if (i + 2 < set.length() && set[i + 1] == '-' && set[i] <= set[i + 2])
            {
                for (juce::juce_wchar c = set[i]; c <= set[i + 2]; ++c)
                    out += juce::String::charToString (c);

                i += 2;
                continue;
            }

            out += juce::String::charToString (set[i]);
        }

        return out;
    }

    int appletTr (const juce::StringArray& argv, ShellIo& io, ShellState&)
    {
        bool deleteMode = false;
        int argi = 1;

        if (argv.size() > 1 && argv[1] == "-d")
        {
            deleteMode = true;
            argi = 2;
        }

        const int remaining = argv.size() - argi;

        if (deleteMode)
        {
            if (remaining != 1)
            {
                writeText (io.err, "tr: usage: tr -d set\n");
                return 2;
            }
        }
        else if (remaining != 2)
        {
            writeText (io.err, "tr: usage: tr set1 set2\n");
            return 2;
        }

        const auto from = expandTrSet (argv[argi]);
        juce::String to = deleteMode ? juce::String() : expandTrSet (argv[argi + 1]);

        if (! deleteMode)
        {
            if (to.isEmpty())
            {
                writeText (io.err, "tr: set2 is empty\n");
                return 2;
            }

            while (to.length() < from.length())
                to += juce::String::charToString (to[to.length() - 1]);
        }

        juce::String input;

        if (io.in != nullptr)
            input = io.in->readEntireStreamAsString();

        juce::String output;

        for (int i = 0; i < input.length(); ++i)
        {
            const auto c = input[i];
            int mapped = -1;

            for (int j = 0; j < from.length(); ++j)
            {
                if (c == from[j])
                {
                    mapped = j;
                    break;
                }
            }

            if (deleteMode)
            {
                if (mapped < 0)
                    output += c;

                continue;
            }

            output += mapped >= 0 ? to[mapped] : c;
        }

        writeText (io.out, output);
        return 0;
    }

    juce::String appletBasename (const juce::String& argv0)
    {
        auto name = argv0;

        while (name.endsWithChar ('/') || name.endsWithChar ('\\'))
            name = name.dropLastCharacters (1);

        const auto slash = juce::jmax (name.lastIndexOfChar ('/'),
                                       name.lastIndexOfChar ('\\'));

        if (slash >= 0)
            return name.substring (slash + 1);

        return name;
    }

    bool isInsideSandbox (const ShellState& state, const juce::File& file)
    {
        if (state.sandboxRoot == juce::File())
            return true;

        return file == state.sandboxRoot || file.isAChildOf (state.sandboxRoot);
    }

    int appletPwd (const juce::StringArray&, ShellIo& io, ShellState& state)
    {
        writeText (io.out, state.cwd.getFullPathName() + "\n");
        return 0;
    }

    int appletCd (const juce::StringArray& argv, ShellIo&, ShellState& state)
    {
        juce::String dest;

        if (argv.size() <= 1)
        {
            const auto found = state.env.find ("HOME");
            dest = found != state.env.end() ? found->second
                                            : state.sandboxRoot.getFullPathName();
        }
        else
        {
            dest = argv[1];
        }

        juce::File target;

        if (dest.isEmpty())
            target = state.sandboxRoot;
        else if (juce::File::isAbsolutePath (dest))
            target = juce::File (dest);
        else
            target = state.cwd.getChildFile (dest);

        // Result.output は stderr を結合するので、ここへ診断を出すと
        // `cd missing || echo fail` の自己チェックが fail\n だけにならない。
        if (! target.isDirectory())
            return 1;

        juce::File resolved = target;
        int hops = 0;

        while (resolved.isSymbolicLink() && hops++ < 32)
            resolved = resolved.getLinkedTarget();

        if (! resolved.isDirectory() || ! isInsideSandbox (state, resolved))
            return 1;

        state.cwd = resolved;
        state.env["PWD"] = resolved.getFullPathName();
        return 0;
    }

    juce::String interpretPrintfEscapes (const juce::String& format)
    {
        juce::String out;

        for (int i = 0; i < format.length(); ++i)
        {
            if (format[i] == '\\' && i + 1 < format.length())
            {
                const auto next = format[i + 1];

                if (next == 'n')
                {
                    out << '\n';
                    ++i;
                    continue;
                }

                if (next == 't')
                {
                    out << '\t';
                    ++i;
                    continue;
                }

                if (next == '\\')
                {
                    out << '\\';
                    ++i;
                    continue;
                }
            }

            out << format[i];
        }

        return out;
    }

    int appletPrintf (const juce::StringArray& argv, ShellIo& io, ShellState&)
    {
        if (argv.size() < 2)
            return 0;

        const auto format = interpretPrintfEscapes (argv[1]);
        juce::String out;
        int argIndex = 2;

        for (int i = 0; i < format.length(); ++i)
        {
            if (format[i] == '%' && i + 1 < format.length())
            {
                const auto spec = format[i + 1];

                if (spec == '%')
                {
                    out << '%';
                    ++i;
                    continue;
                }

                if (spec == 's')
                {
                    if (argIndex < argv.size())
                        out << argv[argIndex++];

                    ++i;
                    continue;
                }

                if (spec == 'd')
                {
                    if (argIndex < argv.size())
                        out << argv[argIndex++].getIntValue();

                    ++i;
                    continue;
                }
            }

            out << format[i];
        }

        writeText (io.out, out);
        return 0;
    }

    int appletWhich (const juce::StringArray& argv, ShellIo& io, ShellState&)
    {
        if (argv.size() < 2)
            return 1;

        const auto name = argv[1];

        if (! ProjucerShell::availableCommands().contains (name))
            return 1;

        writeText (io.out, name + "\n");
        return 0;
    }

    int appletHelp (const juce::StringArray&, ShellIo& io, ShellState&)
    {
        writeText (io.out, ProjucerShell::availableCommands().joinIntoString (" ") + "\n");
        return 0;
    }

    int appletExit (const juce::StringArray& argv, ShellIo&, ShellState& state)
    {
        state.exitRequested = true;

        if (argv.size() <= 1)
            return state.lastStatus;

        return argv[1].getIntValue();
    }

    int appletExport (const juce::StringArray& argv, ShellIo&, ShellState& state)
    {
        for (int i = 1; i < argv.size(); ++i)
        {
            const auto eq = argv[i].indexOfChar ('=');

            if (eq >= 0)
                state.env[argv[i].substring (0, eq)] = argv[i].substring (eq + 1);
            else if (state.env.find (argv[i]) == state.env.end())
                state.env[argv[i]] = {};
        }

        return 0;
    }

    int appletUnset (const juce::StringArray& argv, ShellIo&, ShellState& state)
    {
        for (int i = 1; i < argv.size(); ++i)
            state.env.erase (argv[i]);

        return 0;
    }

    int appletEnv (const juce::StringArray&, ShellIo& io, ShellState& state)
    {
        for (const auto& entry : state.env)
            writeText (io.out, entry.first + "=" + entry.second + "\n");

        return 0;
    }

    int appletSh (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        const auto name = appletBasename (argv[0]);

        if (argv.size() < 3 || argv[1] != "-c")
        {
            writeText (io.err, name + ": usage: " + name + " -c command\n");
            return 2;
        }

        return runInProcessScript (argv[2], io, state);
    }

    bool walkCancelled (const ShellState& state)
    {
        return state.cancelFlag != nullptr && state.cancelFlag->load();
    }

    bool walkTimedOut (const ShellState& state)
    {
        return state.deadlineMs != 0
            && (juce::int64) juce::Time::getMillisecondCounter() >= state.deadlineMs;
    }

    int checkWalkLimit (ShellIo& io, const ShellState& state)
    {
        if (walkCancelled (state))
            return 1;

        if (walkTimedOut (state))
        {
            writeText (io.err, "The command timed out after "
                               + juce::String (state.timeoutMs / 1000) + " seconds.\n");
            return 124;
        }

        return 0;
    }

    int appletGit (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        const auto result = ProjucerGit::run (argv, state.cwd, state.cancelFlag);

        if (io.out != nullptr)
            io.out->writeText (result.output, false, false, nullptr);

        return result.exitCode;
    }

    juce::File requireWritable (const ShellState& state, ShellIo& io, const juce::String& path)
    {
        if (io.err != nullptr)
            return resolveWritablePath (state, path, *io.err);

        juce::MemoryOutputStream discard;
        return resolveWritablePath (state, path, discard);
    }

    bool isDashOption (const juce::String& token)
    {
        return token.startsWithChar ('-') && token != "-" && token != "--";
    }

    bool parseNonNegativeInt (const juce::String& text, int& value)
    {
        if (text.isEmpty())
            return false;

        int n = 0;

        for (int i = 0; i < text.length(); ++i)
        {
            const auto c = text[i];

            if (c < '0' || c > '9')
                return false;

            n = n * 10 + (int) (c - '0');
        }

        value = n;
        return true;
    }

    juce::StringArray readLines (juce::InputStream* in)
    {
        if (in == nullptr)
            return {};

        const auto text = in->readEntireStreamAsString();
        auto lines = juce::StringArray::fromLines (text);

        if (text.endsWithChar ('\n') && ! lines.isEmpty() && lines[lines.size() - 1].isEmpty())
            lines.remove (lines.size() - 1);

        return lines;
    }

    int writeLineRange (ShellIo& io, const juce::StringArray& lines, int start, int count)
    {
        const int n = lines.size();
        const int begin = juce::jlimit (0, n, start);
        const int end = juce::jlimit (0, n, start + count);

        for (int i = begin; i < end; ++i)
            writeText (io.out, lines[i] + "\n");

        return 0;
    }

    int unknownOption (ShellIo& io, const juce::String& name, const juce::String& token)
    {
        writeText (io.err, name + ": unknown option: " + token + "\n");
        return 1;
    }

    juce::String lsTypeChar (const juce::File& file)
    {
        if (file.isDirectory())
            return "d";

        return "-";
    }

    int printLsEntry (ShellIo& io, const juce::File& file, const juce::String& name, bool longFormat)
    {
        if (longFormat)
            writeText (io.out, lsTypeChar (file) + " " + juce::String (file.getSize()) + " " + name + "\n");
        else
            writeText (io.out, name + "\n");

        return 0;
    }

    int lsDirectory (const juce::File& dir, const juce::String& heading, bool showAll,
                     bool longFormat, bool recursive, bool printHeading,
                     ShellIo& io, ShellState& state)
    {
        if (const auto limited = checkWalkLimit (io, state))
            return limited;

        if (printHeading)
            writeText (io.out, heading + ":\n");

        if (showAll)
        {
            printLsEntry (io, dir, ".", longFormat);
            printLsEntry (io, dir.getParentDirectory(), "..", longFormat);
        }

        auto children = dir.findChildFiles (juce::File::findFilesAndDirectories, false, "*",
                                            juce::File::FollowSymlinks::no);
        children.sort();

        juce::Array<juce::File> subdirs;

        for (const auto& child : children)
        {
            const auto name = child.getFileName();

            if (! showAll && name.startsWithChar ('.'))
                continue;

            printLsEntry (io, child, name, longFormat);

            if (recursive && child.isDirectory() && ! child.isSymbolicLink())
                subdirs.add (child);
        }

        int status = 0;

        for (const auto& sub : subdirs)
        {
            writeText (io.out, "\n");
            const auto childHeading = heading == "." ? sub.getFileName()
                                                     : heading + "/" + sub.getFileName();
            const auto subStatus = lsDirectory (sub, childHeading, showAll, longFormat,
                                                recursive, true, io, state);

            if (subStatus != 0)
                status = subStatus;
        }

        return status;
    }

    int appletLs (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        bool showAll = false;
        bool longFormat = false;
        bool recursive = false;
        juce::StringArray operands;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (! isDashOption (token))
            {
                operands.add (token);
                continue;
            }

            for (int j = 1; j < token.length(); ++j)
            {
                const auto c = token[j];

                if (c == 'a')
                    showAll = true;
                else if (c == 'l')
                    longFormat = true;
                else if (c == '1')
                    continue;
                else if (c == 'R')
                    recursive = true;
                else
                    return unknownOption (io, "ls", juce::String ("-") + juce::String::charToString (c));
            }
        }

        if (operands.isEmpty())
            operands.add (".");

        const bool printHeading = recursive || operands.size() > 1;
        int status = 0;
        bool printed = false;

        for (const auto& operand : operands)
        {
            const auto file = resolvePath (state, operand);

            if (! file.exists() && ! file.isSymbolicLink())
            {
                writeText (io.err, "ls: " + operand + ": No such file or directory\n");
                status = 1;
                continue;
            }

            if (printed)
                writeText (io.out, "\n");

            printed = true;

            if (file.isDirectory() && ! file.isSymbolicLink())
            {
                const auto heading = operand;
                const auto dirStatus = lsDirectory (file, heading, showAll, longFormat, recursive,
                                                    printHeading, io, state);

                if (dirStatus != 0)
                    status = dirStatus;
            }
            else
            {
                printLsEntry (io, file, file.getFileName(), longFormat);
            }
        }

        return status;
    }

    int appletMkdir (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        bool parents = false;
        juce::StringArray operands;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (! isDashOption (token))
            {
                operands.add (token);
                continue;
            }

            for (int j = 1; j < token.length(); ++j)
            {
                if (token[j] == 'p')
                    parents = true;
                else
                    return unknownOption (io, "mkdir", juce::String ("-") + juce::String::charToString (token[j]));
            }
        }

        if (operands.isEmpty())
        {
            writeText (io.err, "mkdir: missing operand\n");
            return 1;
        }

        int status = 0;

        for (const auto& operand : operands)
        {
            const auto dest = requireWritable (state, io, operand);

            if (dest == juce::File())
            {
                status = 1;
                continue;
            }

            if (dest.isDirectory())
            {
                if (! parents)
                {
                    writeText (io.err, "mkdir: " + operand + ": File exists\n");
                    status = 1;
                }

                continue;
            }

            if (dest.exists())
            {
                writeText (io.err, "mkdir: " + operand + ": File exists\n");
                status = 1;
                continue;
            }

            if (! parents && ! dest.getParentDirectory().isDirectory())
            {
                writeText (io.err, "mkdir: " + operand + ": No such file or directory\n");
                status = 1;
                continue;
            }

            if (! dest.createDirectory().wasOk() || ! dest.isDirectory())
            {
                writeText (io.err, "mkdir: " + operand + ": cannot create directory\n");
                status = 1;
            }
        }

        return status;
    }

    int removeTree (const juce::File& file, ShellIo& io, ShellState& state)
    {
        if (const auto limited = checkWalkLimit (io, state))
            return limited;

        if (! file.isSymbolicLink() && file.isDirectory())
        {
            auto children = file.findChildFiles (juce::File::findFilesAndDirectories, false, "*",
                                                 juce::File::FollowSymlinks::no);

            for (const auto& child : children)
            {
                const auto childStatus = removeTree (child, io, state);

                if (childStatus != 0)
                    return childStatus;
            }
        }

        if (! file.deleteFile() && file.exists())
        {
            writeText (io.err, "rm: cannot remove " + file.getFullPathName() + "\n");
            return 1;
        }

        return 0;
    }

    bool isSandboxRootPath (const ShellState& state, const juce::File& file)
    {
        return state.sandboxRoot != juce::File() && file == state.sandboxRoot;
    }

    int appletRm (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        bool recursive = false;
        bool force = false;
        juce::StringArray operands;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (! isDashOption (token))
            {
                operands.add (token);
                continue;
            }

            for (int j = 1; j < token.length(); ++j)
            {
                const auto c = token[j];

                if (c == 'r')
                    recursive = true;
                else if (c == 'f')
                    force = true;
                else
                    return unknownOption (io, "rm", juce::String ("-") + juce::String::charToString (c));
            }
        }

        if (operands.isEmpty())
        {
            if (force)
                return 0;

            writeText (io.err, "rm: missing operand\n");
            return 1;
        }

        int status = 0;

        for (const auto& operand : operands)
        {
            const auto file = requireWritable (state, io, operand);

            if (file == juce::File())
            {
                status = 1;
                continue;
            }

            if (isSandboxRootPath (state, file))
            {
                writeText (io.err, "rm: refusing to remove sandbox root\n");
                status = 1;
                continue;
            }

            if (! file.exists() && ! file.isSymbolicLink())
            {
                if (! force)
                {
                    writeText (io.err, "rm: " + operand + ": No such file or directory\n");
                    status = 1;
                }

                continue;
            }

            if (! file.isSymbolicLink() && file.isDirectory() && ! recursive)
            {
                writeText (io.err, "rm: " + operand + ": is a directory\n");
                status = 1;
                continue;
            }

            const auto removeStatus = removeTree (file, io, state);

            if (removeStatus != 0)
                status = removeStatus;
        }

        return status;
    }

    int appletRmdir (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        juce::StringArray operands;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (isDashOption (token))
                return unknownOption (io, "rmdir", token);

            operands.add (token);
        }

        if (operands.isEmpty())
        {
            writeText (io.err, "rmdir: missing operand\n");
            return 1;
        }

        int status = 0;

        for (const auto& operand : operands)
        {
            const auto file = requireWritable (state, io, operand);

            if (file == juce::File())
            {
                status = 1;
                continue;
            }

            if (isSandboxRootPath (state, file))
            {
                writeText (io.err, "rmdir: refusing to remove sandbox root\n");
                status = 1;
                continue;
            }

            if (! file.isDirectory())
            {
                writeText (io.err, "rmdir: " + operand + ": No such file or directory\n");
                status = 1;
                continue;
            }

            if (file.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0)
            {
                writeText (io.err, "rmdir: " + operand + ": Directory not empty\n");
                status = 1;
                continue;
            }

            if (! file.deleteFile())
            {
                writeText (io.err, "rmdir: " + operand + ": cannot remove\n");
                status = 1;
            }
        }

        return status;
    }

    int copyTree (const juce::File& src, const juce::File& dest, bool recursive,
                  ShellIo& io, ShellState& state)
    {
        if (const auto limited = checkWalkLimit (io, state))
            return limited;

        if (! src.isSymbolicLink() && src.isDirectory())
        {
            if (! recursive)
            {
                writeText (io.err, "cp: " + src.getFileName() + ": is a directory\n");
                return 1;
            }

            if (! dest.createDirectory().wasOk() && ! dest.isDirectory())
            {
                writeText (io.err, "cp: cannot create directory " + dest.getFullPathName() + "\n");
                return 1;
            }

            auto children = src.findChildFiles (juce::File::findFilesAndDirectories, false, "*",
                                                juce::File::FollowSymlinks::no);
            int status = 0;

            for (const auto& child : children)
            {
                const auto childStatus = copyTree (child, dest.getChildFile (child.getFileName()),
                                                   true, io, state);

                if (childStatus != 0)
                    status = childStatus;
            }

            return status;
        }

        if (! src.copyFileTo (dest))
        {
            writeText (io.err, "cp: cannot copy to " + dest.getFullPathName() + "\n");
            return 1;
        }

        return 0;
    }

    juce::StringArray parseSimpleFlags (const juce::StringArray& argv, const juce::String& name,
                                        const juce::String& allowed, ShellIo& io, int& status)
    {
        juce::StringArray operands;
        status = 0;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (! isDashOption (token))
            {
                operands.add (token);
                continue;
            }

            for (int j = 1; j < token.length(); ++j)
            {
                if (allowed.containsChar (token[j]))
                    continue;

                status = unknownOption (io, name, juce::String ("-") + juce::String::charToString (token[j]));
                return {};
            }
        }

        return operands;
    }

    int appletCp (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        bool recursive = false;
        juce::StringArray operands;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (! isDashOption (token))
            {
                operands.add (token);
                continue;
            }

            for (int j = 1; j < token.length(); ++j)
            {
                const auto c = token[j];

                if (c == 'R' || c == 'r')
                    recursive = true;
                else
                    return unknownOption (io, "cp", juce::String ("-") + juce::String::charToString (c));
            }
        }

        if (operands.size() < 2)
        {
            writeText (io.err, "cp: missing operand\n");
            return 1;
        }

        const auto destOperand = operands[operands.size() - 1];
        const auto dest = requireWritable (state, io, destOperand);

        if (dest == juce::File())
            return 1;

        const bool destIsDir = dest.isDirectory();

        if (operands.size() > 2 && ! destIsDir)
        {
            writeText (io.err, "cp: " + destOperand + ": Not a directory\n");
            return 1;
        }

        int status = 0;

        for (int i = 0; i < operands.size() - 1; ++i)
        {
            const auto src = resolvePath (state, operands[i]);

            if (! src.exists() && ! src.isSymbolicLink())
            {
                writeText (io.err, "cp: " + operands[i] + ": No such file or directory\n");
                status = 1;
                continue;
            }

            const auto target = destIsDir ? dest.getChildFile (src.getFileName()) : dest;
            const auto copyStatus = copyTree (src, target, recursive, io, state);

            if (copyStatus != 0)
                status = copyStatus;
        }

        return status;
    }

    int appletMv (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        juce::StringArray operands;
        int flagStatus = 0;
        operands = parseSimpleFlags (argv, "mv", {}, io, flagStatus);

        if (flagStatus != 0)
            return flagStatus;

        if (operands.size() < 2)
        {
            writeText (io.err, "mv: missing operand\n");
            return 1;
        }

        const auto destOperand = operands[operands.size() - 1];
        const auto dest = requireWritable (state, io, destOperand);

        if (dest == juce::File())
            return 1;

        const bool destIsDir = dest.isDirectory();

        if (operands.size() > 2 && ! destIsDir)
        {
            writeText (io.err, "mv: " + destOperand + ": Not a directory\n");
            return 1;
        }

        int status = 0;

        for (int i = 0; i < operands.size() - 1; ++i)
        {
            const auto src = requireWritable (state, io, operands[i]);

            if (src == juce::File())
            {
                status = 1;
                continue;
            }

            if (isSandboxRootPath (state, src))
            {
                writeText (io.err, "mv: refusing to move sandbox root\n");
                status = 1;
                continue;
            }

            if (! src.exists() && ! src.isSymbolicLink())
            {
                writeText (io.err, "mv: " + operands[i] + ": No such file or directory\n");
                status = 1;
                continue;
            }

            const auto target = destIsDir ? dest.getChildFile (src.getFileName()) : dest;

            if (src.moveFileTo (target))
                continue;

            const auto copyStatus = copyTree (src, target, true, io, state);

            if (copyStatus != 0)
            {
                status = copyStatus;
                continue;
            }

            const auto removeStatus = removeTree (src, io, state);

            if (removeStatus != 0)
                status = removeStatus;
        }

        return status;
    }

    int appletTouch (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        juce::StringArray operands;
        int flagStatus = 0;
        operands = parseSimpleFlags (argv, "touch", {}, io, flagStatus);

        if (flagStatus != 0)
            return flagStatus;

        if (operands.isEmpty())
        {
            writeText (io.err, "touch: missing operand\n");
            return 1;
        }

        int status = 0;

        for (const auto& operand : operands)
        {
            const auto file = requireWritable (state, io, operand);

            if (file == juce::File())
            {
                status = 1;
                continue;
            }

            if (file.exists())
            {
                if (! file.setLastModificationTime (juce::Time::getCurrentTime()))
                {
                    writeText (io.err, "touch: " + operand + ": cannot touch\n");
                    status = 1;
                }

                continue;
            }

            if (! file.getParentDirectory().isDirectory())
            {
                writeText (io.err, "touch: " + operand + ": No such file or directory\n");
                status = 1;
                continue;
            }

            juce::FileOutputStream out (file);

            if (! out.openedOk())
            {
                writeText (io.err, "touch: " + operand + ": cannot touch\n");
                status = 1;
            }
        }

        return status;
    }

    int parseHeadTailArgs (const juce::StringArray& argv, const juce::String& name,
                           int defaultCount, ShellIo& io, int& count, juce::StringArray& operands)
    {
        count = defaultCount;
        operands.clear();

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (! isDashOption (token))
            {
                operands.add (token);
                continue;
            }

            for (int j = 1; j < token.length(); ++j)
            {
                if (token[j] != 'n')
                    return unknownOption (io, name, juce::String ("-") + juce::String::charToString (token[j]));

                juce::String number = token.substring (j + 1);

                if (number.isEmpty())
                {
                    if (i + 1 >= argv.size())
                    {
                        writeText (io.err, name + ": option requires an argument -- n\n");
                        return 1;
                    }

                    number = argv[++i];
                }

                if (! parseNonNegativeInt (number, count))
                {
                    writeText (io.err, name + ": invalid number of lines: " + number + "\n");
                    return 1;
                }

                break;
            }
        }

        return 0;
    }

    int appletHead (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        int count = 10;
        juce::StringArray operands;
        const auto parsed = parseHeadTailArgs (argv, "head", 10, io, count, operands);

        if (parsed != 0)
            return parsed;

        if (operands.isEmpty())
            return writeLineRange (io, readLines (io.in), 0, count);

        int status = 0;

        for (const auto& operand : operands)
        {
            juce::FileInputStream in (resolvePath (state, operand));

            if (! in.openedOk())
            {
                writeText (io.err, "head: " + operand + ": No such file or directory\n");
                status = 1;
                continue;
            }

            writeLineRange (io, readLines (&in), 0, count);
        }

        return status;
    }

    int appletTail (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        int count = 10;
        juce::StringArray operands;
        const auto parsed = parseHeadTailArgs (argv, "tail", 10, io, count, operands);

        if (parsed != 0)
            return parsed;

        auto emit = [count, &io] (const juce::StringArray& lines)
        {
            const int start = juce::jmax (0, lines.size() - count);
            return writeLineRange (io, lines, start, count);
        };

        if (operands.isEmpty())
            return emit (readLines (io.in));

        int status = 0;

        for (const auto& operand : operands)
        {
            juce::FileInputStream in (resolvePath (state, operand));

            if (! in.openedOk())
            {
                writeText (io.err, "tail: " + operand + ": No such file or directory\n");
                status = 1;
                continue;
            }

            emit (readLines (&in));
        }

        return status;
    }

    juce::String readStreamText (juce::InputStream* in)
    {
        if (in == nullptr)
            return {};

        return in->readEntireStreamAsString();
    }

    bool readFileText (const juce::File& file, juce::String& text, juce::MemoryBlock* raw)
    {
        juce::FileInputStream in (file);

        if (! in.openedOk())
            return false;

        juce::MemoryBlock block;
        in.readIntoMemoryBlock (block);

        if (raw != nullptr)
            *raw = block;

        if (block.getSize() == 0)
            text = {};
        else
            text = juce::String::fromUTF8 (static_cast<const char*> (block.getData()),
                                           (int) block.getSize());

        return true;
    }

    bool bytesContainNul (const juce::MemoryBlock& block)
    {
        return block.getSize() > 0
            && std::memchr (block.getData(), 0, block.getSize()) != nullptr;
    }

    int mergeGrepStatus (int a, int b)
    {
        if (a == 124 || b == 124)
            return 124;

        if (a == 2 || b == 2)
            return 2;

        if (a == 0 || b == 0)
            return 0;

        return 1;
    }

    bool grepLineMatches (const juce::String& line, const juce::String& pattern,
                          const std::regex* compiled, bool fixed, bool ignoreCase)
    {
        if (fixed)
            return ignoreCase ? line.containsIgnoreCase (pattern) : line.contains (pattern);

        return compiled != nullptr && std::regex_search (line.toStdString(), *compiled);
    }

    int grepText (const juce::String& text, const juce::String& display,
                  bool printName, const juce::String& pattern, const std::regex* compiled,
                  bool fixed, bool ignoreCase, bool lineNumber, bool invert,
                  bool countOnly, bool listFiles, ShellIo& io)
    {
        const auto lines = [&]
        {
            auto parsed = juce::StringArray::fromLines (text);

            if (text.endsWithChar ('\n') && ! parsed.isEmpty() && parsed[parsed.size() - 1].isEmpty())
                parsed.remove (parsed.size() - 1);

            return parsed;
        }();

        int matches = 0;

        for (int i = 0; i < lines.size(); ++i)
        {
            const bool hit = grepLineMatches (lines[i], pattern, compiled, fixed, ignoreCase);

            if (hit == invert)
                continue;

            ++matches;

            if (countOnly || listFiles)
                continue;

            juce::String out;

            if (printName)
                out << display << ":";

            if (lineNumber)
                out << juce::String (i + 1) << ":";

            out << lines[i] << "\n";
            writeText (io.out, out);
        }

        if (listFiles && matches > 0)
            writeText (io.out, display + "\n");

        if (countOnly)
        {
            juce::String out;

            if (printName)
                out << display << ":";

            out << juce::String (matches) << "\n";
            writeText (io.out, out);
        }

        return matches > 0 ? 0 : 1;
    }

    bool matchesIncludeGlobs (const juce::String& display, const juce::File& file,
                              const juce::StringArray& includeGlobs)
    {
        if (includeGlobs.isEmpty())
            return true;

        const auto name = file.getFileName();

        for (const auto& glob : includeGlobs)
            if (globMatch (name, glob) || globMatch (display, glob))
                return true;

        return false;
    }

    int grepWalk (const juce::String& display, const juce::File& file, bool recursive,
                  const juce::String& pattern, const std::regex* compiled,
                  bool fixed, bool ignoreCase, bool lineNumber, bool invert,
                  bool countOnly, bool listFiles, bool printName,
                  const juce::StringArray& includeGlobs,
                  ShellIo& io, ShellState& state)
    {
        if (const auto limited = checkWalkLimit (io, state))
            return limited;

        if (file.isDirectory() && ! file.isSymbolicLink())
        {
            if (! recursive)
            {
                writeText (io.err, "grep: " + display + ": Is a directory\n");
                return 2;
            }

            auto children = file.findChildFiles (juce::File::findFilesAndDirectories, false, "*",
                                                 juce::File::FollowSymlinks::no);
            children.sort();
            int status = 1;

            for (const auto& child : children)
            {
                const auto childDisplay = display == "."
                    ? child.getFileName()
                    : display + "/" + child.getFileName();
                const auto childStatus = grepWalk (childDisplay, child, true, pattern, compiled,
                                                   fixed, ignoreCase, lineNumber, invert,
                                                   countOnly, listFiles, true, includeGlobs, io, state);
                status = mergeGrepStatus (status, childStatus);
            }

            return status;
        }

        if (! matchesIncludeGlobs (display, file, includeGlobs))
            return 1;

        juce::MemoryBlock raw;
        juce::String text;

        if (! readFileText (file, text, &raw))
        {
            writeText (io.err, "grep: " + display + ": No such file or directory\n");
            return 2;
        }

        if (bytesContainNul (raw))
            return 1;

        return grepText (text, display, printName, pattern, compiled, fixed, ignoreCase,
                         lineNumber, invert, countOnly, listFiles, io);
    }

    int appletGrep (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        bool fixed = false;
        bool ignoreCase = false;
        bool lineNumber = false;
        bool recursive = false;
        bool invert = false;
        bool countOnly = false;
        bool listFiles = false;
        juce::StringArray operands;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (! isDashOption (token))
            {
                operands.add (token);
                continue;
            }

            for (int j = 1; j < token.length(); ++j)
            {
                const auto c = token[j];

                if (c == 'E')
                    fixed = false;
                else if (c == 'F')
                    fixed = true;
                else if (c == 'i')
                    ignoreCase = true;
                else if (c == 'n')
                    lineNumber = true;
                else if (c == 'r' || c == 'R')
                    recursive = true;
                else if (c == 'v')
                    invert = true;
                else if (c == 'c')
                    countOnly = true;
                else if (c == 'l')
                    listFiles = true;
                else
                    return unknownOption (io, "grep", juce::String ("-") + juce::String::charToString (c));
            }
        }

        if (operands.isEmpty())
        {
            writeText (io.err, "grep: missing pattern\n");
            return 2;
        }

        const auto pattern = operands[0];
        operands.remove (0);

        std::regex compiled;
        const std::regex* compiledPtr = nullptr;

        if (! fixed)
        {
            try
            {
                auto flags = std::regex_constants::ECMAScript;

                if (ignoreCase)
                    flags |= std::regex_constants::icase;

                compiled = std::regex (pattern.toStdString(), flags);
                compiledPtr = &compiled;
            }
            catch (const std::regex_error&)
            {
                writeText (io.err, "grep: invalid regular expression\n");
                return 2;
            }
        }

        if (operands.isEmpty())
        {
            if (recursive)
                operands.add (".");
            else
                return grepText (readStreamText (io.in), "(standard input)", false, pattern,
                                 compiledPtr, fixed, ignoreCase, lineNumber, invert,
                                 countOnly, listFiles, io);
        }

        const bool printName = recursive || operands.size() > 1;
        int status = 1;

        for (const auto& operand : operands)
        {
            const auto file = resolvePath (state, operand);
            const auto walkStatus = grepWalk (operand, file, recursive, pattern, compiledPtr,
                                              fixed, ignoreCase, lineNumber, invert,
                                              countOnly, listFiles, printName, {}, io, state);
            status = mergeGrepStatus (status, walkStatus);
        }

        return status;
    }

    juce::StringArray globsForRgType (const juce::String& type)
    {
        const auto t = type.toLowerCase();

        if (t == "cpp" || t == "cc" || t == "cxx" || t == "c++")
            return { "*.cpp", "*.cc", "*.cxx", "*.h", "*.hpp", "*.hh", "*.mm", "*.m" };

        if (t == "c")
            return { "*.c", "*.h" };

        if (t == "py" || t == "python")
            return { "*.py" };

        if (t == "rs" || t == "rust")
            return { "*.rs" };

        if (t == "js" || t == "ts")
            return { "*.js", "*.ts", "*.jsx", "*.tsx" };

        if (t == "md" || t == "markdown")
            return { "*.md" };

        if (t == "json")
            return { "*.json" };

        if (t == "txt" || t == "text")
            return { "*.txt" };

        return { "*." + t };
    }

    int appletRg (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        bool fixed = false;
        bool ignoreCase = false;
        bool lineNumber = true;
        bool invert = false;
        bool countOnly = false;
        bool listFiles = false;
        juce::String pattern;
        bool havePattern = false;
        juce::StringArray includeGlobs;
        juce::StringArray operands;

        const auto takeValue = [&] (int& i, const juce::String& opt) -> juce::String
        {
            if (i + 1 >= argv.size())
            {
                writeText (io.err, "rg: option requires an argument: " + opt + "\n");
                return {};
            }

            return argv[++i];
        };

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (token == "-e" || token == "--regexp")
            {
                const auto value = takeValue (i, token);

                if (value.isEmpty() && i + 1 >= argv.size())
                    return 2;

                pattern = value;
                havePattern = true;
                continue;
            }

            if (token == "-g" || token == "--glob")
            {
                const auto value = takeValue (i, token);

                if (value.isEmpty() && i + 1 >= argv.size())
                    return 2;

                includeGlobs.add (value);
                continue;
            }

            if (token.startsWith ("--glob="))
            {
                includeGlobs.add (token.fromFirstOccurrenceOf ("=", false, false));
                continue;
            }

            if (token == "-t" || token == "--type")
            {
                const auto value = takeValue (i, token);

                if (value.isEmpty() && i + 1 >= argv.size())
                    return 2;

                includeGlobs.addArray (globsForRgType (value));
                continue;
            }

            if (token.startsWith ("--type="))
            {
                includeGlobs.addArray (globsForRgType (token.fromFirstOccurrenceOf ("=", false, false)));
                continue;
            }

            if (token == "--hidden" || token == "--no-heading" || token.startsWith ("--color"))
            {
                if (token == "--color"
                    && i + 1 < argv.size()
                    && (argv[i + 1] == "never" || argv[i + 1] == "always" || argv[i + 1] == "auto"))
                    ++i;

                continue;
            }

            if (! isDashOption (token))
            {
                operands.add (token);
                continue;
            }

            if (token.startsWith ("--"))
                return unknownOption (io, "rg", token);

            for (int j = 1; j < token.length(); ++j)
            {
                const auto c = token[j];

                if (c == 'e')
                {
                    const auto rest = token.substring (j + 1);
                    juce::String value;

                    if (rest.isNotEmpty())
                    {
                        value = rest;
                    }
                    else
                    {
                        value = takeValue (i, "-e");

                        if (value.isEmpty() && i + 1 >= argv.size())
                            return 2;
                    }

                    pattern = value;
                    havePattern = true;
                    break;
                }

                if (c == 'g')
                {
                    const auto rest = token.substring (j + 1);
                    juce::String value;

                    if (rest.isNotEmpty())
                    {
                        value = rest;
                    }
                    else
                    {
                        value = takeValue (i, "-g");

                        if (value.isEmpty() && i + 1 >= argv.size())
                            return 2;
                    }

                    includeGlobs.add (value);
                    break;
                }

                if (c == 't')
                {
                    const auto rest = token.substring (j + 1);
                    juce::String value;

                    if (rest.isNotEmpty())
                    {
                        value = rest;
                    }
                    else
                    {
                        value = takeValue (i, "-t");

                        if (value.isEmpty() && i + 1 >= argv.size())
                            return 2;
                    }

                    includeGlobs.addArray (globsForRgType (value));
                    break;
                }

                if (c == 'F')
                    fixed = true;
                else if (c == 'i')
                    ignoreCase = true;
                else if (c == 'n')
                    lineNumber = true;
                else if (c == 'N')
                    lineNumber = false;
                else if (c == 'v')
                    invert = true;
                else if (c == 'c')
                    countOnly = true;
                else if (c == 'l')
                    listFiles = true;
                else
                    return unknownOption (io, "rg", juce::String ("-") + juce::String::charToString (c));
            }
        }

        if (! havePattern)
        {
            if (operands.isEmpty())
            {
                writeText (io.err, "rg: missing pattern\n");
                return 2;
            }

            pattern = operands[0];
            operands.remove (0);
        }

        std::regex compiled;
        const std::regex* compiledPtr = nullptr;

        if (! fixed)
        {
            try
            {
                auto flags = std::regex_constants::ECMAScript;

                if (ignoreCase)
                    flags |= std::regex_constants::icase;

                compiled = std::regex (pattern.toStdString(), flags);
                compiledPtr = &compiled;
            }
            catch (const std::regex_error&)
            {
                writeText (io.err, "rg: invalid regular expression\n");
                return 2;
            }
        }

        if (operands.isEmpty())
            operands.add (".");

        const bool printName = true;
        int status = 1;

        for (const auto& operand : operands)
        {
            const auto file = resolvePath (state, operand);
            const auto walkStatus = grepWalk (operand, file, true, pattern, compiledPtr,
                                              fixed, ignoreCase, lineNumber, invert,
                                              countOnly, listFiles, printName, includeGlobs,
                                              io, state);
            status = mergeGrepStatus (status, walkStatus);
        }

        return status;
    }

    void countWc (const juce::String& text, int& lines, int& words, int& bytes)
    {
        lines = 0;
        words = 0;
        const auto utf8 = text.toUTF8();
        bytes = (int) utf8.sizeInBytes() - 1;
        bool inWord = false;

        for (int i = 0; i < text.length(); ++i)
        {
            const auto c = text[i];

            if (c == '\n')
                ++lines;

            const bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r';

            if (ws)
            {
                inWord = false;
            }
            else if (! inWord)
            {
                inWord = true;
                ++words;
            }
        }
    }

    juce::String formatWc (bool showLines, bool showWords, bool showBytes,
                           int lines, int words, int bytes, const juce::String& name)
    {
        juce::String out;
        auto append = [&] (int value)
        {
            if (out.isNotEmpty())
                out << " ";

            out << juce::String (value);
        };

        if (showLines)
            append (lines);

        if (showWords)
            append (words);

        if (showBytes)
            append (bytes);

        if (name.isNotEmpty())
        {
            if (out.isNotEmpty())
                out << " ";

            out << name;
        }

        out << "\n";
        return out;
    }

    int appletWc (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        bool showLines = false;
        bool showWords = false;
        bool showBytes = false;
        juce::StringArray operands;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (! isDashOption (token))
            {
                operands.add (token);
                continue;
            }

            for (int j = 1; j < token.length(); ++j)
            {
                const auto c = token[j];

                if (c == 'l')
                    showLines = true;
                else if (c == 'w')
                    showWords = true;
                else if (c == 'c')
                    showBytes = true;
                else
                    return unknownOption (io, "wc", juce::String ("-") + juce::String::charToString (c));
            }
        }

        if (! showLines && ! showWords && ! showBytes)
        {
            showLines = true;
            showWords = true;
            showBytes = true;
        }

        auto emit = [&] (const juce::String& text, const juce::String& name)
        {
            int lines = 0, words = 0, bytes = 0;
            countWc (text, lines, words, bytes);
            writeText (io.out, formatWc (showLines, showWords, showBytes, lines, words, bytes, name));
        };

        if (operands.isEmpty())
        {
            emit (readStreamText (io.in), {});
            return 0;
        }

        int status = 0;
        int totalLines = 0, totalWords = 0, totalBytes = 0;

        for (const auto& operand : operands)
        {
            juce::String text;

            if (! readFileText (resolvePath (state, operand), text, nullptr))
            {
                writeText (io.err, "wc: " + operand + ": No such file or directory\n");
                status = 1;
                continue;
            }

            int lines = 0, words = 0, bytes = 0;
            countWc (text, lines, words, bytes);
            totalLines += lines;
            totalWords += words;
            totalBytes += bytes;
            writeText (io.out, formatWc (showLines, showWords, showBytes, lines, words, bytes, operand));
        }

        if (operands.size() > 1)
            writeText (io.out, formatWc (showLines, showWords, showBytes,
                                         totalLines, totalWords, totalBytes, "total"));

        return status;
    }

    bool parseLeadingInt64 (const juce::String& text, juce::int64& value, bool exact = false)
    {
        int i = 0;

        while (i < text.length() && (text[i] == ' ' || text[i] == '\t'))
            ++i;

        if (exact && i != 0)
            return false;

        bool negative = false;

        if (i < text.length() && (text[i] == '-' || text[i] == '+'))
        {
            negative = text[i] == '-';
            ++i;
        }

        if (i >= text.length() || text[i] < '0' || text[i] > '9')
        {
            value = 0;
            return false;
        }

        juce::int64 n = 0;

        while (i < text.length() && text[i] >= '0' && text[i] <= '9')
        {
            n = n * 10 + (text[i] - '0');
            ++i;
        }

        if (exact && i != text.length())
            return false;

        value = negative ? -n : n;
        return true;
    }

    int compareSortLines (const juce::String& a, const juce::String& b, bool numeric)
    {
        if (numeric)
        {
            juce::int64 na = 0, nb = 0;
            parseLeadingInt64 (a, na);
            parseLeadingInt64 (b, nb);

            if (na != nb)
                return na < nb ? -1 : 1;
        }

        return a.compare (b);
    }

    int appletSort (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        bool reverse = false;
        bool numeric = false;
        bool unique = false;
        juce::StringArray operands;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (! isDashOption (token))
            {
                operands.add (token);
                continue;
            }

            for (int j = 1; j < token.length(); ++j)
            {
                const auto c = token[j];

                if (c == 'r')
                    reverse = true;
                else if (c == 'n')
                    numeric = true;
                else if (c == 'u')
                    unique = true;
                else
                    return unknownOption (io, "sort", juce::String ("-") + juce::String::charToString (c));
            }
        }

        juce::StringArray lines;

        if (operands.isEmpty())
        {
            lines = readLines (io.in);
        }
        else
        {
            for (const auto& operand : operands)
            {
                juce::FileInputStream in (resolvePath (state, operand));

                if (! in.openedOk())
                {
                    writeText (io.err, "sort: " + operand + ": No such file or directory\n");
                    return 1;
                }

                lines.addArray (readLines (&in));
            }
        }

        std::vector<juce::String> rows;

        for (const auto& line : lines)
            rows.push_back (line);

        std::sort (rows.begin(), rows.end(),
                   [numeric, reverse] (const juce::String& a, const juce::String& b)
                   {
                       const auto cmp = compareSortLines (a, b, numeric);
                       return reverse ? cmp > 0 : cmp < 0;
                   });

        if (unique)
        {
            rows.erase (std::unique (rows.begin(), rows.end()), rows.end());
        }

        for (const auto& row : rows)
            writeText (io.out, row + "\n");

        return 0;
    }

    int appletUniq (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        juce::StringArray operands;
        int flagStatus = 0;
        operands = parseSimpleFlags (argv, "uniq", {}, io, flagStatus);

        if (flagStatus != 0)
            return flagStatus;

        juce::StringArray lines;

        if (operands.isEmpty())
        {
            lines = readLines (io.in);
        }
        else if (operands.size() == 1)
        {
            juce::FileInputStream in (resolvePath (state, operands[0]));

            if (! in.openedOk())
            {
                writeText (io.err, "uniq: " + operands[0] + ": No such file or directory\n");
                return 1;
            }

            lines = readLines (&in);
        }
        else
        {
            writeText (io.err, "uniq: extra operand\n");
            return 1;
        }

        juce::String previous;
        bool havePrevious = false;

        for (const auto& line : lines)
        {
            if (havePrevious && line == previous)
                continue;

            writeText (io.out, line + "\n");
            previous = line;
            havePrevious = true;
        }

        return 0;
    }

    bool parseCutFields (const juce::String& spec, juce::Array<int>& fields, int& fromField)
    {
        fields.clear();
        fromField = 0;

        if (spec.endsWithChar ('-') && spec.length() >= 2)
        {
            int n = 0;

            if (! parseNonNegativeInt (spec.dropLastCharacters (1), n) || n < 1)
                return false;

            fromField = n;
            return true;
        }

        juce::StringArray parts;
        parts.addTokens (spec, ",", {});

        for (const auto& part : parts)
        {
            int n = 0;

            if (! parseNonNegativeInt (part, n) || n < 1)
                return false;

            fields.add (n);
        }

        return fields.size() > 0;
    }

    juce::StringArray splitKeepEmpty (const juce::String& line, juce::juce_wchar delim)
    {
        juce::StringArray parts;
        int start = 0;

        for (int i = 0; i < line.length(); ++i)
        {
            if (line[i] == delim)
            {
                parts.add (line.substring (start, i));
                start = i + 1;
            }
        }

        parts.add (line.substring (start));
        return parts;
    }

    int appletCut (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        juce::juce_wchar delim = '\t';
        bool haveFields = false;
        juce::Array<int> fields;
        int fromField = 0;
        juce::StringArray operands;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (token == "-d" || token.startsWith ("-d"))
            {
                juce::String value;

                if (token == "-d")
                {
                    if (i + 1 >= argv.size())
                    {
                        writeText (io.err, "cut: option requires an argument -- d\n");
                        return 1;
                    }

                    value = argv[++i];
                }
                else
                {
                    value = token.substring (2);
                }

                if (value.isEmpty())
                {
                    writeText (io.err, "cut: invalid delimiter\n");
                    return 1;
                }

                delim = value[0];
                continue;
            }

            if (token == "-f" || token.startsWith ("-f"))
            {
                juce::String value;

                if (token == "-f")
                {
                    if (i + 1 >= argv.size())
                    {
                        writeText (io.err, "cut: option requires an argument -- f\n");
                        return 1;
                    }

                    value = argv[++i];
                }
                else
                {
                    value = token.substring (2);
                }

                if (! parseCutFields (value, fields, fromField))
                {
                    writeText (io.err, "cut: invalid field list\n");
                    return 1;
                }

                haveFields = true;
                continue;
            }

            if (isDashOption (token))
                return unknownOption (io, "cut", token);

            operands.add (token);
        }

        if (! haveFields)
        {
            writeText (io.err, "cut: you must specify a list of fields\n");
            return 1;
        }

        juce::StringArray lines;

        if (operands.isEmpty())
        {
            lines = readLines (io.in);
        }
        else
        {
            for (const auto& operand : operands)
            {
                juce::FileInputStream in (resolvePath (state, operand));

                if (! in.openedOk())
                {
                    writeText (io.err, "cut: " + operand + ": No such file or directory\n");
                    return 1;
                }

                lines.addArray (readLines (&in));
            }
        }

        for (const auto& line : lines)
        {
            const auto cols = splitKeepEmpty (line, delim);
            juce::StringArray chosen;

            if (fromField > 0)
            {
                for (int i = fromField; i <= cols.size(); ++i)
                    chosen.add (cols[i - 1]);
            }
            else
            {
                for (const auto field : fields)
                    if (field >= 1 && field <= cols.size())
                        chosen.add (cols[field - 1]);
            }

            writeText (io.out, chosen.joinIntoString (juce::String::charToString (delim)) + "\n");
        }

        return 0;
    }

    void emitUnifiedDiff (const juce::StringArray& a, const juce::StringArray& b,
                          const juce::String& nameA, const juce::String& nameB, ShellIo& io)
    {
        const int n = a.size();
        const int m = b.size();
        std::vector<std::vector<int>> dp ((size_t) n + 1, std::vector<int> ((size_t) m + 1, 0));

        for (int i = n - 1; i >= 0; --i)
            for (int j = m - 1; j >= 0; --j)
                dp[(size_t) i][(size_t) j] = a[i] == b[j]
                    ? dp[(size_t) i + 1][(size_t) j + 1] + 1
                    : std::max (dp[(size_t) i + 1][(size_t) j], dp[(size_t) i][(size_t) j + 1]);

        writeText (io.out, "--- " + nameA + "\n+++ " + nameB + "\n");

        int i = 0, j = 0;

        while (i < n && j < m)
        {
            if (a[i] == b[j])
            {
                ++i;
                ++j;
            }
            else if (dp[(size_t) i + 1][(size_t) j] >= dp[(size_t) i][(size_t) j + 1])
            {
                writeText (io.out, "-" + a[i++] + "\n");
            }
            else
            {
                writeText (io.out, "+" + b[j++] + "\n");
            }
        }

        while (i < n)
            writeText (io.out, "-" + a[i++] + "\n");

        while (j < m)
            writeText (io.out, "+" + b[j++] + "\n");
    }

    int appletDiff (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        bool unified = false;
        juce::StringArray operands;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "--")
            {
                for (int j = i + 1; j < argv.size(); ++j)
                    operands.add (argv[j]);

                break;
            }

            if (! isDashOption (token))
            {
                operands.add (token);
                continue;
            }

            for (int j = 1; j < token.length(); ++j)
            {
                if (token[j] == 'u')
                    unified = true;
                else
                    return unknownOption (io, "diff", juce::String ("-") + juce::String::charToString (token[j]));
            }
        }

        juce::ignoreUnused (unified);

        if (operands.size() != 2)
        {
            writeText (io.err, "diff: usage: diff -u file1 file2\n");
            return 2;
        }

        juce::FileInputStream inA (resolvePath (state, operands[0]));
        juce::FileInputStream inB (resolvePath (state, operands[1]));

        if (! inA.openedOk() || ! inB.openedOk())
        {
            if (! inA.openedOk())
                writeText (io.err, "diff: " + operands[0] + ": No such file or directory\n");

            if (! inB.openedOk())
                writeText (io.err, "diff: " + operands[1] + ": No such file or directory\n");

            return 2;
        }

        const auto a = readLines (&inA);
        const auto b = readLines (&inB);

        if (a == b)
            return 0;

        emitUnifiedDiff (a, b, operands[0], operands[1], io);
        return 1;
    }

    struct SedCommand
    {
        std::regex pattern;
        juce::String replacement;
        bool global = false;
    };

    bool parseSedScript (const juce::String& script, SedCommand& command, juce::String& error)
    {
        if (script.length() < 4 || script[0] != 's')
        {
            error = "sed: unsupported script\n";
            return false;
        }

        const auto delim = script[1];
        int i = 2;
        juce::String pattern;

        while (i < script.length())
        {
            if (script[i] == '\\' && i + 1 < script.length() && script[i + 1] == delim)
            {
                pattern += delim;
                i += 2;
                continue;
            }

            if (script[i] == delim)
                break;

            pattern += script[i++];
        }

        if (i >= script.length() || script[i] != delim)
        {
            error = "sed: unterminated substitute\n";
            return false;
        }

        ++i;
        juce::String replacement;

        while (i < script.length())
        {
            if (script[i] == '\\' && i + 1 < script.length() && script[i + 1] == delim)
            {
                replacement += delim;
                i += 2;
                continue;
            }

            if (script[i] == delim)
                break;

            replacement += script[i++];
        }

        if (i >= script.length() || script[i] != delim)
        {
            error = "sed: unterminated substitute\n";
            return false;
        }

        ++i;
        bool ignoreCase = false;
        command.global = false;

        for (; i < script.length(); ++i)
        {
            if (script[i] == 'g')
                command.global = true;
            else if (script[i] == 'i')
                ignoreCase = true;
            else
            {
                error = "sed: unsupported script\n";
                return false;
            }
        }

        try
        {
            auto flags = std::regex_constants::ECMAScript;

            if (ignoreCase)
                flags |= std::regex_constants::icase;

            command.pattern = std::regex (pattern.toStdString(), flags);
            command.replacement = replacement;
        }
        catch (const std::regex_error&)
        {
            error = "sed: invalid regular expression\n";
            return false;
        }

        return true;
    }

    juce::String applySedCommands (const juce::String& line, const std::vector<SedCommand>& commands)
    {
        auto current = line.toStdString();

        for (const auto& command : commands)
        {
            auto format = std::regex_constants::format_sed;

            if (! command.global)
                format |= std::regex_constants::format_first_only;

            current = std::regex_replace (current, command.pattern,
                                          command.replacement.toStdString(), format);
        }

        return juce::String (current);
    }

    juce::String sedTransformText (const juce::String& text, const std::vector<SedCommand>& commands)
    {
        auto lines = juce::StringArray::fromLines (text);
        const bool trailingNewline = text.endsWithChar ('\n');

        if (trailingNewline && ! lines.isEmpty() && lines[lines.size() - 1].isEmpty())
            lines.remove (lines.size() - 1);

        juce::String out;

        for (const auto& line : lines)
            out << applySedCommands (line, commands) << "\n";

        if (! trailingNewline && text.isNotEmpty() && out.endsWithChar ('\n'))
            out = out.dropLastCharacters (1);

        return out;
    }

    int appletSed (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        bool inPlace = false;
        juce::StringArray scripts;
        juce::StringArray operands;
        bool endedOptions = false;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (! endedOptions && token == "--")
            {
                endedOptions = true;
                continue;
            }

            if (! endedOptions && (token == "-i" || token == "-i''"))
            {
                inPlace = true;
                continue;
            }

            if (! endedOptions && (token == "-e" || token.startsWith ("-e")))
            {
                if (token == "-e")
                {
                    if (i + 1 >= argv.size())
                    {
                        writeText (io.err, "sed: option requires an argument -- e\n");
                        return 2;
                    }

                    scripts.add (argv[++i]);
                }
                else
                {
                    scripts.add (token.substring (2));
                }

                continue;
            }

            if (! endedOptions && isDashOption (token))
                return unknownOption (io, "sed", token);

            operands.add (token);
        }

        if (scripts.isEmpty())
        {
            if (operands.isEmpty())
            {
                writeText (io.err, "sed: missing script\n");
                return 2;
            }

            scripts.add (operands[0]);
            operands.remove (0);
        }

        std::vector<SedCommand> commands;
        juce::String error;

        for (const auto& script : scripts)
        {
            SedCommand command;

            if (! parseSedScript (script, command, error))
            {
                writeText (io.err, error);
                return 2;
            }

            commands.push_back (std::move (command));
        }

        if (inPlace && operands.isEmpty())
        {
            writeText (io.err, "sed: -i requires a file operand\n");
            return 2;
        }

        if (operands.isEmpty())
        {
            writeText (io.out, sedTransformText (readStreamText (io.in), commands));
            return 0;
        }

        int status = 0;

        for (const auto& operand : operands)
        {
            if (inPlace)
            {
                const auto file = requireWritable (state, io, operand);

                if (file == juce::File())
                {
                    status = 1;
                    continue;
                }

                juce::String text;

                if (! readFileText (file, text, nullptr))
                {
                    writeText (io.err, "sed: " + operand + ": No such file or directory\n");
                    status = 1;
                    continue;
                }

                const auto transformed = sedTransformText (text, commands);

                if (! file.replaceWithText (transformed, false, false, "\n"))
                {
                    writeText (io.err, "sed: " + operand + ": cannot write\n");
                    status = 1;
                }

                continue;
            }

            juce::String text;

            if (! readFileText (resolvePath (state, operand), text, nullptr))
            {
                writeText (io.err, "sed: " + operand + ": No such file or directory\n");
                status = 1;
                continue;
            }

            writeText (io.out, sedTransformText (text, commands));
        }

        return status;
    }

    bool globMatch (const juce::String& text, const juce::String& pattern)
    {
        int ti = 0, pi = 0, starP = -1, starT = -1;

        while (ti < text.length())
        {
            if (pi < pattern.length() && (pattern[pi] == '?' || pattern[pi] == text[ti]))
            {
                ++ti;
                ++pi;
            }
            else if (pi < pattern.length() && pattern[pi] == '*')
            {
                starP = pi++;
                starT = ti;
            }
            else if (starP >= 0)
            {
                pi = starP + 1;
                ti = ++starT;
            }
            else
            {
                return false;
            }
        }

        while (pi < pattern.length() && pattern[pi] == '*')
            ++pi;

        return pi == pattern.length();
    }

    int findWalk (const juce::File& file, const juce::String& display,
                  const juce::String& nameGlob, juce::juce_wchar typeFilter,
                  ShellIo& io, ShellState& state)
    {
        if (const auto limited = checkWalkLimit (io, state))
            return limited;

        const bool isDir = file.isDirectory();
        const bool isFile = file.existsAsFile() && ! isDir;
        bool typeOk = true;

        if (typeFilter == 'f')
            typeOk = isFile;
        else if (typeFilter == 'd')
            typeOk = isDir;

        const auto base = display == "." ? juce::String (".") : file.getFileName();
        const bool nameOk = nameGlob.isEmpty() || globMatch (base, nameGlob);

        if (typeOk && nameOk)
            writeText (io.out, display + "\n");

        if (! isDir || file.isSymbolicLink())
            return 0;

        auto children = file.findChildFiles (juce::File::findFilesAndDirectories, false, "*",
                                             juce::File::FollowSymlinks::no);
        children.sort();
        int status = 0;

        for (const auto& child : children)
        {
            const auto childDisplay = display == "."
                ? juce::String ("./") + child.getFileName()
                : display + "/" + child.getFileName();
            const auto childStatus = findWalk (child, childDisplay, nameGlob, typeFilter, io, state);

            if (childStatus != 0)
                status = childStatus;
        }

        return status;
    }

    int appletFind (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        juce::String start;
        juce::String nameGlob;
        juce::juce_wchar typeFilter = 0;

        for (int i = 1; i < argv.size(); ++i)
        {
            const auto& token = argv[i];

            if (token == "-name")
            {
                if (i + 1 >= argv.size())
                {
                    writeText (io.err, "find: missing argument to -name\n");
                    return 1;
                }

                nameGlob = argv[++i];
                continue;
            }

            if (token == "-type")
            {
                if (i + 1 >= argv.size())
                {
                    writeText (io.err, "find: missing argument to -type\n");
                    return 1;
                }

                const auto value = argv[++i];

                if (value != "f" && value != "d")
                {
                    writeText (io.err, "find: unknown -type " + value + "\n");
                    return 1;
                }

                typeFilter = value[0];
                continue;
            }

            if (token.startsWithChar ('-'))
            {
                writeText (io.err, "find: " + token + " not supported\n");
                return 1;
            }

            if (start.isNotEmpty())
            {
                writeText (io.err, "find: extra operand not supported\n");
                return 1;
            }

            start = token;
        }

        if (start.isEmpty())
            start = ".";

        const auto file = resolvePath (state, start);

        if (! file.exists() && ! file.isSymbolicLink())
        {
            writeText (io.err, "find: " + start + ": No such file or directory\n");
            return 1;
        }

        return findWalk (file, start, nameGlob, typeFilter, io, state);
    }

    int evalTest (const juce::StringArray& args, ShellIo& io, ShellState& state, const juce::String& name)
    {
        if (args.isEmpty())
            return 1;

        if (args.size() == 1)
            return args[0].isEmpty() ? 1 : 0;

        if (args.size() == 2)
        {
            const auto op = args[0];
            const auto value = args[1];
            const auto file = resolvePath (state, value);

            if (op == "-f")
                return file.existsAsFile() ? 0 : 1;

            if (op == "-d")
                return file.isDirectory() ? 0 : 1;

            if (op == "-e")
                return (file.exists() || file.isSymbolicLink()) ? 0 : 1;

            if (op == "-s")
                return (file.exists() && file.getSize() > 0) ? 0 : 1;

            if (op == "-n")
                return value.isEmpty() ? 1 : 0;

            if (op == "-z")
                return value.isEmpty() ? 0 : 1;

            writeText (io.err, name + ": unary operator expected\n");
            return 2;
        }

        if (args.size() == 3)
        {
            const auto left = args[0];
            const auto op = args[1];
            const auto right = args[2];

            if (op == "=")
                return left == right ? 0 : 1;

            if (op == "!=")
                return left != right ? 0 : 1;

            if (op == "-eq" || op == "-ne" || op == "-lt" || op == "-gt")
            {
                juce::int64 a = 0, b = 0;

                if (! parseLeadingInt64 (left, a, true) || ! parseLeadingInt64 (right, b, true))
                {
                    writeText (io.err, name + ": integer expression expected\n");
                    return 2;
                }

                if (op == "-eq")
                    return a == b ? 0 : 1;

                if (op == "-ne")
                    return a != b ? 0 : 1;

                if (op == "-lt")
                    return a < b ? 0 : 1;

                return a > b ? 0 : 1;
            }

            writeText (io.err, name + ": unknown binary operator\n");
            return 2;
        }

        writeText (io.err, name + ": too many arguments\n");
        return 2;
    }

    int appletTest (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        const auto name = appletBasename (argv[0]);
        juce::StringArray args;

        for (int i = 1; i < argv.size(); ++i)
            args.add (argv[i]);

        if (name == "[")
        {
            if (args.isEmpty() || args[args.size() - 1] != "]")
            {
                writeText (io.err, "[: missing ']'\n");
                return 2;
            }

            args.remove (args.size() - 1);
        }

        return evalTest (args, io, state, name);
    }
}

void registerBuiltinApplets()
{
    registerApplet ("echo", appletEcho);
    registerApplet ("printf", appletPrintf);
    registerApplet ("true", appletTrue);
    registerApplet ("false", appletFalse);
    registerApplet ("cat", appletCat);
    registerApplet ("tr", appletTr);
    registerApplet ("pwd", appletPwd);
    registerApplet ("cd", appletCd);
    registerApplet ("which", appletWhich);
    registerApplet ("help", appletHelp);
    registerApplet ("exit", appletExit);
    registerApplet ("export", appletExport);
    registerApplet ("unset", appletUnset);
    registerApplet ("env", appletEnv);
    registerApplet ("sh", appletSh);
    registerApplet ("bash", appletSh);
    registerApplet ("ls", appletLs);
    registerApplet ("mkdir", appletMkdir);
    registerApplet ("rm", appletRm);
    registerApplet ("rmdir", appletRmdir);
    registerApplet ("cp", appletCp);
    registerApplet ("mv", appletMv);
    registerApplet ("touch", appletTouch);
    registerApplet ("head", appletHead);
    registerApplet ("tail", appletTail);
    registerApplet ("grep", appletGrep);
    registerApplet ("rg", appletRg);
    registerApplet ("wc", appletWc);
    registerApplet ("sort", appletSort);
    registerApplet ("uniq", appletUniq);
    registerApplet ("cut", appletCut);
    registerApplet ("diff", appletDiff);
    registerApplet ("sed", appletSed);
    registerApplet ("find", appletFind);
    registerApplet ("test", appletTest);
    registerApplet ("[", appletTest);
    registerApplet ("git", appletGit);
}
