# オンデバイスデバッガ設計文書

作成日: 2026-08-23

根拠: iPad 上の Projucer は既に JUCE アプリをコンパイル・リンク・署名・
インストール・起動できる。次に足りないのは「止めて中を見る」手段である。
`jassert` が飛んでもアプリが黙って消えるだけで、原因が分からない。

## 1. 目的

iPad の Projucer 上でビルドしたアプリを **デバッグ実行** し、Xcode の
デバッガに相当する操作をデバイス単体で行えるようにする。

成功条件（v1）:

1. ソースエディタの行にブレークポイントを置き、そこで実行が止まる。
2. `jassert` / C++ 例外 / クラッシュ（`EXC_BAD_ACCESS`）で止まり、
   原因の行が表示される。
3. 停止時にコールスタックが関数名・ファイル・行で表示され、
   フレームを選ぶとその位置のソースへ飛ぶ。
4. 選択フレームのローカル変数と引数が、型に従ってツリー展開される。
5. 任意アドレスのメモリを 16 進ダンプで閲覧できる。
6. 続行 / 次の行へ / 中に入る / 抜ける、が動く。

### 決定的な制約

**iOS では他プロセスをデバッグできない。** `task_for_pid` と
`debugserver` は Apple 特別署名の entitlement を必要とし、通常の開発者
証明書では取得できない。したがって「Projucer が対象アプリにアタッチする」
形は原理的に成立しない。

成立するのは **自プロセスデバッグ** のみ。`mach_task_self()` の例外ポート
は常に自分で掌握できる。よってデバッグ対象アプリの中にデバッグエージェント
を同居させ、Projucer はその外部 UI として振る舞う。

## 2. 対象と非対象

**対象**

- iOS 実機およびシミュレータでのデバッグ実行
- C / C++（JUCE アプリ）。arm64 のみ
- 行ブレークポイント、例外・アサート・クラッシュでの停止
- コールスタック、ローカル変数・引数、メモリ閲覧
- ステップ実行（over / into / out）

**対象外（v1）**

- Objective-C / Swift のシンボル対応
- 条件付きブレークポイント、ヒットカウント
- 式評価（`p foo.bar()` のような関数呼び出し）
- `std::string` / `juce::String` 等の整形表示（生のメンバとして出る）
- ウォッチポイント（HW 機構は同時に手に入るが UI は後回し）
- グローバル変数、静的変数
- macOS 版 Projucer からのデバッグ（同じ設計で後から載る）
- リリースビルドのデバッグ

## 3. アーキテクチャ

LLDB と同じ責務分割にする。**エージェントは薄く、シンボルは全部ホスト側**。

```
┌─ Projucer (iPad) ──────────────┐          ┌─ デバッグ実行アプリ ──────────┐
│ 賢い側 — DebuggerSession        │  TCP     │ 薄い側 — DebugAgent            │
│                                 │ loopback │                                │
│ ・DWARF 解析 (llvm::DWARFContext)│ ←────→  │ ・自 task の Mach 例外ポート  │
│ ・addr ⇄ file:line 双方向        │          │ ・HW BP レジスタ設定           │
│ ・変数の位置(DW_AT_location)と型 │          │ ・thread_get/set_state         │
│ ・スタック巻き戻し (fp チェーン) │          │ ・メモリ read / write          │
│ ・ASLR スライド補正              │          │ ・停止 / 再開 / スレッド一覧   │
│ ・UI: ガター/スタック/変数/メモリ│          │ ＋ ユーザーのアプリ本体        │
└─────────────────────────────────┘          └────────────────────────────────┘
```

エージェントは「レジスタとメモリとイベント」しか知らない。関数名も行番号も
型も持たない。この線引きにより、エージェントは 1000 行程度に収まり、
シンボル側は純粋関数として macOS 上で単体試験できる。

### 3.1 なぜこの分割か

- エージェントはユーザーのアプリと同居する。**小さいほど、対象アプリの
  挙動を汚さない。** LLVM をアプリ側に持ち込むのは論外。
- DWARF 解析はメモリを食い、失敗しうる。ホスト側に置けば、失敗しても
  対象アプリは走り続ける。
- ホスト側は既に `llvm-ios.a` をリンクしており、`llvm::DWARFContext` が
  その中に入っていることを確認済み。行テーブル・型・CFI を自前で書かない。

### 3.2 デバッグ実行アプリの作り方

**DebugAgent を静的リンクした実行アプリを、ビルドのたびに作り、既存の
`itms-services` インストール経路でそのまま入れる。**

常駐スタブアプリを置いて成果物を `dlopen` で差し替える案も検討したが、
App Group 共有コンテナと外部 dylib の署名検証という未検証の関門が 2 つ
増える。上記の方式は新しい未知数がゼロで、プロセス分離とクラッシュ隔離
という利点はそのまま残る。代償はデバッグ実行のたびにインストールの
プロンプトが 1 回出ること。`dlopen` 常駐化は後から効く最適化として残す。

