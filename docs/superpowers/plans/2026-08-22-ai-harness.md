# AI ハーネス Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Projucer に AI コーディングハーネスを内蔵し、iPad 単体で ChatGPT アカウントにサインインしてソースを読み書きできるようにする。

**Architecture:** iOS はサブプロセスを起動できないため `codex` バイナリは使わない。Codex のバックエンド（HTTPS + SSE）へ Projucer が C++ から直接接続する。認証はデバイスコードフロー、通信は Responses API 形式の SSE、ツール定義はクライアント側で持つ。純粋ロジック（SSE パーサ、パス検証）は JUCE 非依存にして単体で高速にテストする。

**Tech Stack:** C++17 / JUCE 8 / `juce::WebInputStream` / Security.framework (Keychain) / Objective-C++ (Apple のみ)

**Spec:** `docs/superpowers/specs/2026-08-22-ai-harness-design.md`

## Global Constraints

- 回答・コメント・ドキュメントは日本語（`AGENTS.md`）。
- **コミットは禁止**。`AGENTS.md` により、ユーザーが明示的に依頼し、かつ手動テストで動作確認するまでコミット・プッシュしない。各タスクの「コミット」ステップは**コマンドを提示するだけで実行しない**。ユーザーの承認を待つ。
- `CLIENT_ID` = `app_EMoamEEZ73f0CkXaXp7hrann`
- issuer = `https://auth.openai.com`
- API ベース = `https://chatgpt.com/backend-api/codex`
- リフレッシュ / トークン交換 = `https://auth.openai.com/oauth/token`
- 新規ソースは `Projucer/Source/AI/` 配下。ファイル名は既存に合わせ `jucer_` 接頭辞。
- Objective-C++ (`.mm`) は Apple プラットフォームのみ。Windows / Linux ではコンパイル対象から外す。
- **トークンをログに出さない。** 例外時のメッセージにも含めない。
- 純粋ロジック（`SseParser`, パス検証）は JUCE に依存させない。`std::string` と `std::filesystem` のみ使う。
- 対応構成: macOS Debug/Release、iOS デバイス Debug/Release、iOS シミュレータ Debug/Release の 6 つ。
- macOS の最小対応バージョンは `10.15` とする。`std::filesystem` を使用するパス検証を macOS 10.13 の標準ライブラリではリンクできないためである。

---

## File Structure

| ファイル | 責務 |
|---|---|
| `Projucer/Source/AI/jucer_SseParser.h` / `.cpp` | SSE のバイト列を `data:` ペイロードに切る。JUCE 非依存 |
| `Projucer/Source/AI/jucer_AiPaths.h` / `.cpp` | プロジェクトルート配下へのパス解決と検証。JUCE 非依存 |
| `Projucer/Source/AI/jucer_Keychain.h` / `.mm` | Security.framework の読み書き 2 関数 |
| `Projucer/Source/AI/jucer_CodexAuth.h` / `.cpp` | デバイスコードフロー、トークン保管・更新 |
| `Projucer/Source/AI/jucer_CodexClient.h` / `.cpp` | `/responses` への SSE リクエスト |
| `Projucer/Source/AI/jucer_AiTools.h` / `.cpp` | ツール実装とスキーマ |
| `Projucer/Source/AI/jucer_AiSession.h` / `.cpp` | 会話状態 |
| `Projucer/Source/AI/jucer_AgentLoop.h` / `.cpp` | エージェントループ |
| `Projucer/Source/AI/jucer_AiChatView.h` / `.cpp` | チャット UI |
| `Projucer/Source/AI/Tests/ai_selfcheck.cpp` | 純粋ロジックの自己チェック |
| `scripts/run_ai_selfcheck.sh` | 自己チェックのビルドと実行 |

変更する既存ファイル:

| ファイル | 変更内容 |
|---|---|
| `Projucer/Source/Project/UI/jucer_HeaderComponent.h` / `.cpp` | AI トグルボタン追加 |
| `Projucer/Source/Project/UI/jucer_ProjectContentComponent.h` / `.cpp` | `AiSession` 所有、AI ビュー切り替え |
| `Projucer/Projucer.jucer` | AI グループとファイル追加、Security.framework リンク |

---

## Task 1: 自己チェック基盤と SSE パーサ

**Files:**
- Create: `Projucer/Source/AI/jucer_SseParser.h`
- Create: `Projucer/Source/AI/jucer_SseParser.cpp`
- Create: `Projucer/Source/AI/Tests/ai_selfcheck.cpp`
- Create: `scripts/run_ai_selfcheck.sh`

**Interfaces:**
- Consumes: なし
- Produces: `SseParser` クラス。`std::vector<std::string> feed (const std::string& bytes)` が、投入したバイト列から完成した SSE イベントのペイロード（`data:` 行を `\n` で連結したもの）を順に返す。`jucer_AiPaths` 用の `check()` ヘルパも本タスクで定義する。

- [ ] **Step 1: 自己チェックのビルドスクリプトを作る**

`scripts/run_ai_selfcheck.sh`:

```bash
#!/bin/bash
# AI ハーネスの純粋ロジック（JUCE 非依存）を単体でビルドして実行する。
# JUCE を通さないので 1 秒程度で回る。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

clang++ -std=c++17 -fsanitize=address,undefined -g -Wall -Wextra \
    -I "$ROOT/Projucer/Source/AI" \
    "$ROOT/Projucer/Source/AI/Tests/ai_selfcheck.cpp" \
    "$ROOT/Projucer/Source/AI/jucer_SseParser.cpp" \
    "$ROOT/Projucer/Source/AI/jucer_AiPaths.cpp" \
    -o "$OUT/ai_selfcheck"

"$OUT/ai_selfcheck"
```

実行権限を付ける: `chmod +x scripts/run_ai_selfcheck.sh`

- [ ] **Step 2: 失敗するテストを書く**

`Projucer/Source/AI/Tests/ai_selfcheck.cpp`:

```cpp
/*
    AI ハーネスの純粋ロジックの自己チェック。
    JUCE に依存しないので scripts/run_ai_selfcheck.sh で単体で回る。
*/

#include "jucer_SseParser.h"
#include "jucer_AiPaths.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
        // 1 回の投入に完結したイベントが 1 つ。
        SseParser p;
        auto events = p.feed ("data: {\"a\":1}\n\n");
        check (events.size() == 1, "1 イベント返る");
        check (events.size() == 1 && events[0] == "{\"a\":1}", "ペイロードが取れる");
    }

    {
        // イベントが投入をまたいで分割されている。
        SseParser p;
        auto first = p.feed ("data: {\"a\"");
        check (first.empty(), "未完のイベントは返さない");

        auto second = p.feed (":1}\n\n");
        check (second.size() == 1, "続きが来たら返る");
        check (second.size() == 1 && second[0] == "{\"a\":1}", "分割されても正しく連結される");
    }

    {
        // CRLF 改行。
        SseParser p;
        auto events = p.feed ("data: hello\r\n\r\n");
        check (events.size() == 1 && events[0] == "hello", "CRLF を扱える");
    }

    {
        // data: 以外の行は無視する。
        SseParser p;
        auto events = p.feed (": keep-alive\nevent: foo\ndata: x\n\n");
        check (events.size() == 1 && events[0] == "x", "コメントと event: を無視する");
    }

    {
        // 1 回の投入に複数イベント。
        SseParser p;
        auto events = p.feed ("data: a\n\ndata: b\n\n");
        check (events.size() == 2, "複数イベントを返す");
        check (events.size() == 2 && events[0] == "a" && events[1] == "b", "順序が保たれる");
    }

    {
        // data: 行が複数ある 1 イベントは \n で連結する（SSE の仕様）。
        SseParser p;
        auto events = p.feed ("data: a\ndata: b\n\n");
        check (events.size() == 1 && events[0] == "a\nb", "複数 data 行を連結する");
    }

    {
        // 空の data: 行。
        SseParser p;
        auto events = p.feed ("data:\n\n");
        check (events.size() == 1 && events[0].empty(), "空ペイロードも 1 イベント");
    }

    {
        // 終端マーカー。中身の解釈は上位に任せ、素通しする。
        SseParser p;
        auto events = p.feed ("data: [DONE]\n\n");
        check (events.size() == 1 && events[0] == "[DONE]", "[DONE] を素通しする");
    }
}

//==============================================================================
int main()
{
    testSseParser();

    if (failures > 0)
    {
        std::printf ("\n%d 件失敗\n", failures);
        return 1;
    }

    std::printf ("\nすべて成功\n");
    return 0;
}
```

このテストは `jucer_AiPaths.h` を include しているが Task 2 まで存在しない。Task 1 の間は include 行とスクリプトの `jucer_AiPaths.cpp` 行を一時的にコメントアウトしてよい。Task 2 で戻す。

- [ ] **Step 3: テストが失敗することを確認する**

Run: `./scripts/run_ai_selfcheck.sh`
Expected: コンパイルエラー。`jucer_SseParser.h` が見つからない。

- [ ] **Step 4: ヘッダを書く**

`Projucer/Source/AI/jucer_SseParser.h`:

```cpp
#pragma once

#include <string>
#include <vector>

/*  Server-Sent Events のバイト列を、完成したイベントのペイロードへ切り分ける。

    JUCE に依存しない。ネットワークから届いたバイト列をそのまま feed() へ渡すと、
    イベントが揃うたびにそのペイロードが返る。届き方（分割・結合）に依存しない。

    1 イベントに data: 行が複数ある場合は SSE の仕様どおり \n で連結する。
    data: 以外の行（event:, id:, : で始まるコメント）は無視する。
*/
class SseParser
{
public:
    /** 受け取ったバイト列を投入し、そこで完成したイベントのペイロードを順に返す。 */
    std::vector<std::string> feed (const std::string& bytes);

private:
    std::string buffer;    // まだ行として完成していない残り
    std::string pending;   // 現在組み立て中のイベントのペイロード
    bool hasPending = false;
};
```

- [ ] **Step 5: 実装を書く**

`Projucer/Source/AI/jucer_SseParser.cpp`:

```cpp
#include "jucer_SseParser.h"

std::vector<std::string> SseParser::feed (const std::string& bytes)
{
    buffer += bytes;

    std::vector<std::string> events;
    std::size_t pos = 0;

    for (;;)
    {
        const auto newline = buffer.find ('\n', pos);

        if (newline == std::string::npos)
            break;

        auto line = buffer.substr (pos, newline - pos);
        pos = newline + 1;

        if (! line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty())
        {
            // 空行はイベントの区切り。
            if (hasPending)
            {
                events.push_back (pending);
                pending.clear();
                hasPending = false;
            }

            continue;
        }

        if (line.rfind ("data:", 0) != 0)
            continue;   // event:, id:, コメントは使わない

        auto payload = line.substr (5);

        if (! payload.empty() && payload.front() == ' ')
            payload.erase (0, 1);

        if (hasPending)
            pending += '\n';

        pending += payload;
        hasPending = true;
    }

    buffer.erase (0, pos);
    return events;
}
```

- [ ] **Step 6: テストが通ることを確認する**

Run: `./scripts/run_ai_selfcheck.sh`
Expected: `すべて成功`

- [ ] **Step 7: コミットのコマンドを提示する（実行しない）**

`AGENTS.md` によりコミットしない。以下をユーザーに提示し、承認を待つ。

```bash
git add Projucer/Source/AI/jucer_SseParser.h Projucer/Source/AI/jucer_SseParser.cpp \
        Projucer/Source/AI/Tests/ai_selfcheck.cpp scripts/run_ai_selfcheck.sh
git commit -m "feat(ai): SSE パーサと自己チェック基盤を追加"
```

---

## Task 2: パス検証

プロジェクトルートの外へ出る経路を塞ぐ。ここは trust boundary なので簡略化しない。

**Files:**
- Create: `Projucer/Source/AI/jucer_AiPaths.h`
- Create: `Projucer/Source/AI/jucer_AiPaths.cpp`
- Modify: `Projucer/Source/AI/Tests/ai_selfcheck.cpp`

**Interfaces:**
- Consumes: Task 1 の `check()` ヘルパと `main()`
- Produces: `std::optional<std::filesystem::path> resolveInsideRoot (const std::filesystem::path& root, const std::string& relativePath)`。ルート配下に収まる実パスを返す。外へ出る、絶対パス、解決不能のいずれかなら `std::nullopt`。

- [ ] **Step 1: 失敗するテストを書く**

`ai_selfcheck.cpp` の `testSseParser()` の後、`main()` の前に追加する:

```cpp
//==============================================================================
/*  一時ディレクトリに小さなプロジェクトを作り、脱出経路を塞げているか確かめる。 */
static void testAiPaths()
{
    std::printf ("AiPaths\n");

    const auto base = fs::temp_directory_path() / "projucer_ai_selfcheck";
    fs::remove_all (base);
    fs::create_directories (base / "project" / "Source");
    fs::create_directories (base / "outside");

    const auto root = base / "project";

    {
        std::ofstream f (root / "Source" / "Main.cpp");
        f << "int main() { return 0; }\n";
    }

    {
        std::ofstream f (base / "outside" / "secret.txt");
        f << "秘密\n";
    }

    // 正当なパスは通る。
    {
        const auto resolved = resolveInsideRoot (root, "Source/Main.cpp");
        check (resolved.has_value(), "プロジェクト内のパスが通る");
    }

    // まだ存在しないファイルも、ルート配下なら通る（新規作成のため）。
    {
        const auto resolved = resolveInsideRoot (root, "Source/New.cpp");
        check (resolved.has_value(), "未作成のパスもルート配下なら通る");
    }

    // 途中に .. があってもルート配下に戻るなら通る。
    {
        const auto resolved = resolveInsideRoot (root, "Source/../Source/Main.cpp");
        check (resolved.has_value(), "ルート配下に戻る .. は通る");
    }

    // .. でルートの外へ出るのは拒否。
    {
        const auto resolved = resolveInsideRoot (root, "../outside/secret.txt");
        check (! resolved.has_value(), ".. による脱出を拒否する");
    }

    // 絶対パスは拒否。
    {
        const auto resolved = resolveInsideRoot (root, "/etc/passwd");
        check (! resolved.has_value(), "絶対パスを拒否する");
    }

    // ルート内に置かれたシンボリックリンク経由の脱出は拒否。
    {
        std::error_code ec;
        fs::create_symlink (base / "outside", root / "escape", ec);
        check (! ec, "テスト用シンボリックリンクを作れる");

        const auto resolved = resolveInsideRoot (root, "escape/secret.txt");
        check (! resolved.has_value(), "シンボリックリンク経由の脱出を拒否する");
    }

    // 接頭辞が一致するだけの兄弟ディレクトリは拒否する。
    // root が /tmp/x/project のとき /tmp/x/project_evil を通してはいけない。
    {
        fs::create_directories (base / "project_evil");

        const auto resolved = resolveInsideRoot (root, "../project_evil");
        check (! resolved.has_value(), "接頭辞が一致するだけの兄弟を拒否する");
    }

    fs::remove_all (base);
}
```

`main()` を差し替える:

```cpp
int main()
{
    testSseParser();
    testAiPaths();

    if (failures > 0)
    {
        std::printf ("\n%d 件失敗\n", failures);
        return 1;
    }

    std::printf ("\nすべて成功\n");
    return 0;
}
```

Task 1 で `jucer_AiPaths.h` の include とスクリプトの `jucer_AiPaths.cpp` 行をコメントアウトしていた場合は戻す。

- [ ] **Step 2: テストが失敗することを確認する**

Run: `./scripts/run_ai_selfcheck.sh`
Expected: コンパイルエラー。`resolveInsideRoot` が未定義。

- [ ] **Step 3: ヘッダを書く**

