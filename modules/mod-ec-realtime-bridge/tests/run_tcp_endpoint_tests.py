#!/usr/bin/env python3
"""Compile and test the exact production connector; no WoW/database mutation."""
import json
import pathlib
import socket
import subprocess
import sys
import tempfile

here = pathlib.Path(__file__).resolve().parent
results = []
with tempfile.TemporaryDirectory(prefix="ec-tcp-test-") as tmp:
    binary = pathlib.Path(tmp) / "probe"
    subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(here.parent / "src"),
                    str(here / "tcp_endpoint_probe.cpp"), "-o", str(binary)], check=True)

    def case(name, host, port, expected, enabled="1", budget="400", attempts="1"):
        run = subprocess.run([str(binary), host, str(port), budget, enabled, attempts],
                             capture_output=True, text=True, timeout=15)
        results.append({"name": name, "passed": run.returncode == expected,
                        "exitCode": run.returncode, "output": run.stdout.strip(), "stderr": run.stderr})

    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(8)
        port = listener.getsockname()[1]
        case("numeric IPv4", "127.0.0.1", port, 0)
        case("hostname and alternate address family", "localhost", port, 0)
    case("refusal and descriptor cleanup", "127.0.0.1", port, 2, attempts="64")
    case("invalid DNS name", "ec-nonexistent.invalid", port, 2)
    case("shutdown requested", "localhost", port, 2, enabled="0")
    case("zero connection budget", "localhost", port, 2, budget="0")
    with socket.socket(socket.AF_INET6) as listener:
        listener.bind(("::1", 0))
        listener.listen(4)
        case("numeric IPv6", "::1", listener.getsockname()[1], 0)
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", port))
        listener.listen(4)
        case("retry after listener returns", "localhost", port, 0)
print(json.dumps(results, indent=2))
sys.exit(0 if all(result["passed"] for result in results) else 1)
