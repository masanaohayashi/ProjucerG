#!/usr/bin/env python3

"""MCP stdio adapter for a running ProjucerG live-edit bridge."""

from __future__ import annotations

import json
import socket
import struct
import sys
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable


MAGIC = 0xF2B49E2C
SESSION_FILE = Path.home() / "Library" / "Projucer" / "live-edit-session.json"
PROTOCOL_VERSION = "2025-06-18"
MAX_RESPONSE_SIZE = 16 * 1024 * 1024

SERVER_INSTRUCTIONS = (
    "Start by calling attach_to_active_gui_document to use the GUI document already visible "
    "in the user's running ProjucerG. Do not open or switch files when that succeeds. "
    "Only call list_open_projects and select_edit_target when no GUI document is active or "
    "when the user explicitly asks to switch targets. "
    "Inspect before editing and inspect again afterwards. Additions are applied immediately "
    "as undoable Projucer transactions. Delete only component IDs returned by inspection, "
    "ask for confirmation in chat, and pass userConfirmed=true only after the user agrees."
)


class ProjucerError(RuntimeError):
    """A connection or domain error returned by ProjucerG."""


def _receive_exact(connection: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size

    while remaining:
        chunk = connection.recv(remaining)
        if not chunk:
            raise ProjucerError("ProjucerG closed the live-edit connection.")
        chunks.append(chunk)
        remaining -= len(chunk)

    return b"".join(chunks)


class ProjucerClient:
    def __init__(self, session_file: Path = SESSION_FILE) -> None:
        self.session_file = session_file

    def request(
        self,
        method: str,
        params: dict[str, Any],
        document_file: Path | None = None,
        project_file: Path | None = None,
    ) -> Any:
        if not self.session_file.is_file():
            raise ProjucerError("ProjucerG live editing is not running.")

        try:
            session = json.loads(self.session_file.read_text(encoding="utf-8"))
            port = int(session["port"])
            token = str(session["token"])
        except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
            raise ProjucerError("ProjucerG session information is invalid.") from error

        request: dict[str, Any] = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": method,
            "token": token,
            "params": params,
        }
        if document_file is not None:
            request["documentFile"] = str(document_file)
        if project_file is not None:
            request["projectFile"] = str(project_file)

        payload = json.dumps(request, ensure_ascii=False).encode("utf-8")

        try:
            with socket.create_connection(("127.0.0.1", port), timeout=3.0) as connection:
                connection.sendall(struct.pack("<II", MAGIC, len(payload)) + payload)
                magic, response_size = struct.unpack("<II", _receive_exact(connection, 8))
                if magic != MAGIC:
                    raise ProjucerError("ProjucerG returned an invalid message header.")
                if response_size <= 0 or response_size > MAX_RESPONSE_SIZE:
                    raise ProjucerError("ProjucerG returned an invalid message size.")
                response = json.loads(_receive_exact(connection, response_size).decode("utf-8"))
        except (OSError, struct.error, json.JSONDecodeError) as error:
            raise ProjucerError("Could not communicate with ProjucerG.") from error

        if not isinstance(response, dict):
            raise ProjucerError("ProjucerG returned an invalid response.")
        if "error" in response:
            error = response["error"]
            if isinstance(error, dict):
                raise ProjucerError(str(error.get("message", "ProjucerG rejected the request.")))
            raise ProjucerError("ProjucerG rejected the request.")
        return response.get("result")


@dataclass(frozen=True)
class EditTarget:
    project_file: Path
    document_file: Path


def _object_schema(properties: dict[str, Any], required: list[str] | None = None) -> dict[str, Any]:
    schema: dict[str, Any] = {"type": "object", "properties": properties, "additionalProperties": False}
    if required:
        schema["required"] = required
    return schema


TARGET_ID = {"type": "string", "description": "ID returned by attach_to_active_gui_document or select_edit_target."}

