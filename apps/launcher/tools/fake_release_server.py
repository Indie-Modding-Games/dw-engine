#!/usr/bin/env python3
"""A local stand-in for the GitHub releases API, to try the launcher's updates by hand
(docs/modulos/apps/launcher.md). Each subfolder of <releases> is a release: its files are the
release's files and notes.md (if there) its notes; the newest folder (by name) comes first. The
folder is read again on every request, so a release can be added while the launcher runs.

    fake_release_server.py <releases> [--port 8765] [--token secret] [--repo team/game]

Start the launcher with DWE_LAUNCHER_API=http://127.0.0.1:<port>. With --token, requests without
"Authorization: Bearer <token>" get 404, as a private repository answers.
"""
import argparse
import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def releases(root: Path, base: str, repo: str):
    listed = []
    for number, folder in enumerate(sorted((p for p in root.iterdir() if p.is_dir()), reverse=True)):
        files = sorted(f for f in folder.iterdir() if f.is_file() and f.name != "notes.md")
        notes = folder / "notes.md"
        assets = []
        for index, file in enumerate(files):
            asset_id = (number + 1) * 1000 + index
            assets.append({"id": asset_id, "name": file.name, "size": file.stat().st_size,
                           "url": f"{base}/repos/{repo}/releases/assets/{asset_id}", "_path": str(file)})
        listed.append({"tag_name": folder.name, "name": folder.name, "draft": False, "prerelease": True,
                       "published_at": "2026-11-04T12:00:00Z",
                       "body": notes.read_text(encoding="utf-8") if notes.exists() else "", "assets": assets})
    return listed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--token")
    parser.add_argument("--repo", default="Indie-Modding-Games/dw-engine")
    args = parser.parse_args()
    base = f"http://127.0.0.1:{args.port}"

    class Handler(BaseHTTPRequestHandler):
        def send(self, code, body=b"", kind="application/json"):
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if args.token and self.headers.get("Authorization") != f"Bearer {args.token}":
                return self.send(404, b'{"message":"Not Found"}')
            listing = releases(args.root, base, args.repo)
            path = self.path.split("?")[0]
            if path == f"/repos/{args.repo}/releases":
                public = [{**r, "assets": [{k: v for k, v in a.items() if k != "_path"} for a in r["assets"]]}
                          for r in listing]
                return self.send(200, json.dumps(public).encode())
            prefix = f"/repos/{args.repo}/releases/assets/"
            if path.startswith(prefix):
                for release in listing:
                    for asset in release["assets"]:
                        if str(asset["id"]) == path[len(prefix):]:
                            data = Path(asset["_path"]).read_bytes()
                            return self.send(200, data, "application/octet-stream")
            self.send(404, b'{"message":"Not Found"}')

        def log_message(self, fmt, *values):
            print("fake-release:", fmt % values, flush=True)

    os.chdir(args.root)
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
