# ProjucerG ライブAI編集機能 実装計画

## 1. 目的

起動中のProjucerGをCodexやClaudeから自然言語で操作し、GUI Editor上に編集結果がリアルタイムで見える機能を追加する。

最初の完成目標は、次の指示を処理できることとする。

> Lowpass Filter用のスライダーをComponentの中央から少し左下に置いて。値は20から20000Hzでロータリースライダー、テキストボックスはいらない。

期待する動作は次のとおり。

1. AIがユーザーに、どの `.jucer` プロジェクトとGUIドキュメントを編集するか確認する。
2. ProjucerGが指定された `.jucer` を開いていることを確認し、対象GUIドキュメントをアクティブにする。未オープンの場合は、ユーザーの許可を得て開く。
3. AIが対象GUIドキュメント、キャンバスサイズ、既存コンポーネントを取得する。
4. ProjucerGのGUI Editorに半透明のSliderプレビューが表示される。
5. ProjucerGに「AI編集中」バーが表示され、ユーザーは一時停止・中断・取消を実行できる。
6. AIが確定を指示すると、実際のSliderがGUI Editorへ追加される。
7. Sliderには、範囲 `20.0`〜`20000.0`、ロータリースタイル、`NoTextBox`が設定される。
8. 追加されたSliderが選択され、既存のプロパティパネルにも設定が表示される。
9. セッション全体を一度のUndoで元に戻せる。
10. AIが編集後の画面と構造化状態を再取得し、結果を確認できる。
11. ユーザーがEscapeキーまたは「中断」を実行した場合、AIが中断イベントを受け取り、「ユーザーにより中断された」と会話上で明示する。

本機能は、AIにマウス座標をクリックさせるGUI自動化ではなく、ProjucerG内部のGUI Editorモデルを通常操作と同じ経路で変更する。

## 2. スコープ

### 2.1 初期版に含めるもの

- macOS上で起動中のProjucerGへのローカル接続（P0: must / 最優先）
- アクティブなGUIドキュメントの取得
- キャンバスサイズ、グリッド、コンポーネント一覧、選択状態の取得
- Sliderの非破壊プレビュー
- Sliderの追加
- Sliderの名前、メンバー名、位置、サイズ、範囲、スタイル、テキストボックス設定
- AI編集状態を示す操作バー
- 一時停止、中断、今回の編集を戻す操作
- Escapeキーによる緊急停止
- AI操作単位のUndo
- GUI Editor領域の画像取得
- MCPサーバーとCodex／Claudeの設定例
- 通信・操作・エラーのローカルログ

### 2.2 初期版に含めないもの

- 任意のC++、Python、シェルコード実行
- ProjucerGの全メニューを遠隔操作する機能
- インターネット経由の接続
- 複数AIクライアントからの同時編集
- Slider以外の全コンポーネント対応
- AIによる自動保存
- ユーザーの確認なしでのプロジェクト生成・ビルド・IDE起動

LabelやButtonなどへの対応は、Sliderによる縦切りの完成後に同じ仕組みへ追加する。

### 2.3 プラットフォーム優先順位

| Priority | Platform | 方針 |
|---|---|---|
| P0 | macOS | 初期版の必須対象。設計、実装、手動受け入れテストを最初に完了する。 |
| P1 | Windows | macOS版の安定後に対応する。IPC、ランタイムファイル、プロセス生存確認をWindowsへ適合させる。 |
| P2 | Linux | Windows版の後に対応する。各Desktop EnvironmentでのGUI Editor表示とIPCを確認する。 |

プロトコルと `GuiDocumentAdapter` は最初からOS非依存に保つが、P1/P2のためにP0の完成を遅らせない。

## 3. 設計原則

### 3.1 ユーザーの停止操作を常に優先する

- 理想形では、ユーザーはAI編集中でもProjucerGを通常操作できる。
- 初期版で安全な同時編集が難しい場合、AI編集セッション中はドキュメントの通常編集を排他にしてよい。
- 排他中でも、Escapeキー、「中断」、「一時停止」、「今回の編集を戻す」、アプリ終了は常に操作できる。
- Escapeキーと「中断」ボタンは、通常のAI操作キューより優先して処理する。
- AI操作待ちやネットワーク待ちでJUCEメッセージスレッドをブロックしない。
- 同時編集を許可する段階では、ユーザーがドキュメントを変更した場合、古い状態を前提にしたAI操作を拒否する。

