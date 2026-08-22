#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <optional>

/* File tools exposed to the AI agent. */
class AiTools
{
public:
    explicit AiTools (const juce::File& projectRootToUse);
    ~AiTools();

    /*  実行中のシェルを止める。AgentLoop の停止から呼ぶ。 */
    void cancel();

    struct Result
    {
        bool ok = false;
        juce::String output;
        juce::String diffPreview;
    };

    static juce::var getToolSchemas();
    static bool requiresApproval (const juce::String& toolName);

    Result preview (const juce::String& toolName, const juce::var& arguments);
    Result execute (const juce::String& toolName, const juce::var& arguments);

    void setUseVisibleTerminal (bool shouldUse) noexcept    { useVisibleTerminal = shouldUse; }
    void setVisibleTerminalRunner (std::function<bool (const juce::String&,
                                                       juce::String&,
                                                       int,
                                                       std::atomic<bool>&)> runner)
    {
        visibleTerminalRunner = std::move (runner);
    }

private:
    struct PreviewState
    {
        juce::String toolName;
        juce::String argumentsKey;
        juce::File file;
        bool existed = false;
        juce::String content;
    };

    bool resolve (const juce::var& arguments, juce::File& fileOut, juce::String& errorOut) const;

    /*  読み取り用の解決。Codex の SandboxPolicy と同じく、読み取りは範囲を
        制限しない（ReadOnly は全ディスク読み取りを許す）。絶対パスも、
        プロジェクト相対パスも受け付ける。書き込み系は resolve() を使い、
        プロジェクトルート配下に限定したままにすること。 */
    bool resolveForReading (const juce::var& arguments, juce::File& fileOut, juce::String& errorOut) const;

    Result doListFiles (const juce::var& arguments) const;
    Result doReadFile  (const juce::var& arguments) const;
    Result doWriteFile (const juce::var& arguments, bool actuallyWrite, PreviewState* previewStateOut = nullptr) const;
    Result doApplyPatch (const juce::var& arguments, bool actuallyWrite, PreviewState* previewStateOut = nullptr) const;
    Result doExecCommand (const juce::var& arguments, bool actuallyRun, PreviewState* previewStateOut = nullptr);
    bool resolveExecWorkdir (const juce::var& arguments, juce::File& directoryOut, juce::String& errorOut) const;

    static juce::String makeArgumentsKey (const juce::var& arguments);
    bool isPreviewStateCurrent (const PreviewState& previewState, juce::String& errorOut) const;
    bool revalidateForWrite (const juce::var& arguments,
                             const juce::File& expectedFile,
                             juce::File& fileOut,
                             juce::String& errorOut) const;

    juce::File projectRoot;
    mutable juce::CriticalSection ioLock;
    mutable std::optional<PreviewState> pendingPreview;
    juce::ChildProcess runningProcess;
    std::atomic<bool> cancelRequested { false };
    bool useVisibleTerminal = false;
    std::function<bool (const juce::String&, juce::String&, int, std::atomic<bool>&)> visibleTerminalRunner;

    static constexpr int maxReadBytes = 1024 * 1024;
    static constexpr int defaultExecTimeoutMs = 300000;
    static constexpr int minExecTimeoutMs = 10000;
    static constexpr int maxExecTimeoutMs = 300000;
    static constexpr int maxExecOutputChars = 64 * 1024;
};
