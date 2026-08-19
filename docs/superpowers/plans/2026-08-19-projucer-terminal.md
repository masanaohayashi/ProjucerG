# Projucer 統合ターミナル 実装計画

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Projucer のプロジェクトウィンドウ下部に、`vim` や `claude code` などの全画面 TUI が動作する本物の対話シェルパネルを追加する。表示/非表示の切り替えと、セパレータのドラッグによる高さ変更ができること。

**Architecture:** 依存が一方向の4層。`PseudoTerminal`（forkpty による生の fd 操作）、同梱した libvterm（エスケープシーケンス解釈と画面モデル）、`TerminalView`（JUCE によるセルグリッド描画とキー入力）、`TerminalPanel`（タブを持つ下部ドック）。`ProjectContentComponent` がパネルと `ResizableEdgeComponent` を所有する。

**Tech Stack:** C++17 / JUCE 8.0.13 / libvterm (MIT, 同梱) / macOS の `forkpty(3)` / ビルドは Xcode と CMake の二本立て

**Spec:** `docs/superpowers/specs/2026-08-19-projucer-terminal-design.md`

## Global Constraints

- 対象プラットフォームは **macOS のみ**。PTY 実装本体は `#if JUCE_MAC` で囲む。ヘッダは OS 非依存に保つ。
- **仮想インターフェースやファクトリを作らない。** 実装が1つしかない段階での抽象化は導入しない。
- 新規ソースは `Projucer/Source/Terminal/` 以下に置く。
- 新しいコンパイル対象ファイルは、**`Projucer/CMakeLists.txt` の `target_sources` と `Projucer/Projucer.jucer` の両方**に登録する。片方だけの登録は不完全とみなす。
- ファイル名は既存の慣習に従い `jucer_` 接頭辞を付ける（同梱する libvterm 本体を除く）。
- 既存ファイルの改行コードを変換しない。このリポジトリには CRLF と LF が混在しており、エディタによる一括正規化は巨大な差分を生む。
- メッセージスレッドは PTY の read / write でブロックしない（`O_NONBLOCK`）。
- コミットは各タスクの末尾で行う。**push は行わない。**
- ビルド確認コマンド:
  `xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj -scheme "Projucer - App" -configuration Debug build`

---

### Task 1: libvterm の同梱とビルド統合

libvterm を取り込み、Projucer のビルドに1本のファイルとして組み込む。この時点では誰も使わないが、コンパイルとリンクが通ることを確認する。

**Files:**
- Create: `Projucer/Source/Terminal/libvterm/` (配布物のソースをそのまま)
- Create: `Projucer/Source/Terminal/libvterm/VERSION.txt`
- Create: `Projucer/Source/Terminal/jucer_libvterm_unity.c`
- Modify: `Projucer/CMakeLists.txt` (`target_sources`, 43 行目付近)
- Modify: `Projucer/Projucer.jucer` (`<MAINGROUP>` 内)
- Test: `tools/test_terminal_screen.py`

**Interfaces:**
- Consumes: なし
- Produces: libvterm の公開 API が `#include "libvterm/include/vterm.h"` で使える状態。主に使うのは
  `vterm_new(int rows, int cols)`, `vterm_free(VTerm*)`, `vterm_set_utf8(VTerm*, int)`,
  `vterm_obtain_screen(VTerm*)`, `vterm_screen_reset(VTermScreen*, int)`,
  `vterm_screen_set_callbacks(VTermScreen*, const VTermScreenCallbacks*, void*)`,
  `vterm_output_set_callback(VTerm*, VTermOutputCallback*, void*)`,
  `vterm_input_write(VTerm*, const char*, size_t)`,
  `vterm_screen_get_cell(const VTermScreen*, VTermPos, VTermScreenCell*)`,
  `vterm_set_size(VTerm*, int rows, int cols)`,
  `vterm_keyboard_unichar(VTerm*, uint32_t, VTermModifier)`,
  `vterm_keyboard_key(VTerm*, VTermKey, VTermModifier)`,
  `vterm_screen_convert_color_to_rgb(const VTermScreen*, VTermColor*)`

- [ ] **Step 1: libvterm のソースを取得して配置**

```bash
cd /tmp
git clone --depth 1 https://github.com/neovim/libvterm.git libvterm-src
cd libvterm-src && git log -1 --format=%H > /tmp/libvterm-sha.txt
mkdir -p /Users/ring2/Documents/src/Projucer8/Projucer/Source/Terminal/libvterm
cp -R include src LICENSE /Users/ring2/Documents/src/Projucer8/Projucer/Source/Terminal/libvterm/
```

`VERSION.txt` に取得元とコミット SHA を記録する。

```bash
cd /Users/ring2/Documents/src/Projucer8/Projucer/Source/Terminal/libvterm
{ echo "source: https://github.com/neovim/libvterm"; \
  echo "commit: $(cat /tmp/libvterm-sha.txt)"; \
  echo "license: MIT (see LICENSE)"; } > VERSION.txt
```

- [ ] **Step 2: エンコーディングテーブルを生成してコミット対象にする**

libvterm は `src/encoding/*.inc` を Perl スクリプトで生成する。Projucer のビルドに Perl を要求しないため、ここで一度だけ生成して成果物をリポジトリに含める。

```bash
cd /tmp/libvterm-src
ls src/encoding/          # *.tbl があるはず
for tbl in src/encoding/*.tbl; do
    perl -CSD tbl2inc_c.pl "$tbl" > "${tbl%.tbl}.inc"
done
cp src/encoding/*.inc /Users/ring2/Documents/src/Projucer8/Projucer/Source/Terminal/libvterm/src/encoding/
ls /Users/ring2/Documents/src/Projucer8/Projucer/Source/Terminal/libvterm/src/encoding/*.inc
```

生成された `.inc` が1つも無い場合、libvterm 側のスクリプト名が異なる。`ls *.pl` で確認して読み替えること。`.inc` が既に配布物に含まれていればこのステップは不要。

- [ ] **Step 3: unity ビルドファイルを書く**

`Projucer/Source/Terminal/jucer_libvterm_unity.c`:

```c
/*
    libvterm を1つの翻訳単位にまとめる。

    こうすることで、ビルド定義（CMakeLists.txt と Projucer.jucer の両方）に
    登録するファイルがこの1本だけで済む。同梱した libvterm のソースは
    一切変更しないこと。更新時は libvterm/ を丸ごと差し替える。
*/

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wconversion"
 #pragma clang diagnostic ignored "-Wsign-conversion"
 #pragma clang diagnostic ignored "-Wshorten-64-to-32"
 #pragma clang diagnostic ignored "-Wunused-parameter"
 #pragma clang diagnostic ignored "-Wmissing-prototypes"
#endif

#include "libvterm/src/encoding.c"
#include "libvterm/src/keyboard.c"
#include "libvterm/src/mouse.c"
#include "libvterm/src/parser.c"
#include "libvterm/src/pen.c"
#include "libvterm/src/screen.c"
#include "libvterm/src/state.c"
#include "libvterm/src/unicode.c"
#include "libvterm/src/vterm.c"

#if defined (__clang__)
 #pragma clang diagnostic pop
#endif
```

`libvterm/src/` の実際のファイル一覧を `ls` で確認し、上のリストと食い違う場合は実際のファイルに合わせること。`.c` を漏らすとリンクエラーになる。

- [ ] **Step 4: 自動テストを書く（この時点では失敗する）**

`tools/test_terminal_screen.py`:

