#include "jucer_GitCommand.h"

#include "../AI/jucer_Keychain.h"

#if JUCE_MAC || JUCE_IOS

#include <git2.h>

namespace ProjucerGit
{

namespace
{
    /*  libgit2 はプロセスにつき 1 回だけ初期化する。ここを通らずに API を
        呼ぶとクラッシュするので、すべての入り口で ensureLibrary() を呼ぶ。 */
    void ensureLibrary()
    {
        struct Library
        {
            Library()   { git_libgit2_init(); }
            ~Library()  { git_libgit2_shutdown(); }
        };

        static Library library;
        juce::ignoreUnused (library);
    }

    juce::String lastError()
    {
        if (const auto* error = git_error_last(); error != nullptr && error->message != nullptr)
            return juce::String::fromUTF8 (error->message);

        return "unknown libgit2 error";
    }

    /*  git_*_free を持つ型をまとめて片付ける。&owned で T** を渡せる。 */
    template <typename T, void (*FreeFunction) (T*)>
    struct Owned
    {
        Owned() = default;
        Owned (const Owned&) = delete;
        Owned& operator= (const Owned&) = delete;
        ~Owned()                        { reset(); }

        void reset()                    { if (ptr != nullptr) FreeFunction (ptr); ptr = nullptr; }
        T** operator&()                 { reset(); return &ptr; }
        operator T*() const noexcept    { return ptr; }
        T* get() const noexcept         { return ptr; }

        T* ptr = nullptr;
    };

    using Repo        = Owned<git_repository, git_repository_free>;
    using Index       = Owned<git_index, git_index_free>;
    using Reference   = Owned<git_reference, git_reference_free>;
    using Object      = Owned<git_object, git_object_free>;
    using Commit      = Owned<git_commit, git_commit_free>;
    using Tree        = Owned<git_tree, git_tree_free>;
    using Signature   = Owned<git_signature, git_signature_free>;
    using Diff        = Owned<git_diff, git_diff_free>;
    using DiffStats   = Owned<git_diff_stats, git_diff_stats_free>;
    using StatusList  = Owned<git_status_list, git_status_list_free>;
    using RevWalk     = Owned<git_revwalk, git_revwalk_free>;
    using Remote      = Owned<git_remote, git_remote_free>;
    using Config      = Owned<git_config, git_config_free>;
    using BranchIter  = Owned<git_branch_iterator, git_branch_iterator_free>;
    using Annotated   = Owned<git_annotated_commit, git_annotated_commit_free>;
    using Submodule   = Owned<git_submodule, git_submodule_free>;

    struct Buffer
    {
        ~Buffer()                       { git_buf_dispose (&buf); }
        juce::String toString() const   { return buf.ptr != nullptr ? juce::String::fromUTF8 (buf.ptr, (int) buf.size)
                                                                    : juce::String(); }
        git_buf buf { nullptr, 0, 0 };
    };

    //==============================================================================
    /*  コマンドライン引数。値を取るオプションはコマンドごとに違うので、
        呼び出し側が名前を渡す。 */
    struct Args
    {
        juce::StringArray positional;
        juce::StringArray flags;                    // "--cached" や "-a"
        juce::StringArray valueKeys, valueValues;

        bool has (juce::StringRef name) const       { return flags.contains (name); }

        bool hasAny (const juce::StringArray& names) const
        {
            for (const auto& name : names)
                if (flags.contains (name))
                    return true;

            return false;
        }

        juce::String value (juce::StringRef name, const juce::String& fallback = {}) const
        {
            const auto index = valueKeys.indexOf (name);
            return index >= 0 ? valueValues[index] : fallback;
        }

        bool hasValue (juce::StringRef name) const  { return valueKeys.contains (name); }
    };

    Args parseArgs (const juce::StringArray& argv, const juce::StringArray& valueTaking)
    {
        Args args;
        bool noMoreOptions = false;

        for (int i = 0; i < argv.size(); ++i)
        {
            const auto token = argv[i];

            if (noMoreOptions || ! token.startsWith ("-") || token == "-")
            {
                args.positional.add (token);
                continue;
            }

            if (token == "--")
            {
                noMoreOptions = true;
                continue;
            }

            if (token.startsWith ("--") && token.containsChar ('='))
            {
                args.valueKeys.add (token.upToFirstOccurrenceOf ("=", false, false));
                args.valueValues.add (token.fromFirstOccurrenceOf ("=", false, false));
                continue;
            }

            /*  "-m" のように値を取るものは次のトークンを吸う。短いフラグの
                連結 ("-am") は展開して 1 文字ずつ見る。 */
            if (valueTaking.contains (token))
            {
                args.valueKeys.add (token);
                args.valueValues.add (i + 1 < argv.size() ? argv[++i] : juce::String());
                continue;
            }

            if (! token.startsWith ("--") && token.length() > 2)
            {
                for (int c = 1; c < token.length(); ++c)
                {
                    const juce::String single ("-" + juce::String::charToString (token[c]));

                    if (valueTaking.contains (single))
                    {
                        // 連結の最後に来た値付きフラグは、残りか次のトークンを値にする。
                        const auto inlineValue = token.substring (c + 1);
                        args.valueKeys.add (single);
                        args.valueValues.add (inlineValue.isNotEmpty() ? inlineValue
                                                                       : (i + 1 < argv.size() ? argv[++i] : juce::String()));
                        break;
                    }

                    args.flags.add (single);
                }

                continue;
            }

            args.flags.add (token);
        }

        return args;
    }

    //==============================================================================
    juce::StringArray tokenise (const juce::String& commandLine)
    {
        juce::StringArray tokens;
        juce::String current;
        bool haveCurrent = false;
        juce::juce_wchar quote = 0;

        for (auto text = commandLine.getCharPointer(); ! text.isEmpty(); ++text)
        {
            const auto character = *text;

            if (quote != 0)
            {
                if (character == quote)     quote = 0;
                else                        current << juce::String::charToString (character);

                continue;
            }

            if (character == '\'' || character == '"')
            {
                quote = character;
                haveCurrent = true;
                continue;
            }

            if (character == '\\')
            {
                ++text;

                if (text.isEmpty())
                    break;

                current << juce::String::charToString (*text);
                haveCurrent = true;
                continue;
            }

            if (juce::CharacterFunctions::isWhitespace (character))
            {
                if (haveCurrent || current.isNotEmpty())
                    tokens.add (current);

                current.clear();
                haveCurrent = false;
                continue;
            }

            current << juce::String::charToString (character);
            haveCurrent = true;
        }

        if (haveCurrent || current.isNotEmpty())
            tokens.add (current);

        return tokens;
    }

    //==============================================================================
    //  認証。HTTPS + トークンのみ。ホストごとに Keychain へ入れる。
    const char* const credentialService = "tokyo.studio-r.juce.theprojucer.git";

    juce::String hostFromUrl (const juce::String& url)
    {
        auto rest = url.fromFirstOccurrenceOf ("://", false, false);

        if (rest.isEmpty())
            rest = url;

        rest = rest.fromLastOccurrenceOf ("@", false, false).isNotEmpty()
                 ? rest.fromLastOccurrenceOf ("@", false, false)
                 : rest;

        return rest.upToFirstOccurrenceOf ("/", false, false)
                   .upToFirstOccurrenceOf (":", false, false)
                   .toLowerCase();
    }

    struct Credential
    {
        juce::String username, secret;
        bool isValid() const noexcept   { return secret.isNotEmpty(); }
    };

    Credential readCredential (const juce::String& host)
    {
        const auto stored = keychainRead (credentialService, host);

        if (stored.isEmpty())
            return {};

        return { stored.upToFirstOccurrenceOf ("\n", false, false),
                 stored.fromFirstOccurrenceOf ("\n", false, false) };
    }

    struct NetworkPayload
    {
        std::atomic<bool>* cancelFlag = nullptr;
        juce::String credentialError;
    };

