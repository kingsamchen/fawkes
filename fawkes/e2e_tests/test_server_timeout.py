"""Socket deadlines and asynchronous handler lifetime in standalone fawkes."""

import argparse
import http.client
from pathlib import Path
import select
import sys
import time
import unittest

from server_process import running_server


class TimeoutTest(unittest.TestCase):
    server_binary = None

    # No first request or no next keep-alive request → idle timeout closes it.
    def test_idle_deadlines(self):
        for keep_alive in (False, True):
            with (self.subTest(keep_alive=keep_alive),
                  running_server(self.server_binary,
                                 idle_timeout_ms=1000) as server,
                  server.connect() as connection):
                if keep_alive:
                    connection.sendall(
                        b"GET /ping HTTP/1.1\r\nHost: localhost\r\n\r\n")
                    with http.client.HTTPResponse(connection) as response:
                        response.begin()
                        self.assertEqual(response.status, 200)
                        self.assertEqual(response.read(), b"Pong!")
                        self.assertFalse(response.will_close)
                started = time.monotonic()
                server.expect_closed(connection)
                elapsed = time.monotonic() - started
                self.assertGreaterEqual(elapsed, .6)
                self.assertLess(elapsed, 2.5)
                self.assertEqual(server.ping(), (200, b"Pong!"))

    def assert_read_deadline(self, part, read_ms, serve_ms, trickle):
        payloads = {
            "header": b"GET /ping HTTP/1.1\r\nHost: localhost\r\nX-Partial: ",
            "body": (b"POST /echo HTTP/1.1\r\nHost: localhost\r\n"
                     b"Content-Length: 100\r\n\r\nx"),
        }
        with (running_server(self.server_binary, idle_timeout_ms=5000,
                             read_timeout_ms=read_ms,
                             serve_timeout_ms=serve_ms) as server,
              server.connect() as connection):
            started = time.monotonic()
            connection.sendall(payloads[part])
            if trickle:
                for _ in range(3):
                    time.sleep(.5)
                    connection.sendall(b"x")
            server.expect_closed(connection)
            elapsed = time.monotonic() - started
            expected = min(read_ms, serve_ms) / 1000
            self.assertGreaterEqual(elapsed, expected - .4)
            self.assertLess(elapsed, expected + 1)
            self.assertEqual(server.ping(), (200, b"Pong!"))

    # Header/body bytes keep arriving → the absolute read deadline still wins.
    def test_slow_reads_do_not_extend_deadline(self):
        for part in ("header", "body"):
            with self.subTest(part=part):
                self.assert_read_deadline(part, 2000, 5000, True)

    # Serve timeout shorter than read timeout → it bounds header and body reads.
    def test_serve_deadline_bounds_reads(self):
        for part in ("header", "body"):
            with self.subTest(part=part):
                self.assert_read_deadline(part, 5000, 1000, False)

    # Handler held beyond serve timeout → it stays pending while other IO works.
    def test_serve_deadline_does_not_interrupt_handler(self):
        with (running_server(self.server_binary, serve_timeout_ms=1000,
                             hold_ms=2200) as server,
              server.connect() as connection):
            connection.sendall(
                b"GET /wait HTTP/1.1\r\nHost: localhost\r\n"
                b"Connection: close\r\n\r\n")
            server.wait_event("holding")
            self.assertEqual(server.ping(), (200, b"Pong!"))
            self.assertFalse(select.select([connection], [], [], 1.3)[0],
                             "Request completed before the handler finished")
            self.assertIsNone(server.process.poll())
            server.wait_event("handled")
            # The deadline applies when socket IO resumes, not to handler work.
            try:
                with http.client.HTTPResponse(connection) as response:
                    response.begin()
                    self.assertEqual(response.status, 200)
                    self.assertEqual(response.read(), b"finished")
            except (http.client.RemoteDisconnected, ConnectionResetError):
                # Expired socket IO may close instead of writing a response.
                pass
            server.expect_closed(connection)
            self.assertEqual(server.ping(), (200, b"Pong!"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    arguments, test_arguments = parser.parse_known_args()
    TimeoutTest.server_binary = arguments.server.resolve()
    unittest.main(argv=[sys.argv[0], *test_arguments], verbosity=2)
