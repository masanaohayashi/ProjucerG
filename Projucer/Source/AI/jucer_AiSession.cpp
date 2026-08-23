#include "jucer_AiSession.h"
#include "jucer_AgentLoop.h"
#include "jucer_AiModels.h"
#include "jucer_AiSessionStore.h"

#include <utility>

namespace
{
    /*  保存を諦めたときの断り。追記の失敗はどこで起きても同じ意味なので、
        文言も 1 か所に置く。 */
    juce::String notSavedNotice (const juce::File& projectRoot)
    {
        return "This conversation is not being saved: cannot write to "
             + projectRoot.getChildFile (".projucer").getFullPathName()
             + ". It will not appear in /resume.";
    }
}

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
    conversationCharacters.store (AiSessionStore::countCharacters (conversation));
    entries = std::move (restored.entries);
    sessionFile = file;
    nextOrdinal = restored.nextOrdinal;
    persistenceGaveUp = false;
    pendingApproval.reset();
    sendChangeMessage();
}

void AiSession::compact()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (isBusy())
    {
        addLocalNotice ("Stop the current turn before compacting.");
        return;
    }

    // 走っていないので conversation を読んでよい。resumeFrom() と同じ約束。
    if (conversation.isEmpty())
    {
        addLocalNotice ("There is nothing to compact yet.");
        return;
    }

    if (loop == nullptr)
    {
        AgentLoop::reapRetainedLoops();
        loop = std::make_unique<AgentLoop> (shared_from_this(), chatgptAuth, grokAuth, projectRoot);
    }

    addLocalNotice ("Compacting the conversation...");
    loop->startCompaction();
}

void AiSession::applyCompaction (const juce::String& summaryText)
{
    /*  ワーカースレッドから呼ぶ。conversation はワーカーだけが触るという既存の
        約束をそのまま守れる場所がここしかないため。表示用の entries は
        メッセージスレッドの持ち物なので、作るだけ作って向こうで差し替える。 */
    const auto kept = AiSessionStore::selectRecentUserMessages (conversation);
    const auto itemsBefore = conversation.size();

    juce::Array<juce::var> compacted;
    compacted.add (AiSessionStore::makeSummaryMessage (summaryText));
    compacted.addArray (kept);

    /*  ファイルにも同じ形を残す。compact 行のあとに残す発言を item として積み直せば、
        読み側の「n > replaces_through の item だけ残す」がこの状態を再現する。 */
    juce::String warning;

    if (! persistenceGaveUp && sessionFile != juce::File())
    {
        auto written = AiSessionStore::appendCompaction (sessionFile, nextOrdinal, summaryText,
                                                         nextOrdinal - 1);
        if (written)
            ++nextOrdinal;

        for (const auto& item : kept)
        {
            if (! written)
                break;

            written = AiSessionStore::appendItem (sessionFile, nextOrdinal, item);

            if (written)
                ++nextOrdinal;
        }

        /*  途中で書けなくなったら、ファイルには圧縮前の履歴と書きかけの compact が
            残る。連番を使い回すと、次に書けた item が消えた発言の番号を名乗って
            その発言を永久に隠す。appendConversationItem と同じく保存を諦める。 */
        if (! written)
        {
            persistenceGaveUp = true;
            warning = notSavedNotice (projectRoot);
        }
    }

    conversation = compacted;
    conversationCharacters.store (AiSessionStore::countCharacters (conversation));

    const auto notice = "Compacted the conversation: " + juce::String (itemsBefore) + " items to "
                      + juce::String (compacted.size()) + ".";

    /*  差し替えとターンの終了は 1 つのコールバックで済ませる。二つに分けると、
        その間にメッセージスレッドが足したエントリが差し替えで消える。 */
    juce::MessageManager::callAsync ([weakSelf = weak_from_this(),
                                      newEntries = AiSessionStore::rebuildEntries (compacted),
                                      notice,
                                      warning]
    {
        const auto liveSession = weakSelf.lock();

        if (liveSession == nullptr)
            return;

        liveSession->entries = newEntries;
        liveSession->appendEntry (Entry::Kind::tool, notice);

        if (warning.isNotEmpty())
            liveSession->appendEntry (Entry::Kind::error, warning);

        liveSession->finishTurn();
    });
}

void AiSession::appendConversationItem (const juce::var& item)
{
    conversation.add (item);
    conversationCharacters += AiSessionStore::countCharacters (item);

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

    const auto notice = notSavedNotice (projectRoot);

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
