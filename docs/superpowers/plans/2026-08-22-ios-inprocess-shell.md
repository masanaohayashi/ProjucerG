# iOS プロセス内シェル Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** iOS 上の AI `exec_command` を、子プロセス無しの POSIX サブセットシェルで動かす。

**Architecture:** `ProjucerShell::run` がコマンド文字列を解釈し、アプレット関数を同じプロセスで呼ぶ。パイプは pthread 相当の `std::thread` とメモリバッファ。`git` は既存 `ProjucerGit` へ委譲する。macOS の `exec_command` は触らない。`JUCE_IOS` のときだけこのエンジンに切り替える。

**Tech Stack:** C++17 / juce_core / 既存 libgit2（git アプレット） / Mac 自己チェック（`git_selfcheck` と同型）

**Spec:** `docs/superpowers/specs/2026-08-22-ios-inprocess-shell-design.md`

## Global Constraints

- 回答・コメント・ドキュメントは日本語（`AGENTS.md`）。
- **コミットは禁止。** `AGENTS.md` により、ユーザーが明示的に依頼し、かつ手動テストで動作確認するまでコミット・プッシュしない。各タスク末尾の「コミット」ステップはコマンドを提示するだけで **実行しない**。
- 新規ソースは `Projucer/Source/Shell/`。ファイル名は `jucer_` 接頭辞。
- 新しいコンパイル対象は **`Projucer/Projucer.jucer` と `Projucer/CMakeLists.txt` の両方**に登録する（アプリへ繋ぐタスクで一度にやる。それまでは自己チェックだけがコンパイルする）。
- 既存ファイルの改行コードを変換しない。
- `ios_system` / toybox / a-Shell / iSH を取り込まない。
- LLVM / OnDeviceBuild をシェルから呼ばない。`clang++` 等はメッセージを返して 127。
- `./a.out` は実装しない。
- 下部ターミナル・PTY には繋がない。
- 仮想インターフェースやファクトリを作らない。
- エンジンは Mac でもリンクでき、自己チェックは Mac で回す。
- 対応構成の切替は `JUCE_IOS` のみ。シミュレータでもプロセス内シェルを使う。

---

## File Structure

| ファイル | 責務 |
|---|---|
| `Projucer/Source/Shell/jucer_InProcessShell.h` | `Request` / `Result` / `run` / `availableCommands` |
| `Projucer/Source/Shell/jucer_InProcessShell.cpp` | 字句解析、構文、環境、パイプ、リダイレクト、`sh -c`、サンドボックス、タイムアウト |
| `Projucer/Source/Shell/jucer_ShellApplets.h` | `registerBuiltinApplets` |
| `Projucer/Source/Shell/jucer_ShellApplets.cpp` | アプレット本体 |
| `Projucer/Source/Shell/Tests/shell_selfcheck.cpp` | 自己チェック |
| `scripts/run_shell_selfcheck.sh` | ビルドして実行 |

変更する既存ファイル（Task 8）:

| ファイル | 変更内容 |
|---|---|
| `Projucer/Source/AI/jucer_AiTools.cpp` | iOS の `doExecCommand` を `ProjucerShell::run` へ |
| `Projucer/Source/AI/jucer_AgentLoop.cpp` | system 指示の iOS シェル説明 |
| `Projucer/Projucer.jucer` | Shell グループ |
| `Projucer/CMakeLists.txt` | `target_sources` |

内部型（ヘッダには出さない。cpp 内）:

```cpp
struct ShellIo
{
    juce::InputStream* in = nullptr;
    juce::OutputStream* out = nullptr;
    juce::OutputStream* err = nullptr;
};

struct ShellState
{
    juce::File cwd;
    juce::File sandboxRoot;
    std::map<juce::String, juce::String> env;
    std::atomic<bool>* cancelFlag = nullptr;
    juce::int64 deadlineMs = 0;
    int maxOutputChars = 64 * 1024;
};

using AppletFn = int (*) (const juce::StringArray& argv, ShellIo& io, ShellState& state);

void registerApplet (const juce::String& name, AppletFn fn);
```

パス解決ヘルパ（`jucer_InProcessShell.cpp` の無名名前空間）:

```cpp
/* 読み取り用。絶対パスも相対パスも可。失敗したら {} */
juce::File resolvePath (const ShellState&, const juce::String& operand);

/* 書き込み / cd 用。sandboxRoot の外なら {} を返し、err に理由を書く */
juce::File resolveWritablePath (const ShellState&, const juce::String& operand, juce::OutputStream& err);
```

`availableCommands()` は登録済みアプレット名をソートして返す。コンパイラ名は表に載せない。

---

### Task 1: 自己チェック基盤と echo / true / false

