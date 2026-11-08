"""Real TCP backpressure during response writes and graceful fawkes shutdown."""

import argparse
import http.client
from pathlib import Path
import socket
import subprocess
import sys
import time
import unittest

from server_process import running_server


class ResponseWriteTest(unittest.TestCase):
    server_binary = None

    def assert_large_write(self, shutdown):
        body = b"x" * (8 * 1024 * 1024)
        timeout_ms = 8000 if shutdown else 1500
        with (
            running_server(self.server_binary,
                           serve_timeout_ms=timeout_ms) as server,
            server.connect() as connection,
        ):
            connection.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                  64 * 1024)
            connection.sendall(
                b"GET /large HTTP/1.1\r\nHost: localhost\r\n\r\n")
            with http.client.HTTPResponse(connection) as response:
                response.begin()
                self.assertEqual(response.status, 200)
                self.assertEqual(response.length, len(body))
                self.assertFalse(response.will_close)
                prefix = response.read(1024)
                self.assertEqual(prefix, body[:1024])
                self.assertEqual(server.ping(), (200, b"Pong!"))
                if shutdown:
                    server.stop()
                    server.wait_stopping()
                    with self.assertRaises(subprocess.TimeoutExpired):
                        server.process.wait(timeout=.2)
                    connection.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                          2 * 1024 * 1024)
                    self.assertEqual(prefix + response.read(), body)
                    server.expect_closed(connection)
                    server.wait_for_exit()
                else:
                    time.sleep(1.8)
                    connection.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                          2 * 1024 * 1024)
                    try:
                        response.read()
                    except http.client.IncompleteRead as error:
                        self.assertLess(len(prefix) + len(error.partial),
                                        len(body))
                        self.assertEqual(error.partial,
                                         b"x" * len(error.partial))
                    except ConnectionResetError:
                        # A reset also represents a truncated socket write.
                        pass
                    else:
                        self.fail("Slow client received the entire response "
                                  "after timeout")
                    self.assertIsNone(server.process.poll())
                    self.assertEqual(server.ping(), (200, b"Pong!"))

    # Slow response consumer → write timeout truncates it, other requests work.
    def test_stalled_write_times_out(self):
        self.assert_large_write(False)

    # Stop during a stalled write → close the listener and drain all bytes.
    def test_stalled_write_finishes_after_stop(self):
        self.assert_large_write(True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    arguments, test_arguments = parser.parse_known_args()
    ResponseWriteTest.server_binary = arguments.server.resolve()
    unittest.main(argv=[sys.argv[0], *test_arguments], verbosity=2)
