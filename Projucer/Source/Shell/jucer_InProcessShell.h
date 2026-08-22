#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

/*  プロセス内 POSIX サブセットシェル。

    iOS では fork/exec が使えないので、コマンド文字列を同じプロセス内の
    アプレット関数へ翻訳して実行する。macOS でも同じ実装をリンクでき、
    自己チェックは Mac 上で回す。実際の exec_command 差し替えは iOS のみ。
*/
namespace ProjucerShell
{
    struct Result
    {
        int exitCode = 1;
        juce::String output;   // stdout と stderr を結合。既存 exec_command と同じ
    };

    struct Request
    {
        juce::String commandLine;
        juce::File workingDirectory;
        juce::File sandboxRoot;          // 書き込みと cd の上限。通常は projectRoot
        std::atomic<bool>* cancelFlag = nullptr;
        int timeoutMs = 300000;
        int maxOutputChars = 64 * 1024;
    };

    Result run (const Request&);
    juce::StringArray availableCommands();

    /*  iOS の AI 向け。使えるコマンドと、それ以外は動かないこと、
        コンパイルは build ツールに任せること。 */
    juce::String iosAgentGuidance();
}
