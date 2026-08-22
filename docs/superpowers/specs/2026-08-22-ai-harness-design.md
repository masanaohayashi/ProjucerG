# AI ハーネス 設計文書

作成日: 2026-08-22

対象: Projucer に AI コーディングハーネスを内蔵し、iPad 単体で Vibe Coding
を成立させる。VS Code の Cline 拡張に相当する機能を、外部プロセスに頼らず
アプリ内で実現する。

## 1. 目的

iPad 上の Projucer で、ChatGPT アカウントにサインインし、開いている
プロジェクトについて自然言語で指示すると、AI がソースを読み、差分を提示し、
ユーザーの承認を経てファイルへ反映する。

Mac は関与しない。iPad とインターネット接続だけで完結する。

成功条件: iPad 実機で、サインインからファイル反映までが通ること。
チャットが表示されただけ、差分が出ただけでは成功ではない。

### CX 要件（必須）

1. サインインはアプリ内で完結する。既定はブラウザ OAuth（アプリ内に認可ページを提示）。
   デバイスコード認証は代替経路として選べるが、ChatGPT 側の設定が要ることを明示する。
2. AI の応答はストリーミング表示する。完了まで無言で待たせない。
3. ファイルを書き換える前に必ず差分を見せ、承認を取る。
4. 実行中はいつでも停止できる。UI は固まらない。
5. AI が書き換えたファイルを開いているエディタは、実ファイルに追従する。
   古い内容を保存して AI の変更を失うことがない。

## 2. 対象と非対象

**対象（M1 の「動く」）**

- ChatGPT サブスクによるサインイン（デバイスコードフロー）
- トークンの Keychain 保管と自動更新
- Codex バックエンドへの SSE ストリーミングリクエスト
- エージェントループ（ツール呼び出しの往復）
- ツール: `list_files` / `read_file` / `apply_patch` / `write_file`
- 差分承認 UI
- チャット UI（メインエリア切り替え）
- macOS / iOS 双方で同一コード

**対象外（M1 以降）**

- Grok 対応（Grok CLI がオープンソースなので同様の手順で追加可能）
- Claude 対応
- ビルド実行ツール（`build_project`）
- GUI Editor 操作ツール（`PROJUCERG_LIVE_AI_EDIT_IMPLEMENTATION_PLAN.md` のツール設計を M2 で取り込む）
- 会話履歴の永続化（アプリ再起動で消えてよい）
- 複数スレッドの管理・切り替え
- MCP サーバー接続
- シェル実行（iOS では原理的に不可）

## 3. 前提と根拠

### 3.1 iOS の制約

iOS は `fork` / `exec` / `posix_spawn` をサンドボックスで禁止している。
したがって `codex` バイナリを子プロセスとして起動する方式（`codex app-server`
による JSON-RPC 連携を含む）は iOS で成立しない。既存の `Source/Terminal/`
の PTY 実装も同じ理由で iOS では動かない。

Codex のバックエンドは通常の HTTPS + SSE であるため、Projucer が C++ から
直接通信すればこの制約を回避できる。

macOS の最小対応バージョンは 10.15 とする。パス検証で使用する
`std::filesystem` を macOS 10.13 の標準ライブラリではリンクできないためである。

### 3.2 ライセンスと利用規約

Codex CLI は Apache-2.0。本設計はそのソースから公開された定数と
プロトコル仕様を参照する。実装コードの複製は行わない。

OpenAI は "Codex for Open Source" として、Cline / OpenCode / pi 等の
サードパーティハーネスから ChatGPT サブスクを使うことを公式に支持している。
API 課金ではなくサブスクで動作させることが本機能の要件である。

### 3.3 Responses API のツール定義

Responses API ではツール定義をクライアントが送る。したがって Codex の
シェル実行ツールを再現する必要はなく、Projucer 固有のツールを定義して
渡せばよい。iOS にシェルが無いことは設計上の制約にならない。

## 4. 全体構造

```
  AiChatView          UI。チャット表示・入力・差分承認
      │
  AiSession           会話履歴とターン状態。UI より長生きする
      │
  AgentLoop           送信 → SSE 受信 → ツール実行 → 再送
      │        ╲
  CodexClient        AiTools
   (HTTP/SSE)      (list/read/patch/write)
      │
  CodexAuth          デバイスコード認証・トークン更新・Keychain
```