    int credentialsCallback (git_credential** out,
                             const char* url,
                             const char* usernameFromUrl,
                             unsigned int allowedTypes,
                             void* payload)
    {
        auto* network = static_cast<NetworkPayload*> (payload);
        const auto host = hostFromUrl (juce::String::fromUTF8 (url != nullptr ? url : ""));
        const auto credential = readCredential (host);

        if (! credential.isValid())
        {
            if (network != nullptr)
                network->credentialError
                    << "No stored credentials for " << host << ".\n"
                    << "Store a personal access token first:\n"
                    << "  git credential set " << host << " <username> <token>";

            return GIT_EUSER;
        }

        if ((allowedTypes & GIT_CREDENTIAL_USERPASS_PLAINTEXT) == 0)
        {
            if (network != nullptr)
                network->credentialError = "The server did not offer HTTPS basic authentication. "
                                           "Only HTTPS with a token is supported.";
            return GIT_EUSER;
        }

        auto username = credential.username;

        if (username.isEmpty())
            username = usernameFromUrl != nullptr ? juce::String::fromUTF8 (usernameFromUrl)
                                                  : juce::String ("x-access-token");

        return git_credential_userpass_plaintext_new (out,
                                                      username.toRawUTF8(),
                                                      credential.secret.toRawUTF8());
    }

    int transferProgressCallback (const git_indexer_progress*, void* payload)
    {
        auto* network = static_cast<NetworkPayload*> (payload);

        if (network != nullptr && network->cancelFlag != nullptr
            && network->cancelFlag->load (std::memory_order_acquire))
            return GIT_EUSER;

        return 0;
    }

    int pushTransferProgressCallback (unsigned int, unsigned int, size_t, void* payload)
    {
        return transferProgressCallback (nullptr, payload);
    }

    void installNetworkCallbacks (git_remote_callbacks& callbacks, NetworkPayload& payload)
    {
        callbacks.credentials = credentialsCallback;
        callbacks.transfer_progress = transferProgressCallback;
        callbacks.push_transfer_progress = pushTransferProgressCallback;
        callbacks.payload = &payload;
    }

    //==============================================================================
    struct Context
    {
        juce::File workingDirectory;
        std::atomic<bool>* cancelFlag = nullptr;
        juce::String out;

        Result fail (const juce::String& message)
        {
            out << (out.isEmpty() ? "" : "\n") << "error: " << message;
            return { 1, out };
        }

        Result failFromLibrary (const juce::String& what)
        {
            return fail (what + ": " + lastError());
        }

        Result ok()                     { return { 0, out }; }
    };

    /*  git 由来のファイル操作は、AiTools が許した作業ディレクトリの外へ
        出さない。clone や init の宛先はここを通す。 */
    bool resolveInsideWorkingDirectory (const Context& context,
                                        const juce::String& relativeOrAbsolute,
                                        juce::File& fileOut)
    {
        const auto candidate = juce::File::isAbsolutePath (relativeOrAbsolute)
                                 ? juce::File (relativeOrAbsolute)
                                 : context.workingDirectory.getChildFile (relativeOrAbsolute);

        if (candidate != context.workingDirectory && ! candidate.isAChildOf (context.workingDirectory))
            return false;

        fileOut = candidate;
        return true;
    }

    bool openRepo (Context& context, Repo& repo)
    {
        return git_repository_open_ext (&repo,
                                        context.workingDirectory.getFullPathName().toRawUTF8(),
                                        0, nullptr) == 0;
    }

    juce::String shortId (const git_oid& id)
    {
        char text[GIT_OID_SHA1_HEXSIZE + 1] = {};
        git_oid_tostr (text, sizeof (text), &id);
        return juce::String (text).substring (0, 7);
    }

    juce::String fullId (const git_oid& id)
    {
        char text[GIT_OID_SHA1_HEXSIZE + 1] = {};
        git_oid_tostr (text, sizeof (text), &id);
        return juce::String (text);
    }

    juce::String formatGitTime (git_time_t seconds, int offsetMinutes)
    {
        const juce::Time time ((juce::int64) (seconds + offsetMinutes * 60) * 1000);
        const auto sign = offsetMinutes < 0 ? "-" : "+";
        const auto absolute = std::abs (offsetMinutes);

        return time.formatted ("%a %b %d %H:%M:%S %Y ")
                 + sign
                 + juce::String (absolute / 60).paddedLeft ('0', 2)
                 + juce::String (absolute % 60).paddedLeft ('0', 2);
    }

    juce::String currentBranchName (git_repository* repo)
    {
        Reference head;

        if (git_repository_head (&head, repo) != 0)
            return {};

        if (const auto* name = git_reference_shorthand (head); name != nullptr)
            return juce::String::fromUTF8 (name);

        return {};
    }

    int diffLineCallback (const git_diff_delta*, const git_diff_hunk*, const git_diff_line* line, void* payload)
    {
        auto& out = *static_cast<juce::String*> (payload);

        if (line->origin == GIT_DIFF_LINE_CONTEXT
            || line->origin == GIT_DIFF_LINE_ADDITION
            || line->origin == GIT_DIFF_LINE_DELETION)
            out << juce::String::charToString ((juce::juce_wchar) line->origin);

        out << juce::String::fromUTF8 (line->content, (int) line->content_len);
        return 0;
    }

    /*  clone --recursive から使うので先に宣言しておく。定義はサブモジュールの
        まとまりの中にある。 */
    bool updateSubmodules (git_repository* repo,
                           const juce::StringArray& requested,
                           bool initialise,
                           bool recursive,
                           NetworkPayload& payload,
                           juce::String& out,
                           juce::String& errorOut);

    //==============================================================================
    Result cmdVersion (Context& context, const juce::StringArray&)
    {
        context.out << "git version 2.x (Projucer built-in, libgit2 " << LIBGIT2_VERSION << ")";
        return context.ok();
    }

    Result cmdInit (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        juce::File target = context.workingDirectory;

        if (args.positional.size() > 0 && ! resolveInsideWorkingDirectory (context, args.positional[0], target))
            return context.fail ("The target directory must stay inside the working directory.");

        Repo repo;

        if (git_repository_init (&repo, target.getFullPathName().toRawUTF8(), args.has ("--bare") ? 1 : 0) != 0)
            return context.failFromLibrary ("could not initialise a repository");

        context.out << "Initialized empty Git repository in " << target.getFullPathName() << "/.git/";
        return context.ok();
    }

    Result cmdClone (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, { "-b", "--branch", "--depth" });

        if (args.positional.isEmpty())
            return context.fail ("usage: git clone <url> [directory]");

        const auto url = args.positional[0];
        auto directoryName = args.positional.size() > 1 ? args.positional[1]
                                                        : url.fromLastOccurrenceOf ("/", false, false)
                                                             .upToLastOccurrenceOf (".git", false, false);

        if (directoryName.isEmpty())
            return context.fail ("Could not work out the destination directory from the URL.");

        juce::File target;

        if (! resolveInsideWorkingDirectory (context, directoryName, target))
            return context.fail ("The destination must stay inside the working directory.");

        if (target.exists() && target.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0)
            return context.fail ("destination path '" + directoryName + "' already exists and is not an empty directory.");

        NetworkPayload payload { context.cancelFlag, {} };
        git_clone_options options = GIT_CLONE_OPTIONS_INIT;
        installNetworkCallbacks (options.fetch_opts.callbacks, payload);

        const auto branch = args.value ("-b", args.value ("--branch"));

        if (branch.isNotEmpty())
            options.checkout_branch = branch.toRawUTF8();

        const auto depth = args.value ("--depth");

        if (depth.isNotEmpty())
            options.fetch_opts.depth = depth.getIntValue();

        Repo repo;

        if (git_clone (&repo, url.toRawUTF8(), target.getFullPathName().toRawUTF8(), &options) != 0)
            return context.failFromLibrary (payload.credentialError.isNotEmpty() ? payload.credentialError
                                                                                 : juce::String ("could not clone"));

        context.out << "Cloning into '" << directoryName << "'...\ndone.";

        if (args.hasAny ({ "--recursive", "--recurse-submodules" }))
        {
            context.out << "\n";
            juce::String error;

            if (! updateSubmodules (repo, {}, true, true, payload, context.out, error))
                return context.fail (error);
        }

        return context.ok();
    }

