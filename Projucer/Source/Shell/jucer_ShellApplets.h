#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <map>

/* シェル本体とアプレット TU で共有する内部型。公開 API には出さない。 */
struct ShellIo
{
    juce::InputStream* in = nullptr;
    juce::OutputStream* out = nullptr;
    juce::OutputStream* err = nullptr;
};

struct ShellState
{
    juce::File cwd;
    juce::File sandboxRoot;
    std::map<juce::String, juce::String> env;
    std::atomic<bool>* cancelFlag = nullptr;
    juce::int64 deadlineMs = 0;
    int timeoutMs = 0;
    int maxOutputChars = 64 * 1024;
};

using AppletFn = int (*) (const juce::StringArray& argv, ShellIo& io, ShellState& state);

void registerApplet (const juce::String& name, AppletFn fn);
void registerBuiltinApplets();
int runInProcessScript (const juce::String& commandLine, ShellIo& io, ShellState& state);
juce::File resolvePath (const ShellState& state, const juce::String& operand);
juce::File resolveWritablePath (const ShellState& state, const juce::String& operand, juce::OutputStream& err);