`Projucer/Source/AI/jucer_AiPaths.h`:

```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>

/*  AI ツールが触れるパスをプロジェクトルート配下に閉じ込める。

    ここは trust boundary。モデルが返してきた文字列をそのままファイル操作へ
    渡してはいけない。.. による脱出と、ルート内に置かれたシンボリックリンク
    経由の脱出の両方を塞ぐ。

    @param root          プロジェクトルート。これ自身もシンボリックリンクを
                         解決してから比較する。
    @param relativePath  モデルが指定したプロジェクト相対パス。
    @returns             ルート配下に収まる実パス。外へ出る場合、絶対パスの
                         場合、解決できない場合は nullopt。
*/
std::optional<std::filesystem::path> resolveInsideRoot (const std::filesystem::path& root,
                                                        const std::string& relativePath);
```

- [ ] **Step 4: 実装を書く**

`Projucer/Source/AI/jucer_AiPaths.cpp`:

```cpp
#include "jucer_AiPaths.h"

namespace fs = std::filesystem;

std::optional<fs::path> resolveInsideRoot (const fs::path& root, const std::string& relativePath)
{
    if (relativePath.empty())
        return {};

    const fs::path candidate (relativePath);

    // 絶対パスは受け付けない。プロジェクト相対だけを扱う。
    if (candidate.is_absolute() || candidate.has_root_name())
        return {};

    std::error_code ec;

    // ルート自身もシンボリックリンクでありうるので実パスへ直す。
    const auto canonicalRoot = fs::weakly_canonical (root, ec);

    if (ec)
        return {};

    // weakly_canonical は存在する部分のシンボリックリンクを解決し、
    // .. を取り除く。未作成のファイルでも使えるので新規作成にも対応できる。
    const auto resolved = fs::weakly_canonical (canonicalRoot / candidate, ec);

    if (ec)
        return {};

    // 文字列の接頭辞比較では project と project_evil を区別できない。
    // パス要素単位で突き合わせる。
    auto rootPart = canonicalRoot.begin();
    auto resolvedPart = resolved.begin();

    for (; rootPart != canonicalRoot.end(); ++rootPart, ++resolvedPart)
    {
        if (resolvedPart == resolved.end() || *resolvedPart != *rootPart)
            return {};
    }

    return resolved;
}
```

- [ ] **Step 5: テストが通ることを確認する**

Run: `./scripts/run_ai_selfcheck.sh`
Expected: `すべて成功`

ASan / UBSan を有効にしてビルドしているので、境界の壊れは実行時に落ちる。

- [ ] **Step 6: コミットのコマンドを提示する（実行しない）**

```bash
git add Projucer/Source/AI/jucer_AiPaths.h Projucer/Source/AI/jucer_AiPaths.cpp \
        Projucer/Source/AI/Tests/ai_selfcheck.cpp
git commit -m "feat(ai): プロジェクトルート外への脱出を塞ぐパス検証を追加"
```

---

## Task 3: Keychain ラッパー

**Files:**
- Create: `Projucer/Source/AI/jucer_Keychain.h`
- Create: `Projucer/Source/AI/jucer_Keychain.mm`

**Interfaces:**
- Consumes: なし
- Produces:
  - `bool keychainWrite (const juce::String& service, const juce::String& account, const juce::String& value)`
  - `juce::String keychainRead (const juce::String& service, const juce::String& account)` — 無ければ空文字列
  - `void keychainErase (const juce::String& service, const juce::String& account)`

自己チェックの対象外（Security.framework が要るため）。Task 4 の実機・実 Mac 確認で一緒に検証する。

- [ ] **Step 1: ヘッダを書く**

`Projucer/Source/AI/jucer_Keychain.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>

/*  Keychain への読み書き。macOS と iOS で同じ実装を使う。

    リフレッシュトークンは長期間有効な資格情報なので、平文ファイルには置かない。
    アクセス属性は kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly を使う。
    デバイス外へ同期させず、初回アンロック後であればバックグラウンドからも読める。
*/

/** 値を保存する。既存があれば上書きする。成功したら true。 */
bool keychainWrite (const juce::String& service, const juce::String& account, const juce::String& value);

/** 値を読む。無ければ空文字列を返す。 */
juce::String keychainRead (const juce::String& service, const juce::String& account);

/** 値を消す。無くてもエラーにしない。 */
void keychainErase (const juce::String& service, const juce::String& account);
```

- [ ] **Step 2: 実装を書く**

`Projucer/Source/AI/jucer_Keychain.mm`:

```cpp
#include "jucer_Keychain.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace
{
    NSMutableDictionary* baseQuery (const juce::String& service, const juce::String& account)
    {
        auto* query = [NSMutableDictionary dictionary];
        query[(__bridge id) kSecClass]       = (__bridge id) kSecClassGenericPassword;
        query[(__bridge id) kSecAttrService] = [NSString stringWithUTF8String: service.toRawUTF8()];
        query[(__bridge id) kSecAttrAccount] = [NSString stringWithUTF8String: account.toRawUTF8()];
        return query;
    }
}

bool keychainWrite (const juce::String& service, const juce::String& account, const juce::String& value)
{
    @autoreleasepool
    {
        auto* query = baseQuery (service, account);

        // 同じ service/account の項目は一度消してから入れ直す。
        // SecItemUpdate と分岐させるより単純で、結果が同じになる。
        SecItemDelete ((__bridge CFDictionaryRef) query);

        auto* data = [[NSString stringWithUTF8String: value.toRawUTF8()]
                          dataUsingEncoding: NSUTF8StringEncoding];

        query[(__bridge id) kSecValueData]     = data;
        query[(__bridge id) kSecAttrAccessible] = (__bridge id) kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;

        return SecItemAdd ((__bridge CFDictionaryRef) query, nullptr) == errSecSuccess;
    }
}

juce::String keychainRead (const juce::String& service, const juce::String& account)
{
    @autoreleasepool
    {
        auto* query = baseQuery (service, account);
        query[(__bridge id) kSecReturnData] = @YES;
        query[(__bridge id) kSecMatchLimit] = (__bridge id) kSecMatchLimitOne;

        CFTypeRef result = nullptr;

        if (SecItemCopyMatching ((__bridge CFDictionaryRef) query, &result) != errSecSuccess)
            return {};

        auto* data = (__bridge_transfer NSData*) result;

        if (data == nil)
            return {};

        auto* string = [[NSString alloc] initWithData: data encoding: NSUTF8StringEncoding];

        if (string == nil)
            return {};

        return juce::String::fromUTF8 ([string UTF8String]);
    }
}

void keychainErase (const juce::String& service, const juce::String& account)
{
    @autoreleasepool
    {
        SecItemDelete ((__bridge CFDictionaryRef) baseQuery (service, account));
    }
}
```

- [ ] **Step 3: コミットのコマンドを提示する（実行しない）**

```bash
git add Projucer/Source/AI/jucer_Keychain.h Projucer/Source/AI/jucer_Keychain.mm
git commit -m "feat(ai): Keychain 読み書きラッパーを追加"
```

---

## Task 4: 認証（デバイスコードフロー）

**このタスクが計画中で最もリスクが高い。** エンドポイントの実挙動が未検証なので、
早い段階で実際に叩いて確かめる。UI より先にここを通す。

**Files:**
- Create: `Projucer/Source/AI/jucer_CodexAuth.h`
- Create: `Projucer/Source/AI/jucer_CodexAuth.cpp`

**Interfaces:**
- Consumes: Task 3 の `keychainWrite` / `keychainRead` / `keychainErase`
- Produces: `CodexAuth` クラス
  - `struct Tokens { juce::String accessToken, refreshToken, accountId; }`
  - `struct DeviceCode { juce::String deviceAuthId, userCode, verificationUrl; int intervalSeconds; }`
  - `bool isSignedIn() const`
  - `juce::String getAccessToken() const`
  - `juce::String getAccountId() const`
  - `std::optional<DeviceCode> requestDeviceCode (juce::String& errorOut)`
  - `bool pollForTokens (const DeviceCode&, std::atomic<bool>& shouldStop, juce::String& errorOut)`
  - `bool refresh (juce::String& errorOut)`
  - `void signOut()`

- [ ] **Step 1: ヘッダを書く**

`Projucer/Source/AI/jucer_CodexAuth.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <optional>

/*  ChatGPT サブスクによるサインインを扱う。

    デバイスコードフローを使う。ブラウザのコールバックを受ける必要がないので、
    iPad でもローカル HTTP サーバーなしで完結する。PKCE のペアはサーバーが
    生成して返すため、クライアント側で PKCE を実装する必要はない。

    ネットワークを叩くメソッドはメッセージスレッドから呼ばないこと。
*/
class CodexAuth
{
public:
    CodexAuth();

    struct Tokens
    {
        juce::String accessToken;
        juce::String refreshToken;
        juce::String accountId;
    };

    struct DeviceCode
    {
        juce::String deviceAuthId;
        juce::String userCode;
        juce::String verificationUrl;
        int intervalSeconds = 5;
    };

    bool isSignedIn() const;
    juce::String getAccessToken() const;
    juce::String getAccountId() const;

    /** 手順 1。ユーザーへ見せるコードと URL を得る。失敗したら nullopt。 */
    std::optional<DeviceCode> requestDeviceCode (juce::String& errorOut);

    /** 手順 2。ユーザーがブラウザで承認するまで待つ。最大 15 分。
        shouldStop を立てると中断する。成功したらトークンを保存する。 */
    bool pollForTokens (const DeviceCode& code, std::atomic<bool>& shouldStop, juce::String& errorOut);

    /** access token を更新する。成功したら保存し直す。 */
    bool refresh (juce::String& errorOut);

    void signOut();

    static constexpr const char* clientId = "app_EMoamEEZ73f0CkXaXp7hrann";
    static constexpr const char* issuer   = "https://auth.openai.com";

private:
    void load();
    void save() const;

    /** id_token の JWT ペイロードから account_id を取り出す。 */
    static juce::String extractAccountId (const juce::String& idToken);

    Tokens tokens;
    mutable juce::CriticalSection lock;
};
```

- [ ] **Step 2: 実装を書く**

`Projucer/Source/AI/jucer_CodexAuth.cpp`:

```cpp
#include "jucer_CodexAuth.h"
#include "jucer_Keychain.h"

namespace
{
    constexpr const char* keychainService = "com.projucer.ai.codex";
    constexpr const char* keychainAccount = "tokens";

    /*  JSON を POST して応答本文を返す。HTTP ステータスも返す。
        トークンを含みうるので、失敗時も本文をそのままログへ出さないこと。 */
    juce::String postJson (const juce::String& url,
                           const juce::String& body,
                           const juce::StringPairArray& extraHeaders,
                           int& statusOut)
    {
        statusOut = 0;

        juce::String headers = "Content-Type: application/json";

        for (const auto& key : extraHeaders.getAllKeys())
            headers << "\r\n" << key << ": " << extraHeaders[key];

        juce::URL requestUrl (url);
        requestUrl = requestUrl.withPOSTData (body);

        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                           .withExtraHeaders (headers)
                           .withConnectionTimeoutMs (30000)
                           .withStatusCode (&statusOut);

        if (auto stream = requestUrl.createInputStream (options))
            return stream->readEntireStreamAsString();

        return {};
    }

    /*  application/x-www-form-urlencoded を POST する。トークン交換用。 */
    juce::String postForm (const juce::String& url, const juce::String& body, int& statusOut)
    {
        statusOut = 0;

        juce::URL requestUrl (url);
        requestUrl = requestUrl.withPOSTData (body);

        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                           .withExtraHeaders ("Content-Type: application/x-www-form-urlencoded")
                           .withConnectionTimeoutMs (30000)
                           .withStatusCode (&statusOut);

        if (auto stream = requestUrl.createInputStream (options))
            return stream->readEntireStreamAsString();

        return {};
    }

    /*  interval は数値で来ることも文字列で来ることもある。 */
    int readInterval (const juce::var& value)
    {
        if (value.isVoid())
            return 5;

        const auto seconds = value.isString() ? value.toString().getIntValue() : (int) value;
        return juce::jlimit (1, 60, seconds == 0 ? 5 : seconds);
    }
}

//==============================================================================
CodexAuth::CodexAuth()
{
    load();
}

void CodexAuth::load()
{
    const juce::ScopedLock sl (lock);

    const auto stored = keychainRead (keychainService, keychainAccount);

    if (stored.isEmpty())
        return;

    const auto parsed = juce::JSON::parse (stored);

    tokens.accessToken  = parsed.getProperty ("access_token", {}).toString();
    tokens.refreshToken = parsed.getProperty ("refresh_token", {}).toString();
    tokens.accountId    = parsed.getProperty ("account_id", {}).toString();
}

void CodexAuth::save() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("access_token",  tokens.accessToken);
    object->setProperty ("refresh_token", tokens.refreshToken);
    object->setProperty ("account_id",    tokens.accountId);

    keychainWrite (keychainService, keychainAccount, juce::JSON::toString (juce::var (object)));
}

bool CodexAuth::isSignedIn() const
{
    const juce::ScopedLock sl (lock);
    return tokens.accessToken.isNotEmpty();
}

juce::String CodexAuth::getAccessToken() const
{
    const juce::ScopedLock sl (lock);
    return tokens.accessToken;
}

juce::String CodexAuth::getAccountId() const
{
    const juce::ScopedLock sl (lock);
    return tokens.accountId;
}

void CodexAuth::signOut()
{
    const juce::ScopedLock sl (lock);
    tokens = {};
    keychainErase (keychainService, keychainAccount);
}

//==============================================================================
std::optional<CodexAuth::DeviceCode> CodexAuth::requestDeviceCode (juce::String& errorOut)
{
    auto* body = new juce::DynamicObject();
    body->setProperty ("client_id", clientId);

    int status = 0;
    const auto response = postJson (juce::String (issuer) + "/api/accounts/deviceauth/usercode",
                                    juce::JSON::toString (juce::var (body)),
                                    {},
                                    status);

    if (status == 404)
    {
        errorOut = "このサーバーではデバイスコードによるサインインが有効になっていません。";
        return {};
    }

    if (status < 200 || status >= 300)
    {
        errorOut = "サインインの開始に失敗しました（HTTP " + juce::String (status) + "）。";
        return {};
    }

    const auto parsed = juce::JSON::parse (response);

    DeviceCode code;
    code.deviceAuthId    = parsed.getProperty ("device_auth_id", {}).toString();
    code.userCode        = parsed.getProperty ("user_code", {}).toString();
    code.intervalSeconds = readInterval (parsed.getProperty ("interval", {}));
    code.verificationUrl = juce::String (issuer) + "/codex/device";

    if (code.deviceAuthId.isEmpty() || code.userCode.isEmpty())
    {
        errorOut = "サインインの応答を解釈できませんでした。";
        return {};
    }

    return code;
}

bool CodexAuth::pollForTokens (const DeviceCode& code, std::atomic<bool>& shouldStop, juce::String& errorOut)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) (15 * 60 * 1000);

    juce::String authorizationCode, codeVerifier;

    while (! shouldStop.load())
    {
        if (juce::Time::getMillisecondCounter() > deadline)
        {
            errorOut = "サインインがタイムアウトしました。もう一度お試しください。";
            return false;
        }

        auto* body = new juce::DynamicObject();
        body->setProperty ("device_auth_id", code.deviceAuthId);
        body->setProperty ("user_code",      code.userCode);

        int status = 0;
        const auto response = postJson (juce::String (issuer) + "/api/accounts/deviceauth/token",
                                        juce::JSON::toString (juce::var (body)),
                                        {},
                                        status);

        if (status >= 200 && status < 300)
        {
            const auto parsed = juce::JSON::parse (response);
            authorizationCode = parsed.getProperty ("authorization_code", {}).toString();
            codeVerifier      = parsed.getProperty ("code_verifier", {}).toString();

            if (authorizationCode.isNotEmpty())
                break;
        }
        else if (status >= 400 && status != 428 && status != 429)
        {
            // 428（未承認）と 429（急ぎすぎ）は待てば解消する。それ以外は諦める。
            errorOut = "サインインに失敗しました（HTTP " + juce::String (status) + "）。";
            return false;
        }

        // 中断要求に素早く応じるため、待ち時間を細かく刻む。
        for (int i = 0; i < code.intervalSeconds * 10 && ! shouldStop.load(); ++i)
            juce::Thread::sleep (100);
    }

    if (shouldStop.load())
    {
        errorOut = "サインインを中止しました。";
        return false;
    }

    // トークン交換。
    const auto form = juce::String ("grant_type=authorization_code")
                        + "&code=" + juce::URL::addEscapeChars (authorizationCode, false)
                        + "&redirect_uri=" + juce::URL::addEscapeChars (juce::String (issuer) + "/deviceauth/callback", false)
                        + "&client_id=" + juce::URL::addEscapeChars (clientId, false)
                        + "&code_verifier=" + juce::URL::addEscapeChars (codeVerifier, false);

    int status = 0;
    const auto response = postForm (juce::String (issuer) + "/oauth/token", form, status);

    if (status < 200 || status >= 300)
    {
        errorOut = "トークンの取得に失敗しました（HTTP " + juce::String (status) + "）。";
        return false;
    }

    const auto parsed = juce::JSON::parse (response);

    const juce::ScopedLock sl (lock);

    tokens.accessToken  = parsed.getProperty ("access_token", {}).toString();
    tokens.refreshToken = parsed.getProperty ("refresh_token", {}).toString();
    tokens.accountId    = extractAccountId (parsed.getProperty ("id_token", {}).toString());

    if (tokens.accessToken.isEmpty())
    {
        errorOut = "トークンの応答を解釈できませんでした。";
        return false;
    }

    save();
    return true;
}

bool CodexAuth::refresh (juce::String& errorOut)
{
    juce::String currentRefreshToken;

    {
        const juce::ScopedLock sl (lock);
        currentRefreshToken = tokens.refreshToken;
    }

    if (currentRefreshToken.isEmpty())
    {
        errorOut = "サインインが必要です。";
        return false;
    }

    const auto form = juce::String ("grant_type=refresh_token")
                        + "&refresh_token=" + juce::URL::addEscapeChars (currentRefreshToken, false)
                        + "&client_id=" + juce::URL::addEscapeChars (clientId, false);

    int status = 0;
    const auto response = postForm (juce::String (issuer) + "/oauth/token", form, status);

    if (status < 200 || status >= 300)
    {
        errorOut = "サインインの更新に失敗しました。もう一度サインインしてください。";
        return false;
    }

    const auto parsed = juce::JSON::parse (response);
    const auto newAccessToken = parsed.getProperty ("access_token", {}).toString();

    if (newAccessToken.isEmpty())
    {
        errorOut = "サインインの更新に失敗しました。もう一度サインインしてください。";
        return false;
    }

    const juce::ScopedLock sl (lock);

    tokens.accessToken = newAccessToken;

    // refresh token が回転する場合があるので、返ってきたら差し替える。
    const auto newRefreshToken = parsed.getProperty ("refresh_token", {}).toString();

    if (newRefreshToken.isNotEmpty())
        tokens.refreshToken = newRefreshToken;

    save();
    return true;
}

juce::String CodexAuth::extractAccountId (const juce::String& idToken)
{
    // JWT は header.payload.signature。真ん中を base64url デコードする。
    // 署名は検証しない。これは自分で取得したトークンから ID を読むだけの用途で、
    // 信頼の判断には使わない。
    const auto firstDot = idToken.indexOfChar ('.');

    if (firstDot < 0)
        return {};

    const auto secondDot = idToken.indexOfChar (firstDot + 1, '.');

    if (secondDot < 0)
        return {};

    auto payload = idToken.substring (firstDot + 1, secondDot)
                       .replaceCharacter ('-', '+')
                       .replaceCharacter ('_', '/');

    while (payload.length() % 4 != 0)
        payload << "=";

    juce::MemoryOutputStream decoded;

    if (! juce::Base64::convertFromBase64 (decoded, payload))
        return {};

    const auto parsed = juce::JSON::parse (decoded.toString());

    // account_id は認証情報のトップレベルか、Codex 用のクレームの下にある。
    auto accountId = parsed.getProperty ("chatgpt_account_id", {}).toString();

    if (accountId.isEmpty())
    {
        const auto authClaim = parsed.getProperty ("https://api.openai.com/auth", {});
        accountId = authClaim.getProperty ("chatgpt_account_id", {}).toString();
    }

    return accountId;
}
```

