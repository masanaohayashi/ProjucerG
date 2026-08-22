#include "jucer_InProcessShell.h"
#include "jucer_ShellApplets.h"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace
{
    std::map<juce::String, AppletFn>& appletTable()
    {
        static std::map<juce::String, AppletFn> table;
        return table;
    }

    std::once_flag& appletsOnce()
    {
        static std::once_flag flag;
        return flag;
    }

    void ensureAppletsRegistered()
    {
        std::call_once (appletsOnce(), [] { registerBuiltinApplets(); });
    }

    juce::String commandBasename (const juce::String& argv0)
    {
        // File コンストラクタは絶対パス前提なので、区切り文字だけで basename を取る。
        auto name = argv0;

        while (name.endsWithChar ('/') || name.endsWithChar ('\\'))
            name = name.dropLastCharacters (1);

        const auto slash = juce::jmax (name.lastIndexOfChar ('/'),
                                       name.lastIndexOfChar ('\\'));

        if (slash >= 0)
            return name.substring (slash + 1);

        return name;
    }

    juce::String unknownCommandMessage (const juce::String& name)
    {
        return "command not found: " + name + "\n"
             + "This in-process shell has no child processes. Available commands:\n"
             + ProjucerShell::availableCommands().joinIntoString (" ") + "\n";
    }

    bool isCompilerLikeName (const juce::String& name)
    {
        static const char* const names[] = {
            "clang", "clang++", "cc", "c++", "swiftc", "make", "cmake",
            "ninja", "xcodebuild", "ld", "ar"
        };

        for (auto* candidate : names)
            if (name == candidate)
                return true;

        return false;
    }

    juce::String compilerRefuseMessage (const juce::String& name)
    {
        return name + " cannot run as a process on iOS.\n"
             + "Compile with the build tool (in-app On-Device Build). "
               "This shell does not produce or run executables.\n";
    }

    enum class TokenKind
    {
        Word,
        Pipe,
        AndIf,
        OrIf,
        Semi,
        Redirect
    };

    struct WordSegment
    {
        juce::String text;
        bool expand = false;
        bool fieldSplit = false;
        bool glob = false;
    };

    struct Token
    {
        TokenKind kind = TokenKind::Word;
        juce::String text;
        std::vector<WordSegment> segments;
    };

    bool isBlank (juce::juce_wchar c) noexcept
    {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }

    bool isDigitChar (juce::juce_wchar c) noexcept
    {
        return c >= '0' && c <= '9';
    }

    bool isDoubleQuoteEscape (juce::juce_wchar c) noexcept
    {
        return c == '$' || c == '`' || c == '"' || c == '\\' || c == '\n';
    }

    void writeText (juce::OutputStream* stream, const juce::String& text)
    {
        if (stream == nullptr)
            return;

        const auto utf8 = text.toUTF8();
        stream->write (utf8.getAddress(), (size_t) utf8.sizeInBytes() - 1);
    }

    bool parseRedirect (const juce::String& input, int& i, Token& token)
    {
        const int n = input.length();
        int j = i;

        while (j < n && isDigitChar (input[j]))
            ++j;

        if (j >= n || (input[j] != '<' && input[j] != '>'))
            return false;

        const auto op = input[j];
        ++j;

        if (j < n && input[j] == op)
            ++j;
        else if (j < n && input[j] == '&')
        {
            ++j;

            while (j < n && isDigitChar (input[j]))
                ++j;
        }

        token.kind = TokenKind::Redirect;
        token.text = input.substring (i, j);
        i = j;
        return true;
    }

    bool isNameStart (juce::juce_wchar c) noexcept
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    }

    bool isNameChar (juce::juce_wchar c) noexcept
    {
        return isNameStart (c) || isDigitChar (c);
    }

    void appendSegment (std::vector<WordSegment>& segments, bool expand, bool fieldSplit, bool glob,
                        const juce::String& text)
    {
        if (text.isEmpty())
            return;

        if (! segments.empty())
        {
            auto& last = segments.back();

            if (last.expand == expand && last.fieldSplit == fieldSplit && last.glob == glob)
            {
                last.text += text;
                return;
            }
        }

        segments.push_back ({ text, expand, fieldSplit, glob });
    }

    void appendSegmentChar (std::vector<WordSegment>& segments, bool expand, bool fieldSplit, bool glob,
                            juce::juce_wchar c)
    {
        juce::String text;
        text += c;
        appendSegment (segments, expand, fieldSplit, glob, text);
    }

    bool scanCommandSubst (const juce::String& input, int& i, juce::String& text, juce::String& error)
    {
        const int n = input.length();
        const int start = i;

        if (i + 1 >= n || input[i] != '$' || input[i + 1] != '(')
            return false;

        i += 2;
        int depth = 1;
        bool inSingle = false;
        bool inDouble = false;

        while (i < n && depth > 0)
        {
            const auto c = input[i];

            if (inSingle)
            {
                if (c == '\'')
                    inSingle = false;

                ++i;
                continue;
            }

            if (inDouble)
            {
                if (c == '\\' && i + 1 < n)
                {
                    i += 2;
                    continue;
                }

                if (c == '"')
                    inDouble = false;

                ++i;
                continue;
            }

            if (c == '\\' && i + 1 < n)
            {
                i += 2;
                continue;
            }

            if (c == '\'')
            {
                inSingle = true;
                ++i;
                continue;
            }

            if (c == '"')
            {
                inDouble = true;
                ++i;
                continue;
            }

            if (c == '`')
            {
                ++i;

                while (i < n && input[i] != '`')
                {
                    if (input[i] == '\\' && i + 1 < n)
                        i += 2;
                    else
                        ++i;
                }

                if (i < n)
                    ++i;

                continue;
            }

            if (c == '(')
                ++depth;
            else if (c == ')')
                --depth;

            ++i;
        }

        if (depth != 0)
        {
            error = "syntax error: unclosed command substitution\n";
            return false;
        }

        text = input.substring (start, i);
        return true;
    }

    bool scanBacktick (const juce::String& input, int& i, juce::String& text, juce::String& error)
    {
        const int n = input.length();
        const int start = i;

        if (i >= n || input[i] != '`')
            return false;

        ++i;

        while (i < n)
        {
            if (input[i] == '\\' && i + 1 < n)
            {
                i += 2;
                continue;
            }

            if (input[i] == '`')
            {
                ++i;
                text = input.substring (start, i);
                return true;
            }

            ++i;
        }

        error = "syntax error: unclosed command substitution\n";
        return false;
    }

    bool parseOperator (const juce::String& input, int& i, Token& token)
    {
        const int n = input.length();

        if (i >= n)
            return false;

        if (parseRedirect (input, i, token))
            return true;

        const auto c = input[i];

        if (c == '&' && i + 1 < n && input[i + 1] == '&')
        {
            token = { TokenKind::AndIf, "&&" };
            i += 2;
            return true;
        }

        if (c == '|' && i + 1 < n && input[i + 1] == '|')
        {
            token = { TokenKind::OrIf, "||" };
            i += 2;
            return true;
        }

        if (c == '|')
        {
            token = { TokenKind::Pipe, "|" };
            ++i;
            return true;
        }

        if (c == ';')
        {
            token = { TokenKind::Semi, ";" };
            ++i;
            return true;
        }

        if (c == '&')
        {
            token = { TokenKind::Redirect, "&" };
            ++i;
            return true;
        }

        return false;
    }

    bool tokenize (const juce::String& input, std::vector<Token>& tokens, juce::String& error)
    {
        const int n = input.length();
        int i = 0;

        while (i < n)
        {
            while (i < n && isBlank (input[i]))
                ++i;

            if (i >= n)
                break;

            if (input[i] == '#')
            {
                while (i < n && input[i] != '\n')
                    ++i;

                continue;
            }

            Token token;

            if (parseOperator (input, i, token))
            {
                tokens.push_back (std::move (token));
                continue;
            }

            std::vector<WordSegment> segments;
            bool inSingle = false;
            bool inDouble = false;
            bool sawQuotes = false;

            auto addUnquoted = [&] (const juce::String& text)
            {
                appendSegment (segments, true, true, true, text);
            };

            auto addDouble = [&] (const juce::String& text)
            {
                appendSegment (segments, true, false, false, text);
            };

            auto addLiteral = [&] (juce::juce_wchar c)
            {
                appendSegmentChar (segments, false, false, false, c);
            };

            while (i < n)
            {
                const auto c = input[i];

                if (inSingle)
                {
                    if (c == '\'')
                        inSingle = false;
                    else
                        appendSegmentChar (segments, false, false, false, c);

                    ++i;
                    continue;
                }

                if (inDouble)
                {
                    if (c == '"')
                    {
                        inDouble = false;
                        ++i;
                        continue;
                    }

                    if (c == '\\' && i + 1 < n && isDoubleQuoteEscape (input[i + 1]))
                    {
                        if (input[i + 1] != '\n')
                            addLiteral (input[i + 1]);

                        i += 2;
                        continue;
                    }

                    if (c == '$' && i + 1 < n && input[i + 1] == '(')
                    {
                        juce::String subst;

                        if (! scanCommandSubst (input, i, subst, error))
                            return false;

                        addDouble (subst);
                        continue;
                    }

                    if (c == '`')
                    {
                        juce::String subst;

                        if (! scanBacktick (input, i, subst, error))
                            return false;

                        addDouble (subst);
                        continue;
                    }

                    appendSegmentChar (segments, true, false, false, c);
                    ++i;
                    continue;
                }

                if (isBlank (c) || c == '#' || c == '|' || c == '&' || c == ';'
                    || c == '<' || c == '>')
                    break;

                if (c == '\'')
                {
                    inSingle = true;
                    sawQuotes = true;
                    ++i;
                    continue;
                }

                if (c == '"')
                {
                    inDouble = true;
                    sawQuotes = true;
                    ++i;
                    continue;
                }

                if (c == '\\')
                {
                    if (i + 1 >= n)
                    {
                        error = "syntax error: trailing backslash\n";
                        return false;
                    }

                    addLiteral (input[i + 1]);
                    i += 2;
                    continue;
                }

                if (c == '$' && i + 1 < n && input[i + 1] == '(')
                {
                    juce::String subst;

                    if (! scanCommandSubst (input, i, subst, error))
                        return false;

                    addUnquoted (subst);
                    continue;
                }

                if (c == '`')
                {
                    juce::String subst;

                    if (! scanBacktick (input, i, subst, error))
                        return false;

                    addUnquoted (subst);
                    continue;
                }

                appendSegmentChar (segments, true, true, true, c);
                ++i;
            }

            if (inSingle || inDouble)
            {
                error = "syntax error: unclosed quote\n";
                return false;
            }

            if (segments.empty() && ! sawQuotes)
                continue;

            juce::String word;

            for (const auto& segment : segments)
                word += segment.text;

            tokens.push_back ({ TokenKind::Word, word, std::move (segments) });
        }

        return true;
    }

    struct ShellWord
    {
        std::vector<WordSegment> segments;
    };

    struct Redirection
    {
        enum class Kind
        {
            In,
            Out,
            OutAppend,
            Err,
            ErrAppend,
            ErrToOut
        };

        Kind kind = Kind::Out;
        ShellWord operand;
        juce::String operandText;
    };

    struct SimpleCommand
    {
        std::vector<ShellWord> words;
        std::vector<Redirection> redirects;
    };

    struct Pipeline
    {
        std::vector<SimpleCommand> commands;
        TokenKind join = TokenKind::Semi;
    };

    bool classifyRedirect (const juce::String& text, Redirection::Kind& kind)
    {
        if (text == "<" || text == "0<")
            kind = Redirection::Kind::In;
        else if (text == ">" || text == "1>")
            kind = Redirection::Kind::Out;
        else if (text == ">>" || text == "1>>")
            kind = Redirection::Kind::OutAppend;
        else if (text == "2>")
            kind = Redirection::Kind::Err;
        else if (text == "2>>")
            kind = Redirection::Kind::ErrAppend;
        else if (text == "2>&1")
            kind = Redirection::Kind::ErrToOut;
        else
            return false;

        return true;
    }

    bool parseList (const std::vector<Token>& tokens, std::vector<Pipeline>& pipelines, juce::String& error)
    {
        SimpleCommand current;
        std::vector<SimpleCommand> commands;

        auto currentIsEmpty = [&]
        {
            return current.words.empty() && current.redirects.empty();
        };

        auto flushCommand = [&] () -> bool
        {
            if (currentIsEmpty())
            {
                error = "syntax error\n";
                return false;
            }

            commands.push_back (std::move (current));
            current = {};
            return true;
        };

        auto flushPipeline = [&] (TokenKind join) -> bool
        {
            if (currentIsEmpty() && commands.empty())
            {
                if (join == TokenKind::Semi)
                    return true;

                error = "syntax error\n";
                return false;
            }

            if (currentIsEmpty() || ! flushCommand())
            {
                error = "syntax error\n";
                return false;
            }

            pipelines.push_back ({ std::move (commands), join });
            commands.clear();
            return true;
        };

        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const auto& token = tokens[i];

            if (token.kind == TokenKind::Word)
            {
                current.words.push_back ({ token.segments });
                continue;
            }

            if (token.kind == TokenKind::Redirect)
            {
                Redirection redir;

                if (! classifyRedirect (token.text, redir.kind))
                {
                    error = "syntax error\n";
                    return false;
                }

                if (redir.kind != Redirection::Kind::ErrToOut)
                {
                    if (i + 1 >= tokens.size() || tokens[i + 1].kind != TokenKind::Word)
                    {
                        error = "syntax error\n";
                        return false;
                    }

                    redir.operand.segments = tokens[++i].segments;
                }

                current.redirects.push_back (std::move (redir));
                continue;
            }

            if (token.kind == TokenKind::Pipe)
            {
                if (! flushCommand())
                    return false;

                continue;
            }

            if (! flushPipeline (token.kind))
                return false;
        }

        if (currentIsEmpty() && commands.empty())
        {
            if (! pipelines.empty()
                && (pipelines.back().join == TokenKind::AndIf || pipelines.back().join == TokenKind::OrIf))
            {
                error = "syntax error\n";
                return false;
            }

            return true;
        }

        if (currentIsEmpty() || ! flushCommand())
        {
            error = "syntax error\n";
            return false;
        }

        pipelines.push_back ({ std::move (commands), TokenKind::Semi });
        return true;
    }

    bool isCancelled (const ShellState& state)
    {
        return state.cancelFlag != nullptr && state.cancelFlag->load();
    }



    juce::String lookupVar (const ShellState& state, const juce::String& name)
    {
        const auto found = state.env.find (name);
        return found != state.env.end() ? found->second : juce::String();
    }

    juce::String stripOneTrailingNewline (juce::String text)
    {
        if (text.endsWithChar ('\n'))
            text = text.dropLastCharacters (1);

        return text;
    }

    juce::String expandText (const juce::String& text, ShellIo& io, ShellState& state)
    {
        juce::String out;
        const int n = text.length();
        int i = 0;

        while (i < n)
        {
            const auto c = text[i];

            if (c == '`')
            {
                juce::String subst;
                juce::String error;
                int j = i;

                if (! scanBacktick (text, j, subst, error))
                {
                    out += c;
                    ++i;
                    continue;
                }

                const auto inner = subst.substring (1, subst.length() - 1);
                juce::MemoryOutputStream captured;
                ShellIo innerIo = io;
                innerIo.out = &captured;
                runInProcessScript (inner, innerIo, state);
                out += stripOneTrailingNewline (captured.toString());
                i = j;
                continue;
            }

            if (c == '$' && i + 1 < n)
            {
                if (text[i + 1] == '{')
                {
                    int j = i + 2;

                    while (j < n && text[j] != '}')
                        ++j;

                    out += lookupVar (state, text.substring (i + 2, j));
                    i = j < n ? j + 1 : n;
                    continue;
                }

                if (text[i + 1] == '(')
                {
                    juce::String subst;
                    juce::String error;
                    int j = i;

                    if (scanCommandSubst (text, j, subst, error))
                    {
                        const auto inner = subst.substring (2, subst.length() - 1);
                        juce::MemoryOutputStream captured;
                        ShellIo innerIo = io;
                        innerIo.out = &captured;
                        runInProcessScript (inner, innerIo, state);
                        out += stripOneTrailingNewline (captured.toString());
                        i = j;
                        continue;
                    }
                }

                if (text[i + 1] == '?')
                {
                    out += juce::String (state.lastStatus);
                    i += 2;
                    continue;
                }

                if (isNameStart (text[i + 1]))
                {
                    int j = i + 1;

                    while (j < n && isNameChar (text[j]))
                        ++j;

                    out += lookupVar (state, text.substring (i + 1, j));
                    i = j;
                    continue;
                }
            }

            out += c;
            ++i;
        }

        return out;
    }

    juce::StringArray splitFields (const juce::String& text)
    {
        juce::StringArray parts;
        juce::String current;
        bool inField = false;

        for (int i = 0; i < text.length(); ++i)
        {
            if (isBlank (text[i]))
            {
                if (inField)
                {
                    parts.add (current);
                    current = {};
                    inField = false;
                }
            }
            else
            {
                current += text[i];
                inField = true;
            }
        }

        if (inField)
            parts.add (current);

        return parts;
    }

    struct ExpandedField
    {
        juce::String text;
        juce::String globMask;
    };

    void appendFieldText (ExpandedField& field, const juce::String& text, bool glob)
    {
        field.text += text;

        for (int i = 0; i < text.length(); ++i)
            field.globMask += glob ? juce::juce_wchar ('1') : juce::juce_wchar ('0');
    }

    bool hasEligibleGlobChar (const ExpandedField& field)
    {
        const int n = juce::jmin (field.text.length(), field.globMask.length());

        for (int i = 0; i < n; ++i)
        {
            const auto c = field.text[i];

            if ((c == '*' || c == '?') && field.globMask[i] == '1')
                return true;
        }

        return false;
    }

    bool globMatch (const juce::String& name, const juce::String& pattern, const juce::String& mask, int ni, int pi)
    {
        if (pi >= pattern.length())
            return ni >= name.length();

        const auto pc = pattern[pi];
        const bool glob = pi < mask.length() && mask[pi] == '1';

        if (glob && pc == '*')
        {
            if (globMatch (name, pattern, mask, ni, pi + 1))
                return true;

            if (ni < name.length())
                return globMatch (name, pattern, mask, ni + 1, pi);

            return false;
        }

        if (ni >= name.length())
            return false;

        if ((glob && pc == '?') || name[ni] == pc)
            return globMatch (name, pattern, mask, ni + 1, pi + 1);

        return false;
    }

    std::vector<ExpandedField> expandWord (const std::vector<WordSegment>& segments, ShellIo& io, ShellState& state)
    {
        std::vector<ExpandedField> fields;
        fields.push_back ({});

        bool hadLiteral = false;
        bool hadUnsplit = false;

        for (const auto& segment : segments)
        {
            if (! segment.expand)
            {
                appendFieldText (fields.back(), segment.text, segment.glob);
                hadLiteral = true;
                continue;
            }

            const auto expanded = expandText (segment.text, io, state);

            if (segment.fieldSplit)
            {
                const auto parts = splitFields (expanded);

                if (parts.isEmpty())
                    continue;

                appendFieldText (fields.back(), parts[0], segment.glob);

                for (int i = 1; i < parts.size(); ++i)
                {
                    ExpandedField next;
                    appendFieldText (next, parts[i], segment.glob);
                    fields.push_back (std::move (next));
                }
            }
            else
            {
                appendFieldText (fields.back(), expanded, segment.glob);
                hadUnsplit = true;
            }
        }

        if (fields.size() == 1 && fields[0].text.isEmpty() && ! hadLiteral && ! hadUnsplit)
            return {};

        return fields;
    }

    juce::StringArray globField (const ShellState& state, const ExpandedField& field)
    {
        if (! hasEligibleGlobChar (field))
            return { field.text };

        const auto slash = juce::jmax (field.text.lastIndexOfChar ('/'),
                                       field.text.lastIndexOfChar ('\\'));
        juce::File dir;
        juce::String pattern;
        juce::String patternMask;
        juce::String prefix;

        if (slash >= 0)
        {
            prefix = field.text.substring (0, slash);
            pattern = field.text.substring (slash + 1);
            patternMask = field.globMask.substring (slash + 1);
            dir = resolvePath (state, prefix.isEmpty() ? juce::String ("/") : prefix);
        }
        else
        {
            pattern = field.text;
            patternMask = field.globMask;
            dir = state.cwd;
        }

        juce::Array<juce::File> candidates;
        dir.findChildFiles (candidates, juce::File::findFilesAndDirectories, false, "*");

        juce::StringArray names;

        for (const auto& file : candidates)
        {
            const auto name = file.getFileName();

            if (! globMatch (name, pattern, patternMask, 0, 0))
                continue;

            if (slash >= 0)
                names.add (prefix + "/" + name);
            else
                names.add (name);
        }

        if (names.isEmpty())
            return { field.text };

        names.sort (false);
        return names;
    }

    juce::StringArray expandToArgv (const std::vector<WordSegment>& segments, ShellIo& io, ShellState& state)
    {
        juce::StringArray argv;

        for (const auto& field : expandWord (segments, io, state))
            argv.addArray (globField (state, field));

        return argv;
    }

    bool isAssignmentWord (const ShellWord& word)
    {
        juce::String text;

        for (const auto& segment : word.segments)
        {
            if (! segment.fieldSplit)
                return false;

            text += segment.text;
        }

        const auto eq = text.indexOfChar ('=');

        if (eq <= 0)
            return false;

        if (! isNameStart (text[0]))
            return false;

        for (int i = 1; i < eq; ++i)
            if (! isNameChar (text[i]))
                return false;

        return true;
    }

    juce::String assignmentText (const ShellWord& word)
    {
        juce::String text;

        for (const auto& segment : word.segments)
            text += segment.text;

        return text;
    }

    void applyAssignmentWord (const ShellWord& word, ShellIo& io, ShellState& state)
    {
        const auto text = assignmentText (word);
        const auto eq = text.indexOfChar ('=');
        state.env[text.substring (0, eq)] = expandText (text.substring (eq + 1), io, state);
    }

    struct SavedAssignment
    {
        juce::String name;
        juce::String previous;
        bool existed = false;
    };

    SavedAssignment snapshotAssignment (const ShellState& state, const juce::String& name)
    {
        SavedAssignment saved;
        saved.name = name;
        const auto found = state.env.find (name);
        saved.existed = found != state.env.end();

        if (saved.existed)
            saved.previous = found->second;

        return saved;
    }

    void restoreAssignments (ShellState& state, const std::vector<SavedAssignment>& saved)
    {
        for (const auto& item : saved)
        {
            if (item.existed)
                state.env[item.name] = item.previous;
            else
                state.env.erase (item.name);
        }
    }

    int applyRedirects (const SimpleCommand& command, ShellIo& io, ShellState& state,
                        std::vector<std::unique_ptr<juce::OutputStream>>& outHolders,
                        std::vector<std::unique_ptr<juce::InputStream>>& inHolders)
    {
        for (const auto& redir : command.redirects)
        {
            if (redir.kind == Redirection::Kind::ErrToOut)
            {
                io.err = io.out;
                continue;
            }

            if (redir.kind == Redirection::Kind::In)
            {
                auto in = std::make_unique<juce::FileInputStream> (resolvePath (state, redir.operandText));

                if (! in->openedOk())
                {
                    writeText (io.err, redir.operandText + ": No such file or directory\n");
                    return 1;
                }

                io.in = in.get();
                inHolders.push_back (std::move (in));
                continue;
            }

            juce::MemoryOutputStream discard;
            auto& errStream = io.err != nullptr ? *io.err : discard;
            const auto file = resolveWritablePath (state, redir.operandText, errStream);

            if (file == juce::File())
                return 1;

            if (! file.getParentDirectory().isDirectory())
            {
                writeText (io.err, redir.operandText + ": No such file or directory\n");
                return 1;
            }

            auto out = std::make_unique<juce::FileOutputStream> (file);

            if (! out->openedOk())
            {
                writeText (io.err, redir.operandText + ": cannot open\n");
                return 1;
            }

            if (redir.kind == Redirection::Kind::Out || redir.kind == Redirection::Kind::Err)
            {
                if (! out->setPosition (0) || ! out->truncate())
                {
                    writeText (io.err, redir.operandText + ": cannot truncate\n");
                    return 1;
                }
            }

            if (redir.kind == Redirection::Kind::Out || redir.kind == Redirection::Kind::OutAppend)
                io.out = out.get();
            else
                io.err = out.get();

            outHolders.push_back (std::move (out));
        }

        return 0;
    }

    int executeSimple (const juce::StringArray& argv, ShellIo& io, ShellState& state)
    {
        if (argv.isEmpty() || argv[0].isEmpty())
            return 0;

        const auto name = commandBasename (argv[0]);
        const auto found = appletTable().find (name);

        if (found == appletTable().end())
        {
            if (isCompilerLikeName (name))
                writeText (io.err, compilerRefuseMessage (name));
            else
                writeText (io.err, unknownCommandMessage (name));

            return 127;
        }

        return found->second (argv, io, state);
    }

    int executeCommand (const SimpleCommand& command, ShellIo io, ShellState& state)
    {
        SimpleCommand expanded;
        std::vector<SavedAssignment> prefixAssignments;
        size_t wordIndex = 0;

        while (wordIndex < command.words.size() && isAssignmentWord (command.words[wordIndex]))
        {
            const auto text = assignmentText (command.words[wordIndex]);
            const auto eq = text.indexOfChar ('=');
            prefixAssignments.push_back (snapshotAssignment (state, text.substring (0, eq)));
            applyAssignmentWord (command.words[wordIndex], io, state);
            ++wordIndex;
        }

        juce::StringArray argv;

        for (; wordIndex < command.words.size(); ++wordIndex)
            argv.addArray (expandToArgv (command.words[wordIndex].segments, io, state));

        for (const auto& redir : command.redirects)
        {
            Redirection copy = redir;

            if (copy.kind != Redirection::Kind::ErrToOut)
            {
                const auto fields = expandToArgv (redir.operand.segments, io, state);

                if (fields.size() != 1)
                {
                    writeText (io.err, "ambiguous redirect\n");

                    if (! argv.isEmpty() && ! argv[0].isEmpty())
                        restoreAssignments (state, prefixAssignments);

                    return 1;
                }

                copy.operandText = fields[0];
            }

            expanded.redirects.push_back (std::move (copy));
        }

        std::vector<std::unique_ptr<juce::OutputStream>> outHolders;
        std::vector<std::unique_ptr<juce::InputStream>> inHolders;
        int status = applyRedirects (expanded, io, state, outHolders, inHolders);

        if (status == 0)
            status = executeSimple (argv, io, state);

        if (! argv.isEmpty() && ! argv[0].isEmpty())
            restoreAssignments (state, prefixAssignments);

        return status;
    }

    int executePipeline (const Pipeline& pipeline, ShellIo& outerIo, ShellState& state)
    {
        int status = 0;
        juce::MemoryBlock captured;
        const int n = (int) pipeline.commands.size();

        for (int i = 0; i < n; ++i)
        {
            if (isCancelled (state))
            {
                writeText (outerIo.err, "The command was stopped.\n");
                return 1;
            }

            ShellIo io = outerIo;
            juce::MemoryInputStream inStream (captured.getData(), captured.getSize(), false);

            if (i > 0)
                io.in = &inStream;

            juce::MemoryOutputStream pipeOut;

            if (i + 1 < n)
                io.out = &pipeOut;

            status = executeCommand (pipeline.commands[(size_t) i], io, state);

            if (i + 1 < n)
                captured = pipeOut.getMemoryBlock();
        }

        return status;
    }

    int executeList (const std::vector<Pipeline>& pipelines, ShellIo& io, ShellState& state)
    {
        int status = 0;
        bool runThis = true;

        for (const auto& pipeline : pipelines)
        {
            if (isCancelled (state))
            {
                writeText (io.err, "The command was stopped.\n");
                return 1;
            }

            if (state.deadlineMs != 0
                && (juce::int64) juce::Time::getMillisecondCounter() >= state.deadlineMs)
            {
                writeText (io.err, "The command timed out after "
                                   + juce::String (state.timeoutMs / 1000) + " seconds.\n");
                return 124;
            }

            if (runThis)
            {
                status = executePipeline (pipeline, io, state);
                state.lastStatus = status;
            }

            if (pipeline.join == TokenKind::AndIf)
                runThis = (status == 0);
            else if (pipeline.join == TokenKind::OrIf)
                runThis = (status != 0);
            else
                runThis = true;
        }

        return status;
    }
}

