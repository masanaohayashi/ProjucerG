#pragma once

#include <juce_core/juce_core.h>

/*  プロジェクトの指示ファイル（AGENTS.md）の収集。

    Codex の core/src/agents_md.rs と同じ方式を採る。

      1. 作業ディレクトリから上へ辿り、標識 (.git) を持つ最も近い祖先を探す。
         これがプロジェクトルート。見つからなければ作業ディレクトリのみを見る。
      2. プロジェクトルートから作業ディレクトリまでの各階層について、
         候補ファイル名を順に探し、最初に見つかった 1 つを採る。
      3. ルート側から順に連結する。下位のディレクトリの指示ほど後に来るので、
         内容が競合する場合は下位が後勝ちになる。

    重要: ここで上へ辿るのは「指示の収集」だけである。ファイルの読み書きの
    範囲は作業ディレクトリのままで、広がらない。Codex も同じ二層構造で、
    AGENTS.md はルートまで登って集めるが、ツールの作業範囲は cwd に固定される。
*/
namespace ProjectInstructions
{
    /** 1 つの指示ファイル。 */
    struct Entry
    {
        juce::File file;
        juce::String contents;
    };

    /** 作業ディレクトリから見て有効な指示ファイルを、ルート側から順に返す。 */
    juce::Array<Entry> collect (const juce::File& workingDirectory);

    /** collect() の結果を 1 つの文字列へ連結する。無ければ空文字列。 */
    juce::String buildText (const juce::File& workingDirectory);

    /** 標識 (.git) を持つ最も近い祖先。無ければ無効な File。 */
    juce::File findProjectRoot (const juce::File& workingDirectory);
}