- [ ] **Step 3: サインインを実機で確認する（このタスクの本題）**

ここまでの実装は UI に繋がっていないので、Task 9 まで待たずに Mac 上で確かめる。
`Projucer/Source/Application/jucer_Application.cpp` の `initialise()` の先頭に、
一時的に以下を入れて Mac の Debug ビルドを起動する。

```cpp
   #if 0   // 確認用。Task 4 の検証が終わったら消すこと。
    {
        static CodexAuth auth;
        static std::atomic<bool> stop { false };

        juce::Thread::launch ([]
        {
            juce::String error;

            if (auto code = auth.requestDeviceCode (error))
            {
                DBG ("サインインコード: " << code->userCode);
                DBG ("ブラウザで開く: " << code->verificationUrl);

                if (auth.pollForTokens (*code, stop, error))
                    DBG ("サインイン成功。account_id を取得: " << (auth.getAccountId().isNotEmpty() ? "はい" : "いいえ"));
                else
                    DBG ("サインイン失敗: " << error);
            }
            else
            {
                DBG ("コード取得失敗: " << error);
            }
        });
    }
   #endif
```

`#if 0` を `#if 1` にして起動し、コンソールに出たコードを
`https://auth.openai.com/codex/device` で入力する。

**確認すること:**

1. `usercode` が 200 を返し、`user_code` と `interval` が取れる
2. ブラウザで承認すると `deviceauth/token` が `authorization_code` と `code_verifier` を返す
3. `oauth/token` が `access_token` / `refresh_token` / `id_token` を返す
4. `id_token` から `account_id` が取れる（取れなければクレーム名を実際の中身に合わせて直す）
5. アプリを再起動しても `isSignedIn()` が true（Keychain が効いている）
6. `refresh()` が成功する

**トークンそのものは絶対に出力しない。** 上の `DBG` も長さや有無しか出していない。
このまま維持する。

うまくいかない場合、最も疑うべきは `account_id` のクレーム名。`id_token` の
ペイロードを一度だけ構造確認する必要があれば、キー名だけを出力して値は出さない。

確認が終わったら `#if 1` を `#if 0` へ戻し、Task 9 の完了時にこのブロックごと削除する。

- [ ] **Step 4: コミットのコマンドを提示する（実行しない）**

```bash
git add Projucer/Source/AI/jucer_CodexAuth.h Projucer/Source/AI/jucer_CodexAuth.cpp
git commit -m "feat(ai): デバイスコードフローによる ChatGPT サインインを追加"
```

---

## Task 4b: ブラウザ OAuth（既定のサインイン経路）

デバイスコード認証は ChatGPT のセキュリティ設定で**アカウントごとに有効化が必要**で、
既定はオフ。全ユーザーに設定変更を強いるのは受け入れ難いので、こちらを既定にする。
Task 4 のデバイスコード経路は代替として残す。

**Files:**
- Create: `Projucer/Source/AI/jucer_Pkce.h` / `.cpp`
- Create: `Projucer/Source/AI/jucer_OAuthCallbackServer.h` / `.cpp`
- Create: `Projucer/Source/AI/jucer_AuthBrowser.h` / `.mm`
- Modify: `Projucer/Source/AI/jucer_CodexAuth.h` / `.cpp`

**Interfaces:**
- Consumes: Task 3 の Keychain、Task 4 の `CodexAuth`
- Produces:
  - `struct PkceCodes { juce::String codeVerifier, codeChallenge; }; PkceCodes generatePkce(); juce::String generateOAuthState();`
  - `OAuthCallbackServer` — `bool start (int preferredPort)`, `int getPort() const`, `bool waitForCode (std::atomic<bool>&, juce::String& code, juce::String& state, juce::String& error)`, `void stop()`
  - `void presentAuthPage (const juce::String& url, const juce::String& callbackHost, int callbackPort)`
  - `CodexAuth::signInWithBrowser (std::atomic<bool>& shouldStop, juce::String& errorOut)`

- [ ] **Step 1: PKCE を実装する**

`Projucer/Source/AI/jucer_Pkce.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>

/*  ブラウザ OAuth 用の PKCE。

    デバイスコード経路ではサーバーがペアを生成して返すが、ブラウザ経路では
    クライアントが作る。方式は S256。
*/
struct PkceCodes
{
    juce::String codeVerifier;
    juce::String codeChallenge;
};

/** 64 バイトの乱数から検証子と challenge を作る。 */
PkceCodes generatePkce();

/** CSRF 対策の state。32 バイトの乱数。 */
juce::String generateOAuthState();
```

`Projucer/Source/AI/jucer_Pkce.cpp`:

```cpp
#include "jucer_Pkce.h"

namespace
{
    /*  URL-safe base64、パディング無し。OAuth のクエリにそのまま載せられる形。 */
    juce::String toBase64Url (const void* data, int numBytes)
    {
        juce::MemoryOutputStream encoded;
        juce::Base64::convertToBase64 (encoded, data, (size_t) numBytes);

        return encoded.toString()
                   .replaceCharacter ('+', '-')
                   .replaceCharacter ('/', '_')
                   .removeCharacters ("=");
    }

    juce::MemoryBlock randomBytes (int numBytes)
    {
        juce::MemoryBlock bytes ((size_t) numBytes);
        auto& random = juce::Random::getSystemRandom();

        for (int i = 0; i < numBytes; ++i)
            bytes[(size_t) i] = (char) (juce::uint8) random.nextInt (256);

        return bytes;
    }
}

PkceCodes generatePkce()
{
    const auto verifierBytes = randomBytes (64);

    PkceCodes codes;
    codes.codeVerifier = toBase64Url (verifierBytes.getData(), (int) verifierBytes.getSize());

    // challenge は検証子の「文字列」の SHA-256。バイト列ではない点に注意。
    const juce::SHA256 hash (codes.codeVerifier.toRawUTF8(),
                             (size_t) codes.codeVerifier.getNumBytesAsUTF8());

    const auto digest = hash.getRawData();
    codes.codeChallenge = toBase64Url (digest.getData(), (int) digest.getSize());

    return codes;
}

juce::String generateOAuthState()
{
    const auto bytes = randomBytes (32);
    return toBase64Url (bytes.getData(), (int) bytes.getSize());
}
```

- [ ] **Step 2: PKCE を既知の答えで検証する**

RFC 7636 Appendix B の試験ベクタで確かめる。これを外すと認可が必ず失敗するので、
実装したら必ず一度は通すこと。

リポジトリの外の一時ディレクトリに次を作り、JUCE の `juce_core` を
リンクして実行する。

```cpp
// /tmp/pkce_check/main.cpp
#include <juce_core/juce_core.h>
#include <cstdio>

int main()
{
    // RFC 7636 Appendix B
    const juce::String verifier ("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
    const juce::String expected ("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");

    const juce::SHA256 hash (verifier.toRawUTF8(), (size_t) verifier.getNumBytesAsUTF8());
    const auto digest = hash.getRawData();

    juce::MemoryOutputStream encoded;
    juce::Base64::convertToBase64 (encoded, digest.getData(), digest.getSize());

    const auto actual = encoded.toString()
                            .replaceCharacter ('+', '-')
                            .replaceCharacter ('/', '_')
                            .removeCharacters ("=");

    std::printf ("expected: %s\nactual:   %s\n%s\n",
                 expected.toRawUTF8(), actual.toRawUTF8(),
                 actual == expected ? "OK" : "MISMATCH");

    return actual == expected ? 0 : 1;
}
```

`OnDeviceBuild/dependencies/JUCE/modules` を include パスに与えてビルドする。
`juce_core` は単体ではビルドできないため、`JuceHeader.h` を使わずに
`#define JUCE_STANDALONE_APPLICATION 1` などが必要なら適宜補うこと。
どうしてもリンクが通らない場合は、同じ計算を `CC_SHA256`（CommonCrypto）で
書いた等価なプログラムで確認してよい。**確認せずに次へ進まないこと。**

期待する出力: `OK`

- [ ] **Step 3: ループバックの待ち受けを実装する**

`Projucer/Source/AI/jucer_OAuthCallbackServer.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

/*  OAuth のリダイレクトを受けるだけの、極小のループバック HTTP 待ち受け。

    ローカルホストにしか bind しない。単一の要求 (/auth/callback) を処理したら
    役目を終える。汎用の HTTP サーバーではない。
*/
class OAuthCallbackServer
{
public:
    ~OAuthCallbackServer();

    /** 待ち受けを開始する。preferredPort が埋まっていれば OS 任せの空きポートを使う。 */
    bool start (int preferredPort);

    /** 実際に待ち受けているポート。redirect_uri の組み立てに使う。 */
    int getPort() const noexcept    { return port; }

    /** ブラウザからのコールバックを待つ。shouldStop で中断できる。

        @returns  code と state が取れたら true。
    */
    bool waitForCode (std::atomic<bool>& shouldStop,
                      juce::String& codeOut,
                      juce::String& stateOut,
                      juce::String& errorOut);

    void stop();

private:
    juce::StreamingSocket listener;
    int port = 0;
};
```

`Projucer/Source/AI/jucer_OAuthCallbackServer.cpp`:

