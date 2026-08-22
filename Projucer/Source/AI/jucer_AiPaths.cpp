#include "jucer_AiPaths.h"

namespace fs = std::filesystem;

namespace
{
    bool isMissingPathError (const std::error_code& error)
    {
        return error == std::errc::no_such_file_or_directory;
    }
}

std::optional<fs::path> resolveInsideRoot (const fs::path& root, const std::string& relativePath)
{
    // Reject an empty string.
    if (relativePath.empty())
        return {};

    // Reject embedded NUL characters.
    if (relativePath.find('\0') != std::string::npos)
        return {};

    const fs::path candidate (relativePath);

    // Reject absolute paths; only project-relative paths are supported.
    if (candidate.is_absolute() || candidate.has_root_name())
        return {};

    std::error_code ec;

    // The root itself may be a symlink, so resolve it to its real path.
    const auto canonicalRoot = fs::weakly_canonical (root, ec);

    if (ec)
        return {};

    // The project root itself must be an existing directory.
    // weakly_canonical can return a non-existent root, so check it explicitly here.
    const auto rootStatus = fs::status (canonicalRoot, ec);

    if (ec || ! fs::is_directory (rootStatus))
        return {};

    // weakly_canonical resolves symlinks in existing components and removes .. .
    // It also works for files that do not yet exist, allowing new files to be created.
    const auto resolved = fs::weakly_canonical (canonicalRoot / candidate, ec);

    if (ec)
        return {};

    // Walk each path component and inspect symlink targets as well.
    // weakly_canonical cannot resolve symlinks whose targets do not exist,
    // so they must be checked explicitly.
    auto current = canonicalRoot;
    for (const auto& component : candidate)
    {
        current = current / component;

        // Check whether this component is a symlink.
        // symlink_status also handles symlinks whose targets do not exist.
        std::error_code symlink_ec;
        auto status = fs::symlink_status (current, symlink_ec);

        if (symlink_ec)
        {
            // Allow only a missing path intended for a new file. The remaining
            // components do not exist either, so they cannot be inspected and
            // the weakly_canonical result must be used.
            if (isMissingPathError (symlink_ec))
                break;

            // Do not treat an unchecked state, such as permission denial, as success.
            return {};
        }

        if (! fs::status_known (status))
            return {};

        if (fs::is_symlink (status))
        {
            // weakly_canonical may leave a dangling symlink unresolved, so check
            // the target status before proceeding to the final comparison.
            std::error_code target_ec;
            fs::status (current, target_ec);

            if (target_ec)
                return {};
        }

        // An internal symlink is judged by whether its final real path is inside the root.
    }

    // A string-prefix comparison cannot distinguish project from project_evil.
    // Compare path components instead.
    auto rootPart = canonicalRoot.begin();
    auto resolvedPart = resolved.begin();

    for (; rootPart != canonicalRoot.end(); ++rootPart, ++resolvedPart)
    {
        if (resolvedPart == resolved.end() || *resolvedPart != *rootPart)
            return {};
    }

    return resolved;
}
