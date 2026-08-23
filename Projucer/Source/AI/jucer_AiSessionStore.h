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

    /*  要約が n ≤ replacesThrough の item を置き換える、という 1 行を足す。
        このあと残す発言を appendItem で積み直すと、restore() がメモリ上の
        圧縮後の状態をそのまま返す。 */
    bool appendCompaction (const juce::File& sessionFile,
                           int ordinal,
                           const juce::String& summaryText,
                           int replacesThrough);

    /*  要約の前に置く断り書き。これが無いと、次のモデルが要約を「自分がさっき
        言ったこと」と取り違える。要約かどうかの印も兼ねる。 */
    constexpr const char* summaryPreamble =
        "Another language model started to solve this problem and produced a summary of its "
        "thinking process. You also have access to the state of the tools that were used by that "
        "language model. Use this to build on the work that has already been done and avoid "
        "duplicating work. Here is the summary produced by the other language model, use the "
        "information in this summary to assist with your own analysis:\n";

    /*  compact 行の summary を会話へ戻すときの形。前置きはここで足すので、
        ファイルにはモデルが書いた要約そのものだけが残る。 */
    juce::var makeSummaryMessage (const juce::String& summaryText);

    /** makeSummaryMessage が作ったものか。前置きで見分ける。 */
    bool isSummaryMessage (const juce::var& item);

    /*  圧縮のときに生のまま残すユーザー発言を、新しい順に予算いっぱいまで拾って
        時系列順に返す。予算を跨ぐ 1 件は末尾を切って入れる。 */
    juce::Array<juce::var> selectRecentUserMessages (const juce::Array<juce::var>& conversation,
                                                     int characterBudget = 40000);

    /** 会話から表示用の entries を作る。復元と圧縮で食い違わないよう 1 箇所に置く。 */
    juce::Array<AiSession::Entry> rebuildEntries (const juce::Array<juce::var>& conversation);

    /*  コンテキストの重さの目安。Responses API の usage は SSE に載ってこないので、
        送っている JSON の文字数で代用する。トークンはその 1/4 として見積もる。 */
    int countCharacters (const juce::var& item);
    int countCharacters (const juce::Array<juce::var>& conversation);

    /** 840 / 12.3K / 128K の見た目にする。概算だと分かるよう "~" は呼び手が付ける。 */
    juce::String formatTokenCount (int tokens);

    /** 更新の新しい順。各ファイルは見出しを作るのに必要な先頭だけ読む。 */
    juce::Array<Listing> listRecent (const juce::File& projectRoot, int limit);

    /** 読めた分だけ返す。壊れた行や書きかけで切れた行は飛ばす。 */
    Restored restore (const juce::File& sessionFile);
}
