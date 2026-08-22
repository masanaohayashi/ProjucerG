#pragma once

#include "jucer_AiSession.h"

/*  会話を追記専用の JSONL として残し、/resume で読み戻す。

    保存先は <projectRoot>/.projucer/ai-sessions/<時刻>-<uuid>.jsonl。
    1 行 1 レコードで、封筒は必ず { ts, n, type, payload } の形にする。
    n はファイル内の 0 起点の連番で、将来 /fork が「n ≤ K をコピーして
    parent を記録する」だけで済むように振ってある。

    ファイルは truncate しない。追記しかしない。読み側は知らない type を
    黙って飛ばすので、後から新しいレコードを足しても古い版で開ける。
*/
namespace AiSessionStore
{
    struct Listing
    {
        juce::File file;
        juce::String title;
        juce::Time modified;
    };

    struct Restored
    {
        juce::Array<juce::var> conversation;
        juce::Array<AiSession::Entry> entries;

        /** 読み込んだファイルへ追記を続けるための、次に振る連番。 */
        int nextOrdinal = 0;
    };

    /** meta 行だけを書いた新しいファイルを作る。作れなければ空の File を返す。 */
    juce::File createSessionFile (const juce::File& projectRoot, const juce::String& model);

    /** API の input item を 1 行足す。書けたかどうかを返す。 */
    bool appendItem (const juce::File& sessionFile, int ordinal, const juce::var& item);

    /** 更新の新しい順。各ファイルは見出しを作るのに必要な先頭だけ読む。 */
    juce::Array<Listing> listRecent (const juce::File& projectRoot, int limit);

    /** 読めた分だけ返す。壊れた行や書きかけで切れた行は飛ばす。 */
    Restored restore (const juce::File& sessionFile);
}