juce::File resolvePath (const ShellState& state, const juce::String& operand)
{
    if (operand.isEmpty())
        return {};

    if (juce::File::isAbsolutePath (operand))
        return juce::File (operand);

    return state.cwd.getChildFile (operand);
}

juce::File resolveWritablePath (const ShellState& state, const juce::String& operand, juce::OutputStream& err)
{
    const auto writeErr = [&err] (const juce::String& text)
    {
        const auto utf8 = text.toUTF8();
        err.write (utf8.getAddress(), (size_t) utf8.sizeInBytes() - 1);
    };

    const auto file = resolvePath (state, operand);

    if (file == juce::File())
    {
        writeErr ("invalid path\n");
        return {};
    }

    juce::File cursor = file;
    juce::StringArray missing;
    int guard = 0;

    while (cursor != juce::File()
           && cursor.getParentDirectory() != cursor
           && ! cursor.exists()
           && ! cursor.isSymbolicLink()
           && guard++ < 256)
    {
        missing.insert (0, cursor.getFileName());
        cursor = cursor.getParentDirectory();
    }

    int hops = 0;

    while (cursor.isSymbolicLink() && hops++ < 32)
        cursor = cursor.getLinkedTarget();

    auto resolved = cursor;

    for (const auto& part : missing)
        resolved = resolved.getChildFile (part);

    if (state.sandboxRoot != juce::File()
        && resolved != state.sandboxRoot
        && ! resolved.isAChildOf (state.sandboxRoot))
    {
        writeErr ("permission denied: " + operand + "\n");
        return {};
    }

    // 判定は解決後のパス、操作対象は指定されたパス（最後の symlink は辿らない）。
    return file;
}