TOOLS = [
    {
        "name": "attach_to_active_gui_document",
        "description": "Attach to the GUI document currently visible in the user's running ProjucerG without opening or switching any file. Use this first for normal editing.",
        "inputSchema": _object_schema({}),
        "annotations": {"readOnlyHint": True},
    },
    {
        "name": "list_open_projects",
        "description": "List GUI documents in the frontmost open ProjucerG project. Present the choices to the user; do not choose implicitly.",
        "inputSchema": _object_schema({}),
        "annotations": {"readOnlyHint": True},
    },
    {
        "name": "select_edit_target",
        "description": "Validate and open the exact project and GUI document explicitly confirmed by the user.",
        "inputSchema": _object_schema(
            {
                "projectFile": {"type": "string"},
                "documentFile": {"type": "string"},
                "userConfirmed": {"type": "boolean", "const": True},
            },
            ["projectFile", "documentFile", "userConfirmed"],
        ),
        "annotations": {"readOnlyHint": False, "destructiveHint": False},
    },
    {
        "name": "get_active_gui_document",
        "description": "Read canvas, grid, and existing top-level components for the selected target. Call before editing and after applying.",
        "inputSchema": _object_schema({"targetId": TARGET_ID}, ["targetId"]),
        "annotations": {"readOnlyHint": True},
    },
    {
        "name": "get_gui_editor_image",
        "description": "Capture the visible GUI Editor as a PNG image so the current layout or preview can be inspected visually.",
        "inputSchema": _object_schema({"targetId": TARGET_ID}, ["targetId"]),
        "annotations": {"readOnlyHint": True},
    },
    {
        "name": "list_component_types",
        "description": "List every GUI component type supported by this ProjucerG build, including default size and raw editable XML properties.",
        "inputSchema": _object_schema({"targetId": TARGET_ID}, ["targetId"]),
        "annotations": {"readOnlyHint": True},
    },
    {
        "name": "add_components",
        "description": "Immediately add supported top-level GUI components as one undoable Projucer transaction. Get valid types and properties from list_component_types first, then inspect the result.",
        "inputSchema": _object_schema(
            {
                "targetId": TARGET_ID,
                "components": {
                    "type": "array",
                    "minItems": 1,
                    "items": _object_schema(
                        {
                            "type": {"type": "string"},
                            "name": {"type": "string"},
                            "memberName": {"type": "string"},
                            "x": {"type": "integer"},
                            "y": {"type": "integer"},
                            "width": {"type": "integer", "minimum": 1},
                            "height": {"type": "integer", "minimum": 1},
                            "properties": {"type": "object", "additionalProperties": True},
                        },
                        ["type", "name", "memberName", "x", "y", "width", "height"],
                    ),
                },
            },
            ["targetId", "components"],
        ),
        "annotations": {"readOnlyHint": False, "destructiveHint": False},
    },
    {
        "name": "delete_components_by_ids",
        "description": "Immediately delete specific top-level components as one undoable Projucer transaction. Ask the user in chat first and only pass userConfirmed=true after explicit confirmation.",
        "inputSchema": _object_schema(
            {
                "targetId": TARGET_ID,
                "componentIds": {
                    "type": "array",
                    "minItems": 1,
                    "uniqueItems": True,
                    "items": {"type": "string", "minLength": 1},
                },
                "userConfirmed": {"type": "boolean", "const": True},
            },
            ["targetId", "componentIds", "userConfirmed"],
        ),
        "annotations": {"readOnlyHint": False, "destructiveHint": True},
    },
]