### 3.2 対象プロジェクトを暗黙に決めない

- AIは編集を開始する前に、対象の `.jucer` ファイルをユーザーへ確認する。
- 複数のGUIドキュメントがある場合は、対象のGUIドキュメントも確認する。
- 「現在アクティブだから」という理由だけで対象を確定しない。
- ユーザーが指定した `.jucer` と、ProjucerGで開いているProjectが一致することを検証する。
- 対象が開かれていない場合、AIは勝手に別Projectを編集せず、開いてよいかユーザーへ確認する。

### 3.3 プレビューと確定を分離する

- プレビューはドキュメント、UndoManager、生成コードを変更しない。
- プレビューはGUI Editor上のオーバーレイだけで表現する。
- 確定時だけ既存の `ComponentLayout` と `UndoManager` を通して変更する。

### 3.4 AI専用の別モデルを作らない

- Sliderの作成と復元には既存の `ComponentTypeHandler` / `SliderHandler` を利用する。
- コンポーネント追加には既存の `ComponentLayout::addComponentFromXml()` を利用する。
- 位置とプロパティは既存のXML表現を正とする。
- 保存とコード生成は既存のJucerDocument保存経路に任せる。

### 3.5 任意コード実行を提供しない

AIに公開するのは、型と引数が検証されたドメイン操作だけとする。MCPツールやIPCコマンドに `execute_code`、`run_shell`、任意ファイル書き込みを追加しない。

## 4. 全体構成

```text
Codex / Claude
      |
      | MCP (stdio)
      v
projucerg-mcp
      |
      | JSONメッセージ / localhost限定IPC
      v
ProjucerG AutomationServer
      |
      v
LiveEditSessionManager
      |-----------------------------|
      v                             v
GuiDocumentAdapter             LiveEditOverlay
      |                             |
      v                             v
JucerDocument / ComponentLayout    GUI Editor上のプレビューと操作バー
      |
      v
UndoManager / 通常の保存・コード生成
```

### 4.1 ProjucerG内のModule

#### AutomationServer

外部MCPサーバーとの接続、JSONメッセージの受信、応答の送信を担当する。編集ロジックは持たない。

責務:

- localhost限定の待受
- 接続トークンの検証
- リクエストIDとレスポンスの対応付け
- メッセージサイズ上限
- JSON構文検証
- 接続切断時のセッション中断
- 受信した操作をJUCEメッセージスレッドへ配送
- ProjucerG終了・ユーザー中断・ドキュメント閉鎖などのイベントをMCPサーバーへ送信

#### LiveEditSessionManager

ライブ編集の状態遷移と中断を担当する。

状態:

```text
idle
  -> previewing
  -> applying
  -> paused
  -> completed
  -> cancelled
  -> failed
```

一度にアクティブにできる編集セッションは1つだけとする。

#### GuiDocumentAdapter

AI向けの安定したインターフェースと、既存GUI Editor実装の間を接続するAdapter。

初期インターフェース:

```cpp
struct GuiDocumentSnapshot;
struct SliderDraft;
struct ApplyResult;

class GuiDocumentAdapter
{
public:
    ResultOr<GuiDocumentSnapshot> snapshotActiveDocument() const;
    Result validate (const SliderDraft&) const;
    ResultOr<ApplyResult> addSlider (const SliderDraft&);
    Result undoCurrentAiTransaction();
    Image captureActiveEditor() const;
};
```

MCP、JSON、ソケットに関する型をこのインターフェースへ持ち込まない。

#### LiveEditOverlay

GUI Editor上に次を表示する。

- 半透明コンポーネントプレビュー
- 対象名と処理内容
- 「一時停止」「再開」「中断」「今回の編集を戻す」ボタン
- 接続状態
- エラーや競合状態

プレビューは既存コンポーネント配列へ追加せず、オーバーレイ自身が描画する。