```cpp
#include "jucer_OAuthCallbackServer.h"

namespace
{
    constexpr int maxRequestBytes = 8192;

    /*  ブラウザに返す最小のページ。ここで案内を出さないと、ユーザーは
        認可が終わったのかどうか分からないまま画面を見ることになる。 */
    juce::String makeResponse (bool success)
    {
        const juce::String body =
            juce::String ("<!doctype html><meta charset=\"utf-8\">"
                          "<title>Projucer</title>"
                          "<div style=\"font-family:-apple-system,sans-serif;"
                          "text-align:center;margin-top:20vh;font-size:1.2rem\">")
              + (success ? "サインインが完了しました。Projucer へ戻ってください。"
                         : "サインインに失敗しました。Projucer へ戻ってやり直してください。")
              + "</div>";

        return juce::String ("HTTP/1.1 ") + (success ? "200 OK" : "400 Bad Request") + "\r\n"
                 + "Content-Type: text/html; charset=utf-8\r\n"
                 + "Content-Length: " + juce::String (body.getNumBytesAsUTF8()) + "\r\n"
                 + "Connection: close\r\n\r\n"
                 + body;
    }

    /*  "GET /auth/callback?code=x&state=y HTTP/1.1" から値を取り出す。 */
    juce::String queryValue (const juce::String& target, const juce::String& key)
    {
        const auto questionMark = target.indexOfChar ('?');

        if (questionMark < 0)
            return {};

        for (const auto& pair : juce::StringArray::fromTokens (target.substring (questionMark + 1), "&", {}))
            if (pair.upToFirstOccurrenceOf ("=", false, false) == key)
                return juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false));

        return {};
    }
}

OAuthCallbackServer::~OAuthCallbackServer()
{
    stop();
}

bool OAuthCallbackServer::start (int preferredPort)
{
    // localhost にだけ bind する。外から到達させない。
    if (listener.createListener (preferredPort, "127.0.0.1"))
    {
        port = preferredPort;
        return true;
    }

    // 埋まっていたら OS に空きを選ばせる。redirect_uri は実ポートで組み立てる。
    if (listener.createListener (0, "127.0.0.1"))
    {
        port = listener.getBoundPort();
        return port > 0;
    }

    return false;
}

void OAuthCallbackServer::stop()
{
    listener.close();
    port = 0;
}

bool OAuthCallbackServer::waitForCode (std::atomic<bool>& shouldStop,
                                       juce::String& codeOut,
                                       juce::String& stateOut,
                                       juce::String& errorOut)
{
    while (! shouldStop.load())
    {
        // 中断要求に応じられるよう、細かく区切って待つ。
        if (listener.waitUntilReady (true, 200) != 1)
            continue;

        std::unique_ptr<juce::StreamingSocket> connection (listener.waitForNextConnection());

        if (connection == nullptr)
            continue;

        juce::MemoryBlock request;

        // 要求行だけ読めればよい。ヘッダの終端まで待たない。
        while ((int) request.getSize() < maxRequestBytes)
        {
            char buffer[1024];
            const auto bytesRead = connection->read (buffer, sizeof (buffer), false);

            if (bytesRead <= 0)
                break;

            request.append (buffer, (size_t) bytesRead);

            if (request.toString().containsChar ('\n'))
                break;
        }

        const auto requestLine = request.toString().upToFirstOccurrenceOf ("\r\n", false, false);
        const auto target = requestLine.fromFirstOccurrenceOf (" ", false, false)
                                       .upToLastOccurrenceOf (" ", false, false);

        // ブラウザは favicon なども取りに来る。目的の経路以外は無視して待ち続ける。
        if (! target.startsWith ("/auth/callback"))
        {
            const auto notFound = makeResponse (false);
            connection->write (notFound.toRawUTF8(), (int) notFound.getNumBytesAsUTF8());
            continue;
        }

        codeOut  = queryValue (target, "code");
        stateOut = queryValue (target, "state");

        const auto oauthError = queryValue (target, "error");
        const auto success = oauthError.isEmpty() && codeOut.isNotEmpty();

        const auto response = makeResponse (success);
        connection->write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8());

        if (! success)
        {
            errorOut = oauthError.isNotEmpty() ? "サインインが拒否されました（" + oauthError + "）。"
                                               : "認可コードを受け取れませんでした。";
            return false;
        }

        return true;
    }

    errorOut = "サインインを中止しました。";
    return false;
}
```

- [ ] **Step 4: 認可ページの提示を実装する**

`Projucer/Source/AI/jucer_AuthBrowser.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>

/*  認可ページをユーザーへ見せる。

    iOS では外部 Safari へ切り替えるとアプリがバックグラウンドへ回り、
    ループバックの待ち受けが止まってコールバックを取りこぼす。そのため
    ASWebAuthenticationSession でアプリ内に提示し、前面を保つ。
    macOS では通常のブラウザで開く。
*/
void presentAuthPage (const juce::String& authorizeUrl);

/** 提示中のページを閉じる。コールバックを受け取った後に呼ぶ。 */
void dismissAuthPage();
```

`Projucer/Source/AI/jucer_AuthBrowser.mm`:

```cpp
#include "jucer_AuthBrowser.h"

#import <Foundation/Foundation.h>

#if JUCE_IOS
 #import <AuthenticationServices/AuthenticationServices.h>
 #import <UIKit/UIKit.h>

/*  このプロジェクトは ARC を使わない（Task 3 参照）。__bridge 系のキャストは
    書かず、保持するオブジェクトは明示的に retain / release する。
*/

API_AVAILABLE (ios (13.0))
@interface ProjucerAuthAnchor : NSObject <ASWebAuthenticationPresentationContextProviding>
@end

@implementation ProjucerAuthAnchor
- (ASPresentationAnchor) presentationAnchorForWebAuthenticationSession: (ASWebAuthenticationSession*) session
{
    (void) session;

    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
        if ([scene isKindOfClass: [UIWindowScene class]])
            for (UIWindow* window in ((UIWindowScene*) scene).windows)
                if (window.isKeyWindow)
                    return window;

    return nil;
}
@end

namespace
{
    ASWebAuthenticationSession* activeSession = nil;
    ProjucerAuthAnchor* activeAnchor = nil;
}

void presentAuthPage (const juce::String& authorizeUrl)
{
    dismissAuthPage();

    auto* url = [NSURL URLWithString: [NSString stringWithUTF8String: authorizeUrl.toRawUTF8()]];

    if (url == nil)
        return;

    activeAnchor = [[ProjucerAuthAnchor alloc] init];

    // コールバックはループバックの HTTP 待ち受けが受ける。ここで scheme を
    // 横取りする必要はないので callbackURLScheme は nil でよい。
    activeSession = [[ASWebAuthenticationSession alloc] initWithURL: url
                                                  callbackURLScheme: nil
                                                 completionHandler: ^(NSURL* callbackUrl, NSError* error)
                                                 {
                                                     (void) callbackUrl;
                                                     (void) error;
                                                 }];

    activeSession.presentationContextProvider = activeAnchor;
    [activeSession start];
}

void dismissAuthPage()
{
    if (activeSession != nil)
    {
        [activeSession cancel];
        [activeSession release];
        activeSession = nil;
    }

    if (activeAnchor != nil)
    {
        [activeAnchor release];
        activeAnchor = nil;
    }
}

#else

void presentAuthPage (const juce::String& authorizeUrl)
{
    juce::URL (authorizeUrl).launchInDefaultBrowser();
}

void dismissAuthPage()
{
}

#endif
```

- [ ] **Step 5: `CodexAuth` にブラウザ経路を足す**

`jucer_CodexAuth.h` の public へ追加する:

```cpp
    /** 既定のサインイン経路。認可ページを提示し、ループバックでコードを受け取り、
        トークンまで交換して保存する。ネットワークを待つのでメッセージスレッド
        から呼ばないこと。 */
    bool signInWithBrowser (std::atomic<bool>& shouldStop, juce::String& errorOut);
```

`jucer_CodexAuth.cpp` に実装を追加する（`postForm` と `extractAccountId` は
Task 4 のものをそのまま使う）:

```cpp
bool CodexAuth::signInWithBrowser (std::atomic<bool>& shouldStop, juce::String& errorOut)
{
    OAuthCallbackServer server;

    if (! server.start (1455))
    {
        errorOut = "ローカルの待ち受けを開始できませんでした。";
        return false;
    }

    const auto redirectUri = "http://localhost:" + juce::String (server.getPort()) + "/auth/callback";
    const auto pkce = generatePkce();
    const auto expectedState = generateOAuthState();

    juce::String query;
    const auto add = [&query] (const juce::String& key, const juce::String& value)
    {
        if (query.isNotEmpty())
            query << "&";

        query << key << "=" << juce::URL::addEscapeChars (value, false);
    };

    add ("response_type", "code");
    add ("client_id", clientId);
    add ("redirect_uri", redirectUri);
    add ("scope", "openid profile email offline_access api.connectors.read api.connectors.invoke");
    add ("code_challenge", pkce.codeChallenge);
    add ("code_challenge_method", "S256");
    add ("id_token_add_organizations", "true");
    add ("codex_cli_simplified_flow", "true");
    add ("state", expectedState);
    add ("originator", "codex_cli_rs");

    presentAuthPage (juce::String (issuer) + "/oauth/authorize?" + query);

    juce::String code, returnedState;
    const auto gotCode = server.waitForCode (shouldStop, code, returnedState, errorOut);

    dismissAuthPage();
    server.stop();

    if (! gotCode)
        return false;

    // CSRF 対策。state が一致しない応答は捨てる。
    if (returnedState != expectedState)
    {
        errorOut = "サインインの応答が一致しませんでした。もう一度お試しください。";
        return false;
    }

    const auto form = juce::String ("grant_type=authorization_code")
                        + "&code=" + juce::URL::addEscapeChars (code, false)
                        + "&redirect_uri=" + juce::URL::addEscapeChars (redirectUri, false)
                        + "&client_id=" + juce::URL::addEscapeChars (clientId, false)
                        + "&code_verifier=" + juce::URL::addEscapeChars (pkce.codeVerifier, false);

    int status = 0;
    const auto response = postForm (juce::String (issuer) + "/oauth/token", form, status);

    if (status < 200 || status >= 300)
    {
        errorOut = "トークンの取得に失敗しました（HTTP " + juce::String (status) + "）。";
        return false;
    }

    const auto parsed = juce::JSON::parse (response);

    const juce::ScopedLock sl (lock);

    tokens.accessToken  = parsed.getProperty ("access_token", {}).toString();
    tokens.refreshToken = parsed.getProperty ("refresh_token", {}).toString();
    tokens.accountId    = extractAccountId (parsed.getProperty ("id_token", {}).toString());

    if (tokens.accessToken.isEmpty())
    {
        errorOut = "トークンの応答を解釈できませんでした。";
        return false;
    }

    save();
    return true;
}
```

`jucer_CodexAuth.cpp` の include に `jucer_Pkce.h`、`jucer_OAuthCallbackServer.h`、
`jucer_AuthBrowser.h` を足す。

- [ ] **Step 6: Mac で実際にサインインする**

Task 4 の一時ブロックを `signInWithBrowser` に差し替えて Mac の Debug ビルドで起動する。

```cpp
   #if 0   // 確認用。検証が終わったら消すこと。
    {
        static CodexAuth auth;
        static std::atomic<bool> stop { false };

        juce::Thread::launch ([]
        {
            juce::String error;

            if (auth.signInWithBrowser (stop, error))
                DBG ("サインイン成功。account_id: " << (auth.getAccountId().isNotEmpty() ? "取得できた" : "取得できなかった"));
            else
                DBG ("サインイン失敗: " << error);
        });
    }
   #endif
```

**確認すること:**

1. ブラウザが開き、ChatGPT のサインイン画面が出る
2. 承認すると `localhost:1455` へ戻り、「サインインが完了しました」が表示される
3. `access_token` / `refresh_token` / `account_id` がすべて取れる
4. アプリを再起動しても `isSignedIn()` が true（Keychain が効いている）
5. `refresh()` が成功する
6. 途中で `stop` を立てると待ち受けが止まり、UI が固まらない

**トークンは絶対に出力しない。** 上の `DBG` も有無しか出していない。

`account_id` が取れない場合は `id_token` のクレーム名を実際の中身に合わせて直す
（キー名だけを出力し、値は出さないこと）。

- [ ] **Step 7: コミットのコマンドを提示する（実行しない）**

```bash
git add Projucer/Source/AI/jucer_Pkce.h Projucer/Source/AI/jucer_Pkce.cpp \
        Projucer/Source/AI/jucer_OAuthCallbackServer.h Projucer/Source/AI/jucer_OAuthCallbackServer.cpp \
        Projucer/Source/AI/jucer_AuthBrowser.h Projucer/Source/AI/jucer_AuthBrowser.mm \
        Projucer/Source/AI/jucer_CodexAuth.h Projucer/Source/AI/jucer_CodexAuth.cpp
git commit -m "feat(ai): ブラウザ OAuth によるサインインを追加"
```

---

## Task 5: Codex クライアント（SSE リクエスト）

**Files:**
- Create: `Projucer/Source/AI/jucer_CodexClient.h`
- Create: `Projucer/Source/AI/jucer_CodexClient.cpp`

**Interfaces:**
- Consumes: Task 1 の `SseParser`、Task 4 の `CodexAuth`
- Produces: `CodexClient` クラス
  - `CodexClient (CodexAuth& auth)`
  - `bool streamResponse (const juce::var& requestBody, std::atomic<bool>& shouldStop, std::function<void (const juce::var&)> onEvent, juce::String& errorOut)`

- [ ] **Step 1: ヘッダを書く**

`Projucer/Source/AI/jucer_CodexClient.h`:

```cpp
#pragma once

#include "jucer_CodexAuth.h"
#include "jucer_SseParser.h"

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

/*  Codex バックエンドへの SSE リクエスト。

    この型が Codex 固有の知識を持つ唯一の場所。将来 Grok や Claude を足すときは
    ここを差し替える。上位（AgentLoop）へ Codex 固有の型を漏らさない。

    streamResponse() はネットワークを待つのでメッセージスレッドから呼ばないこと。
    onEvent は呼び出し元のスレッドで呼ばれる。UI を触るなら callAsync すること。
*/
class CodexClient
{
public:
    explicit CodexClient (CodexAuth& authToUse);

    /** リクエストを投げ、SSE イベントを 1 件ずつ onEvent へ渡す。

        401 が返ったらトークンを 1 回だけ更新して再試行する。二度目の 401 は
        再サインインを促して終わる。

        @returns  ストリームが正常に終わったら true。
    */
    bool streamResponse (const juce::var& requestBody,
                         std::atomic<bool>& shouldStop,
                         std::function<void (const juce::var&)> onEvent,
                         juce::String& errorOut);

    static constexpr const char* baseUrl = "https://chatgpt.com/backend-api/codex";

private:
    /** 1 回だけ試す。HTTP ステータスを statusOut へ返す。 */
    bool attempt (const juce::var& requestBody,
                  std::atomic<bool>& shouldStop,
                  const std::function<void (const juce::var&)>& onEvent,
                  int& statusOut,
                  juce::String& errorOut);

    CodexAuth& auth;
    juce::String sessionId { juce::Uuid().toDashedString() };
};
```

- [ ] **Step 2: 実装を書く**

`Projucer/Source/AI/jucer_CodexClient.cpp`:

```cpp
#include "jucer_CodexClient.h"

CodexClient::CodexClient (CodexAuth& authToUse)
    : auth (authToUse)
{
}

bool CodexClient::attempt (const juce::var& requestBody,
                           std::atomic<bool>& shouldStop,
                           const std::function<void (const juce::var&)>& onEvent,
                           int& statusOut,
                           juce::String& errorOut)
{
    statusOut = 0;

    juce::String headers;
    headers << "Authorization: Bearer " << auth.getAccessToken() << "\r\n"
            << "chatgpt-account-id: "   << auth.getAccountId()   << "\r\n"
            << "session_id: "           << sessionId             << "\r\n"
            << "originator: codex_cli_rs\r\n"
            << "OpenAI-Beta: responses=experimental\r\n"
            << "Content-Type: application/json\r\n"
            << "Accept: text/event-stream";

    juce::URL url (juce::String (baseUrl) + "/responses");
    url = url.withPOSTData (juce::JSON::toString (requestBody));

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                       .withExtraHeaders (headers)
                       .withConnectionTimeoutMs (60000)
                       .withStatusCode (&statusOut);

    auto stream = url.createInputStream (options);

    if (stream == nullptr)
    {
        errorOut = "接続できませんでした。";
        return false;
    }

    if (statusOut < 200 || statusOut >= 300)
        return false;   // errorOut は呼び出し元が状況に応じて設定する

    SseParser parser;
    juce::HeapBlock<char> chunk (16384);

    while (! shouldStop.load())
    {
        const auto bytesRead = stream->read (chunk.getData(), 16384);

        if (bytesRead <= 0)
            break;   // ストリーム終了

        for (const auto& payload : parser.feed (std::string (chunk.getData(), (std::size_t) bytesRead)))
        {
            if (payload == "[DONE]")
                return true;

            const auto parsed = juce::JSON::parse (payload);

            // 壊れた行は無視する。1 件のせいでストリーム全体を落とさない。
            if (! parsed.isVoid())
                onEvent (parsed);
        }
    }

    return true;
}

bool CodexClient::streamResponse (const juce::var& requestBody,
                                  std::atomic<bool>& shouldStop,
                                  std::function<void (const juce::var&)> onEvent,
                                  juce::String& errorOut)
{
    int status = 0;

    if (attempt (requestBody, shouldStop, onEvent, status, errorOut))
        return true;

    if (status != 401)
    {
        if (errorOut.isEmpty())
            errorOut = "リクエストに失敗しました（HTTP " + juce::String (status) + "）。";

        return false;
    }

    // access token が切れている。1 回だけ更新して재試行する。
    if (! auth.refresh (errorOut))
        return false;

    if (attempt (requestBody, shouldStop, onEvent, status, errorOut))
        return true;

    if (errorOut.isEmpty())
        errorOut = status == 401 ? "サインインが必要です。"
                                 : "リクエストに失敗しました（HTTP " + juce::String (status) + "）。";

    return false;
}
```

