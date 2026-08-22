#include "jucer_ProjectInstructions.h"

namespace ProjectInstructions
{
    namespace
    {
        /*  Codex の DEFAULT_PROJECT_ROOT_MARKERS（config/src/project_root_markers.rs）。
            既定は .git ただ一つ。 */
        const char* const projectRootMarkers[] = { ".git" };

        /*  Codex の candidate_filenames（core/src/agents_md.rs）。
            AGENTS.override.md を先に見て、無ければ AGENTS.md。 */
        const char* const candidateFilenames[] = { "AGENTS.override.md", "AGENTS.md" };

        /*  無限に登らないための保険。壊れたシンボリックリンクなどで親子が
            循環した場合にここで止まる。 */
        constexpr int maximumAncestorHops = 64;

        bool hasMarker (const juce::File& directory)
        {
            for (const auto* marker : projectRootMarkers)
            {
                const auto candidate = directory.getChildFile (marker);

                // .git はディレクトリのことも、worktree ではファイルのこともある。
                if (candidate.exists())
                    return true;
            }

            return false;
        }

        /** そのディレクトリで最初に見つかった指示ファイル。無ければ無効な File。 */
        juce::File findInstructionFile (const juce::File& directory)
        {
            for (const auto* name : candidateFilenames)
            {
                const auto candidate = directory.getChildFile (name);

                if (candidate.existsAsFile())
                    return candidate;
            }

            return {};
        }
    }

    juce::File findProjectRoot (const juce::File& workingDirectory)
    {
        auto directory = workingDirectory;

        for (int hop = 0; hop < maximumAncestorHops && directory.isDirectory(); ++hop)
        {
            if (hasMarker (directory))
                return directory;

            const auto parent = directory.getParentDirectory();

            // ルートに達すると getParentDirectory は自分自身を返す。
            if (parent == directory)
                break;

            directory = parent;
        }

        return {};
    }

    juce::Array<Entry> collect (const juce::File& workingDirectory)
    {
        juce::Array<Entry> entries;

        if (! workingDirectory.isDirectory())
            return entries;

        const auto root = findProjectRoot (workingDirectory);

        // 作業ディレクトリからルートまでの各階層。まず下から積んで、あとで反転する。
        juce::Array<juce::File> directories;
        auto cursor = workingDirectory;

        for (int hop = 0; hop < maximumAncestorHops; ++hop)
        {
            directories.add (cursor);

            // ルートが見つからなかった場合は作業ディレクトリだけを見る。
            if (! root.isDirectory() || cursor == root)
                break;

            const auto parent = cursor.getParentDirectory();

            if (parent == cursor)
                break;

            cursor = parent;
        }

        // ルート側が先。下位のディレクトリの指示ほど後に来る。
        for (int i = directories.size() - 1; i >= 0; --i)
        {
            const auto file = findInstructionFile (directories.getReference (i));

            if (file.existsAsFile())
                entries.add ({ file, file.loadFileAsString() });
        }

        return entries;
    }

    juce::String buildText (const juce::File& workingDirectory)
    {
        juce::String text;

        for (const auto& entry : collect (workingDirectory))
        {
            if (entry.contents.trim().isEmpty())
                continue;

            if (text.isNotEmpty())
                text << "\n\n";

            // どのファイル由来かを示す。競合したときにモデルが判断できる。
            text << "# " << entry.file.getFullPathName() << "\n\n" << entry.contents.trim();
        }

        return text;
    }
}
