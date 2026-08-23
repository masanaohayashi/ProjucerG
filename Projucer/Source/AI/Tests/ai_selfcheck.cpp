/*
    Self-checks for the AI harness's pure logic.
    It does not depend on JUCE and runs standalone via scripts/run_ai_selfcheck.sh.
*/

#include "jucer_SseParser.h"
#include "jucer_AiPaths.h"
#include "jucer_AiSessionStore.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

static void check (bool condition, const char* what)
{
    if (condition)
        return;

    std::printf ("  FAIL: %s\n", what);
    ++failures;
}

//==============================================================================
static void testSseParser()
{
    std::printf ("SseParser\n");

    {
        /*  One complete event in a single feed. */
        SseParser p;
        auto events = p.feed ("data: {\"a\":1}\n\n");
        check (events.size() == 1, "Returns one event");
        check (events.size() == 1 && events[0] == "{\"a\":1}", "Extracts the payload");
    }

    {
        /*  The event is split across feeds. */
        SseParser p;
        auto first = p.feed ("data: {\"a\"");
        check (first.empty(), "Does not return an incomplete event");

        auto second = p.feed (":1}\n\n");
        check (second.size() == 1, "Returns the event when the remainder arrives");
        check (second.size() == 1 && second[0] == "{\"a\":1}", "Joins split input correctly");
    }

    {
        /*  CRLF line endings. */
        SseParser p;
        auto events = p.feed ("data: hello\r\n\r\n");
        check (events.size() == 1 && events[0] == "hello", "Handles CRLF line endings");
    }

    {
        /*  Ignore lines other than data:. */
        SseParser p;
        auto events = p.feed (": keep-alive\nevent: foo\ndata: x\n\n");
        check (events.size() == 1 && events[0] == "x", "Ignores comments and event: lines");
    }

    {
        /*  Multiple events in a single feed. */
        SseParser p;
        auto events = p.feed ("data: a\n\ndata: b\n\n");
        check (events.size() == 2, "Returns multiple events");
        check (events.size() == 2 && events[0] == "a" && events[1] == "b", "Preserves event order");
    }

    {
        /*  Multiple data: lines in one event are joined with \n as specified by SSE. */
        SseParser p;
        auto events = p.feed ("data: a\ndata: b\n\n");
        check (events.size() == 1 && events[0] == "a\nb", "Joins multiple data: lines");
    }

    {
        /*  An empty data: line. */
        SseParser p;
        auto events = p.feed ("data:\n\n");
        check (events.size() == 1 && events[0].empty(), "Treats an empty payload as one event");
    }

    {
        /*  Pass through the terminator and leave its interpretation to the caller. */
        SseParser p;
        auto events = p.feed ("data: [DONE]\n\n");
        check (events.size() == 1 && events[0] == "[DONE]", "Passes [DONE] through unchanged");
    }
}