デバッグビルド時の差分:

- `-g -O0 -fno-omit-frame-pointer` を強制
- `DebugAgent` のオブジェクトを追加リンク
- エントリポイントを差し替え、`main` の前にエージェントを起動して
  Projucer の待受ポートへ接続させる
- **リンク後のバイナリを strip 前に保存する。** これが DWARF の入手源に
  なる（別途 dSYM は作らない）

## 4. コンポーネント

### 4.1 DebugAgent（デバッグ実行アプリ側）

`OnDeviceBuildSource/agent/` に置く。JUCE に依存しない素の C++。

責務:

| 機能 | 実装 |
|---|---|
| 例外の捕捉 | `task_set_exception_ports(mach_task_self(), EXC_MASK_BREAKPOINT \| EXC_MASK_BAD_ACCESS \| EXC_MASK_BAD_INSTRUCTION \| EXC_MASK_ARITHMETIC, port, EXCEPTION_STATE_IDENTITY \| MACH_EXCEPTION_CODES, ARM_THREAD_STATE64)` + 専用スレッドの `mach_msg` ループ |
| 停止 | 例外スレッドは応答を返すまで待つ。他スレッドは `thread_suspend` |
| レジスタ | `thread_get_state` / `thread_set_state` の `ARM_THREAD_STATE64` |
| HW ブレークポイント | `ARM_DEBUG_STATE64` の `__bvr[]` / `__bcr[]`。全スレッドに同じ設定を配る |
| シングルステップ | `ARM_DEBUG_STATE64` の `__mdscr_el1` bit 0 (SS) |
| メモリ read/write | `vm_read_overwrite` / `vm_write`。不正アドレスで落ちずエラーが返る（`memcpy` は使わない） |
| 起動時報告 | `_dyld_get_image_header(0)` と slide を最初のメッセージで送る |
| アサート | `jassert` は `__builtin_debugtrap()` = `brk #0xf000` → `EXC_BREAKPOINT` として同じ経路で届く |
| C++ 例外 | `std::set_terminate` に停止フックを入れる |

**注意点**

- 例外ハンドラスレッド自身を `thread_suspend` してはならない。自スレッド
  の port を除外する。
- `EXC_BREAKPOINT` からの継続時、`brk` 由来なら PC を +4 する。HW BP 由来
  なら PC は据え置き。両者は例外コードで区別する。
- HW BP が張られた命令からの継続は、BP を一時無効化 → シングルステップ →
  再有効化、の 3 手が要る。素直に continue すると同じ場所で即座に再発火する。

### 4.2 デバッグプロトコル

ローカル TCP、1 行 1 JSON。既存のログ経路と同じくテキストで読める形にする。
接続はデバッグ実行アプリ側から Projucer へ。

ホスト → エージェント: `setBreakpoints(addr[])`, `continue`, `step`,
`readMemory(addr, len)`, `writeMemory(addr, bytes)`, `getRegisters(thread)`,
`getThreads`, `detach`

エージェント → ホスト: `loaded(imageBase, slide)`, `stopped(reason, thread,
pc, threads[])`, `exited(code)`, `output(text)`

### 4.3 シンボル層（Projucer 側）

`Projucer/Source/Debugger/jucer_DebugSymbols.*`。**純粋関数の集まり**として
書き、Mach にもネットワークにも触れない。macOS 上で単体試験する。

- `llvm::DWARFContext::create()` に strip 前バイナリを渡す
- 行テーブル: `file:line → addr`（BP 設置用）、`addr → file:line`（停止表示用）
- `DW_TAG_subprogram` の範囲から `addr → 関数名`
- `DW_TAG_formal_parameter` / `DW_TAG_variable` の `DW_AT_location` を評価
  （v1 は `DW_OP_fbreg` + `DW_AT_frame_base` の常用ケースのみ。それ以外は
  「表示不可」と出す）
- 型ツリー: `DW_TAG_base_type` / `pointer_type` / `array_type` /
  `structure_type` / `class_type` / `typedef` / `const_type` を辿る
- 全アドレスは静的アドレス。ASLR スライドの加減算は境界の 1 か所に閉じる

### 4.4 スタック巻き戻し

arm64 は `-fno-omit-frame-pointer` 下で `x29` チェーンが素直に辿れる。
`[fp]` が呼び出し元の fp、`[fp+8]` が LR。v1 はこれで足りる。

DWARF CFI による正確な巻き戻しは、最適化ビルドと prologue 途中の停止を
扱うために要るが、v1 の対象外（`-O0` 固定なので影響が小さい）。ただし
fp が 0 か、単調増加していない場合は打ち切る（無限ループ防止）。

### 4.5 UI（Projucer 側）

`Projucer/Source/Debugger/jucer_DebuggerPanel.*`。既存のターミナルパネルと
同じく下部の切替タブとして置く。

- **BP ガター**: `SourceCodeEditor` の行番号領域をクリックで設置・解除。
  BP は `file:line` のリストとしてプロジェクトに保存する