```python
#!/usr/bin/env python3
"""Self-check for the vendored libvterm.

Compiles the unity build of libvterm together with a small C driver, feeds it
canned escape sequences and asserts the resulting cell grid. This is what keeps
us honest that vim-class TUI output is being interpreted, not just echoed.

Run: python3 tools/test_terminal_screen.py
"""
import pathlib, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).parent.parent
TERMINAL = ROOT / "Projucer/Source/Terminal"
UNITY = TERMINAL / "jucer_libvterm_unity.c"

DRIVER = r"""
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "libvterm/include/vterm.h"

static VTerm *vt;
static VTermScreen *screen;

static void feed (const char *s) { vterm_input_write (vt, s, strlen (s)); }

/* The character at (row, col), or '?' for anything not a single plain char. */
static char charAt (int row, int col)
{
    VTermScreenCell cell;
    VTermPos pos = { .row = row, .col = col };
    if (! vterm_screen_get_cell (screen, pos, &cell)) return '?';
    if (cell.chars[0] == 0) return ' ';
    return (char) cell.chars[0];
}

static void rowText (int row, int cols, char *out)
{
    for (int c = 0; c < cols; ++c) out[c] = charAt (row, c);
    out[cols] = 0;
    for (int c = cols - 1; c >= 0 && out[c] == ' '; --c) out[c] = 0;
}

static int gridCols = 80;

static void expectRow (int row, const char *expected)
{
    char got[256];
    rowText (row, gridCols, got);
    if (strcmp (got, expected) != 0)
    {
        printf ("FAIL row %d: expected \"%s\" got \"%s\"\n", row, expected, got);
        exit (1);
    }
}

int main (void)
{
    vt = vterm_new (24, 80);
    vterm_set_utf8 (vt, 1);
    screen = vterm_obtain_screen (vt);
    vterm_screen_reset (screen, 1);

    /* Plain text lands on the first row. */
    feed ("hello");
    expectRow (0, "hello");

    /* CUP moves the cursor absolutely: row 5, column 10 (1-based in the wire
       protocol, 0-based in the API). */
    feed ("\x1b[6;11Hworld");
    expectRow (5, "         world");

    /* SGR sets colour and boldness on subsequent cells. */
    feed ("\x1b[2;1H\x1b[1;31mred\x1b[0m");
    {
        VTermScreenCell cell;
        VTermPos pos = { .row = 1, .col = 0 };
        vterm_screen_get_cell (screen, pos, &cell);
        assert (cell.attrs.bold == 1);
        VTermColor fg = cell.fg;
        vterm_screen_convert_color_to_rgb (screen, &fg);
        if (! (fg.rgb.red > fg.rgb.blue && fg.rgb.red > fg.rgb.green))
        {
            printf ("FAIL: expected a reddish foreground, got %d,%d,%d\n",
                    fg.rgb.red, fg.rgb.green, fg.rgb.blue);
            return 1;
        }
    }

    /* The alternate screen must hide, then restore, the primary screen.
       This is what vim and less switch into, and getting it wrong is the
       classic way a home-made terminal eats the user's scrollback. */
    feed ("\x1b[?1049h");
    expectRow (0, "");
    feed ("alt-screen");
    expectRow (0, "alt-screen");
    feed ("\x1b[?1049l");
    expectRow (0, "hello");

    /* DECSTBM: with a scroll region set, a linefeed at the bottom of the
       region scrolls only inside it. */
    vterm_screen_reset (screen, 1);
    feed ("\x1b[1;3r");          /* scroll region = rows 1..3 */
    feed ("\x1b[1;1Ha\x1b[2;1Hb\x1b[3;1Hc");
    feed ("\x1b[3;1H\n");        /* linefeed at the bottom of the region */
    expectRow (0, "b");
    expectRow (1, "c");

    /* Resizing must not lose the visible text. This is the path the panel
       exercises every time the user drags the separator, so a regression here
       shows up as vim redrawing into a corrupt grid. */
    vterm_screen_reset (screen, 1);
    feed ("\x1b[1;1Hkeep-me");
    vterm_set_size (vt, 30, 100);  gridCols = 100;
    expectRow (0, "keep-me");
    vterm_set_size (vt, 24, 80);   gridCols = 80;
    expectRow (0, "keep-me");

    printf ("all terminal screen checks passed\n");
    vterm_free (vt);
    return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        driver = tmp / "driver.c"
        driver.write_text(DRIVER)
        binary = tmp / "driver"

        compile_cmd = [
            "cc", "-std=c99", "-o", str(binary),
            str(driver), str(UNITY),
            f"-I{TERMINAL}",
            f"-I{TERMINAL / 'libvterm/include'}",
        ]
        result = subprocess.run(compile_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit("compile failed:\n" + result.stdout + result.stderr)

        result = subprocess.run([str(binary)], capture_output=True, text=True)
        print(result.stdout, end="")
        if result.returncode != 0:
            sys.exit("terminal screen check failed:\n" + result.stderr)


if __name__ == "__main__":
    main()
```

- [ ] **Step 5: テストを実行して失敗を確認する**

Run: `python3 tools/test_terminal_screen.py`
Expected: この時点ではまだ通ることもある。もし `compile failed` になったら、`jucer_libvterm_unity.c` の `#include` 一覧が実ファイルと食い違っているか、`.inc` の生成漏れ。Step 2 と Step 3 に戻って直す。

- [ ] **Step 6: テストを通す**

Run: `python3 tools/test_terminal_screen.py`
Expected: `all terminal screen checks passed`

ここが通れば、libvterm がエスケープシーケンスを正しく解釈できており、代替画面とスクロール領域という TUI の要になる2機能が動いていることが確認できた状態。

- [ ] **Step 7: CMakeLists.txt に登録**

`Projucer/CMakeLists.txt` の `target_sources(Projucer PRIVATE ...)` の一覧に、アルファベット順の位置へ1行追加する。

```cmake
    Source/Settings/jucer_StoredSettings.cpp
    Source/Terminal/jucer_libvterm_unity.c
    Source/Utility/Helpers/jucer_CodeHelpers.cpp
```

- [ ] **Step 8: Projucer.jucer に登録**

`Projucer.jucer` の `<MAINGROUP>` 内に、既存の `<GROUP>` と同じ書式で Terminal グループを追加する。`id` は他と衝突しない任意の値。既存ファイルの改行コードを壊さないよう、エディタの自動整形をかけないこと。

```xml
      <GROUP id="{A1B2C3D4-0000-4000-8000-000000000001}" name="Terminal">
        <FILE id="tRmVt1" name="jucer_libvterm_unity.c" compile="1" resource="0"
              file="Source/Terminal/jucer_libvterm_unity.c"/>
      </GROUP>
```

`compile="1"` を忘れるとビルド対象にならない。

- [ ] **Step 9: ビルドを確認**

Run:
```bash
xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
           -scheme "Projucer - App" -configuration Debug build
```
Expected: `** BUILD SUCCEEDED **`

libvterm がリンクされたが、まだ誰からも呼ばれていない状態。リンカが未使用として落とす可能性があるが、この段階ではコンパイルが通ることが目的。

- [ ] **Step 10: コミット**

```bash
cd /Users/ring2/Documents/src/Projucer8
git add Projucer/Source/Terminal tools/test_terminal_screen.py \
        Projucer/CMakeLists.txt Projucer/Projucer.jucer
git commit -m "Vendor libvterm for the integrated terminal"
```

---

### Task 2: PseudoTerminal — forkpty によるシェル起動

シェルを疑似端末で起動し、非ブロッキングで読み書きし、サイズを通知し、終了を検知するクラス。JUCE のスレッドや GUI には一切依存させず、単体でテストできる形に保つ。

**Files:**
- Create: `Projucer/Source/Terminal/jucer_PseudoTerminal.h`
- Create: `Projucer/Source/Terminal/jucer_PseudoTerminal.cpp`
- Modify: `Projucer/CMakeLists.txt`
- Modify: `Projucer/Projucer.jucer`
- Test: `tools/test_pseudo_terminal.py`

**Interfaces:**
- Consumes: なし
- Produces: 以下のクラス。Task 3 の `TerminalView` がこれを所有する。

```cpp
class PseudoTerminal
{
public:
    PseudoTerminal();
    ~PseudoTerminal();

    bool start (const juce::File& workingDirectory, int numColumns, int numRows);
    void stop();

    bool isRunning() const noexcept;
    juce::String getLastError() const;
    int getExitCode() const noexcept;

    int readBytes  (char* destination, int maxBytes);
    int writeBytes (const char* source, int numBytes);

    void setSize (int numColumns, int numRows);
    int getFileDescriptor() const noexcept;
};
```

`readBytes` は読めたバイト数、何も無ければ 0、子が終了していれば -1 を返す。
`writeBytes` は実際に書けたバイト数を返す（部分書き込みがあり得る）。

- [ ] **Step 1: 失敗するテストを書く**

`tools/test_pseudo_terminal.py`:

```python
#!/usr/bin/env python3
"""Self-check for PseudoTerminal.

Compiles jucer_PseudoTerminal.cpp against minimal stubs for the two JUCE types
it touches, then drives a real shell through a real pty: send a command, read
the output back, and confirm the child exits. Run:
    python3 tools/test_pseudo_terminal.py
"""
import pathlib, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).parent.parent
TERMINAL = ROOT / "Projucer/Source/Terminal"

STUBS = r"""
#include <string>
#include <cstdio>

/* The parts of JUCE that PseudoTerminal touches, and nothing else. */
namespace juce
{
    class String
    {
    public:
        String() = default;
        String (const char* s) : text (s ? s : "") {}
        String (const std::string& s) : text (s) {}
        String& operator+= (const String& o) { text += o.text; return *this; }
        bool isEmpty() const { return text.empty(); }
        const char* toRawUTF8() const { return text.c_str(); }
        std::string text;
    };

    class File
    {
    public:
        File() = default;
        File (const String& p) : path (p) {}
        String getFullPathName() const { return path; }
        bool isDirectory() const { return ! path.isEmpty(); }
        String path;
    };
}

#define JUCE_MAC 1
"""

DRIVER = r"""
#include <cassert>
#include <cstring>
#include <string>
#include <unistd.h>

int main()
{
    PseudoTerminal pty;

    if (! pty.start (juce::File ("/tmp"), 80, 24))
    {
        printf ("FAIL: start() failed: %s\n", pty.getLastError().toRawUTF8());
        return 1;
    }

    /* Ask the shell to print a distinctive string and leave. */
    const char* cmd = "printf 'PTYOK\\n'; exit 0\n";
    pty.writeBytes (cmd, (int) strlen (cmd));

    /* Drain until we see our marker or the child goes away. Reading must never
       block, so an empty read is normal and we simply try again. */
    std::string collected;
    for (int attempt = 0; attempt < 2000; ++attempt)
    {
        char buffer[1024];
        const int numRead = pty.readBytes (buffer, sizeof (buffer));

        if (numRead > 0)          collected.append (buffer, (size_t) numRead);
        else if (numRead < 0)     break;
        else                      usleep (5000);

        if (collected.find ("PTYOK") != std::string::npos && ! pty.isRunning())
            break;
    }

    if (collected.find ("PTYOK") == std::string::npos)
    {
        printf ("FAIL: never saw the marker. Got: [%s]\n", collected.c_str());
        return 1;
    }

    /* Resizing a live pty must not throw or corrupt the descriptor. */
    pty.setSize (100, 40);

    pty.stop();
    if (pty.isRunning())
    {
        printf ("FAIL: still running after stop()\n");
        return 1;
    }

    /* A bad working directory is a user-visible failure, not a crash. */
    PseudoTerminal broken;
    broken.start (juce::File ("/definitely/not/a/real/directory"), 80, 24);
    broken.stop();

    printf ("all pseudo terminal checks passed\n");
    return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        source = (TERMINAL / "jucer_PseudoTerminal.cpp").read_text()
        header = (TERMINAL / "jucer_PseudoTerminal.h").read_text()

        # Splice the real header and implementation between our stubs and the
        # driver, so the code under test is compiled exactly as it ships.
        header = header.replace('#pragma once', '')
        source = source.replace('#include "jucer_PseudoTerminal.h"', '')

        combined = tmp / "combined.cpp"
        combined.write_text(STUBS + header + source + DRIVER)

        binary = tmp / "driver"
        result = subprocess.run(
            ["c++", "-std=c++17", "-o", str(binary), str(combined)],
            capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit("compile failed:\n" + result.stdout + result.stderr)

        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        print(result.stdout, end="")
        if result.returncode != 0:
            sys.exit("pseudo terminal check failed:\n" + result.stderr)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: テストを実行して失敗を確認する**

Run: `python3 tools/test_pseudo_terminal.py`
Expected: FAIL。`jucer_PseudoTerminal.h` がまだ存在しないため `FileNotFoundError`。

- [ ] **Step 3: ヘッダを書く**

`Projucer/Source/Terminal/jucer_PseudoTerminal.h`:

```cpp
#pragma once

//==============================================================================
/**
    A shell running under a pseudo-terminal.

    This owns the master side of a pty and the child process attached to its
    slave side. It deals in raw bytes and knows nothing about escape sequences -
    interpreting those is libvterm's job, one layer up.

    Reads and writes never block, so the message thread can drive this directly.
    A read that returns 0 means "nothing right now", not "finished".

    Only the macOS implementation exists today; the interface is deliberately
    free of anything platform-specific so a ConPTY body can be added later
    without disturbing anything above it.
*/
class PseudoTerminal
{
public:
    PseudoTerminal();
    ~PseudoTerminal();

    /** Launches the user's login shell with the given working directory and
        terminal size. Returns false on failure, with the reason in
        getLastError(). */
    bool start (const juce::File& workingDirectory, int numColumns, int numRows);

    /** Hangs up the child and closes the pty. Safe to call more than once, and
        safe to call when start() was never called or failed. */
    void stop();

    bool isRunning() const noexcept;
    juce::String getLastError() const                 { return lastError; }

    /** Valid once isRunning() returns false. -1 if the child never ran. */
    int getExitCode() const noexcept                  { return exitCode; }

    /** Returns the number of bytes read, 0 if none are waiting, or -1 if the
        child has gone away. */
    int readBytes (char* destination, int maxBytes);

    /** Returns the number of bytes actually written, which may be fewer than
        requested. The caller must keep the remainder and retry. */
    int writeBytes (const char* source, int numBytes);

    /** Tells the child its window has changed size, which raises SIGWINCH so
        that a running vim or top redraws itself. */
    void setSize (int numColumns, int numRows);

    int getFileDescriptor() const noexcept            { return masterFd; }

private:
    void reapChildIfNeeded();

    int masterFd = -1;
    int childPid = -1;
    int exitCode = -1;
    bool childHasExited = true;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PseudoTerminal)
};
```

テスト用スタブには `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` が無いので、テストの `STUBS` に以下を追加すること。

```c
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(C) \
    C (const C&) = delete; C& operator= (const C&) = delete;
```

- [ ] **Step 4: 実装を書く**

`Projucer/Source/Terminal/jucer_PseudoTerminal.cpp`:

```cpp
#include "jucer_PseudoTerminal.h"

#if JUCE_MAC

#include <util.h>          // forkpty
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

PseudoTerminal::PseudoTerminal() = default;

PseudoTerminal::~PseudoTerminal()
{
    stop();
}

bool PseudoTerminal::start (const juce::File& workingDirectory, int numColumns, int numRows)
{
    stop();

    struct winsize ws = {};
    ws.ws_col = (unsigned short) juce::jmax (1, numColumns);
    ws.ws_row = (unsigned short) juce::jmax (1, numRows);

    // Everything the child needs must be built here, before the fork. Between
    // fork() and exec() only async-signal-safe calls are legal, and this is a
    // multi-threaded app - allocating there would risk deadlocking on a malloc
    // lock held by another thread at the moment we forked.
    const juce::String shellPath = [&]
    {
        if (auto* fromEnv = getenv ("SHELL"))
            if (strlen (fromEnv) > 0)
                return juce::String (fromEnv);

        return juce::String ("/bin/zsh");
    }();

    const std::string shell (shellPath.toRawUTF8());
    const std::string directory (workingDirectory.isDirectory()
                                     ? workingDirectory.getFullPathName().toRawUTF8()
                                     : "/");

    const pid_t pid = forkpty (&masterFd, nullptr, nullptr, &ws);

    if (pid < 0)
    {
        lastError = juce::String ("Could not open a pseudo-terminal: ") + strerror (errno);
        masterFd = -1;
        return false;
    }

    if (pid == 0)
    {
        // Child. Only async-signal-safe calls from here to execl().
        if (chdir (directory.c_str()) != 0)
            _exit (127);

        setenv ("TERM", "xterm-256color", 1);

        execl (shell.c_str(), shell.c_str(), "-l", (char*) nullptr);
        _exit (127);
    }

    childPid = (int) pid;
    childHasExited = false;
    exitCode = -1;
    lastError = {};

    // Non-blocking, so that a read on the message thread returns immediately
    // when the child has nothing to say.
    fcntl (masterFd, F_SETFL, fcntl (masterFd, F_GETFL, 0) | O_NONBLOCK);

    return true;
}

void PseudoTerminal::stop()
{
    if (childPid > 0 && ! childHasExited)
    {
        kill ((pid_t) childPid, SIGHUP);

        // Give it a moment to leave on its own before insisting.
        for (int i = 0; i < 100 && ! childHasExited; ++i)
        {
            reapChildIfNeeded();

            if (! childHasExited)
                usleep (5000);
        }

        if (! childHasExited)
        {
            kill ((pid_t) childPid, SIGKILL);
            int status = 0;
            waitpid ((pid_t) childPid, &status, 0);
            childHasExited = true;
        }
    }

    if (masterFd >= 0)
    {
        close (masterFd);
        masterFd = -1;
    }

    childPid = -1;
}

void PseudoTerminal::reapChildIfNeeded()
{
    if (childPid <= 0 || childHasExited)
        return;

    int status = 0;
    const pid_t result = waitpid ((pid_t) childPid, &status, WNOHANG);

    if (result == (pid_t) childPid)
    {
        childHasExited = true;
        exitCode = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
    }
    else if (result < 0 && errno == ECHILD)
    {
        childHasExited = true;
    }
}

bool PseudoTerminal::isRunning() const noexcept
{
    return childPid > 0 && ! childHasExited;
}

int PseudoTerminal::readBytes (char* destination, int maxBytes)
{
    reapChildIfNeeded();

    if (masterFd < 0)
        return -1;

    const auto numRead = ::read (masterFd, destination, (size_t) maxBytes);

    if (numRead > 0)
        return (int) numRead;

    if (numRead == 0)
        return -1;                                  // the slave side closed

    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return 0;                                   // simply nothing waiting

    return -1;                                      // EIO: the child is gone
}

int PseudoTerminal::writeBytes (const char* source, int numBytes)
{
    if (masterFd < 0 || numBytes <= 0)
        return 0;

    const auto numWritten = ::write (masterFd, source, (size_t) numBytes);

    if (numWritten > 0)
        return (int) numWritten;

    return 0;                                       // full buffer: retry later
}