**Files:**
- Create: `Projucer/Source/Shell/jucer_InProcessShell.h`
- Create: `Projucer/Source/Shell/jucer_InProcessShell.cpp`
- Create: `Projucer/Source/Shell/jucer_ShellApplets.h`
- Create: `Projucer/Source/Shell/jucer_ShellApplets.cpp`
- Create: `Projucer/Source/Shell/Tests/shell_selfcheck.cpp`
- Create: `scripts/run_shell_selfcheck.sh`

**Interfaces:**
- Consumes: なし
- Produces: `ProjucerShell::run` と `availableCommands`。この時点で動くコマンドは `echo`（`-n` あり）、`true`、`false` のみ。未登録コマンドは exit 127。

- [ ] **Step 1: 自己チェックのビルドスクリプトを作る**

`scripts/run_shell_selfcheck.sh`:

```bash
#!/bin/bash
# プロセス内シェルの純粋ロジックを juce_core だけでビルドして実行する。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JUCE="$ROOT/OnDeviceBuild/dependencies/JUCE"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

if [ ! -d "$JUCE/modules/juce_core" ]; then
    echo "JUCE is missing at $JUCE. Run scripts/prepare_dependencies.sh first." >&2
    exit 1
fi

clang++ -std=c++17 -g -Wall -Wextra -Wno-missing-field-initializers -DDEBUG=1 \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 \
    -I "$JUCE/modules" \
    -I "$ROOT/Projucer/Source" \
    -x objective-c++ "$JUCE/modules/juce_core/juce_core.mm" \
    -x c++ \
    "$ROOT/Projucer/Source/Shell/jucer_InProcessShell.cpp" \
    "$ROOT/Projucer/Source/Shell/jucer_ShellApplets.cpp" \
    "$ROOT/Projucer/Source/Shell/Tests/shell_selfcheck.cpp" \
    -framework Foundation -framework Security -framework CoreFoundation \
    -framework CoreServices -framework IOKit -framework Cocoa \
    -o "$OUT/shell_selfcheck"

"$OUT/shell_selfcheck"
```

実行権限を付ける: `chmod +x scripts/run_shell_selfcheck.sh`

- [ ] **Step 2: 公開ヘッダを書く**

`Projucer/Source/Shell/jucer_InProcessShell.h` は spec 4.2 の API をそのまま置く。
`#include <juce_core/juce_core.h>` と `<atomic>`。

- [ ] **Step 3: 失敗する自己チェックを書く**

`shell_selfcheck.cpp` は `git_selfcheck.cpp` と同じ形。`juce_compilationDate` /
`juce_compilationTime` を定義する。一時ディレクトリを `sandboxRoot` 兼 `cwd` にする。

```cpp
#include "../jucer_InProcessShell.h"
#include <cassert>
#include <iostream>

namespace juce
{
    extern const char* const juce_compilationDate = __DATE__;
    extern const char* const juce_compilationTime = __TIME__;
}

namespace
{
    juce::File root;

    ProjucerShell::Result sh (const juce::String& commandLine)
    {
        ProjucerShell::Request request;
        request.commandLine = commandLine;
        request.workingDirectory = root;
        request.sandboxRoot = root;
        const auto result = ProjucerShell::run (request);
        std::cout << "$ " << commandLine << "\n" << result.output << "\n\n";
        return result;
    }

    void expectExit (const juce::String& commandLine, int code)
    {
        assert (sh (commandLine).exitCode == code);
    }

    void expectOutput (const juce::String& commandLine, const juce::String& text)
    {
        const auto result = sh (commandLine);
        assert (result.exitCode == 0);
        assert (result.output == text);
    }

    void expectContains (const juce::String& commandLine, const juce::String& text)
    {
        const auto result = sh (commandLine);
        assert (result.output.contains (text));
        juce::ignoreUnused (result);
    }
}

int main()
{
    root = juce::File::getSpecialLocation (juce::File::tempDirectory)
             .getChildFile ("projucer-shell-selfcheck-" + juce::String (juce::Time::currentTimeMillis()));
    root.createDirectory();

    expectOutput ("echo hello", "hello\n");
    expectOutput ("echo -n hello", "hello");
    expectExit ("true", 0);
    expectExit ("false", 1);
    expectExit ("not_a_command", 127);
    expectContains ("not_a_command", "command not found");
    expectContains ("not_a_command", "echo");

    std::cout << "all in-process shell checks passed\n";
    return 0;
}
```

このタスクでは上記だけ。後のタスクで `main` にケースを足す。

- [ ] **Step 4: スクリプトを回し、リンクまたは assert で失敗することを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: コンパイル失敗、または実行時 assert。

- [ ] **Step 5: 最小実装**

`jucer_InProcessShell.cpp`:
- アプレット表 `std::map<juce::String, AppletFn>`
- `registerApplet` をこの TU に置き、applets 側から呼ぶ
- `run` はコマンドラインを空白で split しただけ（まだクォート無し）
- argv[0] の basename を表で探す。無ければ spec 6.1 の未知コマンド文を err 相当の output に書いて 127
- stdout は `juce::MemoryOutputStream`

