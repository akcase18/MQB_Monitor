#!/usr/bin/env python3
"""Stands in for the ESP32-C3 so the phone configurator can be developed
and tested on a laptop against the desktop simulator -- no C3, no Teensy,
no Wi-Fi.

It plays both of the C3's roles:
  * the Teensy-facing end of the UART link, over a pseudo-terminal the
    simulator opens as its MQB_REMOTE_SERIAL_PORT (answering CMD_AP_ENABLE/
    CMD_AP_DISABLE the way platform/esp32/src/main.cpp does, so the
    simulator's WiFi screen works);
  * the phone-facing web server, serving platform/esp32/web/index.html
    straight from disk (edit, reload) and relaying /api/* exactly as
    platform/esp32/src/config_web.cpp does.

    python3 tools/web/dev_server.py --sim    # also launches the simulator
    open http://localhost:8080/

Without --sim it prints the pty path to pass to a simulator you start
yourself:  MQB_REMOTE_SERIAL_PORT=<path> .pio/build/simulator/program
"""

import argparse
import os
import queue
import subprocess
import threading
import tty
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGE = os.path.join(ROOT, "platform", "esp32", "web", "index.html")
SIM = os.path.join(ROOT, ".pio", "build", "simulator", "program")

# remote/protocol/inc/remote_protocol.hpp
SYNC = 0xA5
REQ_GET_STATE, RESP_STATE = 0x01, 0x02
REQ_APPLY_CONFIG, RESP_APPLY_RESULT = 0x03, 0x04
RESP_ERROR = 0x05
REQ_PREVIEW_CONFIG, REQ_REVERT_PREVIEW = 0x06, 0x07
CMD_AP_ENABLE, EVT_AP_READY = 0x10, 0x11
CMD_AP_DISABLE, EVT_AP_DISABLED = 0x12, 0x13


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def frame(msg_type, payload=b""):
    head = bytes([msg_type, len(payload) & 0xFF, len(payload) >> 8])
    crc = crc16(head + payload)
    return bytes([SYNC]) + head + payload + bytes([crc & 0xFF, crc >> 8])


class Link:
    """The UART end: a pty master, a frame parser, and one-at-a-time
    request/response like RelayLink::send_request_and_wait()."""

    def __init__(self):
        self.master, slave = os.openpty()
        tty.setraw(slave)
        self.path = os.ttyname(slave)
        self._slave = slave  # keep open so the pty survives sim restarts
        self.responses = queue.Queue()
        self.lock = threading.Lock()
        threading.Thread(target=self._read_loop, daemon=True).start()

    def _send(self, data):
        os.write(self.master, data)

    def _read_loop(self):
        buf = bytearray()
        while True:
            buf += os.read(self.master, 4096)
            while True:
                start = buf.find(SYNC)
                if start < 0:
                    buf.clear()
                    break
                del buf[:start]
                if len(buf) < 4:
                    break
                length = buf[2] | (buf[3] << 8)
                if len(buf) < 4 + length + 2:
                    break
                body = bytes(buf[1 : 4 + length])
                got = buf[4 + length] | (buf[5 + length] << 8)
                if crc16(body) != got:
                    del buf[0]  # resync, like FrameReader
                    continue
                del buf[: 6 + length]
                self._on_frame(body[0], body[3:])

    def _on_frame(self, msg_type, payload):
        if msg_type == CMD_AP_ENABLE:
            print("[link] CMD_AP_ENABLE -> EVT_AP_READY (127.0.0.1)")
            self._send(frame(EVT_AP_READY, bytes([127, 0, 0, 1])))
        elif msg_type == CMD_AP_DISABLE:
            print("[link] CMD_AP_DISABLE -> EVT_AP_DISABLED")
            self._send(frame(EVT_AP_DISABLED))
        else:
            self.responses.put((msg_type, payload))

    def request(self, msg_type, payload, expect, timeout=0.8):
        with self.lock:
            while not self.responses.empty():
                self.responses.get_nowait()
            self._send(frame(msg_type, payload))
            try:
                got_type, got = self.responses.get(timeout=timeout)
            except queue.Empty:
                return None
            return got if got_type == expect else None


def make_handler(link):
    class Handler(BaseHTTPRequestHandler):
        def _reply(self, code, body, ctype="text/plain"):
            data = body if isinstance(body, bytes) else body.encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                with open(PAGE, "rb") as f:
                    self._reply(200, f.read(), "text/html; charset=utf-8")
            elif self.path == "/api/state":
                resp = link.request(REQ_GET_STATE, b"", RESP_STATE)
                if resp is None:
                    self._reply(504, "relay timeout")
                else:
                    self._reply(200, resp.hex())
            else:
                self._reply(404, "not found")

        def do_POST(self):
            routes = {
                "/api/preview": REQ_PREVIEW_CONFIG,
                "/api/save": REQ_APPLY_CONFIG,
                "/api/revert": REQ_REVERT_PREVIEW,
            }
            if self.path not in routes:
                self._reply(404, "not found")
                return
            body = self.rfile.read(int(self.headers.get("Content-Length") or 0))
            try:
                payload = bytes.fromhex(body.decode())
            except ValueError:
                self._reply(400, "bad hex body")
                return
            resp = link.request(routes[self.path], payload, RESP_APPLY_RESULT)
            if not resp:
                self._reply(504, "relay timeout")
            else:
                self._reply(200, str(resp[0]))

        def log_message(self, fmt, *args):
            print("[http] " + fmt % args)

    return Handler


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--sim", action="store_true", help="launch the simulator build too")
    args = ap.parse_args()

    link = Link()
    print(f"[link] pty: {link.path}")

    sim = None
    if args.sim:
        if not os.path.exists(SIM):
            raise SystemExit("No simulator build -- run `pio run -e simulator` first.")
        env = dict(os.environ, MQB_REMOTE_SERIAL_PORT=link.path)
        sim = subprocess.Popen([SIM], cwd=ROOT, env=env)
        print(f"[sim] started (pid {sim.pid})")
    else:
        print(f"[sim] start it with: MQB_REMOTE_SERIAL_PORT={link.path} {SIM}")

    server = ThreadingHTTPServer(("0.0.0.0", args.port), make_handler(link))
    print(f"[http] http://localhost:{args.port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if sim is not None:
            sim.terminate()


if __name__ == "__main__":
    main()
