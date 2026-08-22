#include "jucer_InProcessTerminal.h"

#include <cstring>

namespace
{
    juce::String utf8FromCodepoint (juce::uint32 cp)
    {
        return juce::String::charToString ((juce::juce_wchar) cp);
    }

    juce::String utf8Slice (const std::string& text, size_t from, size_t to)
    {
        if (from >= text.size() || to <= from)
            return {};

        to = juce::jmin (to, text.size());
        return juce::String::createStringFromData (text.data() + from, (int) (to - from));
    }

    int utf8Codepoints (const std::string& text, size_t from, size_t to)
    {
        int count = 0;
        to = juce::jmin (to, text.size());

        for (size_t i = from; i < to; ++i)
            if ((((unsigned char) text[i]) & 0xc0) != 0x80)
                ++count;

        return count;
    }

    size_t previousUtf8 (const std::string& text, size_t index)
    {
        if (index == 0)
            return 0;

        --index;

        while (index > 0 && (((unsigned char) text[index]) & 0xc0) == 0x80)
            --index;

        return index;
    }

    size_t nextUtf8 (const std::string& text, size_t index)
    {
        if (index >= text.size())
            return text.size();

        ++index;

        while (index < text.size() && (((unsigned char) text[index]) & 0xc0) == 0x80)
            ++index;

        return index;
    }

    void appendCursorLeft (juce::String& moves, int count)
    {
        for (int i = 0; i < count; ++i)
            moves += "\x1b[D";
    }
}

void InProcessTerminal::start (const juce::File& workingDirectory, const juce::File& sandboxRoot)
{
    stop();

    state = {};
    state.cwd = workingDirectory.isDirectory() ? workingDirectory : sandboxRoot;
    state.sandboxRoot = sandboxRoot;
    state.cancelFlag = &cancelFlag;
    state.timeoutMs = 0;
    state.maxOutputChars = 0;
    state.env["PWD"] = state.cwd.getFullPathName();
    state.env["HOME"] = (state.sandboxRoot != juce::File() ? state.sandboxRoot : state.cwd)
                            .getFullPathName();
    state.env["PATH"] = "/bin:/usr/bin";
    state.env["TERM"] = "xterm-256color";

    lineBytes.clear();
    cursorBytes = 0;
    history.clear();
    historyDraft.clear();
    historyIndex = -1;
    escState = EscState::Normal;
    utf8Need = 0;
    utf8Acc = 0;
    cancelFlag.store (false);
    sessionExitCode.store (0);
    {
        const std::lock_guard<std::mutex> lock (outputLock);
        output.reset();
        outputRead = 0;
    }

    running.store (true);
    appendOut ("Projucer in-process shell. No child processes; vim and ./a.out will not run.\n"
               "Type help for commands.\n");
    writePrompt();
}

void InProcessTerminal::stop()
{
    requestCancel();
    running.store (false);
}

void InProcessTerminal::feed (const char* data, int numBytes)
{
    if (data == nullptr || numBytes <= 0 || ! running.load())
        return;

    for (int i = 0; i < numBytes; ++i)
        handleByte ((unsigned char) data[i]);
}

void InProcessTerminal::requestCancel()
{
    cancelFlag.store (true);
}

int InProcessTerminal::read (char* destination, int maxBytes)
{
    if (destination == nullptr || maxBytes <= 0)
        return running.load() ? 0 : -1;

    const std::lock_guard<std::mutex> lock (outputLock);
    const auto available = (int) (output.getSize() - outputRead);

    if (available > 0)
    {
        const int n = juce::jmin (maxBytes, available);
        std::memcpy (destination, (const char*) output.getData() + outputRead, (size_t) n);
        outputRead += (size_t) n;

        if (outputRead == output.getSize())
        {
            output.reset();
            outputRead = 0;
        }

        return n;
    }

    if (! running.load())
        return -1;

    return 0;
}

bool InProcessTerminal::isRunning() const noexcept
{
    return running.load();
}

bool InProcessTerminal::isExecuting() const noexcept
{
    return executing.load();
}

int InProcessTerminal::getExitCode() const noexcept
{
    return sessionExitCode.load();
}