//==============================================================================
/*  Create a small project in a temporary directory and verify that escape paths are blocked. */
static void testAiPaths()
{
    std::printf ("AiPaths\n");

    const auto base = fs::temp_directory_path() / "projucer_ai_selfcheck";
    fs::remove_all (base);
    fs::create_directories (base / "project" / "Source");
    fs::create_directories (base / "outside");

    const auto root = base / "project";

    std::error_code setupEc;
    fs::create_symlink (root, base / "project_alias", setupEc);
    check (! setupEc, "Can create the test root symlink");

    {
        std::ofstream f (root / "Source" / "Main.cpp");
        f << "int main() { return 0; }\n";
    }

    {
        std::ofstream f (base / "outside" / "secret.txt");
        f << "secret\n";
    }

    // A valid path is allowed.
    {
        const auto resolved = resolveInsideRoot (base / "project_alias", "Source/Main.cpp");
        check (resolved.has_value(), "Allows a path inside the project");
    }

    // A file that does not exist yet is allowed when it is under the root (for creation).
    {
        const auto resolved = resolveInsideRoot (root, "Source/New.cpp");
        check (resolved.has_value(), "Allows a non-existent path under the root");
    }

    // .. is allowed when the path returns inside the root.
    {
        const auto resolved = resolveInsideRoot (root, "Source/../Source/Main.cpp");
        check (resolved.has_value(), "Allows .. when the path remains under the root");
    }

    // If a symlink inside the root points inside the root, judge safety by the resolved real path.
    {
        std::error_code ec;
        fs::create_symlink (root / "Source", root / "SourceLink", ec);
        check (! ec, "Can create the test internal symlink");

        const auto resolved = resolveInsideRoot (root, "SourceLink/Main.cpp");
        check (resolved.has_value(), "Allows a symlink pointing inside the root");
    }

    // Reject paths that escape the root through .. .
    {
        const auto resolved = resolveInsideRoot (root, "../outside/secret.txt");
        check (! resolved.has_value(), "Rejects escaping through ..");
    }

    // Reject absolute paths.
    {
        const auto resolved = resolveInsideRoot (root, "/etc/passwd");
        check (! resolved.has_value(), "Rejects absolute paths");
    }

    // Reject escapes through a symlink located inside the root.
    {
        std::error_code ec;
        fs::create_symlink (base / "outside", root / "escape", ec);
        check (! ec, "Can create the test escape symlink");

        const auto resolved = resolveInsideRoot (root, "escape/secret.txt");
        check (! resolved.has_value(), "Rejects escaping through a symlink");
    }

    // Security-sensitive: also reject escapes through a symlink whose target does not exist.
    // Even when weakly_canonical cannot resolve the target,
    // the explicit symlink check must reject it.
    {
        std::error_code ec;
        fs::create_symlink (base / "outside" / "pwned.txt", root / "dangle", ec);
        check (! ec, "Can create a symlink to a non-existent target");

        const auto resolved = resolveInsideRoot (root, "dangle");
        check (! resolved.has_value(), "Rejects a symlink to a non-existent target");
    }

    // Allow a new file below an existing directory.
    {
        const auto resolved = resolveInsideRoot (root, "Source/New/Nested.cpp");
        check (resolved.has_value(), "Allows a non-existent destination for a new file");
    }

    // Reject embedded NUL characters.
    {
        std::string pathWithNul = std::string ("Source/Main.cpp") + '\0' + "extra";
        const auto resolved = resolveInsideRoot (root, pathWithNul);
        check (! resolved.has_value(), "Rejects embedded NUL characters");
    }

    // Reject a sibling directory that only shares the root's prefix.
    // If root is /tmp/x/project, /tmp/x/project_evil must not be allowed.
    {
        fs::create_directories (base / "project_evil");

        const auto resolved = resolveInsideRoot (root, "../project_evil");
        check (! resolved.has_value(), "Rejects a sibling that only shares the prefix");
    }

    fs::remove_all (base);
}

//==============================================================================
/*  会話を書いて読み戻す。壊れた行と知らない type が混ざっても、
    読めた分だけ返って落ちないことまで見る。 */