`jucer_ShellApplets.cpp`:
- `echo`: `-n` が先頭なら改行なし。残りを空白連結
- `true` → 0、`false` → 1
- `registerBuiltinApplets()` を `run` の初回で呼ぶ（`std::call_once`）

`jucer_ShellApplets.h`:

```cpp
#pragma once
void registerBuiltinApplets();
```

- [ ] **Step 6: 自己チェックが通ることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: `all in-process shell checks passed`

- [ ] **Step 7: コミット（実行しない）**

```bash
git add scripts/run_shell_selfcheck.sh \
  Projucer/Source/Shell/jucer_InProcessShell.h \
  Projucer/Source/Shell/jucer_InProcessShell.cpp \
  Projucer/Source/Shell/jucer_ShellApplets.h \
  Projucer/Source/Shell/jucer_ShellApplets.cpp \
  Projucer/Source/Shell/Tests/shell_selfcheck.cpp
# git commit -m "Add in-process shell skeleton with echo true false"
```

---

### Task 2: 引用符・コメント・リスト（`;` `&&` `||`）

**Files:**
- Modify: `Projucer/Source/Shell/jucer_InProcessShell.cpp`
- Modify: `Projucer/Source/Shell/Tests/shell_selfcheck.cpp`

**Interfaces:**
- Consumes: Task 1 の `run`
- Produces: 字句解析。`echo 'a b'` は引数 1 個。`#` から行末までコメント。`&&` は左が 0 のときだけ右を実行。`||` はその逆。`;` は左の終了コードを捨てて右を実行し、リスト全体の終了コードは最後。

- [ ] **Step 1: 失敗するテストを足す**

`main()` の echo テストの後:

```cpp
    expectOutput ("echo 'a b'", "a b\n");
    expectOutput ("echo \"a b\"", "a b\n");
    expectOutput ("echo a # comment\n", "a\n");
    expectOutput ("true && echo ok", "ok\n");
    expectOutput ("false && echo no", "");
    expectOutput ("false || echo yes", "yes\n");
    expectOutput ("true || echo no", "");
    expectOutput ("false; echo after", "after\n");
    expectExit ("true && false", 1);
    expectExit ("false || true", 0);
```

- [ ] **Step 2: 自己チェックが落ちることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: assert 失敗。

- [ ] **Step 3: トークナイザとリスト実行を実装する**

トークン種別: `Word`, `Pipe`, `AndIf`, `OrIf`, `Semi`, `Redirect`（このタスクでは Redirect/Pipe は構文エラーで 2 を返してよい。次タスクで実装する）。

ルール:
- 空白で区切る
- `'` の中は次の `'` まで生文字
- `"` の中は `\` と（次タスクの `$`）。今は `$` をまだ展開しなくてよい
- クォート外の `\` は次の 1 文字を生にする
- クォート外の `#` は行末まで捨てる
- クォート外の `&&` `||` `;` `|` は演算子

実行は左から。`&&` / `||` でスキップする側は実行しない。

まだパイプが無いので、1 つのパイプラインはコマンド 1 個。

- [ ] **Step 4: 自己チェックが通ることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: `all in-process shell checks passed`

- [ ] **Step 5: コミット（実行しない）**

```bash
git add Projucer/Source/Shell/jucer_InProcessShell.cpp \
  Projucer/Source/Shell/Tests/shell_selfcheck.cpp
# git commit -m "Parse quotes and && || ; in the in-process shell"
```

---

### Task 3: リダイレクトとパイプ

**Files:**
- Modify: `Projucer/Source/Shell/jucer_InProcessShell.cpp`
- Modify: `Projucer/Source/Shell/jucer_ShellApplets.cpp`
- Modify: `Projucer/Source/Shell/Tests/shell_selfcheck.cpp`

**Interfaces:**
- Consumes: Task 2 のトークン列
- Produces: `>` `>>` `<` `2>` `2>>` `2>&1` と `|`。パイプの終了コードは最後のコマンド。このタスクで `cat` と `tr` の最小実装が要る（パイプの検証用）。`tr` は `tr from to` の 1 対 1 置換だけでよい。完全な POSIX `tr` は Task 6。

- [ ] **Step 1: 失敗するテストを足す**

```cpp
    expectOutput ("echo hi > f.txt && cat f.txt", "hi\n");
    expectOutput ("echo a >> f.txt && echo b >> f.txt && cat f.txt", "hi\na\nb\n");
    expectOutput ("echo hello | tr e a", "hallo\n");
    expectOutput ("echo x 2>&1", "x\n");
```

`echo hi > f.txt && cat f.txt` の出力は `cat` の stdout だけ。`echo` はファイルへ書いたので結合結果は `hi\n`。

- [ ] **Step 2: 自己チェックが落ちることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: assert 失敗。

- [ ] **Step 3: `cat` と最小 `tr` を足す**

`cat`: 引数が無ければ `io.in` を最後まで `io.out` へ。引数があれば各ファイルを読む。無いファイルは err に書いて 1。

