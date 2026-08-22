#include "jucer_AgentLoop.h"

#include "jucer_AiModels.h"
#include "jucer_ProjectInstructions.h"
#include "../Application/jucer_Headers.h"
#include "../Application/jucer_Application.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace
{
    template <typename Fn>
    void onMessageThread (const std::weak_ptr<AiSession>& session, Fn&& fn)
    {
        juce::MessageManager::callAsync ([session, fn = std::forward<Fn> (fn)]() mutable
        {
            if (auto liveSession = session.lock())
                fn (*liveSession);
        });
    }

    /*  拡張子から画像の MIME を決める。判別できなければ空。 */
    constexpr juce::int64 maximumImageBytes = 8 * 1024 * 1024;

    juce::String imageMimeTypeFor (const juce::File& file)
    {
        const auto extension = file.getFileExtension().toLowerCase();

        if (extension == ".png")                        return "image/png";
        if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
        if (extension == ".gif")                        return "image/gif";
        if (extension == ".webp")                       return "image/webp";

        return {};
    }

    juce::var makeUserMessage (const juce::String& text,
                               const juce::Array<juce::File>& attachments)
    {
        juce::Array<juce::var> content;

        auto* textPart = new juce::DynamicObject();
        textPart->setProperty ("type", "input_text");
        textPart->setProperty ("text", text);
        content.add (juce::var (textPart));

        /*  画像はパスを書いても読めない。data URL にして本文へ載せると、
            モデルが実際に見られる。画像以外は read_file で読ませればよいので
            ここでは何もしない。 */
        for (const auto& file : attachments)
        {
            const auto mimeType = imageMimeTypeFor (file);

            if (mimeType.isEmpty() || ! file.existsAsFile())
                continue;

            // 大きすぎる画像は本文が膨れて通らなくなるので送らない。
            if (file.getSize() > maximumImageBytes)
                continue;

            juce::MemoryBlock bytes;

            if (! file.loadFileAsData (bytes) || bytes.getSize() == 0)
                continue;

            /*  juce::MemoryBlock::toBase64Encoding() は JUCE 独自の文字集合で、
                標準の Base64 ではない。data URL には使えないので juce::Base64 を使う。 */
            juce::MemoryOutputStream encoded;

            if (! juce::Base64::convertToBase64 (encoded, bytes.getData(), bytes.getSize()))
                continue;

            auto* imagePart = new juce::DynamicObject();
            imagePart->setProperty ("type", "input_image");
            imagePart->setProperty ("image_url",
                                    "data:" + mimeType + ";base64," + encoded.toString());
            content.add (juce::var (imagePart));
        }

        auto* message = new juce::DynamicObject();
        message->setProperty ("type", "message");
        message->setProperty ("role", "user");
        message->setProperty ("content", content);
        return juce::var (message);
    }

    juce::var makeAssistantMessage (const juce::String& text)
    {
        auto* content = new juce::DynamicObject();
        content->setProperty ("type", "output_text");
        content->setProperty ("text", text);

        auto* message = new juce::DynamicObject();
        message->setProperty ("type", "message");
        message->setProperty ("role", "assistant");
        message->setProperty ("content", juce::Array<juce::var> { juce::var (content) });
        return juce::var (message);
    }

    juce::var makeFunctionCallOutput (const juce::String& callId, const juce::String& output)
    {
        auto* item = new juce::DynamicObject();
        item->setProperty ("type", "function_call_output");
        item->setProperty ("call_id", callId);
        item->setProperty ("output", output);
        return juce::var (item);
    }

    constexpr const char* systemInstructions =
        "You are a coding assistant built into Projucer for JUCE and C++.\n"
        "Before proposing a change, read the current file with read_file.\n"
        "Use apply_patch for focused edits to existing files and keep old_text unique.\n"
        "Follow the surrounding code style. Keep responses concise and in English.\n"
        "Reading is not restricted to the project root: list_files and read_file accept\n"
        "absolute paths and paths above it, so look there before saying a file is missing.\n"
        "Writing is limited to the project root and always needs the user's approval.\n"
        "You have exec_command. Use it to run shell commands in the project directory,\n"
        "including git clone, builds and tests. Do not say you lack a terminal or git.\n"
        "Prefer apply_patch for editing existing files; use exec_command when a command\n"
        "is the right tool. You have web_search. Use it whenever the answer depends on\n"
        "anything current, such as upstream repositories, releases or documentation.\n"
        "Do not claim you cannot reach the internet.";
}

