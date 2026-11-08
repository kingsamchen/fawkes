"""Request/context isolation across real keep-alive fawkes connections."""

import argparse
import concurrent.futures
import contextlib
import http.client
import json
from pathlib import Path
import sys
import unittest

from server_process import running_server


class RequestContextTest(unittest.TestCase):
    server_binary = None

    @staticmethod
    def exchange(connection, path, body=None, headers=None):
        connection.request("POST", path, body=body, headers=headers or {})
        with connection.getresponse() as response:
            data = response.read()
            return {"status": response.status, "body": json.loads(data),
                    "headers": response.headers, "closed": response.will_close,
                    "length": len(data), "socket": connection.sock}

    def assert_response(self, result, original_socket, marker, request_id,
                        body, mode):
        self.assertIs(result["socket"], original_socket)
        self.assertFalse(result["closed"])
        self.assertEqual(result["headers"].get("Content-Type"),
                         "application/json")
        self.assertEqual(result["headers"].get("Content-Length"),
                         str(result["length"]))
        self.assertEqual(result["headers"].get("X-Test-Header-Marker"), marker)
        self.assertEqual(result["headers"].get("X-Test-Header-Request-Id"),
                         request_id)
        self.assertEqual(result["headers"].get("X-Test-Has-Context"),
                         "false" if marker is None else "true")
        self.assertEqual(result["headers"].get("X-Test-Context-Marker"), marker)
        context_id = request_id if marker is not None else None
        self.assertEqual(result["headers"].get("X-Test-Context-Request-Id"),
                         context_id)
        self.assertEqual(result["headers"].get("X-Test-Handler"),
                         None if mode == "abort" else "executed")
        if mode in ("error", "exception"):
            status = 422
            message = "Context probe failure"
            if mode == "exception":
                status = 500
                message = "Unexpected server error"
            self.assertEqual(result["status"], status)
            self.assertEqual(result["body"], {
                "error": {"message": message}})
        else:
            context = None
            if marker is not None:
                context = {"marker": marker, "request_id": request_id or ""}
            self.assertEqual(result["status"], 400 if mode == "abort" else 200)
            self.assertEqual(result["body"], {
                "marker": marker or "", "request_id": request_id or "",
                "body": body, "context": context})

    # Reused/concurrent requests, including 500 → no stale request state.
    def test_keep_alive_and_concurrent_context_isolation(self):
        sequence = (
            ("alpha", "ok", True),
            ("bravo", "ok", True),
            ("alpha", "abort", True),
            (None, "ok", False),
            ("bravo", "ok", True),
            ("alpha", "error", True),
            ("bravo", "exception", True),
            (None, "ok", False),
            (None, "exception", False),
            ("alpha", "ok", True),
            (None, "abort", False),
            (None, "error", False),
            ("bravo", "ok", False),
            (None, "ok", True),
            ("alpha", "ok", True),
        )
        for lanes in (1, 4):
            with (
                self.subTest(lanes=lanes),
                concurrent.futures.ThreadPoolExecutor(
                    max_workers=lanes) as executor,
                running_server(self.server_binary) as server,
                contextlib.ExitStack() as stack,
            ):
                connections = []
                for _ in range(lanes):
                    connection = stack.enter_context(contextlib.closing(
                        http.client.HTTPConnection(
                            "127.0.0.1", server.port, timeout=5)))
                    connection.connect()
                    connections.append(connection)
                sockets = [connection.sock for connection in connections]
                release = stack.enter_context(contextlib.closing(
                    http.client.HTTPConnection(
                        "127.0.0.1", server.port, timeout=5)))
                for iteration in range(2):
                    for step, (actor, mode, with_id) in enumerate(sequence):
                        pending = []
                        for lane, connection in enumerate(connections):
                            tag = f"lane-{lane}-round-{iteration}-step-{step}"
                            marker = None
                            if actor is not None:
                                marker = f"{actor}-{lane}"
                            request_id = tag if with_id else None
                            body = f"body-{tag}"
                            headers = {"X-Test-Mode": mode}
                            if marker is not None:
                                headers["X-Test-Marker"] = marker
                            if request_id is not None:
                                headers["X-Test-Request-Id"] = request_id
                            if mode == "exception":
                                server.expect_request_error(
                                    f"Context probe internal failure: {body}")
                            future = executor.submit(
                                self.exchange, connection, "/context",
                                body, headers)
                            pending.append((future, marker, request_id, body))
                        arrived = [server.wait_event("context_waiting")
                                   for _ in range(lanes)]
                        self.assertCountEqual(
                            [event["body"] for event in arrived],
                            [item[3] for item in pending])
                        for future, _, _, body in pending:
                            self.assertFalse(
                                future.done(),
                                f"Request escaped the gate: {body}")
                        released = self.exchange(release, "/context/release")
                        self.assertEqual(released["status"], 200)
                        self.assertEqual(released["body"], {"released": lanes})
                        for lane, (future, marker, request_id, body) in (
                                enumerate(pending)):
                            result = future.result(timeout=5)
                            self.assert_response(result, sockets[lane], marker,
                                                 request_id, body, mode)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    arguments, test_arguments = parser.parse_known_args()
    RequestContextTest.server_binary = arguments.server.resolve()
    unittest.main(argv=[sys.argv[0], *test_arguments], verbosity=2)