void PseudoTerminal::setSize (int numColumns, int numRows)
{
    if (masterFd < 0)
        return;

    struct winsize ws = {};
    ws.ws_col = (unsigned short) juce::jmax (1, numColumns);
    ws.ws_row = (unsigned short) juce::jmax (1, numRows);

    ioctl (masterFd, TIOCSWINSZ, &ws);
}

#else

// Windows and Linux bodies go here. Until then the panel is never created on
// those platforms, so these do nothing rather than pretending to work.

PseudoTerminal::PseudoTerminal() = default;
PseudoTerminal::~PseudoTerminal() = default;

bool PseudoTerminal::start (const juce::File&, int, int)
{
    lastError = "The integrated terminal is only available on macOS.";
    return false;
}

void PseudoTerminal::stop()                             {}
bool PseudoTerminal::isRunning() const noexcept         { return false; }
int  PseudoTerminal::readBytes (char*, int)             { return -1; }
int  PseudoTerminal::writeBytes (const char*, int)      { return 0; }
void PseudoTerminal::setSize (int, int)                 {}

#endif
```

テスト用スタブに `juce::jmax` が無いので、`STUBS` に追加すること。

```c
namespace juce { template <typename T> T jmax (T a, T b) { return a > b ? a : b; } }
```

- [ ] **Step 5: テストを実行して通ることを確認**

Run: `python3 tools/test_pseudo_terminal.py`
Expected: `all pseudo terminal checks passed`

失敗する場合、まず疑うのは `execl` に渡すシェルのパス。`echo $SHELL` の値がログインシェルとして実在するか確認する。

- [ ] **Step 6: ビルド定義に登録**

`Projucer/CMakeLists.txt` の `target_sources` に追加:

```cmake
    Source/Terminal/jucer_PseudoTerminal.cpp
```

`Projucer.jucer` の Terminal グループに追加:

```xml
        <FILE id="tRmPt1" name="jucer_PseudoTerminal.cpp" compile="1" resource="0"
              file="Source/Terminal/jucer_PseudoTerminal.cpp"/>
        <FILE id="tRmPt2" name="jucer_PseudoTerminal.h" compile="0" resource="0"
              file="Source/Terminal/jucer_PseudoTerminal.h"/>
```

- [ ] **Step 7: ビルドを確認**

Run:
```bash
xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
           -scheme "Projucer - App" -configuration Debug build
```
Expected: `** BUILD SUCCEEDED **`

- [ ] **Step 8: コミット**

```bash
git add Projucer/Source/Terminal tools/test_pseudo_terminal.py \
        Projucer/CMakeLists.txt Projucer/Projucer.jucer
git commit -m "Add PseudoTerminal, a shell running under a pty"
```

---

### Task 3: TerminalView — グリッド描画とキー入力

PTY と libvterm を繋ぎ、セルグリッドを描画し、キー入力を送り返す JUCE コンポーネント。このタスクが完了すると `vim` と `claude code` が動く。

**Files:**
- Create: `Projucer/Source/Terminal/jucer_TerminalView.h`
- Create: `Projucer/Source/Terminal/jucer_TerminalView.cpp`
- Modify: `Projucer/CMakeLists.txt`
- Modify: `Projucer/Projucer.jucer`

**Interfaces:**
- Consumes: Task 2 の `PseudoTerminal`、Task 1 の libvterm API
- Produces:

```cpp
class TerminalView final : public juce::Component,
                           private juce::Timer
{
public:
    explicit TerminalView (const juce::File& workingDirectory);
    ~TerminalView() override;

    /** True while the shell is alive. The panel uses this to label its tabs. */
    bool isShellRunning() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
};
```

- [ ] **Step 1: ヘッダを書く**

`Projucer/Source/Terminal/jucer_TerminalView.h`:

```cpp
#pragma once

#include "jucer_PseudoTerminal.h"

struct VTerm;
struct VTermScreen;

//==============================================================================
/**
    One terminal: a shell, an emulator, and the grid of cells they produce.

    Bytes arrive from the shell on a background thread, because a read that
    finds nothing should not spin the message thread. They are handed over
    through a lock-free fifo and interpreted on the message thread, where
    libvterm's damage callbacks tell us which rows actually changed so that a
    busy program like top repaints two lines rather than the whole window.
*/
class TerminalView final : public juce::Component,
                           private juce::Timer,
                           private juce::AsyncUpdater
{
public:
    explicit TerminalView (const juce::File& workingDirectory);
    ~TerminalView() override;

    bool isShellRunning() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    // libvterm calls these back on the message thread, from inside
    // vterm_input_write(). They are public only because the callback table
    // that names them is built at file scope in the .cpp.
    static int damageCallback (VTermRect, void* user);
    static int moveCursorCallback (VTermPos, VTermPos, int, void* user);
    static int pushScrollbackLine (int cols, const VTermScreenCell*, void* user);
    static void outputCallback (const char* bytes, size_t len, void* user);

private:
    class Reader;

    void timerCallback() override;
    void handleAsyncUpdate() override;

    void consumePendingBytes();
    void sendToShell (const char* bytes, int numBytes);
    void updateGridSizeFromBounds();
    juce::Rectangle<int> getCellBounds (int row, int column) const;

    PseudoTerminal pty;
    VTerm* vterm = nullptr;
    VTermScreen* screen = nullptr;

    std::unique_ptr<Reader> reader;
    juce::AbstractFifo incoming { 1 << 16 };
    juce::HeapBlock<char> incomingBuffer { (size_t) (1 << 16) };

    juce::Font font { juce::FontOptions {} };
    float cellWidth = 8.0f;
    int cellHeight = 16;
    int numColumns = 80;
    int numRows = 24;

    int cursorRow = 0, cursorColumn = 0;
    bool cursorVisible = true;
    bool cursorBlinkOn = true;

    juce::String statusMessage;
    juce::String pendingOutput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerminalView)
};
```

- [ ] **Step 2: 実装を書く**

`Projucer/Source/Terminal/jucer_TerminalView.cpp`:

```cpp
#include "../Application/jucer_Headers.h"
#include "jucer_TerminalView.h"

extern "C" {
 #include "libvterm/include/vterm.h"
}

//==============================================================================
/** Drains the pty on its own thread so that the message thread never waits on
    a read. It only moves bytes; nothing here interprets them. */
class TerminalView::Reader final : public juce::Thread
{
public:
    Reader (PseudoTerminal& p, juce::AbstractFifo& f, char* b, TerminalView& v)
        : juce::Thread ("Projucer terminal reader"), pty (p), fifo (f), buffer (b), view (v)
    {
    }

    void run() override
    {
        char chunk[4096];

        while (! threadShouldExit())
        {
            const int numRead = pty.readBytes (chunk, (int) sizeof (chunk));

            if (numRead > 0)
            {
                int start1, size1, start2, size2;
                fifo.prepareToWrite (numRead, start1, size1, start2, size2);

                if (size1 > 0) memcpy (buffer + start1, chunk, (size_t) size1);
                if (size2 > 0) memcpy (buffer + start2, chunk + size1, (size_t) size2);

                fifo.finishedWrite (size1 + size2);
                view.triggerAsyncUpdate();
            }
            else if (numRead < 0)
            {
                view.triggerAsyncUpdate();
                break;                       // the shell has gone
            }
            else
            {
                wait (10);                   // nothing waiting: idle briefly
            }
        }
    }

private:
    PseudoTerminal& pty;
    juce::AbstractFifo& fifo;
    char* buffer;
    TerminalView& view;
};

//==============================================================================
static const VTermScreenCallbacks screenCallbacks =
{
    TerminalView::damageCallback,
    nullptr,                                  // moverect
    TerminalView::moveCursorCallback,
    nullptr,                                  // settermprop
    nullptr,                                  // bell
    nullptr,                                  // resize
    TerminalView::pushScrollbackLine,
    nullptr,                                  // sb_popline
    nullptr                                   // sb_clear
};

TerminalView::TerminalView (const juce::File& workingDirectory)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    font = getAppSettings().appearance.getCodeFont();
    cellWidth  = juce::GlyphArrangement::getStringWidth (font, "M");
    cellHeight = (int) std::ceil (font.getHeight());

    vterm = vterm_new (numRows, numColumns);
    vterm_set_utf8 (vterm, 1);

    screen = vterm_obtain_screen (vterm);
    vterm_screen_reset (screen, 1);
    vterm_screen_set_callbacks (screen, &screenCallbacks, this);
    vterm_output_set_callback (vterm, outputCallback, this);

    if (! pty.start (workingDirectory, numColumns, numRows))
    {
        statusMessage = pty.getLastError();
    }
    else
    {
        reader = std::make_unique<Reader> (pty, incoming, incomingBuffer.get(), *this);
        reader->startThread();
    }

    startTimer (500);                          // cursor blink
}

TerminalView::~TerminalView()
{
    stopTimer();
    cancelPendingUpdate();

    if (reader != nullptr)
    {
        reader->signalThreadShouldExit();
        reader->stopThread (2000);
        reader.reset();
    }

    pty.stop();

    if (vterm != nullptr)
    {
        vterm_free (vterm);
        vterm = nullptr;
        screen = nullptr;
    }
}