AgentLoop::AgentLoop (std::shared_ptr<AiSession> sessionToUse,
                     std::shared_ptr<CodexAuth> auth,
                     const juce::File& projectRoot)
    : juce::Thread ("AI Agent Loop"),
      session (std::move (sessionToUse)),
      client (std::move (auth)),
      tools (projectRoot),
      workingDirectory (projectRoot)
{
}

AgentLoop::~AgentLoop()
{
    jassert (! isThreadRunning());
}

void AgentLoop::start (const juce::String& userMessage,
                       const juce::Array<juce::File>& attachments)
{
    if (isThreadRunning())
        return;

    shouldStop.store (false);
    pendingUserMessage = userMessage;
    pendingAttachments = attachments;
    approvalGranted.store (false);
    startThread();
}

void AgentLoop::requestStop()
{
    shouldStop.store (true);
    client.cancelActiveRequest();
    tools.cancel();
    approvalArrived.signal();
}

void AgentLoop::retainUntilStopped (std::unique_ptr<AgentLoop> loopToRetain)
{
    if (loopToRetain == nullptr)
        return;

    reapRetainedLoops();
    retainedLoops().push_back (std::move (loopToRetain));
}

void AgentLoop::reapRetainedLoops()
{
    auto& loops = retainedLoops();
    loops.erase (std::remove_if (loops.begin(), loops.end(), [] (const auto& loop)
    {
        return ! loop->isThreadRunning();
    }), loops.end());
}

std::vector<std::unique_ptr<AgentLoop>>& AgentLoop::retainedLoops()
{
    static auto* loops = new std::vector<std::unique_ptr<AgentLoop>>();
    return *loops;
}

void AgentLoop::provideApproval (bool approved)
{
    approvalGranted.store (approved);
    approvalArrived.signal();
}

juce::var AgentLoop::buildRequestBody() const
{
    auto* body = new juce::DynamicObject();
    body->setProperty ("model", AiModels::getSelectedModel());

    // Codex と同じく reasoning.effort で推論の強さを伝える。
    auto* reasoning = new juce::DynamicObject();
    reasoning->setProperty ("effort", AiModels::getSelectedEffort());
    body->setProperty ("reasoning", juce::var (reasoning));

    /*  速度。Codex は service_tier をトップレベルに載せる (core/src/client.rs:175)。
        標準のときは何も送らない。 */
    const auto serviceTier = AiModels::getSelectedSpeedTier();

    if (serviceTier.isNotEmpty())
        body->setProperty ("service_tier", serviceTier);
    /*  Codex と同じく、作業ディレクトリから .git のある祖先まで登って
        AGENTS.md を集め、内蔵の指示のあとに重ねる。ルート側が先、下位の
        ディレクトリの指示ほど後に来るので、競合したら下位が後勝ちになる。
        登るのは指示の収集だけで、ツールの作業範囲は広がらない。 */
    juce::String instructions (systemInstructions);

    const auto projectInstructions = ProjectInstructions::buildText (workingDirectory);

    if (projectInstructions.isNotEmpty())
        instructions << "\n\n"
                     << "The following instructions come from the project's AGENTS.md files.\n"
                     << "Follow them. Where they conflict with the guidance above, they win.\n\n"
                     << projectInstructions;

    body->setProperty ("instructions", instructions);
    body->setProperty ("input", conversation);
    body->setProperty ("tools", AiTools::getToolSchemas());
    body->setProperty ("stream", true);
    body->setProperty ("store", false);
    return juce::var (body);
}

bool AgentLoop::waitForApproval()
{
    while (! shouldStop.load())
    {
        if (approvalArrived.wait (200))
            return approvalGranted.load() && ! shouldStop.load();
    }

    return false;
}

