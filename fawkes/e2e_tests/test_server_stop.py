"""HTTP and shutdown checks against the standalone fawkes test server."""

import argparse
import contextlib
import http.client
from pathlib import Path
import select
import subprocess
import sys
import unittest

from server_process import running_server


class ServerStopTest(unittest.TestCase):
    server_binary = None

    # GET/HEAD/GET then echo on one connection → no stray HEAD body bytes.
    def test_keep_alive_and_echo(self):
        with running_server(self.server_binary) as server:
            with contextlib.closing(http.client.HTTPConnection(
                    "127.0.0.1", server.port, timeout=5)) as connection:
                connection.request("GET", "/ping")
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(response.read(), b"Pong!")
                self.assertFalse(response.will_close)
                first_socket = connection.sock

                for method in ("HEAD", "GET"):
                    connection.request(method, "/ping")
                    self.assertIs(connection.sock, first_socket)
                    response = connection.getresponse()
                    self.assertEqual(response.status, 200)
                    self.assertEqual(response.getheader("Content-Length"), "5")
                    self.assertEqual(response.read(),
                                     b"" if method == "HEAD" else b"Pong!")
                    self.assertFalse(response.will_close)

                connection.request("POST", "/echo", body=b"payload",
                                   headers={"Connection": "close"})
                self.assertIs(connection.sock, first_socket)
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(response.read(), b"payload")
                self.assertTrue(response.will_close)

    # Stop with idle/incomplete reads → close without waiting for more input.
    def test_idle_and_incomplete_reads_are_cancelled(self):
        requests = {
            "idle": b"",
            "header": b"GET /ping HTTP/1.1\r\nHost: localhost\r\nX-Partial: ",
            "body": (b"POST /echo HTTP/1.1\r\nHost: localhost\r\n"
                     b"Content-Length: 10\r\n\r\nx"),
        }
        for part, request in requests.items():
            with (
                self.subTest(part=part),
                running_server(self.server_binary) as server,
                server.connect() as connection,
            ):
                if request:
                    connection.sendall(request)
                # A later request on the same IO worker acts as a barrier.
                self.assertEqual(server.ping(), (200, b"Pong!"))
                server.stop()
                server.wait_stopping()
                try:
                    closed = connection.recv(1) == b""
                except ConnectionResetError:
                    closed = True
                self.assertTrue(closed, "Incomplete request received a response")
                server.wait_for_exit()

    # Stop after a keep-alive response → cancel the next idle request read.
    def test_idle_keep_alive_connection_is_cancelled(self):
        with running_server(self.server_binary) as server:
            with contextlib.closing(http.client.HTTPConnection(
                    "127.0.0.1", server.port, timeout=5)) as connection:
                connection.request("GET", "/ping")
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(response.read(), b"Pong!")
                self.assertFalse(response.will_close)
                self.assertEqual(server.ping(), (200, b"Pong!"))
                server.stop()
                server.wait_stopping()
                self.assertEqual(connection.sock.recv(1), b"")
                server.wait_for_exit()

    def assert_request_finishes_after_stop(self, path, event, keep_alive):
        with running_server(self.server_binary) as server:
            with server.connect() as connection:
                policy = "keep-alive" if keep_alive else "close"
                connection.sendall(
                    (f"GET {path} HTTP/1.1\r\nHost: localhost\r\n"
                     f"Connection: {policy}\r\n\r\n").encode())
                server.wait_event(event)
                server.stop()
                server.wait_stopping()
                response = http.client.HTTPResponse(connection)
                response.begin()
                self.assertEqual(response.status, 200)
                self.assertEqual(response.read(), b"finished")
                self.assertTrue(response.will_close)
                self.assertEqual(connection.recv(1), b"")
                server.wait_for_exit()

    # Stop during a handler → finish its response and close the connection.
    def test_active_request_finishes_before_shutdown(self):
        self.assert_request_finishes_after_stop("/active", "handling", True)

    # Stop during an awaitable handler → wait, then send a closing reply.
    def test_awaitable_handler_finishes_before_shutdown(self):
        with (running_server(self.server_binary, hold_ms=1000) as server,
              server.connect() as connection):
            connection.sendall(b"GET /wait HTTP/1.1\r\nHost: localhost\r\n\r\n")
            server.wait_event("holding")
            self.assertEqual(server.ping(), (200, b"Pong!"))
            server.stop()
            server.wait_stopping()
            with self.assertRaises(subprocess.TimeoutExpired):
                server.process.wait(timeout=.2)
            self.assertFalse(select.select([connection], [], [], 0)[0],
                             "Stop discarded the in-flight handler")
            server.wait_event("handled")
            with http.client.HTTPResponse(connection) as response:
                response.begin()
                self.assertEqual(response.status, 200)
                self.assertEqual(response.read(), b"finished")
                self.assertTrue(response.will_close)
            server.expect_closed(connection)
            server.wait_for_exit()

    # Cancel queued behind write completion → finish safely, with no ASan UAF.
    def test_queued_stop_after_session_exit(self):
        self.assert_request_finishes_after_stop("/stop", "paused", False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    arguments, test_arguments = parser.parse_known_args()
    ServerStopTest.server_binary = arguments.server.resolve()
    unittest.main(argv=[sys.argv[0], *test_arguments], verbosity=2)
