# ProjucerG MCP server

起動中のProjucerGへ、CodexまたはClaudeから安全な構造化操作を送るためのローカルstdio MCPサーバーです。任意コード実行や任意ファイル書き込みは公開しません。

## Codexへの登録

プロジェクト用の `.codex/config.toml` または `~/.codex/config.toml` に追加します。

```toml
[mcp_servers.projucerg]
command = "python3"
args = ["/Users/ring2/Documents/src/Projucer8/tools/projucerg-mcp/server.py"]
cwd = "/Users/ring2/Documents/src/Projucer8"
startup_timeout_sec = 10
tool_timeout_sec = 300
enabled = true
required = false
default_tools_approval_mode = "writes"
```

設定後、Codexを再起動します。Codex CLI、IDE拡張、ChatGPTデスクトップは同じCodexホスト上のMCP設定を共有します。

## 必須ワークフロー

1. `list_open_projects` でGUIドキュメント候補を取得する。
2. ユーザーに `.jucer` ProjectとGUIドキュメントを確認する。
3. `select_edit_target` を `userConfirmed=true` で呼び、`targetId`を得る。このフラグは操作手順のガードであり、実際の承認境界にはCodexホストのwrite approvalも使用する。
4. `get_active_gui_document` でキャンバス、Grid、既存コンポーネントを取得する。
5. `list_component_types` で、このProjucerGが扱える全コンポーネント型・既定サイズ・属性を取得する。
6. `preview_components` で任意の対応コンポーネントをProjucerG上に半透明プレビュー表示する。Slider専用の互換ツールとして`preview_sliders`も利用できる。
7. `get_gui_editor_image` で表示中のGUI Editorを画像として確認する。
8. ユーザーが見た目を確認する。EscapeまたはCancelの場合は中断を伝え、自動再試行しない。
9. ユーザーが明示的に承認した場合だけ `apply_live_edit` を `userConfirmed=true` で呼ぶ。
10. 適用後に `get_active_gui_document` と `get_gui_editor_image` で結果を再確認する。

## テスト

```sh
python3 -m unittest discover -s tools/projucerg-mcp/tests -v
```

ProjucerGとの実接続確認は、ProjucerGを起動して対象Projectを開いた状態でCodexを再起動してから行います。
