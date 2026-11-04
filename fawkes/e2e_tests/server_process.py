"""Own a standalone fawkes test server and report child-process failures."""

import contextlib
import http.client
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile
import time


class ServerProcess:
    def __init__(self, process, port, log_path):
        self.process = process
        self.port = port
        self.log_path = log_path
        self.events = bytearray()
        self.stop_sent = False

    def failure(self, message):
        log = self.log_path.read_text(errors="replace")
        return AssertionError(f"{message}\nServer stderr:\n{log}")

    def wait_event(self, expected, timeout=5):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.events:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise self.failure(f"Timed out waiting for {expected!r}")
            readable, _, _ = select.select([self.process.stdout], [], [], remaining)
            if not readable:
                raise self.failure(f"Timed out waiting for {expected!r}")
            chunk = os.read(self.process.stdout.fileno(), 4096)
            if not chunk:
                raise self.failure(f"Server closed stdout before {expected!r}")
            self.events.extend(chunk)
        line, _, remainder = self.events.partition(b"\n")
        self.events = bytearray(remainder)
        try:
            event = json.loads(line)
        except ValueError as error:
            raise self.failure(f"Invalid server event: {line!r}") from error
        if not isinstance(event, dict):
            raise self.failure(f"Invalid server event: {event!r}")
        if event.get("event") != expected:
            raise self.failure(f"Expected event {expected!r}, received {event!r}")
        return event

    def connect(self):
        return socket.create_connection(("127.0.0.1", self.port), timeout=5)

    def ping(self):
        with contextlib.closing(
                http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)) as conn:
            conn.request("GET", "/ping", headers={"Connection": "close"})
            response = conn.getresponse()
            return response.status, response.read()

    def stop(self):
        if not self.stop_sent and self.process.poll() is None:
            self.process.terminate()
            self.stop_sent = True

    def check_exit(self, code):
        if code != 0:
            raise self.failure(f"Server exited with code {code}")
        log = self.log_path.read_text(errors="replace")
        for marker in ("AddressSanitizer", "LeakSanitizer",
                       "ThreadSanitizer", "runtime error:"):
            if marker in log:
                raise self.failure(f"Server reported {marker}")

    def wait_for_exit(self):
        try:
            code = self.process.wait(timeout=10)
        except subprocess.TimeoutExpired as error:
            raise self.failure("Server did not finish shutdown") from error
        self.check_exit(code)


@contextlib.contextmanager
def running_server(binary):
    # Fail on a bind collision; never connect a test to an existing service.
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]

    environment = os.environ.copy()
    for name, options in (
            ("ASAN_OPTIONS", "halt_on_error=1:abort_on_error=1:detect_leaks=1:"
                             "log_path=stderr"),
            ("LSAN_OPTIONS", "log_path=stderr"),
            ("UBSAN_OPTIONS", "halt_on_error=1:log_path=stderr"),
            ("TSAN_OPTIONS", "halt_on_error=1:log_path=stderr")):
        environment[name] = ":".join(filter(None, (environment.get(name), options)))

    with tempfile.TemporaryDirectory(prefix="fawkes-e2e-") as directory:
        log_path = Path(directory) / "server.stderr"
        with log_path.open("wb") as stderr:
            process = subprocess.Popen(
                [str(binary), f"--port={port}"],
                stdout=subprocess.PIPE, stderr=stderr, bufsize=0, env=environment)
            server = ServerProcess(process, port, log_path)
            try:
                event = server.wait_event("ready")
                if event.get("port") != port:
                    raise server.failure(f"Unexpected listening port: {event!r}")
                yield server
            finally:
                failed = sys.exc_info()[0] is not None
                server.stop()
                timed_out = False
                try:
                    code = process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    timed_out = True
                    process.kill()
                    code = process.wait(timeout=5)
                finally:
                    process.stdout.close()
                if timed_out:
                    raise server.failure("Server required SIGKILL during cleanup")
                server.check_exit(code)
                if failed:
                    print(log_path.read_text(errors="replace"), file=sys.stderr)
