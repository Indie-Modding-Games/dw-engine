#!/usr/bin/env python3
"""A local stand-in for the GitHub releases API, to try the launcher's updates by hand
(docs/modulos/apps/launcher.md). Each subfolder of <releases> is a release: its files are the
release's files and notes.md (if there) its notes; the newest folder (by name) comes first. The
folder is read again on every request, so a release can be added while the launcher runs.

    fake_release_server.py <releases> [--port 8765] [--token secret] [--repo team/game]

Start the launcher with DWE_LAUNCHER_API=http://127.0.0.1:<port>, or run the beta gateway
(services/beta-gateway) with GITHUB_API=http://127.0.0.1:<port> in front of it. With --token,
requests without "Authorization: Bearer <token>" get 404, as a private repository answers.

Like GitHub, a file asked for as application/octet-stream is a 302 to a signed URL on another
host (http://localhost:<port>/_objects/<id>), which refuses any request that carries an
Authorization header (as GitHub's storage does); without octet-stream it is the file's
description. /releases/tags/<tag> answers one release.
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
    args.root = args.root.resolve()
    base = f"http://127.0.0.1:{args.port}"

    class Handler(BaseHTTPRequestHandler):
        def send(self, code, body=b"", kind="application/json"):
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def find(self, listing, asset_id):
            for release in listing:
                for asset in release["assets"]:
                    if str(asset["id"]) == asset_id:
                        return asset
            return None

        def do_GET(self):
            listing = releases(args.root, base, args.repo)
            path = self.path.split("?")[0]
            if path.startswith("/_objects/"):
                if self.headers.get("Authorization"):
                    return self.send(400, b'{"message":"Only one auth mechanism allowed"}')
                asset = self.find(listing, path[len("/_objects/"):])
                if asset is None:
                    return self.send(404, b'{"message":"Not Found"}')
                return self.send(200, Path(asset["_path"]).read_bytes(), "application/octet-stream")
            if args.token and self.headers.get("Authorization") != f"Bearer {args.token}":
                return self.send(404, b'{"message":"Not Found"}')
            public = [{**r, "assets": [{k: v for k, v in a.items() if k != "_path"} for a in r["assets"]]}
                      for r in listing]
            if path == f"/repos/{args.repo}/releases":
                return self.send(200, json.dumps(public).encode())
            tags = f"/repos/{args.repo}/releases/tags/"
            if path.startswith(tags):
                for release in public:
                    if release["tag_name"] == path[len(tags):]:
                        return self.send(200, json.dumps(release).encode())
            prefix = f"/repos/{args.repo}/releases/assets/"
            if path.startswith(prefix):
                asset = self.find(listing, path[len(prefix):])
                if asset is not None:
                    if "application/octet-stream" in self.headers.get("Accept", ""):
                        self.send_response(302)
                        self.send_header("Location", f"http://localhost:{args.port}/_objects/{asset['id']}?sig=fake")
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                        return
                    return self.send(200, json.dumps({k: v for k, v in asset.items() if k != "_path"}).encode())
            self.send(404, b'{"message":"Not Found"}')

        def log_message(self, fmt, *values):
            print("fake-release:", fmt % values, flush=True)

    os.chdir(args.root)
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
