#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn

STATUS_TEXT = {
    200: "OK",
    400: "Bad Request",
    500: "Internal Server Error",
    503: "Service Unavailable",
}


def build_chat_payload(text: str) -> dict:
    return {
        "id": "mock-chatcmpl-1",
        "object": "chat.completion",
        "choices": [
            {
                "index": 0,
                "message": {
                    "role": "assistant",
                    "content": text,
                },
            }
        ],
    }


def validate_request(
    path: str, headers, body: bytes, expected_path: str
) -> tuple[str | None, dict | None]:
    if path != expected_path:
        return f"expected path {expected_path}, got {path}", None

    content_type = headers.get("Content-Type")
    if content_type is None or "application/json" not in content_type:
        return "missing or invalid Content-Type", None

    content_length = headers.get("Content-Length")
    if content_length is None:
        return "missing Content-Length", None

    authorization = headers.get("Authorization")
    if authorization is None or not authorization.strip():
        return "missing Authorization", None

    connection = headers.get("Connection")
    if connection is None or connection.lower() != "close":
        return "Connection header must be 'close'", None

    try:
        declared_length = int(content_length)
    except ValueError:
        return "invalid Content-Length", None

    if declared_length != len(body):
        return (
            f"Content-Length mismatch: declared {declared_length}, actual {len(body)}",
            None,
        )

    try:
        payload = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        return f"body is not valid JSON: {exc}", None

    if not isinstance(payload.get("model"), str) or not payload["model"]:
        return "missing model string", None

    messages = payload.get("messages")
    if not isinstance(messages, list) or len(messages) < 2:
        return "messages must contain at least system and user", None

    first = messages[0]
    second = messages[1]
    if not isinstance(first, dict) or first.get("role") != "system":
        return "first message must be system", None
    if not isinstance(second, dict) or second.get("role") != "user":
        return "second message must be user", None

    if "content" not in first or "content" not in second:
        return "messages must contain content", None

    if "max_tokens" in payload and not isinstance(payload.get("max_tokens"), int):
        return "max_tokens must be an integer", None

    if payload.get("stream") is not False:
        return "stream must be false", None

    return None, payload