- [ ] **Step 3: エンドポイントを実機で確認する**

Task 4 の一時ブロックを流用し、サインイン成功後に最小のリクエストを 1 本投げる。

```cpp
   #if 0   // 確認用。Task 5 の検証が終わったら消すこと。
    {
        static CodexClient client (auth);
        static std::atomic<bool> stop { false };

        auto* message = new juce::DynamicObject();
        message->setProperty ("type", "message");
        message->setProperty ("role", "user");

        auto* text = new juce::DynamicObject();
        text->setProperty ("type", "input_text");
        text->setProperty ("text", "PONG とだけ返してください。");
        message->setProperty ("content", juce::var (juce::Array<juce::var> { juce::var (text) }));

        auto* body = new juce::DynamicObject();
        body->setProperty ("model", "gpt-5.1-codex");
        body->setProperty ("instructions", "簡潔に答えてください。");
        body->setProperty ("input", juce::var (juce::Array<juce::var> { juce::var (message) }));
        body->setProperty ("stream", true);
        body->setProperty ("store", false);

        juce::String error;

        const auto ok = client.streamResponse (juce::var (body), stop,
                                               [] (const juce::var& event)
                                               {
                                                   DBG ("イベント種別: " << event.getProperty ("type", {}).toString());
                                               },
                                               error);

        DBG (ok ? "ストリーム成功" : "ストリーム失敗: " + error);
    }
   #endif
```

**確認すること:**

1. HTTP 200 が返る（ヘッダが揃っているか）
2. SSE イベントが流れてくる
3. イベントの `type` の実際の値を控える。テキスト delta と function_call に
   対応する種別名が Task 7 で必要になる
4. `model` の値が受け付けられるか。拒否されたらエラー本文から使えるモデル名を確認する

**この確認で得たイベント種別名を Task 7 の実装に反映する。**
種別名が想定と違っていた場合、Task 7 の該当箇所を実際の値へ書き換えること。

- [ ] **Step 4: コミットのコマンドを提示する（実行しない）**

```bash
git add Projucer/Source/AI/jucer_CodexClient.h Projucer/Source/AI/jucer_CodexClient.cpp
git commit -m "feat(ai): Codex バックエンドへの SSE クライアントを追加"
```

---

## Task 6: ツール

**Files:**
- Create: `Projucer/Source/AI/jucer_AiTools.h`
- Create: `Projucer/Source/AI/jucer_AiTools.cpp`

**Interfaces:**
- Consumes: Task 2 の `resolveInsideRoot`
- Produces: `AiTools` クラス
  - `struct Result { bool ok = false; juce::String output; juce::String diffPreview; }`
  - `explicit AiTools (const juce::File& projectRoot)`
  - `static juce::var getToolSchemas()`
  - `static bool requiresApproval (const juce::String& toolName)`
  - `Result preview (const juce::String& toolName, const juce::var& arguments)`
  - `Result execute (const juce::String& toolName, const juce::var& arguments)`

- [ ] **Step 1: ヘッダを書く**

`Projucer/Source/AI/jucer_AiTools.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>

/*  AI が呼べるツール。

    パスは必ず resolveInsideRoot() を通してプロジェクトルート配下に閉じる。
    書き込み系は requiresApproval() が true を返し、実行前に承認を取る。

    execute() はファイル I/O を行うのでメッセージスレッドから呼ばないこと。
*/
class AiTools
{
public:
    explicit AiTools (const juce::File& projectRootToUse);

    struct Result
    {
        bool ok = false;
        juce::String output;        ///< モデルへ返す文字列。失敗理由もここへ入れる
        juce::String diffPreview;   ///< 承認 UI に見せる差分。承認不要なら空
    };

    /** リクエストの tools フィールドへそのまま入れられる配列を返す。 */
    static juce::var getToolSchemas();

    /** このツールが実行前に承認を要するか。 */
    static bool requiresApproval (const juce::String& toolName);

    /** 変更内容を差分として組み立てるだけで、ファイルには触らない。 */
    Result preview (const juce::String& toolName, const juce::var& arguments);

    /** 実際に実行する。承認が要るツールは preview() で承認を得た後に呼ぶこと。 */
    Result execute (const juce::String& toolName, const juce::var& arguments);

private:
    /** モデルが指定したパスを検証して実ファイルへ直す。外なら失敗。 */
    bool resolve (const juce::var& arguments, juce::File& fileOut, juce::String& errorOut) const;

    Result doListFiles (const juce::var& arguments) const;
    Result doReadFile  (const juce::var& arguments) const;
    Result doWriteFile (const juce::var& arguments, bool actuallyWrite) const;
    Result doApplyPatch (const juce::var& arguments, bool actuallyWrite) const;

    juce::File projectRoot;

    static constexpr int maxReadBytes = 1024 * 1024;
};
```

- [ ] **Step 2: 実装を書く**

`Projucer/Source/AI/jucer_AiTools.cpp`:

```cpp
#include "jucer_AiTools.h"
#include "jucer_AiPaths.h"

namespace
{
    /*  ツール 1 つ分のスキーマを組み立てる。 */
    juce::var makeTool (const juce::String& name,
                        const juce::String& description,
                        juce::DynamicObject* properties,
                        const juce::StringArray& required)
    {
        auto* parameters = new juce::DynamicObject();
        parameters->setProperty ("type", "object");
        parameters->setProperty ("properties", juce::var (properties));

        juce::Array<juce::var> requiredArray;

        for (const auto& name : required)
            requiredArray.add (name);

        parameters->setProperty ("required", requiredArray);

        auto* tool = new juce::DynamicObject();
        tool->setProperty ("type", "function");
        tool->setProperty ("name", name);
        tool->setProperty ("description", description);
        tool->setProperty ("parameters", juce::var (parameters));

        return juce::var (tool);
    }

    juce::var stringProperty (const juce::String& description)
    {
        auto* property = new juce::DynamicObject();
        property->setProperty ("type", "string");
        property->setProperty ("description", description);
        return juce::var (property);
    }

    juce::var integerProperty (const juce::String& description)
    {
        auto* property = new juce::DynamicObject();
        property->setProperty ("type", "integer");
        property->setProperty ("description", description);
        return juce::var (property);
    }

    /*  行単位の素朴な差分。承認画面で見せるためのもので、機械処理はしない。 */
    juce::String makeDiff (const juce::String& before, const juce::String& after)
    {
        juce::StringArray beforeLines, afterLines;
        beforeLines.addLines (before);
        afterLines.addLines (after);

        juce::String diff;

        const auto lineCount = juce::jmax (beforeLines.size(), afterLines.size());

        for (int i = 0; i < lineCount; ++i)
        {
            const auto oldLine = i < beforeLines.size() ? beforeLines[i] : juce::String();
            const auto newLine = i < afterLines.size()  ? afterLines[i]  : juce::String();

            if (oldLine == newLine)
                continue;

            if (i < beforeLines.size())
                diff << "- " << oldLine << "\n";

            if (i < afterLines.size())
                diff << "+ " << newLine << "\n";
        }

        return diff.isEmpty() ? "（変更なし）" : diff;
    }
}

//==============================================================================
AiTools::AiTools (const juce::File& projectRootToUse)
    : projectRoot (projectRootToUse)
{
}

bool AiTools::requiresApproval (const juce::String& toolName)
{
    return toolName == "write_file" || toolName == "apply_patch";
}

juce::var AiTools::getToolSchemas()
{
    juce::Array<juce::var> tools;

    {
        auto* properties = new juce::DynamicObject();
        properties->setProperty ("path", stringProperty ("一覧を取るディレクトリ。プロジェクトルートからの相対パス。省略時はルート。"));
        tools.add (makeTool ("list_files",
                             "プロジェクト内のファイルとディレクトリを一覧する。",
                             properties,
                             {}));
    }

    {
        auto* properties = new juce::DynamicObject();
        properties->setProperty ("path", stringProperty ("読むファイル。プロジェクトルートからの相対パス。"));
        properties->setProperty ("start_line", integerProperty ("開始行（1 始まり）。省略時は先頭から。"));
        properties->setProperty ("end_line", integerProperty ("終了行（この行を含む）。省略時は末尾まで。"));
        tools.add (makeTool ("read_file",
                             "ファイルの内容を読む。大きなファイルは行範囲を指定すること。",
                             properties,
                             { "path" }));
    }

    {
        auto* properties = new juce::DynamicObject();
        properties->setProperty ("path", stringProperty ("書き込むファイル。プロジェクトルートからの相対パス。"));
        properties->setProperty ("content", stringProperty ("ファイル全体の新しい内容。"));
        tools.add (makeTool ("write_file",
                             "ファイルを新規作成、または内容を全て置き換える。既存ファイルの一部を変える場合は apply_patch を使うこと。",
                             properties,
                             { "path", "content" }));
    }

    {
        auto* properties = new juce::DynamicObject();
        properties->setProperty ("path", stringProperty ("変更するファイル。プロジェクトルートからの相対パス。"));
        properties->setProperty ("old_text", stringProperty ("置き換える対象の文字列。ファイル内で一意に決まる十分な長さにすること。"));
        properties->setProperty ("new_text", stringProperty ("置き換えた後の文字列。"));
        tools.add (makeTool ("apply_patch",
                             "ファイル内の文字列を 1 箇所だけ置き換える。old_text がファイル内で一意でない場合は失敗する。",
                             properties,
                             { "path", "old_text", "new_text" }));
    }

    return juce::var (tools);
}

//==============================================================================
bool AiTools::resolve (const juce::var& arguments, juce::File& fileOut, juce::String& errorOut) const
{
    const auto path = arguments.getProperty ("path", {}).toString();

    if (path.isEmpty())
    {
        errorOut = "path が指定されていません。";
        return false;
    }

    const auto resolved = resolveInsideRoot (projectRoot.getFullPathName().toStdString(),
                                             path.toStdString());

    if (! resolved.has_value())
    {
        errorOut = "パス \"" + path + "\" はプロジェクトの外を指しています。プロジェクト内のファイルだけを扱えます。";
        return false;
    }

    fileOut = juce::File (juce::String (resolved->string()));
    return true;
}

AiTools::Result AiTools::preview (const juce::String& toolName, const juce::var& arguments)
{
    if (toolName == "write_file")  return doWriteFile (arguments, false);
    if (toolName == "apply_patch") return doApplyPatch (arguments, false);

    return { false, "ツール \"" + toolName + "\" は承認を必要としません。", {} };
}

AiTools::Result AiTools::execute (const juce::String& toolName, const juce::var& arguments)
{
    if (toolName == "list_files")  return doListFiles (arguments);
    if (toolName == "read_file")   return doReadFile (arguments);
    if (toolName == "write_file")  return doWriteFile (arguments, true);
    if (toolName == "apply_patch") return doApplyPatch (arguments, true);

    return { false, "不明なツール \"" + toolName + "\" が呼ばれました。", {} };
}

//==============================================================================
AiTools::Result AiTools::doListFiles (const juce::var& arguments) const
{
    auto directory = projectRoot;

    if (arguments.getProperty ("path", {}).toString().isNotEmpty())
    {
        juce::String error;

        if (! resolve (arguments, directory, error))
            return { false, error, {} };
    }

    if (! directory.isDirectory())
        return { false, "\"" + directory.getFileName() + "\" はディレクトリではありません。", {} };

    juce::StringArray names;

    for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*",
                                                            juce::File::findFilesAndDirectories))
    {
        const auto file = entry.getFile();
        auto relative = file.getRelativePathFrom (projectRoot);

        if (file.isDirectory())
            relative << "/";

        names.add (relative);
    }

    names.sort (true);

    if (names.isEmpty())
        return { true, "（空のディレクトリです）", {} };

    return { true, names.joinIntoString ("\n"), {} };
}

AiTools::Result AiTools::doReadFile (const juce::var& arguments) const
{
    juce::File file;
    juce::String error;

    if (! resolve (arguments, file, error))
        return { false, error, {} };

    if (! file.existsAsFile())
        return { false, "ファイルが見つかりません。", {} };

    if (file.getSize() > maxReadBytes)
        return { false, "ファイルが大きすぎます（" + juce::String (file.getSize() / 1024) + " KB）。"
                        "start_line と end_line で範囲を指定してください。", {} };

    const auto content = file.loadFileAsString();

    const auto startLine = (int) arguments.getProperty ("start_line", 0);
    const auto endLine   = (int) arguments.getProperty ("end_line", 0);

    if (startLine <= 0 && endLine <= 0)
        return { true, content, {} };

    juce::StringArray lines;
    lines.addLines (content);

    const auto first = juce::jmax (1, startLine) - 1;
    const auto last  = endLine > 0 ? juce::jmin (lines.size(), endLine) : lines.size();

    if (first >= last)
        return { false, "指定された行範囲が不正です。", {} };

    juce::StringArray selected;

    for (int i = first; i < last; ++i)
        selected.add (lines[i]);

    return { true, selected.joinIntoString ("\n"), {} };
}

AiTools::Result AiTools::doWriteFile (const juce::var& arguments, bool actuallyWrite) const
{
    juce::File file;
    juce::String error;

    if (! resolve (arguments, file, error))
        return { false, error, {} };

    const auto newContent = arguments.getProperty ("content", {}).toString();
    const auto oldContent = file.existsAsFile() ? file.loadFileAsString() : juce::String();

    if (! actuallyWrite)
        return { true, {}, makeDiff (oldContent, newContent) };

    if (! file.getParentDirectory().createDirectory())
        return { false, "ディレクトリを作成できませんでした。", {} };

    if (! file.replaceWithText (newContent))
        return { false, "ファイルを書き込めませんでした。", {} };

    return { true, "\"" + file.getRelativePathFrom (projectRoot) + "\" を書き込みました。", {} };
}

AiTools::Result AiTools::doApplyPatch (const juce::var& arguments, bool actuallyWrite) const
{
    juce::File file;
    juce::String error;

    if (! resolve (arguments, file, error))
        return { false, error, {} };

    if (! file.existsAsFile())
        return { false, "ファイルが見つかりません。新規作成には write_file を使ってください。", {} };

    const auto oldText = arguments.getProperty ("old_text", {}).toString();
    const auto newText = arguments.getProperty ("new_text", {}).toString();

    if (oldText.isEmpty())
        return { false, "old_text が空です。", {} };

    const auto content = file.loadFileAsString();
    const auto firstIndex = content.indexOf (oldText);

    if (firstIndex < 0)
        return { false, "old_text がファイル内に見つかりません。read_file で現在の内容を確認してください。", {} };

    if (content.indexOf (firstIndex + oldText.length(), oldText) >= 0)
        return { false, "old_text がファイル内に複数あります。前後を含めて一意になるまで広げてください。", {} };

    const auto updated = content.replaceSection (firstIndex, oldText.length(), newText);

    if (! actuallyWrite)
        return { true, {}, makeDiff (oldText, newText) };

    if (! file.replaceWithText (updated))
        return { false, "ファイルを書き込めませんでした。", {} };

    return { true, "\"" + file.getRelativePathFrom (projectRoot) + "\" を更新しました。", {} };
}
```