依存は一方向。下位は上位を知らない。

**`AiSession` の所有者は `ProjectContentComponent`。**
`ContentViewComponent::setContent()` は保持中の `unique_ptr` を差し替えて
中身を破棄するため、`AiChatView` をそこに載せると会話が消える。View は
使い捨てとし、状態は `AiSession` に置く。AI タブを離れて戻っても会話が続く。

**`CodexClient` が唯一の Codex 依存箇所。**
将来 Grok / Claude を足す際はここだけ差し替える。ただし M1 では
インターフェースを切らない。実装が 1 つしかない段階で抽象を作ると、
2 つ目が来たときに実態と合わない境界が残る。M1 で守るのは
「Codex 固有の型を `AgentLoop` より上に漏らさない」ことだけとする。

### 4.1 ファイル構成

`Projucer/Source/AI/` を新設する。

| ファイル | 役割 |
|---|---|
| `jucer_CodexAuth.h` / `.cpp` | 二経路のサインイン、トークン保管・更新 |
| `jucer_Pkce.h` / `.cpp` | PKCE と state の生成（ブラウザ OAuth 用） |
| `jucer_OAuthCallbackServer.h` / `.cpp` | ループバックの HTTP 待ち受け。`/auth/callback` の code と state を拾う |
| `jucer_AuthBrowser.h` / `.mm` | 認可ページの提示。iOS は `ASWebAuthenticationSession`、macOS は通常のブラウザ |
| `jucer_Keychain.h` / `.mm` | Security.framework の薄いラッパー |
| `jucer_CodexClient.h` / `.cpp` | `/responses` への SSE リクエスト |
| `jucer_SseParser.h` / `.cpp` | SSE 行パーサ |
| `jucer_AgentLoop.h` / `.cpp` | エージェントループ |
| `jucer_AiTools.h` / `.cpp` | ツール実装とスキーマ、パス検証 |
| `jucer_AiSession.h` / `.cpp` | 会話状態 |
| `jucer_AiChatView.h` / `.cpp` | UI |

### 4.2 スレッド方針

HTTP / SSE は専用スレッドで行う。UI 更新は `MessageManager::callAsync` で
メッセージスレッドへ戻す。メッセージスレッドをネットワーク待ちで
ブロックしない。既存のオンデバイスビルドと同じ方針。

`AgentLoop` は自身の停止要求を `std::atomic<bool>` で持ち、SSE 読み取り
ループの各行境界で確認する。

## 5. 認証

### 5.1 定数

| 名前 | 値 | 出典 |
|---|---|---|
| `CLIENT_ID` | `app_EMoamEEZ73f0CkXaXp7hrann` | `codex-rs/login/src/auth/manager.rs:1678` |
| issuer | `https://auth.openai.com` | 同上 |
| リフレッシュ | `https://auth.openai.com/oauth/token` | `manager.rs:194` |
| API ベース | `https://chatgpt.com/backend-api/codex` | `codex-rs/model-provider-info/src/lib.rs:39` |

### 5.2 二つのサインイン経路

Codex は二つのサインイン方式を持ち、本設計は**両方を実装する**。

| 経路 | 既定 | 前提 | 使いどころ |
|---|---|---|---|
| ブラウザ OAuth（5.2.1） | ○ | なし | 通常はこちら |
| デバイスコード（5.2.2） | | **ChatGPT 側でアカウントごとに有効化が必要** | ローカル待ち受けが使えない場合の代替 |

デバイスコード認証は ChatGPT のセキュリティ設定で既定オフであり、未設定のアカウントでは
`device code login is not enabled for this Codex server` に相当する応答が返る。
全ユーザーに設定変更を要求するのは受け入れ難いため、既定はブラウザ OAuth とする。

### 5.2.1 ブラウザ OAuth（既定）

PKCE のペアは**クライアントが生成する**（デバイスコード経路と異なる点）。

1. PKCE を作る。64 バイトの乱数を URL-safe base64（パディング無し）で符号化して
   `code_verifier`、その SHA-256 を同じ符号化にしたものが `code_challenge`。
   方式は S256。あわせて 32 バイト乱数から `state` を作る。
2. ループバックの HTTP 待ち受けを開始する。既定ポートは 1455、埋まっていれば別のポート。
   `redirect_uri` は `http://localhost:{実際のポート}/auth/callback`。