void AgentLoop::run()
{
    conversation.add (makeUserMessage (pendingUserMessage, pendingAttachments));

    for (int iteration = 0; iteration < maxIterations && ! shouldStop.load(); ++iteration)
    {
        struct PendingCall
        {
            juce::String callId;
            juce::String itemId;
            juce::String name;
            juce::String argumentsJson;
        };

        juce::Array<PendingCall> calls;
        juce::String assistantText;
        juce::String error;

        const auto ok = client.streamResponse (buildRequestBody(), shouldStop,
            [&] (const juce::var& event)
            {
                const auto type = event.getProperty ("type", {}).toString();

                if (type == "response.output_text.delta")
                {
                    const auto delta = event.getProperty ("delta", {}).toString();
                    assistantText << delta;
                    onMessageThread (session, [delta] (AiSession& liveSession)
                    {
                        liveSession.appendToLastAssistantEntry (delta);
                    });
                    return;
                }

                if (type == "response.function_call_arguments.delta"
                    || type == "response.function_call_arguments.done")
                {
                    const auto callId = event.getProperty ("call_id", {}).toString();
                    const auto itemId = event.getProperty ("item_id", {}).toString();
                    auto index = -1;
                    for (int i = 0; i < calls.size(); ++i)
                        if ((! callId.isEmpty() && calls.getReference (i).callId == callId)
                            || (! itemId.isEmpty() && calls.getReference (i).itemId == itemId))
                            index = i;

                    if (index < 0)
                    {
                        PendingCall call;
                        call.callId = callId;
                        call.itemId = itemId;
                        call.name = event.getProperty ("name", {}).toString();
                        calls.add (call);
                        index = calls.size() - 1;
                    }

                    auto& call = calls.getReference (index);
                    if (call.callId.isEmpty())
                        call.callId = callId;
                    if (type.endsWith (".done"))
                    {
                        const auto completeArguments = event.getProperty ("arguments", {}).toString();
                        if (! completeArguments.isEmpty())
                            call.argumentsJson = completeArguments;
                    }
                    else
                    {
                        call.argumentsJson << event.getProperty ("delta", {}).toString();
                    }
                    return;
                }

                if (type == "response.output_item.done")
                {
                    const auto item = event.getProperty ("item", {});
                    if (item.getProperty ("type", {}).toString() != "function_call")
                        return;

                    const auto callId = item.getProperty ("call_id", {}).toString();
                    const auto itemId = item.getProperty ("id", {}).toString();
                    auto index = -1;
                    for (int i = 0; i < calls.size(); ++i)
                        if ((! callId.isEmpty() && calls.getReference (i).callId == callId)
                            || (! itemId.isEmpty() && calls.getReference (i).itemId == itemId))
                            index = i;

                    if (index < 0)
                    {
                        PendingCall call;
                        call.callId = callId;
                        call.itemId = itemId;
                        call.name = item.getProperty ("name", {}).toString();
                        call.argumentsJson = item.getProperty ("arguments", {}).toString();
                        calls.add (call);
                    }
                    else
                    {
                        auto& call = calls.getReference (index);
                        if (call.callId.isEmpty())
                            call.callId = callId;
                        if (call.itemId.isEmpty())
                            call.itemId = itemId;
                        if (call.name.isEmpty())
                            call.name = item.getProperty ("name", {}).toString();
                        if (call.argumentsJson.isEmpty())
                            call.argumentsJson = item.getProperty ("arguments", {}).toString();
                    }
                    return;
                }

                if (type == "response.failed" || type == "error")
                {
                    auto message = event.getProperty ("message", {}).toString();
                    if (message.isEmpty())
                        message = event.getProperty ("error", {}).getProperty ("message", {}).toString();
                    if (message.isEmpty())
                        message = "The response failed.";

                    onMessageThread (session, [message] (AiSession& liveSession)
                    {
                        liveSession.appendEntry (AiSession::Entry::Kind::error, message);
                    });
                }
            }, error);

        if (! ok)
        {
            if (! shouldStop.load())
                onMessageThread (session, [error] (AiSession& liveSession)
                {
                    liveSession.appendEntry (AiSession::Entry::Kind::error, error);
                });
            break;
        }

        if (! assistantText.isEmpty())
            conversation.add (makeAssistantMessage (assistantText));

        if (calls.isEmpty())
            break;

        for (const auto& call : calls)
        {
            if (shouldStop.load())
                break;

            auto* item = new juce::DynamicObject();
            item->setProperty ("type", "function_call");
            if (! call.itemId.isEmpty())
                item->setProperty ("id", call.itemId);
            item->setProperty ("call_id", call.callId);
            item->setProperty ("name", call.name);
            item->setProperty ("arguments", call.argumentsJson);
            conversation.add (juce::var (item));

            const auto arguments = juce::JSON::parse (call.argumentsJson);
            juce::String toolOutput;

            if (arguments.isVoid() && ! call.argumentsJson.isEmpty())
            {
                toolOutput = "The tool arguments are not valid JSON.";
                conversation.add (makeFunctionCallOutput (call.callId, toolOutput));
                onMessageThread (session, [toolOutput] (AiSession& liveSession)
                {
                    liveSession.appendEntry (AiSession::Entry::Kind::error, toolOutput);
                });
                continue;
            }

            const auto liveSession = session.lock();
            if (liveSession == nullptr)
            {
                shouldStop.store (true);
                break;
            }

            /*  承認の扱いは 3 段階。onUnsafe では、既存ファイルへの部分変更
                (apply_patch) は確認せず、新規作成や全置換 (write_file) だけ確認する。
                後者は失うものが大きいので、自動では通さない。 */
            const auto mode = liveSession->getApprovalMode();

            const auto needsApproval = AiTools::requiresApproval (call.name)
                                     && (mode == AiSession::ApprovalMode::ask
                                         || (mode == AiSession::ApprovalMode::onUnsafe
                                             && call.name != "apply_patch"));

            if (needsApproval)
            {
                const auto previewResult = tools.preview (call.name, arguments);
                if (! previewResult.ok)
                {
                    toolOutput = previewResult.output;
                    conversation.add (makeFunctionCallOutput (call.callId, toolOutput));
                    onMessageThread (session, [toolOutput] (AiSession& liveSession)
                    {
                        liveSession.appendEntry (AiSession::Entry::Kind::error, toolOutput);
                    });
                    continue;
                }

                AiSession::PendingApproval approval { call.callId, call.name, previewResult.diffPreview };

                /* Reset before publishing the approval. A UI response may arrive immediately. */
                approvalArrived.reset();
                approvalGranted.store (false);
                onMessageThread (session, [approval] (AiSession& liveSession)
                {
                    liveSession.setPendingApproval (approval);
                });

                const auto approved = waitForApproval();
                onMessageThread (session, [] (AiSession& liveSession)
                {
                    liveSession.clearPendingApproval();
                });

                if (! approved)
                {
                    toolOutput = shouldStop.load()
                               ? "The operation was stopped."
                               : "The user rejected this change.";
                    conversation.add (makeFunctionCallOutput (call.callId, toolOutput));
                    if (! shouldStop.load())
                        onMessageThread (session, [name = call.name] (AiSession& liveSession)
                        {
                            liveSession.appendEntry (AiSession::Entry::Kind::tool, name + " was rejected");
                        });
                    continue;
                }
            }

            const auto result = tools.execute (call.name, arguments);
            toolOutput = result.output;
            conversation.add (makeFunctionCallOutput (call.callId, toolOutput));

            // 引数と結果をそのまま残す。UI の折りたたみを開かなくても、
            // どの引数で何に失敗したかがログだけで追える。
            DBG ("[AI] tool " << call.name << (result.ok ? " ok" : " FAILED")
                              << "\n     args: " << call.argumentsJson
                              << "\n     out : " << result.output);

            if (result.ok && AiTools::requiresApproval (call.name))
                juce::MessageManager::callAsync ([]
                {
                    ProjucerApplication::getApp().openDocumentManager.reloadModifiedFiles();
                });

            /*  失敗の理由を見出しに出す。開かないと理由が見えないと、毎回
                ログを追う羽目になる。成功時は引数の要約だけを見出しにする。 */
            onMessageThread (session, [name = call.name,
                                       ok = result.ok,
                                       detail = result.output,
                                       args = call.argumentsJson] (AiSession& liveSession)
            {
                const auto summary = detail.upToFirstOccurrenceOf ("\n", false, false).trim();

                liveSession.appendEntry (AiSession::Entry::Kind::tool,
                                         ok ? name + " completed  " + args
                                            : name + " failed: " + summary + "  " + args);
            });
        }

        if (! shouldStop.load() && iteration == maxIterations - 1)
            onMessageThread (session, [] (AiSession& liveSession)
            {
                liveSession.appendEntry (AiSession::Entry::Kind::error,
                                         "The tool-call iteration limit was reached.");
            });
    }

    onMessageThread (session, [] (AiSession& liveSession)
    {
        liveSession.finishTurn();
    });
}
