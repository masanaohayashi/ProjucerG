#include "jucer_AiSessionStore.h"

#include <algorithm>

namespace
{
    constexpr int maximumTitleLength = 60;

    /*  見出しを作るのに読む量。meta 行と最初の発言はこの中に収まる。
        ponytail: 先頭に巨大な画像添付が来ると見出しが空になるが、
        そのときは「(no message)」で済ませる。全部読むのは高い。 */
    constexpr int headBytesToRead = 16 * 1024;

    juce::File sessionsFolderFor (const juce::File& projectRoot)
    {
        return projectRoot.getChildFile (".projucer").getChildFile ("ai-sessions");
    }

    bool appendRecord (const juce::File& file,
                       int ordinal,
                       const juce::String& type,
                       const juce::var& payload)
    {
        if (file == juce::File())
            return false;

        /*  直前の書き込みが改行の前で切れていることがある。そのまま足すと壊れた行と
            新しいレコードが 1 行に繋がり、読む側が両方まとめて捨てる。区切りを先に
            立て直して、失われるのを壊れた行だけに留める。 */
        if (const auto size = file.getSize(); size > 0)
        {
            juce::FileInputStream tail (file);

            if (! tail.openedOk() || ! tail.setPosition (size - 1))
                return false;

            if (tail.readByte() != '\n' && ! file.appendText ("\n"))
                return false;
        }

        auto* record = new juce::DynamicObject();
        record->setProperty ("ts", juce::Time::getCurrentTime().toISO8601 (true));
        record->setProperty ("n", ordinal);
        record->setProperty ("type", type);
        record->setProperty ("payload", payload);

        /*  ponytail: 1 行ごとに開いて閉じる。会話の速さならこれで十分速く、
            途中で落ちても書けた行までは必ず残る。まとめ書きが要るほど
            item が増えたらストリームを持ち回る。 */
        return file.appendText (juce::JSON::toString (juce::var (record), true) + "\n");
    }

    /*  1 行を封筒として読む。読めなければ type が空のまま返る。 */
    struct Record
    {
        int ordinal = 0;
        juce::String type;
        juce::var payload;
    };

    Record parseRecord (const juce::String& line)
    {
        if (line.trim().isEmpty())
            return {};

        const auto parsed = juce::JSON::parse (line);

        if (parsed.getDynamicObject() == nullptr)
            return {};

        return { static_cast<int> (parsed["n"]), parsed["type"].toString(), parsed["payload"] };
    }

    /*  message item の本文。content の各要素の text をつなぐ。 */
    juce::String textOfMessage (const juce::var& item)
    {
        juce::String text;

        if (const auto* parts = item["content"].getArray())
            for (const auto& part : *parts)
                text << part["text"].toString();

        return text;
    }

    juce::var makeUserMessage (const juce::String& text)
    {
        auto* part = new juce::DynamicObject();
        part->setProperty ("type", "input_text");
        part->setProperty ("text", text);

        auto* message = new juce::DynamicObject();
        message->setProperty ("type", "message");
        message->setProperty ("role", "user");
        message->setProperty ("content", juce::Array<juce::var> { juce::var (part) });
        return juce::var (message);
    }

    juce::String titleOf (const juce::File& file)
    {
        juce::FileInputStream stream (file);

        if (! stream.openedOk())
            return "(unreadable)";

        juce::MemoryBlock head;
        stream.readIntoMemoryBlock (head, headBytesToRead);

        juce::StringArray lines;
        lines.addLines (head.toString());

        for (const auto& line : lines)
        {
            const auto record = parseRecord (line);

            if (record.type != "item" || record.payload["role"].toString() != "user")
                continue;

            const auto text = textOfMessage (record.payload).replaceCharacters ("\r\n\t", "   ").trim();

            if (text.isEmpty())
                continue;

            return text.length() > maximumTitleLength
                 ? text.substring (0, maximumTitleLength) + "..."
                 : text;
        }

        return "(no message)";
    }
}

