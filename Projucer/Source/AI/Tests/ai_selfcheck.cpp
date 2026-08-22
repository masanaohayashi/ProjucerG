/*
    Self-checks for the AI harness's pure logic.
    It does not depend on JUCE and runs standalone via scripts/run_ai_selfcheck.sh.
*/

#include "jucer_SseParser.h"
#include "jucer_AiPaths.h"

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
int main()
{
    testSseParser();
    testAiPaths();

    if (failures > 0)
    {
        std::printf ("\n%d checks failed\n", failures);
        return 1;
    }

    std::printf ("\nAll checks passed\n");
    return 0;
}
