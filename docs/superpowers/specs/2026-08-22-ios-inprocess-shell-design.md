# iOS プロセス内シェル（AI `exec_command`）設計文書

作成日: 2026-08-22

根拠: iOS では `fork` / `exec` / `posix_spawn` が使えず、AI はそれでも
`exec_command` に `ls` / `grep` / パイプを頻繁に投げる。git を libgit2 で
プロセス内実行しているのと同じ考え方を、POSIX コマンド集合へ広げる。

## 1. 目的

iPad 上の Projucer で、AI の `exec_command` が **子プロセス無し** で
Unix コマンドを実行し、stdout / stderr / exit code を返す。

成功条件: iOS（実機とシミュレータ）で、AI が `ls Source | grep jucer` や
`mkdir -p build && echo ok` を `exec_command` に投げ、`exit_code: 0` と
出力が返ること。下部ターミナルの対話セッションは対象外。

### CX 要件（必須）

1. macOS の `exec_command` は今どおり本物のシェルを使う。挙動を変えない。
2. iOS の `exec_command` はプロセス内シェルへ切り替える。git 専用の早期 return は消す（パイプの左辺で git を使えるようにする）。
3. 書き込みと `cd` はプロジェクトルートの外へ出ない。
4. 未知コマンドは黙って失敗せず、使えるコマンドを列挙して返す。
5. 実行中は既存どおり停止できる。出力は既存の上限で切る。

## 2. 対象と非対象

**対象**

- AI ツール `exec_command` の iOS 実装
- POSIX sh の狭い部分集合（後述）
- ファイル・テキスト系アプレット
- `git` をアプレットとして既存 `ProjucerGit` へ委譲
- `sh -c` / `/bin/sh -c` / `bash -c` を同じシェルへ再帰
- Mac 上の自己チェック（エンジンは iOS 専用でも Mac でテストする。git と同じ）

**対象外**

- 下部ターミナル、PTY、`vim` / `top` などの対話 TUI
- ユーザがコンパイルした `./a.out` の実行
- Clang / LLVM をシェルから叩くこと。LLVM は On-Device Build に既にある。
  `clang` / `clang++` / `cc` / `c++` / `make` / `cmake` / `ninja` /
  `xcodebuild` は「アプリ内の Build & Install を使え」と明示して exit 127
- bash/zsh 固有機能（配列、プロセス置換、ヒアドキュメント）
- `awk`、Python、curl、tar（必要になったら後続）
- `ios_system` / a-Shell / toybox / iSH の丸ごと取り込み
- リモート Mac へのシェル転送

## 3. 前提

iOS アプリは子プロセスを作れない（Apple DTS）。pthread、pipe、ファイル I/O は使える。
「fork」はスレッド、「exec」は関数表、で代替する。a-Shell の `ios_system` と
同じ思想だが、JUCE アプリに ObjC framework 群を持ち込まない。C++ で小さく書く。

シミュレータは macOS 上なので本物の fork が通ることがある。**`JUCE_IOS` なら
必ずプロセス内シェル**にする。実機とシミュレータで経路を分けない。

## 4. 全体構造

```
AiTools::doExecCommand          既存。iOS だけ差し替え
        │
ProjucerShell::run              新規。構文解析と実行
        │
  applets: echo, ls, grep, …    関数呼び出し
  git     → ProjucerGit::run
  sh -c   → 同じ run を再帰
```

依存は一方向。`ProjucerShell` は AI UI を知らない。git は既にある。
Clang / OnDeviceBuild には依存しない。

### 4.1 ファイル構成

| ファイル | 役割 |
|---|---|
| `Projucer/Source/Shell/jucer_InProcessShell.h` / `.cpp` | 公開 API、字句解析、構文、パイプライン、環境変数、サンドボックス |
| `Projucer/Source/Shell/jucer_ShellApplets.h` / `.cpp` | アプレット実装と登録 |
| `Projucer/Source/Shell/Tests/shell_selfcheck.cpp` | Mac 上の自己チェック |
| `scripts/run_shell_selfcheck.sh` | ビルドして実行 |

