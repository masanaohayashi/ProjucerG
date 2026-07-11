# ProjucerG MCP server

This is a local stdio MCP server for sending safe, structured operations from Codex or Claude to a running ProjucerG instance. It does not expose arbitrary code execution or arbitrary file writes.

## Registering with Codex

Add the following to your project-level `.codex/config.toml` or `~/.codex/config.toml`:

```toml
[mcp_servers.projucerg]
command = "python3"
args = ["tools/projucerg-mcp/server.py"]
cwd = "/path/to/Projucer8"
startup_timeout_sec = 10
tool_timeout_sec = 300
enabled = true
required = false
default_tools_approval_mode = "approve"
```

After updating the config, restart Codex. Codex CLI, the IDE extension, and ChatGPT Desktop share the same MCP settings on the Codex host.

## Required workflow

1. Use `attach_to_active_gui_document` to connect to the GUI document that the user already has open in ProjucerG and obtain `targetId`. This does not open files or switch windows/documents.
2. Only if there is no active GUI document, or the user explicitly asks to switch targets, call `list_open_projects` to get candidates.
3. When a target switch is needed, confirm the candidate with the user, then call `select_edit_target` with `userConfirmed=true`. This flag is a guard for the operation flow; the actual approval boundary is still enforced by Codex host write approval.
4. Use `get_active_gui_document` to read the canvas, grid, and existing components.
5. Use `list_component_types` to read every component type, default size, and property supported by this ProjucerG build.
6. Additions are applied immediately with `add_components`. The whole addition becomes one Projucer undo transaction.
7. Deletions must only target IDs returned by `get_active_gui_document`, and must be confirmed with the user in chat. After approval, call `delete_components_by_ids` with `userConfirmed=true`. The whole deletion becomes one Projucer undo transaction.
8. For both additions and deletions, do not use Projucer preview bars, translucent overlays, or Apply/Cancel flows.
9. After the operation, use `get_active_gui_document` and `get_gui_editor_image` to verify the result.

`default_tools_approval_mode = "approve"` means Codex will run tools from this MCP server without asking for per-tool approval. The deletion confirmation boundary is preserved by the MCP schema's `userConfirmed=true` and the workflow above.

## Testing

```sh
python3 -m unittest discover -s tools/projucerg-mcp/tests -v
```

For an end-to-end ProjucerG connection check, start ProjucerG, open the target project, then restart Codex and attach to the active GUI document.

## ProjucerG MCP サーバー

起動中の ProjucerG に対して、Codex または Claude から安全で構造化された操作を送るためのローカル stdio MCP サーバーです。任意コード実行や任意ファイル書き込みは公開しません。

## Codex への登録

プロジェクト単位の `.codex/config.toml` または `~/.codex/config.toml` に次の設定を追加します。

```toml
[mcp_servers.projucerg]
command = "python3"
args = ["tools/projucerg-mcp/server.py"]
cwd = "/path/to/Projucer8"
startup_timeout_sec = 10
tool_timeout_sec = 300
enabled = true
required = false
default_tools_approval_mode = "approve"
```

設定後、Codex を再起動します。Codex CLI、IDE 拡張、ChatGPT Desktop は、Codex ホスト上の同じ MCP 設定を共有します。

## 必須ワークフロー

1. `attach_to_active_gui_document` を使って、ユーザーがすでに ProjucerG 上で開いている GUI ドキュメントに接続し、`targetId` を取得します。この操作ではファイルを開いたり、ウィンドウやドキュメントを切り替えたりしません。
2. アクティブな GUI ドキュメントがない場合、またはユーザーが対象切替を明示した場合だけ、`list_open_projects` で候補を取得します。
3. 対象切替が必要な場合は、ユーザーに候補を確認してから `select_edit_target` を `userConfirmed=true` で呼びます。このフラグは操作手順のガードであり、実際の承認境界には Codex ホストの write approval も使います。
4. `get_active_gui_document` でキャンバス、Grid、既存コンポーネントを取得します。
5. `list_component_types` で、この ProjucerG が扱える全コンポーネント型・既定サイズ・属性を取得します。
6. 追加は `add_components` で即時適用します。追加全体が 1 つの Projucer Undo トランザクションになります。
7. 削除は `get_active_gui_document` が返した ID だけを対象にし、チャット内でユーザーへ確認します。承認後に限り `delete_components_by_ids` を `userConfirmed=true` で呼びます。削除全体が 1 つの Projucer Undo トランザクションになります。
8. 追加・削除とも、Projucer 上のプレビューバーや半透明表示、Apply/Cancel 操作は使いません。
9. 操作後は `get_active_gui_document` と `get_gui_editor_image` で結果を再確認します。

`default_tools_approval_mode = "approve"` は、この MCP サーバーのツールを Codex 側で都度確認せず実行する設定です。削除の確認境界は MCP スキーマの `userConfirmed=true` と上記ワークフローで維持します。

## テスト

```sh
python3 -m unittest discover -s tools/projucerg-mcp/tests -v
```

ProjucerG との実接続確認は、ProjucerG を起動して対象プロジェクトを開いた状態で Codex を再起動してから行います。