    //==============================================================================
    juce::String statusCodes (unsigned int status)
    {
        char index = ' ', workdir = ' ';

        if ((status & GIT_STATUS_INDEX_NEW) != 0)           index = 'A';
        if ((status & GIT_STATUS_INDEX_MODIFIED) != 0)      index = 'M';
        if ((status & GIT_STATUS_INDEX_DELETED) != 0)       index = 'D';
        if ((status & GIT_STATUS_INDEX_RENAMED) != 0)       index = 'R';
        if ((status & GIT_STATUS_INDEX_TYPECHANGE) != 0)    index = 'T';

        if ((status & GIT_STATUS_WT_MODIFIED) != 0)         workdir = 'M';
        if ((status & GIT_STATUS_WT_DELETED) != 0)          workdir = 'D';
        if ((status & GIT_STATUS_WT_RENAMED) != 0)          workdir = 'R';
        if ((status & GIT_STATUS_WT_TYPECHANGE) != 0)       workdir = 'T';
        if ((status & GIT_STATUS_WT_NEW) != 0)              { index = '?'; workdir = '?'; }
        if ((status & GIT_STATUS_IGNORED) != 0)             { index = '!'; workdir = '!'; }

        return juce::String::charToString ((juce::juce_wchar) index)
             + juce::String::charToString ((juce::juce_wchar) workdir);
    }

    juce::String statusPath (const git_status_entry& entry)
    {
        if (entry.index_to_workdir != nullptr && entry.index_to_workdir->new_file.path != nullptr)
            return juce::String::fromUTF8 (entry.index_to_workdir->new_file.path);

        if (entry.head_to_index != nullptr && entry.head_to_index->new_file.path != nullptr)
            return juce::String::fromUTF8 (entry.head_to_index->new_file.path);

        return {};
    }

    Result cmdStatus (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository (or any of the parent directories): .git");

        git_status_options options = GIT_STATUS_OPTIONS_INIT;
        options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
        options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
                      | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
                      | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX
                      | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR;

        StatusList list;

        if (git_status_list_new (&list, repo, &options) != 0)
            return context.failFromLibrary ("could not read the status");

        const auto count = git_status_list_entrycount (list);
        const auto shortFormat = args.hasAny ({ "-s", "--short", "--porcelain" });
        const auto branch = currentBranchName (repo);

        if (shortFormat)
        {
            for (size_t i = 0; i < count; ++i)
            {
                const auto* entry = git_status_byindex (list, i);
                context.out << statusCodes (entry->status) << " " << statusPath (*entry) << "\n";
            }

            return context.ok();
        }

        context.out << "On branch " << (branch.isNotEmpty() ? branch : juce::String ("(unborn)")) << "\n";

        juce::StringArray staged, unstaged, untracked;

        for (size_t i = 0; i < count; ++i)
        {
            const auto* entry = git_status_byindex (list, i);
            const auto path = statusPath (*entry);
            const auto status = entry->status;

            if ((status & GIT_STATUS_WT_NEW) != 0)
                untracked.add ("\t" + path);
            else if ((status & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_INDEX_DELETED
                                | GIT_STATUS_INDEX_RENAMED | GIT_STATUS_INDEX_TYPECHANGE)) != 0)
                staged.add ("\t" + statusCodes (status).substring (0, 1) + " " + path);

            if ((status & (GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED
                           | GIT_STATUS_WT_RENAMED | GIT_STATUS_WT_TYPECHANGE)) != 0)
                unstaged.add ("\t" + statusCodes (status).substring (1, 2) + " " + path);
        }

        if (! staged.isEmpty())
            context.out << "\nChanges to be committed:\n" << staged.joinIntoString ("\n") << "\n";

        if (! unstaged.isEmpty())
            context.out << "\nChanges not staged for commit:\n" << unstaged.joinIntoString ("\n") << "\n";

        if (! untracked.isEmpty())
            context.out << "\nUntracked files:\n" << untracked.joinIntoString ("\n") << "\n";

        if (staged.isEmpty() && unstaged.isEmpty() && untracked.isEmpty())
            context.out << "nothing to commit, working tree clean";