- **コールスタック**: フレーム一覧。選択でソースへジャンプ
- **変数ツリー**: 選択フレームの引数とローカル。`TreeView`
- **メモリビュー**: アドレス入力 + 16 進 / ASCII ダンプ
- **実行制御**: 続行 / 次の行へ / 中に入る / 抜ける / 停止

## 5. データフロー

**ブレークポイント設置**

```
ユーザーがガターをクリック
  → file:line を BP リストへ
  → 行テーブルで file:line → 静的アドレス
  → スライドを足して実行時アドレス
  → setBreakpoints でエージェントへ
  → 全スレッドの ARM_DEBUG_STATE64 を更新
```

**停止**

```
HW BP 発火 → EXC_BREAKPOINT → 例外スレッドが受信
  → 他スレッドを suspend → stopped をホストへ
  → ホストが pc からスライドを引く
  → 行テーブルで file:line、関数名を引く
  → fp チェーンを readMemory で辿ってフレーム列を作る
  → 選択フレームの変数を DW_AT_location で評価し readMemory で値を取る
  → 型ツリーに従ってツリー表示
```

## 6. エラー処理

| 状況 | 扱い |
|---|---|
| HW BP の同時数（6 個程度）を超えた | 超過分に印を付け「この BP は有効化されていない」と明示する。黙って無視しない |
| BP を置いた行にコードが無い | 行テーブルの次の実行可能行へ寄せ、寄せた先をガターに表示する |
| DWARF が読めない / 情報が無い関数 | フレームを `<unknown> + 0xNN` として出す。落とさない |
| 変数の location が未対応の形式 | 「表示不可（未対応の location）」と出す |
| `readMemory` が失敗 | 該当セルを `??` と出す |
| 接続が切れた / アプリが落ちた | セッションを終了して理由を出す。Projucer は生き続ける |
| 対象がデッドロックして応答しない | タイムアウトして「応答なし」を出し、強制 detach を提供する |

## 7. テスト方針

**Mach API は macOS と iOS で同一。** よってエージェントの中身は macOS 上で
テストできる。iOS 実機を要するのはインストール経路だけ。

- **シンボル層**: 純粋関数。既知の `-g` 付き `.o` を固定資産として置き、
  行テーブル・型・location の期待値を突き合わせる。既存の selfcheck に載せる
- **エージェント**: macOS のテストバイナリに組み込み、自分で `brk` を踏み、
  自分で HW BP を張って止まることを確認する
- **巻き戻し**: 既知の深さの再帰関数を止めてフレーム数と関数名を検証する
- **結合**: iPad 上で `jassert(false)` を含む小さな JUCE アプリを
  ビルド → デバッグ実行 → 止まる → スタックが出る、を手で通す

## 8. フェーズ

### Phase 0 — spike（必須。ここが赤なら設計を変える）

**iOS 実機で `thread_set_state(ARM_DEBUG_STATE64)` による HW ブレーク
ポイントとシングルステップが効くか。**

macOS では確立した手法だが、iOS で許可されるかは未検証。200 行程度の
検証コードで白黒つく。あわせて `jassert` の `brk` が本当に
`EXC_BREAKPOINT` として届くかも確認する。

**赤だった場合の代替**: コンパイラ計装に切り替える。手元の clang に
`-fsanitize-coverage=trace-pc-guard` を付けてビルドし、各基本ブロック
先頭のフックで停止する。確実に動き、BP 数は無制限、ステップ実行も自然に
できる。代償は実行速度低下と、行 BP がブロック境界に丸められること。
**この分岐は 3.1 の責務分割を変えない。** 変わるのはエージェント内部の
「止め方」だけなので、Phase 1 以降はそのまま使える。

### Phase 1 — 止まって見える

エージェント（例外ポート・停止・レジスタ・メモリ）、プロトコル、
Projucer 側の接続とセッション管理、コールスタック（fp チェーン）、
メモリビュー。この時点で `jassert` とクラッシュは捕まる。

### Phase 2 — 行ブレークポイント

DWARF 行テーブル、ガター UI、BP の永続化、HW BP の設置と継続処理。

### Phase 3 — 変数インスペクタ

`DW_AT_location` 評価、型ツリー、変数ツリー UI。

### Phase 4 — ステップ実行

over / into / out。シングルステップ + 一時 BP の組み合わせ。
`into` は行テーブルで次の行の境界を判定する。

## 9. 判断が必要な残件

- BP をプロジェクトファイル（`.jucer`）に保存するか、別ファイルにするか。
  `.jucer` を汚さない別ファイルを推す
- デバッグ実行アプリを通常ビルドと同じ bundle ID にするか分けるか。
  分けると通常版と共存でき、混乱が減る
- Phase 0 が赤で計装方式に倒れた場合、通常ビルドとデバッグビルドの
  成果物が別物になる。UI 上でその区別をどう見せるか
