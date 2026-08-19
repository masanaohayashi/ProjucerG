# Projucer 統合ターミナル 設計文書

作成日: 2026-08-19

## 1. 目的

Projucer のプロジェクトウィンドウ下部に、VS Code の統合ターミナルに相当する
パネルを追加する。本物の対話シェルであり、`vim` / `top` / `claude code` などの
全画面 TUI アプリケーションが正しく動作することを要件とする。

### CX 要件（必須）

1. ターミナル部分を表示・非表示できること。
2. ターミナル部分の高さを、セパレータのドラッグで変更できること。

この2点が満たされないものは未完成とみなす。

## 2. 対象プラットフォーム

第一段階は macOS のみ。Windows と Linux は後続の作業とする。

このため PTY 層のヘッダは OS 非依存に保ち、実装本体のみ `#if JUCE_MAC` で囲む。
Linux は同じ `forkpty()` が使えるためヘッダの差し替えのみで済み、Windows は
ConPTY による別実装を同じヘッダの下に追加する。

**仮想インターフェースやファクトリは作らない。** 実装が2つ目になった時点で、
必要であれば導入すればよい。実装が1つしかない段階での抽象化は不要な複雑さである。

## 3. 全体構造

新規ディレクトリ `Projucer/Source/Terminal/` に、依存が一方向の4層を置く。

| 層 | ファイル | 責務 | 依存 |
|---|---|---|---|
| PTY | `jucer_PseudoTerminal.h` / `.cpp` | `forkpty()` によるシェル起動、非ブロッキング read/write、`TIOCSWINSZ` によるサイズ通知、子プロセス終了検知 | OS のみ |
| エミュレータ | `libvterm/` + `jucer_libvterm_unity.c` | エスケープシーケンスの解釈と画面モデルの保持 | なし |
| 描画 | `jucer_TerminalView.h` / `.cpp` | セルグリッドの描画、キー入力の変換、スクロールバック、選択とコピー&ペースト | PTY, エミュレータ |
| パネル | `jucer_TerminalPanel.h` / `.cpp` | 下部ドック本体。タブ、`＋` / 終了ボタン、高さの保持 | 描画 |

各層は上位層を知らない。`PseudoTerminal` は単体でテスト可能であり、
`TerminalView` は `PseudoTerminal` をコンストラクタで受け取る。

## 4. 端末エミュレーション: libvterm の同梱

エスケープシーケンスの解釈は自前で書かず、libvterm を同梱する。

- neovim が採用している実績あるライブラリであり、依存はゼロ、約 10k 行の C。
- ライセンスは MIT であり、Projucer の GPL / AGPL デュアルライセンスと矛盾しない。
- 自前で VT100/xterm 相当を書いた場合、`vim` が崩れない水準に達するまでに
  数週間の実装と長期のバグ取りを要する。ここは購入する。

### 同梱の形

`Source/Terminal/libvterm/` に配布物をそのまま置き、その隣に
`jucer_libvterm_unity.c` を1本だけ作成し、libvterm の全 `.c` を `#include` する。

ビルド定義への登録が必要なのは、この unity ファイル1本のみとなる。Projucer は
`Projucer/CMakeLists.txt` の `target_sources` と、261個の `<FILE>` 要素を手で
編集する形式の `Projucer.jucer` の**両方**を更新する必要があるため、
登録ファイル数を 1 に抑える意味は大きい。

`jucer_libvterm_unity.c` に対しては、libvterm 側の警告でビルドが止まらないよう
ターゲット単位ではなくファイル単位で警告を緩める。

## 5. 既存コードへの接続

### 5.1 レイアウト

`ProjectContentComponent` に以下を追加する。

```cpp
std::unique_ptr<TerminalPanel> terminalPanel;
std::unique_ptr<ResizableEdgeComponent> terminalResizerBar;
ComponentBoundsConstrainer terminalSizeConstrainer;
```

`jucer_ProjectContentComponent.cpp` の `resized()`（現状 80 行目付近）で、
サイドバーの左右分割を行う**前**に下端を確保する。

```
r.removeFromRight (10);
r.removeFromLeft (15);
r.removeFromTop (5);

projectMessagesComponent.setBounds (...);
headerComponent.setBounds (r.removeFromTop (40));
r.removeFromTop (10);

// ここに追加:
if (terminalPanel != nullptr && terminalPanel->isVisible())
{
    auto terminalArea = r.removeFromBottom (terminalPanel->getHeight());
    terminalPanel->setBounds (terminalArea);
    terminalResizerBar->setBounds (terminalArea.withHeight (4).translated (0, -2));
}

// 以降、既存のサイドバー分割
```

これによりターミナルはサイドバーとエディタの両方の下に横幅いっぱいで並ぶ。
VS Code と同じ配置である。

### 5.2 高さの変更（CX 要件 2）

サイドバー幅の変更にすでに使われている `ResizableEdgeComponent` を、向きだけ
変えて流用する。`jucer_ProjectContentComponent.cpp` の 140 行目付近が既存の前例。

```cpp
terminalResizerBar = std::make_unique<ResizableEdgeComponent> (terminalPanel.get(),
                                                              &terminalSizeConstrainer,
                                                              ResizableEdgeComponent::topEdge);
addAndMakeVisible (terminalResizerBar.get());
terminalResizerBar->setAlwaysOnTop (true);
```

`terminalSizeConstrainer` には最小高さ（約 80px = 数行）と、親の高さに対する
最大値を設定する。既存の `childBoundsChanged()` はサイドバーに対して `resized()`
を呼んでいるので、ここに `terminalPanel.get()` の分岐を足せば、ドラッグ中の
高さ変化がそのままレイアウト再計算につながる。