3. 次の URL を開く。

   `{issuer}/oauth/authorize?` に以下をクエリとして与える。

   | キー | 値 |
   |---|---|
   | `response_type` | `code` |
   | `client_id` | `{CLIENT_ID}` |
   | `redirect_uri` | 上記 |
   | `scope` | `openid profile email offline_access api.connectors.read api.connectors.invoke` |
   | `code_challenge` | 上記 |
   | `code_challenge_method` | `S256` |
   | `id_token_add_organizations` | `true` |
   | `codex_cli_simplified_flow` | `true` |
   | `state` | 上記 |
   | `originator` | `codex_cli_rs` |

4. ブラウザが `/auth/callback?code=…&state=…` へ戻る。**`state` が送出時と一致しない
   要求は破棄する**（CSRF 対策。一致しない場合はサインインを失敗として終える）。
5. `POST {issuer}/oauth/token` へ
   `grant_type=authorization_code&code=…&redirect_uri=…&client_id=…&code_verifier=…`
   を `application/x-www-form-urlencoded` で送る。
6. 待ち受けを閉じる。ブラウザには成功した旨の簡単なページを返す。

**iOS での注意（重要）**: 外部の Safari へ切り替えるとアプリはバックグラウンドへ回り、
待ち受けが停止してコールバックを取りこぼす。したがって認可ページは
`ASWebAuthenticationSession` でアプリ内に提示し、アプリを前面に保ったまま
ループバックへ戻す。macOS では通常のブラウザ起動でよい。

### 5.2.2 デバイスコード（代替）

1. `POST {issuer}/api/accounts/deviceauth/usercode`
   - body: `{"client_id": "app_EMoamEEZ73f0CkXaXp7hrann"}`
   - 応答: `{device_auth_id, user_code, interval}`
   - `interval` は文字列で返る場合があるため数値へ変換する
   - 404 は「このサーバーでデバイスコードログインが有効でない」を意味する

2. UI に `user_code` と検証 URL `{issuer}/codex/device` を表示する。
   コードは選択・コピー可能にし、Safari を開くボタンを添える
   (`URL::launchInDefaultBrowser`)。

3. `POST {issuer}/api/accounts/deviceauth/token` を `interval` 秒ごとに
   ポーリングする。上限 15 分。
   - body: `{device_auth_id, user_code}`
   - 成功時: `{authorization_code, code_challenge, code_verifier}`

4. `POST {issuer}/oauth/token`
   - `grant_type=authorization_code`
   - `code={authorization_code}`
   - `redirect_uri={issuer}/deviceauth/callback`
   - `client_id={CLIENT_ID}`
   - `code_verifier={code_verifier}`
   - 応答: `access_token` / `refresh_token` / `id_token`

5. `account_id` は `id_token` の JWT ペイロードから取り出す。

**この経路に限り PKCE のペアはサーバーが生成して返す**（`device_code_auth.rs:54-59`）。
ブラウザのコールバックが不要なため、ループバック待ち受けも要らない。

この経路は ChatGPT のセキュリティ設定でデバイスコード認証を有効にしたアカウントでのみ
使える。有効化されていない場合はその旨を UI に表示し、ブラウザ OAuth を案内する。

### 5.3 保管

Keychain (`kSecClassGenericPassword`) に保存する。service は
`com.projucer.ai.codex`、account は `tokens`。値は JSON。
アクセス属性は `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`。

平文ファイルには置かない。ログにもトークンを出さない。

### 5.4 更新

`access_token` は数時間で失効する。API が 401 を返したら
`grant_type=refresh_token` で `oauth/token` を叩き直し、
元のリクエストを **1 回だけ** 再試行する。二度目の 401 は
再サインインを促して終わる（無限ループを作らない）。

### 5.5 キャンセル

ポーリング中も UI は応答し、いつでも中止できる。中止したら
`device_auth_id` を破棄して最初からやり直す。

## 6. API 呼び出し

### 6.1 リクエスト

`POST https://chatgpt.com/backend-api/codex/responses`

ヘッダ:

| 名前 | 値 |
|---|---|
| `Authorization` | `Bearer {access_token}` |
| `chatgpt-account-id` | `{account_id}` |
| `session_id` | セッション毎の UUID |
| `originator` | `codex_cli_rs` |
| `OpenAI-Beta` | `responses=experimental` |
| `Content-Type` | `application/json` |
| `Accept` | `text/event-stream` |

