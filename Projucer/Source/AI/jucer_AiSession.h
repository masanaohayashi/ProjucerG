#pragma once

#include "jucer_AiTools.h"
#include "jucer_CodexClient.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>

class AgentLoop;

/* Owns the conversation state for one project. */
class AiSession final : public juce::ChangeBroadcaster,
                        public std::enable_shared_from_this<AiSession>
{
public:
    AiSession (std::shared_ptr<CodexAuth> auth, const juce::File& projectRoot);
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
    const PendingApproval* getPendingApproval() const;
    void resolveApproval (bool approved);

    /*  AI を経由せずチャットへ 1 行出す。/model のようなローカルの操作結果を
        会話の流れの中に見せるために使う。 */
    void addLocalNotice (const juce::String& text);

    /*  承認の扱い。ChatGPT の「ChatGPT のアクションの承認方法」と同じ 3 段階。

        ask       書き込みは毎回確認する。既定
        onUnsafe  プロジェクト内の既存ファイルへの部分変更は確認しない。
                  新規作成や全置換など、影響が大きいものだけ確認する
        full      確認しない。書き込み範囲の制限も外す
    */
    enum class ApprovalMode { ask, onUnsafe, full };

    void setApprovalMode (ApprovalMode mode);
    ApprovalMode getApprovalMode() const { return approvalMode.load(); }

    void setAutoApprove (bool shouldAutoApprove);
    bool getAutoApprove() const { return approvalMode.load() != ApprovalMode::ask; }

private:
    friend class AgentLoop;

    void appendEntry (Entry::Kind kind, const juce::String& text);
    void appendToLastAssistantEntry (const juce::String& delta);
    void setPendingApproval (const PendingApproval& approval);
    void clearPendingApproval();
    void finishTurn();

    std::shared_ptr<CodexAuth> auth;
    juce::File projectRoot;
    juce::Array<Entry> entries;
    std::unique_ptr<PendingApproval> pendingApproval;
    std::unique_ptr<AgentLoop> loop;
    std::atomic<ApprovalMode> approvalMode { ApprovalMode::ask };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiSession)
};
