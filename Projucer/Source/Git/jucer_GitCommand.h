#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

/*  In-process git.

    iOS ではシェルも子プロセスも使えないので、git のコマンドラインを
    libgit2 の API へ翻訳して同じプロセスの中で実行する。オンデバイスの
    clang を組み込んだのと同じ考え方で、外部バイナリに頼らない。

    macOS でも同じ実装を通せるようにしてあるので、挙動の確認は Mac 上で
    できる。実際に使うのは iOS のみ（AiTools が振り分ける）。
*/
namespace ProjucerGit
{
    struct Result
    {
        int exitCode = 1;
        juce::String output;
    };

    /** "git status -s" のような 1 行が git 呼び出しかどうかを判定する。 */
    bool isGitCommandLine (const juce::String& commandLine);

    /** コマンドラインを分解して run() へ渡す。先頭の "git" は省略可。 */
    Result runCommandLine (const juce::String& commandLine,
                           const juce::File& workingDirectory,
                           std::atomic<bool>* cancelFlag = nullptr);

    /** 引数（"git" を含まない）を実行する。 */
    Result run (const juce::StringArray& args,
                const juce::File& workingDirectory,
                std::atomic<bool>* cancelFlag = nullptr);

    /** ツールスキーマの説明に載せる、実装済みサブコマンドの一覧。 */
    juce::StringArray supportedCommands();
}
