/*  Built-in git の最小の動作確認。実際のリポジトリを一時ディレクトリに
    作り、AI が使う経路（コマンドライン文字列）で叩く。 */

#include "../jucer_GitCommand.h"

#include <cassert>
#include <iostream>

/*  JuceHeader を使わない単体ビルドなので、juce_core が参照する
    コンパイル時刻をここで用意する。 */
namespace juce
{
    extern const char* const juce_compilationDate = __DATE__;
    extern const char* const juce_compilationTime = __TIME__;
}

namespace
{
    juce::File root;

    ProjucerGit::Result git (const juce::String& commandLine)
    {
        const auto result = ProjucerGit::runCommandLine (commandLine, root);
        std::cout << "$ " << commandLine << "\n" << result.output << "\n\n";
        return result;
    }

    void expectOk (const juce::String& commandLine)
    {
        const auto result = git (commandLine);
        assert (result.exitCode == 0);
        juce::ignoreUnused (result);
    }

    void expectFailure (const juce::String& commandLine)
    {
        const auto result = git (commandLine);
        assert (result.exitCode != 0);
        juce::ignoreUnused (result);
    }

    void expectContains (const juce::String& commandLine, const juce::String& text)
    {
        const auto result = git (commandLine);
        assert (result.exitCode == 0);
        assert (result.output.contains (text));
        juce::ignoreUnused (result, text);
    }
}

int main()
{
    root = juce::File::getSpecialLocation (juce::File::tempDirectory)
             .getChildFile ("projucer-git-selfcheck-" + juce::String (juce::Time::currentTimeMillis()));
    root.createDirectory();

    expectOk ("git init");
    expectOk ("git config user.name Tester");
    expectOk ("git config user.email tester@example.com");

    root.getChildFile ("a.txt").replaceWithText ("one\n");
    expectOk ("git add .");
    expectContains ("git status --porcelain", "A  a.txt");

    expectOk ("git commit -m \"first commit\"");
    expectContains ("git log --oneline", "first commit");
    expectContains ("git show HEAD", "+one");

    root.getChildFile ("a.txt").replaceWithText ("one\ntwo\n");
    expectContains ("git diff", "+two");
    expectContains ("git status -s", " M a.txt");

    expectOk ("git add a.txt");
    expectContains ("git diff --cached", "+two");
    expectOk ("git commit -m second");

    expectOk ("git branch feature");
    expectOk ("git checkout feature");
    expectContains ("git rev-parse --abbrev-ref HEAD", "feature");
    expectContains ("git branch", "* feature");

    expectOk ("git tag v1");
    expectContains ("git tag", "v1");
    expectContains ("git ls-files", "a.txt");

    // 引用符の中の空白は 1 つの引数として扱う。
    root.getChildFile ("b file.txt").replaceWithText ("x\n");
    expectOk ("git add \"b file.txt\"");
    expectContains ("git status --porcelain", "b file.txt");
    expectOk ("git commit -m \"a message with spaces\"");
    expectContains ("git log -n 1 --oneline", "a message with spaces");

    // ワーキングディレクトリの外へは出さない。
    expectFailure ("git init ../escape");
    expectFailure ("git clone https://example.com/x.git ../escape");

    // 未対応のコマンドははっきり断る。
    expectFailure ("git rebase main");

    // リモートの登録はネットワーク無しでできる。
    expectOk ("git remote add origin https://example.com/x.git");
    expectContains ("git remote -v", "https://example.com/x.git");

    // 資格情報が無いままの fetch は、保存方法を案内して失敗する。
    const auto fetched = git ("git fetch origin");
    assert (fetched.exitCode != 0);
    juce::ignoreUnused (fetched);

    root.deleteRecursively();
    std::cout << "git selfcheck passed\n";
    return 0;
}
