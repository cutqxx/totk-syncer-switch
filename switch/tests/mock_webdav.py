#!/usr/bin/env python3
"""Tiny in-memory WebDAV server for host tests of the Switch client.

Records every request (method, path, headers) to a JSON log so the C tests
can assert on what the client actually sent (Content-Length, Expect, ...).

Usage: mock_webdav.py <port> <request-log.json>
Special paths:
  any path containing "fail500"  -> first request answers 500 (tests retries)
Auth: user "u", password "p" (anything else -> 401).
"""
import base64
import json
import sys
import threading
from email.utils import formatdate
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, unquote, urlparse

FILES = {}       # path -> (bytes, mtime)
DIRS = {"/"}
LOG = []
FAILED_ONCE = set()
LOCK = threading.Lock()
CLOCK = [1_800_000_000]


def tick():
    CLOCK[0] += 10
    return CLOCK[0]


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def _record(self, body_len=None):
        with LOCK:
            LOG.append({
                "method": self.command,
                "path": unquote(urlparse(self.path).path),
                "headers": {k.lower(): v for k, v in self.headers.items()},
                "body_len": body_len,
            })
            with open(sys.argv[2], "w") as f:
                json.dump(LOG, f)

    def _send(self, code, body=b"", ctype="text/plain"):
        self.send_response(code)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Content-Type", ctype)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _auth(self):
        h = self.headers.get("Authorization", "")
        return h == "Basic " + base64.b64encode(b"u:p").decode()

    def _read_body(self):
        if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
            data = b""
            while True:
                size = int(self.rfile.readline().strip(), 16)
                if size == 0:
                    self.rfile.readline()
                    break
                data += self.rfile.read(size)
                self.rfile.readline()
            return data
        n = int(self.headers.get("Content-Length", "0"))
        return self.rfile.read(n) if n else b""

    def _path(self):
        p = unquote(urlparse(self.path).path)
        return p.rstrip("/") or "/"

    def _gate(self):
        body = self._read_body()
        self._record(len(body))
        if not self._auth():
            self._send(401)
            return None
        p = self._path()
        if "fail500" in p and p not in FAILED_ONCE:
            FAILED_ONCE.add(p)
            self._send(500)
            return None
        return p, body

    def do_PROPFIND(self):
        g = self._gate()
        if not g:
            return
        p, _ = g
        if p in FILES:
            items = [p]
        elif p in DIRS:
            items = [p]
            if self.headers.get("Depth", "1") != "0":
                pref = p.rstrip("/") + "/"
                for f in list(FILES) + list(DIRS):
                    if f != p and f.startswith(pref) and "/" not in f[len(pref):]:
                        items.append(f)
        else:
            self._send(404)
            return
        out = ['<?xml version="1.0" encoding="utf-8"?><d:multistatus xmlns:d="DAV:">']
        for it in items:
            href = "/".join(quote(s) for s in it.split("/"))
            if it in DIRS:
                out.append(f"<d:response><d:href>{href}/</d:href><d:propstat><d:prop>"
                           "<d:resourcetype><d:collection/></d:resourcetype>"
                           "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>")
            else:
                data, mtime = FILES[it]
                out.append(f"<d:response><d:href>{href}</d:href><d:propstat><d:prop>"
                           f"<d:resourcetype/><d:getcontentlength>{len(data)}</d:getcontentlength>"
                           f"<d:getlastmodified>{formatdate(mtime, usegmt=True)}</d:getlastmodified>"
                           "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>")
        out.append("</d:multistatus>")
        self._send(207, "".join(out).encode(), "application/xml")

    def do_MKCOL(self):
        g = self._gate()
        if not g:
            return
        p, _ = g
        if p in DIRS or p in FILES:
            self._send(405)
            return
        parent = p.rsplit("/", 1)[0] or "/"
        if parent not in DIRS:
            self._send(409)
            return
        DIRS.add(p)
        self._send(201)

    def do_PUT(self):
        g = self._gate()
        if not g:
            return
        p, body = g
        parent = p.rsplit("/", 1)[0] or "/"
        if parent not in DIRS:
            self._send(409)
            return
        FILES[p] = (body, tick())
        self._send(201)

    def do_MOVE(self):
        g = self._gate()
        if not g:
            return
        p, _ = g
        dest = unquote(urlparse(self.headers.get("Destination", "")).path).rstrip("/")
        if p not in FILES:
            self._send(404)
            return
        if dest in FILES and self.headers.get("Overwrite", "T") == "F":
            self._send(412)
            return
        FILES[dest] = FILES.pop(p)
        self._send(201)

    def do_GET(self):
        g = self._gate()
        if not g:
            return
        p, _ = g
        if p not in FILES:
            self._send(404)
            return
        self._send(200, FILES[p][0], "application/octet-stream")


if __name__ == "__main__":
    srv = ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H)
    print("ready", flush=True)
    srv.serve_forever()