int runInProcessScript (const juce::String& commandLine, ShellIo& io, ShellState& state)
{
    std::vector<Token> tokens;
    juce::String error;

    if (! tokenize (commandLine, tokens, error))
    {
        writeText (io.err, error);
        return 2;
    }

    std::vector<Pipeline> pipelines;

    if (! parseList (tokens, pipelines, error))
    {
        writeText (io.err, error);
        return 2;
    }

    if (pipelines.empty())
        return 0;

    return executeList (pipelines, io, state);
}

void registerApplet (const juce::String& name, AppletFn fn)
{
    appletTable()[name] = fn;
}

namespace ProjucerShell
{
    juce::StringArray availableCommands()
    {
        ensureAppletsRegistered();

        juce::StringArray names;

        for (const auto& entry : appletTable())
            names.add (entry.first);

        names.sort (false);
        return names;
    }

    juce::String iosAgentGuidance()
    {
        return "On iOS, exec_command is an in-process POSIX subset with no child processes.\n"
               "The only available commands are:\n"
             + availableCommands().joinIntoString (" ") + "\n"
             + "Any other command fails, including clang, clang++, cc, c++, swiftc, make, cmake, "
               "ninja, xcodebuild, ld, ar, and running compiled binaries.\n"
               "To compile this project, call the build tool. It saves the project and runs the "
               "same On-Device Build as the Build button. Do not try to compile through exec_command.\n";
    }

