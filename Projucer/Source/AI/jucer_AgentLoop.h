#pragma once

#include "jucer_AiSession.h"
#include "jucer_GrokAuth.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

/* Runs one agent turn on a dedicated thread. */
class AgentLoop final : public juce::Thread
{
public:
    AgentLoop (std::shared_ptr<AiSession> session,
               std::shared_ptr<CodexAuth> chatgptAuth,
               std::shared_ptr<GrokAuth> grokAuth,
               const juce::File& projectRoot);
    ~AgentLoop() override;

    void start (const juce::String& userMessage,
                const juce::Array<juce::File>& attachments = {});

    /*  会話を要約させる 1 往復だけを回す。ツールは付けないのでループしない。 */
    void startCompaction();
    void requestStop();
    void provideApproval (bool approved);
    void run() override;

    static void retainUntilStopped (std::unique_ptr<AgentLoop> loop);
    static void reapRetainedLoops();

private:
    juce::var buildRequestBody (bool forCompaction) const;
    void runCompaction();
    void appendConversationItem (const juce::var& item);
    CodexClient& activeClient();
    bool waitForApproval();

    std::weak_ptr<AiSession> session;
    CodexClient chatgptClient;
    CodexClient grokClient;
    AiTools tools;

    /*  指示ファイルの収集に使う。ツールの作業範囲と同じディレクトリ。 */
    juce::File workingDirectory;
    juce::String pendingUserMessage;
    juce::Array<juce::File> pendingAttachments;

    // スレッドが動いていないときだけ書き換える。startThread() が同期点になる。
    bool compacting = false;
    std::atomic<bool> shouldStop { false };
    juce::WaitableEvent approvalArrived;
    std::atomic<bool> approvalGranted { false };

    static std::vector<std::unique_ptr<AgentLoop>>& retainedLoops();

    static constexpr int maxIterations = 25;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AgentLoop)
};