        return context.ok();
    }

    //==============================================================================
    Result cmdAdd (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        Index index;

        if (git_repository_index (&index, repo) != 0)
            return context.failFromLibrary ("could not open the index");

        auto paths = args.positional;

        if (args.hasAny ({ "-A", "--all", "-u", "--update" }) && paths.isEmpty())
            paths.add (".");

        if (paths.isEmpty())
            return context.fail ("nothing specified, nothing added.");

        std::vector<char*> raw;
        std::vector<juce::String> storage (paths.begin(), paths.end());

        for (auto& path : storage)
            raw.push_back (const_cast<char*> (path.toRawUTF8()));

        git_strarray pathspec { raw.data(), raw.size() };
        const auto updateOnly = args.hasAny ({ "-u", "--update" });

        const auto status = updateOnly ? git_index_update_all (index, &pathspec, nullptr, nullptr)
                                       : git_index_add_all (index, &pathspec, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr);

        if (status != 0)
            return context.failFromLibrary ("could not add files");

        if (git_index_write (index) != 0)
            return context.failFromLibrary ("could not write the index");

        context.out << "Staged " << paths.joinIntoString (" ");
        return context.ok();
    }

    Result cmdRm (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});

        if (args.positional.isEmpty())
            return context.fail ("usage: git rm [--cached] <path>...");

        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        Index index;

        if (git_repository_index (&index, repo) != 0)
            return context.failFromLibrary ("could not open the index");

        const auto cachedOnly = args.has ("--cached");

        for (const auto& path : args.positional)
        {
            if (git_index_remove_bypath (index, path.toRawUTF8()) != 0)
                return context.failFromLibrary ("could not remove '" + path + "' from the index");

            if (! cachedOnly)
            {
                juce::File file;

                if (resolveInsideWorkingDirectory (context, path, file) && file.existsAsFile())
                    file.deleteFile();
            }

            context.out << "rm '" << path << "'\n";
        }

        if (git_index_write (index) != 0)
            return context.failFromLibrary ("could not write the index");

        return context.ok();
    }

    Result cmdMv (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});

        if (args.positional.size() < 2)
            return context.fail ("usage: git mv <source> <destination>");

        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        juce::File source, destination;

        if (! resolveInsideWorkingDirectory (context, args.positional[0], source)
            || ! resolveInsideWorkingDirectory (context, args.positional[1], destination))
            return context.fail ("Both paths must stay inside the working directory.");

        if (! source.exists())
            return context.fail ("bad source '" + args.positional[0] + "'");

        if (destination.isDirectory())
            destination = destination.getChildFile (source.getFileName());

        if (! source.moveFileTo (destination))
            return context.fail ("could not move '" + args.positional[0] + "'");

        Index index;

        if (git_repository_index (&index, repo) != 0)
            return context.failFromLibrary ("could not open the index");

        git_index_remove_bypath (index, args.positional[0].toRawUTF8());

        const auto relative = destination.getRelativePathFrom (context.workingDirectory);

        if (git_index_add_bypath (index, relative.toRawUTF8()) != 0)
            return context.failFromLibrary ("could not stage '" + relative + "'");

        if (git_index_write (index) != 0)
            return context.failFromLibrary ("could not write the index");

        context.out << "Renamed " << args.positional[0] << " -> " << relative;
        return context.ok();
    }

    //==============================================================================
    Result cmdCommit (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, { "-m", "--message" });
        const auto message = args.value ("-m", args.value ("--message"));

        if (message.isEmpty())
            return context.fail ("A commit message is required: git commit -m \"...\"");

        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        Index index;

        if (git_repository_index (&index, repo) != 0)
            return context.failFromLibrary ("could not open the index");

        if (args.hasAny ({ "-a", "--all" }))
        {
            char all[] = "*";
            char* specs[] = { all };
            git_strarray pathspec { specs, 1 };

            if (git_index_update_all (index, &pathspec, nullptr, nullptr) != 0)
                return context.failFromLibrary ("could not stage tracked changes");

            if (git_index_write (index) != 0)
                return context.failFromLibrary ("could not write the index");
        }

        git_oid treeId {};

        if (git_index_write_tree (&treeId, index) != 0)
            return context.failFromLibrary ("could not write a tree from the index");

        Tree tree;

        if (git_tree_lookup (&tree, repo, &treeId) != 0)
            return context.failFromLibrary ("could not look up the tree");

        Signature signature;

        if (git_signature_default (&signature, repo) != 0)
            return context.fail ("The committer identity is not configured. Run:\n"
                                 "  git config --global user.name \"Your Name\"\n"
                                 "  git config --global user.email \"you@example.com\"");

        const auto amend = args.has ("--amend");
        Object headObject;
        const auto haveHead = git_revparse_single (&headObject, repo, "HEAD") == 0;

        Commit headCommit;

        if (haveHead && git_commit_lookup (&headCommit, repo, git_object_id (headObject)) != 0)
            return context.failFromLibrary ("could not look up HEAD");

        git_oid commitId {};
        int status = 0;

        if (amend)
        {
            if (! haveHead)
                return context.fail ("There is nothing to amend.");

            status = git_commit_amend (&commitId, headCommit, "HEAD", signature, signature,
                                       nullptr, message.toRawUTF8(), tree);
        }
        else
        {
            const git_commit* parents[] = { headCommit.get() };

            status = git_commit_create (&commitId, repo, "HEAD", signature, signature,
                                        nullptr, message.toRawUTF8(), tree,
                                        haveHead ? 1 : 0,
                                        haveHead ? parents : nullptr);
        }

        if (status != 0)
            return context.failFromLibrary ("could not create the commit");

        const auto branch = currentBranchName (repo);
        context.out << "[" << (branch.isNotEmpty() ? branch : juce::String ("detached")) << " "
                    << shortId (commitId) << "] " << message.upToFirstOccurrenceOf ("\n", false, false);
        return context.ok();
    }

    //==============================================================================
    Result cmdLog (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, { "-n", "--max-count" });
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        RevWalk walk;

        if (git_revwalk_new (&walk, repo) != 0)
            return context.failFromLibrary ("could not walk the history");

        git_revwalk_sorting (walk, GIT_SORT_TIME);

        if (args.positional.isEmpty())
        {
            if (git_revwalk_push_head (walk) != 0)
                return context.fail ("your current branch does not have any commits yet");
        }
        else
        {
            Object target;

            if (git_revparse_single (&target, repo, args.positional[0].toRawUTF8()) != 0)
                return context.failFromLibrary ("unknown revision '" + args.positional[0] + "'");

            if (git_revwalk_push (walk, git_object_id (target)) != 0)
                return context.failFromLibrary ("could not walk from that revision");
        }

        auto limit = args.value ("-n", args.value ("--max-count")).getIntValue();

        if (limit <= 0)
            limit = args.hasValue ("-n") || args.hasValue ("--max-count") ? 1 : 50;

        const auto oneline = args.has ("--oneline");
        git_oid id {};
        int printed = 0;

        while (printed < limit && git_revwalk_next (&id, walk) == 0)
        {
            Commit commit;

            if (git_commit_lookup (&commit, repo, &id) != 0)
                break;

            const auto* author = git_commit_author (commit);
            const auto summary = juce::String::fromUTF8 (git_commit_summary (commit) != nullptr
                                                            ? git_commit_summary (commit) : "");

            if (oneline)
            {
                context.out << shortId (id) << " " << summary << "\n";
            }
            else
            {
                context.out << "commit " << fullId (id) << "\n"
                            << "Author: " << juce::String::fromUTF8 (author->name)
                            << " <" << juce::String::fromUTF8 (author->email) << ">\n"
                            << "Date:   " << formatGitTime (author->when.time, author->when.offset) << "\n\n";

                const auto body = juce::String::fromUTF8 (git_commit_message (commit) != nullptr
                                                             ? git_commit_message (commit) : "");

                for (const auto& line : juce::StringArray::fromLines (body.trimEnd()))
                    context.out << "    " << line << "\n";

                context.out << "\n";
            }

            ++printed;
        }

        if (printed == 0)
            context.out << "(no commits)";

        return context.ok();
    }

    Result cmdDiff (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        git_diff_options options = GIT_DIFF_OPTIONS_INIT;
        std::vector<char*> raw;
        std::vector<juce::String> storage (args.positional.begin(), args.positional.end());

        for (auto& path : storage)
            raw.push_back (const_cast<char*> (path.toRawUTF8()));

        if (! raw.empty())
        {
            options.pathspec.strings = raw.data();
            options.pathspec.count = raw.size();
        }

        Diff diff;
        const auto staged = args.hasAny ({ "--cached", "--staged" });

        if (staged)
        {
            Object headTree;
            Tree tree;

            if (git_revparse_single (&headTree, repo, "HEAD^{tree}") == 0)
                git_tree_lookup (&tree, repo, git_object_id (headTree));

            if (git_diff_tree_to_index (&diff, repo, tree.get(), nullptr, &options) != 0)
                return context.failFromLibrary ("could not diff the index");
        }
        else
        {
            if (git_diff_index_to_workdir (&diff, repo, nullptr, &options) != 0)
                return context.failFromLibrary ("could not diff the working tree");
        }

        if (args.has ("--stat"))
        {
            DiffStats stats;

            if (git_diff_get_stats (&stats, diff) != 0)
                return context.failFromLibrary ("could not summarise the diff");

            Buffer buffer;

            if (git_diff_stats_to_buf (&buffer.buf, stats, GIT_DIFF_STATS_FULL, 0) != 0)
                return context.failFromLibrary ("could not format the summary");

            context.out << buffer.toString();
            return context.ok();
        }

        if (args.hasAny ({ "--name-only", "--name-status" }))
        {
            for (size_t i = 0, count = git_diff_num_deltas (diff); i < count; ++i)
            {
                const auto* delta = git_diff_get_delta (diff, i);
                context.out << juce::String::fromUTF8 (delta->new_file.path) << "\n";
            }

            return context.ok();
        }

        if (git_diff_print (diff, GIT_DIFF_FORMAT_PATCH, diffLineCallback, &context.out) != 0)
            return context.failFromLibrary ("could not print the diff");

        if (context.out.isEmpty())
            context.out << "(no changes)";

        return context.ok();
    }

    Result cmdShow (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        const auto spec = args.positional.isEmpty() ? juce::String ("HEAD") : args.positional[0];
        Object target;

        if (git_revparse_single (&target, repo, spec.toRawUTF8()) != 0)
            return context.failFromLibrary ("unknown revision '" + spec + "'");

        Commit commit;

        if (git_commit_lookup (&commit, repo, git_object_id (target)) != 0)
            return context.fail ("'" + spec + "' does not point at a commit.");

        const auto* author = git_commit_author (commit);
        context.out << "commit " << fullId (*git_commit_id (commit)) << "\n"
                    << "Author: " << juce::String::fromUTF8 (author->name)
                    << " <" << juce::String::fromUTF8 (author->email) << ">\n"
                    << "Date:   " << formatGitTime (author->when.time, author->when.offset) << "\n\n";

        for (const auto& line : juce::StringArray::fromLines (juce::String::fromUTF8 (git_commit_message (commit)).trimEnd()))
            context.out << "    " << line << "\n";

        context.out << "\n";

        Tree commitTree, parentTree;

        if (git_commit_tree (&commitTree, commit) != 0)
            return context.failFromLibrary ("could not read the commit tree");

        if (git_commit_parentcount (commit) > 0)
        {
            Commit parent;

            if (git_commit_parent (&parent, commit, 0) == 0)
                git_commit_tree (&parentTree, parent);
        }

        git_diff_options options = GIT_DIFF_OPTIONS_INIT;
        Diff diff;

        if (git_diff_tree_to_tree (&diff, repo, parentTree.get(), commitTree.get(), &options) != 0)
            return context.failFromLibrary ("could not diff the commit");

        git_diff_print (diff, GIT_DIFF_FORMAT_PATCH, diffLineCallback, &context.out);
        return context.ok();
    }

    //==============================================================================
    Result cmdBranch (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        if (args.hasAny ({ "-d", "-D", "--delete" }))
        {
            if (args.positional.isEmpty())
                return context.fail ("usage: git branch -d <name>");

            Reference branch;

            if (git_branch_lookup (&branch, repo, args.positional[0].toRawUTF8(), GIT_BRANCH_LOCAL) != 0)
                return context.fail ("branch '" + args.positional[0] + "' not found.");

            if (git_branch_delete (branch) != 0)
                return context.failFromLibrary ("could not delete the branch");

            context.out << "Deleted branch " << args.positional[0];
            return context.ok();
        }

        if (! args.positional.isEmpty())
        {
            Object head;

            if (git_revparse_single (&head, repo, args.positional.size() > 1 ? args.positional[1].toRawUTF8() : "HEAD") != 0)
                return context.failFromLibrary ("could not resolve the starting point");

            Commit target;

            if (git_commit_lookup (&target, repo, git_object_id (head)) != 0)
                return context.failFromLibrary ("the starting point is not a commit");

            Reference created;

            if (git_branch_create (&created, repo, args.positional[0].toRawUTF8(), target, 0) != 0)
                return context.failFromLibrary ("could not create the branch");

            context.out << "Created branch " << args.positional[0];
            return context.ok();
        }

        const auto listAll = args.hasAny ({ "-a", "--all", "-r", "--remotes" });
        const auto remotesOnly = args.hasAny ({ "-r", "--remotes" });
        BranchIter iterator;

        if (git_branch_iterator_new (&iterator, repo,
                                     remotesOnly ? GIT_BRANCH_REMOTE
                                                 : (listAll ? GIT_BRANCH_ALL : GIT_BRANCH_LOCAL)) != 0)
            return context.failFromLibrary ("could not list branches");

        const auto current = currentBranchName (repo);
        git_reference* rawReference = nullptr;
        git_branch_t type {};

        while (git_branch_next (&rawReference, &type, iterator) == 0)
        {
            Reference reference;
            reference.ptr = rawReference;
            rawReference = nullptr;

            const char* name = nullptr;
            git_branch_name (&name, reference);
            const auto branchName = juce::String::fromUTF8 (name != nullptr ? name : "");

            context.out << (branchName == current ? "* " : "  ") << branchName << "\n";
        }

        return context.ok();
    }

    Result cmdCheckout (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, { "-b", "-c" });
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        auto target = args.value ("-b", args.value ("-c"));
        const auto creating = target.isNotEmpty();

        if (! creating)
        {
            if (args.positional.isEmpty())
                return context.fail ("usage: git checkout [-b] <branch|revision>");

            target = args.positional[0];
        }

        if (creating)
        {
            Object start;
            const auto startSpec = args.positional.isEmpty() ? juce::String ("HEAD") : args.positional[0];

            if (git_revparse_single (&start, repo, startSpec.toRawUTF8()) != 0)
                return context.failFromLibrary ("could not resolve '" + startSpec + "'");

            Commit startCommit;

            if (git_commit_lookup (&startCommit, repo, git_object_id (start)) != 0)
                return context.failFromLibrary ("the starting point is not a commit");

            Reference created;

            if (git_branch_create (&created, repo, target.toRawUTF8(), startCommit, 0) != 0)
                return context.failFromLibrary ("could not create branch '" + target + "'");
        }

        Object object;
        Reference reference;

        if (git_revparse_ext (&object, &reference, repo, target.toRawUTF8()) != 0)
            return context.failFromLibrary ("pathspec '" + target + "' did not match any file(s) known to git");

        git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
        options.checkout_strategy = GIT_CHECKOUT_SAFE;

        if (git_checkout_tree (repo, object, &options) != 0)
            return context.failFromLibrary ("could not check out");

        const auto status = reference.get() != nullptr
                              ? git_repository_set_head (repo, git_reference_name (reference))
                              : git_repository_set_head_detached (repo, git_object_id (object));

        if (status != 0)
            return context.failFromLibrary ("could not move HEAD");

        context.out << (creating ? "Switched to a new branch '" : "Switched to '") << target << "'";
        return context.ok();
    }

    Result cmdReset (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        const auto spec = args.positional.isEmpty() ? juce::String ("HEAD") : args.positional[0];
        Object target;

        if (git_revparse_single (&target, repo, spec.toRawUTF8()) != 0)
            return context.failFromLibrary ("unknown revision '" + spec + "'");

        auto mode = GIT_RESET_MIXED;

        if (args.has ("--soft"))     mode = GIT_RESET_SOFT;
        if (args.has ("--hard"))     mode = GIT_RESET_HARD;

        git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;

        if (git_reset (repo, target, mode, &options) != 0)
            return context.failFromLibrary ("could not reset");

        context.out << "HEAD is now at " << shortId (*git_object_id (target));
        return context.ok();
    }

    Result cmdRevParse (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        if (args.has ("--abbrev-ref") && args.positional.contains ("HEAD"))
        {
            context.out << currentBranchName (repo);
            return context.ok();
        }

        const auto spec = args.positional.isEmpty() ? juce::String ("HEAD") : args.positional[0];
        Object target;

        if (git_revparse_single (&target, repo, spec.toRawUTF8()) != 0)
            return context.failFromLibrary ("unknown revision '" + spec + "'");

        context.out << (args.has ("--short") ? shortId (*git_object_id (target))
                                             : fullId (*git_object_id (target)));
        return context.ok();
    }

    Result cmdLsFiles (Context& context, const juce::StringArray&)
    {
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        Index index;

        if (git_repository_index (&index, repo) != 0)
            return context.failFromLibrary ("could not open the index");

        for (size_t i = 0, count = git_index_entrycount (index); i < count; ++i)
            if (const auto* entry = git_index_get_byindex (index, i); entry != nullptr)
                context.out << juce::String::fromUTF8 (entry->path) << "\n";

        return context.ok();
    }

    Result cmdTag (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        if (args.hasAny ({ "-d", "--delete" }))
        {
            if (args.positional.isEmpty())
                return context.fail ("usage: git tag -d <name>");

            if (git_tag_delete (repo, args.positional[0].toRawUTF8()) != 0)
                return context.failFromLibrary ("could not delete the tag");

            context.out << "Deleted tag '" << args.positional[0] << "'";
            return context.ok();
        }

        if (! args.positional.isEmpty())
        {
            const auto spec = args.positional.size() > 1 ? args.positional[1] : juce::String ("HEAD");
            Object target;

            if (git_revparse_single (&target, repo, spec.toRawUTF8()) != 0)
                return context.failFromLibrary ("unknown revision '" + spec + "'");

            git_oid id {};

            if (git_tag_create_lightweight (&id, repo, args.positional[0].toRawUTF8(), target, 0) != 0)
                return context.failFromLibrary ("could not create the tag");

            context.out << "Created tag " << args.positional[0];
            return context.ok();
        }

        git_strarray tags {};

        if (git_tag_list (&tags, repo) != 0)
            return context.failFromLibrary ("could not list tags");

        for (size_t i = 0; i < tags.count; ++i)
            context.out << juce::String::fromUTF8 (tags.strings[i]) << "\n";

        git_strarray_dispose (&tags);
        return context.ok();
    }

    //==============================================================================
    Result cmdRemote (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        const auto subcommand = args.positional.isEmpty() ? juce::String() : args.positional[0];

        if (subcommand == "add")
        {
            if (args.positional.size() < 3)
                return context.fail ("usage: git remote add <name> <url>");

            Remote remote;

            if (git_remote_create (&remote, repo, args.positional[1].toRawUTF8(), args.positional[2].toRawUTF8()) != 0)
                return context.failFromLibrary ("could not add the remote");

            return context.ok();
        }

        if (subcommand == "remove" || subcommand == "rm")
        {
            if (args.positional.size() < 2)
                return context.fail ("usage: git remote remove <name>");

            if (git_remote_delete (repo, args.positional[1].toRawUTF8()) != 0)
                return context.failFromLibrary ("could not remove the remote");

            return context.ok();
        }

        if (subcommand == "set-url")
        {
            if (args.positional.size() < 3)
                return context.fail ("usage: git remote set-url <name> <url>");

            if (git_remote_set_url (repo, args.positional[1].toRawUTF8(), args.positional[2].toRawUTF8()) != 0)
                return context.failFromLibrary ("could not set the URL");

            return context.ok();
        }

        if (subcommand == "get-url")
        {
            if (args.positional.size() < 2)
                return context.fail ("usage: git remote get-url <name>");

            Remote remote;

            if (git_remote_lookup (&remote, repo, args.positional[1].toRawUTF8()) != 0)
                return context.failFromLibrary ("no such remote");

            context.out << juce::String::fromUTF8 (git_remote_url (remote));
            return context.ok();
        }

        git_strarray remotes {};

        if (git_remote_list (&remotes, repo) != 0)
            return context.failFromLibrary ("could not list remotes");

        const auto verbose = args.hasAny ({ "-v", "--verbose" });

        for (size_t i = 0; i < remotes.count; ++i)
        {
            const auto name = juce::String::fromUTF8 (remotes.strings[i]);

            if (! verbose)
            {
                context.out << name << "\n";
                continue;
            }

            Remote remote;

            if (git_remote_lookup (&remote, repo, remotes.strings[i]) == 0)
            {
                const auto url = juce::String::fromUTF8 (git_remote_url (remote));
                context.out << name << "\t" << url << " (fetch)\n"
                            << name << "\t" << url << " (push)\n";
            }
        }

        git_strarray_dispose (&remotes);
        return context.ok();
    }

    /*  リモート操作の共通部分。名前が省略されたら origin、上流が設定されて
        いればそちらを使う。 */
    juce::String remoteNameOrDefault (git_repository* repo, const juce::StringArray& positional)
    {
        if (! positional.isEmpty() && ! positional[0].startsWith ("-"))
            return positional[0];

        juce::ignoreUnused (repo);
        return "origin";
    }

    Result cmdFetch (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        const auto remoteName = remoteNameOrDefault (repo, args.positional);
        Remote remote;

        if (git_remote_lookup (&remote, repo, remoteName.toRawUTF8()) != 0)
            return context.fail ("'" + remoteName + "' does not appear to be a git repository");

        NetworkPayload payload { context.cancelFlag, {} };
        git_fetch_options options = GIT_FETCH_OPTIONS_INIT;
        installNetworkCallbacks (options.callbacks, payload);

        if (git_remote_fetch (remote, nullptr, &options, "fetch") != 0)
            return context.failFromLibrary (payload.credentialError.isNotEmpty() ? payload.credentialError
                                                                                 : juce::String ("could not fetch"));

        context.out << "Fetched from " << remoteName;
        return context.ok();
    }

    /*  fast-forward だけを行う。分岐している場合は素直に断る。 */
    Result fastForwardTo (Context& context, git_repository* repo, git_annotated_commit* incoming, const juce::String& label)
    {
        git_merge_analysis_t analysis {};
        git_merge_preference_t preference {};
        const git_annotated_commit* heads[] = { incoming };

        if (git_merge_analysis (&analysis, &preference, repo, heads, 1) != 0)
            return context.failFromLibrary ("could not analyse the merge");

        if ((analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) != 0)
        {
            context.out << "Already up to date.";
            return context.ok();
        }

        if ((analysis & GIT_MERGE_ANALYSIS_FASTFORWARD) == 0
            && (analysis & GIT_MERGE_ANALYSIS_UNBORN) == 0)
            return context.fail ("The branches have diverged. Only fast-forward merges are supported on iOS. "
                                 "Rebase or merge on a desktop machine, or reset to the remote branch.");

        Object target;

        if (git_object_lookup (&target, repo, git_annotated_commit_id (incoming), GIT_OBJECT_COMMIT) != 0)
            return context.failFromLibrary ("could not look up the incoming commit");

        git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
        options.checkout_strategy = GIT_CHECKOUT_SAFE;

        if (git_checkout_tree (repo, target, &options) != 0)
            return context.failFromLibrary ("could not check out the incoming tree");

        Reference head, updated;

        if (git_repository_head (&head, repo) == 0)
        {
            if (git_reference_set_target (&updated, head, git_annotated_commit_id (incoming), "pull: Fast-forward") != 0)
                return context.failFromLibrary ("could not move the branch");
        }
        else if (git_repository_set_head_detached_from_annotated (repo, incoming) != 0)
        {
            return context.failFromLibrary ("could not move HEAD");
        }

        context.out << "Fast-forwarded " << label << " to "
                    << shortId (*git_annotated_commit_id (incoming));
        return context.ok();
    }

    Result cmdMerge (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});

        if (args.positional.isEmpty())
            return context.fail ("usage: git merge <branch>");

        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        Object target;

        if (git_revparse_single (&target, repo, args.positional[0].toRawUTF8()) != 0)
            return context.failFromLibrary ("unknown revision '" + args.positional[0] + "'");

        Annotated incoming;

        if (git_annotated_commit_lookup (&incoming, repo, git_object_id (target)) != 0)
            return context.failFromLibrary ("could not prepare the merge");

        return fastForwardTo (context, repo, incoming, currentBranchName (repo));
    }

    Result cmdPull (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        const auto fetchResult = cmdFetch (context, argv);

        if (fetchResult.exitCode != 0)
            return fetchResult;

        context.out << "\n";

        Reference head;

        if (git_repository_head (&head, repo) != 0)
            return context.fail ("your current branch does not have any commits yet");

        Reference upstream;
        juce::String upstreamName;

        if (git_branch_upstream (&upstream, head) == 0)
        {
            upstreamName = juce::String::fromUTF8 (git_reference_name (upstream));
        }
        else
        {
            // 上流が未設定なら "<remote>/<現在のブランチ>" を見に行く。
            const auto remoteName = remoteNameOrDefault (repo, args.positional);
            const auto candidate = "refs/remotes/" + remoteName + "/" + currentBranchName (repo);

            if (git_reference_lookup (&upstream, repo, candidate.toRawUTF8()) != 0)
                return context.fail ("There is no tracking information for the current branch. "
                                     "Set one with: git branch --set-upstream-to=" + remoteName + "/<branch>");

            upstreamName = candidate;
        }

        Annotated incoming;

        if (git_annotated_commit_from_ref (&incoming, repo, upstream) != 0)
            return context.failFromLibrary ("could not read " + upstreamName);

        return fastForwardTo (context, repo, incoming, currentBranchName (repo));
    }

    Result cmdPush (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        const auto remoteName = remoteNameOrDefault (repo, args.positional);
        Remote remote;

        if (git_remote_lookup (&remote, repo, remoteName.toRawUTF8()) != 0)
            return context.fail ("'" + remoteName + "' does not appear to be a git repository");

        const auto branch = currentBranchName (repo);

        if (branch.isEmpty())
            return context.fail ("HEAD is detached, so there is nothing to push.");

        auto refspec = args.positional.size() > 1 ? args.positional[1] : branch;

        if (! refspec.contains (":"))
            refspec = "refs/heads/" + refspec + ":refs/heads/" + refspec;

        if (args.hasAny ({ "-f", "--force" }))
            refspec = "+" + refspec;

        NetworkPayload payload { context.cancelFlag, {} };
        git_push_options options = GIT_PUSH_OPTIONS_INIT;
        installNetworkCallbacks (options.callbacks, payload);

        auto specStorage = refspec;
        char* specs[] = { const_cast<char*> (specStorage.toRawUTF8()) };
        git_strarray refspecs { specs, 1 };

        if (git_remote_push (remote, &refspecs, &options) != 0)
            return context.failFromLibrary (payload.credentialError.isNotEmpty() ? payload.credentialError
                                                                                 : juce::String ("could not push"));

        if (args.hasAny ({ "-u", "--set-upstream" }))
        {
            Reference localBranch;

            if (git_branch_lookup (&localBranch, repo, branch.toRawUTF8(), GIT_BRANCH_LOCAL) == 0)
                git_branch_set_upstream (localBranch, (remoteName + "/" + branch).toRawUTF8());
        }

        context.out << "Pushed " << refspec << " to " << remoteName;
        return context.ok();
    }

    //==============================================================================
    Result cmdConfig (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        const auto global = args.has ("--global");

        Config config;
        Repo repo;

        if (! global && openRepo (context, repo))
        {
            if (git_repository_config (&config, repo) != 0)
                return context.failFromLibrary ("could not open the configuration");
        }
        else if (git_config_open_default (&config) != 0)
        {
            return context.failFromLibrary ("could not open the configuration");
        }

        Config level;

        if (global)
        {
            if (git_config_open_level (&level, config, GIT_CONFIG_LEVEL_GLOBAL) != 0)
            {
                /*  ~/.gitconfig がまだ無い環境ではレベルを開けない。空の
                    ファイルを作って開き直す。iOS では HOME がアプリの
                    コンテナなので、ここは常に書ける。 */
                const auto path = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                    .getChildFile (".gitconfig");
                path.create();

                if (git_config_open_ondisk (&level, path.getFullPathName().toRawUTF8()) != 0)
                    return context.failFromLibrary ("could not open the global configuration");
            }
        }

        auto* target = global ? level.get() : config.get();

        if (args.hasAny ({ "-l", "--list" }))
        {
            git_config_iterator* rawIterator = nullptr;

            if (git_config_iterator_new (&rawIterator, target) != 0)
                return context.failFromLibrary ("could not list the configuration");

            git_config_entry* entry = nullptr;

            while (git_config_next (&entry, rawIterator) == 0)
                context.out << juce::String::fromUTF8 (entry->name) << "="
                            << juce::String::fromUTF8 (entry->value) << "\n";

            git_config_iterator_free (rawIterator);
            return context.ok();
        }

        if (args.positional.isEmpty())
            return context.fail ("usage: git config [--global] <name> [value]");

        const auto name = args.positional[0];

        if (args.has ("--unset"))
        {
            if (git_config_delete_entry (target, name.toRawUTF8()) != 0)
                return context.failFromLibrary ("could not unset '" + name + "'");

            return context.ok();
        }

        if (args.positional.size() > 1)
        {
            if (git_config_set_string (target, name.toRawUTF8(), args.positional[1].toRawUTF8()) != 0)
                return context.failFromLibrary ("could not set '" + name + "'");

            return context.ok();
        }

        Buffer buffer;

        if (git_config_get_string_buf (&buffer.buf, target, name.toRawUTF8()) != 0)
            return { 1, context.out };     // 実際の git も未設定なら無出力で終了コード 1

        context.out << buffer.toString();
        return context.ok();
    }

    /*  Projucer 独自。iOS には credential helper を動かすシェルが無いので、
        HTTPS のトークンを Keychain へ入れる口をサブコマンドとして持つ。 */
    Result cmdCredential (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        const auto subcommand = args.positional.isEmpty() ? juce::String() : args.positional[0];

        if (subcommand == "set")
        {
            if (args.positional.size() < 4)
                return context.fail ("usage: git credential set <host> <username> <token>");

            const auto host = args.positional[1].toLowerCase();

            if (! keychainWrite (credentialService, host, args.positional[2] + "\n" + args.positional[3]))
                return context.fail ("could not store the token in the Keychain");

            context.out << "Stored a token for " << host << ".";
            return context.ok();
        }

        if (subcommand == "erase")
        {
            if (args.positional.size() < 2)
                return context.fail ("usage: git credential erase <host>");

            keychainErase (credentialService, args.positional[1].toLowerCase());
            context.out << "Removed the token for " << args.positional[1].toLowerCase() << ".";
            return context.ok();
        }

        if (subcommand == "show")
        {
            if (args.positional.size() < 2)
                return context.fail ("usage: git credential show <host>");

            const auto credential = readCredential (args.positional[1].toLowerCase());
            context.out << (credential.isValid() ? "A token is stored for " + args.positional[1].toLowerCase()
                                                     + " (username: " + credential.username + ")."
                                                 : juce::String ("No token is stored for ") + args.positional[1] + ".");
            return context.ok();
        }

        return context.fail ("usage: git credential set|erase|show <host> ...");
    }

    //==============================================================================
    int collectSubmoduleName (git_submodule*, const char* name, void* payload)
    {
        static_cast<juce::StringArray*> (payload)->add (juce::String::fromUTF8 (name));
        return 0;
    }

    juce::StringArray submoduleNames (git_repository* repo)
    {
        juce::StringArray names;
        git_submodule_foreach (repo, collectSubmoduleName, &names);
        return names;
    }

    /*  引数で絞られていればその名前だけ、無ければ全部を返す。git は path でも
        name でも受け付けるので、lookup に通るかどうかで判断する。 */
    juce::StringArray selectedSubmodules (git_repository* repo, const juce::StringArray& requested)
    {
        if (requested.isEmpty())
            return submoduleNames (repo);

        juce::StringArray names;

        for (const auto& request : requested)
        {
            Submodule submodule;

            if (git_submodule_lookup (&submodule, repo, request.toRawUTF8()) == 0)
                names.add (juce::String::fromUTF8 (git_submodule_name (submodule)));
        }

        return names;
    }

    juce::String submoduleStatusLine (git_repository* repo, const juce::String& name)
    {
        Submodule submodule;

        if (git_submodule_lookup (&submodule, repo, name.toRawUTF8()) != 0)
            return {};

        unsigned int status = 0;
        git_submodule_status (&status, repo, name.toRawUTF8(), GIT_SUBMODULE_IGNORE_UNSPECIFIED);

        const auto* id = git_submodule_wd_id (submodule) != nullptr ? git_submodule_wd_id (submodule)
                                                                    : git_submodule_index_id (submodule);

        juce::String prefix (" ");

        if ((status & GIT_SUBMODULE_STATUS_WD_UNINITIALIZED) != 0)      prefix = "-";
        else if ((status & GIT_SUBMODULE_STATUS_WD_MODIFIED) != 0)      prefix = "+";

        juce::String line;
        line << prefix << (id != nullptr ? fullId (*id) : juce::String ("0000000000000000000000000000000000000000"))
             << " " << juce::String::fromUTF8 (git_submodule_path (submodule));

        if (const auto* branch = git_submodule_branch (submodule); branch != nullptr)
            line << " (" << juce::String::fromUTF8 (branch) << ")";

        return line;
    }

    /*  update は入れ子になったサブモジュールへも降りられるようにする。
        --recursive のときだけ、クローンした先で同じことを繰り返す。 */
    bool updateSubmodules (git_repository* repo,
                           const juce::StringArray& requested,
                           bool initialise,
                           bool recursive,
                           NetworkPayload& payload,
                           juce::String& out,
                           juce::String& errorOut)
    {
        for (const auto& name : selectedSubmodules (repo, requested))
        {
            Submodule submodule;

            if (git_submodule_lookup (&submodule, repo, name.toRawUTF8()) != 0)
            {
                errorOut = "could not look up submodule '" + name + "': " + lastError();
                return false;
            }

            git_submodule_update_options options = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
            installNetworkCallbacks (options.fetch_opts.callbacks, payload);
            options.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;

            if (git_submodule_update (submodule, initialise ? 1 : 0, &options) != 0)
            {
                errorOut = payload.credentialError.isNotEmpty()
                             ? payload.credentialError
                             : "could not update submodule '" + name + "': " + lastError();
                return false;
            }

            out << "Submodule path '" << juce::String::fromUTF8 (git_submodule_path (submodule))
                << "': checked out\n";

            if (! recursive)
                continue;

            Repo nested;

            if (git_submodule_open (&nested, submodule) != 0)
                continue;

            if (! updateSubmodules (nested, {}, initialise, true, payload, out, errorOut))
                return false;
        }

        return true;
    }

    Result cmdSubmodule (Context& context, const juce::StringArray& argv)
    {
        const auto args = parseArgs (argv, {});
        Repo repo;

        if (! openRepo (context, repo))
            return context.fail ("not a git repository");

        auto positional = args.positional;
        const auto subcommand = positional.isEmpty() ? juce::String ("status") : positional[0];

        if (! positional.isEmpty())
            positional.remove (0);

        if (subcommand == "status")
        {
            for (const auto& name : selectedSubmodules (repo, positional))
                context.out << submoduleStatusLine (repo, name) << "\n";

            if (context.out.isEmpty())
                context.out << "(no submodules)";

            return context.ok();
        }

        if (subcommand == "add")
        {
            if (positional.isEmpty())
                return context.fail ("usage: git submodule add <url> [path]");

            const auto url = positional[0];
            auto path = positional.size() > 1 ? positional[1]
                                              : url.fromLastOccurrenceOf ("/", false, false)
                                                   .upToLastOccurrenceOf (".git", false, false);

            if (path.isEmpty())
                return context.fail ("Could not work out the submodule path from the URL.");

            juce::File target;

            if (! resolveInsideWorkingDirectory (context, path, target))
                return context.fail ("The submodule path must stay inside the working directory.");

            Submodule submodule;

            if (git_submodule_add_setup (&submodule, repo, url.toRawUTF8(), path.toRawUTF8(), 1) != 0)
                return context.failFromLibrary ("could not add the submodule");

            NetworkPayload payload { context.cancelFlag, {} };
            git_submodule_update_options options = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
            installNetworkCallbacks (options.fetch_opts.callbacks, payload);

            Repo cloned;

            if (git_submodule_clone (&cloned, submodule, &options) != 0)
                return context.failFromLibrary (payload.credentialError.isNotEmpty() ? payload.credentialError
                                                                                     : juce::String ("could not clone the submodule"));

            if (git_submodule_add_finalize (submodule) != 0)
                return context.failFromLibrary ("could not finish adding the submodule");

            context.out << "Adding existing repo at '" << path << "' to the index";
            return context.ok();
        }

        if (subcommand == "init")
        {
            for (const auto& name : selectedSubmodules (repo, positional))
            {
                Submodule submodule;

                if (git_submodule_lookup (&submodule, repo, name.toRawUTF8()) != 0)
                    continue;

                if (git_submodule_init (submodule, 0) != 0)
                    return context.failFromLibrary ("could not initialise submodule '" + name + "'");

                context.out << "Submodule '" << name << "' ("
                            << juce::String::fromUTF8 (git_submodule_url (submodule)) << ") registered for path '"
                            << juce::String::fromUTF8 (git_submodule_path (submodule)) << "'\n";
            }

            if (context.out.isEmpty())
                context.out << "(no submodules)";

            return context.ok();
        }

        if (subcommand == "update")
        {
            NetworkPayload payload { context.cancelFlag, {} };
            juce::String error;

            if (! updateSubmodules (repo, positional,
                                    args.has ("--init"), args.has ("--recursive"),
                                    payload, context.out, error))
                return context.fail (error);

            if (context.out.isEmpty())
                context.out << "(no submodules)";

            return context.ok();
        }

        if (subcommand == "sync")
        {
            for (const auto& name : selectedSubmodules (repo, positional))
            {
                Submodule submodule;

                if (git_submodule_lookup (&submodule, repo, name.toRawUTF8()) != 0)
                    continue;

                if (git_submodule_sync (submodule) != 0)
                    return context.failFromLibrary ("could not sync submodule '" + name + "'");

                context.out << "Synchronizing submodule url for '" << name << "'\n";
            }

            return context.ok();
        }

        if (subcommand == "set-url")
        {
            if (positional.size() < 2)
                return context.fail ("usage: git submodule set-url <path> <url>");

            if (git_submodule_set_url (repo, positional[0].toRawUTF8(), positional[1].toRawUTF8()) != 0)
                return context.failFromLibrary ("could not set the submodule URL");

            return context.ok();
        }

        if (subcommand == "set-branch")
        {
            if (positional.size() < 2)
                return context.fail ("usage: git submodule set-branch <path> <branch>");

            if (git_submodule_set_branch (repo, positional[0].toRawUTF8(), positional[1].toRawUTF8()) != 0)
                return context.failFromLibrary ("could not set the submodule branch");

            return context.ok();
        }

        if (subcommand == "foreach")
            return context.fail ("git submodule foreach needs a shell, which iOS does not have. "
                                 "Run the command in each submodule with git -C <path> ... instead.");

        return context.fail ("git submodule " + subcommand + " is not supported yet.\n"
                             "Supported: status, add, init, update, sync, set-url, set-branch");
    }

    //==============================================================================
    using Handler = Result (*) (Context&, const juce::StringArray&);

    struct Command
    {
        const char* name;
        Handler handler;
    };

    const Command commands[] =
    {
        { "version",    cmdVersion },
        { "init",       cmdInit },
        { "clone",      cmdClone },
        { "status",     cmdStatus },
        { "add",        cmdAdd },
        { "rm",         cmdRm },
        { "mv",         cmdMv },
        { "commit",     cmdCommit },
        { "log",        cmdLog },
        { "diff",       cmdDiff },
        { "show",       cmdShow },
        { "branch",     cmdBranch },
        { "checkout",   cmdCheckout },
        { "switch",     cmdCheckout },
        { "reset",      cmdReset },
        { "rev-parse",  cmdRevParse },
        { "ls-files",   cmdLsFiles },
        { "tag",        cmdTag },
        { "remote",     cmdRemote },
        { "fetch",      cmdFetch },
        { "pull",       cmdPull },
        { "push",       cmdPush },
        { "merge",      cmdMerge },
        { "config",     cmdConfig },
        { "submodule",  cmdSubmodule },
        { "credential", cmdCredential }
    };
}