### 4.2 外部MCPサーバー

`tools/projucerg-mcp/` に独立したMCPサーバーを置く。初期実装言語はPythonを第一候補とするが、ProjucerG内の編集ロジックはC++側に置く。

責務:

- MCPのstdioトランスポート
- MCPツールの宣言
- ProjucerG接続情報の発見
- IPCへのリクエスト変換
- タイムアウト、キャンセル、エラーのMCP応答への変換
- 画像結果のMCP形式への変換

## 5. IPC設計

### 5.1 トランスポート

初期版はJUCEの `InterprocessConnectionServer` を使用し、`127.0.0.1` のみにbindする。

Named Pipeも候補だが、MCPサーバー側にJUCE互換のメッセージフレーミング実装が必要になる。初期プロトタイプではlocalhost TCPを採用し、動作確立後にNamed PipeまたはUnix Domain Socketへの置換を評価する。

### 5.2 接続情報

ProjucerG起動時に次を含むランタイムファイルをユーザー専用一時ディレクトリへ作成する。

```json
{
  "pid": 12345,
  "port": 49152,
  "token": "起動ごとのランダムトークン",
  "protocolVersion": 1
}
```

- ファイルのアクセス権は現在のユーザーだけに限定する。
- ProjucerG終了時に削除する。
- 前回クラッシュ時の古いファイルはPIDと接続確認で判定する。
- TCPサーバーは固定ポートではなく空いている動的ポートを使用する。

### 5.3 メッセージ形式

リクエスト:

```json
{
  "protocolVersion": 1,
  "requestId": "req-001",
  "token": "...",
  "method": "previewSlider",
  "params": {},
  "expectedRevision": 12
}
```

成功レスポンス:

```json
{
  "requestId": "req-001",
  "ok": true,
  "revision": 12,
  "result": {}
}
```

失敗レスポンス:

```json
{
  "requestId": "req-001",
  "ok": false,
  "error": {
    "code": "revision_conflict",
    "message": "The GUI document changed after it was inspected."
  }
}
```

### 5.4 中断

中断は通常コマンドとは別の共有キャンセル状態として保持する。

- ProjucerGの「中断」ボタンとEscapeキーは即座にキャンセルフラグを立てる。
- 未実行の操作キューを破棄する。
- 適用処理は各ステップの前後でキャンセルを確認する。
- MCPサーバーからの `cancelSession` も同じ経路を使う。
- IPC切断時はセッションを `cancelled` にする。
- キャンセル後に部分適用が残った場合、「残す」「今回の編集を戻す」を選択可能にする。
- ProjucerGは中断理由を含む `sessionCancelled` イベントをMCPサーバーへ送る。
- MCPサーバーは待機中のツール呼び出しを中断結果で完了させ、AIが会話上で「ユーザーにより中断された」と伝えられる構造化結果を返す。
- AIは中断後に自動で操作を再試行しない。再開にはユーザーの新しい指示を必要とする。

中断イベント例:

```json
{
  "event": "sessionCancelled",
  "sessionId": "edit-123",
  "reason": "user_escape",
  "message": "The user stopped the live edit from ProjucerG."
}
```

初期版のSlider追加は1個のUndoableActionとして短時間で完了するため、適用途中の強制停止ではなく、プレビュー待機中の停止と適用直後の一括Undoを保証する。

### 5.5 ProjucerGの正常終了・強制終了

#### 正常終了

ProjucerGの終了処理では次を順番に行う。

1. 新規Automationリクエストの受付を停止する。
2. 実行中セッションを `cancelled` にし、理由 `application_quitting` を通知する。
3. プレビューとAI編集バーを閉じる。
4. IPC接続へ `applicationClosing` イベントを送る。
5. AutomationServerの待受と接続スレッドを停止する。
6. ランタイム接続情報ファイルを削除する。
7. 既存のProjucerG終了処理を継続する。

未保存変更の扱いは既存ProjucerGの確認ダイアログに従い、Automationが自動保存や自動破棄を行わない。

#### 強制終了・クラッシュ

強制終了時はクリーンアップ処理が実行されない前提とする。

