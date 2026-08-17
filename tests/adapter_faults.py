"""Real HTTP adapter calls against deterministic fault-injecting runtime API doubles."""
import http.server
import json
import pathlib
import socketserver
import subprocess
import sys
import tempfile
import threading


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, status, body=None):
        self.send_response(status)
        self.end_headers()
        if body is not None:
            self.wfile.write(json.dumps(body).encode())

    def do_GET(self):
        s = self.server
        if s.backend == "docker":
            if "/images/" in self.path:
                return self.reply(200, {})
            if self.path.startswith("/v1.44/containers/json?"):
                return self.reply(200, [{"Labels": {"runyard.attempt": "test-attempt"}}] if s.exists else [])
            return self.reply(200, {"Id": "runtime-uid", "Config": s.spec, "State": {"Status": "running" if s.starts else "created"}}) if s.exists else self.reply(404)
        if "pods?" in self.path:
            return self.reply(200, {"items": []})
        if "?" in self.path:
            return self.reply(200, {"metadata": {}, "items": [s.spec] if s.exists else []})
        return self.reply(200, s.spec) if s.exists else self.reply(404)

    def do_POST(self):
        s = self.server
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        if "/start" in self.path:
            s.starts += 1
            return self.reply(204)
        if "/stop" in self.path:
            return self.reply(204 if s.exists else 404)
        s.spec = json.loads(body)
        s.exists = True
        s.creates += 1
        if s.backend == "kubernetes":
            s.spec["metadata"]["uid"] = "runtime-uid"
            assert s.spec["spec"]["backoffLimit"] == 0
        else:
            assert s.spec["HostConfig"]["Memory"] == 512 * 1024 * 1024
            assert "/var/run/docker.sock" not in json.dumps(s.spec)
        # Persist the object but fail the response, reproducing an ambiguous create.
        self.reply(503, {})

    def do_DELETE(self):
        self.rfile.read(int(self.headers.get("Content-Length", "0")))
        self.server.exists = False
        self.reply(200 if self.server.backend == "kubernetes" else 204, {})


with tempfile.TemporaryDirectory(prefix="runyard-adapter-") as tmp:
    for backend in ("docker", "kubernetes"):
        token = pathlib.Path(tmp) / "token"
        token.write_text("test-token")
        address = str(pathlib.Path(tmp) / "docker.sock") if backend == "docker" else ("127.0.0.1", 0)
        server = (socketserver.UnixStreamServer if backend == "docker" else http.server.HTTPServer)(address, Handler)
        server.backend, server.exists, server.creates, server.starts = backend, False, 0, 0
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            target = address if backend == "docker" else "http://127.0.0.1:" + str(server.server_port)
            subprocess.run([sys.argv[1], backend, target, str(token)], check=True, timeout=30)
            assert server.creates == 1
            assert server.starts == (1 if backend == "docker" else 0)
        finally:
            server.shutdown()
            thread.join()
            server.server_close()
        print(backend + " adapter passed: lost create response, idempotent start, inventory, duplicate cleanup")
