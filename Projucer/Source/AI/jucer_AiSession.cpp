#include "jucer_AiSession.h"
#include "jucer_AgentLoop.h"
#include "jucer_AiModels.h"
#include "jucer_AiSessionStore.h"

#include <utility>

AiSession::AiSession (std::shared_ptr<CodexAuth> chatgptAuthToUse,
                     std::shared_ptr<GrokAuth> grokAuthToUse,
                     const juce::File& projectRootToUse)
    : chatgptAuth (std::move (chatgptAuthToUse)),
      grokAuth (std::move (grokAuthToUse)),
      projectRoot (projectRootToUse)
{
}

AiSession::~AiSession()
{
    if (loop != nullptr)
    {
        auto activeLoop = std::move (loop);
        activeLoop->requestStop();

        if (! activeLoop->waitForThreadToExit (0))
            AgentLoop::retainUntilStopped (std::move (activeLoop));
    }
}

void AiSession::sendMessage (const juce::String& text, const juce::Array<juce::File>& attachments)
{
    if (text.trim().isEmpty() || isBusy())
        return;

    if (loop == nullptr)
    {
        AgentLoop::reapRetainedLoops();
        loop = std::make_unique<AgentLoop> (shared_from_this(), chatgptAuth, grokAuth, projectRoot);
    }

    appendEntry (Entry::Kind::user, text);
    loop->start (text, attachments);
}

void AiSession::stop()
{
    if (loop != nullptr)
        loop->requestStop();
}

bool AiSession::isBusy() const
{
    return loop != nullptr && loop->isThreadRunning();
}

const AiSession::PendingApproval* AiSession::getPendingApproval() const
{
    return pendingApproval.get();
}

void AiSession::resolveApproval (bool approved)
{
    if (loop != nullptr)
        loop->provideApproval (approved);
}

void AiSession::addLocalNotice (const juce::String& text)
{
    appendEntry (Entry::Kind::tool, text);
}

void AiSession::resumeFrom (const juce::File& file)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (isBusy())
    {
        addLocalNotice ("Stop the current turn before resuming another conversation.");
        return;
    }

    auto restored = AiSessionStore::restore (file);

    if (restored.conversation.isEmpty())
    {
        addLocalNotice ("That conversation could not be read.");
        return;
    }

    /*  走っていないので作り直してよい。読み戻したファイルへそのまま
        追記を続けるので、resume のたびにファイルが増えることはない。 */
    loop.reset();
    conversation = std::move (restored.conversation);
    entries = std::move (restored.entries);
    sessionFile = file;
    nextOrdinal = restored.nextOrdinal;
    persistenceGaveUp = false;
    pendingApproval.reset();
    sendChangeMessage();
}

void AiSession::appendConversationItem (const juce::var& item)
{
    conversation.add (item);

    if (persistenceGaveUp)
        return;

    // 最初の item で初めてファイルを作る。空の会話でファイルを増やさない。
    if (sessionFile == juce::File())
    {
        sessionFile = AiSessionStore::createSessionFile (projectRoot, AiModels::getSelectedModel());
        nextOrdinal = 1;
    }

    if (AiSessionStore::appendItem (sessionFile, nextOrdinal, item))
    {
        ++nextOrdinal;
        return;
    }

    /*  書けない場所では作り直しても結果は同じで、そのたび連番が 1 に戻って
        会話の尻尾だけのファイルが増える。一度だけ伝えて保存を諦める。 */
    persistenceGaveUp = true;

    const auto notice = "This conversation is not being saved: cannot write to "
                      + projectRoot.getChildFile (".projucer").getFullPathName()
                      + ". It will not appear in /resume.";

    // 追記はワーカースレッドから来る。チャットへの表示はメッセージスレッドで。
    juce::MessageManager::callAsync ([weakSelf = weak_from_this(), notice]
    {
        if (const auto liveSession = weakSelf.lock())
            liveSession->addLocalNotice (notice);
    });
}

void AiSession::setApprovalMode (ApprovalMode mode)
{
    approvalMode.store (mode);
    sendChangeMessage();
}

void AiSession::setAutoApprove (bool shouldAutoApprove)
{
    setApprovalMode (shouldAutoApprove ? ApprovalMode::onUnsafe : ApprovalMode::ask);
}

void AiSession::setExecDestination (ExecDestination destination)
{
    execDestination.store (destination);
    sendChangeMessage();
}

void AiSession::setTerminalRunner (TerminalRunner runner)
{
    terminalRunner = std::move (runner);
}

bool AiSession::dispatchToTerminal (const juce::String& commandLine,
                                    juce::String& output,
                                    int timeoutMs,
                                    std::atomic<bool>& cancelled)
{
    if (terminalRunner == nullptr)
        return false;

    return terminalRunner (commandLine, output, timeoutMs, cancelled);
}

void AiSession::appendEntry (Entry::Kind kind, const juce::String& text)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    entries.add ({ kind, text });
    sendChangeMessage();
}

void AiSession::appendToLastAssistantEntry (const juce::String& delta)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (entries.isEmpty() || entries.getReference (entries.size() - 1).kind != Entry::Kind::assistant)
        entries.add ({ Entry::Kind::assistant, {} });

    entries.getReference (entries.size() - 1).text << delta;
    sendChangeMessage();
}

void AiSession::setPendingApproval (const PendingApproval& approval)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    pendingApproval = std::make_unique<PendingApproval> (approval);
    sendChangeMessage();
}

void AiSession::clearPendingApproval()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    pendingApproval.reset();
    sendChangeMessage();
}

void AiSession::finishTurn()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    pendingApproval.reset();
    sendChangeMessage();
}