class McpServer:
    def __init__(self, client: ProjucerClient | None = None) -> None:
        self.client = client or ProjucerClient()
        self.targets: dict[str, EditTarget] = {}

    def _target(self, arguments: dict[str, Any]) -> EditTarget:
        target_id = arguments.get("targetId")
        if not isinstance(target_id, str) or target_id not in self.targets:
            raise ProjucerError("Unknown targetId. Ask the user to confirm a target, then call select_edit_target.")
        return self.targets[target_id]

    def call_tool(self, name: str, arguments: dict[str, Any]) -> Any:
        if name == "attach_to_active_gui_document":
            result = self.client.request("document.attachActive", {})
            if not isinstance(result, dict):
                raise ProjucerError("ProjucerG returned an invalid active GUI document.")

            project_value = result.get("projectFile")
            document_value = result.get("documentFile")
            if not isinstance(project_value, str) or not project_value:
                raise ProjucerError("The active GUI document has no Projucer project.")
            if not isinstance(document_value, str) or not document_value:
                raise ProjucerError("ProjucerG has no active GUI document.")

            project = Path(project_value).expanduser().resolve()
            document = Path(document_value).expanduser().resolve()
            target_id = uuid.uuid4().hex
            self.targets[target_id] = EditTarget(project, document)
            return {**result, "targetId": target_id}

        if name == "list_open_projects":
            return self.client.request("project.inspect", {})

        if name == "select_edit_target":
            if arguments.get("userConfirmed") is not True:
                raise ProjucerError("The edit target must be explicitly confirmed by the user.")
            project = Path(str(arguments["projectFile"])).expanduser().resolve()
            document = Path(str(arguments["documentFile"])).expanduser().resolve()
            result = self.client.request("document.open", {}, document, project)
            target_id = uuid.uuid4().hex
            self.targets[target_id] = EditTarget(project, document)
            return {**(result or {}), "targetId": target_id}

        target = self._target(arguments)
        if name == "get_active_gui_document":
            return self.client.request("document.inspect", {}, target.document_file, target.project_file)
        if name == "get_gui_editor_image":
            return self.client.request("document.capture", {}, target.document_file, target.project_file)
        if name == "list_component_types":
            return self.client.request("component.catalog", {}, target.document_file, target.project_file)
        if name == "add_components":
            return self.client.request(
                "edit.addComponents", {"components": arguments["components"]},
                target.document_file, target.project_file
            )
        if name == "delete_components_by_ids":
            if arguments.get("userConfirmed") is not True:
                raise ProjucerError("Component deletion must be explicitly confirmed by the user in chat.")
            return self.client.request(
                "edit.deleteComponents", {"componentIds": arguments["componentIds"]},
                target.document_file, target.project_file
            )
        raise ProjucerError(f"Unknown tool: {name}")

    def handle(self, message: dict[str, Any]) -> dict[str, Any] | None:
        if not isinstance(message, dict):
            return self._error(None, -32600, "Request must be a JSON object.")
        if message.get("jsonrpc") != "2.0" or not isinstance(message.get("method"), str):
            return self._error(message.get("id"), -32600, "Invalid JSON-RPC request.")

        method = message.get("method")
        request_id = message.get("id")
        if request_id is None:
            return None

        try:
            if method == "initialize":
                params = message.get("params") or {}
                if not isinstance(params, dict):
                    return self._error(request_id, -32602, "initialize params must be an object.")
                requested = params.get("protocolVersion")
                return self._result(
                    request_id,
                    {
                        "protocolVersion": requested if requested == PROTOCOL_VERSION else PROTOCOL_VERSION,
                        "capabilities": {"tools": {"listChanged": False}},
                        "serverInfo": {"name": "projucerg", "version": "0.1.0"},
                        "instructions": SERVER_INSTRUCTIONS,
                    },
                )
            if method == "ping":
                return self._result(request_id, {})
            if method == "tools/list":
                return self._result(request_id, {"tools": TOOLS})
            if method == "tools/call":
                params = message.get("params", {})
                if not isinstance(params, dict):
                    return self._error(request_id, -32602, "tools/call params must be an object.")
                arguments = params.get("arguments") or {}
                if not isinstance(arguments, dict):
                    return self._error(request_id, -32602, "tool arguments must be an object.")
                value = self.call_tool(str(params.get("name", "")), arguments)
                return self._result(request_id, self._tool_result(value))
            return self._error(request_id, -32601, f"Method not found: {method}")
        except (ProjucerError, AttributeError, KeyError, TypeError, ValueError) as error:
            if method == "tools/call":
                return self._result(request_id, self._tool_result({"error": str(error)}, is_error=True))
            return self._error(request_id, -32602, str(error))

    @staticmethod
    def _tool_result(value: Any, is_error: bool = False) -> dict[str, Any]:
        if (
            not is_error
            and isinstance(value, dict)
            and isinstance(value.get("data"), str)
            and value.get("mimeType") == "image/png"
        ):
            metadata = {key: item for key, item in value.items() if key != "data"}
            return {
                "content": [
                    {"type": "image", "data": value["data"], "mimeType": value["mimeType"]},
                    {"type": "text", "text": json.dumps(metadata, ensure_ascii=False, indent=2)},
                ],
                "structuredContent": metadata,
            }

        result = {
            "content": [{"type": "text", "text": json.dumps(value, ensure_ascii=False, indent=2)}],
            "structuredContent": value if isinstance(value, dict) else {"value": value},
        }
        if is_error:
            result["isError"] = True
        return result

    @staticmethod
    def _result(request_id: Any, result: Any) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": request_id, "result": result}

    @staticmethod
    def _error(request_id: Any, code: int, message: str) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def run(
    server: McpServer,
    input_stream: Any = sys.stdin,
    output_stream: Any = sys.stdout,
) -> None:
    for line in input_stream:
        try:
            message = json.loads(line)
            response = server.handle(message)
        except (AttributeError, json.JSONDecodeError, TypeError) as error:
            response = McpServer._error(None, -32700, f"Parse error: {error}")

        if response is not None:
            output_stream.write(json.dumps(response, ensure_ascii=False, separators=(",", ":")) + "\n")
            output_stream.flush()


if __name__ == "__main__":
    run(McpServer())