void InProcessTerminal::appendOut (const juce::String& text)
{
    if (text.isEmpty())
        return;

    const auto utf8 = text.toRawUTF8();
    juce::MemoryOutputStream converted;

    for (const char* p = utf8; *p != 0; ++p)
    {
        if (*p == '\n')
            converted << '\r';

        converted << *p;
    }

    const std::lock_guard<std::mutex> lock (outputLock);
    output.append (converted.getData(), converted.getDataSize());
}

void InProcessTerminal::writePrompt()
{
    juce::String label = "~";

    if (state.sandboxRoot != juce::File() && state.cwd != state.sandboxRoot
        && state.cwd.isAChildOf (state.sandboxRoot))
        label = state.cwd.getRelativePathFrom (state.sandboxRoot);
    else if (state.cwd != juce::File())
        label = state.cwd.getFileName();

    appendOut (label + " $ ");
}

void InProcessTerminal::executeLine (const juce::String& line)
{
    if (! running.load())
        return;

    executing.store (true);
    cancelFlag.store (false);
    state.cancelFlag = &cancelFlag;
    state.exitRequested = false;

    juce::MemoryOutputStream out;
    juce::MemoryOutputStream err;
    ShellIo io;
    io.out = &out;
    io.err = &err;

    const int status = runInProcessScript (line, io, state);
    state.lastStatus = status;
    sessionExitCode.store (status);

    auto combined = out.toString() + err.toString();

    if (combined.isNotEmpty() && ! combined.endsWithChar ('\n'))
        combined << "\n";

    appendOut (combined);
    executing.store (false);

    if (state.exitRequested || ! running.load())
    {
        running.store (false);
        return;
    }

    writePrompt();
}

void InProcessTerminal::handleBackspace()
{
    if (cursorBytes == 0)
        return;

    const auto start = previousUtf8 (lineBytes, cursorBytes);
    const auto tail = lineBytes.substr (cursorBytes);
    lineBytes.erase (start, cursorBytes - start);
    cursorBytes = start;

    appendOut ("\b");
    appendOut (utf8Slice (tail, 0, tail.size()));
    appendOut (" ");

    juce::String back;
    appendCursorLeft (back, utf8Codepoints (tail, 0, tail.size()) + 1);
    appendOut (back);
}

void InProcessTerminal::finishLine()
{
    if (cursorBytes < lineBytes.size())
        appendOut (utf8Slice (lineBytes, cursorBytes, lineBytes.size()));

    appendOut ("\n");
    const auto line = juce::String::createStringFromData (lineBytes.data(), (int) lineBytes.size());

    if (lineBytes.size() > 0)
        history.push_back (lineBytes);

    lineBytes.clear();
    cursorBytes = 0;
    historyDraft.clear();
    historyIndex = -1;
    utf8Need = 0;
    executeLine (line);
}

void InProcessTerminal::moveCursorLeft()
{
    if (cursorBytes == 0)
        return;

    cursorBytes = previousUtf8 (lineBytes, cursorBytes);
    appendOut ("\x1b[D");
}

void InProcessTerminal::moveCursorRight()
{
    if (cursorBytes >= lineBytes.size())
        return;

    cursorBytes = nextUtf8 (lineBytes, cursorBytes);
    appendOut ("\x1b[C");
}

void InProcessTerminal::moveCursorToStart()
{
    juce::String back;
    appendCursorLeft (back, utf8Codepoints (lineBytes, 0, cursorBytes));
    appendOut (back);
    cursorBytes = 0;
}

void InProcessTerminal::moveCursorToEnd()
{
    if (cursorBytes >= lineBytes.size())
        return;

    appendOut (utf8Slice (lineBytes, cursorBytes, lineBytes.size()));
    cursorBytes = lineBytes.size();
}

void InProcessTerminal::insertAtCursor (const juce::String& text)
{
    if (text.isEmpty())
        return;

    const auto raw = text.toRawUTF8();
    const auto n = (size_t) text.getNumBytesAsUTF8();
    const auto tail = lineBytes.substr (cursorBytes);
    lineBytes.insert (cursorBytes, raw, n);
    cursorBytes += n;
    appendOut (text);

    if (tail.empty())
        return;

    appendOut (utf8Slice (tail, 0, tail.size()));
    juce::String back;
    appendCursorLeft (back, utf8Codepoints (tail, 0, tail.size()));
    appendOut (back);
}