ボディは Responses API 形式。`stream: true`、`store: false`。

`store: false` のため会話履歴はクライアントが保持し、毎回全部送る。
サーバー側の状態に依存しないので実装が単純になり、将来の差し替えも容易。

### 6.2 SSE 受信

`WebInputStream` で読み、`SseParser` が行単位に切って `data:` を JSON として
取り出す。扱うイベント:

| イベント | 動作 |
|---|---|
| テキスト delta | `AiSession` へ追記し UI に流す |
| function_call の引数 delta | 引数バッファへ蓄積 |
| completed | ターンの区切り |
| error | エラーを UI に出してターン終了 |

未知のイベント種別は黙って無視する（サーバー側の追加で壊れないように）。

## 7. エージェントループ

```
input を積む
  ↓
POST → SSE を読む
  ├ テキスト        → ストリーミング表示
  ├ function_call   → 引数を蓄積
  └ completed
       ├ ツール呼び出しなし → ターン終了
       └ あり
            ├ 承認が要るツール → ユーザーへ確認
            │     ├ 却下 → 却下理由を結果として積む
            │     └ 承認 → 実行
            ├ 承認不要 → そのまま実行
            └ 結果を input に積んで再 POST
```

ツール実行の結果は成功・失敗を問わず必ずモデルへ返す。失敗を隠すと
モデルが同じ失敗を繰り返す。

反復上限を設ける（既定 25 往復）。超えたら停止してユーザーに知らせる。

## 8. ツール

### 8.1 一覧（M1）

| ツール | 引数 | 動作 | 承認 |
|---|---|---|---|
| `list_files` | `path`（省略可） | プロジェクト配下の一覧 | 不要 |
| `read_file` | `path`, `start_line`（省略可）, `end_line`（省略可） | ファイル読み | 不要 |
| `apply_patch` | `path`, `old_text`, `new_text` | 一意な文字列置換 | 必要 |
| `write_file` | `path`, `content` | 新規作成・全置換 | 必要 |

### 8.2 パス検証（trust boundary）

すべてのツールは以下を必ず通す。ここは簡略化しない。

1. 引数のパスをプロジェクトルート基準で解決する。
2. シンボリックリンクを解決した実パスを求める。
3. 実パスがプロジェクトルート配下にあることを確認する。外なら拒否。
4. 拒否した場合はエラーをモデルへ返す（黙って握りつぶさない）。

`..` による脱出と、ルート内に置かれたシンボリックリンク経由の脱出の
両方を塞ぐ。ルート自身のパスも実パスへ解決してから比較する。

読み取りサイズにも上限を設ける（既定 1 MB）。超える場合は行範囲指定を
促すエラーを返す。

### 8.3 承認

書き込み系はデフォルトで毎回承認。差分を色付きで表示し「適用 / 却下」を
選ばせる。「以後このセッションでは自動承認」トグルを用意するが、
**既定はオフ**。トグルはセッション終了で失効する。

### 8.4 適用と、開いているドキュメントの整合

ファイル変更はファイルへ直接書く。書き込み後、`OpenDocumentManager` の
`reloadModifiedFiles()` を呼んで、開いているエディタを実ファイルに合わせる。

**これは体裁の問題ではなくデータ損失の防止である。** エディタが古い内容を
保持したままユーザーが保存すると、AI の変更が黙って消える。書き込みのたびに
必ずリロードする。

Undo については保証しない。閉じたファイルへの変更はエディタの Undo 履歴に
乗らないため、Cmd-Z では戻らない。この点があるからこそ、書き込み系は
適用前の差分確認を必須とする（8.3）。差分を見せずに書き換える経路は作らない。

## 9. UI

### 9.1 配置

ヘッダの右端に AI トグル用の `IconButton` を追加する。ヘッダにタブ機構は
存在せず（`tabsWidth` はサイドバー幅に合わせた領域確保用であってタブではない）、
既存のボタンは `projectSettingsButton` と `saveAndOpenInIDEButton` の 2 つの
`IconButton` である。同じパターンで 1 つ足す。

押すと `ContentViewComponent` の中身が `AiChatView` に差し替わる。もう一度
押すと直前に開いていたドキュメントのビューへ戻る。戻し先は
`ProjectContentComponent` が保持している現在のドキュメントから再構築する。

