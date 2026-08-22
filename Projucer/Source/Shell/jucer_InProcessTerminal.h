#pragma once

#include "jucer_ShellApplets.h"

#include <mutex>
#include <string>

/*  対話用のプロセス内シェル。1 行ごとに runInProcessScript を回し、
    cwd / env / $? をセッションの間保持する。PTY は持たない。 */
class InProcessTerminal
{
public:
    InProcessTerminal() = default;

    void start (const juce::File& workingDirectory, const juce::File& sandboxRoot);
    void stop();

    /** キーボードからの生バイト。完成した行があればその場で実行する。 */
    void feed (const char* data, int numBytes);

    /** 実行中コマンドを止める（Ctrl-C）。 */
    void requestCancel();

    /** 待ちが無ければ 0。セッション終了かつバッファ空なら -1。 */
    int read (char* destination, int maxBytes);

    bool isRunning() const noexcept;
    bool isExecuting() const noexcept;
    int getExitCode() const noexcept;

private:
    void appendOut (const juce::String& text);
    void writePrompt();
    void executeLine (const juce::String& line);
    void handleByte (unsigned char c);
    void handleBackspace();
    void finishLine();

    enum class EscState { Normal, Esc, Csi, Osc };

    ShellState state;
    std::string lineBytes;
    EscState escState = EscState::Normal;
    int utf8Need = 0;
    juce::uint32 utf8Acc = 0;
    std::atomic<bool> cancelFlag { false };
    std::atomic<bool> running { false };
    std::atomic<bool> executing { false };
    std::atomic<int> sessionExitCode { 0 };

    juce::MemoryBlock output;
    size_t outputRead = 0;
    mutable std::mutex outputLock;
};