`tr`: argv が 3 要素（名前含む）のとき、stdin の各文字を from[i]→to[i] で置換。長さが違うなら短い方に合わせ、余った from は削除しない（v1 は同じ長さだけサポートし、違えば 2）。

- [ ] **Step 4: リダイレクトとパイプを実装する**

- `>` / `>>` / `2>` / `2>>`: `resolveWritablePath` で開く。まだ sandbox 関数が粗くてよいが、`sandboxRoot` 配下の相対パスは普通に作れること。親ディレクトリが無い `>` は失敗（`mkdir -p` はまだ無い）。
- `<`: 読み取りオープン
- `2>&1`: err を out と同じ `OutputStream*` にする
- `|`: 左の stdout を `MemoryOutputStream` に溜め、完了後に `MemoryInputStream` として右の stdin へ渡す。**スレッドはまだ使わなくてよい。** 左を同期実行してから右を実行する。無限生成パイプは v1 対象外。cancel は左の後にも見る。

リダイレクトの対象ワードはコマンド argv から外す。

- [ ] **Step 5: 自己チェックが通ることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: `all in-process shell checks passed`

- [ ] **Step 6: コミット（実行しない）**

```bash
git add Projucer/Source/Shell/jucer_InProcessShell.cpp \
  Projucer/Source/Shell/jucer_ShellApplets.cpp \
  Projucer/Source/Shell/Tests/shell_selfcheck.cpp
# git commit -m "Add redirects and sequential pipes to the in-process shell"
```

---

### Task 4: 変数、グロブ、コマンド置換、`sh -c`

**Files:**
- Modify: `Projucer/Source/Shell/jucer_InProcessShell.cpp`
- Modify: `Projucer/Source/Shell/jucer_ShellApplets.cpp`
- Modify: `Projucer/Source/Shell/Tests/shell_selfcheck.cpp`

**Interfaces:**
- Consumes: Task 3 の実行器
- Produces: `$VAR` `${VAR}`、`FOO=bar cmd`、`export`/`unset`/`env`/`pwd`/`cd`、クォート無し `*` `?` グロブ、`$(...)` / `` `...` ``、`sh -c` / `bash -c`。`cd` は **この `run` の `ShellState::cwd` だけ** 変える。

- [ ] **Step 1: 失敗するテストを足す**

```cpp
    expectOutput ("FOO=bar echo $FOO", "bar\n");
    expectOutput ("export FOO=baz && echo $FOO", "baz\n");
    expectOutput ("echo $(echo nested)", "nested\n");
    expectOutput ("sh -c 'echo nested2'", "nested2\n");
    expectOutput ("bash -c \"echo nested3\"", "nested3\n");
    expectOutput ("pwd", root.getFullPathName() + "\n");
    expectOutput ("mkdir inner_for_later 2>/dev/null; true", ""); // mkdir は次タスク。ここはまだ使わない

    root.getChildFile ("glob-a.txt").replaceWithText ("a\n");
    root.getChildFile ("glob-b.txt").replaceWithText ("b\n");
    expectContains ("echo glob-*.txt", "glob-a.txt");
    expectContains ("echo glob-*.txt", "glob-b.txt");

    expectOutput ("cd subdir_missing || echo fail", "fail\n");
```

`mkdir` がまだ無いのでグロブ用ファイルはテスト側で作る。`cd` 失敗はディレクトリが無いとき 1。

- [ ] **Step 2: 自己チェックが落ちることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: assert 失敗。

- [ ] **Step 3: 展開と組み込みを実装する**

展開順: クォート解釈 → `$` 展開（ダブルクォート内と非クォート）→ グロブ（非クォートだけ）。
シングルクォート内は展開しない。
未定義変数は空文字。
コマンド置換はネストした `ProjucerShell::run` 相当の内部関数に部分文字列を渡し、出力末尾の改行を 1 つ削って単語分割する（ダブルクォート内なら分割しない）。

`cd`:
- 引数なし → `HOME`（初期値は sandboxRoot）
- 解決後、ディレクトリでかつ writable 判定（sandbox 内）なら `state.cwd` と `env["PWD"]` を更新
- 失敗は err に理由、exit 1

`export NAME=value` / `export NAME` / `unset NAME` / `env`（`NAME=value` をソートして出す）

`sh` / `bash`: `-c` の次の引数を内部 `run`。argv[0] がパス付きなら basename が `sh` または `bash` なら同じ。`-c` 以外は usage を書いて 2。

グロブ: `juce::File::findChildFiles` または手動。マッチゼロなら POSIX どおりパターンをそのまま残す。

- [ ] **Step 4: 自己チェックが通ることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: `all in-process shell checks passed`

- [ ] **Step 5: コミット（実行しない）**

```bash
git add Projucer/Source/Shell/jucer_InProcessShell.cpp \
  Projucer/Source/Shell/jucer_ShellApplets.cpp \
  Projucer/Source/Shell/Tests/shell_selfcheck.cpp
# git commit -m "Add variables, glob, command substitution and sh -c"
```

