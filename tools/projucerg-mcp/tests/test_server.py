import importlib.util
import io
import json
import sys
import unittest
from pathlib import Path


SERVER_PATH = Path(__file__).parents[1] / "server.py"
SPEC = importlib.util.spec_from_file_location("projucerg_mcp_server", SERVER_PATH)
server_module = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = server_module
SPEC.loader.exec_module(server_module)


class FakeClient:
    def __init__(self):
        self.requests = []

    def request(self, method, params, document_file=None, project_file=None):
        self.requests.append((method, params, document_file, project_file))
        if method == "project.inspect":
            return {"projectFile": "/tmp/Test.jucer", "guiDocuments": []}
        if method == "document.open":
            return {"opened": True}
        if method == "document.inspect":
            return {"components": [{"id": "1", "type": "Slider", "selected": True}]}
        if method == "session.status":
            return {"state": "cancelled", "reason": "user_escape"}
        if method == "document.capture":
            return {"mimeType": "image/png", "data": "cG5n", "width": 800, "height": 600}
        return {"ok": True}


class McpServerTests(unittest.TestCase):
    def setUp(self):
        self.client = FakeClient()
        self.server = server_module.McpServer(self.client)

    def test_initialize_exposes_instructions(self):
        response = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18"}})
        self.assertIn("select_edit_target", response["result"]["instructions"])
        self.assertEqual("projucerg", response["result"]["serverInfo"]["name"])

    def test_initialize_does_not_claim_unknown_protocol_version(self):
        response = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2099-01-01"}})
        self.assertEqual(server_module.PROTOCOL_VERSION, response["result"]["protocolVersion"])

    def test_tools_are_listed(self):
        response = self.server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        names = {tool["name"] for tool in response["result"]["tools"]}
        self.assertEqual(
            {"list_open_projects", "select_edit_target", "get_active_gui_document", "get_gui_editor_image", "list_component_types", "preview_components", "preview_sliders", "apply_live_edit", "cancel_live_edit", "get_live_edit_status"},
            names,
        )

    def test_target_must_be_confirmed(self):
        response = self.server.handle({"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "select_edit_target", "arguments": {"projectFile": "/tmp/Test.jucer", "documentFile": "/tmp/Main.cpp", "userConfirmed": False}}})
        self.assertTrue(response["result"]["isError"])
        self.assertEqual([], self.client.requests)

    def test_apply_must_be_confirmed(self):
        selected = self.server.call_tool("select_edit_target", {"projectFile": "/tmp/Test.jucer", "documentFile": "/tmp/Main.cpp", "userConfirmed": True})
        response = self.server.handle({"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "apply_live_edit", "arguments": {"targetId": selected["targetId"], "userConfirmed": False}}})
        self.assertTrue(response["result"]["isError"])
        self.assertEqual("document.open", self.client.requests[-1][0])

    def test_selected_target_is_required_for_preview(self):
        response = self.server.handle({"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "preview_sliders", "arguments": {"targetId": "missing", "sliders": [{}]}}})
        self.assertTrue(response["result"]["isError"])

    def test_status_preserves_user_escape_reason(self):
        selected = self.server.call_tool("select_edit_target", {"projectFile": "/tmp/Test.jucer", "documentFile": "/tmp/Main.cpp", "userConfirmed": True})
        result = self.server.call_tool("get_live_edit_status", {"targetId": selected["targetId"]})
        self.assertEqual({"state": "cancelled", "reason": "user_escape"}, result)

    def test_component_catalog_is_requested_from_projucer(self):
        selected = self.server.call_tool("select_edit_target", {"projectFile": "/tmp/Test.jucer", "documentFile": "/tmp/Main.cpp", "userConfirmed": True})
        self.server.call_tool("list_component_types", {"targetId": selected["targetId"]})
        self.assertEqual("component.catalog", self.client.requests[-1][0])

    def test_generic_components_are_forwarded_to_preview(self):
        selected = self.server.call_tool("select_edit_target", {"projectFile": "/tmp/Test.jucer", "documentFile": "/tmp/Main.cpp", "userConfirmed": True})
        components = [{"type": "TEXTBUTTON", "name": "Run", "memberName": "runButton", "x": 8, "y": 8, "width": 80, "height": 24}]
        self.server.call_tool("preview_components", {"targetId": selected["targetId"], "components": components})
        self.assertEqual(("edit.previewComponents", {"components": components}), self.client.requests[-1][:2])

    def test_active_gui_document_includes_selected_flag(self):
        selected = self.server.call_tool("select_edit_target", {"projectFile": "/tmp/Test.jucer", "documentFile": "/tmp/Main.cpp", "userConfirmed": True})
        response = self.server.call_tool("get_active_gui_document", {"targetId": selected["targetId"]})
        components = response["components"]
        self.assertTrue(any("selected" in component for component in components))

    def test_gui_editor_image_uses_mcp_image_content(self):
        selected = self.server.call_tool("select_edit_target", {"projectFile": "/tmp/Test.jucer", "documentFile": "/tmp/Main.cpp", "userConfirmed": True})
        response = self.server.handle({"jsonrpc": "2.0", "id": 8, "method": "tools/call", "params": {"name": "get_gui_editor_image", "arguments": {"targetId": selected["targetId"]}}})
        result = response["result"]
        self.assertEqual("image", result["content"][0]["type"])
        self.assertEqual("cG5n", result["content"][0]["data"])
        self.assertNotIn("data", result["structuredContent"])

    def test_stdio_uses_one_json_message_per_line(self):
        input_stream = io.StringIO(json.dumps({"jsonrpc": "2.0", "id": 5, "method": "ping"}) + "\n")
        output_stream = io.StringIO()
        server_module.run(self.server, input_stream, output_stream)
        self.assertEqual({}, json.loads(output_stream.getvalue())["result"])

    def test_non_object_json_does_not_stop_stdio_server(self):
        input_stream = io.StringIO("[]\n" + json.dumps({"jsonrpc": "2.0", "id": 6, "method": "ping"}) + "\n")
        output_stream = io.StringIO()
        server_module.run(self.server, input_stream, output_stream)
        responses = [json.loads(line) for line in output_stream.getvalue().splitlines()]
        self.assertEqual(-32600, responses[0]["error"]["code"])
        self.assertEqual({}, responses[1]["result"])

    def test_invalid_params_return_json_rpc_error(self):
        response = self.server.handle({"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": []})
        self.assertEqual(-32602, response["error"]["code"])


if __name__ == "__main__":
    unittest.main()