- MCPサーバーはIPC切断またはハートビート停止を検知し、待機中のツール呼び出しを `application_disconnected` で失敗させる。
- AIへ「ProjucerGとの接続が失われた。操作の完了状態は不明」と返し、成功扱いにしない。
- ランタイム接続情報ファイルが残っていても、記録されたPIDの生存確認と接続確認に失敗したらstaleとして扱う。
- 次回ProjucerG起動時に、自分が所有していない古いランタイムファイルを安全に除去する。
- 最後の操作が適用されたか不明な場合、再接続後に対象 `.jucer` とGUIドキュメントをユーザーへ再確認し、状態を再取得する。
- 強制終了からの復旧時も、AIが自動保存・自動再実行しない。

MCPサーバー自身が強制終了した場合、ProjucerGは接続切断を検出してセッションを中断し、通常の手動編集状態へ戻る。

## 6. Slider縦切りの実装

### 6.1 状態取得

AIは状態取得の前にユーザーから対象 `.jucer` とGUIドキュメントの指定を得る。ProjucerG側では、指定されたProjectと現在開いているProjectの正規化済みパスを比較し、一致を確認する。その後、対象GUIドキュメントを表示し、既存の `JucerDocumentEditor::getActiveDocumentHolder()` から取得する。

対象が曖昧な場合や指定とアクティブProjectが一致しない場合、`target_confirmation_required` を返し、状態取得や編集を続けない。

スナップショットに含める情報:

- ドキュメントIDとファイルパス
- ドキュメントのリビジョン
- Componentの初期幅・高さ
- GUI Editorの表示領域とズーム
- スナップグリッドサイズと有効状態
- 既存コンポーネントのID、型、名前、メンバー名、bounds
- 現在の選択
- AI編集セッション状態

### 6.2 自然言語から位置への変換

自然言語の解釈はAI側で行い、ProjucerGには構造化された配置指定を渡す。

```json
{
  "anchor": "componentCenter",
  "offset": { "x": -80, "y": 70 },
  "size": { "width": 120, "height": 120 },
  "snapToGrid": true
}
```

ProjucerG側でアンカー、オフセット、サイズから最終boundsを計算する。これにより、AIがキャンバス座標系やスクロール量を理解する必要がなくなる。

配置検証:

- キャンバス内に収まること
- 最小サイズを満たすこと
- 数値が有限であること
- グリッド指定時は既存のスナップ規則を利用すること
- 既存コンポーネントとの重なりを警告として返すこと

重なりは初期版ではエラーにせず、警告とプレビューでユーザーに見せる。

### 6.3 プレビュー

`previewSlider` は `SliderDraft` を検証し、`LiveEditOverlay` に次を表示する。

- 実際の `Slider` に近い半透明描画
- boundsの枠
- `Lowpass Filter` / `20–20000 Hz` の説明
- 衝突やキャンバス外の警告

プレビュー中はドキュメントのchangedフラグやUndo履歴を変更しない。

### 6.4 確定

確定時は完成形のSlider XMLを構築し、`ComponentLayout::addComponentFromXml (xml, true)` で追加する。

XMLには少なくとも次を設定する。

```xml
<SLIDER
  name="lowpass filter"
  memberName="lowpassFilterSlider"
  min="20.0"
  max="20000.0"
  int="0.0"
  style="RotaryHorizontalVerticalDrag"
  textBoxPos="NoTextBox"
  textBoxEditable="0"
/>
```

実際には既存の `SliderHandler::createXmlFor()` と共通の属性一式を生成し、ID、位置、相対位置、LookAndFeel、コールバック設定などを欠落させない。

追加後:

- 新しいSliderを選択状態にする。
- GUI EditorのLayoutタブを表示する。
- プロパティパネルを更新する。
- オーバーレイを完了状態へ変更する。
- 新しいドキュメントリビジョンとコンポーネントIDを返す。
- 自動保存は行わない。

### 6.5 Undo

確定直前に次のトランザクションを開始する。

```cpp
document.getUndoManager().beginNewTransaction (
    "AI Edit - Add Lowpass Filter Slider");
```

Sliderの完成形を一度の `AddCompAction` で追加し、1回のUndoで削除できるようにする。