---

### Task 5: ファイルアプレット

**Files:**
- Modify: `Projucer/Source/Shell/jucer_ShellApplets.cpp`
- Modify: `Projucer/Source/Shell/Tests/shell_selfcheck.cpp`
- Modify: `Projucer/Source/Shell/jucer_InProcessShell.cpp`（`resolveWritablePath` を確実に sandbox する）

**Interfaces:**
- Consumes: `resolvePath` / `resolveWritablePath`
- Produces: `ls` `mkdir` `rm` `rmdir` `cp` `mv` `touch` `head` `tail` `pwd`（pwd は Task 4 で済みなら触らない）

フラグは spec の表どおり。それ以外のフラグは無視せず、未知フラグなら err に書いて 1。ただし GNU がよく付ける `--` は「以降はオペランド」として扱ってよい。

- [ ] **Step 1: 失敗するテストを足す**

```cpp
    expectExit ("mkdir -p a/b/c", 0);
    assert (root.getChildFile ("a/b/c").isDirectory());
    expectExit ("touch a/b/c/f.txt", 0);
    root.getChildFile ("a/b/c/f.txt").replaceWithText ("one\ntwo\nthree\n");
    expectOutput ("cat a/b/c/f.txt", "one\ntwo\nthree\n");
    expectOutput ("head -n 1 a/b/c/f.txt", "one\n");
    expectOutput ("tail -n 1 a/b/c/f.txt", "three\n");
    expectContains ("ls a/b/c", "f.txt");
    expectExit ("cp a/b/c/f.txt a/copied.txt", 0);
    expectExit ("mv a/copied.txt a/moved.txt", 0);
    assert (root.getChildFile ("a/moved.txt").existsAsFile());
    expectExit ("rm -rf a", 0);
    assert (! root.getChildFile ("a").exists());
```

- [ ] **Step 2: 自己チェックが落ちることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: assert 失敗。

- [ ] **Step 3: アプレットを実装する**

共通:
- ディレクトリを再帰するときは毎回 `cancelFlag` と `Time::getMillisecondCounter() > deadlineMs` を見る。期限切れは err に `timed out`、exit 124（timeout の本実装は Task 7 でも見る。今 deadline が 0 なら無視）。

`ls`: オペランド無しは cwd。`-a` で `.` 始まりを含める。`-1` は 1 行 1 名前。既定も 1 列でよい（AI 向け）。`-l` は `type size name` 程度で、パーミッション文字列は出さなくてよいが、ディレクトリなら先頭を `d` にする。`-R` はサブディレクトリを見出し付きで。

`mkdir -p`: 中間を作る。既存ディレクトリは成功。

`rm -r -f`: `-f` は欠損を無視。sandbox 外は拒否。`sandboxRoot` 自身の削除は拒否。

`cp -R`: ディレクトリなら再帰。宛先が既存ディレクトリなら中へ。

`mv`: 同じボリュームの rename。跨ぎは copy+rm でよい。

`head`/`tail` `-n N`: ファイルまたは stdin。

- [ ] **Step 4: 自己チェックが通ることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: `all in-process shell checks passed`

- [ ] **Step 5: コミット（実行しない）**

```bash
git add Projucer/Source/Shell/jucer_ShellApplets.cpp \
  Projucer/Source/Shell/jucer_InProcessShell.cpp \
  Projucer/Source/Shell/Tests/shell_selfcheck.cpp
# git commit -m "Add filesystem applets to the in-process shell"
```

---

### Task 6: テキストアプレットと `test`

**Files:**
- Modify: `Projucer/Source/Shell/jucer_ShellApplets.cpp`
- Modify: `Projucer/Source/Shell/Tests/shell_selfcheck.cpp`

**Interfaces:**
- Consumes: stdin / ファイル引数の既存 I/O
- Produces: `grep` `wc` `sort` `uniq` `cut` `diff` `sed` `find` `test` `[`。`tr` を POSIX 寄りの削除/置換に広げる（`tr -d` と集合）。

- [ ] **Step 1: 失敗するテストを足す**

```cpp
    root.getChildFile ("t.cpp").replaceWithText ("int foo;\nint bar;\n");
    root.getChildFile ("u.cpp").replaceWithText ("int foo;\nint baz;\n");
    expectContains ("grep -n foo t.cpp", "1:int foo;");
    expectOutput ("grep -c foo t.cpp", "1\n");
    expectContains ("grep -r bar .", "t.cpp");
    expectOutput ("wc -l t.cpp", "2 t.cpp\n");  // 実装は "2\n" でも "2 t.cpp\n" でもよい。テストを実装に合わせて直すこと。複数ファイル時は名前を付ける
    expectOutput ("sed 's/foo/qux/' t.cpp", "int qux;\nint bar;\n");
    expectExit ("sed -i 's/bar/qux/' t.cpp", 0);
    assert (root.getChildFile ("t.cpp").loadFileAsString().contains ("qux"));
    expectContains ("find . -name '*.cpp'", "u.cpp");
    expectExit ("test -f u.cpp", 0);
    expectExit ("[ -d . ]", 0);
    expectExit ("test -f missing", 1);
    expectContains ("diff -u t.cpp u.cpp", "-");
```