static void testAiSessionStore()
{
    std::printf ("AiSessionStore\n");

    const juce::File root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("projucer_ai_session_store_check"));
    root.deleteRecursively();
    root.createDirectory();

    const auto file = AiSessionStore::createSessionFile (root, "gpt-5.6-luna");
    check (file.existsAsFile(), "Creates the session file with its meta line");

    const auto makeMessage = [] (const char* role, const char* type, const juce::String& text)
    {
        auto* part = new juce::DynamicObject();
        part->setProperty ("type", type);
        part->setProperty ("text", text);

        auto* message = new juce::DynamicObject();
        message->setProperty ("type", "message");
        message->setProperty ("role", role);
        message->setProperty ("content", juce::Array<juce::var> { juce::var (part) });
        return juce::var (message);
    };

    auto* call = new juce::DynamicObject();
    call->setProperty ("type", "function_call");
    call->setProperty ("call_id", "c1");
    call->setProperty ("name", "read_file");
    call->setProperty ("arguments", "{\"path\":\"Source/Main.cpp\"}");

    auto* output = new juce::DynamicObject();
    output->setProperty ("type", "function_call_output");
    output->setProperty ("call_id", "c1");
    output->setProperty ("output", "int main() {}");

    AiSessionStore::appendItem (file, 1, makeMessage ("user", "input_text", "Explain Main.cpp"));
    AiSessionStore::appendItem (file, 2, juce::var (call));
    AiSessionStore::appendItem (file, 3, juce::var (output));
    AiSessionStore::appendItem (file, 4, makeMessage ("assistant", "output_text", "It starts the app."));

    // 知らない type と、改行の前で切れた行。どちらも黙って飛ばされるはず。
    file.appendText ("{\"ts\":\"now\",\"n\":5,\"type\":\"turn_context\",\"payload\":{\"cwd\":\"/x\"}}\n");
    file.appendText ("{\"ts\":\"now\",\"n\":6,\"type\":\"ite");

    const auto restored = AiSessionStore::restore (file);

    check (restored.conversation.size() == 4, "Restores every item and nothing else");
    check (restored.nextOrdinal == 6, "Continues the ordinals after the last readable record");

    check (restored.conversation[0]["role"].toString() == "user", "Keeps the item order");
    check (restored.conversation[1]["name"].toString() == "read_file", "Keeps the call payload verbatim");
    check (restored.conversation[3]["content"][0]["text"].toString() == "It starts the app.",
           "Keeps the assistant text verbatim");

    check (restored.entries.size() == 3, "Rebuilds one entry per message and per tool call");
    check (restored.entries.size() == 3
            && restored.entries[0].kind == AiSession::Entry::Kind::user
            && restored.entries[0].text == "Explain Main.cpp", "Rebuilds the user entry");
    check (restored.entries.size() == 3
            && restored.entries[1].kind == AiSession::Entry::Kind::tool
            && restored.entries[1].text.startsWith ("read_file"), "Rebuilds the tool entry");
    check (restored.entries.size() == 3
            && restored.entries[2].kind == AiSession::Entry::Kind::assistant
            && restored.entries[2].text == "It starts the app.", "Rebuilds the assistant entry");

    /*  切れた行のあとに足したレコードが、その行に飲み込まれないこと。
        繋がると 1 行として捨てられ、復元後の最初の発言が黙って消える。 */
    check (AiSessionStore::appendItem (file, 6, makeMessage ("user", "input_text", "And Second.cpp")),
           "Appends after a torn line");

    const auto afterTorn = AiSessionStore::restore (file);
    check (afterTorn.conversation.size() == 5, "Keeps what was appended after a torn line");
    check (afterTorn.conversation.size() == 5
            && afterTorn.conversation[4]["content"][0]["text"].toString() == "And Second.cpp",
           "Reads the record that followed the torn line");

    const auto listed = AiSessionStore::listRecent (root, 10);
    check (listed.size() == 1, "Lists the saved conversation");
    check (listed.size() == 1 && listed[0].title == "Explain Main.cpp", "Titles it with the first user message");

    /*  要約が replaces_through までを置き換える。ここでは n=2 の function_call が
        消えるので、n=3 に残った出力も親を失って一緒に落ちる。 */
    file.appendText ("{\"ts\":\"now\",\"n\":7,\"type\":\"compact\","
                     "\"payload\":{\"summary\":\"Earlier: Main.cpp\",\"replaces_through\":2}}\n");

    const auto compacted = AiSessionStore::restore (file);
    check (compacted.conversation.size() == 3, "Drops the items the summary replaces");
    check (compacted.conversation.size() == 3
            && compacted.conversation[0]["content"][0]["text"].toString().endsWith ("Earlier: Main.cpp"),
           "Puts the summary first");
    check (compacted.conversation.size() == 3
            && AiSessionStore::isSummaryMessage (compacted.conversation[0]),
           "Marks the restored summary as a summary");
    check (! compacted.entries.isEmpty()
            && compacted.entries[0].kind == AiSession::Entry::Kind::tool,
           "Shows the summary as a collapsible line, not as the user speaking");

    /*  ターンの途中で終わったファイルは function_call で終わる。出力の無い呼び出しを
        そのまま送ると Responses API が 400 を返すので、読むときに落とす。 */
    {
        const auto interrupted = AiSessionStore::createSessionFile (root, "gpt-5.6-luna");
        AiSessionStore::appendItem (interrupted, 1, makeMessage ("user", "input_text", "Read it"));

        auto* dangling = new juce::DynamicObject();
        dangling->setProperty ("type", "function_call");
        dangling->setProperty ("call_id", "c9");
        dangling->setProperty ("name", "read_file");
        dangling->setProperty ("arguments", "{}");
        AiSessionStore::appendItem (interrupted, 2, juce::var (dangling));

        const auto reopened = AiSessionStore::restore (interrupted);
        check (reopened.conversation.size() == 1, "Drops a function_call with no output");
        check (reopened.entries.size() == 1, "Drops the entry for that call too");
    }

    // 中身がまったく読めなくても落ちない。
    const auto broken = root.getChildFile (".projucer").getChildFile ("ai-sessions").getChildFile ("broken.jsonl");
    broken.replaceWithText ("not json at all\n{\n");
    check (AiSessionStore::restore (broken).conversation.isEmpty(), "Returns nothing for an unreadable file");
    check (AiSessionStore::listRecent (root, 10).size() == 3, "Still lists a file it cannot parse");

    root.deleteRecursively();
}