後続版で既存コンポーネントの複数プロパティを段階的に変更する場合も、セッション単位で同一トランザクションにまとめる。ただしユーザーが途中で手動操作した場合はAIトランザクションを閉じ、ユーザー操作と混ぜない。

## 7. MCPツール

初期版で公開するツール:

### `list_open_projects`

ProjucerGで開いている `.jucer` ProjectとGUIドキュメントの候補を返す。この結果はユーザーへ選択肢を提示するために使い、AIが対象を暗黙に決定するためには使わない。

### `select_edit_target`

ユーザーが指定した `.jucer` パスとGUIドキュメントをライブ編集対象として検証・選択する。以後の変更系ツールは対象IDを必須引数とする。

### `get_active_gui_document`

現在のGUIドキュメント、キャンバス、既存コンポーネント、選択状態を返す。

### `capture_gui_editor`

GUI Editor部分をPNGとして返す。AIは操作前後にこのツールを使って視覚的に確認する。

### `begin_live_edit`

ライブ編集セッションを開始する。ユーザーのProjucerGにAI編集バーを表示する。

### `preview_slider`

SliderDraftを半透明表示する。ドキュメントは変更しない。

### `apply_slider`

表示中のSliderDraftを確定し、GUI Editorへ追加する。

### `pause_live_edit` / `resume_live_edit`

AI側からセッションを一時停止・再開する。ユーザー側のボタン操作も同じ状態機械を利用する。

### `cancel_live_edit`

未実行操作とプレビューを破棄する。適用済み変更を戻すかどうかを引数で指定できるが、デフォルトはユーザーに判断を委ねる。

### `get_live_edit_status`

セッション状態、ユーザーによる中断、競合、警告を返す。

ツール説明には、対象 `.jucer` のユーザー確認、変更前の状態取得、プレビュー、確定後の画像確認を必須手順として記載する。また、中断結果を受け取ったAIはユーザーへ中断を明示し、自動再試行してはならないことを記載する。

## 8. UI設計

### 8.1 AI編集バー

GUI Editor上部または下部に、編集中だけ表示する。

```text
+--------------------------------------------------------------+
| AI編集中: Lowpass Filter Sliderを追加                         |
| [一時停止] [中断] [今回の編集を戻す]              接続中 ●   |
+--------------------------------------------------------------+
```

要件:

- 「中断」は最も目立つ位置と色にする。
- Escapeキーでも同じ処理を実行する。
- 一時停止中は「再開」に切り替える。
- AIの現在の処理内容を短い日本語で表示する。
- MCP切断時は即座に表示を更新する。
- 完了後は数秒間結果を表示し、ユーザーが閉じられるようにする。

### 8.2 プレビューオーバーレイ

- 既存のComponent Overlayより手前に描画する。
- マウスイベントを奪わない。
- 半透明と破線枠で未確定であることを示す。
- 確定時に短いフェードを行って実コンポーネントへ切り替える。
- アニメーション中も中断を受け付ける。

## 9. 競合制御

P0初期版では安全性を優先し、ライブ編集セッション中の通常ドキュメント編集を排他にしてよい。ただし、中断、一時停止、Undo、アプリ終了は排他対象に含めない。

同時編集を有効にする場合は、以下のリビジョン方式を使用する。

各GUIドキュメントにAutomation用の単調増加リビジョンを持たせる。

リビジョンを増やす契機:

- コンポーネント追加・削除
- bounds変更
- 編集可能プロパティ変更
- Undo / Redo
- ドキュメント再読み込み
- AI以外のユーザー操作

AIの変更リクエストには `expectedRevision` を必須とする。一致しない場合は変更せず `revision_conflict` を返す。

プレビュー中にユーザー操作でリビジョンが変わった場合:

1. プレビューを一時停止する。
2. AI編集バーに「ユーザー変更を検出」と表示する。
3. AIへ再取得を要求する。
4. AIが新しい状態から配置を再計算するまで確定を禁止する。

## 10. セキュリティと障害対策

