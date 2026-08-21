#!/usr/bin/env python3

import json
import os
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOST = "127.0.0.1"
PORT = 38472


class InstallHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/install":
            self.send_error(404)
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            app_path = os.path.realpath(payload["appPath"])
            bundle_id = payload["bundleId"]

            if not app_path.endswith(".app") or not os.path.isdir(app_path):
                raise RuntimeError(f"app bundle not found: {app_path}")

            install = subprocess.run(
                ["xcrun", "simctl", "install", "booted", app_path],
                capture_output=True,
                text=True,
                timeout=120,
            )
            output = (install.stdout + install.stderr).strip()

            if install.returncode != 0:
                raise RuntimeError(output or f"simctl install exited with {install.returncode}")

            launch = subprocess.run(
                ["xcrun", "simctl", "launch", "booted", bundle_id],
                capture_output=True,
                text=True,
                timeout=30,
            )
            output = (output + "\n" + launch.stdout + launch.stderr).strip()

            if launch.returncode != 0:
                raise RuntimeError(output or f"simctl launch exited with {launch.returncode}")

            self._reply({"success": True, "output": output})
        except Exception as error:
            self._reply({"success": False, "output": str(error)})

    def _reply(self, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    ThreadingHTTPServer((HOST, PORT), InstallHandler).serve_forever()
