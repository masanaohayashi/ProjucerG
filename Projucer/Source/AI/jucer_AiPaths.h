#pragma once

#include <filesystem>
#include <optional>
#include <string>

/*  Keep paths accessed by AI tools within the project root.

    This is a trust boundary. Never pass a path returned by the model directly
    to file operations. Block both .. traversal and escapes through symlinks
    located inside the root.

    @param root          Project root. Resolve symlinks before comparing it.
    @param relativePath Project-relative path specified by the model.
    @returns             Resolved path within the root, or nullopt if it escapes,
                         is absolute, or cannot be resolved.
*/
std::optional<std::filesystem::path> resolveInsideRoot (const std::filesystem::path& root,
                                                        const std::string& relativePath);