bool TerminalView::isShellRunning() const
{
    return pty.isRunning();
}

//==============================================================================
void TerminalView::handleAsyncUpdate()
{
    consumePendingBytes();

    if (! pty.isRunning() && statusMessage.isEmpty())
    {
        statusMessage = "[process exited " + juce::String (pty.getExitCode()) + "]";
        repaint();
    }
}

void TerminalView::consumePendingBytes()
{
    for (;;)
    {
        int start1, size1, start2, size2;
        incoming.prepareToRead (incoming.getNumReady(), start1, size1, start2, size2);

        if (size1 + size2 == 0)
            break;

        if (size1 > 0) vterm_input_write (vterm, incomingBuffer.get() + start1, (size_t) size1);
        if (size2 > 0) vterm_input_write (vterm, incomingBuffer.get() + start2, (size_t) size2);

        incoming.finishedRead (size1 + size2);
    }
}

void TerminalView::sendToShell (const char* bytes, int numBytes)
{
    int offset = 0;

    while (offset < numBytes)
    {
        const int written = pty.writeBytes (bytes + offset, numBytes - offset);

        if (written <= 0)
        {
            // The child's buffer is full. Keep the rest and let the timer retry
            // rather than spinning here on the message thread.
            pendingOutput += juce::String::fromUTF8 (bytes + offset, numBytes - offset);
            return;
        }

        offset += written;
    }
}

//==============================================================================
int TerminalView::damageCallback (VTermRect rect, void* user)
{
    auto& self = *static_cast<TerminalView*> (user);

    const auto topLeft     = self.getCellBounds (rect.start_row, rect.start_col);
    const auto bottomRight = self.getCellBounds (rect.end_row - 1, rect.end_col - 1);

    self.repaint (topLeft.getUnion (bottomRight));
    return 1;
}

int TerminalView::moveCursorCallback (VTermPos pos, VTermPos oldPos, int visible, void* user)
{
    auto& self = *static_cast<TerminalView*> (user);

    self.repaint (self.getCellBounds (oldPos.row, oldPos.col));
    self.cursorRow = pos.row;
    self.cursorColumn = pos.col;
    self.cursorVisible = visible != 0;
    self.repaint (self.getCellBounds (pos.row, pos.col));

    return 1;
}

int TerminalView::pushScrollbackLine (int, const VTermScreenCell*, void*)
{
    // Scrollback arrives in Task 6. Accepting and dropping the line here keeps
    // libvterm happy in the meantime.
    return 1;
}

void TerminalView::outputCallback (const char* bytes, size_t len, void* user)
{
    static_cast<TerminalView*> (user)->sendToShell (bytes, (int) len);
}

//==============================================================================
juce::Rectangle<int> TerminalView::getCellBounds (int row, int column) const
{
    return { (int) (column * cellWidth), row * cellHeight,
             (int) std::ceil (cellWidth) + 1, cellHeight };
}

void TerminalView::updateGridSizeFromBounds()
{
    const int columns = juce::jmax (1, (int) (getWidth() / cellWidth));
    const int rows    = juce::jmax (1, getHeight() / cellHeight);

    if (columns == numColumns && rows == numRows)
        return;

    numColumns = columns;
    numRows = rows;

    // Both halves must be told: libvterm so its model reflows, and the pty so
    // the child gets SIGWINCH and redraws itself.
    vterm_set_size (vterm, numRows, numColumns);
    pty.setSize (numColumns, numRows);

    repaint();
}

void TerminalView::resized()
{
    updateGridSizeFromBounds();
}

void TerminalView::timerCallback()
{
    cursorBlinkOn = ! cursorBlinkOn;
    repaint (getCellBounds (cursorRow, cursorColumn));

    if (pendingOutput.isNotEmpty())
    {
        const auto utf8 = pendingOutput.toRawUTF8();
        const int length = (int) strlen (utf8);
        pendingOutput = {};
        sendToShell (utf8, length);
    }
}

//==============================================================================
static juce::Colour toColour (VTermScreen* screen, VTermColor colour, juce::Colour fallback)
{
    if (VTERM_COLOR_IS_DEFAULT_FG (&colour) || VTERM_COLOR_IS_DEFAULT_BG (&colour))
        return fallback;

    vterm_screen_convert_color_to_rgb (screen, &colour);
    return juce::Colour (colour.rgb.red, colour.rgb.green, colour.rgb.blue);
}

void TerminalView::paint (juce::Graphics& g)
{
    const auto defaultBackground = findColour (juce::CodeEditorComponent::backgroundColourId);
    const auto defaultForeground = findColour (juce::CodeEditorComponent::defaultTextColourId);

    g.fillAll (defaultBackground);

    const auto clip = g.getClipBounds();
    const int firstRow = juce::jmax (0, clip.getY() / cellHeight);
    const int lastRow  = juce::jmin (numRows - 1, clip.getBottom() / cellHeight);

    g.setFont (font);

    for (int row = firstRow; row <= lastRow; ++row)
    {
        for (int column = 0; column < numColumns; ++column)
        {
            VTermScreenCell cell;
            VTermPos pos;
            pos.row = row;
            pos.col = column;

            if (! vterm_screen_get_cell (screen, pos, &cell))
                continue;

            auto foreground = toColour (screen, cell.fg, defaultForeground);
            auto background = toColour (screen, cell.bg, defaultBackground);

            if (cell.attrs.reverse)
                std::swap (foreground, background);

            const auto area = getCellBounds (row, column);

            if (background != defaultBackground)
            {
                g.setColour (background);
                g.fillRect (area);
            }

            if (cell.chars[0] != 0 && cell.chars[0] != ' ')
            {
                g.setColour (foreground);
                g.setFont (font.withStyle (cell.attrs.bold ? juce::Font::bold
                                                           : juce::Font::plain));
                g.drawText (juce::String::charToString ((juce::juce_wchar) cell.chars[0]),
                            area, juce::Justification::centredLeft, false);
            }
        }
    }

    if (cursorVisible && cursorBlinkOn && hasKeyboardFocus (true))
    {
        g.setColour (defaultForeground.withAlpha (0.7f));
        g.fillRect (getCellBounds (cursorRow, cursorColumn));
    }

    if (statusMessage.isNotEmpty())
    {
        g.setColour (defaultForeground.withAlpha (0.6f));
        g.setFont (font);
        g.drawText (statusMessage,
                    getLocalBounds().removeFromBottom (cellHeight).reduced (4, 0),
                    juce::Justification::centredLeft);
    }
}

//==============================================================================
void TerminalView::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

bool TerminalView::keyPressed (const juce::KeyPress& key)
{
    if (! pty.isRunning())
        return false;

    VTermModifier modifiers = VTERM_MOD_NONE;

    if (key.getModifiers().isShiftDown())   modifiers = (VTermModifier) (modifiers | VTERM_MOD_SHIFT);
    if (key.getModifiers().isAltDown())     modifiers = (VTermModifier) (modifiers | VTERM_MOD_ALT);
    if (key.getModifiers().isCtrlDown())    modifiers = (VTermModifier) (modifiers | VTERM_MOD_CTRL);

    struct { int juceKey; VTermKey vtermKey; } const specialKeys[] =
    {
        { juce::KeyPress::returnKey,     VTERM_KEY_ENTER },
        { juce::KeyPress::escapeKey,     VTERM_KEY_ESCAPE },
        { juce::KeyPress::backspaceKey,  VTERM_KEY_BACKSPACE },
        { juce::KeyPress::deleteKey,     VTERM_KEY_DEL },
        { juce::KeyPress::tabKey,        VTERM_KEY_TAB },
        { juce::KeyPress::upKey,         VTERM_KEY_UP },
        { juce::KeyPress::downKey,       VTERM_KEY_DOWN },
        { juce::KeyPress::leftKey,       VTERM_KEY_LEFT },
        { juce::KeyPress::rightKey,      VTERM_KEY_RIGHT },
        { juce::KeyPress::homeKey,       VTERM_KEY_HOME },
        { juce::KeyPress::endKey,        VTERM_KEY_END },
        { juce::KeyPress::pageUpKey,     VTERM_KEY_PAGEUP },
        { juce::KeyPress::pageDownKey,   VTERM_KEY_PAGEDOWN },
    };

    for (const auto& mapping : specialKeys)
    {
        if (key.getKeyCode() == mapping.juceKey)
        {
            vterm_keyboard_key (vterm, mapping.vtermKey, modifiers);
            return true;
        }
    }

    const auto character = key.getTextCharacter();

    if (character != 0)
    {
        vterm_keyboard_unichar (vterm, (uint32_t) character, modifiers);
        return true;
    }

    // Ctrl+letter produces no text character, but the shell needs it - this is
    // how Ctrl+C reaches the running program.
    if (key.getModifiers().isCtrlDown())
    {
        const int code = key.getKeyCode();

        if (code >= 'A' && code <= 'Z')
        {
            vterm_keyboard_unichar (vterm, (uint32_t) code, VTERM_MOD_CTRL);
            return true;
        }
    }

    return false;
}
```

- [ ] **Step 3: `getCodeFont` の取得元を確認する**

`getAppSettings().appearance.getCodeFont()` を使うため、`jucer_Headers.h` 経由で `jucer_StoredSettings.h` と `jucer_AppearanceSettings.h` が見えていることを確認する。見えていなければ `jucer_TerminalView.cpp` の先頭で明示的に include すること。

Run: `grep -n "StoredSettings\|AppearanceSettings" Projucer/Source/Application/jucer_Headers.h`

- [ ] **Step 4: ビルド定義に登録**

`Projucer/CMakeLists.txt`:

```cmake
    Source/Terminal/jucer_TerminalView.cpp