グリッドの行数はピクセル高さから算出されるため、高さ変更は
`vterm_set_size()` と `TIOCSWINSZ` の両方に伝播させる。`SIGWINCH` により
実行中のシェルや `vim` も追従する。

### 5.3 表示・非表示（CX 要件 1）

`jucer_CommandIDs.h` の 74 行目付近の並びに `showTerminal = 0x300037` を追加し、
`ProjectContentComponent::getAllCommands` / `getCommandInfo` / `perform` に
登録する。ショートカットは `Cmd + ^`（VS Code に合わせる）。

トグルは `setVisible()` と `resized()` のみで行い、ターミナルの
プロセスは**終了させない**。非表示中も裏でビルドが走り続け、再表示すると
続きが見える。これは統合ターミナルとして期待される挙動である。

パネルを一度も開いていない場合は `TerminalPanel` 自体を生成しない
（遅延生成）。ターミナルを使わない利用者にシェルプロセスの負担を課さない。

### 5.4 その他の接続

- 起動時のカレントディレクトリは、プロジェクトファイルの親ディレクトリ。
- フォントは `getAppSettings().appearance.getCodeFont()` を再利用する。
  コードエディタ用に設定済みの等幅フォントがそのまま反映される。
- パネルの**高さ**は `StoredSettings` に保存し、次回開いたときに復元する。
  表示状態は保存しない。起動時に復元すると、パネルを開いたことのある利用者は
  プロジェクトを開くたびにシェルプロセスが起動することになり、上記の遅延生成の
  方針と衝突するため。既定は非表示とする。
- `setProject()` でプロジェクトが差し替わる際は、既存の `sidebar` /
  `resizerBar` と同様に破棄・再生成する。

## 6. データフロー

### 読み込み方向

```
背景スレッド: read(fd)  →  ロックフリー FIFO (AbstractFifo)
                              ↓  AsyncUpdater
メッセージスレッド:  vterm_input_write()  →  damage コールバック
                              ↓
                      変化した行のみ repaint(rect)
```

### 書き込み方向

```
メッセージスレッド: KeyPress  →  vterm_keyboard_unichar / vterm_keyboard_key
                              ↓  libvterm の出力コールバック
                          write(fd)   ※ O_NONBLOCK
```

メッセージスレッドは read / write のいずれでもブロックしない。書き込みが
`EAGAIN` を返した場合は残りをバッファに積み、次の機会に送出する。

### fork() に関する既知の危険

マルチスレッドな JUCE アプリケーションにおける `fork()` は、fork から exec の
間に async-signal-safe な呼び出ししか許されない。環境変数（`TERM=xterm-256color`
など）の準備、作業ディレクトリの決定、引数配列の構築はすべて fork 前に完了させ、
子プロセス側では `chdir` と `execvp` のみを呼ぶ。malloc を伴う処理を子側に
書いてはならない。

## 7. エラー処理

| 事象 | 挙動 |
|---|---|
| `forkpty()` 失敗 | パネル内に errno の内容を表示。クラッシュさせない |
| 子プロセスの終了 | `[process exited N]` を表示し、再起動の手段を提供 |
| `read` が 0 / EIO を返す | 子の終了として扱い、上と同じ |
| 不正な UTF-8 の受信 | libvterm に委ねる（置換文字が表示される） |
| パネルを閉じずにウィンドウを閉じた | デストラクタで `SIGHUP` を送り、スレッドを join する |

## 8. テスト

### 自動

`tools/test_terminal_screen.py` を追加する。既存の `tools/test_separator_layout.py`
と同じ流儀のヘッドレス自己チェックとし、既定のバイト列を libvterm 層に流し込んで
出来上がったセルグリッドを assert する。

最低限、以下を検証する。

- SGR による前景色・背景色・太字の反映
- カーソル絶対移動（`CUP`）とスクロール領域（`DECSTBM`）
- 代替画面バッファの出入り（`?1049h` / `?1049l`）で元の画面が復元されること
- リサイズ後の折り返しが破綻しないこと

### 手動

`vim` の起動と終了、`top` の連続再描画、`claude code` の対話、`git log` の
ページャ、ウィンドウリサイズ中の追従、コピー & ペースト。

## 9. 実装フェーズ

1. **PTY と素通し表示** — `forkpty`、背景スレッド、キー入力、`TIOCSWINSZ` が
   通ることの確認。この段階では制御文字がそのまま見える。
2. **libvterm の同梱とグリッド描画** — この段階で `vim` と `claude code` が動く。
3. **パネル統合** — タブ、`ResizableEdgeComponent` による高さ変更、
   表示切り替えコマンド、`StoredSettings` への保存。**CX 要件はここで満たされる。**
4. **選択、コピー & ペースト、スクロールバック。**

各フェーズの終わりにビルドが通り、手動で動作確認できる状態を保つ。

## 10. 意図的に作らないもの

以下は今回の範囲に含めない。必要になった時点で追加する。

- 分割ペイン（タブのみとする）
- シェル統合によるコマンド区切りの装飾
- フォントのリガチャ対応
- sixel / kitty などの画像表示プロトコル
- ターミナルプロファイルの設定 UI
- 検索機能

## 付録A: libvterm 同梱時の注意

- ライセンスが MIT であることは配布元（neovim/libvterm）で確認済み。
- libvterm は文字エンコーディングのテーブルを、ビルド時に Perl スクリプト
  （`tbl2inc_c.pl`）で `encoding/*.inc` として生成する。Projucer のビルドに
  Perl を要求するのは避けたいため、**生成済みの `.inc` を一度だけ生成して
  リポジトリにコミットする**。同梱時にこの手順を踏まないとコンパイルが通らない。
- 同梱するのはリリース版のソースであり、リポジトリの HEAD ではない。
  同梱したバージョン番号を `Source/Terminal/libvterm/VERSION.txt` に記録する。
