#include "jucer_AiTools.h"
#include "jucer_AiPaths.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#if defined (__APPLE__)
 #include <cerrno>
 #include <dirent.h>
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace
{
    juce::var makeStringProperty (const juce::String& description)
    {
        auto* property = new juce::DynamicObject();
        property->setProperty ("type", "string");
        property->setProperty ("description", description);
        return juce::var (property);
    }

    juce::var makeIntegerProperty (const juce::String& description)
    {
        auto* property = new juce::DynamicObject();
        property->setProperty ("type", "integer");
        property->setProperty ("description", description);
        return juce::var (property);
    }

    juce::String quoteForShell (const juce::String& text)
    {
        juce::String quoted ("'");

        for (int i = 0; i < text.length(); ++i)
        {
            const auto c = text[i];

            if (c == '\'')
                quoted << "'\\''";
            else
                quoted << c;
        }

        quoted << "'";
        return quoted;
    }

    juce::var makeTool (const juce::String& name,
                        const juce::String& description,
                        juce::DynamicObject* properties,
                        const juce::StringArray& required)
    {
        auto* parameters = new juce::DynamicObject();
        parameters->setProperty ("type", "object");
        parameters->setProperty ("properties", juce::var (properties));
        parameters->setProperty ("additionalProperties", false);

        juce::Array<juce::var> requiredArray;
        for (const auto& requiredName : required)
            requiredArray.add (requiredName);

        parameters->setProperty ("required", requiredArray);

        auto* tool = new juce::DynamicObject();
        tool->setProperty ("type", "function");
        tool->setProperty ("name", name);
        tool->setProperty ("description", description);
        tool->setProperty ("parameters", juce::var (parameters));
        return juce::var (tool);
    }

    juce::String makeDiff (const juce::String& before, const juce::String& after)
    {
        juce::StringArray beforeLines, afterLines;
        beforeLines.addLines (before);
        afterLines.addLines (after);

        juce::String diff;
        const auto count = juce::jmax (beforeLines.size(), afterLines.size());

        for (int i = 0; i < count; ++i)
        {
            const auto oldLine = i < beforeLines.size() ? beforeLines[i] : juce::String();
            const auto newLine = i < afterLines.size() ? afterLines[i] : juce::String();

            if (oldLine != newLine)
            {
                if (i < beforeLines.size())
                    diff << "- " << oldLine << "\n";
                if (i < afterLines.size())
                    diff << "+ " << newLine << "\n";
            }
        }

        return diff.isEmpty() ? "No changes." : diff;
    }

    bool getLineRange (const juce::var& arguments,
                       int& firstLine,
                       int& lastLine,
                       juce::String& errorOut)
    {
        const auto* object = arguments.getDynamicObject();

        if (object == nullptr)
        {
            errorOut = "Tool arguments must be a JSON object.";
            return false;
        }

        const auto readLine = [&] (const char* name, int& result)
        {
            result = 0;

            if (! object->hasProperty (name))
                return true;

            const auto value = object->getProperty (name);

            if (! value.isInt() && ! value.isInt64())
            {
                errorOut = juce::String (name) + " must be an integer.";
                return false;
            }

            const auto line = static_cast<std::int64_t> (value);

            if (line < 1 || line > std::numeric_limits<int>::max())
            {
                errorOut = juce::String (name) + " must be a positive line number.";
                return false;
            }

            result = static_cast<int> (line);
            return true;
        };

        if (! readLine ("start_line", firstLine) || ! readLine ("end_line", lastLine))
            return false;

        if (firstLine == 0)
            firstLine = 1;

        if (lastLine > 0 && firstLine > lastLine)
        {
            errorOut = "start_line must not be after end_line.";
            return false;
        }

        return true;
    }

    bool extractPatchArguments (const juce::var& arguments,
                                juce::String& oldText,
                                juce::String& newText,
                                juce::String& errorOut)
    {
        const auto* object = arguments.getDynamicObject();

        if (object == nullptr)
        {
            errorOut = "Tool arguments must be a JSON object.";
            return false;
        }

        if (! object->hasProperty ("path")
            || ! object->hasProperty ("old_text")
            || ! object->hasProperty ("new_text")
            || object->getProperties().size() != 3)
        {
            errorOut = "apply_patch requires only path, old_text, and new_text fields.";
            return false;
        }

        const auto oldValue = object->getProperty ("old_text");
        const auto newValue = object->getProperty ("new_text");

        if (! oldValue.isString() || ! newValue.isString())
        {
            errorOut = "old_text and new_text must be strings.";
            return false;
        }

        oldText = oldValue.toString();
        newText = newValue.toString();

        if (oldText.isEmpty())
        {
            errorOut = "old_text must not be empty.";
            return false;
        }

        return true;
    }

    bool getSafeRelativeComponents (const juce::var& arguments,
                                    std::vector<std::string>& components,
                                    juce::String& errorOut)
    {
        const auto* object = arguments.getDynamicObject();
        if (object == nullptr || ! object->hasProperty ("path"))
        {
            errorOut = "The path argument is required.";
            return false;
        }

        const auto pathValue = object->getProperty ("path");
        if (! pathValue.isString() || pathValue.toString().isEmpty())
        {
            errorOut = "The path argument must be a non-empty string.";
            return false;
        }

        const std::filesystem::path candidate (pathValue.toString().toStdString());
        if (candidate.is_absolute() || candidate.has_root_name())
        {
            errorOut = "The path must be project-relative.";
            return false;
        }

        components.clear();
        for (const auto& component : candidate)
        {
            const auto value = component.string();
            if (value.empty() || value == "." || value == "..")
            {
                errorOut = "The path must not contain . or .. components.";
                return false;
            }

            components.push_back (value);
        }

        if (components.empty())
        {
            errorOut = "The path must name a file.";
            return false;
        }

        return true;
    }

#if defined (__APPLE__)
    class ScopedFd
    {
    public:
        explicit ScopedFd (const int valueToUse = -1) : value (valueToUse) {}
        ~ScopedFd() { reset(); }

        ScopedFd (const ScopedFd&) = delete;
        ScopedFd& operator= (const ScopedFd&) = delete;

        ScopedFd (ScopedFd&& other) noexcept : value (other.value) { other.value = -1; }

        ScopedFd& operator= (ScopedFd&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                value = other.value;
                other.value = -1;
            }

            return *this;
        }

        int get() const noexcept { return value; }
        int release() noexcept { const auto released = value; value = -1; return released; }
        void reset (const int newValue = -1) noexcept
        {
            if (value >= 0)
                close (value);
            value = newValue;
        }

    private:
        int value = -1;
    };

    bool readDescriptor (const int descriptor, std::string& contents)
    {
        contents.clear();
        char buffer[8192];

        for (;;)
        {
            const auto bytesRead = read (descriptor, buffer, sizeof (buffer));
            if (bytesRead == 0)
                return true;
            if (bytesRead < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }

            contents.append (buffer, static_cast<std::size_t> (bytesRead));
        }
    }

    bool readProjectFileWithoutFollowingSymlinks (const juce::File& projectRoot,
                                                  const juce::var& arguments,
                                                  juce::String& contents,
                                                  std::int64_t& size,
                                                  juce::String& errorOut)
    {
        std::vector<std::string> components;
        if (! getSafeRelativeComponents (arguments, components, errorOut))
            return false;

        ScopedFd parent (open (projectRoot.getFullPathName().toRawUTF8(),
                               O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (parent.get() < 0)
        {
            errorOut = "The project root could not be opened without following a symbolic link.";
            return false;
        }

        for (std::size_t i = 0; i + 1 < components.size(); ++i)
        {
            ScopedFd next (openat (parent.get(), components[i].c_str(),
                                   O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
            if (next.get() < 0)
            {
                errorOut = "The parent directory could not be opened without following a symbolic link.";
                return false;
            }
            parent = std::move (next);
        }

        ScopedFd target (openat (parent.get(), components.back().c_str(),
                                 O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (target.get() < 0)
        {
            errorOut = errno == ENOENT ? "The requested file does not exist."
                                       : "The file could not be opened safely.";
            return false;
        }

        struct stat targetStatus {};
        if (fstat (target.get(), &targetStatus) != 0 || ! S_ISREG (targetStatus.st_mode))
        {
            errorOut = "The requested path is not a regular file.";
            return false;
        }

        size = static_cast<std::int64_t> (targetStatus.st_size);
        if (size <= 1024 * 1024)
        {
            std::string bytes;
            if (! readDescriptor (target.get(), bytes))
            {
                errorOut = "The file could not be read safely.";
                return false;
            }
            contents = juce::String::fromUTF8 (bytes.data(), static_cast<int> (bytes.size()));
        }

        return true;
    }


    bool writeDescriptor (const int descriptor, const std::string& contents)
    {
        std::size_t offset = 0;
        while (offset < contents.size())
        {
            const auto bytesWritten = write (descriptor, contents.data() + offset, contents.size() - offset);
            if (bytesWritten < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }

            if (bytesWritten == 0)
                return false;

            offset += static_cast<std::size_t> (bytesWritten);
        }

        return true;
    }

    bool replaceFileWithoutFollowingSymlinks (const juce::File& projectRoot,
                                              const juce::var& arguments,
                                              const juce::String& expectedContent,
                                              const bool expectedExisted,
                                              const juce::String& newContent,
                                              const bool createMissingDirectories,
                                              juce::String& errorOut)
    {
        std::vector<std::string> components;
        if (! getSafeRelativeComponents (arguments, components, errorOut))
            return false;

        ScopedFd parent (open (projectRoot.getFullPathName().toRawUTF8(),
                               O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (parent.get() < 0)
        {
            errorOut = "The project root could not be opened without following a symbolic link.";
            return false;
        }

        for (std::size_t i = 0; i + 1 < components.size(); ++i)
        {
            ScopedFd next (openat (parent.get(), components[i].c_str(),
                                   O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));

            if (next.get() < 0 && createMissingDirectories && errno == ENOENT)
            {
                if (mkdirat (parent.get(), components[i].c_str(), 0755) != 0 && errno != EEXIST)
                {
                    errorOut = "The parent directory could not be created safely.";
                    return false;
                }

                next.reset (openat (parent.get(), components[i].c_str(),
                                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
            }

            if (next.get() < 0)
            {
                errorOut = "The parent directory could not be opened without following a symbolic link.";
                return false;
            }

            parent = std::move (next);
        }

        const auto& targetName = components.back();
        struct stat targetStat {};
        const auto initialStatus = fstatat (parent.get(), targetName.c_str(), &targetStat, AT_SYMLINK_NOFOLLOW);
        const bool targetExists = initialStatus == 0;

        if (! targetExists && errno != ENOENT)
        {
            errorOut = "The target file could not be inspected safely.";
            return false;
        }

        if (targetExists && S_ISLNK (targetStat.st_mode))
        {
            errorOut = "The target path is a symbolic link and cannot be written.";
            return false;
        }

        if (targetExists && ! S_ISREG (targetStat.st_mode))
        {
            errorOut = "The target path is not a regular file.";
            return false;
        }

        if (targetExists != expectedExisted)
        {
            errorOut = "The file changed after preview. Review the new preview before approving.";
            return false;
        }

        if (targetExists)
        {
            ScopedFd existing (openat (parent.get(), targetName.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
            std::string currentContent;
            const auto expectedBytes = std::string (expectedContent.toRawUTF8(),
                                                    static_cast<std::size_t> (expectedContent.getNumBytesAsUTF8()));

            if (existing.get() < 0 || ! readDescriptor (existing.get(), currentContent) || currentContent != expectedBytes)
            {
                errorOut = "The file changed after preview. Review the new preview before approving.";
                return false;
            }
        }

        const auto bytes = std::string (newContent.toRawUTF8(),
                                        static_cast<std::size_t> (newContent.getNumBytesAsUTF8()));
        const auto mode = targetExists ? (targetStat.st_mode & 0777) : 0666;
        ScopedFd temporary;
        std::string temporaryName;

        for (int attempt = 0; attempt < 8 && temporary.get() < 0; ++attempt)
        {
            temporaryName = ".projucer-ai-"
                          + juce::Uuid().toString().removeCharacters ("-{}").toStdString()
                          + ".tmp";
            temporary.reset (openat (parent.get(), temporaryName.c_str(),
                                     O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode));
        }

        if (temporary.get() < 0)
        {
            errorOut = "A safe temporary file could not be created.";
            return false;
        }

        const auto cleanupTemporary = [&]
        {
            unlinkat (parent.get(), temporaryName.c_str(), 0);
        };

        if ((targetExists && fchmod (temporary.get(), mode) != 0)
            || ! writeDescriptor (temporary.get(), bytes)
            || fsync (temporary.get()) != 0)
        {
            cleanupTemporary();
            errorOut = "The file could not be written safely.";
            return false;
        }

        struct stat beforeRename {};
        const auto statusBeforeRename = fstatat (parent.get(), targetName.c_str(), &beforeRename, AT_SYMLINK_NOFOLLOW);
        const auto existsBeforeRename = statusBeforeRename == 0;
        if ((! existsBeforeRename && errno != ENOENT)
            || existsBeforeRename != expectedExisted
            || (existsBeforeRename && (S_ISLNK (beforeRename.st_mode) || ! S_ISREG (beforeRename.st_mode))))
        {
            cleanupTemporary();
            errorOut = "The file changed after preview. Review the new preview before approving.";
            return false;
        }

        if (existsBeforeRename)
        {
            ScopedFd existing (openat (parent.get(), targetName.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
            std::string currentContent;
            const auto expectedBytes = std::string (expectedContent.toRawUTF8(),
                                                    static_cast<std::size_t> (expectedContent.getNumBytesAsUTF8()));

            if (existing.get() < 0 || ! readDescriptor (existing.get(), currentContent) || currentContent != expectedBytes)
            {
                cleanupTemporary();
                errorOut = "The file changed after preview. Review the new preview before approving.";
                return false;
            }
        }

        if (renameat (parent.get(), temporaryName.c_str(), parent.get(), targetName.c_str()) != 0)
        {
            cleanupTemporary();
            errorOut = "The file could not be replaced safely.";
            return false;
        }

        fsync (parent.get());
        return true;
    }
#else
    bool containsSymbolicLink (const juce::File& projectRoot,
                               const std::vector<std::string>& components)
    {
        std::error_code error;
        auto current = std::filesystem::weakly_canonical (std::filesystem::path (projectRoot.getFullPathName().toStdString()), error);
        if (error)
            return true;

        for (const auto& component : components)
        {
            current /= component;
            const auto status = std::filesystem::symlink_status (current, error);
            if (error == std::errc::no_such_file_or_directory)
                break;
            if (error || std::filesystem::is_symlink (status))
                return true;
        }

        return false;
    }

    bool replaceFileConservatively (const juce::File& projectRoot,
                                    const juce::var& arguments,
                                    const juce::File& file,
                                    const juce::String& expectedContent,
                                    const bool expectedExisted,
                                    const juce::String& newContent,
                                    juce::String& errorOut)
    {
        juce::ignoreUnused (projectRoot, arguments, file, expectedContent, expectedExisted, newContent);
        errorOut = "Safe AI writes are available only on Apple platforms.";
        return false;
    }
#endif
}

AiTools::AiTools (const juce::File& projectRootToUse)
    : projectRoot (projectRootToUse)
{
}

AiTools::~AiTools()
{
    cancel();
}

void AiTools::cancel()
{
    cancelRequested.store (true, std::memory_order_release);
    runningProcess.kill();
}

juce::String AiTools::makeArgumentsKey (const juce::var& arguments)
{
    return juce::JSON::toString (arguments, true);
}

bool AiTools::isPreviewStateCurrent (const PreviewState& previewState, juce::String& errorOut) const
{
    if (previewState.file.exists() && ! previewState.file.existsAsFile())
    {
        errorOut = "The file changed after preview. Review the new preview before approving.";
        return false;
    }

    const auto exists = previewState.file.existsAsFile();
    if (exists != previewState.existed)
    {
        errorOut = "The file changed after preview. Review the new preview before approving.";
        return false;
    }

    if (exists && previewState.file.loadFileAsString() != previewState.content)
    {
        errorOut = "The file changed after preview. Review the new preview before approving.";
        return false;
    }

    return true;
}

bool AiTools::revalidateForWrite (const juce::var& arguments,
                                  const juce::File& expectedFile,
                                  juce::File& fileOut,
                                  juce::String& errorOut) const
{
    if (! resolve (arguments, fileOut, errorOut))
    {
        errorOut = "The path could not be revalidated immediately before writing.";
        return false;
    }

    if (fileOut.getFullPathName() != expectedFile.getFullPathName())
    {
        errorOut = "The path changed after preview. Review the new preview before approving.";
        return false;
    }

    return true;
}

bool AiTools::requiresApproval (const juce::String& toolName)
{
    return toolName == "write_file" || toolName == "apply_patch" || toolName == "exec_command";
}

juce::var AiTools::getToolSchemas()
{
    juce::Array<juce::var> tools;

    {
        auto* properties = new juce::DynamicObject();
        properties->setProperty ("path", makeStringProperty ("Directory to list. Relative paths resolve against the project root; absolute paths and .. are allowed. Defaults to the project root."));
        tools.add (makeTool ("list_files", "List files and directories. Reading is not restricted to the project root, so you may inspect parent directories and absolute paths.", properties, {}));
    }

    {
        auto* properties = new juce::DynamicObject();
        properties->setProperty ("path", makeStringProperty ("File to read. Relative paths resolve against the project root; absolute paths and .. are allowed."));
        properties->setProperty ("start_line", makeIntegerProperty ("Optional one-based first line, inclusive."));
        properties->setProperty ("end_line", makeIntegerProperty ("Optional one-based last line, inclusive."));
        tools.add (makeTool ("read_file", "Read a project file. Use a line range for large files.", properties, { "path" }));
    }

    {
        auto* properties = new juce::DynamicObject();
        properties->setProperty ("path", makeStringProperty ("Project-relative file path. Writing is limited to the project root."));
        properties->setProperty ("content", makeStringProperty ("Complete replacement file content."));
        tools.add (makeTool ("write_file", "Create a file or replace its complete contents.", properties, { "path", "content" }));
    }

    {
        auto* properties = new juce::DynamicObject();
        properties->setProperty ("path", makeStringProperty ("Project-relative file path. Writing is limited to the project root."));
        properties->setProperty ("old_text", makeStringProperty ("Unique text to replace."));
        properties->setProperty ("new_text", makeStringProperty ("Replacement text."));
        tools.add (makeTool ("apply_patch", "Apply one unique old_text/new_text replacement to a project file.", properties, { "path", "old_text", "new_text" }));
    }

    /*  Codex の exec_command と同じ役割。git clone やビルドなど、ファイル
        ツールではできない作業をシェルで行う。追加のキーは Codex 側の形に
        合わせて受け取り、知らないものは無視する。 */
    {
        auto* properties = new juce::DynamicObject();
        properties->setProperty ("cmd", makeStringProperty ("Shell command to execute. Runs in a login shell so git and compilers from the user PATH are available."));
        properties->setProperty ("workdir", makeStringProperty ("Working directory relative to the project root. Defaults to the project root. Must stay inside the project."));
        properties->setProperty ("yield_time_ms", makeIntegerProperty ("Maximum time to wait in milliseconds. Defaults to 300000 (5 minutes). Range 10000-300000."));

        auto tool = makeTool ("exec_command",
                              "Run a shell command in the project working directory. Use this for git clone, builds, tests, and other command-line work. Commands that write files or use the network need the user's approval unless they chose Full access.",
                              properties, { "cmd" });

        if (auto* object = tool.getDynamicObject())
            if (auto* parameters = object->getProperty ("parameters").getDynamicObject())
                parameters->setProperty ("additionalProperties", true);

        tools.add (tool);
    }

    /*  Web 検索はサーバー側が持つ組み込みツール。こちらで実装する必要はなく、
        tools にこの 1 項目を載せるだけでモデルが検索と取得を行う。
        Codex も同じ形で載せている (tools/src/tool_spec.rs の web_search)。

        external_web_access = true で、キャッシュではなく実際にネットを見に行く。 */
    {
        auto* webSearch = new juce::DynamicObject();
        webSearch->setProperty ("type", "web_search");
        webSearch->setProperty ("external_web_access", true);
        tools.add (juce::var (webSearch));
    }

    return juce::var (tools);
}

bool AiTools::resolve (const juce::var& arguments, juce::File& fileOut, juce::String& errorOut) const
{
    const auto* object = arguments.getDynamicObject();

    if (object == nullptr)
    {
        errorOut = "Tool arguments must be a JSON object.";
        return false;
    }

    if (! object->hasProperty ("path"))
    {
        errorOut = "The path argument is required.";
        return false;
    }

    const auto pathValue = object->getProperty ("path");

    if (! pathValue.isString())
    {
        errorOut = "The path argument must be a string.";
        return false;
    }

    const auto path = pathValue.toString();

    if (path.isEmpty())
    {
        errorOut = "The path argument is required.";
        return false;
    }

    const auto resolved = resolveInsideRoot (std::filesystem::path (projectRoot.getFullPathName().toStdString()),
                                             path.toStdString());

    if (! resolved.has_value())
    {
        errorOut = "The path is outside the project root or cannot be resolved.";
        return false;
    }

    fileOut = juce::File (juce::String::fromUTF8 (resolved->string().c_str()));
    return true;
}

AiTools::Result AiTools::preview (const juce::String& toolName, const juce::var& arguments)
{
    const juce::ScopedLock scopedLock (ioLock);

    if (toolName == "write_file")
    {
        pendingPreview.reset();
        PreviewState state;
        const auto result = doWriteFile (arguments, false, &state);
        if (result.ok)
        {
            state.toolName = toolName;
            state.argumentsKey = makeArgumentsKey (arguments);
            pendingPreview = state;
        }
        return result;
    }

    if (toolName == "apply_patch")
    {
        pendingPreview.reset();
        PreviewState state;
        const auto result = doApplyPatch (arguments, false, &state);
        if (result.ok)
        {
            state.toolName = toolName;
            state.argumentsKey = makeArgumentsKey (arguments);
            pendingPreview = state;
        }
        return result;
    }

    if (toolName == "exec_command")
    {
        pendingPreview.reset();
        PreviewState state;
        const auto result = doExecCommand (arguments, false, &state);
        if (result.ok)
        {
            state.toolName = toolName;
            state.argumentsKey = makeArgumentsKey (arguments);
            pendingPreview = state;
        }
        return result;
    }

    return { false, "The tool does not require approval.", {} };
}

AiTools::Result AiTools::execute (const juce::String& toolName, const juce::var& arguments)
{
    DBG ("[AI] execute " << toolName << "  root=" << projectRoot.getFullPathName());

    const juce::ScopedLock scopedLock (ioLock);

    if (toolName == "list_files")
        return doListFiles (arguments);
    if (toolName == "read_file")
        return doReadFile (arguments);
    if (toolName == "write_file")
    {
        const auto result = doWriteFile (arguments, true);
        pendingPreview.reset();
        return result;
    }
    if (toolName == "apply_patch")
    {
        const auto result = doApplyPatch (arguments, true);
        pendingPreview.reset();
        return result;
    }

    if (toolName == "exec_command")
    {
        const auto result = doExecCommand (arguments, true);
        pendingPreview.reset();
        return result;
    }

    return { false, "Unknown tool.", {} };
}

bool AiTools::resolveForReading (const juce::var& arguments, juce::File& fileOut, juce::String& errorOut) const
{
    const auto* object = arguments.getDynamicObject();

    if (object == nullptr)
    {
        errorOut = "Tool arguments must be a JSON object.";
        return false;
    }

    // path を省略した場合はプロジェクトルート。list_files の既定の対象になる。
    if (! object->hasProperty ("path"))
    {
        fileOut = projectRoot;
        return true;
    }

    const auto pathValue = object->getProperty ("path");

    if (! pathValue.isString())
    {
        errorOut = "The path argument must be a string.";
        return false;
    }

    auto path = pathValue.toString();

    if (path.isEmpty())
    {
        fileOut = projectRoot;
        return true;
    }

    // ~ から始まる指定はホーム基準として扱う。シェルと同じ感覚で書けるように。
    if (path.startsWith ("~"))
        path = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getFullPathName() + path.substring (1);

    /*  読み取りは範囲を制限しない。絶対パスはそのまま、相対パスは
        プロジェクトルート基準で解決する。.. で外へ出るのも許す。 */
    const auto candidate = juce::File::isAbsolutePath (path)
                             ? juce::File (path)
                             : projectRoot.getChildFile (path);

    if (candidate.getFullPathName().isEmpty())
    {
        errorOut = "The path could not be resolved.";
        return false;
    }

    fileOut = candidate;
    return true;
}

AiTools::Result AiTools::doListFiles (const juce::var& arguments) const
{
    const auto* object = arguments.getDynamicObject();

    if (! arguments.isVoid() && object == nullptr)
        return { false, "Tool arguments must be a JSON object.", {} };

    juce::File directory;
    juce::String error;

    if (! resolveForReading (arguments, directory, error))
        return { false, error, {} };

    if (! directory.isDirectory())
        return { false, "The requested path is not a directory.", {} };

    /*  読み取りは範囲を制限しない（Codex の ReadOnly と同じ）。シンボリックリンクも
        辿ってよい。脱出を防ぐ必要があるのは書き込み側だけで、そちらは
        resolve() が引き続きプロジェクトルート配下に限定する。 */
    juce::StringArray names;

    for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*",
                                                            juce::File::findFilesAndDirectories))
    {
        auto name = entry.getFile().getFileName();

        if (entry.getFile().isDirectory())
            name << "/";

        names.add (name);
    }

    names.sort (true);

    if (names.isEmpty())
        return { true, "(empty directory)", {} };

    return { true, names.joinIntoString ("\n"), {} };
}

AiTools::Result AiTools::doReadFile (const juce::var& arguments) const
{
    juce::File file;
    juce::String error;

    // 読み取りは範囲を制限しない（Codex の ReadOnly と同じ）。
    if (! resolveForReading (arguments, file, error))
        return { false, error, {} };
    int firstLine = 0, lastLine = 0;
    juce::String rangeError;
    if (! getLineRange (arguments, firstLine, lastLine, rangeError))
        return { false, rangeError, {} };

    std::int64_t fileSize = file.getSize();
    juce::String safeContents;

    /*  読み取りは範囲を制限しないので、シンボリックリンクを避けて開く
        専用の経路は使わない（あれはプロジェクト相対パス前提で、絶対パスや
        .. を弾いてしまう）。解決済みのファイルをそのまま読む。 */
    if (! file.existsAsFile())
        return { false, "The requested file does not exist: " + file.getFullPathName(), {} };

    if (fileSize > maxReadBytes && firstLine == 1 && lastLine == 0)
        return { false, "The file exceeds the 1 MB read limit. Specify start_line and end_line.", {} };

    safeContents = file.loadFileAsString();

    if (fileSize == 0 && firstLine == 1 && lastLine == 0)
        return { true, {}, {} };

    if (fileSize <= maxReadBytes && firstLine == 1 && lastLine == 0)
        return { true, safeContents, {} };

    if (fileSize > maxReadBytes && lastLine == 0 && firstLine == 1)
        return { false, "The file exceeds the 1 MB read limit. Specify start_line and end_line.", {} };

    juce::StringArray selected;
    std::int64_t lineNumber = 0;
    std::int64_t outputBytes = 0;
    juce::StringArray lines;
    lines.addLines (safeContents);

    for (const auto& line : lines)
    {
        ++lineNumber;

        if (lineNumber < firstLine)
            continue;

        if (lastLine > 0 && lineNumber > lastLine)
            break;

        outputBytes += line.getNumBytesAsUTF8() + (selected.isEmpty() ? 0 : 1);

        if (outputBytes > maxReadBytes)
            return { false, "The requested line range exceeds the 1 MB read limit.", {} };

        selected.add (line);
    }

    if (selected.isEmpty() && lineNumber < firstLine)
        return { false, "The requested line range is outside the file.", {} };

    return { true, selected.joinIntoString ("\n"), {} };
}

AiTools::Result AiTools::doWriteFile (const juce::var& arguments,
                                      bool actuallyWrite,
                                      PreviewState* previewStateOut) const
{
    juce::File file;
    juce::String error;
    if (! resolve (arguments, file, error))
        return { false, error, {} };

    const auto* object = arguments.getDynamicObject();
    const auto contentValue = object->getProperty ("content");

    if (! object->hasProperty ("content") || ! contentValue.isString())
        return { false, "The content argument is required and must be a string.", {} };

    if (file.exists() && ! file.existsAsFile())
        return { false, "The requested path is not a file.", {} };

    const auto newContent = contentValue.toString();
    const auto fileExisted = file.existsAsFile();
    juce::String oldContent;
#if defined (__APPLE__)
    if (fileExisted)
    {
        std::int64_t oldSize = 0;
        if (! readProjectFileWithoutFollowingSymlinks (projectRoot, arguments, oldContent, oldSize, error))
            return { false, error, {} };
        if (oldSize > maxReadBytes)
            return { false, "The existing file exceeds the 1 MB read limit.", {} };
    }
#else
    oldContent = fileExisted ? file.loadFileAsString() : juce::String();
#endif
    const auto diff = makeDiff (oldContent, newContent);

    if (! actuallyWrite)
    {
        if (previewStateOut != nullptr)
        {
            previewStateOut->file = file;
            previewStateOut->existed = fileExisted;
            previewStateOut->content = oldContent;
        }

        return { true, "Preview generated.", diff };
    }

    if (pendingPreview.has_value())
    {
        const auto& previewState = *pendingPreview;
        if (previewState.toolName != "write_file"
            || previewState.argumentsKey != makeArgumentsKey (arguments)
            || previewState.file.getFullPathName() != file.getFullPathName())
            return { false, "The approval preview does not match this request.", diff };

        if (! isPreviewStateCurrent (previewState, error))
            return { false, error, diff };
    }

    juce::File revalidatedFile;
    if (! revalidateForWrite (arguments, file, revalidatedFile, error))
        return { false, error, diff };
    file = revalidatedFile;

    if (pendingPreview.has_value() && ! isPreviewStateCurrent (*pendingPreview, error))
        return { false, error, diff };

#if defined (__APPLE__)
    if (! replaceFileWithoutFollowingSymlinks (projectRoot, arguments, oldContent, fileExisted,
                                               newContent, true, error))
        return { false, error, diff };
#else
    if (! replaceFileConservatively (projectRoot, arguments, file, oldContent, fileExisted, newContent, error))
        return { false, error, diff };
#endif

    return { true, "File written successfully.", diff };
}

AiTools::Result AiTools::doApplyPatch (const juce::var& arguments,
                                       bool actuallyWrite,
                                       PreviewState* previewStateOut) const
{
    juce::File file;
    juce::String error;
    if (! resolve (arguments, file, error))
        return { false, error, {} };
    if (! file.existsAsFile())
        return { false, "The requested file does not exist. Use write_file to create it.", {} };

    juce::String oldText, newText;
    if (! extractPatchArguments (arguments, oldText, newText, error))
        return { false, error, {} };

    juce::String content;
#if defined (__APPLE__)
    std::int64_t contentSize = 0;
    if (! readProjectFileWithoutFollowingSymlinks (projectRoot, arguments, content, contentSize, error))
        return { false, error, {} };
    if (contentSize > maxReadBytes)
        return { false, "The existing file exceeds the 1 MB read limit.", {} };
#else
    content = file.loadFileAsString();
#endif
    const auto firstIndex = content.indexOf (oldText);
    if (firstIndex < 0)
        return { false, "old_text was not found in the file.", {} };
    if (content.indexOf (firstIndex + 1, oldText) >= 0)
        return { false, "old_text is not unique in the file.", {} };

    const auto updated = content.replaceSection (firstIndex, oldText.length(), newText);
    const auto diff = makeDiff (content, updated);
    if (! actuallyWrite)
    {
        if (previewStateOut != nullptr)
        {
            previewStateOut->file = file;
            previewStateOut->existed = true;
            previewStateOut->content = content;
        }

        return { true, "Preview generated.", diff };
    }

    if (pendingPreview.has_value())
    {
        const auto& previewState = *pendingPreview;
        if (previewState.toolName != "apply_patch"
            || previewState.argumentsKey != makeArgumentsKey (arguments)
            || previewState.file.getFullPathName() != file.getFullPathName())
            return { false, "The approval preview does not match this request.", diff };

        if (! isPreviewStateCurrent (previewState, error))
            return { false, error, diff };
    }

    juce::File revalidatedFile;
    if (! revalidateForWrite (arguments, file, revalidatedFile, error))
        return { false, error, diff };
    file = revalidatedFile;

    if (pendingPreview.has_value() && ! isPreviewStateCurrent (*pendingPreview, error))
        return { false, error, diff };

    juce::String contentBeforeWrite;
#if defined (__APPLE__)
    std::int64_t contentBeforeWriteSize = 0;
    if (! readProjectFileWithoutFollowingSymlinks (projectRoot, arguments, contentBeforeWrite,
                                                   contentBeforeWriteSize, error))
        return { false, error, diff };
#else
    contentBeforeWrite = file.loadFileAsString();
#endif
    if (contentBeforeWrite != content)
        return { false, "The file changed after preview. Review the new preview before approving.", diff };

#if defined (__APPLE__)
    if (! replaceFileWithoutFollowingSymlinks (projectRoot, arguments, content, true,
                                               updated, false, error))
        return { false, error, diff };
#else
    if (! replaceFileConservatively (projectRoot, arguments, file, content, true, updated, error))
        return { false, error, diff };
#endif

    return { true, "File patched successfully.", diff };
}

bool AiTools::resolveExecWorkdir (const juce::var& arguments, juce::File& directoryOut, juce::String& errorOut) const
{
    const auto* object = arguments.getDynamicObject();

    if (object == nullptr)
    {
        errorOut = "Tool arguments must be a JSON object.";
        return false;
    }

    juce::String requested;

    if (object->hasProperty ("workdir"))
    {
        const auto workdirValue = object->getProperty ("workdir");

        if (! workdirValue.isString())
        {
            errorOut = "The workdir argument must be a string.";
            return false;
        }

        requested = workdirValue.toString().trim();
    }

    /*  File のコンストラクタは絶対パス専用。相対の "." や "JUCE" を渡すと
        Debug で jassertfalse になるので、必ず getChildFile か resolveInsideRoot
        経由にする。 */
    if (requested.isEmpty() || requested == "." || requested == "./")
    {
        directoryOut = projectRoot;
        return true;
    }

    if (juce::File::isAbsolutePath (requested))
    {
        const juce::File asFile (requested);

        if (asFile != projectRoot && ! asFile.isAChildOf (projectRoot))
        {
            errorOut = "The working directory must stay inside the project root.";
            return false;
        }

        requested = asFile.getRelativePathFrom (projectRoot);

        if (requested.isEmpty() || requested == ".")
        {
            directoryOut = projectRoot;
            return true;
        }
    }

    const auto resolved = resolveInsideRoot (std::filesystem::path (projectRoot.getFullPathName().toStdString()),
                                             requested.toStdString());

    if (! resolved.has_value())
    {
        errorOut = "The working directory is outside the project root or cannot be resolved.";
        return false;
    }

    directoryOut = projectRoot.getChildFile (juce::String::fromUTF8 (resolved->string().c_str()));

    if (! directoryOut.isDirectory())
    {
        errorOut = "The working directory does not exist.";
        return false;
    }

    return true;
}

AiTools::Result AiTools::doExecCommand (const juce::var& arguments,
                                        bool actuallyRun,
                                        PreviewState* previewStateOut)
{
    const auto* object = arguments.getDynamicObject();

    if (object == nullptr)
        return { false, "Tool arguments must be a JSON object.", {} };

    if (! object->hasProperty ("cmd") || ! object->getProperty ("cmd").isString())
        return { false, "The cmd argument is required and must be a string.", {} };

    const auto cmd = object->getProperty ("cmd").toString();

    if (cmd.trim().isEmpty())
        return { false, "The cmd argument must not be empty.", {} };

    juce::File cwd;
    juce::String error;

    if (! resolveExecWorkdir (arguments, cwd, error))
        return { false, error, {} };

    juce::String preview;
    preview << "Command:\n"
            << cmd << "\n\n"
            << "Working directory:\n"
            << cwd.getFullPathName();

    if (! actuallyRun)
    {
        if (previewStateOut != nullptr)
        {
            previewStateOut->file = cwd;
            previewStateOut->existed = cwd.isDirectory();
            previewStateOut->content = cmd;
        }

        return { true, "Preview generated.", preview };
    }

    if (pendingPreview.has_value())
    {
        const auto& previewState = *pendingPreview;

        if (previewState.toolName != "exec_command"
            || previewState.argumentsKey != makeArgumentsKey (arguments))
            return { false, "The approval preview does not match this request.", preview };
    }

    cancelRequested.store (false, std::memory_order_release);

    auto timeoutMs = defaultExecTimeoutMs;

    if (object->hasProperty ("yield_time_ms"))
    {
        const auto value = object->getProperty ("yield_time_ms");

        if (value.isInt() || value.isInt64() || value.isDouble())
            timeoutMs = juce::jlimit (minExecTimeoutMs, maxExecTimeoutMs,
                                      (int) static_cast<std::int64_t> (value));
    }

    if (useVisibleTerminal)
    {
        auto line = cmd.trimEnd();

        if (cwd.getFullPathName() != projectRoot.getFullPathName())
            line = "(cd -- " + quoteForShell (cwd.getFullPathName()) + " && " + line + ")";

        if (visibleTerminalRunner == nullptr)
            return { false, "The bottom terminal is not available.", preview };

        juce::String output;

        if (! visibleTerminalRunner (line, output, timeoutMs, cancelRequested))
            return { false, output.isNotEmpty() ? output
                                                : "Could not run the command in the bottom terminal.",
                     preview };

        return { output.contains ("exit_code: 0"), output, preview };
    }

   #if JUCE_WINDOWS
    juce::StringArray args { "cmd.exe", "/c",
                             "cd /d \"" + cwd.getFullPathName() + "\" && " + cmd };
   #else
    juce::String shell ("/bin/sh");

    if (const auto* fromEnv = std::getenv ("SHELL"); fromEnv != nullptr && fromEnv[0] != '\0')
        shell = fromEnv;

    juce::StringArray args { shell, "-lc", "cd -- \"$1\" && eval \"$2\"",
                             "exec_command", cwd.getFullPathName(), cmd };
   #endif

    if (! runningProcess.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return { false, "Could not start a shell to run the command.", preview };

    const auto startedAt = juce::Time::getMillisecondCounter();

    while (runningProcess.isRunning())
    {
        if (cancelRequested.load (std::memory_order_acquire))
        {
            runningProcess.kill();
            return { false, "The command was stopped.", preview };
        }

        if ((int) (juce::Time::getMillisecondCounter() - startedAt) >= timeoutMs)
        {
            runningProcess.kill();
            return { false, "The command timed out after " + juce::String (timeoutMs / 1000) + " seconds.",
                     preview };
        }

        runningProcess.waitForProcessToFinish (200);
    }

    auto output = runningProcess.readAllProcessOutput();
    const auto exitCode = (int) runningProcess.getExitCode();

    if (output.length() > maxExecOutputChars)
        output = output.substring (0, maxExecOutputChars) + "\n...[truncated]";

    juce::String resultText;
    resultText << "exit_code: " << exitCode << "\n";

    if (output.isNotEmpty())
        resultText << output;
    else
        resultText << "(no output)";

    return { exitCode == 0, resultText, preview };
}