juce::File AiSessionStore::createSessionFile (const juce::File& projectRoot, const juce::String& model)
{
    const auto folder = sessionsFolderFor (projectRoot);

    if (! folder.createDirectory())
        return {};

    const juce::Uuid id;
    const auto now = juce::Time::getCurrentTime();

    // ファイル名にコロンは使えないので、時刻の区切りだけハイフンへ置き換える。
    const auto stamp = now.toISO8601 (true).upToFirstOccurrenceOf (".", false, false)
                                           .replaceCharacter (':', '-');

    const auto file = folder.getChildFile (stamp + "-" + id.toDashedString() + ".jsonl");

    auto* meta = new juce::DynamicObject();
    meta->setProperty ("id", id.toDashedString());
    meta->setProperty ("created", now.toISO8601 (true));
    meta->setProperty ("cwd", projectRoot.getFullPathName());
    meta->setProperty ("model", model);
    meta->setProperty ("parent", juce::var());

    // meta が書けなければ以降の追記も落ちる。呼び手に空を返して気づかせる。
    if (! appendRecord (file, 0, "meta", juce::var (meta)))
        return {};

    return file;
}

bool AiSessionStore::appendItem (const juce::File& sessionFile, int ordinal, const juce::var& item)
{
    return appendRecord (sessionFile, ordinal, "item", item);
}

juce::Array<AiSessionStore::Listing> AiSessionStore::listRecent (const juce::File& projectRoot, int limit)
{
    const auto folder = sessionsFolderFor (projectRoot);

    if (! folder.isDirectory())
        return {};

    juce::Array<Listing> found;

    for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*.jsonl", juce::File::findFiles))
    {
        const auto file = entry.getFile();
        found.add ({ file, titleOf (file), file.getLastModificationTime() });
    }

    std::sort (found.begin(), found.end(), [] (const Listing& a, const Listing& b)
    {
        return a.modified > b.modified;
    });

    if (limit > 0 && found.size() > limit)
        found.removeRange (limit, found.size() - limit);

    return found;
}

AiSessionStore::Restored AiSessionStore::restore (const juce::File& sessionFile)
{
    juce::StringArray lines;
    lines.addLines (sessionFile.loadFileAsString());

    juce::Array<Record> records;

    for (const auto& line : lines)
    {
        const auto record = parseRecord (line);

        // 壊れた行や書きかけで切れた行はここで落ちる。読めた分だけ使う。
        if (record.type.isNotEmpty())
            records.add (record);
    }

    /*  compact 行があれば、その要約が replaces_through までの item を置き換える。
        書き出す側 (/compact) はまだ無いが、後から読み側を直す方が高くつく。 */
    int replacesThrough = -1;
    juce::String summary;

    for (const auto& record : records)
        if (record.type == "compact")
        {
            summary = record.payload["summary"].toString();
            replacesThrough = static_cast<int> (record.payload["replaces_through"]);
        }

    Restored restored;

    if (summary.isNotEmpty())
        restored.conversation.add (makeUserMessage (summary));

    for (const auto& record : records)
    {
        restored.nextOrdinal = juce::jmax (restored.nextOrdinal, record.ordinal + 1);

        // 知らない type は黙って飛ばす。前方互換のため。
        if (record.type == "item" && record.ordinal > replacesThrough)
            restored.conversation.add (record.payload);
    }

    /*  ターンの途中で終わったファイルは function_call で終わっていることがある。
        承認待ちやツール実行の最中に閉じるとそうなる。出力の無い呼び出しを送ると
        Responses API が 400 を返し、読み戻した会話が丸ごと使えなくなるので落とす。 */
    juce::StringArray answeredCalls;

    for (const auto& item : restored.conversation)
        if (item["type"].toString() == "function_call_output")
            answeredCalls.add (item["call_id"].toString());

    for (int i = restored.conversation.size(); --i >= 0;)
    {
        const auto& item = restored.conversation.getReference (i);

        if (item["type"].toString() == "function_call"
             && ! answeredCalls.contains (item["call_id"].toString()))
            restored.conversation.remove (i);
    }

    /*  ponytail: 表示は conversation から作り直すだけにする。error と /model 等の
        ローカル通知は API へ送っていないので復元されない。第 2 のログを持つ方が高い。
        function_call_output も本文には要るが、見出しは function_call の 1 行で足りる。 */
    for (const auto& item : restored.conversation)
    {
        const auto type = item["type"].toString();

        if (type == "message")
        {
            const auto text = textOfMessage (item);

            if (text.isEmpty())
                continue;

            restored.entries.add ({ item["role"].toString() == "user"
                                        ? AiSession::Entry::Kind::user
                                        : AiSession::Entry::Kind::assistant,
                                    text });
        }
        else if (type == "function_call")
        {
            restored.entries.add ({ AiSession::Entry::Kind::tool,
                                    item["name"].toString() + "  " + item["arguments"].toString() });
        }
    }

    return restored;
}