`wc` の出力形式は実装時にテストを実装へ合わせる。空白の数で揉めないよう `expectContains` でもよい。

- [ ] **Step 2: 自己チェックが落ちることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: assert 失敗。

- [ ] **Step 3: 実装する**

`grep`:
- 既定と `-E` は `std::regex`（ECMAScript）。無効正規表現は err と 2
- `-F` は `contains` / `containsIgnoreCase`
- `-i` `-n` `-v` `-c` `-l`
- `-r`/`-R` はディレクトリを再帰。バイナリは「NUL を含むファイルをスキップ」
- マッチ無しは 1、エラーは 2、ありは 0

`sed`:
- スクリプトは `s<delim><pat><delim><repl><delim>[gi]` のみ。`<delim>` は `/` でなくてもよい
- それ以外のスクリプトは err と 2（`sed` を完全実装しない）
- `-e` は複数可、順に適用
- `-i` は GNU どおりバックアップ無し上書き。ファイル引数必須
- パターンは `std::regex`。`g` で全部、無しは最初だけ

`find`:
- 最初の非オプションが起点。無ければ `.`
- `-name` はグロブ（`*` `?`）。`fnmatch` が無ければ自前の `*` `?` だけ
- `-type f` / `-type d`
- 未知オプション（`-exec` 含む）は err に「not supported」と 1
- 出力は 1 行 1 パス（cwd 相対でよい）

`test` / `[`:
- `[` は最後の argv が `]` であること
- 単項: `-f -d -e -s -n -z`
- 二項: `=` `!=` `-eq -ne -lt -gt`
- 引数無しは 1

`sort`: 行単位。`-r` 逆、`-n` 先頭の整数、`-u` 隣接重複
`uniq`: 隣接行
`cut -d D -f N`: 1-based。N は `1` または `1,3` または `2-` のどれか一つでよい。複雑なら `-f` 単一番号だけ
`diff -u`: 2 ファイル。unified の厳密さより、違いがあれば非 0 で `-` `+` 行が出ればよい
`tr -d set`: 削除。`tr set1 set2`: 置換

- [ ] **Step 4: 自己チェックが通ることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: `all in-process shell checks passed`

- [ ] **Step 5: コミット（実行しない）**

```bash
git add Projucer/Source/Shell/jucer_ShellApplets.cpp \
  Projucer/Source/Shell/Tests/shell_selfcheck.cpp
# git commit -m "Add grep sed find test and text applets"
```

---

### Task 7: git 委譲、コンパイラ拒否、sandbox、timeout / cancel

**Files:**
- Modify: `scripts/run_shell_selfcheck.sh`（libgit2 と `jucer_GitCommand.cpp` と Keychain をリンク）
- Modify: `Projucer/Source/Shell/jucer_ShellApplets.cpp`
- Modify: `Projucer/Source/Shell/jucer_InProcessShell.cpp`
- Modify: `Projucer/Source/Shell/Tests/shell_selfcheck.cpp`

**Interfaces:**
- Consumes: `ProjucerGit::run (const juce::StringArray& args, const juce::File& workingDirectory, std::atomic<bool>*)`
- Produces: `git` アプレット。未知コマンドの一覧。`clang++` 等の専用 127。sandbox 外書き込み失敗。`timeoutMs` と `cancelFlag`。

- [ ] **Step 1: リンクを足す**

`run_shell_selfcheck.sh` を `scripts/run_git_selfcheck.sh` に合わせる。
`LIBGIT2="$ROOT/OnDeviceBuild/third_party/libgit2"`。
`libgit2.a` が無ければ git 自己チェックと同じエラーで exit。
追加ソース: `jucer_GitCommand.cpp`、`jucer_Keychain.mm`。
追加: `-I "$LIBGIT2/include"`、`libgit2.a`、`-lz -liconv`。

- [ ] **Step 2: 失敗するテストを足す**

