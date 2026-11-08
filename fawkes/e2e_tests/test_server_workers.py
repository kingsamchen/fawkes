"""Saturated independent Asio workers must leave fawkes socket IO responsive."""

import argparse
import concurrent.futures
import contextlib
import http.client
import json
from pathlib import Path
import sys
import unittest

from server_process import running_server


class WorkerSaturationTest(unittest.TestCase):
    server_binary = None

    @staticmethod
    def exchange(port, method, path, body=None, headers=None):
        with contextlib.closing(http.client.HTTPConnection(
                "127.0.0.1", port, timeout=3)) as connection:
            connection.request(method, path, body=body, headers=headers or {})
            with connection.getresponse() as response:
                return {"status": response.status, "body": response.read(),
                        "headers": response.headers,
                        "closed": response.will_close}

    # Every worker is held by a real job → GET/HEAD/413 finish before release.
    def test_saturated_workers_leave_socket_io_responsive(self):
        for workers in (1, 4):
            with (
                self.subTest(workers=workers),
                concurrent.futures.ThreadPoolExecutor(
                    max_workers=workers + 3) as executor,
                running_server(self.server_binary, worker_threads=workers,
                               body_limit=4096) as server,
            ):
                tags = [f"held-{index}" for index in range(workers)]
                held = [executor.submit(self.exchange, server.port,
                                        "POST", "/work", tag) for tag in tags]
                try:
                    arrived = [server.wait_event("worker_blocked")
                               for _ in range(workers)]
                    self.assertCountEqual([event["tag"] for event in arrived],
                                          tags)
                    probes = {
                        "GET": executor.submit(self.exchange, server.port,
                                               "GET", "/ping"),
                        "HEAD": executor.submit(self.exchange, server.port,
                                                "HEAD", "/ping"),
                        "413": executor.submit(
                            self.exchange, server.port, "POST", "/work", None,
                            {"Content-Length": "4097"}),
                    }
                    for method in ("GET", "HEAD"):
                        result = probes[method].result(timeout=2)
                        self.assertEqual(result["status"], 200)
                        self.assertEqual(result["body"],
                                         b"Pong!" if method == "GET" else b"")
                        self.assertEqual(
                            result["headers"].get("Content-Length"), "5")
                    rejected = probes["413"].result(timeout=2)
                    self.assertEqual(rejected["status"], 413)
                    self.assertEqual(json.loads(rejected["body"]), {
                        "error": {"message": "Request body too large"}})
                    self.assertTrue(rejected["closed"])
                    for future in held:
                        self.assertFalse(future.done(),
                                         "Worker completed before release")
                finally:
                    released = self.exchange(
                        server.port, "POST", "/work/release")
                self.assertEqual(released["status"], 200)
                self.assertEqual(json.loads(released["body"]),
                                 {"released": workers})
                for tag, future in zip(tags, held):
                    result = future.result(timeout=3)
                    self.assertEqual(result["status"], 200)
                    self.assertEqual(json.loads(result["body"]), {"tag": tag})
                self.assertEqual(server.ping(), (200, b"Pong!"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    arguments, test_arguments = parser.parse_known_args()
    WorkerSaturationTest.server_binary = arguments.server.resolve()
    unittest.main(argv=[sys.argv[0], *test_arguments], verbosity=2)