void InProcessTerminal::replaceCurrentLine (const std::string& next)
{
    moveCursorToStart();
    appendOut ("\x1b[K");
    lineBytes = next;
    cursorBytes = next.size();
    appendOut (utf8Slice (lineBytes, 0, lineBytes.size()));
}

void InProcessTerminal::recallHistory (int direction)
{
    if (history.empty())
        return;

    if (direction < 0)
    {
        if (historyIndex < 0)
        {
            historyDraft = lineBytes;
            historyIndex = (int) history.size() - 1;
        }
        else if (historyIndex > 0)
        {
            --historyIndex;
        }

        replaceCurrentLine (history[(size_t) historyIndex]);
        return;
    }

    if (historyIndex < 0)
        return;

    if (historyIndex + 1 < (int) history.size())
    {
        ++historyIndex;
        replaceCurrentLine (history[(size_t) historyIndex]);
        return;
    }

    historyIndex = -1;
    replaceCurrentLine (historyDraft);
}

void InProcessTerminal::handleCsi (unsigned char finalByte)
{
    switch (finalByte)
    {
        case 'A': recallHistory (-1); break;
        case 'B': recallHistory (1);  break;
        case 'C': moveCursorRight();  break;
        case 'D': moveCursorLeft();   break;
        case 'H': moveCursorToStart(); break;
        case 'F': moveCursorToEnd();  break;
        default: break;
    }
}

void InProcessTerminal::handleByte (unsigned char c)
{
    if (! running.load())
        return;

    switch (escState)
    {
        case EscState::Esc:
            if (c == '[')
                escState = EscState::Csi;
            else if (c == ']')
                escState = EscState::Osc;
            else if (c == 'O')
                escState = EscState::Ss3;
            else
                escState = EscState::Normal;
            return;

        case EscState::Csi:
            if (c >= 0x40 && c <= 0x7e)
            {
                escState = EscState::Normal;
                handleCsi (c);
            }
            return;

        case EscState::Ss3:
            escState = EscState::Normal;
            handleCsi (c);
            return;

        case EscState::Osc:
            if (c == 0x07)
                escState = EscState::Normal;
            else if (c == 0x1b)
                escState = EscState::Esc;
            return;

        case EscState::Normal:
            break;
    }

    if (utf8Need > 0)
    {
        if ((c & 0xc0) != 0x80)
        {
            utf8Need = 0;
        }
        else
        {
            utf8Acc = (utf8Acc << 6) | (juce::uint32) (c & 0x3f);

            if (--utf8Need == 0)
                insertAtCursor (utf8FromCodepoint (utf8Acc));

            return;
        }
    }

    if (c == 0x1b)
    {
        escState = EscState::Esc;
        return;
    }

    if (c == 0x9b)
    {
        escState = EscState::Csi;
        return;
    }

    if (c == 0x03)
    {
        lineBytes.clear();
        cursorBytes = 0;
        historyIndex = -1;
        utf8Need = 0;
        appendOut ("^C\n");
        writePrompt();
        return;
    }

    if (c == 0x04)
    {
        if (lineBytes.empty())
        {
            appendOut ("exit\n");
            sessionExitCode.store (state.lastStatus);
            running.store (false);
        }

        return;
    }

    if (c == 0x08 || c == 0x7f)
    {
        handleBackspace();
        return;
    }

    if (c == 0x15)
    {
        replaceCurrentLine ({});
        return;
    }

    if (c == '\r' || c == '\n')
    {
        finishLine();
        return;
    }

    if (c == '\t')
        return;

    if (c < 32)
        return;

    if (c < 0x80)
    {
        insertAtCursor (juce::String::charToString ((juce::juce_wchar) c));
        return;
    }

    if ((c & 0xe0) == 0xc0)
    {
        utf8Need = 1;
        utf8Acc = (juce::uint32) (c & 0x1f);
    }
    else if ((c & 0xf0) == 0xe0)
    {
        utf8Need = 2;
        utf8Acc = (juce::uint32) (c & 0x0f);
    }
    else if ((c & 0xf8) == 0xf0)
    {
        utf8Need = 3;
        utf8Acc = (juce::uint32) (c & 0x07);
    }
}

