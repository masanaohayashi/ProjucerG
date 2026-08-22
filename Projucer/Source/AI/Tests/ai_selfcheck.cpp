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

    /*  /compact はまだ書き出さないが、読み側だけ先に用意してある。
        要約が replaces_through までを置き換えることを見ておく。 */
    file.appendText ("{\"ts\":\"now\",\"n\":7,\"type\":\"compact\","
                     "\"payload\":{\"summary\":\"Earlier: Main.cpp\",\"replaces_through\":2}}\n");

    const auto compacted = AiSessionStore::restore (file);
    check (compacted.conversation.size() == 4, "Drops the items the summary replaces");
    check (compacted.conversation.size() == 4
            && compacted.conversation[0]["content"][0]["text"].toString() == "Earlier: Main.cpp",
           "Puts the summary first");

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
int main()
{
    testSseParser();
    testAiPaths();
    testAiSessionStore();

    if (failures > 0)
    {
        std::printf ("\n%d checks failed\n", failures);
        return 1;
    }

    std::printf ("\nAll checks passed\n");
    return 0;
}