`Projucer.jucer` と `Projucer/CMakeLists.txt` の両方にコンパイル対象を足す。

### 4.2 公開 API

```cpp
namespace ProjucerShell
{
    struct Result
    {
        int exitCode = 1;
        juce::String output;   // stdout と stderr を結合。既存 exec_command と同じ
    };

    struct Request
    {
        juce::String commandLine;
        juce::File workingDirectory;
        juce::File sandboxRoot;          // 書き込みと cd の上限。通常は projectRoot
        std::atomic<bool>* cancelFlag = nullptr;
        int timeoutMs = 300000;
        int maxOutputChars = 64 * 1024;
    };

    Result run (const Request&);
    juce::StringArray availableCommands();
}
```

`availableCommands()` は未知コマンドのエラー文と、必要ならツール説明に使う。

## 5. シェル言語（v1 で実装するもの）

1 回の `run` はログインシェルではなく、渡された 1 文字列を解釈する。
`/bin/sh -c` 相当。

**やる**

- 単語分割、`'` `"` `\`、`#` コメント
- `$VAR` `${VAR}`、`export` / `unset`、アサインメント `FOO=bar cmd`
- グロブ `*` `?`（クォートしていない単語だけ）
- リダイレクト `>` `>>` `<` `2>` `2>>` `2>&1`
- パイプ `|`
- リスト `;` `&&` `||`
- コマンド置換 `$(...)` とバッククォート（入れ子の `run`、出力の末尾改行は POSIX どおり削る）
- `sh -c` / `bash -c` / パス付きの `*/sh` `*/bash` は中身を同じエンジンで実行
- `cd` は **その `run` の間だけ** 効く。次の `exec_command` の cwd は `workdir` 引数に戻る

**やらない**

- `if` / `for` / `while` / 関数 / サブシェル `( )` / `{ }`
- ジョブ制御、`&` バックグラウンド
- ヒアドキュメント
- `source` / `.`
- エイリアス

`if` が無いので、モデルには `test -f a && cat a` が通る、という状態で十分。

初期環境:

| 変数 | 値 |
|---|---|
| `PWD` | 開始時の workingDirectory |
| `HOME` | sandboxRoot |
| `PATH` | `/bin:/usr/bin`（名前解決には使わない。存在するフリ） |

コマンド名の解決: `argv[0]` の basename をアプレット表で探す。
`/bin/ls` は `ls`。PATH 探索も `dlopen` も無い。

## 6. アプレット

引数は展開済みの `juce::StringArray`。I/O はスレッドごとに
`InputStream` / `OutputStream`。`FILE*` の差し替えはしない。

| 名前 | 最低限のフラグ |
|---|---|
| `true` `false` `echo` `printf` `pwd` `env` `export` `unset` `which` `help` | `echo -n` |
| `cd` | 引数 0 個なら HOME |
| `ls` | `-l` `-a` `-1` `-R` |
| `cat` `head` `tail` | `-n`（行数） |
| `mkdir` | `-p` |
| `rm` | `-r` `-f` |
| `rmdir` | |
| `cp` | `-R` `-r` |
| `mv` `touch` | |
| `grep` | `-E` `-F` `-i` `-n` `-r` `-R` `-v` `-c` `-l`。既定は拡張正規表現 |
| `wc` | `-l` `-c` `-w` |
| `sort` | `-r` `-n` `-u` |
| `uniq` `tr` `cut` | `cut -d -f`、`tr` は文字集合の削除/置換 |
| `diff` | `-u` |
| `sed` | `s///` と `s///g` `s///i`、`-e`、`-i`（GNU と同じく拡張子なしで上書き） |
| `find` | 起点、`-name`（グロブ）、`-type f\|d`。**`-exec` は無い** |
| `test` `[` | `-f -d -e -s -n -z`、文字列 `=` `!=`、整数 `-eq -ne -lt -gt` |
| `git` | `ProjucerGit::run`。未対応サブコマンドはそのまま git 側のエラー |
| `sh` `bash` | `-c` のみ。それ以外は使い方を返して 2 |