```cpp
    expectExit ("git init", 0);
    expectContains ("git status", "No commits"); // 文言は libgit2 実装に合わせる。status が 0 で何か出せばよいなら expectExit のみ
    expectContains ("echo hi | git", "usage"); // git にコマンドが無ければ git 側の失敗。パイプ接続の確認が目的なら `true | git status` で exit 0

    expectExit ("true | git status", 0);

    expectExit ("clang++ foo.cpp -o foo", 127);
    expectContains ("clang++ foo.cpp -o foo", "On-Device Build");
    expectContains ("make", "On-Device Build");

    expectExit ("echo pwned > ../outside.txt", 1);
    expectExit ("cd ..", 1);

    {
        std::atomic<bool> cancelled { true };
        ProjucerShell::Request request;
        request.commandLine = "echo should-not-run";
        request.workingDirectory = root;
        request.sandboxRoot = root;
        request.cancelFlag = &cancelled;
        const auto result = ProjucerShell::run (request);
        assert (result.exitCode != 0);
        assert (result.output.contains ("stopped") || result.output.contains ("cancel"));
    }

    {
        ProjucerShell::Request request;
        request.commandLine = "grep -r e .";
        request.workingDirectory = root;
        request.sandboxRoot = root;
        request.timeoutMs = 1;
        const auto result = ProjucerShell::run (request);
        assert (result.exitCode != 0);
        // 1ms では走り切る可能性あり。確実にするならループの多い find / 大きなファイルを使う。
        // timeout が効いたとき 124。効かなくてもテストを fragile にしないこと。
        juce::ignoreUnused (result);
    }
```

cancel のテストは **実行前に flag が true** なら、最初のコマンドの前で止まればよい。timeout の assert は、実装が期限を見ることを別の小さい内部ループ（`true` を 1000 回 `;` で繋ぐ等）で確認する。`;` の間で deadline を見るなら:

```cpp
    juce::String many;
    for (int i = 0; i < 5000; ++i)
        many << "true; ";
    ProjucerShell::Request request;
    request.commandLine = many;
    request.workingDirectory = root;
    request.sandboxRoot = root;
    request.timeoutMs = 1;
    const auto result = ProjucerShell::run (request);
    assert (result.exitCode == 124);
```

- [ ] **Step 3: 自己チェックが落ちることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: assert 失敗。

- [ ] **Step 4: 実装する**

`git` アプレット:
```cpp
int gitApplet (const juce::StringArray& argv, ShellIo& io, ShellState& state)
{
    auto result = ProjucerGit::run (argv, state.cwd, state.cancelFlag);
    if (io.out != nullptr)
        io.out->writeText (result.output, false, false, nullptr);
    return result.exitCode;
}
```
argv は `git` を含む。`ProjucerGit::run` は先頭の `git` を外す。

コンパイラ名はアプレット登録せず、`run` の「command not found」の直前で名前を見て spec 6.1 の文を返す。

`resolveWritablePath`: 解決＋symlink 後のフルパスが `sandboxRoot` と等しいか `isAChildOf`。外なら err。
`cd ..` が root の親になるなら失敗。

`run` の冒頭: `cancelFlag` が true なら output `"The command was stopped.\n"`、exit 1。
各リスト要素の前で `Time::getMillisecondCounter() >= deadlineMs` なら `"The command timed out after N seconds.\n"`（N は timeoutMs/1000）、exit 124。
出力が `maxOutputChars` を超えたら切り詰め `"\n...[truncated]"`。切り詰め後も exit code はコマンドのもの。

- [ ] **Step 5: 自己チェックが通ることを確認する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: `all in-process shell checks passed`

- [ ] **Step 6: コミット（実行しない）**

```bash
git add scripts/run_shell_selfcheck.sh \
  Projucer/Source/Shell/jucer_ShellApplets.cpp \
  Projucer/Source/Shell/jucer_InProcessShell.cpp \
  Projucer/Source/Shell/Tests/shell_selfcheck.cpp
# git commit -m "Delegate git, refuse compilers, enforce sandbox in the in-process shell"
```

---

### Task 8: `exec_command` に接続する

**Files:**
- Modify: `Projucer/Source/AI/jucer_AiTools.cpp`（`doExecCommand` の iOS 分岐、ツール説明）
- Modify: `Projucer/Source/AI/jucer_AgentLoop.cpp`（`systemInstructions`）
- Modify: `Projucer/Projucer.jucer`
- Modify: `Projucer/CMakeLists.txt`

**Interfaces:**
- Consumes: `ProjucerShell::run`
- Produces: iOS の `exec_command` がプロセス内シェルを使う。macOS 経路は不変。

- [ ] **Step 1: `jucer_AiTools.cpp` の先頭で Shell を include する**

```cpp
#include "../Shell/jucer_InProcessShell.h"
```

- [ ] **Step 2: iOS 分岐を置き換える**

`doExecCommand` の `#if JUCE_IOS` ブロック（git 特例を含む）を次にする。プレビュー・承認・workdir 解決はそのまま残す。

```cpp
   #if JUCE_IOS
    ProjucerShell::Request request;
    request.commandLine = cmd;
    request.workingDirectory = cwd;
    request.sandboxRoot = projectRoot;
    request.cancelFlag = &cancelRequested;
    request.timeoutMs = timeoutMs;
    request.maxOutputChars = maxExecOutputChars;

    const auto shellResult = ProjucerShell::run (request);

    auto output = shellResult.output;

    if (output.length() > maxExecOutputChars)
        output = output.substring (0, maxExecOutputChars) + "\n...[truncated]";

    juce::String resultText;
    resultText << "exit_code: " << shellResult.exitCode << "\n"
               << (output.isNotEmpty() ? output : juce::String ("(no output)"));

    return { shellResult.exitCode == 0, resultText, preview };
   #endif
```

