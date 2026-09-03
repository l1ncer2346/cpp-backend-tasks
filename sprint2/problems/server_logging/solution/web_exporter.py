#!/usr/bin/env python3
"""Expose JSON-lines game-server logs as Prometheus metrics.

The exporter uses only the Python standard library. Feed it a file or stdin;
with ``--follow`` it can be used as ``tail -F server.log | web_exporter.py``.
"""

from __future__ import annotations

import argparse
import json
import sys
import threading
import time
from collections import defaultdict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, TextIO


def _escape_label(value: object) -> str:
    return str(value).replace("\\", "\\\\").replace("\n", "\\n").replace('"', '\\"')


def _labels(values: dict[str, object]) -> str:
    if not values:
        return ""
    return "{" + ",".join(f'{key}="{_escape_label(values[key])}"' for key in sorted(values)) + "}"


class Metrics:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.requests: defaultdict[tuple[str], int] = defaultdict(int)
        self.responses: defaultdict[tuple[str, str], int] = defaultdict(int)
        self.errors: defaultdict[tuple[str, str], int] = defaultdict(int)
        self.latency_sum = 0.0
        self.latency_count = 0
        self.latency_buckets: defaultdict[float, int] = defaultdict(int)
        self.latency_limits = (1, 2, 5, 10, 25, 50, 100, 250, 500, 1000)

    def request(self, method: str) -> None:
        with self._lock:
            self.requests[(method,)] += 1

    def response(self, code: object, content_type: object, response_time: object) -> None:
        with self._lock:
            self.responses[(str(code), content_type if isinstance(content_type, str) else "none")] += 1
            if isinstance(response_time, (int, float)):
                value = float(response_time)
                self.latency_sum += value
                self.latency_count += 1
                for limit in self.latency_limits:
                    if value <= limit:
                        self.latency_buckets[limit] += 1

    def error(self, where: str, code: object) -> None:
        with self._lock:
            self.errors[(where, str(code))] += 1

    def render(self) -> str:
        with self._lock:
            lines = [
                "# HELP game_server_requests_total HTTP requests received by the game server",
                "# TYPE game_server_requests_total counter",
            ]
            for (method,), value in sorted(self.requests.items()):
                lines.append(f"game_server_requests_total{_labels({'method': method})} {value}")

            lines += [
                "# HELP game_server_responses_total HTTP responses sent by the game server",
                "# TYPE game_server_responses_total counter",
            ]
            for (code, content_type), value in sorted(self.responses.items()):
                lines.append(
                    f"game_server_responses_total{_labels({'code': code, 'content_type': content_type})} {value}"
                )

            lines += [
                "# HELP game_server_network_errors_total Network errors reported by the game server",
                "# TYPE game_server_network_errors_total counter",
            ]
            for (where, code), value in sorted(self.errors.items()):
                lines.append(f"game_server_network_errors_total{_labels({'code': code, 'where': where})} {value}")

            lines += [
                "# HELP game_server_response_time_milliseconds Time spent forming HTTP responses",
                "# TYPE game_server_response_time_milliseconds histogram",
            ]
            for limit in self.latency_limits:
                lines.append(
                    f"game_server_response_time_milliseconds_bucket{{le=\"{limit}\"}} "
                    f"{self.latency_buckets[limit]}"
                )
            lines.append('game_server_response_time_milliseconds_bucket{le="+Inf"} ' + str(self.latency_count))
            lines.append(f"game_server_response_time_milliseconds_sum {self.latency_sum}")
            lines.append(f"game_server_response_time_milliseconds_count {self.latency_count}")
            return "\n".join(lines) + "\n"


METRICS = Metrics()


def process_record(record: dict[str, Any]) -> None:
    message = record.get("message")
    data = record.get("data")
    if not isinstance(data, dict):
        return
    if message == "request received" and isinstance(data.get("method"), str):
        METRICS.request(data["method"])
    elif message == "response sent":
        METRICS.response(data.get("code"), data.get("content_type"), data.get("response_time"))
    elif message == "error" and isinstance(data.get("where"), str) and data.get("code") is not None:
        METRICS.error(data["where"], data["code"])


def consume(stream: TextIO, follow: bool) -> None:
    while True:
        line = stream.readline()
        if not line:
            if follow:
                time.sleep(0.2)
                continue
            return
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(record, dict):
            process_record(record)


class MetricsHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802 - required by BaseHTTPRequestHandler
        if self.path != "/metrics":
            self.send_error(404)
            return
        payload = METRICS.render().encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; version=0.0.4; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_args: object) -> None:
        return


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=9100, help="Prometheus listener port")
    parser.add_argument("--log-file", type=Path, help="JSON-lines file; stdin is used by default")
    parser.add_argument("--follow", action="store_true", help="wait for more lines after EOF")
    args = parser.parse_args()

    httpd = ThreadingHTTPServer(("0.0.0.0", args.port), MetricsHandler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    if args.log_file is None:
        consume(sys.stdin, args.follow)
    else:
        with args.log_file.open(encoding="utf-8") as stream:
            consume(stream, args.follow)


if __name__ == "__main__":
    main()