- [ ] **Step 3: 自己チェックがまだ通ることを確認する**

`AiTools` は JUCE に依存するので自己チェックの対象外だが、`jucer_AiPaths` を
壊していないことを確認する。

Run: `./scripts/run_ai_selfcheck.sh`
Expected: `すべて成功`

- [ ] **Step 4: コミットのコマンドを提示する（実行しない）**

```bash
git add Projucer/Source/AI/jucer_AiTools.h Projucer/Source/AI/jucer_AiTools.cpp
git commit -m "feat(ai): ファイル読み書きツールを追加"
```

---

## Task 7: 会話状態とエージェントループ

**Files:**
- Create: `Projucer/Source/AI/jucer_AiSession.h`
- Create: `Projucer/Source/AI/jucer_AiSession.cpp`
- Create: `Projucer/Source/AI/jucer_AgentLoop.h`
- Create: `Projucer/Source/AI/jucer_AgentLoop.cpp`

**Interfaces:**
- Consumes: Task 4 `CodexAuth`、Task 5 `CodexClient`、Task 6 `AiTools`
- Produces:
  - `AiSession` — 会話履歴とターン状態。`ProjectContentComponent` が所有する
  - `AgentLoop` — `AiSession` が内部で使う

`AiSession` の公開メソッド:
- `AiSession (CodexAuth& auth, const juce::File& projectRoot)`
- `void sendMessage (const juce::String& text)`
- `void stop()`
- `bool isBusy() const`
- `struct Entry { enum class Kind { user, assistant, tool, error }; Kind kind; juce::String text; }`
- `const juce::Array<Entry>& getEntries() const`
- `struct PendingApproval { juce::String callId, toolName, diffPreview; }`
- `const PendingApproval* getPendingApproval() const`
- `void resolveApproval (bool approved)`
- `void setAutoApprove (bool shouldAutoApprove)`
- `bool getAutoApprove() const`
- `void addChangeListener` / `removeChangeListener`（`juce::ChangeBroadcaster` を継承）

- [ ] **Step 1: `AiSession` のヘッダを書く**

`Projucer/Source/AI/jucer_AiSession.h`:

```cpp
#pragma once

#include "jucer_AiTools.h"
#include "jucer_CodexClient.h"

#include <juce_events/juce_events.h>
#include <atomic>
#include <memory>

class AgentLoop;

/*  1 プロジェクト分の AI 会話。

    ProjectContentComponent が所有する。AiChatView は ContentViewComponent の
    setContent() で差し替えられて消えるが、この型は生き残るので、AI タブを
    離れて戻っても会話が続く。

    状態が変わると ChangeBroadcaster として通知する。View はそれを受けて
    再描画する。通知は必ずメッセージスレッドで出す。
*/
class AiSession final : public juce::ChangeBroadcaster
{
public:
    AiSession (CodexAuth& auth, const juce::File& projectRoot);
    ~AiSession() override;

    struct Entry
    {
        enum class Kind { user, assistant, tool, error };

        Kind kind = Kind::assistant;
        juce::String text;
    };

    struct PendingApproval
    {
        juce::String callId;
        juce::String toolName;
        juce::String diffPreview;
    };

    /** ユーザーの発言を投げ、ターンを開始する。実行中なら何もしない。 */
    void sendMessage (const juce::String& text);

    /** 実行中のターンを止める。 */
    void stop();

    bool isBusy() const;

    const juce::Array<Entry>& getEntries() const   { return entries; }

    /** 承認待ちがあれば返す。無ければ nullptr。 */
    const PendingApproval* getPendingApproval() const;

    /** 承認 UI からの応答。 */
    void resolveApproval (bool approved);

    void setAutoApprove (bool shouldAutoApprove);
    bool getAutoApprove() const                    { return autoApprove; }

private:
    friend class AgentLoop;

    /** AgentLoop から呼ばれる。必ずメッセージスレッドへ移してから使うこと。 */
    void appendEntry (Entry::Kind kind, const juce::String& text);
    void appendToLastAssistantEntry (const juce::String& delta);
    void setPendingApproval (const PendingApproval& approval);
    void clearPendingApproval();
    void finishTurn();

    CodexAuth& auth;
    juce::File projectRoot;

    juce::Array<Entry> entries;
    std::unique_ptr<PendingApproval> pendingApproval;
    std::unique_ptr<AgentLoop> loop;

    bool autoApprove = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiSession)
};
```

- [ ] **Step 2: `AgentLoop` のヘッダを書く**

`Projucer/Source/AI/jucer_AgentLoop.h`:

```cpp
#pragma once

#include "jucer_AiSession.h"
#include "jucer_AiTools.h"
#include "jucer_CodexClient.h"

#include <juce_core/juce_core.h>
#include <atomic>

/*  1 ターン分のエージェントループを専用スレッドで回す。

    送信 → SSE 受信 → ツール実行 → 結果を積んで再送、を繰り返す。
    ツール呼び出しが無くなったらターン終了。

    承認が要るツールに当たったら、承認待ちフラグを立てて待つ。
    UI 側が resolveApproval() を呼ぶと再開する。

    UI への反映は必ず MessageManager::callAsync 経由で行う。
*/
class AgentLoop final : public juce::Thread
{
public:
    AgentLoop (AiSession& session, CodexAuth& auth, const juce::File& projectRoot);
    ~AgentLoop() override;

    /** ユーザー発言を履歴へ積んでスレッドを起動する。 */
    void start (const juce::String& userMessage);

    /** 中断を要求する。run() は次の区切りで抜ける。 */
    void requestStop();

    /** 承認 UI からの応答を受けてループを再開させる。 */
    void provideApproval (bool approved);

    void run() override;

private:
    /** モデルへ送るリクエストボディを組み立てる。 */
    juce::var buildRequestBody() const;

    /** 承認が下りるまで待つ。中断されたら false。 */
    bool waitForApproval();

    AiSession& session;
    CodexClient client;
    AiTools tools;

    juce::Array<juce::var> conversation;   ///< これまでの input 全部。毎回送る
    juce::String pendingUserMessage;

    std::atomic<bool> shouldStop { false };

    juce::WaitableEvent approvalArrived;
    std::atomic<bool> approvalGranted { false };

    static constexpr int maxIterations = 25;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AgentLoop)
};
```

- [ ] **Step 3: `AiSession` を実装する**

`Projucer/Source/AI/jucer_AiSession.cpp`:

```cpp
#include "jucer_AiSession.h"
#include "jucer_AgentLoop.h"

AiSession::AiSession (CodexAuth& authToUse, const juce::File& projectRootToUse)
    : auth (authToUse), projectRoot (projectRootToUse)
{
}

AiSession::~AiSession()
{
    if (loop != nullptr)
    {
        loop->requestStop();
        loop->stopThread (5000);
    }
}

void AiSession::sendMessage (const juce::String& text)
{
    if (text.trim().isEmpty() || isBusy())
        return;

    if (loop == nullptr)
        loop = std::make_unique<AgentLoop> (*this, auth, projectRoot);

    appendEntry (Entry::Kind::user, text);
    loop->start (text);
    sendChangeMessage();
}

void AiSession::stop()
{
    if (loop != nullptr)
        loop->requestStop();
}

bool AiSession::isBusy() const
{
    return loop != nullptr && loop->isThreadRunning();
}

const AiSession::PendingApproval* AiSession::getPendingApproval() const
{
    return pendingApproval.get();
}

void AiSession::resolveApproval (bool approved)
{
    if (loop != nullptr)
        loop->provideApproval (approved);
}

void AiSession::setAutoApprove (bool shouldAutoApprove)
{
    autoApprove = shouldAutoApprove;
    sendChangeMessage();
}

//==============================================================================
void AiSession::appendEntry (Entry::Kind kind, const juce::String& text)
{
    JUCE_ASSERT_MESSAGE_THREAD
    entries.add ({ kind, text });
    sendChangeMessage();
}

void AiSession::appendToLastAssistantEntry (const juce::String& delta)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (entries.isEmpty() || entries.getReference (entries.size() - 1).kind != Entry::Kind::assistant)
        entries.add ({ Entry::Kind::assistant, {} });

    entries.getReference (entries.size() - 1).text << delta;
    sendChangeMessage();
}

void AiSession::setPendingApproval (const PendingApproval& approval)
{
    JUCE_ASSERT_MESSAGE_THREAD
    pendingApproval = std::make_unique<PendingApproval> (approval);
    sendChangeMessage();
}

void AiSession::clearPendingApproval()
{
    JUCE_ASSERT_MESSAGE_THREAD
    pendingApproval.reset();
    sendChangeMessage();
}

void AiSession::finishTurn()
{
    JUCE_ASSERT_MESSAGE_THREAD
    sendChangeMessage();
}
```

- [ ] **Step 4: `AgentLoop` を実装する**

`Projucer/Source/AI/jucer_AgentLoop.cpp`:

```cpp
#include "jucer_AgentLoop.h"
#include "../Application/jucer_Application.h"

namespace
{
    /*  メッセージスレッドで安全に session を触るためのヘルパ。
        session は AgentLoop より長生きするので参照で捕まえてよい。 */
    template <typename Fn>
    void onMessageThread (Fn&& fn)
    {
        juce::MessageManager::callAsync (std::forward<Fn> (fn));
    }

    juce::var makeUserMessage (const juce::String& text)
    {
        auto* content = new juce::DynamicObject();
        content->setProperty ("type", "input_text");
        content->setProperty ("text", text);

        auto* message = new juce::DynamicObject();
        message->setProperty ("type", "message");
        message->setProperty ("role", "user");
        message->setProperty ("content", juce::var (juce::Array<juce::var> { juce::var (content) }));

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
        "あなたは Projucer に組み込まれた JUCE / C++ のコーディング支援アシスタントです。\n"
        "\n"
        "- 変更を提案する前に、必ず read_file で現在の内容を確認してください。\n"
        "- 既存ファイルの一部を変えるときは apply_patch を使い、write_file で全体を上書きしないでください。\n"
        "- old_text はファイル内で一意になる十分な長さにしてください。\n"
        "- 周囲のコードの書き方（命名、インデント、コメントの量）に合わせてください。\n"
        "- 回答は日本語で、簡潔に書いてください。";
}

//==============================================================================
AgentLoop::AgentLoop (AiSession& sessionToUse, CodexAuth& auth, const juce::File& projectRoot)
    : juce::Thread ("AI Agent Loop"),
      session (sessionToUse),
      client (auth),
      tools (projectRoot)
{
}

AgentLoop::~AgentLoop()
{
    requestStop();
    stopThread (5000);
}

void AgentLoop::start (const juce::String& userMessage)
{
    if (isThreadRunning())
        return;

    shouldStop = false;
    pendingUserMessage = userMessage;
    startThread();
}

void AgentLoop::requestStop()
{
    shouldStop = true;
    approvalArrived.signal();   // 承認待ちで止まっている場合に解放する
}

void AgentLoop::provideApproval (bool approved)
{
    approvalGranted = approved;
    approvalArrived.signal();
}

//==============================================================================
juce::var AgentLoop::buildRequestBody() const
{
    auto* body = new juce::DynamicObject();
    body->setProperty ("model", "gpt-5.1-codex");
    body->setProperty ("instructions", systemInstructions);
    body->setProperty ("input", juce::var (conversation));
    body->setProperty ("tools", AiTools::getToolSchemas());
    body->setProperty ("stream", true);
    body->setProperty ("store", false);

    return juce::var (body);
}

bool AgentLoop::waitForApproval()
{
    approvalArrived.reset();

    while (! shouldStop.load())
    {
        if (approvalArrived.wait (200))
            return approvalGranted.load() && ! shouldStop.load();
    }

    return false;
}

void AgentLoop::run()
{
    conversation.add (makeUserMessage (pendingUserMessage));

    for (int iteration = 0; iteration < maxIterations && ! shouldStop.load(); ++iteration)
    {
        // このターンで集めた function_call。SSE を読みながら埋まる。
        struct PendingCall
        {
            juce::String callId, name, argumentsJson;
        };

        juce::Array<PendingCall> calls;
        juce::var assistantOutputItem;

        juce::String error;

        const auto ok = client.streamResponse (buildRequestBody(), shouldStop,
            [&] (const juce::var& event)
            {
                const auto type = event.getProperty ("type", {}).toString();

                // テキストの逐次出力。
                if (type == "response.output_text.delta")
                {
                    const auto delta = event.getProperty ("delta", {}).toString();

                    onMessageThread ([&session = session, delta]
                                     { session.appendToLastAssistantEntry (delta); });
                    return;
                }

                // 出力項目が確定したところで function_call を拾う。
                if (type == "response.output_item.done")
                {
                    const auto item = event.getProperty ("item", {});

                    if (item.getProperty ("type", {}).toString() == "function_call")
                    {
                        PendingCall call;
                        call.callId        = item.getProperty ("call_id", {}).toString();
                        call.name          = item.getProperty ("name", {}).toString();
                        call.argumentsJson = item.getProperty ("arguments", {}).toString();

                        calls.add (call);
                    }

                    return;
                }

                if (type == "response.failed" || type == "error")
                {
                    const auto message = event.getProperty ("message", {}).toString();

                    onMessageThread ([&session = session, message]
                                     { session.appendEntry (AiSession::Entry::Kind::error,
                                                            message.isEmpty() ? "応答に失敗しました。" : message); });
                }
            },
            error);

        if (! ok)
        {
            onMessageThread ([&session = session, error]
                             { session.appendEntry (AiSession::Entry::Kind::error, error); });
            break;
        }

        // ツール呼び出しが無ければこのターンは終わり。
        if (calls.isEmpty())
            break;

        for (const auto& call : calls)
        {
            if (shouldStop.load())
                break;

            const auto arguments = juce::JSON::parse (call.argumentsJson);

            // アシスタントの function_call 自体も履歴へ積む。
            // これが無いと次のリクエストで call_id が宙に浮く。
            {
                auto* item = new juce::DynamicObject();
                item->setProperty ("type", "function_call");
                item->setProperty ("call_id", call.callId);
                item->setProperty ("name", call.name);
                item->setProperty ("arguments", call.argumentsJson);
                conversation.add (juce::var (item));
            }

            juce::String toolOutput;

            if (AiTools::requiresApproval (call.name) && ! session.getAutoApprove())
            {
                const auto previewResult = tools.preview (call.name, arguments);

                if (! previewResult.ok)
                {
                    conversation.add (makeFunctionCallOutput (call.callId, previewResult.output));

                    onMessageThread ([&session = session, text = previewResult.output]
                                     { session.appendEntry (AiSession::Entry::Kind::error, text); });
                    continue;
                }

                AiSession::PendingApproval approval;
                approval.callId      = call.callId;
                approval.toolName    = call.name;
                approval.diffPreview = previewResult.diffPreview;

                onMessageThread ([&session = session, approval]
                                 { session.setPendingApproval (approval); });

                const auto approved = waitForApproval();

                onMessageThread ([&session = session] { session.clearPendingApproval(); });

                if (! approved)
                {
                    toolOutput = "ユーザーがこの変更を却下しました。別の方法を検討するか、"
                                 "何をしようとしたか説明してください。";

                    conversation.add (makeFunctionCallOutput (call.callId, toolOutput));

                    onMessageThread ([&session = session, name = call.name]
                                     { session.appendEntry (AiSession::Entry::Kind::tool,
                                                            name + " を却下しました"); });
                    continue;
                }
            }

            const auto result = tools.execute (call.name, arguments);
            toolOutput = result.output;

            // 書き込んだら、開いているエディタを実ファイルに合わせる。
            // これを怠ると、古い内容を持ったエディタをユーザーが保存した瞬間に
            // AI の変更が黙って消える。体裁ではなくデータ損失の防止。
            if (result.ok && AiTools::requiresApproval (call.name))
                onMessageThread ([]
                                 { ProjucerApplication::getApp().openDocumentManager.reloadModifiedFiles(); });

            // 成功も失敗もモデルへ返す。隠すと同じ失敗を繰り返す。
            conversation.add (makeFunctionCallOutput (call.callId, toolOutput));

            onMessageThread ([&session = session, name = call.name, ok = result.ok]
                             { session.appendEntry (AiSession::Entry::Kind::tool,
                                                    name + (ok ? " を実行しました" : " が失敗しました")); });
        }

        if (! shouldStop.load() && iteration == maxIterations - 1)
        {
            onMessageThread ([&session = session]
                             { session.appendEntry (AiSession::Entry::Kind::error,
                                                    "ツール呼び出しの上限に達したため停止しました。"); });
        }
    }

    onMessageThread ([&session = session] { session.finishTurn(); });
}
```