```

`Projucer.jucer` の Terminal グループ:

```xml
        <FILE id="tRmTv1" name="jucer_TerminalView.cpp" compile="1" resource="0"
              file="Source/Terminal/jucer_TerminalView.cpp"/>
        <FILE id="tRmTv2" name="jucer_TerminalView.h" compile="0" resource="0"
              file="Source/Terminal/jucer_TerminalView.h"/>
```

- [ ] **Step 5: ビルドを確認**

Run:
```bash
xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
           -scheme "Projucer - App" -configuration Debug build
```
Expected: `** BUILD SUCCEEDED **`

`VTERM_COLOR_IS_DEFAULT_FG` などのマクロ名は libvterm のバージョンで異なることがある。コンパイルエラーになったら `libvterm/include/vterm.h` を読んで実際の名前に合わせる。

- [ ] **Step 6: コミット**

```bash
git add Projucer/Source/Terminal Projucer/CMakeLists.txt Projucer/Projucer.jucer
git commit -m "Add TerminalView, drawing the emulator's cell grid"
```

---

### Task 4: TerminalPanel — タブを持つ下部ドック

複数のターミナルをタブで束ね、新規作成と終了のボタンを持つコンテナ。

**Files:**
- Create: `Projucer/Source/Terminal/jucer_TerminalPanel.h`
- Create: `Projucer/Source/Terminal/jucer_TerminalPanel.cpp`
- Modify: `Projucer/CMakeLists.txt`
- Modify: `Projucer/Projucer.jucer`

**Interfaces:**
- Consumes: Task 3 の `TerminalView`
- Produces:

```cpp
class TerminalPanel final : public juce::Component
{
public:
    explicit TerminalPanel (const juce::File& workingDirectory);
    ~TerminalPanel() override;

    /** Opens a new tab and gives it the keyboard. */
    void addTerminal();

    /** Focuses the terminal the user is looking at. Called when the panel is
        shown, so that typing goes straight to the shell. */
    void focusCurrentTerminal();

    void paint (juce::Graphics&) override;
    void resized() override;
};
```

- [ ] **Step 1: ヘッダを書く**

`Projucer/Source/Terminal/jucer_TerminalPanel.h`:

```cpp
#pragma once

#include "jucer_TerminalView.h"

//==============================================================================
/**
    The dock at the bottom of the project window: a row of tabs, each holding
    one shell, plus buttons to open and close them.

    The panel owns the terminals but not its own placement - the height and the
    resizer live in ProjectContentComponent, next to the sidebar's, so that all
    of the window's layout arithmetic stays in one place.
*/
class TerminalPanel final : public juce::Component
{
public:
    explicit TerminalPanel (const juce::File& workingDirectory);
    ~TerminalPanel() override;

    void addTerminal();
    void closeCurrentTerminal();
    void focusCurrentTerminal();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int defaultHeight = 220;
    static constexpr int minimumHeight = 80;

private:
    TerminalView* getCurrentTerminal() const;

    juce::File workingDirectory;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TextButton addButton { "+" };
    juce::TextButton closeButton { "x" };
    int nextTerminalNumber = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerminalPanel)
};
```

- [ ] **Step 2: 実装を書く**

`Projucer/Source/Terminal/jucer_TerminalPanel.cpp`:

```cpp
#include "../Application/jucer_Headers.h"
#include "jucer_TerminalPanel.h"

TerminalPanel::TerminalPanel (const juce::File& wd)
    : workingDirectory (wd)
{
    setOpaque (true);

    addAndMakeVisible (tabs);
    tabs.setOutline (0);

    addAndMakeVisible (addButton);
    addButton.setTooltip ("Open another terminal");
    addButton.onClick = [this] { addTerminal(); };

    addAndMakeVisible (closeButton);
    closeButton.setTooltip ("Close this terminal");
    closeButton.onClick = [this] { closeCurrentTerminal(); };

    addTerminal();
}

TerminalPanel::~TerminalPanel()
{
    // Clearing the tabs deletes the views, and each view's destructor hangs up
    // its shell. Doing it explicitly keeps the order obvious.
    tabs.clearTabs();
}

void TerminalPanel::addTerminal()
{
    const auto name = "Terminal " + juce::String (nextTerminalNumber++);

    tabs.addTab (name,
                 findColour (juce::CodeEditorComponent::backgroundColourId),
                 new TerminalView (workingDirectory),
                 true);

    tabs.setCurrentTabIndex (tabs.getNumTabs() - 1);
    focusCurrentTerminal();
}

void TerminalPanel::closeCurrentTerminal()
{
    if (tabs.getNumTabs() <= 1)
        return;                       // always leave the user one terminal

    tabs.removeTab (tabs.getCurrentTabIndex());
    focusCurrentTerminal();
}

TerminalView* TerminalPanel::getCurrentTerminal() const
{
    return dynamic_cast<TerminalView*> (tabs.getCurrentContentComponent());
}

void TerminalPanel::focusCurrentTerminal()
{
    if (auto* terminal = getCurrentTerminal())
        terminal->grabKeyboardFocus();
}

void TerminalPanel::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::CodeEditorComponent::backgroundColourId));

    // A hairline along the top, so the panel reads as a separate region even
    // before the user notices the resizer sitting on it.
    g.setColour (findColour (juce::CodeEditorComponent::defaultTextColourId).withAlpha (0.2f));
    g.fillRect (0, 0, getWidth(), 1);
}

void TerminalPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop (1);

    auto buttonRow = area.removeFromTop (24).removeFromRight (56);
    closeButton.setBounds (buttonRow.removeFromRight (28).reduced (2));
    addButton.setBounds (buttonRow.reduced (2));

    tabs.setBounds (getLocalBounds().withTrimmedTop (1));
}
```

- [ ] **Step 3: ビルド定義に登録**

`Projucer/CMakeLists.txt`:

```cmake
    Source/Terminal/jucer_TerminalPanel.cpp
```

`Projucer.jucer` の Terminal グループ:

```xml
        <FILE id="tRmTp1" name="jucer_TerminalPanel.cpp" compile="1" resource="0"
              file="Source/Terminal/jucer_TerminalPanel.cpp"/>
        <FILE id="tRmTp2" name="jucer_TerminalPanel.h" compile="0" resource="0"
              file="Source/Terminal/jucer_TerminalPanel.h"/>
```

- [ ] **Step 4: ビルドを確認**

Run:
```bash
xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
           -scheme "Projucer - App" -configuration Debug build
```
Expected: `** BUILD SUCCEEDED **`

- [ ] **Step 5: コミット**

```bash
git add Projucer/Source/Terminal Projucer/CMakeLists.txt Projucer/Projucer.jucer
git commit -m "Add TerminalPanel, the tabbed dock holding the terminals"
```

---

### Task 5: ProjectContentComponent への統合 — 表示切り替えと高さ変更

**このタスクで CX 要件の2点が満たされる。** パネルをウィンドウ下部に配置し、コマンドで表示を切り替え、`ResizableEdgeComponent` で高さを変えられるようにする。

**Files:**
- Modify: `Projucer/Source/Project/UI/jucer_ProjectContentComponent.h` (140-152 行目付近のメンバ、および public メソッド)
- Modify: `Projucer/Source/Project/UI/jucer_ProjectContentComponent.cpp` (80, 115, 129-145, 650, 758, 882 行目付近)
- Modify: `Projucer/Source/Application/jucer_CommandIDs.h` (74 行目付近)
- Modify: `Projucer/Source/Application/jucer_Application.cpp` (`createViewMenu`, 424 行目付近)

**Interfaces:**
- Consumes: Task 4 の `TerminalPanel`（`addTerminal()`, `focusCurrentTerminal()`, `defaultHeight`, `minimumHeight`）
- Produces: `ProjectContentComponent::toggleTerminal()` と `isTerminalVisible() const`

- [ ] **Step 1: コマンド ID を追加**

`Projucer/Source/Application/jucer_CommandIDs.h` の `showExporterSettings` の下に1行追加する。

```cpp
        showProjectSettings     = 0x300030,
        showFileExplorerPanel   = 0x300033,
        showModulesPanel        = 0x300034,
        showExportersPanel      = 0x300035,
        showExporterSettings    = 0x300036,
        showTerminal            = 0x300037,