- localhost以外にbindしない。
- 起動ごとの十分に長いランダムトークンを要求する。
- 接続情報ファイルをユーザー専用権限で作成する。
- JSONメッセージと画像の最大サイズを制限する。
- 文字列長、数値範囲、enum値をC++側で再検証する。
- ファイルパスを返す場合は必要最小限にする。
- 任意コード実行を実装しない。
- ProjucerG終了時とドキュメント切替時にセッションを安全に終了する。
- 正常終了時に終了イベントを送り、ランタイム接続情報を削除する。
- 強制終了後に残ったランタイム接続情報をPIDと接続確認でstale判定する。
- MCPサーバーがクラッシュしてもProjucerGは動作を継続する。
- AutomationServerの例外や不正メッセージでJUCEメッセージスレッドを停止させない。
- ログに接続トークンやユーザーのソースコード本文を記録しない。

## 11. 実装フェーズ

### Phase 0: 実装前の技術スパイク

目的: 既存経路を変更せず、Slider縦切りの成立条件を確認する。

- テスト用コードからアクティブな `JucerDocumentEditor` を取得する。
- `SliderHandler`相当の完成形XMLを構築する。
- `ComponentLayout::addComponentFromXml (xml, true)` でSliderを追加する。
- Sliderが画面、プロパティパネル、保存XML、生成コードへ正しく反映されることを確認する。
- Undo 1回で完全に消えることを確認する。
- Component中央基準のbounds計算を確認する。
- 対象 `.jucer` とGUIドキュメントを明示指定する内部テスト経路を用意する。

完了条件:

- MCPなしの内部テスト操作で、目標のLowpass Sliderを正しく追加・Undoできる。

### Phase 1: Automationの内部インターフェース

- `Source/Automation/` を追加する。
- `GuiDocumentAdapter` とDTOを実装する。
- アクティブドキュメントのスナップショットを取得する。
- SliderDraftの検証とbounds計算を実装する。
- Slider追加処理を既存Undo経路へ接続する。
- JSONやIPCに依存しない単体テストを追加する。

完了条件:

- C++の小さなインターフェースだけで状態取得、Slider追加、Undoをテストできる。

### Phase 2: ライブ編集セッションとUI

- `LiveEditSessionManager` の状態機械を実装する。
- `LiveEditOverlay` とAI編集バーを実装する。
- Sliderプレビューを実装する。
- 一時停止、再開、中断、今回の編集を戻すを実装する。
- Escapeキーの緊急停止を追加する。
- ユーザー中断理由をセッション結果として保持する。
- P0初期版では必要に応じて通常ドキュメント編集を排他にし、停止系操作だけは常時受け付ける。
- ドキュメント切替・閉じる・終了時のクリーンアップを実装する。

完了条件:

- ProjucerG内部のテスト操作で、プレビューから確定までが目に見え、いつでも中断できる。

### Phase 3: IPC

- `AutomationServer` をJUCEのIPC機能で実装する。
- ランタイム接続情報ファイルとトークンを実装する。
- リクエスト／レスポンス、エラー、タイムアウトを実装する。
- 受信操作をメッセージスレッドへ安全に配送する。
- 切断時キャンセルを実装する。
- ユーザー中断、正常終了、ドキュメント閉鎖のイベント通知を実装する。
- ハートビートと強制終了検知を実装する。
- staleなランタイム接続情報の検出・除去を実装する。
- プロトコルバージョンを固定する。

完了条件:

- ローカルのテストクライアントから、状態取得、プレビュー、確定、中断ができる。

### Phase 4: MCPサーバー

- `tools/projucerg-mcp/` を追加する。
- MCP stdioサーバーを実装する。
- 初期ツールを公開する。
- ProjucerG接続の自動検出を実装する。
- 編集前の `.jucer` / GUIドキュメント選択ツールを実装する。
- 中断・切断イベントを待機中ツール呼び出しとAI向け結果へ伝搬する。
- GUI Editor画像をMCP画像結果として返す。
- CodexとClaude向け設定例を追加する。
- MCP Inspectorによる結合試験手順を追加する。

完了条件:

- Codex／Claudeから自然言語の指示で、起動中のProjucerGへSliderをプレビュー・追加できる。