timeoutMs の読み取りは、このブロックより **前** に移す。いま timeout は iOS return の後にある。iOS でも `yield_time_ms` が効くようにする。

- [ ] **Step 3: ツール説明を更新する**

`exec_command` の description を次にする（1 文、両 OS 共通）:

```
Run a shell command in the project working directory. On iOS this is an in-process POSIX subset (pipes, redirects, git, common file/text tools). It cannot spawn compilers or run executables; use On-Device Build to compile. Commands that write files or use the network need the user's approval unless they chose Full access.
```

`git` ツールの「On iOS exec_command only accepts git」に相当する文があれば、「Prefer the git tool or `exec_command` with git; both hit the same in-process implementation.」に直す。

- [ ] **Step 4: `systemInstructions` を更新する**

`jucer_AgentLoop.cpp` の該当箇所を次の意味に置き換える（英語のまま、既存指示に合わせる）:

```
You have exec_command. Use it to run shell commands in the project directory.
On iOS there is an in-process POSIX shell (no child processes): pipes, redirects,
variables, git, and common file/text tools work. There is no clang/make/xcodebuild
and no running of compiled binaries; compilation is the in-app On-Device Build.
Do not say you lack a terminal or git.
You have git. It runs inside the editor process. Use it for version control.
On iOS `git ...` inside exec_command is the same implementation.
```

「On iOS exec_command only accepts git; everything else there must go through the file tools.」は削除する。

- [ ] **Step 5: `Projucer.jucer` にグループを足す**

Git グループの直後:

```xml
    <GROUP id="{A1B2C3D4-0000-4000-8000-000000000004}" name="Shell">
      <FILE id="shHd1" name="jucer_InProcessShell.h" compile="0" resource="0"
            file="Source/Shell/jucer_InProcessShell.h"/>
      <FILE id="shCpp1" name="jucer_InProcessShell.cpp" compile="1" resource="0"
            file="Source/Shell/jucer_InProcessShell.cpp"/>
      <FILE id="shApH1" name="jucer_ShellApplets.h" compile="0" resource="0"
            file="Source/Shell/jucer_ShellApplets.h"/>
      <FILE id="shApC1" name="jucer_ShellApplets.cpp" compile="1" resource="0"
            file="Source/Shell/jucer_ShellApplets.cpp"/>
    </GROUP>
```

- [ ] **Step 6: `Projucer/CMakeLists.txt` の `target_sources` に足す**

`Source/Git/jucer_GitCommand.cpp` の次:

```
    Source/Shell/jucer_InProcessShell.cpp
    Source/Shell/jucer_ShellApplets.cpp
```

- [ ] **Step 7: 自己チェックを再実行する**

Run: `bash scripts/run_shell_selfcheck.sh`
Expected: `all in-process shell checks passed`

- [ ] **Step 8: iOS ターゲットが Shell ファイルを含むことを確認する**

Run: `rg "jucer_InProcessShell.cpp" Projucer/Projucer.jucer Projucer/CMakeLists.txt`
Expected: 両方にヒット。

macOS アプリをこのタスクでフルビルドする必要は無い（iOS 分岐はコンパイルされない）。iOS 実機確認はユーザーが行う。

- [ ] **Step 9: コミット（実行しない）**

```bash
git add Projucer/Source/AI/jucer_AiTools.cpp \
  Projucer/Source/AI/jucer_AgentLoop.cpp \
  Projucer/Projucer.jucer \
  Projucer/CMakeLists.txt
# git commit -m "Run iOS exec_command through the in-process shell"
```

---

## Self-Review

**Spec coverage**

| Spec | Task |
|---|---|
| iOS のみ切替、macOS 不変 | 8 |
| git 早期 return 削除、パイプで git | 7, 8 |
| 書き込みと cd の sandbox | 5, 7 |
| 未知コマンドが一覧を返す | 1, 7 |
| 停止と出力上限 | 7, 8 |
| POSIX サブセット（引用、リスト、パイプ、リダイレクト、変数、グロブ、`$( )`、`sh -c`） | 2–4 |
| ファイル / テキストアプレット | 5, 6 |
| コンパイラ拒否、LLVM を呼ばない | 7 |
| `./a.out` / ターミナル / ios_system をやらない | 全タスクの非対象 |
| 自己チェック | 1–7 |

**Placeholder scan:** なし。timeout テストは fragile 回避を本文に書いた。

**Type consistency:** `ProjucerShell::Request` / `Result` / `run` / `availableCommands` は Task 1 定義のまま。`ShellIo` / `ShellState` / `AppletFn` は cpp 内。`timeoutMs` は Task 8 で iOS 分岐の前に読む。