    Result run (const Request& request)
    {
        ensureAppletsRegistered();

        Result result;

        if (request.cancelFlag != nullptr && request.cancelFlag->load())
        {
            result.exitCode = 1;
            result.output = "The command was stopped.\n";
            return result;
        }

        juce::MemoryOutputStream out;
        juce::MemoryOutputStream err;

        ShellState state;
        state.cwd = request.workingDirectory;
        state.sandboxRoot = request.sandboxRoot;
        state.cancelFlag = request.cancelFlag;
        state.timeoutMs = request.timeoutMs;
        state.maxOutputChars = request.maxOutputChars;
        state.env["PWD"] = state.cwd.getFullPathName();
        state.env["HOME"] = (state.sandboxRoot != juce::File() ? state.sandboxRoot : state.cwd)
                                .getFullPathName();
        state.env["PATH"] = "/bin:/usr/bin";

        if (request.timeoutMs > 0)
            state.deadlineMs = (juce::int64) juce::Time::getMillisecondCounter()
                             + (juce::int64) request.timeoutMs;

        ShellIo io;
        io.out = &out;
        io.err = &err;

        result.exitCode = runInProcessScript (request.commandLine, io, state);
        result.output = out.toString() + err.toString();

        if (request.maxOutputChars > 0 && result.output.length() > request.maxOutputChars)
            result.output = result.output.substring (0, request.maxOutputChars) + "\n...[truncated]";

        return result;
    }
}