```

- [ ] **Step 2: ヘッダにメンバとメソッドを追加**

`jucer_ProjectContentComponent.h` の public 部、`showProjectSettings()` の近くに:

```cpp
    void toggleTerminal();
    bool isTerminalVisible() const;
```

private 部、`resizerBar` の宣言の下に:

```cpp
    std::unique_ptr<TerminalPanel> terminalPanel;
    std::unique_ptr<ResizableEdgeComponent> terminalResizerBar;
    ComponentBoundsConstrainer terminalSizeConstrainer;
```

ファイル先頭の include に追加:

```cpp
#include "../../Terminal/jucer_TerminalPanel.h"
```

- [ ] **Step 3: `resized()` にレイアウトを追加**

`jucer_ProjectContentComponent.cpp` の `resized()`（80 行目付近）。ヘッダを取り除いた直後、サイドバーの左右分割より**前**に挿入する。ターミナルはサイドバーとエディタの両方の下に来るため、横方向の分割より先に下端を確保しなければならない。

```cpp
    r.removeFromTop (10);

    if (terminalPanel != nullptr && terminalPanel->isVisible())
    {
        const auto height = jlimit (TerminalPanel::minimumHeight,
                                    jmax (TerminalPanel::minimumHeight, r.getHeight() - 100),
                                    terminalPanel->getHeight());

        auto terminalArea = r.removeFromBottom (height);
        terminalPanel->setBounds (terminalArea);

        // The resizer straddles the panel's top edge so it is easy to grab.
        terminalResizerBar->setBounds (terminalArea.getX(), terminalArea.getY() - 2,
                                       terminalArea.getWidth(), 5);
    }

    auto sidebarArea = r.removeFromLeft (...);      // 既存のまま
```

- [ ] **Step 4: `childBoundsChanged()` にターミナルの分岐を追加**

115 行目付近。ドラッグ中に `ResizableEdgeComponent` がパネルの高さを直接書き換えるので、それを拾ってレイアウトを再計算する。サイドバーと同じ仕組み。

```cpp
void ProjectContentComponent::childBoundsChanged (Component* child)
{
    if (child == sidebar.get() || child == terminalPanel.get())
        resized();
}
```

- [ ] **Step 5: `setProject()` でパネルを破棄する**

129 行目付近、`resizerBar = nullptr;` の並びに追加する。プロジェクトが変われば作業ディレクトリも変わるため、シェルは作り直す。

```cpp
        hideEditor();
        terminalResizerBar = nullptr;
        terminalPanel = nullptr;
        resizerBar = nullptr;
        sidebar = nullptr;
```

- [ ] **Step 6: トグルを実装**

`jucer_ProjectContentComponent.cpp` に追加する。パネルは初回のトグルまで生成しない。ターミナルを使わない利用者にシェルプロセスを抱えさせないため。

```cpp
bool ProjectContentComponent::isTerminalVisible() const
{
    return terminalPanel != nullptr && terminalPanel->isVisible();
}

void ProjectContentComponent::toggleTerminal()
{
    if (project == nullptr)
        return;

    if (terminalPanel == nullptr)
    {
        // Project already knows its own folder - no need to derive it here.
        terminalPanel = std::make_unique<TerminalPanel> (project->getProjectFolder());
        terminalPanel->setSize (getWidth(),
                                getAppSettings().getGlobalProperties()
                                    .getIntValue ("terminalPanelHeight",
                                                  TerminalPanel::defaultHeight));
        addAndMakeVisible (terminalPanel.get());

        terminalSizeConstrainer.setMinimumHeight (TerminalPanel::minimumHeight);
        terminalSizeConstrainer.setMaximumHeight (4000);

        terminalResizerBar = std::make_unique<ResizableEdgeComponent> (terminalPanel.get(),
                                                                      &terminalSizeConstrainer,
                                                                      ResizableEdgeComponent::topEdge);
        addAndMakeVisible (terminalResizerBar.get());
        terminalResizerBar->setAlwaysOnTop (true);
    }
    else
    {
        const bool nowVisible = ! terminalPanel->isVisible();

        terminalPanel->setVisible (nowVisible);
        terminalResizerBar->setVisible (nowVisible);

        // Deliberately not stopping the shell: a build left running while the
        // panel is hidden should still be there when it comes back.
        if (! nowVisible)
            getAppSettings().getGlobalProperties()
                .setValue ("terminalPanelHeight", terminalPanel->getHeight());
    }

    resized();

    if (isTerminalVisible())
        terminalPanel->focusCurrentTerminal();
}
```

- [ ] **Step 7: コマンドを登録**

`getAllCommands`（650 行目付近）の配列に追加:

```cpp
                         CommandIDs::showExporterSettings,
                         CommandIDs::showTerminal,
```

`getCommandInfo`（758 行目付近）に `showFileExplorerPanel` と同じ書式で追加:

```cpp
    case CommandIDs::showTerminal:
        result.setInfo ("Show Terminal",
                        "Shows or hides the terminal panel at the bottom of the window",
                        CommandCategories::general, 0);
        result.setActive (project != nullptr);
        result.setTicked (isTerminalVisible());
        result.defaultKeypresses.add ({ '`', cmdCtrl, 0 });
        break;
```

`perform`（882 行目付近）のディスパッチに追加:

```cpp
        case CommandIDs::showExporterSettings:      showCurrentExporterSettings();  break;
        case CommandIDs::showTerminal:              toggleTerminal();               break;
```

- [ ] **Step 8: メニューに追加**

`jucer_Application.cpp` の `createViewMenu()`（424 行目付近）:

```cpp
    menu.addCommandItem (commandManager.get(), CommandIDs::showExporterSettings);

    menu.addSeparator();
    menu.addCommandItem (commandManager.get(), CommandIDs::showTerminal);

    menu.addSeparator();
    createColourSchemeItems (menu);
```

- [ ] **Step 9: ビルドを確認**

Run:
```bash
xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
           -scheme "Projucer - App" -configuration Debug build
```
Expected: `** BUILD SUCCEEDED **`

- [ ] **Step 10: 手動で CX 要件を確認する**

自動テストでは確認できない部分なので、実際に動かす。Projucer を起動し、任意のプロジェクトを開いて以下を順に確認する。

1. **表示** — メニュー View > Show Terminal、または `Cmd + ^` でパネルが下部に現れる。
2. **プロンプト** — シェルのプロンプトが表示され、`ls` と打って Enter で結果が出る。
3. **非表示** — もう一度 `Cmd + ^` でパネルが消え、エディタが元の高さに戻る。
4. **状態の保持** — 再度表示すると、先ほどの `ls` の出力がそのまま残っている。
5. **高さ変更** — パネル上端にカーソルを合わせると上下の矢印カーソルになり、ドラッグで高さが変わる。エディタ側が連動して縮む。
6. **最小高さ** — 上端を下まで引っ張っても 80px より小さくならない。
7. **リサイズ追従** — パネル内で `top` を起動した状態で高さを変えると、`top` の表示行数が追従する。`q` で終了。
8. **メニューのチェック** — 表示中は View メニューの Show Terminal にチェックが付く。

いずれかが失敗した場合、そのタスクは未完了。次に進まないこと。

- [ ] **Step 11: コミット**

```bash
git add Projucer/Source/Project/UI/jucer_ProjectContentComponent.h \
        Projucer/Source/Project/UI/jucer_ProjectContentComponent.cpp \
        Projucer/Source/Application/jucer_CommandIDs.h \
        Projucer/Source/Application/jucer_Application.cpp
git commit -m "Dock the terminal panel into the project window"
```

---

### Task 6: スクロールバック、選択、コピー & ペースト

日常的に使える水準に仕上げる。ここまでの5タスクで機能としては成立しているので、このタスクは独立して延期できる。

**Files:**
- Modify: `Projucer/Source/Terminal/jucer_TerminalView.h`
- Modify: `Projucer/Source/Terminal/jucer_TerminalView.cpp`

**Interfaces:**
- Consumes: Task 3 の `TerminalView`
- Produces: 外部に対する新しい API は無い。`pushScrollbackLine` が実際に行を保存するようになる。

- [ ] **Step 1: スクロールバックの保持を実装**

`jucer_TerminalView.h` のメンバに追加:

```cpp
    struct ScrollbackLine { juce::String text; };

    std::deque<ScrollbackLine> scrollback;
    int scrollOffset = 0;                    // rows scrolled up from the live view
    static constexpr int maxScrollbackLines = 10000;
