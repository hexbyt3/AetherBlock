"""A file server with a bad connection, for the download resume test.

  /good     cuts every response off after CHUNK bytes; honors Range
  /stall    goes silent once, partway through the first response; honors Range
  /norange  cuts responses off and ignores Range (always answers 200 from byte 0)
  /missing  404

usage: flaky_server.py <file> <port>
"""
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CHUNK = 700 * 1024
DATA = open(sys.argv[1], "rb").read()
stalled = False


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("server: " + fmt % args + "\n")

    def do_GET(self):
        global stalled
        if self.path == "/missing":
            self.send_error(404)
            return

        start = 0
        m = re.match(r"bytes=(\d+)-$", self.headers.get("Range", ""))
        if m and self.path != "/norange":
            start = int(m.group(1))
            self.send_response(206)
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, len(DATA) - 1, len(DATA)))
        else:
            self.send_response(200)
        body = DATA[start:]
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()

        if self.path == "/stall":
            if not stalled:
                stalled = True
                self.wfile.write(body[:CHUNK])
                self.wfile.flush()
                time.sleep(45)
                return
            self.wfile.write(body)
            return

        # promise the whole body, deliver part of it, hang up
        self.wfile.write(body[:CHUNK])
        self.wfile.flush()
        self.close_connection = True


ThreadingHTTPServer(("127.0.0.1", int(sys.argv[2])), Handler).serve_forever()