class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class MockLLMHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt: str, *args) -> None:
        return

    def do_POST(self) -> None:
        body = self._read_body()
        if body is None:
            return

        error, payload = validate_request(
            self.path,
            self.headers,
            body,
            self.server.expected_path,  # type: ignore[attr-defined]
        )
        if error is not None:
            self._send_json(400, {"error": error})
            return

        self.server.request_count += 1  # type: ignore[attr-defined]
        self.server.last_headers = dict(self.headers.items())  # type: ignore[attr-defined]
        self.server.last_path = self.path  # type: ignore[attr-defined]
        self.server.last_payload = payload  # type: ignore[attr-defined]
        self._dispatch(payload)

    def _read_body(self) -> bytes | None:
        header_value = self.headers.get("Content-Length")
        if header_value is None:
            self._send_json(400, {"error": "missing Content-Length"})
            return None

        try:
            length = int(header_value)
        except ValueError:
            self._send_json(400, {"error": "invalid Content-Length"})
            return None

        return self.rfile.read(length)

    def _dispatch(self, payload: dict) -> None:
        scenario = self.server.scenario  # type: ignore[attr-defined]
        user_text = payload["messages"][1]["content"]

        if scenario == "simple":
            self._send_json(200, build_chat_payload("hello from mock"))
        elif scenario == "echo_user":
            self._send_json(200, build_chat_payload(f"echo:{user_text}"))
        elif scenario == "fragmented":
            self._send_json(
                200,
                build_chat_payload("fragmented delivery works"),
                fragment_sizes=[1, 2, 3, 4, 1, 7, 11, 5],
            )
        elif scenario == "lowercase_length":
            self._send_json(
                200,
                build_chat_payload("lowercase header works"),
                lowercase_length=True,
            )
        elif scenario == "large":
            self._send_json(200, build_chat_payload("LARGE:" + ("x" * 65536)))
        elif scenario == "http_500":
            self._send_json(500, {"error": "mock internal error"})
        elif scenario == "retry_once":
            if self.server.request_count == 1:  # type: ignore[attr-defined]
                self._send_json(503, {"error": "temporary outage"})
            else:
                self._send_json(200, build_chat_payload("recovered after retry"))
        elif scenario == "malformed":
            packet = (
                b"HTTP/1.1 200 OK\r\n"
                b"Content-Length: 5\r\n"
                b"Connection: close\r\n"
                b"hello"
            )
            self.connection.sendall(packet)
            self.close_connection = True

        # ── additional scenarios ────────────────────────────────────
        elif scenario == "request_format_check":
            # Thorough request validation.  validate_request() already ran
            # so if we reach here the request is well-formed.  Return a
            # confirmation payload that includes what we saw.
            checks = []
            if self.command == "POST":
                checks.append("method_ok")
            if self.path == self.server.expected_path:  # type: ignore[attr-defined]
                checks.append("path_ok")
            # Check Host header present
            host_hdr = self.headers.get("Host")
            if host_hdr:
                checks.append("host_header_ok")
            auth_hdr = self.headers.get("Authorization")
            if auth_hdr:
                checks.append("authorization_ok")
            # Check Connection header
            conn_hdr = self.headers.get("Connection")
            if conn_hdr and conn_hdr.lower() == "close":
                checks.append("connection_close_ok")
            # Check model field is present
            if payload.get("model"):
                checks.append("model_ok")
            # Check messages structure (already validated above)
            checks.append("messages_ok")
            if payload.get("stream") is False:
                checks.append("stream_ok")
            if isinstance(payload.get("max_tokens"), int):
                checks.append("max_tokens_ok")
            result_text = "format_valid:" + ",".join(checks)
            self._send_json(200, build_chat_payload(result_text))

        elif scenario == "large_response":
            # 65 KB+ body: repeat a paragraph many times
            paragraph = (
                "The quick brown fox jumps over the lazy dog. "
                "Pack my box with five dozen liquor jugs. "
            )
            # ~100 bytes per paragraph, need ~660 repetitions for 65 KB
            repeated = paragraph * 700
            self._send_json(200, build_chat_payload(repeated))

        elif scenario == "non_200":
            self._send_json(
                500,
                {"error": {"message": "internal server error", "type": "server_error"}},
            )

        elif scenario == "malformed_garbage":
            # Send garbage that is not valid HTTP at all
            garbage = b"THIS IS NOT HTTP\xff\xfe\x00GARBAGE DATA\r\nNOPE\r\n"
            self.connection.sendall(garbage)
            self.close_connection = True

        elif scenario == "no_content_length":
            # Valid HTTP response but without Content-Length header.
            # Uses Connection: close to signal end of body.
            body = json.dumps(
                build_chat_payload("no content length response"),
                separators=(",", ":"),
                ensure_ascii=False,
            ).encode("utf-8")
            packet = (
                b"HTTP/1.1 200 OK\r\n"
                b"Content-Type: application/json\r\n"
                b"Connection: close\r\n"
                b"\r\n"
            ) + body
            self.connection.sendall(packet)
            self.close_connection = True

        elif scenario == "empty_body":
            # Valid HTTP 200 but with empty choices array
            payload_data = {
                "id": "mock-chatcmpl-empty",
                "object": "chat.completion",
                "choices": [],
            }
            self._send_json(200, payload_data)

        elif scenario == "lowercase_headers":
            # Response with all lowercase header names
            body = json.dumps(
                build_chat_payload("lowercase headers response"),
                separators=(",", ":"),
                ensure_ascii=False,
            ).encode("utf-8")
            packet = (
                f"HTTP/1.1 200 OK\r\n"
                f"content-type: application/json\r\n"
                f"content-length: {len(body)}\r\n"
                f"connection: close\r\n"
                f"\r\n"
            ).encode("utf-8") + body
            self.connection.sendall(packet)
            self.close_connection = True

        else:
            self._send_json(500, {"error": f"unknown scenario {scenario}"})

    def _send_json(
        self,
        status: int,
        payload: dict,
        fragment_sizes: list[int] | None = None,
        lowercase_length: bool = False,
    ) -> None:
        body = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode(
            "utf-8"
        )
        length_name = "content-length" if lowercase_length else "Content-Length"
        packet = (
            f"HTTP/1.1 {status} {STATUS_TEXT.get(status, 'Unknown')}\r\n"
            "Content-Type: application/json\r\n"
            f"{length_name}: {len(body)}\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode("utf-8") + body

        if fragment_sizes:
            start = 0
            for chunk_size in fragment_sizes:
                if start >= len(packet):
                    break
                end = min(start + chunk_size, len(packet))
                self.connection.sendall(packet[start:end])
                start = end
                time.sleep(0.01)
            if start < len(packet):
                self.connection.sendall(packet[start:])
        else:
            self.connection.sendall(packet)

        self.close_connection = True


def create_server(
    host: str = "127.0.0.1",
    port: int = 0,
    scenario: str = "simple",
    expected_path: str = "/api/v1/chat/completions",
) -> ThreadedHTTPServer:
    server = ThreadedHTTPServer((host, port), MockLLMHandler)
    server.scenario = scenario  # type: ignore[attr-defined]
    server.expected_path = expected_path  # type: ignore[attr-defined]
    server.request_count = 0  # type: ignore[attr-defined]
    server.last_headers = None  # type: ignore[attr-defined]
    server.last_path = None  # type: ignore[attr-defined]
    server.last_payload = None  # type: ignore[attr-defined]
    return server


def start_mock_server(
    host: str = "127.0.0.1",
    port: int = 0,
    scenario: str = "simple",
    expected_path: str = "/api/v1/chat/completions",
) -> tuple[ThreadedHTTPServer, threading.Thread]:
    server = create_server(
        host=host, port=port, scenario=scenario, expected_path=expected_path
    )
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the lab mock LLM server.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument(
        "--scenario",
        default="simple",
        choices=[
            "simple",
            "echo_user",
            "fragmented",
            "lowercase_length",
            "large",
            "http_500",
            "retry_once",
            "malformed",
            "request_format_check",
            "large_response",
            "non_200",
            "malformed_garbage",
            "no_content_length",
            "empty_body",
            "lowercase_headers",
        ],
    )
    parser.add_argument("--path", default="/api/v1/chat/completions")
    args = parser.parse_args()

    server = create_server(args.host, args.port, args.scenario, args.path)
    print(
        f"mock server listening on http://{args.host}:{server.server_address[1]} ({args.scenario})"
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
