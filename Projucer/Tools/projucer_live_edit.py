#!/usr/bin/env python3

"""Command-line client for a running Projucer live-edit session."""

import argparse
import base64
import json
import socket
import struct
import sys
import time
from pathlib import Path


MAGIC = 0xF2B49E2C
SESSION_FILE = Path.home() / "Library" / "Projucer" / "live-edit-session.json"


def receive_exact(connection: socket.socket, size: int) -> bytes:
    chunks = []
    remaining = size

    while remaining:
        chunk = connection.recv(remaining)
        if not chunk:
            raise RuntimeError("Projucer closed the live-edit connection.")
        chunks.append(chunk)
        remaining -= len(chunk)

    return b"".join(chunks)


def send_request(method: str, params: dict, document_file: Path | None = None,
                 project_file: Path | None = None) -> dict:
    if not SESSION_FILE.is_file():
        raise RuntimeError("Projucer live-edit session is not running.")

    session = json.loads(SESSION_FILE.read_text(encoding="utf-8"))
    request = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": method,
        "token": session["token"],
        "params": params,
    }
    if document_file is not None:
        request["documentFile"] = str(document_file.resolve())
    if project_file is not None:
        request["projectFile"] = str(project_file.resolve())
    payload = json.dumps(request, ensure_ascii=False).encode("utf-8")

    with socket.create_connection(("127.0.0.1", int(session["port"])), timeout=2.0) as connection:
        connection.sendall(struct.pack("<II", MAGIC, len(payload)) + payload)
        header = receive_exact(connection, 8)
        magic, response_size = struct.unpack("<II", header)
        if magic != MAGIC:
            raise RuntimeError("Projucer returned an invalid live-edit message header.")
        response = json.loads(receive_exact(connection, response_size).decode("utf-8"))

    return response


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    project_parser = subparsers.add_parser("list-components")
    project_parser.add_argument("project", type=Path, nargs="?")

    open_parser = subparsers.add_parser("open")
    open_parser.add_argument("document", type=Path)
    open_parser.add_argument("--project", type=Path)

    inspect_parser = subparsers.add_parser("inspect")
    inspect_parser.add_argument("document", type=Path)

    catalog_parser = subparsers.add_parser("component-catalog")
    catalog_parser.add_argument("document", type=Path)

    capture_parser = subparsers.add_parser("capture")
    capture_parser.add_argument("document", type=Path)
    capture_parser.add_argument("output", type=Path)

    preview_parser = subparsers.add_parser("preview-slider")
    preview_parser.add_argument("document", type=Path)
    preview_parser.add_argument("--name", default="Lowpass Filter Slider")
    preview_parser.add_argument("--member-name", default="lowpassFilterSlider")
    preview_parser.add_argument("--minimum", type=float, default=20.0)
    preview_parser.add_argument("--maximum", type=float, default=20000.0)
    preview_parser.add_argument("--interval", type=float, default=0.0)
    preview_parser.add_argument("--offset-x", type=int, default=-60)
    preview_parser.add_argument("--offset-y", type=int, default=60)
    preview_parser.add_argument("--width", type=int, default=120)
    preview_parser.add_argument("--height", type=int, default=120)

    batch_parser = subparsers.add_parser("preview-sliders")
    batch_parser.add_argument("document", type=Path)
    batch_parser.add_argument("spec", type=Path,
                              help="JSON file containing a sliders array with absolute x/y bounds")

    components_parser = subparsers.add_parser("preview-components")
    components_parser.add_argument("document", type=Path)
    components_parser.add_argument("spec", type=Path,
                                   help="JSON file containing a components array")

    for command in ("apply", "cancel", "status"):
        command_parser = subparsers.add_parser(command)
        command_parser.add_argument("document", type=Path)

    wait_parser = subparsers.add_parser("wait")
    wait_parser.add_argument("document", type=Path)
    wait_parser.add_argument("--timeout", type=float, default=300.0)
    wait_parser.add_argument("--interval", type=float, default=0.2)

    return parser


def main() -> int:
    args = build_parser().parse_args()

    document = getattr(args, "document", None)
    project = getattr(args, "project", None)

    if document is not None and not document.is_file():
        raise RuntimeError(f"Document does not exist: {document}")
    if project is not None and not project.is_file():
        raise RuntimeError(f"Project does not exist: {project}")

    if args.command == "wait":
        if args.timeout <= 0.0 or args.interval <= 0.0:
            raise ValueError("--timeout and --interval must be positive.")

        deadline = time.monotonic() + args.timeout
        while True:
            response = send_request("session.status", {}, document, project)
            result = response.get("result", {})
            if result.get("state") in {"cancelled", "applied", "failed"}:
                print(json.dumps(response, ensure_ascii=False, indent=2))
                return 1 if result.get("state") in {"cancelled", "failed"} else 0
            if time.monotonic() >= deadline:
                raise RuntimeError("Timed out waiting for the live-edit session to finish.")
            time.sleep(args.interval)

    if args.command == "capture":
        response = send_request("document.capture", {}, document, project)

        if "error" in response:
            print(json.dumps(response, ensure_ascii=False, indent=2))
            return 1

        result = response.get("result", {})
        args.output.write_bytes(base64.b64decode(result["data"], validate=True))
        print(json.dumps({key: value for key, value in result.items() if key != "data"},
                         ensure_ascii=False, indent=2))
        return 0

    if args.command == "list-components":
        method, params = "project.inspect", {}
    elif args.command == "open":
        method, params = "document.open", {}
    elif args.command == "inspect":
        method, params = "document.inspect", {}
    elif args.command == "component-catalog":
        method, params = "component.catalog", {}
    elif args.command == "preview-slider":
        method = "edit.previewSlider"
        params = {
            "name": args.name,
            "memberName": args.member_name,
            "minimum": args.minimum,
            "maximum": args.maximum,
            "interval": args.interval,
            "offsetX": args.offset_x,
            "offsetY": args.offset_y,
            "width": args.width,
            "height": args.height,
        }
    elif args.command == "preview-sliders":
        method = "edit.previewSliders"
        params = json.loads(args.spec.read_text(encoding="utf-8"))
    elif args.command == "preview-components":
        method = "edit.previewComponents"
        params = json.loads(args.spec.read_text(encoding="utf-8"))
    elif args.command == "apply":
        method, params = "edit.apply", {}
    elif args.command == "status":
        method, params = "session.status", {}
    else:
        method, params = "session.cancel", {}

    response = send_request(method, params, document, project)
    print(json.dumps(response, ensure_ascii=False, indent=2))
    return 1 if "error" in response else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
