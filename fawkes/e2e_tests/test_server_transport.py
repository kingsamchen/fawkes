"""HTTP parser and TCP regressions against the standalone fawkes server."""

import argparse
import concurrent.futures
import http.client
import json
from pathlib import Path
import signal
import socket
import struct
import sys
import time
import unittest

from server_process import running_server


class TransportTest(unittest.TestCase):
    server_binary = None

    def assert_rejection(self, response, status, message, hook=False):
        self.assertEqual(response.status, status)
        self.assertEqual(json.loads(response.read()),
                         {"error": {"message": message}})
        self.assertTrue(response.getheader("Content-Type").startswith(
            "application/json"))
        self.assertTrue(response.will_close)
        self.assertEqual(response.getheader("X-Fawkes-Test-Rejection"),
                         "applied" if hook else None)

    def assert_body_boundary(self, server, limit, framing, size):
        body = b"x" * size
        start = ("POST /echo HTTP/1.1\r\nHost: localhost\r\n"
                 "Connection: close\r\n")
        if framing == "content-length":
            payload = f"{start}Content-Length: {size}\r\n\r\n".encode()
            if size <= limit:
                payload += body
        else:
            chunks = (body[i:i + 1024] for i in range(0, size, 1024))
            payload = f"{start}Transfer-Encoding: chunked\r\n\r\n".encode()
            payload += b"".join(
                f"{len(chunk):x}\r\n".encode() + chunk + b"\r\n"
                for chunk in chunks) + b"0\r\n\r\n"
        with server.connect() as connection:
            connection.sendall(payload)
            with http.client.HTTPResponse(connection) as response:
                response.begin()
                if size > limit:
                    self.assert_rejection(
                        response, 413, "Request body too large")
                else:
                    self.assertEqual(response.status, 200)
                    self.assertEqual(response.read(), body)
                    self.assertTrue(response.will_close)
            server.expect_closed(connection)

    # Default/custom body boundaries, both framings → echo or framework 413.
    def test_body_boundaries(self):
        for configured, limit in ((0, 1024 * 1024), (4096, 4096)):
            with running_server(self.server_binary,
                                body_limit=configured) as server:
                for framing in ("content-length", "chunked"):
                    for size in (0, limit - 1, limit, limit + 1):
                        with self.subTest(limit=limit, framing=framing,
                                          size=size):
                            self.assert_body_boundary(
                                server, limit, framing, size)
                self.assertEqual(server.ping(), (200, b"Pong!"))

    # Default/custom header boundaries → ping or framework 431.
    def test_header_boundaries(self):
        prefix = (b"GET /ping HTTP/1.1\r\nHost: localhost\r\n"
                  b"Connection: close\r\nX-Probe: ")
        for configured, limit in ((0, 8192), (256, 256), (65536, 65536)):
            with running_server(self.server_binary,
                                header_limit=configured) as server:
                for size in (limit - 1, limit, limit + 1):
                    with (self.subTest(limit=limit, size=size),
                          server.connect() as connection):
                        connection.sendall(
                            prefix + b"x" * (size - len(prefix) - 4) +
                            b"\r\n\r\n")
                        with http.client.HTTPResponse(connection) as response:
                            response.begin()
                            if size > limit:
                                self.assert_rejection(
                                    response, 431, "Request header too large")
                            else:
                                self.assertEqual(response.status, 200)
                                self.assertEqual(response.read(), b"Pong!")
                                self.assertTrue(response.will_close)
                        server.expect_closed(connection)
                self.assertEqual(server.ping(), (200, b"Pong!"))

    # The maximum 32-bit header limit → accept headers beyond 64 KiB.
    def test_unlimited_header(self):
        with (running_server(self.server_binary,
                             header_limit=(1 << 32) - 1) as server,
              server.connect() as connection):
            connection.sendall(
                b"GET /ping HTTP/1.1\r\nHost: localhost\r\n"
                b"Connection: close\r\nX-Probe-1: " + b"x" * 32768 +
                b"\r\nX-Probe-2: " + b"x" * 32768 +
                b"\r\n\r\n")
            with http.client.HTTPResponse(connection) as response:
                response.begin()
                self.assertEqual(response.status, 200)
                self.assertEqual(response.read(), b"Pong!")
            server.expect_closed(connection)

    # A disabled body limit → accept payloads above Beast's request default.
    def test_unlimited_body(self):
        with running_server(self.server_binary,
                            body_limit=(1 << 64) - 1) as server:
            for framing in ("content-length", "chunked"):
                with self.subTest(framing=framing):
                    self.assert_body_boundary(
                        server, (1 << 64) - 1, framing, 1024 * 1024 + 1)

    # Header limits in initial/header/trailer reads → 431, hook, and closure.
    def test_header_limit_rejection_hook(self):
        start = b"GET /ping HTTP/1.1\r\nHost: localhost\r\n"
        for limit, hook in ((8192, False), (8192, True),
                            (256, False), (256, True)):
            fields = b"X-Probe: " + b"x" * limit + b"\r\n\r\n"
            cases = {
                "combined": [start + fields],
                "header": [start, fields],
                "request_line": [b"GET /" + b"x" * limit +
                                 b" HTTP/1.1\r\nHost: localhost\r\n\r\n"],
                "trailers": [b"POST /echo HTTP/1.1\r\nHost: localhost\r\n"
                             b"Transfer-Encoding: chunked\r\n\r\n",
                             b"0\r\n" + fields],
            }
            with (self.subTest(limit=limit, hook=hook),
                  running_server(self.server_binary,
                                 header_limit=limit,
                                 rejection_hook=hook) as server):
                for stage, parts in cases.items():
                    with (self.subTest(stage=stage),
                          server.connect() as connection):
                        for index, part in enumerate(parts):
                            if index:
                                self.assertEqual(server.ping(), (200, b"Pong!"))
                            connection.sendall(part)
                        with http.client.HTTPResponse(connection) as response:
                            response.begin()
                            self.assert_rejection(
                                response, 431, "Request header too large", hook)
                        server.expect_closed(connection)
                self.assertEqual(server.ping(), (200, b"Pong!"))

    # An oversized HEAD header → a 431 with JSON metadata and no wire body.
    def test_header_limit_head(self):
        with (running_server(self.server_binary, rejection_hook=True) as server,
              server.connect() as connection):
            connection.sendall(
                b"HEAD /ping HTTP/1.1\r\nHost: localhost\r\nX-Probe: " +
                b"x" * 8192 + b"\r\n\r\n")
            with http.client.HTTPResponse(connection, method="HEAD") as response:
                response.begin()
                self.assertEqual(response.status, 431)
                self.assertEqual(response.read(), b"")
                self.assertEqual(response.getheader("Content-Type"),
                                 "application/json")
                self.assertEqual(response.getheader("X-Fawkes-Test-Rejection"),
                                 "applied")
                self.assertGreater(int(response.getheader("Content-Length")), 0)
                self.assertTrue(response.will_close)
            server.expect_closed(connection)

    # Invalid URL parsing before dispatch → framework 400 and optional hook.
    def test_invalid_target_rejection_hook(self):
        for hook in (False, True):
            with (
                self.subTest(hook=hook),
                running_server(self.server_binary,
                               rejection_hook=hook) as server,
                server.connect() as connection,
            ):
                connection.sendall(
                    b"GET /bad%ZZ HTTP/1.1\r\nHost: localhost\r\n"
                    b"Connection: close\r\n\r\n")
                with http.client.HTTPResponse(connection) as response:
                    response.begin()
                    self.assert_rejection(
                        response, 400, "Invalid request", hook)
                server.expect_closed(connection)

    # Parser failures in each read stage → framework 400, hook, and closure.
    def test_malformed_requests(self):
        start = b"GET /ping HTTP/1.1\r\nHost: localhost\r\n"
        cases = {
            "start_line": [b"GET /ping HTTP/9.x\r\nHost: localhost\r\n\r\n"],
            "header": [start + b"Bad Header\r\n\r\n"],
            "content_length": [start + b"Content-Length: abc\r\n\r\n"],
            "split_header": [start, b"Bad Header\r\n\r\n"],
            "chunk_size": [start + b"Transfer-Encoding: chunked\r\n\r\nzz\r\n"],
        }
        with running_server(self.server_binary, rejection_hook=True) as server:
            for name, parts in cases.items():
                with self.subTest(case=name), server.connect() as connection:
                    for index, part in enumerate(parts):
                        if index:
                            # A round trip enters the follow-up header read.
                            self.assertEqual(server.ping(), (200, b"Pong!"))
                        connection.sendall(part)
                    with http.client.HTTPResponse(connection) as response:
                        response.begin()
                        self.assert_rejection(
                            response, 400, "Invalid request", True)
                    server.expect_closed(connection)
            self.assertEqual(server.ping(), (200, b"Pong!"))

    # Limit failures in initial/header/body reads → the rejection hook runs.
    def test_body_limit_rejection_hook(self):
        start = b"POST /echo HTTP/1.1\r\nHost: localhost\r\n"
        cases = {
            "initial": [start + b"Content-Length: 4097\r\n\r\n"],
            "header": [start, b"Content-Length: 4097\r\n\r\n"],
            "body": [start + b"Transfer-Encoding: chunked\r\n\r\n",
                     b"1001\r\n" + b"x" * 4097 + b"\r\n0\r\n\r\n"],
        }
        with running_server(self.server_binary, body_limit=4096,
                            rejection_hook=True) as server:
            for stage, parts in cases.items():
                with self.subTest(stage=stage), server.connect() as connection:
                    for part_index, part in enumerate(parts):
                        if part_index:
                            self.assertEqual(server.ping(), (200, b"Pong!"))
                        connection.sendall(part)
                    with http.client.HTTPResponse(connection) as response:
                        response.begin()
                        self.assert_rejection(
                            response, 413, "Request body too large", True)
                    server.expect_closed(connection)

    # Upload in flight → default/custom delays preserve 413; zero closes now.
    def test_body_limit_rst_avoidance(self):
        upload_size = 16 * 1024 * 1024

        def upload(connection):
            try:
                for _ in range(upload_size // 65536):
                    connection.sendall(b"x" * 65536)
            except OSError as error:
                return time.monotonic(), error
            raise AssertionError("Oversized upload completed without a reset")

        for delay_ms in (500, 1000, 0):
            # Omit the flag in the first case to exercise the default delay.
            options = {}
            if delay_ms != 500:
                options["rst_avoidance_delay_ms"] = delay_ms
            with (
                self.subTest(delay_ms=delay_ms),
                concurrent.futures.ThreadPoolExecutor(
                    max_workers=1) as executor,
                running_server(self.server_binary, body_limit=4096,
                               **options) as server,
                server.connect() as connection,
            ):
                started = time.monotonic()
                connection.sendall(
                    ("POST /echo HTTP/1.1\r\nHost: localhost\r\n"
                     f"Content-Length: {upload_size}\r\n\r\n").encode())
                future = executor.submit(upload, connection)
                try:
                    with http.client.HTTPResponse(connection) as response:
                        response.begin()
                        self.assert_rejection(
                            response, 413, "Request body too large")
                except (ConnectionResetError, http.client.RemoteDisconnected):
                    if delay_ms:
                        raise
                received = time.monotonic()
                reset_at, error = future.result(timeout=6)
                self.assertIsInstance(
                    error, (BrokenPipeError, ConnectionResetError))
                if delay_ms:
                    self.assertGreaterEqual(reset_at - received,
                                            delay_ms / 1000 - .15)
                    self.assertLess(reset_at - received, delay_ms / 1000 + 1.5)
                else:
                    self.assertLess(reset_at - started, 1)
                self.assertEqual(server.ping(), (200, b"Pong!"))

    # Peers reset in the listen backlog → later connections are still served.
    def test_reset_before_accept(self):
        with running_server(self.server_binary) as server:
            server.process.send_signal(signal.SIGSTOP)
            try:
                for _ in range(3):
                    with server.connect() as connection:
                        connection.setsockopt(
                            socket.SOL_SOCKET, socket.SO_LINGER,
                            struct.pack("ii", 1, 0))
            finally:
                server.process.send_signal(signal.SIGCONT)
            self.assertEqual(server.ping(), (200, b"Pong!"))
            self.assertIsNone(server.process.poll())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    arguments, test_arguments = parser.parse_known_args()
    TransportTest.server_binary = arguments.server.resolve()
    unittest.main(argv=[sys.argv[0], *test_arguments], verbosity=2)