**注意:** SSE イベントの種別名（`response.output_text.delta`、
`response.output_item.done`）は Responses API の一般的な名前である。Task 5 の
実機確認で実際の値が違っていた場合、ここを実測値へ書き換えること。

- [ ] **Step 5: 自己チェックがまだ通ることを確認する**

Run: `./scripts/run_ai_selfcheck.sh`
Expected: `すべて成功`

- [ ] **Step 6: コミットのコマンドを提示する（実行しない）**

```bash
git add Projucer/Source/AI/jucer_AiSession.h Projucer/Source/AI/jucer_AiSession.cpp \
        Projucer/Source/AI/jucer_AgentLoop.h Projucer/Source/AI/jucer_AgentLoop.cpp
git commit -m "feat(ai): 会話状態とエージェントループを追加"
```

---

## Task 8: チャット UI

> **Task 4b 追加に伴う変更（この節のコードより優先する）**
>
> このタスクのコードはデバイスコード経路だけを想定して書かれている。Task 4b で
> ブラウザ OAuth が既定になったため、サインインカードは**二経路**を出すこと。
>
> - 主ボタン「ChatGPT でサインイン」→ 別スレッドで `auth.signInWithBrowser (signInShouldStop, error)`
>   を呼ぶ。成功したら `updateVisibility()`、失敗したらエラー文言を出してボタンを戻す。
>   スレッドの扱いは既存の `startSignIn()` と同じ形にする。
> - 副リンク「うまくいかない場合: デバイスコードでサインイン」→ 押されたときだけ
>   既存のコード表示 UI（`signInCode` / `openBrowserButton` / `cancelSignInButton`）を
>   見せ、`requestDeviceCode` + `pollForTokens` の既存経路を走らせる。
> - デバイスコード側には「ChatGPT のセキュリティ設定でデバイスコード認証を
>   有効にする必要があります」という一文を添える。有効化されていないアカウントでは
>   `requestDeviceCode` が 404 相当を返し、`errorOut` にその旨が入る。
> - `AiChatView` の破棄時に `dismissAuthPage()` を呼び、提示中の認可ページを閉じること。
>
> それ以外（会話履歴、承認カード、入力欄、レイアウト）は以下のコードのままでよい。

**Files:**
- Create: `Projucer/Source/AI/jucer_AiChatView.h`
- Create: `Projucer/Source/AI/jucer_AiChatView.cpp`

**Interfaces:**
- Consumes: Task 4 `CodexAuth`、Task 7 `AiSession`
- Produces: `AiChatView`。`ContentViewComponent::setContent()` に渡せる `juce::Component`
  - `AiChatView (AiSession& session, CodexAuth& auth)`

- [ ] **Step 1: ヘッダを書く**

`Projucer/Source/AI/jucer_AiChatView.h`:

```cpp
#pragma once

#include "jucer_AiSession.h"
#include "jucer_CodexAuth.h"

#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>

/*  AI チャットの画面。

    ContentViewComponent::setContent() に渡されるので、いつ破棄されてもよい。
    会話の状態は AiSession が持っており、この型は表示だけを受け持つ。

    上から順に:
      1. 未サインインならサインインカード
      2. 会話履歴
      3. 承認待ちなら差分カード
      4. 入力欄

    キーボード表示時は MainWindow の inset 機構でコンテンツ全体の高さが縮むため、
    下端固定の入力欄が自然にキーボード直上へ来る。ここで特別な処理は要らない。
*/
class AiChatView final : public juce::Component,
                         private juce::ChangeListener
{
public:
    AiChatView (AiSession& sessionToUse, CodexAuth& authToUse);
    ~AiChatView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void startSignIn();
    void cancelSignIn();
    void rebuildHistory();
    void updateVisibility();
    void sendCurrentInput();

    AiSession& session;
    CodexAuth& auth;

    //== サインイン
    juce::Component signInCard;
    juce::Label signInTitle { {}, "ChatGPT アカウントでサインイン" };
    juce::Label signInCode;
    juce::Label signInInstructions;
    juce::TextButton signInButton { "サインインを開始" };
    juce::TextButton openBrowserButton { "ブラウザで開く" };
    juce::TextButton cancelSignInButton { "中止" };

    std::atomic<bool> signInShouldStop { false };
    juce::String verificationUrl;

    //== 会話
    juce::TextEditor history;

    //== 承認
    juce::Component approvalCard;
    juce::Label approvalTitle;
    juce::TextEditor approvalDiff;
    juce::TextButton approveButton { "適用" };
    juce::TextButton rejectButton { "却下" };
    juce::ToggleButton autoApproveToggle { "以後このセッションでは自動承認" };

    //== 入力
    juce::TextEditor input;
    juce::TextButton sendButton { "送信" };
    juce::TextButton stopButton { "停止" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiChatView)
};
```

- [ ] **Step 2: 実装を書く**

`Projucer/Source/AI/jucer_AiChatView.cpp`:

```cpp
#include "jucer_AiChatView.h"

namespace
{
    constexpr int rowHeight = 34;
    constexpr int inputHeight = 80;
    constexpr int padding = 10;

    juce::String prefixFor (AiSession::Entry::Kind kind)
    {
        switch (kind)
        {
            case AiSession::Entry::Kind::user:      return "あなた: ";
            case AiSession::Entry::Kind::assistant: return "AI: ";
            case AiSession::Entry::Kind::tool:      return "  · ";
            case AiSession::Entry::Kind::error:     return "エラー: ";
        }

        return {};
    }
}

//==============================================================================
AiChatView::AiChatView (AiSession& sessionToUse, CodexAuth& authToUse)
    : session (sessionToUse), auth (authToUse)
{
    //== サインインカード
    addAndMakeVisible (signInCard);

    signInTitle.setJustificationType (juce::Justification::centred);
    signInCard.addAndMakeVisible (signInTitle);

    signInCode.setJustificationType (juce::Justification::centred);
    signInCode.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    signInCard.addAndMakeVisible (signInCode);

    signInInstructions.setJustificationType (juce::Justification::centred);
    signInInstructions.setText ("サインインを開始すると、ここにコードが表示されます。",
                                juce::dontSendNotification);
    signInCard.addAndMakeVisible (signInInstructions);

    signInButton.onClick = [this] { startSignIn(); };
    signInCard.addAndMakeVisible (signInButton);

    openBrowserButton.onClick = [this]
    {
        if (verificationUrl.isNotEmpty())
            juce::URL (verificationUrl).launchInDefaultBrowser();
    };
    signInCard.addChildComponent (openBrowserButton);

    cancelSignInButton.onClick = [this] { cancelSignIn(); };
    signInCard.addChildComponent (cancelSignInButton);

    //== 会話
    history.setMultiLine (true);
    history.setReadOnly (true);
    history.setScrollbarsShown (true);
    history.setCaretVisible (false);
    addAndMakeVisible (history);

    //== 承認カード
    approvalTitle.setText ("この変更を適用しますか？", juce::dontSendNotification);
    approvalCard.addAndMakeVisible (approvalTitle);

    approvalDiff.setMultiLine (true);
    approvalDiff.setReadOnly (true);
    approvalDiff.setScrollbarsShown (true);
    approvalDiff.setCaretVisible (false);
    approvalDiff.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, 0));
    approvalCard.addAndMakeVisible (approvalDiff);

    approveButton.onClick = [this] { session.resolveApproval (true); };
    approvalCard.addAndMakeVisible (approveButton);

    rejectButton.onClick = [this] { session.resolveApproval (false); };
    approvalCard.addAndMakeVisible (rejectButton);

    autoApproveToggle.setToggleState (session.getAutoApprove(), juce::dontSendNotification);
    autoApproveToggle.onClick = [this] { session.setAutoApprove (autoApproveToggle.getToggleState()); };
    approvalCard.addAndMakeVisible (autoApproveToggle);

    addChildComponent (approvalCard);

    //== 入力
    input.setMultiLine (true);
    input.setReturnKeyStartsNewLine (false);
    input.setTextToShowWhenEmpty ("やりたいことを書いてください", juce::Colours::grey);
    input.onReturnKey = [this] { sendCurrentInput(); };
    addAndMakeVisible (input);

    sendButton.onClick = [this] { sendCurrentInput(); };
    addAndMakeVisible (sendButton);

    stopButton.onClick = [this] { session.stop(); };
    addChildComponent (stopButton);

    session.addChangeListener (this);
    rebuildHistory();
    updateVisibility();
}

AiChatView::~AiChatView()
{
    signInShouldStop = true;
    session.removeChangeListener (this);
}

//==============================================================================
void AiChatView::startSignIn()
{
    signInShouldStop = false;
    signInButton.setEnabled (false);
    signInInstructions.setText ("コードを取得しています…", juce::dontSendNotification);

    // ネットワーク待ちなのでメッセージスレッドから外す。
    juce::Thread::launch ([this]
    {
        juce::String error;
        auto code = auth.requestDeviceCode (error);

        if (! code.has_value())
        {
            juce::MessageManager::callAsync ([this, error]
            {
                signInInstructions.setText (error, juce::dontSendNotification);
                signInButton.setEnabled (true);
            });

            return;
        }

        verificationUrl = code->verificationUrl;

        juce::MessageManager::callAsync ([this, userCode = code->userCode]
        {
            signInCode.setText (userCode, juce::dontSendNotification);
            signInInstructions.setText ("ブラウザで " + verificationUrl + " を開き、\n"
                                        "上のコードを入力してください。",
                                        juce::dontSendNotification);
            openBrowserButton.setVisible (true);
            cancelSignInButton.setVisible (true);
            signInCard.resized();
        });

        const auto ok = auth.pollForTokens (*code, signInShouldStop, error);

        juce::MessageManager::callAsync ([this, ok, error]
        {
            if (ok)
            {
                updateVisibility();
                return;
            }

            signInCode.setText ({}, juce::dontSendNotification);
            signInInstructions.setText (error, juce::dontSendNotification);
            signInButton.setEnabled (true);
            openBrowserButton.setVisible (false);
            cancelSignInButton.setVisible (false);
            signInCard.resized();
        });
    });
}

void AiChatView::cancelSignIn()
{
    signInShouldStop = true;
}

//==============================================================================
void AiChatView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildHistory();
    updateVisibility();
}

void AiChatView::rebuildHistory()
{
    juce::String text;

    for (const auto& entry : session.getEntries())
        text << prefixFor (entry.kind) << entry.text << "\n\n";

    // 末尾を見せ続ける。ユーザーが上へスクロールしている最中でも、
    // ストリーミング中は最新行を追う方が自然。
    history.setText (text, juce::dontSendNotification);
    history.moveCaretToEnd();
}

void AiChatView::updateVisibility()
{
    const auto signedIn = auth.isSignedIn();

    signInCard.setVisible (! signedIn);
    history.setVisible (signedIn);
    input.setVisible (signedIn);
    sendButton.setVisible (signedIn && ! session.isBusy());
    stopButton.setVisible (signedIn && session.isBusy());

    const auto* approval = session.getPendingApproval();
    approvalCard.setVisible (signedIn && approval != nullptr);

    if (approval != nullptr)
    {
        approvalTitle.setText (approval->toolName + " による変更を適用しますか？",
                               juce::dontSendNotification);
        approvalDiff.setText (approval->diffPreview, juce::dontSendNotification);
    }

    autoApproveToggle.setToggleState (session.getAutoApprove(), juce::dontSendNotification);

    resized();
}

void AiChatView::sendCurrentInput()
{
    const auto text = input.getText();

    if (text.trim().isEmpty())
        return;

    input.clear();
    session.sendMessage (text);
}

//==============================================================================
void AiChatView::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));
}

void AiChatView::resized()
{
    auto bounds = getLocalBounds().reduced (padding);

    if (signInCard.isVisible())
    {
        signInCard.setBounds (bounds);

        auto card = signInCard.getLocalBounds().reduced (padding);
        const auto contentHeight = rowHeight * 6;
        card = card.withSizeKeepingCentre (juce::jmin (420, card.getWidth()),
                                           juce::jmin (contentHeight, card.getHeight()));

        signInTitle.setBounds (card.removeFromTop (rowHeight));
        signInCode.setBounds (card.removeFromTop (rowHeight + 10));
        signInInstructions.setBounds (card.removeFromTop (rowHeight * 2));
        signInButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 4));
        openBrowserButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 4));
        cancelSignInButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 4));
        return;
    }

    // 入力欄は下端固定。キーボードが出るとコンテンツ全体が縮むので、
    // これだけでキーボード直上に来る。
    {
        auto inputArea = bounds.removeFromBottom (inputHeight);
        auto buttonArea = inputArea.removeFromRight (90);

        sendButton.setBounds (buttonArea.removeFromTop (rowHeight).reduced (2));
        stopButton.setBounds (sendButton.getBounds());

        inputArea.removeFromRight (padding / 2);
        input.setBounds (inputArea);
    }

    bounds.removeFromBottom (padding / 2);

    if (approvalCard.isVisible())
    {
        // 承認カードは画面の半分までに抑え、履歴も見えるようにする。
        auto cardArea = bounds.removeFromBottom (juce::jmin (bounds.getHeight() / 2, 260));
        approvalCard.setBounds (cardArea);

        auto card = approvalCard.getLocalBounds();

        approvalTitle.setBounds (card.removeFromTop (rowHeight));

        auto buttons = card.removeFromBottom (rowHeight);
        approveButton.setBounds (buttons.removeFromLeft (100).reduced (2));
        rejectButton.setBounds (buttons.removeFromLeft (100).reduced (2));
        autoApproveToggle.setBounds (buttons.reduced (4, 2));

        approvalDiff.setBounds (card.reduced (0, 4));

        bounds.removeFromBottom (padding / 2);
    }

    history.setBounds (bounds);
}
```

- [ ] **Step 3: コミットのコマンドを提示する（実行しない）**

```bash
git add Projucer/Source/AI/jucer_AiChatView.h Projucer/Source/AI/jucer_AiChatView.cpp
git commit -m "feat(ai): チャット UI を追加"
```

---

## Task 9: 既存 UI への組み込み

**Files:**
- Modify: `Projucer/Source/Project/UI/jucer_HeaderComponent.h`
- Modify: `Projucer/Source/Project/UI/jucer_HeaderComponent.cpp`
- Modify: `Projucer/Source/Project/UI/jucer_ProjectContentComponent.h`
- Modify: `Projucer/Source/Project/UI/jucer_ProjectContentComponent.cpp`
- Modify: `Projucer/Source/Application/jucer_Application.cpp`（Task 4/5 の一時ブロック削除）

**Interfaces:**
- Consumes: Task 4 `CodexAuth`、Task 7 `AiSession`、Task 8 `AiChatView`
- Produces: `ProjectContentComponent::toggleAiView()` と `bool isAiViewShowing() const`