### Phase 5: 競合制御と品質向上

- ドキュメントリビジョンを実装する。
- 排他編集から安全な同時編集へ進める場合、ユーザー操作との競合検出を追加する。
- 適用前後の構造化検証を追加する。
- 操作ログと診断表示を追加する。
- 不正入力、切断、タイムアウト、連打の試験を追加する。
- UIアニメーションと表示文言を調整する。

完了条件:

- 排他モードでは停止系操作が常に効き、同時編集モードではユーザー操作を上書きせずクラッシュしない。

### Phase 6: コンポーネント対応の拡張

Slider縦切りの設計を共通化し、次の順に追加する。

1. Label
2. TextButton / ToggleButton
3. TextEditor
4. ComboBox
5. ImageComponent / DrawableButton
6. 複数コンポーネントの整列・間隔調整・グループ操作

各型について、読み取り、プレビュー、新規追加、既存要素の更新、Undo、画像確認を同じ基準で実装する。

### Phase 7: P1 Windows / P2 Linux対応

P0 macOSの手動受け入れテスト完了後に進める。

- P1 Windows: localhost IPC、ランタイムファイル権限、PID生存確認、正常終了・強制終了復旧を移植して検証する。
- P2 Linux: 同じ機能を移植し、主要Desktop EnvironmentでGUI表示、Escape中断、画像取得を検証する。
- MCPのツール名、引数、イベント、エラーコードは全OSで共通に保つ。

## 12. 予定ファイル構成

```text
Projucer/Source/Automation/
  jucer_AutomationTypes.h
  jucer_AutomationServer.h
  jucer_AutomationServer.cpp
  jucer_GuiDocumentAdapter.h
  jucer_GuiDocumentAdapter.cpp
  jucer_LiveEditSession.h
  jucer_LiveEditSession.cpp
  jucer_LiveEditOverlay.h
  jucer_LiveEditOverlay.cpp
  jucer_AutomationProtocol.h
  jucer_AutomationProtocol.cpp

tools/projucerg-mcp/
  pyproject.toml
  src/projucerg_mcp/server.py
  src/projucerg_mcp/client.py
  tests/
  README.md

docs/
  live-ai-editing.md
  codex-mcp-setup.md
  claude-mcp-setup.md
```

実際の追加前に、ProjucerのUnity Build／インクルード構成と `.jucer` のファイルグループ規則を確認し、既存の配置規則へ合わせる。

## 13. テスト計画

### 13.1 C++単体テスト

- アンカーとオフセットからboundsを計算できる。
- グリッドへ正しくスナップする。
- 無効な範囲、NaN、未知のSliderStyleを拒否する。
- SliderDraftから正しいXMLを生成する。
- プレビューではドキュメントが変更されない。
- リビジョン不一致時に適用されない。
- 不正トークンと不正JSONを拒否する。

### 13.2 ProjucerG統合テスト

- SliderがGUI Editorに即時表示される。
- Sliderが選択され、プロパティパネルが更新される。
- `min=20`、`max=20000`が保持される。
- `RotaryHorizontalVerticalDrag`が保持される。
- `NoTextBox`が保持される。
- 保存後に再度開いても同じ状態になる。
- 生成コードに正しい `setRange`、`setSliderStyle`、`setTextBoxStyle` が出力される。
- Undo 1回でSliderが消える。
- Redoで同じSliderが戻る。

### 13.3 ライブ操作テスト

- プレビュー中にEscapeで即時停止できる。
- Escape中断がMCPサーバーとAIへ伝わり、AI向け結果が `user_escape` になる。
- 中断後、AIが自動的に操作を再試行しない。
- 一時停止中にAIから確定要求が来ても実行されない。
- 排他モード中も中断、一時停止、Undo、アプリ終了を操作できる。
- ユーザー操作後の古いリビジョンによる確定を拒否する。
- MCP切断時にプレビューが残留しない。
- ドキュメントを閉じてもクラッシュしない。
- ProjucerG終了時に待受スレッドが正常終了する。
- 正常終了がAIへ `applicationClosing` として伝わる。
- ProjucerGを強制終了するとMCPサーバーが切断を検知し、成功ではなく完了状態不明として返す。
- 強制終了後のstaleなランタイム接続情報を次回起動時に安全に処理できる。
- AI編集バーのボタンを連打しても状態が壊れない。