//==============================================================================
juce::StringArray supportedCommands()
{
    juce::StringArray names;

    for (const auto& command : commands)
        names.addIfNotAlreadyThere (command.name);

    return names;
}

bool isGitCommandLine (const juce::String& commandLine)
{
    const auto tokens = tokenise (commandLine);
    return ! tokens.isEmpty() && (tokens[0] == "git" || tokens[0].endsWith ("/git"));
}

Result run (const juce::StringArray& args, const juce::File& workingDirectory, std::atomic<bool>* cancelFlag)
{
    ensureLibrary();

    Context context { workingDirectory, cancelFlag, {} };

    if (! workingDirectory.isDirectory())
        return context.fail ("The working directory does not exist.");

    auto arguments = args;

    if (! arguments.isEmpty() && (arguments[0] == "git" || arguments[0].endsWith ("/git")))
        arguments.remove (0);

    /*  "git -C sub status" のような前置きオプションを先に食べる。 */
    while (! arguments.isEmpty() && arguments[0].startsWith ("-"))
    {
        if (arguments[0] == "-C" && arguments.size() > 1)
        {
            juce::File moved;

            if (! resolveInsideWorkingDirectory (context, arguments[1], moved) || ! moved.isDirectory())
                return context.fail ("The -C directory must be an existing directory inside the working directory.");

            context.workingDirectory = moved;
            arguments.removeRange (0, 2);
            continue;
        }

        if (arguments[0] == "--version")
        {
            arguments.set (0, "version");
            break;
        }

        // 挙動を変えない前置きオプションは黙って捨てる。
        arguments.remove (0);
    }

    if (arguments.isEmpty())
        return context.fail ("usage: git <command> [<args>]\nSupported commands: "
                             + supportedCommands().joinIntoString (", "));

    const auto name = arguments[0];
    arguments.remove (0);

    for (const auto& command : commands)
        if (name == command.name)
            return command.handler (context, arguments);

    return context.fail ("'" + name + "' is not supported by the built-in git yet.\n"
                         "Supported commands: " + supportedCommands().joinIntoString (", "));
}

Result runCommandLine (const juce::String& commandLine, const juce::File& workingDirectory, std::atomic<bool>* cancelFlag)
{
    return run (tokenise (commandLine), workingDirectory, cancelFlag);
}

}

#else

/*  libgit2 は Apple 向けにだけビルドしている。ほかのプラットフォームには
    本物の git があるので、そちらをシェルから使えばよい。 */
namespace ProjucerGit
{
    juce::StringArray supportedCommands()                                   { return {}; }

    bool isGitCommandLine (const juce::String& commandLine)
    {
        const auto first = commandLine.trim().upToFirstOccurrenceOf (" ", false, false);
        return first == "git" || first.endsWith ("/git");
    }

    Result run (const juce::StringArray&, const juce::File&, std::atomic<bool>*)
    {
        return { 1, "The built-in git is only available on macOS and iOS. Use exec_command instead." };
    }

    Result runCommandLine (const juce::String&, const juce::File&, std::atomic<bool>*)
    {
        return { 1, "The built-in git is only available on macOS and iOS. Use exec_command instead." };
    }
}

#endif
