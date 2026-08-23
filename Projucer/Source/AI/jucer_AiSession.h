#pragma once

#include "jucer_AiTools.h"
#include "jucer_CodexAuth.h"
#include "jucer_GrokAuth.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

class AgentLoop;

/* Owns the conversation state for one project. */
class AiSession final : public juce::ChangeBroadcaster,
                        public std::enable_shared_from_this<AiSession>
{
public:
    AiSession (std::shared_ptr<CodexAuth> chatgptAuth,
               std::shared_ptr<GrokAuth> grokAuth,
               const juce::File& projectRoot);
    ~AiSession() override;

    struct Entry
    {
        enum class Kind { user, assistant, tool, error };

        Kind kind = Kind::assistant;
        juce::String text;
    };

    struct PendingApproval
    {
        juce::String callId;
        juce::String toolName;
        juce::String diffPreview;
    };

    /*  添付つきで送る。画像は input_image として本文に載せるので、モデルが
        実際に見られる。画像以外はパスを本文に添えるだけで、中身を読むかは
        モデルが read_file で決める。 */
    void sendMessage (const juce::String& text, const juce::Array<juce::File>& attachments = {});
    void stop();
    bool isBusy() const;

    /** ツールの作業範囲。ファイル選択の初期位置などに使う。 */
    juce::File getProjectRoot() const    { return projectRoot; }

    const juce::Array<Entry>& getEntries() const { return entries; }

    /*  今の会話の概算トークン数。conversation そのものはワーカースレッドの持ち物なので、
        UI から数えに行かず、書いた側が更新したこの値だけを読む。 */
    int getApproximateTokens() const    { return conversationCharacters.load() / 4; }
    const PendingApproval* getPendingApproval() const;
    void resolveApproval (bool approved);

    /*  AI を経由せずチャットへ 1 行出す。/model のようなローカルの操作結果を
        会話の流れの中に見せるために使う。 */
    void addLocalNotice (const juce::String& text);

    /*  保存済みの会話を読み戻す。以降はそのファイルへ追記を続ける。
        実行中は拒否して、その旨をチャットへ出す。 */
    void resumeFrom (const juce::File& sessionFile);

    /*  会話を要約 1 件と直近のユーザー発言だけに縮める。要約はモデルに作らせるので
        1 往復かかる。実行中は拒否して、その旨をチャットへ出す。 */
    void compact();

    /*  承認の扱い。ChatGPT の「ChatGPT のアクションの承認方法」と同じ 3 段階。

        ask       書き込みとコマンド実行は毎回確認する。既定
        onUnsafe  プロジェクト内の既存ファイルへの部分変更は確認しない。
                  新規作成、全置換、シェル実行など影響が大きいものだけ確認する
        full      確認しない。書き込み範囲の制限も外す
    */
    enum class ApprovalMode { ask, onUnsafe, full };

    void setApprovalMode (ApprovalMode mode);
    ApprovalMode getApprovalMode() const { return approvalMode.load(); }

    void setAutoApprove (bool shouldAutoApprove);
    bool getAutoApprove() const { return approvalMode.load() != ApprovalMode::ask; }

    /*  コマンドの実行先。subprocess は結果をチャットへ返す。
        visibleTerminal は下のターミナルへ打ち込む。 */
    enum class ExecDestination { subprocess, visibleTerminal };

    void setExecDestination (ExecDestination destination);
    ExecDestination getExecDestination() const { return execDestination.load(); }

    using TerminalRunner = std::function<bool (const juce::String& commandLine,
                                               juce::String& output,
                                               int timeoutMs,
                                               std::atomic<bool>& cancelled)>;
    void setTerminalRunner (TerminalRunner runner);
    bool dispatchToTerminal (const juce::String& commandLine,
                             juce::String& output,
                             int timeoutMs,
                             std::atomic<bool>& cancelled);

private:
    friend class AgentLoop;

    /*  会話そのもの。API へ送る input item をそのまま並べたもので、
        AgentLoop ではなくここが持つ。走行中の AgentLoop の中にあると、
        あとで /btw のように「履歴の途中から枝を作る」操作が触れないため。 */
    const juce::Array<juce::var>& getConversation() const { return conversation; }

    /*  履歴に 1 件足し、同じものをファイルへ 1 行書く。
        「何を残すか」の判断はここだけに置く。 */
    void appendConversationItem (const juce::var& item);

    /*  出来上がった要約で会話を差し替え、ファイルにも同じ形を残す。
        AgentLoop のワーカースレッドから呼ぶ。 */
    void applyCompaction (const juce::String& summaryText);

    void appendEntry (Entry::Kind kind, const juce::String& text);
    void appendToLastAssistantEntry (const juce::String& delta);
    void setPendingApproval (const PendingApproval& approval);
    void clearPendingApproval();
    void finishTurn();

    std::shared_ptr<CodexAuth> chatgptAuth;
    std::shared_ptr<GrokAuth> grokAuth;
    juce::File projectRoot;
    juce::Array<Entry> entries;

    /*  conversation はワーカースレッド (AgentLoop) だけが触る。ロックを置かないのは、
        ループは同時に 1 本しか走らず、メッセージスレッドからの resumeFrom() は
        isBusy() が偽のときしか通さないため。 */
    juce::Array<juce::var> conversation;

    /*  conversation の JSON の文字数。書いた側が必ず更新する。UI が読むので atomic。 */
    std::atomic<int> conversationCharacters { 0 };
    juce::File sessionFile;
    int nextOrdinal = 0;

    /*  保存を諦めた印。sessionFile が空なだけだと「まだ作っていない」と区別できず、
        書けない場所で item ごとにファイルを作り直しにいってしまう。 */
    bool persistenceGaveUp = false;

    std::unique_ptr<PendingApproval> pendingApproval;
    std::unique_ptr<AgentLoop> loop;
    std::atomic<ApprovalMode> approvalMode { ApprovalMode::ask };
    std::atomic<ExecDestination> execDestination { ExecDestination::subprocess };
    TerminalRunner terminalRunner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiSession)
};