//==============================================================================
/*  /compact が守るべき不変条件: 圧縮したあとのメモリ上の会話と、ファイルから
    読み戻した会話が一致すること。ここではセッションと同じ順で store を叩く。 */
static void testCompaction()
{
    std::printf ("\nCompaction\n");

    const juce::File root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("projucer_ai_compaction_check"));
    root.deleteRecursively();
    root.createDirectory();

    const auto userMessage = [] (const juce::String& text)
    {
        auto* part = new juce::DynamicObject();
        part->setProperty ("type", "input_text");
        part->setProperty ("text", text);

        auto* message = new juce::DynamicObject();
        message->setProperty ("type", "message");
        message->setProperty ("role", "user");
        message->setProperty ("content", juce::Array<juce::var> { juce::var (part) });
        return juce::var (message);
    };

    /*  添付つきの発言。本文は短いが、画像は base64 の data URL として本体に載る。 */
    const auto userMessageWithImage = [] (const juce::String& text, int imageCharacters)
    {
        auto* textPart = new juce::DynamicObject();
        textPart->setProperty ("type", "input_text");
        textPart->setProperty ("text", text);

        auto* imagePart = new juce::DynamicObject();
        imagePart->setProperty ("type", "input_image");
        imagePart->setProperty ("image_url", "data:image/png;base64,"
                                                 + juce::String::repeatedString ("A", imageCharacters));

        auto* message = new juce::DynamicObject();
        message->setProperty ("type", "message");
        message->setProperty ("role", "user");
        message->setProperty ("content", juce::Array<juce::var> { juce::var (textPart), juce::var (imagePart) });
        return juce::var (message);
    };

    const auto assistantMessage = [] (const juce::String& text)
    {
        auto* part = new juce::DynamicObject();
        part->setProperty ("type", "output_text");
        part->setProperty ("text", text);

        auto* message = new juce::DynamicObject();
        message->setProperty ("type", "message");
        message->setProperty ("role", "assistant");
        message->setProperty ("content", juce::Array<juce::var> { juce::var (part) });
        return juce::var (message);
    };

    const auto asJson = [] (const juce::Array<juce::var>& items)
    {
        juce::Array<juce::var> copy (items);
        return juce::JSON::toString (juce::var (copy), true);
    };

    const auto describe = [] (const juce::Array<AiSession::Entry>& entries)
    {
        juce::String text;

        for (const auto& entry : entries)
            text << static_cast<int> (entry.kind) << ":" << entry.text << "\n";

        return text;
    };

    //--------------------------------------------------------------------------
    // 桁の切り替え。境目を間違えると "100.0K" のような表記が 1 度だけ出る。
    {
        check (AiSessionStore::formatTokenCount (0) == "0", "Shows zero as a plain number");
        check (AiSessionStore::formatTokenCount (840) == "840", "Shows small counts as plain numbers");
        check (AiSessionStore::formatTokenCount (999) == "999", "Keeps the last plain number plain");
        check (AiSessionStore::formatTokenCount (1000) == "1K", "Switches to K at a thousand");
        check (AiSessionStore::formatTokenCount (1499) == "1K", "Rounds K down when it should");
        check (AiSessionStore::formatTokenCount (1500) == "2K", "Rounds K up when it should");
        check (AiSessionStore::formatTokenCount (12345) == "12K", "Shows no decimals for K");
        check (AiSessionStore::formatTokenCount (258400) == "258K", "Shows a full context window in K");
        check (AiSessionStore::formatTokenCount (1000000) == "1M", "Drops the trailing .0 at a million");
        check (AiSessionStore::formatTokenCount (1200000) == "1.2M", "Shows one decimal for M");
        check (AiSessionStore::formatTokenCount (12000000) == "12M", "Keeps large M counts whole");
    }

    //--------------------------------------------------------------------------
    // 選択そのもの。予算、切り詰め、時系列の復元。
    {
        juce::Array<juce::var> conversation;
        conversation.add (userMessage ("first"));
        conversation.add (assistantMessage ("ignored"));
        conversation.add (userMessage ("second"));
        conversation.add (userMessage ("third"));

        const auto all = AiSessionStore::selectRecentUserMessages (conversation, 1000);
        check (all.size() == 3, "Keeps every user message that fits");
        check (all.size() == 3
                && all[0]["content"][0]["text"].toString() == "first"
                && all[2]["content"][0]["text"].toString() == "third",
               "Puts the kept messages back in time order");

        /*  予算は送っている JSON の量で測る。ちょうど 2 件分だけ与えて、
            3 件目に手が届かないことを見る。 */
        const auto roomForTwo = AiSessionStore::countCharacters (conversation[2])
                              + AiSessionStore::countCharacters (conversation[3]);

        const auto budgeted = AiSessionStore::selectRecentUserMessages (conversation, roomForTwo);
        check (budgeted.size() == 2, "Stops once the budget is used up");
        check (budgeted.size() == 2 && budgeted[0]["content"][0]["text"].toString() == "second",
               "Keeps the most recent messages, not the oldest");

        const auto truncated = AiSessionStore::selectRecentUserMessages (conversation, 3);
        check (truncated.size() == 1 && truncated[0]["content"][0]["text"].toString() == "thi",
               "Truncates the one message that straddles the budget");

        check (AiSessionStore::selectRecentUserMessages (conversation, 0).isEmpty(),
               "Keeps nothing when there is no budget");
    }

    //--------------------------------------------------------------------------
    /*  添付だらけの会話。base64 は content の text には出てこないので、本文の長さで
        予算を測ると 0 文字扱いになり、画像ごと丸ごと残って会話がむしろ増える。
        /compact が要るのはまさにこの状況なので、必ず縮むことを見る。 */
    {
        juce::Array<juce::var> conversation;

        for (int i = 0; i < 5; ++i)
            conversation.add (userMessageWithImage ("Look at this screenshot", 200000));

        const auto before = AiSessionStore::countCharacters (conversation);

        juce::Array<juce::var> compacted;
        compacted.add (AiSessionStore::makeSummaryMessage ("The user shared screenshots."));
        compacted.addArray (AiSessionStore::selectRecentUserMessages (conversation));

        const auto after = AiSessionStore::countCharacters (compacted);
        check (after < before, "Compacting a conversation full of images makes it smaller");
        check (after < 100000, "Drops the base64 attachments the budget cannot afford");
    }

    //--------------------------------------------------------------------------
    /*  前の要約は本物のユーザー発言ではない。拾ってしまうと圧縮のたびに
        前置きごと積み上がって、要約が要約を要約する。 */
    {
        juce::Array<juce::var> conversation;
        conversation.add (AiSessionStore::makeSummaryMessage ("The earlier summary."));
        conversation.add (userMessage ("Now rename it"));

        const auto keptAfterSummary = AiSessionStore::selectRecentUserMessages (conversation);
        check (keptAfterSummary.size() == 1, "Does not carry the previous summary forward");
        check (keptAfterSummary.size() == 1
                && keptAfterSummary[0]["content"][0]["text"].toString() == "Now rename it",
               "Keeps the real user message instead");
    }

    //--------------------------------------------------------------------------
    /*  圧縮の往復。AiSession::applyCompaction と同じ順でファイルへ書き、
        読み戻したものがメモリ上のものと一致することを見る。 */
    const auto file = AiSessionStore::createSessionFile (root, "gpt-5.6-luna");

    auto* call = new juce::DynamicObject();
    call->setProperty ("type", "function_call");
    call->setProperty ("call_id", "c1");
    call->setProperty ("name", "read_file");
    call->setProperty ("arguments", "{}");

    auto* output = new juce::DynamicObject();
    output->setProperty ("type", "function_call_output");
    output->setProperty ("call_id", "c1");
    output->setProperty ("output", "int main() {}");

    juce::Array<juce::var> conversation;
    conversation.add (userMessage ("Explain Main.cpp"));
    conversation.add (juce::var (call));
    conversation.add (juce::var (output));
    conversation.add (assistantMessage ("It starts the app."));
    conversation.add (userMessage ("Now rename it"));

    int nextOrdinal = 1;

    for (const auto& item : conversation)
        check (AiSessionStore::appendItem (file, nextOrdinal++, item), "Appends the conversation");

    const juce::String summaryText ("Preamble\nThe user asked about Main.cpp.");
    const auto kept = AiSessionStore::selectRecentUserMessages (conversation);

    juce::Array<juce::var> compacted;
    compacted.add (AiSessionStore::makeSummaryMessage (summaryText));
    compacted.addArray (kept);

    check (AiSessionStore::appendCompaction (file, nextOrdinal, summaryText, nextOrdinal - 1),
           "Writes the compact record");
    ++nextOrdinal;

    for (const auto& item : kept)
        check (AiSessionStore::appendItem (file, nextOrdinal++, item), "Rewrites the kept user messages");

    const auto reopened = AiSessionStore::restore (file);

    check (asJson (reopened.conversation) == asJson (compacted),
           "Restores exactly the compacted conversation that is held in memory");
    check (reopened.nextOrdinal == nextOrdinal, "Continues the ordinals after the compaction");
    check (describe (reopened.entries) == describe (AiSessionStore::rebuildEntries (compacted)),
           "Rebuilds the same entries from memory and from the file");

    //--------------------------------------------------------------------------
    // 2 回目の圧縮。最後の compact 行だけが効く。
    const juce::String secondSummary ("Preamble\nStill about Main.cpp.");
    const auto keptAgain = AiSessionStore::selectRecentUserMessages (reopened.conversation);

    juce::Array<juce::var> compactedAgain;
    compactedAgain.add (AiSessionStore::makeSummaryMessage (secondSummary));
    compactedAgain.addArray (keptAgain);

    check (AiSessionStore::appendCompaction (file, nextOrdinal, secondSummary, nextOrdinal - 1),
           "Writes the second compact record");
    ++nextOrdinal;

    for (const auto& item : keptAgain)
        check (AiSessionStore::appendItem (file, nextOrdinal++, item), "Rewrites after the second compaction");

    const auto twice = AiSessionStore::restore (file);
    check (asJson (twice.conversation) == asJson (compactedAgain),
           "Only the last compact record counts");
    check (twice.conversation.size() > 0
            && twice.conversation[0]["content"][0]["text"].toString().endsWith (secondSummary),
           "Uses the newest summary");

    /*  2 回圧縮しても要約は 1 件だけ。前の要約を拾うと、前置き 300 字ごと
        積み上がって要約が要約を要約しはじめる。 */
    auto summariesLeft = 0;

    for (const auto& item : twice.conversation)
        if (AiSessionStore::isSummaryMessage (item))
            ++summariesLeft;

    check (summariesLeft == 1, "Leaves exactly one summary after compacting twice");
    check (asJson (twice.conversation).indexOf (summaryText) < 0,
           "Drops the summary the second compaction replaces");

    //--------------------------------------------------------------------------
    /*  compact の境目が呼び出しと出力の間に落ちると、親の無い function_call_output
        だけが残る。そのまま送ると 400 になるので読むときに落とす。 */
    {
        const auto orphaned = AiSessionStore::createSessionFile (root, "gpt-5.6-luna");
        AiSessionStore::appendItem (orphaned, 1, userMessage ("Read it"));

        auto* callToDrop = new juce::DynamicObject();
        callToDrop->setProperty ("type", "function_call");
        callToDrop->setProperty ("call_id", "c9");
        callToDrop->setProperty ("name", "read_file");
        callToDrop->setProperty ("arguments", "{}");
        AiSessionStore::appendItem (orphaned, 2, juce::var (callToDrop));

        auto* answer = new juce::DynamicObject();
        answer->setProperty ("type", "function_call_output");
        answer->setProperty ("call_id", "c9");
        answer->setProperty ("output", "int main() {}");
        AiSessionStore::appendItem (orphaned, 3, juce::var (answer));

        // n ≤ 2 を置き換える。呼び出しは消え、出力だけが取り残される。
        AiSessionStore::appendCompaction (orphaned, 4, "Earlier work", 2);

        const auto reopenedOrphan = AiSessionStore::restore (orphaned);
        check (reopenedOrphan.conversation.size() == 1, "Drops a function_call_output with no call");
        check (reopenedOrphan.conversation.size() == 1
                && reopenedOrphan.conversation[0]["content"][0]["text"].toString().endsWith ("Earlier work"),
               "Leaves the summary alone");
    }

    root.deleteRecursively();
}

//==============================================================================
int main()
{
    testSseParser();
    testAiPaths();
    testAiSessionStore();
    testCompaction();

    if (failures > 0)
    {
        std::printf ("\n%d checks failed\n", failures);
        return 1;
    }

    std::printf ("\nAll checks passed\n");
    return 0;
}