左サイドバー（`Sidebar` の concertina）には置かない。幅が
`r.getWidth() / 4` しかなく、File / Modules / Exporters と高さを分け合うため、
キーボード表示時にチャットと差分を収められない。

下部パネル（ターミナルの位置）にも置かない。キーボードが出る場所そのもので、
高さが削られると最初に潰れる。

### 9.2 画面構成（上から）

1. 未サインイン時: サインインカード（コード表示、Safari を開くボタン、中止）
2. チャット履歴（ユーザー発言 / AI 応答 / ツール実行ログ。ログは折りたたみ）
3. 承認待ち時: 差分カード（適用 / 却下）
4. 入力欄（下端固定、送信ボタン、実行中は停止ボタン）

### 9.3 キーボード対応

既存の inset 機構をそのまま使う。`MainWindow::getContentComponentBorder()`
が `display->keyboardInsets` の分だけコンテンツ領域を縮め、
`parentSizeChanged()` が `resized()` を呼び直す
(`jucer_MainWindow.cpp:628-693`)。

これによりコンテンツ全体の高さが縮み、下端固定の入力欄がキーボード直上に来る。
`AiChatView` 側で特別な処理は要らない。

## 10. ビルド統合

新規ソースを `Projucer.jucer` に追加し resave する。

`jucer_Keychain.mm` は Objective-C++ のため、Apple 以外のプラットフォームでは
コンパイル対象から外す。Security.framework をリンクする。

以下の全構成でビルドが通ることを確認する。

- macOS Debug / Release
- iOS デバイス Debug / Release
- iOS シミュレータ Debug / Release

iOS では SDK 条件付きリンカフラグの扱いに既知の落とし穴があるため
(`OTHER_LDFLAGS[sdk=iphoneos*]`)、4 構成を実際に回して確認する。

## 11. テスト

自動で回せるのはロジック層のみ。認証と HTTP は実機確認とする。

**自己チェック（承認なしで走る）**

1. パス検証: `..` による脱出が拒否されること
2. パス検証: ルート内のシンボリックリンク経由の脱出が拒否されること
3. パス検証: 正当なプロジェクト内パスが通ること
4. SSE パーサ: 分割された行、空行、`data:` 以外の行を正しく扱うこと

**実機確認（M1 完了条件）**

iPad 実機で、Projucer から ChatGPT アカウントにサインインし、開いている
プロジェクトについて「このクラスにローパスフィルタ用のパラメータを足して」と
指示すると、AI がソースを読んで差分を提示し、承認すると実際にファイルへ
反映される。

## 12. 既存計画との関係

`PROJUCERG_LIVE_AI_EDIT_IMPLEMENTATION_PLAN.md` は「外部の AI が MCP 経由で
Projucer の GUI Editor を操作する」設計であり、本設計とツール層が重複する。

方針: **本設計（内蔵ハーネス）を本命とする。** 既存計画のうち GUI Editor
操作のツール設計（対象ドキュメントの確認、非破壊プレビュー、AI 編集バー、
Escape による緊急停止、操作単位の Undo）は M2 で `AiTools` に取り込む。
外部 MCP サーバーとしての公開は行わない。

## 13. 今後の拡張

| 段階 | 内容 |
|---|---|
| M2 | ビルド実行ツール、GUI Editor 操作ツール |
| M3 | Grok 対応（Grok CLI がオープンソース。同手順で仕様を取得） |
| M4 | Claude 対応 |

M3 の時点で `CodexClient` の上にプロバイダ境界を切る。M1 では切らない。

## 14. 参照

- `codex-rs/login/src/device_code_auth.rs` — デバイスコードフロー
- `codex-rs/login/src/auth/manager.rs` — CLIENT_ID、トークン URL
- `codex-rs/model-provider-info/src/lib.rs` — API ベース URL
- `codex-rs/login/src/server.rs` — トークン交換のパラメータ
- `Projucer/Source/Application/jucer_MainWindow.cpp` — キーボード inset
- `Projucer/Source/Project/UI/jucer_ContentViewComponent.h` — メインエリア差し替え
- `Projucer/Source/Project/UI/jucer_HeaderComponent.{h,cpp}` — ヘッダのボタン配置
- `Projucer/Source/Project/UI/jucer_ProjectContentComponent.cpp` — レイアウト
