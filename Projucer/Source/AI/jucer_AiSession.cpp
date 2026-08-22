#include "jucer_AiSession.h"
#include "jucer_AgentLoop.h"

#include <utility>

AiSession::AiSession (std::shared_ptr<CodexAuth> authToUse, const juce::File& projectRootToUse)
    : auth (std::move (authToUse)),
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
        loop = std::make_unique<AgentLoop> (shared_from_this(), auth, projectRoot);
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

void AiSession::setApprovalMode (ApprovalMode mode)
{
    approvalMode.store (mode);
    sendChangeMessage();
}

void AiSession::setAutoApprove (bool shouldAutoApprove)
{
    setApprovalMode (shouldAutoApprove ? ApprovalMode::onUnsafe : ApprovalMode::ask);
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