stdin 無しでファイル引数を取るコマンドは、引数が無ければ stdin を読む
（`cat` `grep` `sed` `wc` `sort`）。

### 6.1 未知コマンドと「コンパイラに見えるもの」

未知: exit 127、出力は

```
command not found: <name>
This in-process shell has no child processes. Available commands:
<availableCommands を空白区切り>
```

次の名前はアプレット表に載せず、専用メッセージで 127 にする。

`clang` `clang++` `cc` `c++` `swiftc` `make` `cmake` `ninja` `xcodebuild` `ld` `ar`

```
<name> cannot run as a process on iOS.
Compile and install with the in-app On-Device Build. This shell does not produce or run executables.
```

LLVM は既にアプリに入っている。シェルから `-cc1` を再発明しない。

## 7. パイプラインと並行

`A | B` はパイプ 1 本とスレッド 2 本。各アプレットは自分の stdin/stdout だけ触る。
終了コードは POSIX どおり **最後のコマンド**。途中で cancel / timeout なら
残りのスレッドが出力を捨てて戻る。`pthread_cancel` は使わない。
長いアプレット（`grep -r`、`find`）はループの合間に `cancelFlag` と経過時間を見る。

`2>&1` は stderr を stdout と同じストリームへ乗せる。既存
`exec_command` は stdout/stderr を結合して返すので、リダイレクトが無いときも
結果文字列では両方混ざる。アプレット内部の「本当の stderr」は
`2>` のためだけに分ける。

## 8. サンドボックス

`sandboxRoot` はプロジェクトルート。

- パスは cwd 基準で解決する。絶対パスも許すが、**書き込みと `cd` は
  sandboxRoot 自身かその子孫だけ**。`isAChildOf` とシンボリックリンクの
  解決後に判定する（`jucer_AiPaths` と同じ意図。Shell は juce_core だけに依存し、
  AI モジュールは参照しない）。
- 読み取りは制限しない。既存の `read_file` と同じ。
- `../` で root の外へ `cd` したら exit 1。
- `rm -rf /` や root より上は拒否。

## 9. `AiTools` への接続

`doExecCommand` の `#if JUCE_IOS` ブロックを、git 特例ごと
`ProjucerShell::run` に置き換える。返りの形は今と同じ:

```
exit_code: <n>
<output or (no output)>
```

タイムアウト・キャンセル・出力切り詰めは `Request` に渡してシェル側で行う。
承認プレビューは今のままコマンド文字列を見せる。

`AgentLoop` の system 指示から「On iOS exec_command only accepts git」を消し、
プロセス内 POSIX シェルがあること、パイプが使えること、実行ファイルは
作れないこと、ビルドは On-Device Build であることを書く。

ツールスキーマの `exec_command` 説明は両 OS 共通のまま、「プロジェクト内で
シェルコマンドを実行する」とし、iOS のコマンド集合は system 指示側に置く。

## 10. テスト

`scripts/run_shell_selfcheck.sh` が Mac で juce_core と Shell ソースだけを
リンクして実行する。`git_selfcheck` と同じ型。JUCE アプリ全体はビルドしない。

最低限カバーすること:

- `echo`、引用符、`&&` `||` `;`
- `echo a | tr a b`
- `echo hi > f && cat f`
- `mkdir -p a/b && rm -rf a`
- `grep -n foo`、`sed 's/a/b/g'`、`find . -name '*.txt'`
- `FOO=bar echo $FOO`、`echo $(echo x)`
- `sh -c 'echo nested'`
- `cd` がその呼び出し内だけ効く
- sandbox 外への `cd` / 書き込みが失敗
- 未知コマンドが一覧を返す
- `clang++` が専用メッセージ
- git アプレット（libgit2 をリンクした段階）
- cancelFlag で中断

## 11. 非目標の再掲（実装中に足さない）

下部ターミナル接続、`./a.out`、LLVM フロントエンド呼び出し、
`ios_system` の vendoring、awk/python/curl。