### 13.4 手動受け入れテスト

ユーザーが実際にProjucerGを起動し、CodexまたはClaudeへ目標文を入力して確認する。

受け入れ条件:

1. AIが編集前に対象 `.jucer` とGUIドキュメントをユーザーへ確認する。
2. 中央より少し左下に半透明プレビューが見える。
3. ユーザーが中断した場合、Sliderは追加されず、AIが「中断された」と伝える。
4. 確定した場合、Sliderがその場に表示される。
5. プロパティパネルで20〜20000、ロータリー、テキストボックスなしを確認できる。
6. 排他モードの場合はAI編集中の通常編集が無効でもよいが、停止操作は常に実行できる。
7. セッション完了後、ユーザーが手動でSliderを移動・編集できる。
8. Undo 1回でAIの追加を戻せる。
9. AIは変更後の画面を取得し、配置結果を説明できる。
10. ProjucerGの正常終了・強制終了のどちらでも、AIが操作成功と誤認しない。

## 14. 実装時に先に解決する論点

### 14.1 プレビューをどの親Componentへ載せるか

`ComponentLayoutPanel`、編集キャンバス、既存の `ComponentOverlayComponent` の階層を確認し、ズームとスクロールに正しく追従する最小の配置先を選ぶ。

### 14.2 画像取得範囲

GUI Editorのキャンバスだけを取得するか、プロパティパネルを含む編集領域全体を取得するかを決める。初期版は配置確認に必要なキャンバス画像を優先し、構造化プロパティはJSONで返す。

### 14.3 リビジョン通知点

既存の `JucerDocument::changed()` だけで全変更を捕捉できるか確認する。位置だけの変更をまとめる既存最適化を壊さず、Automation向けリビジョンだけを確実に増加させる。

### 14.4 一時停止中のユーザー編集

初期版では許可する。ただしユーザー変更を検出した時点でプレビューをstale扱いとし、AIの確定には新しいスナップショット取得を必須とする。

## 15. 実装開始時の最初の作業単位

最初の変更はMCPやIPCを含めず、次の小さな縦切りにする。

1. デバッグ用コマンドへ対象 `.jucer` とGUIドキュメントを明示して取得する。
2. 指定対象と現在開いているProjectが一致しない場合に拒否する。
3. キャンバス中央から `(-80, +70)` の位置に、120×120のSliderを追加する。
4. 完成形XMLに `20`、`20000`、`RotaryHorizontalVerticalDrag`、`NoTextBox`を設定する。
5. Sliderを選択して画面上に表示する。
6. Undo 1回で消えることを確認する。

この縦切りをユーザーが手動確認した後に、プレビュー、中断、IPC、MCPの順で外側へ広げる。

## 16. 完了定義

本機能の初期版は、次のすべてを満たした時点で完了とする。

- CodexとClaudeの両方から起動中のProjucerGへ接続できる。
- P0 macOSで一連の機能と終了・強制終了時の処理が動作する。
- AIが対象 `.jucer` とGUIドキュメントをユーザーへ確認してから編集する。
- 自然言語から構造化されたSliderDraftを作成できる。
- GUI Editor上で確定前のプレビューを確認できる。
- ユーザーが排他状態を含むどの時点でも一時停止・中断・Undoできる。
- ユーザー中断をAIが認知し、会話上で明示できる。
- 確定したSliderが通常のGUI Editorコンポーネントとして振る舞う。
- 保存・再読込・生成コードに設定が保持される。
- 同時編集を有効にした場合、ユーザー操作との競合時に上書きしない。
- ProjucerGの正常終了・強制終了をAIが検知し、未確認操作を成功扱いしない。
- 任意コード実行や外部ネットワーク公開を必要としない。
- 手動テスト手順をユーザーが完了し、期待どおり動作することを確認している。

コミットおよびプッシュは、ユーザーによる手動テストと動作確認が完了し、ユーザーから明示的に依頼された後にのみ行う。