- [ ] **Step 1: ヘッダに AI ボタンを足す**

`jucer_HeaderComponent.h` の `IconButton` 宣言（102-103 行目付近）へ追加する:

```cpp
    IconButton projectSettingsButton { "Project Settings", getIcons().settings },
               saveAndOpenInIDEButton { "Save and Open in IDE", Image() },
               aiButton { "AI アシスタント", getIcons().settings };
```

アイコンは暫定で `settings` を流用する。専用アイコンは後で差し替える。

`jucer_HeaderComponent.cpp` の `initialiseButtons()` の末尾へ追加する:

```cpp
    aiButton.setTooltip ("AI アシスタントを開く");
    aiButton.onClick = [this]
    {
        if (projectContentComponent != nullptr)
            projectContentComponent->toggleAiView();
    };
    addAndMakeVisible (aiButton);
```

`resized()` の 2 つ目のブロックの直後（`configLabel.setBounds` の後）へ追加する:

```cpp
    // ヘッダ右端に置く。exporter の並びとは独立させる。
    aiButton.setBounds (getLocalBounds().removeFromRight (35)
                                        .withSizeKeepingCentre (25, 25));
```

- [ ] **Step 2: `ProjectContentComponent` に `AiSession` を持たせる**

`jucer_ProjectContentComponent.h` の前方宣言へ追加する:

```cpp
class AiSession;
class CodexAuth;
```

public メソッドへ追加する:

```cpp
    /** AI ビューとドキュメントビューを切り替える。 */
    void toggleAiView();

    bool isAiViewShowing() const noexcept    { return aiViewShowing; }
```

private メンバへ追加する（`terminalPanel` の近く）:

```cpp
    std::unique_ptr<CodexAuth> codexAuth;
    std::unique_ptr<AiSession> aiSession;
    bool aiViewShowing = false;
```

- [ ] **Step 3: 切り替えを実装する**

`jucer_ProjectContentComponent.cpp` の include へ追加する:

```cpp
#include "../../AI/jucer_AiChatView.h"
#include "../../AI/jucer_AiSession.h"
#include "../../AI/jucer_CodexAuth.h"
```

実装を追加する（`setScrollableEditorComponent` の近く）:

```cpp
void ProjectContentComponent::toggleAiView()
{
    if (project == nullptr)
        return;

    if (aiViewShowing)
    {
        // 直前に開いていたドキュメントへ戻す。開いていなければ空にする。
        aiViewShowing = false;

        if (auto* document = ProjucerApplication::getApp().openDocumentManager.getOpenDocument (0))
            showDocument (document, true);
        else
            hideEditor();

        return;
    }

    // AiSession はここが所有する。AiChatView は setContent() で破棄されるが、
    // 会話はこちらに残るので AI タブを離れて戻っても続く。
    if (codexAuth == nullptr)
        codexAuth = std::make_unique<CodexAuth>();

    if (aiSession == nullptr)
        aiSession = std::make_unique<AiSession> (*codexAuth, project->getFile().getParentDirectory());

    aiViewShowing = true;
    contentViewComponent.setContent (std::make_unique<AiChatView> (*aiSession, *codexAuth),
                                     "AI アシスタント");
}
```

`hideEditor()` と `showDocument()` の先頭で `aiViewShowing = false;` を立てる。
ファイルツリーからドキュメントを開いたとき、AI ビューの状態が残らないようにする。

- [ ] **Step 4: 確認用の一時ブロックを消す**

Task 4 と Task 5 で `jucer_Application.cpp` の `initialise()` へ入れた `#if 0`
ブロックを両方削除する。UI から同じ経路を通せるようになったので不要。

Run: `grep -n "確認用" Projucer/Source/Application/jucer_Application.cpp`
Expected: 何も出ない

- [ ] **Step 5: コミットのコマンドを提示する（実行しない）**

```bash
git add Projucer/Source/Project/UI/jucer_HeaderComponent.h \
        Projucer/Source/Project/UI/jucer_HeaderComponent.cpp \
        Projucer/Source/Project/UI/jucer_ProjectContentComponent.h \
        Projucer/Source/Project/UI/jucer_ProjectContentComponent.cpp \
        Projucer/Source/Application/jucer_Application.cpp
git commit -m "feat(ai): ヘッダから AI ビューを開けるようにする"
```

---

## Task 10: ビルド統合と全構成確認

**Files:**
- Modify: `Projucer/Projucer.jucer`
- Modify: 各 `Projucer/Builds/*`（Projucer の resave が生成する）

- [ ] **Step 1: `.jucer` に AI グループを追加する**

`Projucer/Projucer.jucer` の `<GROUP id="{A1B2C3D4-0000-4000-8000-000000000001}" name="Terminal">`
の**直前**へ以下を挿入する。既存の書式（`compile` は `.cpp`/`.mm` が `1`、
ヘッダは `0`）に合わせる。

```xml
    <GROUP id="{A1B2C3D4-0000-4000-8000-000000000002}" name="AI">
      <FILE id="aiSse1" name="jucer_SseParser.h" compile="0" resource="0"
            file="Source/AI/jucer_SseParser.h"/>
      <FILE id="aiSse2" name="jucer_SseParser.cpp" compile="1" resource="0"
            file="Source/AI/jucer_SseParser.cpp"/>
      <FILE id="aiPth1" name="jucer_AiPaths.h" compile="0" resource="0"
            file="Source/AI/jucer_AiPaths.h"/>
      <FILE id="aiPth2" name="jucer_AiPaths.cpp" compile="1" resource="0"
            file="Source/AI/jucer_AiPaths.cpp"/>
      <FILE id="aiKey1" name="jucer_Keychain.h" compile="0" resource="0"
            file="Source/AI/jucer_Keychain.h"/>
      <FILE id="aiKey2" name="jucer_Keychain.mm" compile="1" resource="0"
            file="Source/AI/jucer_Keychain.mm"/>
      <FILE id="aiPkc1" name="jucer_Pkce.h" compile="0" resource="0"
            file="Source/AI/jucer_Pkce.h"/>
      <FILE id="aiPkc2" name="jucer_Pkce.cpp" compile="1" resource="0"
            file="Source/AI/jucer_Pkce.cpp"/>
      <FILE id="aiCbs1" name="jucer_OAuthCallbackServer.h" compile="0" resource="0"
            file="Source/AI/jucer_OAuthCallbackServer.h"/>
      <FILE id="aiCbs2" name="jucer_OAuthCallbackServer.cpp" compile="1" resource="0"
            file="Source/AI/jucer_OAuthCallbackServer.cpp"/>
      <FILE id="aiBrw1" name="jucer_AuthBrowser.h" compile="0" resource="0"
            file="Source/AI/jucer_AuthBrowser.h"/>
      <FILE id="aiBrw2" name="jucer_AuthBrowser.mm" compile="1" resource="0"
            file="Source/AI/jucer_AuthBrowser.mm"/>
      <FILE id="aiAut1" name="jucer_CodexAuth.h" compile="0" resource="0"
            file="Source/AI/jucer_CodexAuth.h"/>
      <FILE id="aiAut2" name="jucer_CodexAuth.cpp" compile="1" resource="0"
            file="Source/AI/jucer_CodexAuth.cpp"/>
      <FILE id="aiCli1" name="jucer_CodexClient.h" compile="0" resource="0"
            file="Source/AI/jucer_CodexClient.h"/>
      <FILE id="aiCli2" name="jucer_CodexClient.cpp" compile="1" resource="0"
            file="Source/AI/jucer_CodexClient.cpp"/>
      <FILE id="aiTol1" name="jucer_AiTools.h" compile="0" resource="0"
            file="Source/AI/jucer_AiTools.h"/>
      <FILE id="aiTol2" name="jucer_AiTools.cpp" compile="1" resource="0"
            file="Source/AI/jucer_AiTools.cpp"/>
      <FILE id="aiSes1" name="jucer_AiSession.h" compile="0" resource="0"
            file="Source/AI/jucer_AiSession.h"/>
      <FILE id="aiSes2" name="jucer_AiSession.cpp" compile="1" resource="0"
            file="Source/AI/jucer_AiSession.cpp"/>
      <FILE id="aiLop1" name="jucer_AgentLoop.h" compile="0" resource="0"
            file="Source/AI/jucer_AgentLoop.h"/>
      <FILE id="aiLop2" name="jucer_AgentLoop.cpp" compile="1" resource="0"
            file="Source/AI/jucer_AgentLoop.cpp"/>
      <FILE id="aiVew1" name="jucer_AiChatView.h" compile="0" resource="0"
            file="Source/AI/jucer_AiChatView.h"/>
      <FILE id="aiVew2" name="jucer_AiChatView.cpp" compile="1" resource="0"
            file="Source/AI/jucer_AiChatView.cpp"/>
    </GROUP>
```

`Tests/ai_selfcheck.cpp` は**含めない**。あれはアプリの一部ではなく、
`scripts/run_ai_selfcheck.sh` が単体でビルドする。

**改行コードに注意。** `Projucer.jucer` は CRLF である。編集で LF に
変換してしまうと全行が差分になる。編集後に必ず確認する:

Run: `file Projucer/Projucer.jucer`
Expected: `CRLF line terminators` が含まれること

- [ ] **Step 2: フレームワークをリンクする**

`XCODE_IPHONE`（`Projucer.jucer:140` 付近）には既に
`extraFrameworks="Network,Security"` がある。**`AuthenticationServices` を追記する**
（Task 4b の `ASWebAuthenticationSession` が要る）→
`extraFrameworks="Network,Security,AuthenticationServices"`。

`XCODE_MAC`（`Projucer.jucer:8` 付近）には `extraFrameworks` 属性が無いので
追加する。`targetFolder="Builds/MacOSX"` と同じ要素の属性として足す:

```xml
    <XCODE_MAC targetFolder="Builds/MacOSX" documentExtensions=".jucer" bigIcon="Zrx1Gl"
               extraFrameworks="Security"
```

（既存の属性は消さないこと。`extraFrameworks` の 1 行を差し込むだけ。）

- [ ] **Step 3: Projucer を resave する**

現在の Projucer（macOS Debug ビルド）で `Projucer.jucer` を開き、
Save Project を実行する。あるいは CLI で:

Run: `Projucer/Builds/MacOSX/build/Debug/Projucer.app/Contents/MacOS/Projucer --resave Projucer/Projucer.jucer`

Expected: エラーなく終了し、`Projucer/Builds/` 配下の各プロジェクトファイルが更新される

- [ ] **Step 4: macOS をビルドする**

Run:
```bash
xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
           -configuration Debug -quiet build
```
Expected: `BUILD SUCCEEDED`

Release も同様に確認する。

- [ ] **Step 5: iOS シミュレータをビルドする**

Run:
```bash
xcodebuild -project Projucer/Builds/iOS/Projucer.xcodeproj \
           -configuration Debug -sdk iphonesimulator -quiet build
```
Expected: `BUILD SUCCEEDED`

Release も同様に確認する。

- [ ] **Step 6: iOS デバイスをビルドする**

Run:
```bash
xcodebuild -project Projucer/Builds/iOS/Projucer.xcodeproj \
           -configuration Debug -sdk iphoneos -quiet build
```
Expected: `BUILD SUCCEEDED`

Release も同様に確認する。

**6 構成すべてが通るまで次へ進まない。** 過去に SDK 条件付きリンカフラグ
(`OTHER_LDFLAGS[sdk=iphoneos*]`) で device と simulator の片方だけが壊れた
事例がある。片方だけ確認して済ませない。

- [ ] **Step 7: 自己チェックを最後にもう一度回す**

Run: `./scripts/run_ai_selfcheck.sh`
Expected: `すべて成功`

- [ ] **Step 8: コミットのコマンドを提示する（実行しない）**

```bash
git add Projucer/Projucer.jucer Projucer/Builds
git commit -m "build(ai): AI ハーネスのソースをビルドへ追加"
```

---

## Task 11: iPad 実機での受け入れ確認

コードは書かない。M1 の完了条件を実機で確かめる。

- [ ] **Step 1: iPad へインストールして起動する**

既存の iOS インストール手順（`docs/` の iPad セットアップ手順）に従う。

- [ ] **Step 2: サインインを確認する**

1. プロジェクトを開く
2. ヘッダの AI ボタンを押す
3. サインインカードが出る
4. 「ChatGPT でサインイン」を押すと認可ページがアプリ内に出る
5. ChatGPT アカウントで承認する
6. 「サインインが完了しました」が表示され、Projucer がチャット画面へ切り替わる

続けて代替経路も確認する（ChatGPT のセキュリティ設定でデバイスコード認証を
有効にしてから）:

7. サインアウトし、「うまくいかない場合: デバイスコードでサインイン」を押す
8. コードが表示され、Safari で入力して承認するとサインインできる
9. 設定を無効に戻した場合、その旨のエラーが出てブラウザ経路を案内される

- [ ] **Step 3: サインインが保持されることを確認する**

アプリを終了して再起動し、AI ボタンを押す。サインインカードではなく
チャット画面が出ること（Keychain が効いている）。

- [ ] **Step 4: キーボード表示時のレイアウトを確認する**

入力欄をタップする。キーボードが出た状態で:

- 入力欄がキーボードに隠れないこと
- 履歴が読めること
- 送信ボタンが押せること

- [ ] **Step 5: 読み取りを確認する**

「このプロジェクトのファイル構成を教えて」と送る。

- 応答がストリーミング表示されること
- `list_files` / `read_file` の実行ログが出ること
- 承認を求められないこと（読み取りは承認不要）

- [ ] **Step 6: 書き込みと承認を確認する**

「このクラスにローパスフィルタ用のパラメータを足して」と送る。

- 差分カードが出ること
- 「却下」を押すと変更されず、AI がその旨を認識して応答すること
- もう一度頼んで「適用」を押すとファイルが実際に変わること
- 変更後のファイルを Projucer のエディタで開いて内容を確認すること

- [ ] **Step 7: 開いているエディタが更新されることを確認する**

変更対象のファイルを Projucer のエディタで**開いたまま**、AI に同じファイルの
変更を依頼して適用する。

- エディタの表示が AI の変更後の内容に切り替わること
- そのままエディタで保存しても AI の変更が消えないこと

これが効いていないと、古い内容を持ったエディタの保存で AI の変更が失われる。

なお、閉じたファイルへの変更は Undo では戻せない（spec 8.4）。だからこそ
適用前の差分確認を必須にしている。

- [ ] **Step 8: 停止を確認する**

長めの依頼を投げ、実行中に「停止」を押す。

- UI が固まらないこと
- ループが止まること
- 再度送信できること

- [ ] **Step 9: パス脱出が防げていることを確認する**

「/etc/passwd の中身を見せて」と送る。

- AI がプロジェクト外である旨のエラーを受け取り、その旨を答えること
- ファイルの内容が表示されないこと

- [ ] **Step 10: ユーザーへ報告し、コミットの可否を確認する**

`AGENTS.md` により、ここまでの手動テストが通ってはじめてコミットできる。
確認結果を報告し、コミットしてよいか尋ねる。

---

## 完了条件

iPad 実機で、Projucer から ChatGPT アカウントにサインインし、開いている
プロジェクトについて「このクラスにローパスフィルタ用のパラメータを足して」と
指示すると、AI がソースを読んで差分を提示し、承認すると実際にファイルへ
反映される。

## 次の段階（この計画の範囲外）

| 段階 | 内容 |
|---|---|
| M2 | ビルド実行ツール、GUI Editor 操作ツール |
| M3 | Grok 対応。この時点で `CodexClient` の上にプロバイダ境界を切る |
| M4 | Claude 対応 |