```

`jucer_TerminalView.cpp` の `pushScrollbackLine` を、行を捨てずに保存する形に置き換える。

```cpp
int TerminalView::pushScrollbackLine (int cols, const VTermScreenCell* cells, void* user)
{
    auto& self = *static_cast<TerminalView*> (user);

    juce::String text;

    for (int i = 0; i < cols; ++i)
        text += (cells[i].chars[0] != 0)
                    ? juce::String::charToString ((juce::juce_wchar) cells[i].chars[0])
                    : " ";

    self.scrollback.push_back ({ text.trimEnd() });

    // A long build log should not become a memory leak.
    while ((int) self.scrollback.size() > maxScrollbackLines)
        self.scrollback.pop_front();

    return 1;
}
```

- [ ] **Step 2: ホイールでのスクロールを実装**

`jucer_TerminalView.h` に追加:

```cpp
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
```

`jucer_TerminalView.cpp`:

```cpp
void TerminalView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const int rowsToScroll = juce::roundToInt (wheel.deltaY * 3.0f);

    if (rowsToScroll == 0)
        return;

    const int maximum = (int) scrollback.size();
    const int wanted = juce::jlimit (0, maximum, scrollOffset + rowsToScroll);

    if (wanted != scrollOffset)
    {
        scrollOffset = wanted;
        repaint();
    }
}
```

`paint()` の行の取得を、`scrollOffset` を考慮した形に変える。スクロールバック領域に入る行は `scrollback` から文字列として描き、それ以外は従来どおり `vterm_screen_get_cell` から描く。

```cpp
    for (int row = firstRow; row <= lastRow; ++row)
    {
        const int historyIndex = (int) scrollback.size() - scrollOffset + row;

        if (scrollOffset > 0 && historyIndex < (int) scrollback.size())
        {
            // A line that has scrolled off the live screen: colours are not
            // retained in the history, so it is drawn in the default pen.
            g.setColour (defaultForeground);
            g.setFont (font);
            g.drawText (scrollback[(size_t) historyIndex].text,
                        getCellBounds (row, 0).withWidth (getWidth()),
                        juce::Justification::centredLeft, false);
            continue;
        }

        for (int column = 0; column < numColumns; ++column)
        {
            // ... 既存のセル描画をそのまま
        }
    }
```

新しい出力が来たらライブ画面に戻す。`consumePendingBytes()` の末尾に追加:

```cpp
    if (scrollOffset != 0)
    {
        scrollOffset = 0;
        repaint();
    }
```

- [ ] **Step 3: 選択とコピー & ペーストを実装**

`jucer_TerminalView.h` に追加:

```cpp
    void mouseDrag (const juce::MouseEvent&) override;
    juce::String getSelectedText() const;

    juce::Point<int> selectionAnchor, selectionEnd;   // in cell coordinates
    bool hasSelection = false;
```

`jucer_TerminalView.cpp`:

`mouseDown` はタスク3で既に書いてある。以下はその**置き換え**であり、追加ではない。

```cpp
static juce::Point<int> toCell (juce::Point<int> pixel, float cellWidth, int cellHeight)
{
    return { (int) (pixel.x / cellWidth), pixel.y / cellHeight };
}

void TerminalView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    selectionAnchor = selectionEnd = toCell (e.getPosition(), cellWidth, cellHeight);
    hasSelection = false;
    repaint();
}

void TerminalView::mouseDrag (const juce::MouseEvent& e)
{
    selectionEnd = toCell (e.getPosition(), cellWidth, cellHeight);
    hasSelection = (selectionEnd != selectionAnchor);
    repaint();
}

juce::String TerminalView::getSelectedText() const
{
    if (! hasSelection)
        return {};

    auto from = selectionAnchor, to = selectionEnd;

    if (to.y < from.y || (to.y == from.y && to.x < from.x))
        std::swap (from, to);

    juce::String result;

    for (int row = from.y; row <= to.y; ++row)
    {
        const int firstColumn = (row == from.y) ? from.x : 0;
        const int lastColumn  = (row == to.y)   ? to.x   : numColumns - 1;

        for (int column = firstColumn; column <= lastColumn; ++column)
        {
            VTermScreenCell cell;
            VTermPos pos;
            pos.row = row;
            pos.col = column;

            if (vterm_screen_get_cell (screen, pos, &cell) && cell.chars[0] != 0)
                result += juce::String::charToString ((juce::juce_wchar) cell.chars[0]);
            else
                result += " ";
        }

        if (row != to.y)
            result += "\n";
    }

    return result.trimEnd();
}
```

`keyPressed` の先頭に、Cmd+C と Cmd+V の処理を加える。これらはシェルに渡してはならない。

```cpp
bool TerminalView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0))
    {
        if (hasSelection)
        {
            juce::SystemClipboard::copyTextToClipboard (getSelectedText());
            return true;
        }
        // Without a selection, fall through so that Ctrl-C style interrupts
        // still reach the shell if the user has remapped them.
    }

    if (key == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0))
    {
        const auto text = juce::SystemClipboard::getTextFromClipboard();
        const auto utf8 = text.toRawUTF8();
        sendToShell (utf8, (int) strlen (utf8));
        return true;
    }

    if (! pty.isRunning())
        return false;

    // ... 既存の処理
}
```

`paint()` の末尾、カーソル描画の前に選択範囲の反転を加える。

```cpp
    if (hasSelection)
    {
        auto from = selectionAnchor, to = selectionEnd;

        if (to.y < from.y || (to.y == from.y && to.x < from.x))
            std::swap (from, to);

        g.setColour (defaultForeground.withAlpha (0.25f));

        for (int row = from.y; row <= to.y; ++row)
        {
            const int firstColumn = (row == from.y) ? from.x : 0;
            const int lastColumn  = (row == to.y)   ? to.x   : numColumns - 1;

            g.fillRect (getCellBounds (row, firstColumn)
                            .getUnion (getCellBounds (row, lastColumn)));
        }
    }
```

- [ ] **Step 4: ビルドを確認**

Run:
```bash
xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
           -scheme "Projucer - App" -configuration Debug build
```
Expected: `** BUILD SUCCEEDED **`

- [ ] **Step 5: 手動で確認**

1. `ls -la /usr/lib` のように画面を溢れる出力を出し、ホイールで遡れること。
2. 遡った状態で何か入力すると、ライブ画面に戻ること。
3. ドラッグで文字を選択でき、選択範囲が反転表示されること。
4. `Cmd + C` でコピーでき、別のアプリに貼り付けられること。
5. `Cmd + V` でクリップボードの内容がプロンプトに入ること。
6. `vim` を開き、`Ctrl + C` がちゃんと vim に届くこと（`Cmd + C` に食われていないこと）。

- [ ] **Step 6: コミット**

```bash
git add Projucer/Source/Terminal
git commit -m "Add scrollback, selection and clipboard support to the terminal"
```

---

### Task 7: 全画面 TUI の動作確認とドキュメント

**Files:**
- Modify: `FORK_CHANGES.md`

- [ ] **Step 1: 全画面 TUI を実際に動かす**

これが本機能の存在意義そのものなので、簡易パーサでは通らない項目を狙って確認する。

1. `vim` を起動 → 文字入力、`:q!` で終了。終了後に元のプロンプトが復元されること（代替画面）。
2. `top` を起動 → 表示が毎秒更新され、画面が流れずその場で書き換わること。`q` で終了。
3. `claude code` を起動 → 対話ができること。
4. `git log` → ページャの上下スクロールが効くこと。`q` で終了。
5. `printf '\033[31mred\033[32mgreen\033[0m\n'` → 色が付くこと。
6. ウィンドウ全体をリサイズ → ターミナルの列数が追従し、`vim` の表示が崩れないこと。
7. 日本語を含むファイル名を `ls` → 文字化けしないこと。

- [ ] **Step 2: 自動テストを両方走らせる**

Run:
```bash
python3 tools/test_terminal_screen.py
python3 tools/test_pseudo_terminal.py
```
Expected: 両方とも `all ... checks passed`

- [ ] **Step 3: FORK_CHANGES.md に追記**

`## GUI Editor Improvements` セクションの後、`## Template Improvements` の前に新しいセクションを追加する。既存の文体と行幅（約 80 桁）に合わせること。

```markdown
## Integrated Terminal

- Added a terminal panel at the bottom of the project window, in the manner of
  the one in VS Code. It runs the user's login shell under a real pseudo-
  terminal, so full-screen programs such as `vim`, `top` and `claude code`
  work rather than merely echoing their escape sequences. The panel is shown
  and hidden from View > Show Terminal (`Cmd`+`` ` ``), its height is set by
  dragging the separator along its top edge, and several shells can be kept
  side by side in tabs. Escape sequences are interpreted by libvterm, which is
  bundled under `Source/Terminal/libvterm`. macOS only for now.
```

- [ ] **Step 4: コミット**

```bash
git add FORK_CHANGES.md
git commit -m "Document the integrated terminal"
```

---

## 完了の定義

以下がすべて満たされたとき、この計画は完了とする。

- `python3 tools/test_terminal_screen.py` が通る。
- `python3 tools/test_pseudo_terminal.py` が通る。
- `xcodebuild ... -configuration Debug build` が `** BUILD SUCCEEDED **` で終わる。
- Task 5 Step 10 の CX 確認8項目がすべて通る。
- Task 7 Step 1 の TUI 確認7項目がすべて通る。
- `FORK_CHANGES.md` に記載がある。
- **push はしていない。** ユーザの手動確認を経て、明示的な指示があるまで行わない。
